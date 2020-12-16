/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Google, Inc.
 */

#define _GNU_SOURCE  /* for strcasestr */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-plugin.h"
#include "mm-device.h"
#include "mm-kernel-device.h"
#include "mm-kernel-device-generic.h"
#include "mm-port-serial-at.h"
#include "mm-port-serial-qcdm.h"
#include "mm-serial-parsers.h"
#include "mm-private-boxed-types.h"
#include "mm-log-object.h"
#include "mm-daemon-enums-types.h"

#if defined WITH_QMI
# include "mm-broadband-modem-qmi.h"
#endif
#if defined WITH_MBIM
# include "mm-broadband-modem-mbim.h"
#endif

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMPlugin, mm_plugin, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

/* Virtual port corresponding to the embedded modem */
static const gchar *virtual_port[] = {"smd0", NULL};

#define HAS_POST_PROBING_FILTERS(self)          \
    (self->priv->vendor_strings ||              \
     self->priv->product_strings ||             \
     self->priv->forbidden_product_strings ||   \
     self->priv->allowed_icera ||               \
     self->priv->forbidden_icera ||             \
     self->priv->allowed_xmm ||                 \
     self->priv->forbidden_xmm ||               \
     self->priv->custom_init)


struct _MMPluginPrivate {
    gchar      *name;
    GHashTable *tasks;
    gboolean    is_generic;

    /* Pre-probing filters */
    gchar **subsystems;
    gchar **drivers;
    gchar **forbidden_drivers;
    guint16 *vendor_ids;
    mm_uint16_pair *product_ids;
    mm_uint16_pair *forbidden_product_ids;
    gchar **udev_tags;

    /* Post probing filters */
    gchar **vendor_strings;
    mm_str_pair *product_strings;
    mm_str_pair *forbidden_product_strings;
    gboolean allowed_icera;
    gboolean forbidden_icera;
    gboolean allowed_xmm;
    gboolean forbidden_xmm;

    /* Probing setup */
    gboolean at;
    gboolean single_at;
    gboolean qcdm;
    gboolean qmi;
    gboolean mbim;
    gboolean icera_probe;
    gboolean xmm_probe;
    MMPortProbeAtCommand *custom_at_probe;
    guint64 send_delay;
    gboolean remove_echo;
    gboolean send_lf;

    /* Probing setup and/or post-probing filter.
     * Plugins may use this method to decide whether they support a given
     * port or not, so should also be considered kind of post-probing filter. */
    MMAsyncMethod *custom_init;
};

enum {
    PROP_0,
    PROP_NAME,
    PROP_IS_GENERIC,
    PROP_ALLOWED_SUBSYSTEMS,
    PROP_ALLOWED_DRIVERS,
    PROP_FORBIDDEN_DRIVERS,
    PROP_ALLOWED_VENDOR_IDS,
    PROP_ALLOWED_PRODUCT_IDS,
    PROP_FORBIDDEN_PRODUCT_IDS,
    PROP_ALLOWED_VENDOR_STRINGS,
    PROP_ALLOWED_PRODUCT_STRINGS,
    PROP_FORBIDDEN_PRODUCT_STRINGS,
    PROP_ALLOWED_UDEV_TAGS,
    PROP_ALLOWED_AT,
    PROP_ALLOWED_SINGLE_AT,
    PROP_ALLOWED_QCDM,
    PROP_ALLOWED_QMI,
    PROP_ALLOWED_MBIM,
    PROP_ICERA_PROBE,
    PROP_ALLOWED_ICERA,
    PROP_FORBIDDEN_ICERA,
    PROP_XMM_PROBE,
    PROP_ALLOWED_XMM,
    PROP_FORBIDDEN_XMM,
    PROP_CUSTOM_AT_PROBE,
    PROP_CUSTOM_INIT,
    PROP_SEND_DELAY,
    PROP_REMOVE_ECHO,
    PROP_SEND_LF,
    LAST_PROP
};

/*****************************************************************************/

const gchar *
mm_plugin_get_name (MMPlugin *self)
{
    return self->priv->name;
}

const gchar **
mm_plugin_get_allowed_subsystems (MMPlugin *self)
{
    return (const gchar **) self->priv->subsystems;
}

const gchar **
mm_plugin_get_allowed_udev_tags (MMPlugin *self)
{
    return (const gchar **) self->priv->udev_tags;
}

const guint16 *
mm_plugin_get_allowed_vendor_ids (MMPlugin *self)
{
    return self->priv->vendor_ids;
}

const mm_uint16_pair *
mm_plugin_get_allowed_product_ids (MMPlugin *self)
{
    return self->priv->product_ids;
}

gboolean
mm_plugin_is_generic (MMPlugin *self)
{
    return self->priv->is_generic;
}

/*****************************************************************************/

static gboolean
device_file_exists (const char *name)
{
    char *devfile;
    struct stat s;
    int result;

    devfile = g_strdup_printf ("/dev/%s", name);
    result = stat (devfile, &s);
    g_free (devfile);

    return (0 == result) ? TRUE : FALSE;
}

static gboolean
is_virtual_port (const gchar *device_name)
{
    guint idx;

    /* Detect any modems accessible through the list of virtual ports */
    for (idx = 0; virtual_port[idx]; idx++)  {
        if (strcmp (device_name, virtual_port[idx]))
            continue;
        if (!device_file_exists (virtual_port[idx]))
            continue;

        return TRUE;
    }

    return FALSE;
}

/* Returns TRUE if the support check request was filtered out */
static gboolean
apply_subsystem_filter (MMPlugin       *self,
                        MMKernelDevice *port)
{
    if (self->priv->subsystems) {
        const gchar *subsys;
        guint i;

        subsys = mm_kernel_device_get_subsystem (port);
        for (i = 0; self->priv->subsystems[i]; i++) {
            if (g_str_equal (subsys, self->priv->subsystems[i]))
                break;
        }

        /* If we didn't match any subsystem: unsupported */
        if (!self->priv->subsystems[i])
            return TRUE;
    }

    return FALSE;
}

/* Returns TRUE if the support check request was filtered out */
static gboolean
apply_pre_probing_filters (MMPlugin       *self,
                           MMDevice       *device,
                           MMKernelDevice *port,
                           gboolean       *need_vendor_probing,
                           gboolean       *need_product_probing)
{
    guint16 vendor;
    guint16 product;
    gboolean product_filtered = FALSE;
    gboolean vendor_filtered = FALSE;
    guint i;

    *need_vendor_probing = FALSE;
    *need_product_probing = FALSE;

    /* The plugin may specify that only some subsystems are supported. If that
     * is the case, filter by subsystem */
    if (apply_subsystem_filter (self, port)) {
        mm_obj_dbg (self, "port %s filtered by subsystem", mm_kernel_device_get_name (port));
        return TRUE;
    }

    /* The plugin may specify that only some drivers are supported, or that some
     * drivers are not supported. If that is the case, filter by driver.
     *
     * The QMI and MBIM *forbidden* drivers filter is implicit. This is, if the
     * plugin doesn't explicitly specify that QMI is allowed and we find a QMI
     * port, the plugin will filter the device. Same for MBIM.
     *
     * The opposite, though, is not applicable. If the plugin specifies that QMI
     * is allowed, we won't take that as a mandatory requirement to look for the
     * QMI driver (as the plugin may handle non-QMI modems as well)
     */
    if (self->priv->drivers ||
        self->priv->forbidden_drivers ||
        !self->priv->qmi ||
        !self->priv->mbim) {
        static const gchar *virtual_drivers [] = { "virtual", NULL };
        const gchar **drivers;

        /* Detect any modems accessible through the list of virtual ports */
        drivers = (is_virtual_port (mm_kernel_device_get_name (port)) ?
                   virtual_drivers :
                   mm_device_get_drivers (device));

        /* If error retrieving driver: unsupported */
        if (!drivers) {
            mm_obj_dbg (self, "port %s filtered as couldn't retrieve drivers", mm_kernel_device_get_name (port));
            return TRUE;
        }

        /* Filtering by allowed drivers */
        if (self->priv->drivers) {
            gboolean found = FALSE;

            for (i = 0; self->priv->drivers[i] && !found; i++) {
                guint j;

                for (j = 0; drivers[j] && !found; j++) {
                    if (g_str_equal (drivers[j], self->priv->drivers[i]))
                        found = TRUE;
                }
            }

            /* If we didn't match any driver: unsupported */
            if (!found) {
                mm_obj_dbg (self, "port %s filtered by drivers", mm_kernel_device_get_name (port));
                return TRUE;
            }
        }

        /* Filtering by forbidden drivers */
        if (self->priv->forbidden_drivers) {
            for (i = 0; self->priv->forbidden_drivers[i]; i++) {
                guint j;

                for (j = 0; drivers[j]; j++) {
                    /* If we match a forbidden driver: unsupported */
                    if (g_str_equal (drivers[j], self->priv->forbidden_drivers[i])) {
                        mm_obj_dbg (self, "port %s filtered by forbidden drivers", mm_kernel_device_get_name (port));
                        return TRUE;
                    }
                }
            }
        }

        /* Implicit filter for forbidden QMI driver */
        if (!self->priv->qmi) {
            guint j;

            for (j = 0; drivers[j]; j++) {
                /* If we match the QMI driver: unsupported */
                if (g_str_equal (drivers[j], "qmi_wwan")) {
                    mm_obj_dbg (self, "port %s filtered by implicit QMI driver", mm_kernel_device_get_name (port));
                    return TRUE;
                }
            }
        }

        /* Implicit filter for forbidden MBIM driver */
        if (!self->priv->mbim) {
            guint j;

            for (j = 0; drivers[j]; j++) {
                /* If we match the MBIM driver: unsupported */
                if (g_str_equal (drivers[j], "cdc_mbim")) {
                    mm_obj_dbg (self, "port %s filtered by implicit MBIM driver", mm_kernel_device_get_name (port));
                    return TRUE;
                }
            }
        }
    }

    vendor = mm_device_get_vendor (device);
    product = mm_device_get_product (device);

    /* The plugin may specify that only some vendor IDs are supported. If that
     * is the case, filter by vendor ID. */
    if (self->priv->vendor_ids) {
        /* If we didn't get any vendor: filtered */
        if (!vendor)
            vendor_filtered = TRUE;
        else {
            for (i = 0; self->priv->vendor_ids[i]; i++)
                if (vendor == self->priv->vendor_ids[i])
                    break;

            /* If we didn't match any vendor: filtered */
            if (!self->priv->vendor_ids[i])
                vendor_filtered = TRUE;
        }
    }

    /* The plugin may specify that only some product IDs are supported. If
     * that is the case, filter by vendor+product ID pair */
    if (self->priv->product_ids) {
        /* If we didn't get any product: filtered */
        if (!product || !vendor)
            product_filtered = TRUE;
        else {
            for (i = 0; self->priv->product_ids[i].l; i++)
                if (vendor == self->priv->product_ids[i].l &&
                    product == self->priv->product_ids[i].r)
                    break;

            /* If we didn't match any product: filtered */
            if (!self->priv->product_ids[i].l)
                product_filtered = TRUE;
        }

        /* When both vendor ids and product ids are given, it may be the case that
         * we're allowing a full VID1 and only a subset of another VID2, so try to
         * handle that properly. */
        if (vendor_filtered && !product_filtered)
            vendor_filtered = FALSE;
        if (product_filtered && self->priv->vendor_ids && !vendor_filtered)
            product_filtered = FALSE;
    }

    /* If we got filtered by vendor or product IDs; mark it as unsupported only if:
     *   a) we do not have vendor or product strings to compare with (i.e. plugin
     *      doesn't have explicit vendor/product strings
     *   b) the port is NOT an AT port which we can use for AT probing
     */
    if ((vendor_filtered || product_filtered) &&
        ((!self->priv->vendor_strings &&
          !self->priv->product_strings &&
          !self->priv->forbidden_product_strings) ||
         g_str_equal (mm_kernel_device_get_subsystem (port), "net") ||
         g_str_has_prefix (mm_kernel_device_get_name (port), "cdc-wdm"))) {
        mm_obj_dbg (self, "port %s filtered by vendor/product IDs", mm_kernel_device_get_name (port));
        return TRUE;
    }

    /* The plugin may specify that some product IDs are not supported. If
     * that is the case, filter by forbidden vendor+product ID pair */
    if (self->priv->forbidden_product_ids && product && vendor) {
        for (i = 0; self->priv->forbidden_product_ids[i].l; i++) {
            if (vendor == self->priv->forbidden_product_ids[i].l &&
                product == self->priv->forbidden_product_ids[i].r) {
                mm_obj_dbg (self, "port %s filtered by forbidden vendor/product IDs", mm_kernel_device_get_name (port));
                return TRUE;
            }
        }
    }

    /* Check if we need vendor/product string probing
     * Only require these probings if the corresponding filters are given, and:
     *  1) if there was no vendor/product ID probing
     *  2) if there was vendor/product ID probing but we got filtered
     *
     * In other words, don't require vendor/product string probing if the plugin
     * already had vendor/product ID filters and we actually passed those. */
    if ((!self->priv->vendor_ids && !self->priv->product_ids) ||
        vendor_filtered ||
        product_filtered) {
        /* If product strings related filters around, we need to probe for both
         * vendor and product strings */
        if (self->priv->product_strings ||
            self->priv->forbidden_product_strings) {
            *need_vendor_probing = TRUE;
            *need_product_probing = TRUE;
        }
        /* If only vendor string filter is needed, only probe for vendor string */
        else if (self->priv->vendor_strings)
            *need_vendor_probing = TRUE;
    }

    /* The plugin may specify that only ports with some given udev tags are
     * supported. If that is the case, filter by udev tag */
    if (self->priv->udev_tags) {
        for (i = 0; self->priv->udev_tags[i]; i++) {
            /* Check if the port or device was tagged */
            if (mm_kernel_device_get_global_property_as_boolean (port, self->priv->udev_tags[i]))
                break;
        }

        /* If we didn't match any udev tag: unsupported */
        if (!self->priv->udev_tags[i]) {
            mm_obj_dbg (self, "port %s filtered by udev tags", mm_kernel_device_get_name (port));
            return TRUE;
        }
    }

    return FALSE;
}

/* Returns TRUE if the support check request was filtered out */
static gboolean
apply_post_probing_filters (MMPlugin *self,
                            MMPortProbeFlag flags,
                            MMPortProbe *probe)
{
    gboolean vendor_filtered = FALSE;
    guint i;

    /* The plugin may specify that only some vendor strings are supported. If
     * that is the case, filter by vendor string. */
    if ((flags & MM_PORT_PROBE_AT_VENDOR) &&
        self->priv->vendor_strings) {
        const gchar *vendor;

        vendor = mm_port_probe_get_vendor (probe);

        /* If we didn't get any vendor: filtered */
        if (!vendor)
            vendor_filtered = TRUE;
        else {
            for (i = 0; self->priv->vendor_strings[i]; i++) {
                gboolean found;
                gchar *casefolded;

                casefolded = g_utf8_casefold (self->priv->vendor_strings[i], -1);
                found = !!strstr (vendor, casefolded);
                g_free (casefolded);
                if (found)
                    break;
            }

            /* If we didn't match any vendor: filtered */
            if (!self->priv->vendor_strings[i])
                vendor_filtered = TRUE;
        }

        if (vendor_filtered) {
            if (!self->priv->product_strings) {
                mm_obj_dbg (self, "port %s filtered by vendor strings", mm_port_probe_get_port_name (probe));
                return TRUE;
            }
        } else
            /* Vendor matched */
            return FALSE;
    }

    /* The plugin may specify that only some vendor+product string pairs are
     * supported or unsupported. If that is the case, filter by product
     * string */
    if ((flags & MM_PORT_PROBE_AT_PRODUCT) &&
        (self->priv->product_strings ||
         self->priv->forbidden_product_strings)) {
        const gchar *vendor;
        const gchar *product;

        vendor = mm_port_probe_get_vendor (probe);
        product = mm_port_probe_get_product (probe);

        if (self->priv->product_strings) {
            /* If we didn't get any vendor or product: filtered */
            if (!vendor || !product) {
                mm_obj_dbg (self, "port %s filtered as no vendor/product strings given", mm_port_probe_get_port_name (probe));
                return TRUE;
            }
            else {
                for (i = 0; self->priv->product_strings[i].l; i++) {
                    gboolean found;
                    gchar *casefolded_vendor;
                    gchar *casefolded_product;

                    casefolded_vendor = g_utf8_casefold (self->priv->product_strings[i].l, -1);
                    casefolded_product = g_utf8_casefold (self->priv->product_strings[i].r, -1);
                    found = (!!strstr (vendor, casefolded_vendor) &&
                             !!strstr (product, casefolded_product));
                    g_free (casefolded_vendor);
                    g_free (casefolded_product);
                    if (found)
                        break;
                }

                /* If we didn't match any product: unsupported */
                if (!self->priv->product_strings[i].l) {
                    mm_obj_dbg (self, "port %s filtered by vendor/product strings", mm_port_probe_get_port_name (probe));
                    return TRUE;
                }
            }
        }

        if (self->priv->forbidden_product_strings && vendor && product) {
            for (i = 0; self->priv->forbidden_product_strings[i].l; i++) {
                gboolean found;
                gchar *casefolded_vendor;
                gchar *casefolded_product;

                casefolded_vendor = g_utf8_casefold (self->priv->forbidden_product_strings[i].l, -1);
                casefolded_product = g_utf8_casefold (self->priv->forbidden_product_strings[i].r, -1);
                found = (!!strstr (vendor, casefolded_vendor) &&
                         !!strstr (product, casefolded_product));
                g_free (casefolded_vendor);
                g_free (casefolded_product);
                if (found) {
                    /* If we match a forbidden product: unsupported */
                    mm_obj_dbg (self, "port %s filtered by forbidden vendor/product strings", mm_port_probe_get_port_name (probe));
                    return TRUE;
                }
            }
        }

        /* Keep on with next filters */
    }

    /* The plugin may specify that only Icera-based modems are supported.
     * If that is the case, filter by allowed Icera support */
    if (self->priv->allowed_icera &&
        !mm_port_probe_is_icera (probe)) {
        /* Unsupported! */
        mm_obj_dbg (self, "port %s filtered as modem is not icera", mm_port_probe_get_port_name (probe));
        return TRUE;
    }

    /* The plugin may specify that Icera-based modems are NOT supported.
     * If that is the case, filter by forbidden Icera support */
    if (self->priv->forbidden_icera &&
        mm_port_probe_is_icera (probe)) {
        /* Unsupported! */
        mm_obj_dbg (self, "port %s filtered as modem is icera", mm_port_probe_get_port_name (probe));
        return TRUE;
    }

    /* The plugin may specify that only Xmm-based modems are supported.
     * If that is the case, filter by allowed Xmm support */
    if (self->priv->allowed_xmm &&
        !mm_port_probe_is_xmm (probe)) {
        /* Unsupported! */
        mm_obj_dbg (self, "port %s filtered as modem is not XMM", mm_port_probe_get_port_name (probe));
        return TRUE;
    }

    /* The plugin may specify that Xmm-based modems are NOT supported.
     * If that is the case, filter by forbidden Xmm support */
    if (self->priv->forbidden_xmm &&
        mm_port_probe_is_xmm (probe)) {
        /* Unsupported! */
        mm_obj_dbg (self, "port %s filtered as modem is XMM", mm_port_probe_get_port_name (probe));
        return TRUE;
    }

    return FALSE;
}

/* Context for the asynchronous probing operation */
typedef struct {
    MMPlugin *self;
    MMDevice *device;
    MMPortProbeFlag flags;
} PortProbeRunContext;

static void
port_probe_run_context_free (PortProbeRunContext *ctx)
{
    g_object_unref (ctx->device);
    g_object_unref (ctx->self);
    g_slice_free (PortProbeRunContext, ctx);
}

static void
port_probe_run_ready (MMPortProbe *probe,
                      GAsyncResult *probe_result,
                      GTask *task)
{
    GError *error = NULL;
    MMPluginSupportsResult result = MM_PLUGIN_SUPPORTS_PORT_UNKNOWN;
    PortProbeRunContext *ctx;

    if (!mm_port_probe_run_finish (probe, probe_result, &error)) {
        /* Probing failed saying the port is unsupported. This is not to be
         * treated as a generic error, the plugin is just telling us as nicely
         * as it can that the port is not supported, so don't warn these cases.
         */
        if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED)) {
            result = MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
            goto out;
        }

        /* Probing failed but the plugin tells us to retry; so we'll defer the
         * probing a bit */
        if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY)) {
            result = MM_PLUGIN_SUPPORTS_PORT_DEFER;
            goto out;
        }

        /* For remaining errors, just propagate them */
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Probing succeeded, recover context */
    ctx = g_task_get_task_data (task);

    /* Apply post probing filters */
    if (!apply_post_probing_filters (ctx->self, ctx->flags, probe)) {
        /* Port is supported! */
        result = MM_PLUGIN_SUPPORTS_PORT_SUPPORTED;

        /* If we were looking for AT ports, and the port is AT,
         * and we were told that only one AT port is expected, cancel AT
         * probings in the other available support tasks of the SAME
         * device. */
        if (ctx->self->priv->single_at &&
            ctx->flags & MM_PORT_PROBE_AT &&
            mm_port_probe_is_at (probe)) {
            GList *l;

            for (l = mm_device_peek_port_probe_list (ctx->device); l; l = g_list_next (l)) {
                if (l->data != probe)
                    mm_port_probe_run_cancel_at_probing (MM_PORT_PROBE (l->data));
            }
        }
    } else
        /* Filtered by post probing filters */
        result = MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

out:
    /* Complete action */
    g_clear_error (&error);
    g_task_return_int (task, result);
    g_object_unref (task);
}

G_STATIC_ASSERT (MM_PLUGIN_SUPPORTS_PORT_UNKNOWN == -1);

MMPluginSupportsResult
mm_plugin_supports_port_finish (MMPlugin      *self,
                                GAsyncResult  *result,
                                GError       **error)
{
    GError *inner_error = NULL;
    gssize value;

    g_return_val_if_fail (MM_IS_PLUGIN (self), MM_PLUGIN_SUPPORTS_PORT_UNKNOWN);
    g_return_val_if_fail (G_IS_TASK (result), MM_PLUGIN_SUPPORTS_PORT_UNKNOWN);

    value = g_task_propagate_int (G_TASK (result), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_PLUGIN_SUPPORTS_PORT_UNKNOWN;
    }
    return (MMPluginSupportsResult)value;
}

void
mm_plugin_supports_port (MMPlugin            *self,
                         MMDevice            *device,
                         MMKernelDevice      *port,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    MMPortProbe *probe = NULL;
    GTask *task;
    PortProbeRunContext *ctx;
    gboolean need_vendor_probing;
    gboolean need_product_probing;
    MMPortProbeFlag probe_run_flags;
    gchar *probe_list_str;

    g_return_if_fail (MM_IS_PLUGIN (self));
    g_return_if_fail (MM_IS_DEVICE (device));
    g_return_if_fail (MM_IS_KERNEL_DEVICE (port));

    /* Create new cancellable task */
    task = g_task_new (self, cancellable, callback, user_data);

    /* Apply filters before launching the probing */
    if (apply_pre_probing_filters (self,
                                   device,
                                   port,
                                   &need_vendor_probing,
                                   &need_product_probing)) {
        /* Filtered! */
        g_task_return_int (task, MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);
        g_object_unref (task);
        return;
    }

    /* Need to launch new probing */
    probe = MM_PORT_PROBE (mm_device_peek_port_probe (device, port));
    if (!probe) {
        /* This may happen if the ports get removed from the device while
         * probing is ongoing */
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "(%s) Missing port probe for port (%s/%s)",
                                 self->priv->name,
                                 mm_kernel_device_get_subsystem (port),
                                 mm_kernel_device_get_name (port));
        g_object_unref (task);
        return;
    }

    /* Build flags depending on what probing needed */
    probe_run_flags = MM_PORT_PROBE_NONE;
    if (g_str_equal (mm_kernel_device_get_subsystem (port), "tty")) {
        if (self->priv->at)
            probe_run_flags |= MM_PORT_PROBE_AT;
        else if (self->priv->single_at)
            probe_run_flags |= MM_PORT_PROBE_AT;
        if (self->priv->qcdm)
            probe_run_flags |= MM_PORT_PROBE_QCDM;
    } else if (g_str_equal (mm_kernel_device_get_subsystem (port), "usbmisc")) {
        if (self->priv->qmi && !g_strcmp0 (mm_kernel_device_get_driver (port), "qmi_wwan"))
            probe_run_flags |= MM_PORT_PROBE_QMI;
        else if (self->priv->mbim && !g_strcmp0 (mm_kernel_device_get_driver (port), "cdc_mbim"))
            probe_run_flags |= MM_PORT_PROBE_MBIM;
        else
            probe_run_flags |= MM_PORT_PROBE_AT;
    } else if (g_str_equal (mm_kernel_device_get_subsystem (port), "rpmsg")) {
        if (self->priv->at)
            probe_run_flags |= MM_PORT_PROBE_AT;
        if (self->priv->qmi)
            probe_run_flags |= MM_PORT_PROBE_QMI;
    } else if (g_str_equal (mm_kernel_device_get_subsystem (port), "wwan")) {
        if (self->priv->mbim)
            probe_run_flags |= MM_PORT_PROBE_MBIM;
        if (self->priv->qmi)
            probe_run_flags |= MM_PORT_PROBE_QMI;
        if (self->priv->qcdm)
            probe_run_flags |= MM_PORT_PROBE_QCDM;
        if (self->priv->at)
            probe_run_flags |= MM_PORT_PROBE_AT;
    }

    /* For potential AT ports, check for more things */
    if (probe_run_flags & MM_PORT_PROBE_AT) {
        if (need_vendor_probing)
            probe_run_flags |= MM_PORT_PROBE_AT_VENDOR;
        if (need_product_probing)
            probe_run_flags |= MM_PORT_PROBE_AT_PRODUCT;
        if (self->priv->icera_probe || self->priv->allowed_icera || self->priv->forbidden_icera)
            probe_run_flags |= MM_PORT_PROBE_AT_ICERA;
        if (self->priv->xmm_probe || self->priv->allowed_xmm || self->priv->forbidden_xmm)
            probe_run_flags |= MM_PORT_PROBE_AT_XMM;
    }

    /* If no explicit probing was required, just request to grab it without
     * probing anything. This happens for all net ports and e.g. for cdc-wdm
     * ports which do not need QMI/MBIM probing. */
    if (probe_run_flags == MM_PORT_PROBE_NONE) {
        mm_obj_dbg (self, "probing of port %s deferred until result suggested", mm_kernel_device_get_name (port));
        g_task_return_int (task, MM_PLUGIN_SUPPORTS_PORT_DEFER_UNTIL_SUGGESTED);
        g_object_unref (task);
        return;
    }

    /* If a modem is already available and the plugin says that only one AT port is
     * expected, check if we alredy got the single AT port. And if so, we know this
     * port being probed won't be AT. */
    if (self->priv->single_at &&
        mm_port_probe_list_has_at_port (mm_device_peek_port_probe_list (device)) &&
        !mm_port_probe_is_at (probe)) {
        mm_obj_dbg (self, "not setting up AT probing tasks in port %s: "
                    "modem already has the expected single AT port",
                    mm_kernel_device_get_name (port));

        /* Assuming it won't be an AT port. We still run the probe anyway, in
         * case we need to check for other port types (e.g. QCDM) */
        mm_port_probe_set_result_at (probe, FALSE);
    }

    /* Setup async call context */
    ctx = g_slice_new0 (PortProbeRunContext);
    ctx->self   = g_object_ref (self);
    ctx->device = g_object_ref (device);
    ctx->flags  = probe_run_flags;

    /* Store context in task */
    g_task_set_task_data (task, ctx, (GDestroyNotify) port_probe_run_context_free);

    /* Launch the probe */
    probe_list_str = mm_port_probe_flag_build_string_from_mask (ctx->flags);
    mm_obj_dbg (self, "probes required for port %s: '%s'",
                mm_kernel_device_get_name (port),
                probe_list_str);
    g_free (probe_list_str);

    mm_port_probe_run (probe,
                       ctx->flags,
                       self->priv->send_delay,
                       self->priv->remove_echo,
                       self->priv->send_lf,
                       self->priv->custom_at_probe,
                       self->priv->custom_init,
                       cancellable,
                       (GAsyncReadyCallback) port_probe_run_ready,
                       task);
}

/*****************************************************************************/

MMPluginSupportsHint
mm_plugin_discard_port_early (MMPlugin       *self,
                              MMDevice       *device,
                              MMKernelDevice *port)
{
    gboolean need_vendor_probing = FALSE;
    gboolean need_product_probing = FALSE;

    /* If fully filtered by pre-probing filters, port unsupported */
    if (apply_pre_probing_filters (self,
                                   device,
                                   port,
                                   &need_vendor_probing,
                                   &need_product_probing))
        return MM_PLUGIN_SUPPORTS_HINT_UNSUPPORTED;

    /* If there are no post-probing filters, this plugin is the only one (except
     * for the generic one) which will grab the port */
    if (!HAS_POST_PROBING_FILTERS (self))
        return MM_PLUGIN_SUPPORTS_HINT_SUPPORTED;

    /* If  no vendor/product probing needed, plugin is likely supported */
    if (!need_vendor_probing && !need_product_probing)
        return MM_PLUGIN_SUPPORTS_HINT_LIKELY;

    /* If vendor/product probing is needed, plugin may be supported */
    return MM_PLUGIN_SUPPORTS_HINT_MAYBE;
}

/*****************************************************************************/

MMBaseModem *
mm_plugin_create_modem (MMPlugin  *self,
                        MMDevice  *device,
                        GError   **error)
{
    MMBaseModem  *modem;
    GList        *port_probes = NULL;
    const gchar **virtual_ports = NULL;
    const gchar **drivers;

    if (!mm_device_is_virtual (device))
        port_probes = mm_device_peek_port_probe_list (device);
    else
        virtual_ports = mm_device_virtual_peek_ports (device);

    drivers = mm_device_get_drivers (device);

    /* Let the plugin create the modem from the port probe results */
    modem = MM_PLUGIN_GET_CLASS (self)->create_modem (MM_PLUGIN (self),
                                                      mm_device_get_uid (device),
                                                      drivers,
                                                      mm_device_get_vendor (device),
                                                      mm_device_get_product (device),
                                                      port_probes,
                                                      error);
    if (!modem)
        return NULL;

    mm_base_modem_set_hotplugged (modem, mm_device_get_hotplugged (device));

    if (port_probes) {
        GList *l;

        /* Grab each port */
        for (l = port_probes; l; l = g_list_next (l)) {
            GError      *inner_error = NULL;
            MMPortProbe *probe;
            gboolean     grabbed = FALSE;
            gboolean     force_ignored = FALSE;
            const gchar *subsys;
            const gchar *name;
            const gchar *driver;
            MMPortType   port_type;

            probe = MM_PORT_PROBE (l->data);

            subsys    = mm_port_probe_get_port_subsys (probe);
            name      = mm_port_probe_get_port_name   (probe);
            port_type = mm_port_probe_get_port_type   (probe);

            driver    = mm_kernel_device_get_driver (mm_port_probe_peek_port (probe));

            /* If grabbing a port fails, just warn. We'll decide if the modem is
             * valid or not when all ports get organized */

            /* We apply again the subsystem filter, as the port may have been
             * probed and accepted by the generic plugin, which is overwritten
             * by the specific one when needed. */
            if (apply_subsystem_filter (self, mm_port_probe_peek_port (probe))) {
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_UNSUPPORTED,
                                           "unsupported subsystem: '%s'",
                                           subsys);
                goto next;
            }

            /* Ports that are explicitly blacklisted will be grabbed as ignored */
            if (mm_port_probe_is_ignored (probe)) {
                mm_obj_dbg (self, "port %s is blacklisted", name);
                force_ignored = TRUE;
                goto grab_port;
            }

            /* Force network ignore rules for devices that use qmi_wwan */
            if (drivers && g_strv_contains (drivers, "qmi_wwan")) {
#if defined WITH_QMI
                if (MM_IS_BROADBAND_MODEM_QMI (modem) &&
                    port_type == MM_PORT_TYPE_NET &&
                    g_strcmp0 (driver, "qmi_wwan") != 0) {
                    /* Non-QMI net ports are ignored in QMI modems */
                    mm_obj_dbg (self, "ignoring non-QMI net port %s in QMI modem", name);
                    force_ignored = TRUE;
                    goto grab_port;
                }

                if (!MM_IS_BROADBAND_MODEM_QMI (modem) &&
                    port_type == MM_PORT_TYPE_NET &&
                    g_strcmp0 (driver, "qmi_wwan") == 0) {
                    /* QMI net ports are ignored in non-QMI modems */
                    mm_obj_dbg (self, "ignoring QMI net port %s in non-QMI modem", name);
                    force_ignored = TRUE;
                    goto grab_port;
                }
#else
                if (port_type == MM_PORT_TYPE_NET &&
                    g_strcmp0 (driver, "qmi_wwan") == 0) {
                    /* QMI net ports are ignored if QMI support not built */
                    mm_obj_dbg (self, "ignoring QMI net port %s as QMI support isn't available", name);
                    force_ignored = TRUE;
                    goto grab_port;
                }
#endif
            }

            /* Force network ignore rules for devices that use cdc_mbim */
            if (drivers && g_strv_contains (drivers, "cdc_mbim")) {
#if defined WITH_MBIM
                if (MM_IS_BROADBAND_MODEM_MBIM (modem) &&
                    port_type == MM_PORT_TYPE_NET &&
                    g_strcmp0 (driver, "cdc_mbim") != 0) {
                    /* Non-MBIM net ports are ignored in MBIM modems */
                    mm_obj_dbg (self, "ignoring non-MBIM net port %s in MBIM modem", name);
                    force_ignored = TRUE;
                    goto grab_port;
                }

                if (!MM_IS_BROADBAND_MODEM_MBIM (modem) &&
                    port_type == MM_PORT_TYPE_NET &&
                    g_strcmp0 (driver, "cdc_mbim") == 0) {
                    /* MBIM net ports are ignored in non-MBIM modems */
                    mm_obj_dbg (self, "ignoring MBIM net port %s in non-MBIM modem", name);
                    force_ignored = TRUE;
                    goto grab_port;
                }
#else
                if (port_type == MM_PORT_TYPE_NET &&
                    g_strcmp0 (driver, "cdc_mbim") == 0) {
                    mm_obj_dbg (self, "ignoring MBIM net port %s as MBIM support isn't available", name);
                    force_ignored = TRUE;
                    goto grab_port;
                }
#endif
            }

        grab_port:
            if (force_ignored)
                grabbed = mm_base_modem_grab_port (modem,
                                                   mm_port_probe_peek_port (probe),
                                                   MM_PORT_TYPE_IGNORED,
                                                   MM_PORT_SERIAL_AT_FLAG_NONE,
                                                   &inner_error);
            else if (MM_PLUGIN_GET_CLASS (self)->grab_port)
                grabbed = MM_PLUGIN_GET_CLASS (self)->grab_port (MM_PLUGIN (self),
                                                                 modem,
                                                                 probe,
                                                                 &inner_error);
            else
                grabbed = mm_base_modem_grab_port (modem,
                                                   mm_port_probe_peek_port (probe),
                                                   mm_port_probe_get_port_type (probe),
                                                   MM_PORT_SERIAL_AT_FLAG_NONE,
                                                   &inner_error);

        next:
            if (!grabbed) {
                mm_obj_warn (self, "could not grab port %s: %s", name, inner_error ? inner_error->message : "unknown error");
                g_clear_error (&inner_error);
            }
        }
    } else if (virtual_ports) {
        guint i;

        for (i = 0; virtual_ports[i]; i++) {
            GError                  *inner_error = NULL;
            MMKernelDevice          *kernel_device;
            MMKernelEventProperties *properties;

            properties = mm_kernel_event_properties_new ();
            mm_kernel_event_properties_set_action (properties, "add");
            mm_kernel_event_properties_set_subsystem (properties, "virtual");
            mm_kernel_event_properties_set_name (properties, virtual_ports[i]);

            /* Give an empty set of rules, because we don't want them to be
             * loaded from the udev rules path (as there may not be any
             * installed yet). */
            kernel_device = mm_kernel_device_generic_new_with_rules (properties, NULL, &inner_error);
            if (!kernel_device) {
                mm_obj_warn (self, "could not create generic device for virtual port %s: %s",
                             virtual_ports[i],
                             inner_error ? inner_error->message : "unknown error");
                g_clear_error (&inner_error);
            } else if (!mm_base_modem_grab_port (modem,
                                                 kernel_device,
                                                 MM_PORT_TYPE_AT,
                                                 MM_PORT_SERIAL_AT_FLAG_NONE,
                                                 &inner_error)) {
                mm_obj_warn (self, "could not grab virtual port %s: %s",
                             virtual_ports[i],
                             inner_error ? inner_error->message : "unknown error");
                g_clear_error (&inner_error);
            }

            if (kernel_device)
                g_object_unref (kernel_device);
            g_object_unref (properties);
        }
    }

    /* If organizing ports fails, consider the modem invalid */
    if (!mm_base_modem_organize_ports (modem, error))
        g_clear_object (&modem);

    return modem;
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMPlugin         *self;
    g_autofree gchar *plugin_name_lowercase = NULL;

    self = MM_PLUGIN (_self);
    plugin_name_lowercase = g_ascii_strdown (self->priv->name, -1);
    return g_strdup_printf ("plugin/%s", plugin_name_lowercase);
}

/*****************************************************************************/

static void
mm_plugin_init (MMPlugin *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_PLUGIN,
                                              MMPluginPrivate);

    /* Defaults */
    self->priv->send_delay = 100000;
    self->priv->remove_echo = TRUE;
    self->priv->send_lf = FALSE;
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMPlugin *self = MM_PLUGIN (object);

    switch (prop_id) {
    case PROP_NAME:
        /* Construct only */
        self->priv->name = g_value_dup_string (value);
        break;
    case PROP_IS_GENERIC:
        /* Construct only */
        self->priv->is_generic = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_SUBSYSTEMS:
        /* Construct only */
        self->priv->subsystems = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_DRIVERS:
        /* Construct only */
        self->priv->drivers = g_value_dup_boxed (value);
        break;
    case PROP_FORBIDDEN_DRIVERS:
        /* Construct only */
        self->priv->forbidden_drivers = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_VENDOR_IDS:
        /* Construct only */
        self->priv->vendor_ids = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_PRODUCT_IDS:
        /* Construct only */
        self->priv->product_ids = g_value_dup_boxed (value);
        break;
    case PROP_FORBIDDEN_PRODUCT_IDS:
        /* Construct only */
        self->priv->forbidden_product_ids = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_VENDOR_STRINGS:
        /* Construct only */
        self->priv->vendor_strings = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_PRODUCT_STRINGS:
        /* Construct only */
        self->priv->product_strings = g_value_dup_boxed (value);
        break;
    case PROP_FORBIDDEN_PRODUCT_STRINGS:
        /* Construct only */
        self->priv->forbidden_product_strings = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_UDEV_TAGS:
        /* Construct only */
        self->priv->udev_tags = g_value_dup_boxed (value);
        break;
    case PROP_ALLOWED_AT:
        /* Construct only */
        self->priv->at = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_SINGLE_AT:
        /* Construct only */
        self->priv->single_at = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_QCDM:
        /* Construct only */
        self->priv->qcdm = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_QMI:
        /* Construct only */
        self->priv->qmi = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_MBIM:
        /* Construct only */
        self->priv->mbim = g_value_get_boolean (value);
        break;
    case PROP_ICERA_PROBE:
        /* Construct only */
        self->priv->icera_probe = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_ICERA:
        /* Construct only */
        self->priv->allowed_icera = g_value_get_boolean (value);
        break;
    case PROP_FORBIDDEN_ICERA:
        /* Construct only */
        self->priv->forbidden_icera = g_value_get_boolean (value);
        break;
    case PROP_XMM_PROBE:
        /* Construct only */
        self->priv->xmm_probe = g_value_get_boolean (value);
        break;
    case PROP_ALLOWED_XMM:
        /* Construct only */
        self->priv->allowed_xmm = g_value_get_boolean (value);
        break;
    case PROP_FORBIDDEN_XMM:
        /* Construct only */
        self->priv->forbidden_xmm = g_value_get_boolean (value);
        break;
    case PROP_CUSTOM_AT_PROBE:
        /* Construct only */
        self->priv->custom_at_probe = g_value_dup_boxed (value);
        break;
    case PROP_CUSTOM_INIT:
        /* Construct only */
        self->priv->custom_init = g_value_dup_boxed (value);
        break;
    case PROP_SEND_DELAY:
        /* Construct only */
        self->priv->send_delay = (guint64)g_value_get_uint64 (value);
        break;
    case PROP_REMOVE_ECHO:
        /* Construct only */
        self->priv->remove_echo = g_value_get_boolean (value);
        break;
    case PROP_SEND_LF:
        /* Construct only */
        self->priv->send_lf = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMPlugin *self = MM_PLUGIN (object);

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, self->priv->name);
        break;
    case PROP_IS_GENERIC:
        g_value_set_boolean (value, self->priv->is_generic);
        break;
    case PROP_ALLOWED_SUBSYSTEMS:
        g_value_set_boxed (value, self->priv->subsystems);
        break;
    case PROP_ALLOWED_DRIVERS:
        g_value_set_boxed (value, self->priv->drivers);
        break;
    case PROP_FORBIDDEN_DRIVERS:
        g_value_set_boxed (value, self->priv->forbidden_drivers);
        break;
    case PROP_ALLOWED_VENDOR_IDS:
        g_value_set_boxed (value, self->priv->vendor_ids);
        break;
    case PROP_ALLOWED_PRODUCT_IDS:
        g_value_set_boxed (value, self->priv->product_ids);
        break;
    case PROP_FORBIDDEN_PRODUCT_IDS:
        g_value_set_boxed (value, self->priv->forbidden_product_ids);
        break;
    case PROP_ALLOWED_VENDOR_STRINGS:
        g_value_set_boxed (value, self->priv->vendor_strings);
        break;
    case PROP_ALLOWED_PRODUCT_STRINGS:
        g_value_set_boxed (value, self->priv->product_strings);
        break;
    case PROP_FORBIDDEN_PRODUCT_STRINGS:
        g_value_set_boxed (value, self->priv->forbidden_product_strings);
        break;
    case PROP_ALLOWED_AT:
        g_value_set_boolean (value, self->priv->at);
        break;
    case PROP_ALLOWED_SINGLE_AT:
        g_value_set_boolean (value, self->priv->single_at);
        break;
    case PROP_ALLOWED_QCDM:
        g_value_set_boolean (value, self->priv->qcdm);
        break;
    case PROP_ALLOWED_QMI:
        g_value_set_boolean (value, self->priv->qmi);
        break;
    case PROP_ALLOWED_MBIM:
        g_value_set_boolean (value, self->priv->mbim);
        break;
    case PROP_ALLOWED_UDEV_TAGS:
        g_value_set_boxed (value, self->priv->udev_tags);
        break;
    case PROP_ICERA_PROBE:
        g_value_set_boolean (value, self->priv->icera_probe);
        break;
    case PROP_ALLOWED_ICERA:
        g_value_set_boolean (value, self->priv->allowed_icera);
        break;
    case PROP_FORBIDDEN_ICERA:
        g_value_set_boolean (value, self->priv->forbidden_icera);
        break;
    case PROP_XMM_PROBE:
        g_value_set_boolean (value, self->priv->xmm_probe);
        break;
    case PROP_ALLOWED_XMM:
        g_value_set_boolean (value, self->priv->allowed_xmm);
        break;
    case PROP_FORBIDDEN_XMM:
        g_value_set_boolean (value, self->priv->forbidden_xmm);
        break;
    case PROP_CUSTOM_AT_PROBE:
        g_value_set_boxed (value, self->priv->custom_at_probe);
        break;
    case PROP_CUSTOM_INIT:
        g_value_set_boxed (value, self->priv->custom_init);
        break;
    case PROP_SEND_DELAY:
        g_value_set_uint64 (value, self->priv->send_delay);
        break;
    case PROP_REMOVE_ECHO:
        g_value_set_boolean (value, self->priv->remove_echo);
        break;
    case PROP_SEND_LF:
        g_value_set_boolean (value, self->priv->send_lf);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMPlugin *self = MM_PLUGIN (object);

    g_free (self->priv->name);

#define _g_boxed_free0(t,p) if (p) g_boxed_free (t, p)

    _g_boxed_free0 (G_TYPE_STRV, self->priv->subsystems);
    _g_boxed_free0 (G_TYPE_STRV, self->priv->drivers);
    _g_boxed_free0 (G_TYPE_STRV, self->priv->forbidden_drivers);
    _g_boxed_free0 (MM_TYPE_UINT16_ARRAY, self->priv->vendor_ids);
    _g_boxed_free0 (MM_TYPE_UINT16_PAIR_ARRAY, self->priv->product_ids);
    _g_boxed_free0 (MM_TYPE_UINT16_PAIR_ARRAY, self->priv->forbidden_product_ids);
    _g_boxed_free0 (G_TYPE_STRV, self->priv->udev_tags);
    _g_boxed_free0 (G_TYPE_STRV, self->priv->vendor_strings);
    _g_boxed_free0 (MM_TYPE_STR_PAIR_ARRAY, self->priv->product_strings);
    _g_boxed_free0 (MM_TYPE_STR_PAIR_ARRAY, self->priv->forbidden_product_strings);
    _g_boxed_free0 (MM_TYPE_POINTER_ARRAY, self->priv->custom_at_probe);
    _g_boxed_free0 (MM_TYPE_ASYNC_METHOD, self->priv->custom_init);

#undef _g_boxed_free0

    G_OBJECT_CLASS (mm_plugin_parent_class)->finalize (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_plugin_class_init (MMPluginClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;

    g_object_class_install_property
        (object_class, PROP_NAME,
         g_param_spec_string (MM_PLUGIN_NAME,
                              "Name",
                              "Name",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_IS_GENERIC,
         g_param_spec_boolean (MM_PLUGIN_IS_GENERIC,
                               "Generic",
                               "Whether the plugin is the generic one",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_SUBSYSTEMS,
         g_param_spec_boxed (MM_PLUGIN_ALLOWED_SUBSYSTEMS,
                             "Allowed subsystems",
                             "List of subsystems this plugin can support, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_DRIVERS,
         g_param_spec_boxed (MM_PLUGIN_ALLOWED_DRIVERS,
                             "Allowed drivers",
                             "List of drivers this plugin can support, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_FORBIDDEN_DRIVERS,
         g_param_spec_boxed (MM_PLUGIN_FORBIDDEN_DRIVERS,
                             "Forbidden drivers",
                             "List of drivers this plugin cannot support, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_VENDOR_IDS,
         g_param_spec_boxed (MM_PLUGIN_ALLOWED_VENDOR_IDS,
                             "Allowed vendor IDs",
                             "List of vendor IDs this plugin can support, "
                             "should be an array of guint16 finished with '0'",
                             MM_TYPE_UINT16_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_PRODUCT_IDS,
         g_param_spec_boxed (MM_PLUGIN_ALLOWED_PRODUCT_IDS,
                             "Allowed product IDs",
                             "List of vendor+product ID pairs this plugin can support, "
                             "should be an array of mm_uint16_pair finished with '0,0'",
                             MM_TYPE_UINT16_PAIR_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_FORBIDDEN_PRODUCT_IDS,
         g_param_spec_boxed (MM_PLUGIN_FORBIDDEN_PRODUCT_IDS,
                             "Forbidden product IDs",
                             "List of vendor+product ID pairs this plugin cannot support, "
                             "should be an array of mm_uint16_pair finished with '0,0'",
                             MM_TYPE_UINT16_PAIR_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_VENDOR_STRINGS,
         g_param_spec_boxed (MM_PLUGIN_ALLOWED_VENDOR_STRINGS,
                             "Allowed vendor strings",
                             "List of vendor strings this plugin can support, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_PRODUCT_STRINGS,
         g_param_spec_boxed (MM_PLUGIN_ALLOWED_PRODUCT_STRINGS,
                             "Allowed product strings",
                             "List of vendor+product string pairs this plugin can support, "
                             "should be an array of mm_str_pair finished with 'NULL,NULL'",
                             MM_TYPE_STR_PAIR_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_FORBIDDEN_PRODUCT_STRINGS,
         g_param_spec_boxed (MM_PLUGIN_FORBIDDEN_PRODUCT_STRINGS,
                             "Forbidden product strings",
                             "List of vendor+product string pairs this plugin cannot support, "
                             "should be an array of mm_str_pair finished with 'NULL,NULL'",
                             MM_TYPE_STR_PAIR_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_UDEV_TAGS,
         g_param_spec_boxed (MM_PLUGIN_ALLOWED_UDEV_TAGS,
                             "Allowed Udev tags",
                             "List of udev tags this plugin may expect, "
                             "should be an array of strings finished with 'NULL'",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_AT,
         g_param_spec_boolean (MM_PLUGIN_ALLOWED_AT,
                               "Allowed AT",
                               "Whether AT ports are allowed in this plugin",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_SINGLE_AT,
         g_param_spec_boolean (MM_PLUGIN_ALLOWED_SINGLE_AT,
                               "Allowed single AT",
                               "Whether just a single AT port is allowed in this plugin. ",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_QCDM,
         g_param_spec_boolean (MM_PLUGIN_ALLOWED_QCDM,
                               "Allowed QCDM",
                               "Whether QCDM ports are allowed in this plugin",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_QMI,
         g_param_spec_boolean (MM_PLUGIN_ALLOWED_QMI,
                               "Allowed QMI",
                               "Whether QMI ports are allowed in this plugin",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_MBIM,
         g_param_spec_boolean (MM_PLUGIN_ALLOWED_MBIM,
                               "Allowed MBIM",
                               "Whether MBIM ports are allowed in this plugin",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ICERA_PROBE,
         g_param_spec_boolean (MM_PLUGIN_ICERA_PROBE,
                               "Icera probe",
                               "Request to probe for Icera support.",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_ICERA,
         g_param_spec_boolean (MM_PLUGIN_ALLOWED_ICERA,
                               "Allowed Icera",
                               "Whether Icera support is allowed in this plugin.",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_FORBIDDEN_ICERA,
         g_param_spec_boolean (MM_PLUGIN_FORBIDDEN_ICERA,
                               "Allowed Icera",
                               "Whether Icera support is forbidden in this plugin.",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_XMM_PROBE,
         g_param_spec_boolean (MM_PLUGIN_XMM_PROBE,
                               "XMM probe",
                               "Request to probe for XMM support.",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_ALLOWED_XMM,
         g_param_spec_boolean (MM_PLUGIN_ALLOWED_XMM,
                               "Allowed XMM",
                               "Whether XMM support is allowed in this plugin.",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_FORBIDDEN_XMM,
         g_param_spec_boolean (MM_PLUGIN_FORBIDDEN_XMM,
                               "Allowed XMM",
                               "Whether XMM support is forbidden in this plugin.",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_CUSTOM_AT_PROBE,
         g_param_spec_boxed (MM_PLUGIN_CUSTOM_AT_PROBE,
                             "Custom AT Probe",
                             "Custom set of commands to probe for AT support, "
                             "should be an array of MMPortProbeAtCommand structs "
                             "finished with 'NULL'",
                             MM_TYPE_POINTER_ARRAY,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_CUSTOM_INIT,
         g_param_spec_boxed (MM_PLUGIN_CUSTOM_INIT,
                             "Custom initialization",
                             "Asynchronous method setup which contains the "
                             "custom initializations this plugin needs.",
                             MM_TYPE_ASYNC_METHOD,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_SEND_DELAY,
         g_param_spec_uint64 (MM_PLUGIN_SEND_DELAY,
                              "Send delay",
                              "Send delay for characters in the AT port, "
                              "in microseconds",
                              0, G_MAXUINT64, 100000,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_REMOVE_ECHO,
         g_param_spec_boolean (MM_PLUGIN_REMOVE_ECHO,
                               "Remove echo",
                               "Remove echo out of the AT responses",
                               TRUE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_SEND_LF,
         g_param_spec_boolean (MM_PLUGIN_SEND_LF,
                               "Send LF",
                               "Send line-feed at the end of each AT command sent",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
