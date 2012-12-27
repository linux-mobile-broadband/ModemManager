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

#include <gudev/gudev.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-plugin.h"
#include "mm-device.h"
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"
#include "mm-serial-parsers.h"
#include "mm-marshal.h"
#include "mm-private-boxed-types.h"
#include "mm-log.h"
#include "mm-daemon-enums-types.h"

G_DEFINE_TYPE (MMPlugin, mm_plugin, G_TYPE_OBJECT)

/* Virtual port corresponding to the embeded modem */
static gchar *virtual_port[] = {"smd0", NULL};

#define HAS_POST_PROBING_FILTERS(self)          \
    (self->priv->vendor_strings ||              \
     self->priv->product_strings ||             \
     self->priv->forbidden_product_strings ||   \
     self->priv->allowed_icera ||               \
     self->priv->forbidden_icera ||             \
     self->priv->custom_init)


struct _MMPluginPrivate {
    gchar *name;
    GHashTable *tasks;

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

    /* Probing setup */
    gboolean at;
    gboolean single_at;
    gboolean qcdm;
    gboolean icera_probe;
    MMPortProbeAtCommand *custom_at_probe;
    guint64 send_delay;
    gboolean remove_echo;

    /* Probing setup and/or post-probing filter.
     * Plugins may use this method to decide whether they support a given
     * port or not, so should also be considered kind of post-probing filter. */
    MMAsyncMethod *custom_init;
};

enum {
    PROP_0,
    PROP_NAME,
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
    PROP_ICERA_PROBE,
    PROP_ALLOWED_ICERA,
    PROP_FORBIDDEN_ICERA,
    PROP_CUSTOM_AT_PROBE,
    PROP_CUSTOM_INIT,
    PROP_SEND_DELAY,
    PROP_REMOVE_ECHO,
    LAST_PROP
};

/*****************************************************************************/

const gchar *
mm_plugin_get_name (MMPlugin *self)
{
    return self->priv->name;
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
apply_subsystem_filter (MMPlugin *self,
                        GUdevDevice *port)
{
    if (self->priv->subsystems) {
        const gchar *subsys;
        guint i;

        subsys = g_udev_device_get_subsystem (port);
        for (i = 0; self->priv->subsystems[i]; i++) {
            if (g_str_equal (subsys, self->priv->subsystems[i]))
                break;
            /* New kernels may report as 'usbmisc' the subsystem */
            else if (g_str_equal (self->priv->subsystems[i], "usb") &&
                     g_str_equal (subsys, "usbmisc"))
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
apply_pre_probing_filters (MMPlugin *self,
                           MMDevice *device,
                           GUdevDevice *port,
                           gboolean *need_vendor_probing,
                           gboolean *need_product_probing)
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
        mm_dbg ("(%s) [%s] filtered by subsystem",
                self->priv->name,
                g_udev_device_get_name (port));
        return TRUE;
    }

    /* The plugin may specify that only some drivers are supported, or that some
     * drivers are not supported. If that is the case, filter by driver */
    if (self->priv->drivers ||
        self->priv->forbidden_drivers) {
        static const gchar *virtual_drivers [] = { "virtual", NULL };
        const gchar **drivers;

        /* Detect any modems accessible through the list of virtual ports */
        drivers = (is_virtual_port (g_udev_device_get_name (port)) ?
                   virtual_drivers :
                   mm_device_get_drivers (device));

        /* If error retrieving driver: unsupported */
        if (!drivers) {
            mm_dbg ("(%s) [%s] filtered as couldn't retrieve drivers",
                    self->priv->name,
                    g_udev_device_get_name (port));
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
                mm_dbg ("(%s) [%s] filtered by drivers",
                        self->priv->name,
                        g_udev_device_get_name (port));
                return TRUE;
            }
        }
        /* Filtering by forbidden drivers */
        else {
            for (i = 0; self->priv->forbidden_drivers[i]; i++) {
                guint j;

                for (j = 0; drivers[j]; j++) {
                    /* If we match a forbidden driver: unsupported */
                    if (g_str_equal (drivers[j], self->priv->forbidden_drivers[i])) {
                        mm_dbg ("(%s) [%s] filtered by forbidden drivers",
                                self->priv->name,
                                g_udev_device_get_name (port));
                        return TRUE;
                    }
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
    }

    /* If we got filtered by vendor or product IDs  and we do not have vendor
     * or product strings to compare with: unsupported */
    if ((vendor_filtered || product_filtered) &&
        !self->priv->vendor_strings &&
        !self->priv->product_strings &&
        !self->priv->forbidden_product_strings) {
        mm_dbg ("(%s) [%s] filtered by vendor/product IDs",
                self->priv->name,
                g_udev_device_get_name (port));
        return TRUE;
    }

    /* The plugin may specify that some product IDs are not supported. If
     * that is the case, filter by forbidden vendor+product ID pair */
    if (self->priv->forbidden_product_ids && product && vendor) {
        for (i = 0; self->priv->forbidden_product_ids[i].l; i++) {
            if (vendor == self->priv->forbidden_product_ids[i].l &&
                product == self->priv->forbidden_product_ids[i].r) {
                mm_dbg ("(%s) [%s] filtered by forbidden vendor/product IDs",
                        self->priv->name,
                        g_udev_device_get_name (port));
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
            /* Check if the port was tagged */
            if (g_udev_device_get_property_as_boolean (port,
                                                       self->priv->udev_tags[i]))
                break;
        }

        /* If we didn't match any udev tag: unsupported */
        if (!self->priv->udev_tags[i]) {
            mm_dbg ("(%s) [%s] filtered by udev tags",
                    self->priv->name,
                    g_udev_device_get_name (port));
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
                mm_dbg ("(%s) [%s] filtered by vendor strings",
                        self->priv->name,
                        mm_port_probe_get_port_name (probe));
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
                mm_dbg ("(%s) [%s] filtered as no vendor/product strings given",
                        self->priv->name,
                        mm_port_probe_get_port_name (probe));
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
                    mm_dbg ("(%s) [%s] filtered by vendor/product strings",
                            self->priv->name,
                            mm_port_probe_get_port_name (probe));
                    return TRUE;
                }
            }
        }

        if (self->priv->forbidden_product_strings && vendor && product) {
            for (i = 0; self->priv->forbidden_product_strings[i].l; i++) {
                gboolean found;
                gchar *casefolded_vendor;
                gchar *casefolded_product;

                casefolded_vendor = g_utf8_casefold (self->priv->product_strings[i].l, -1);
                casefolded_product = g_utf8_casefold (self->priv->product_strings[i].r, -1);
                found = (!!strstr (vendor, casefolded_vendor) &&
                         !!strstr (product, casefolded_product));
                g_free (casefolded_vendor);
                g_free (casefolded_product);
                if (found) {
                    /* If we match a forbidden product: unsupported */
                    mm_dbg ("(%s) [%s] filtered by forbidden vendor/product strings",
                            self->priv->name,
                            mm_port_probe_get_port_name (probe));
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
        mm_dbg ("(%s) [%s] filtered as modem is not icera",
                self->priv->name,
                mm_port_probe_get_port_name (probe));
        return TRUE;
    }

    /* The plugin may specify that Icera-based modems are NOT supported.
     * If that is the case, filter by forbidden Icera support */
    if (self->priv->forbidden_icera &&
        mm_port_probe_is_icera (probe)) {
        /* Unsupported! */
        mm_dbg ("(%s) [%s] filtered as modem is icera",
                self->priv->name,
                mm_port_probe_get_port_name (probe));
        return TRUE;
    }

    return FALSE;
}

/* Context for the asynchronous probing operation */
typedef struct {
    GSimpleAsyncResult *result;
    MMPlugin *self;
    MMPortProbeFlag flags;
    MMDevice *device;
} PortProbeRunContext;

static void
port_probe_run_ready (MMPortProbe *probe,
                      GAsyncResult *probe_result,
                      PortProbeRunContext *ctx)
{
    GError *error = NULL;

    if (!mm_port_probe_run_finish (probe, probe_result, &error)) {
        /* Probing failed saying the port is unsupported. This is not to be
         * treated as a generic error, the plugin is just telling us as nicely
         * as it can that the port is not supported, so don't warn these cases.
         */
        if (g_error_matches (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED)) {
            g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                       GUINT_TO_POINTER (MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED),
                                                       NULL);
        }
        /* Probing failed but the plugin tells us to retry; so we'll defer the
         * probing a bit */
        else if (g_error_matches (error,
                                  MM_CORE_ERROR,
                                  MM_CORE_ERROR_RETRY)) {
            g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                       GUINT_TO_POINTER (MM_PLUGIN_SUPPORTS_PORT_DEFER),
                                                       NULL);
        }
        /* For remaining errors, just propagate them */
        else {
            g_simple_async_result_take_error (ctx->result, error);
        }
    } else {
        /* Probing succeeded */
        MMPluginSupportsResult supports_result;

        if (!apply_post_probing_filters (ctx->self, ctx->flags, probe)) {
            /* Port is supported! */
            supports_result = MM_PLUGIN_SUPPORTS_PORT_SUPPORTED;

            /* If we were looking for AT ports, and the port is AT,
             * and we were told that only one AT port is expected, cancel AT
             * probings in the other available support tasks of the SAME
             * device. */
            if (ctx->self->priv->single_at &&
                ctx->flags & MM_PORT_PROBE_AT &&
                mm_port_probe_is_at (probe)) {
                GList *l;

                for (l = mm_device_peek_port_probe_list (ctx->device); l; l = g_list_next (l)) {
                    if (l->data != probe) {
                        mm_port_probe_run_cancel_at_probing (MM_PORT_PROBE (l->data));
                    }
                }
            }
        } else {
            /* Unsupported port, remove from internal tracking HT */
            supports_result = MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
        }

        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   GUINT_TO_POINTER (supports_result),
                                                   NULL);
    }

    /* Complete the async supports port request */
    g_simple_async_result_complete_in_idle (ctx->result);

    g_object_unref (ctx->device);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

MMPluginSupportsResult
mm_plugin_supports_port_finish (MMPlugin *self,
                                GAsyncResult *result,
                                GError **error)
{
    g_return_val_if_fail (MM_IS_PLUGIN (self),
                          MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result),
                          MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);

    /* Propagate error, if any */
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error)) {
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
    }

    return (MMPluginSupportsResult) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result)));
}

void
mm_plugin_supports_port (MMPlugin *self,
                         MMDevice *device,
                         GUdevDevice *port,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    MMPortProbe *probe;
    GSimpleAsyncResult *async_result;
    PortProbeRunContext *ctx;
    gboolean need_vendor_probing;
    gboolean need_product_probing;
    MMPortProbeFlag probe_run_flags;
    gchar *probe_list_str;

    async_result = g_simple_async_result_new (G_OBJECT (self),
                                              callback,
                                              user_data,
                                              mm_plugin_supports_port);

    /* Apply filters before launching the probing */
    if (apply_pre_probing_filters (self,
                                   device,
                                   port,
                                   &need_vendor_probing,
                                   &need_product_probing)) {
        /* Filtered! */
        g_simple_async_result_set_op_res_gpointer (async_result,
                                                   GUINT_TO_POINTER (MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED),
                                                   NULL);
        g_simple_async_result_complete_in_idle (async_result);
        goto out;
    }

    /* Need to launch new probing */
    probe = MM_PORT_PROBE (mm_device_get_port_probe (device, port));
    if (!probe) {
        /* This may happen if the ports get removed from the device while
         * probing is ongoing */
        g_simple_async_result_set_error (async_result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "(%s) Missing port probe for port (%s/%s)",
                                         self->priv->name,
                                         g_udev_device_get_subsystem (port),
                                         g_udev_device_get_name (port));
        g_simple_async_result_complete_in_idle (async_result);
        goto out;
    }

    /* Before launching any probing, check if the port is a net device. */
    if (g_str_equal (g_udev_device_get_subsystem (port), "net")) {
        mm_dbg ("(%s) [%s] probing deferred until result suggested",
                self->priv->name,
                g_udev_device_get_name (port));
        g_simple_async_result_set_op_res_gpointer (
            async_result,
            GUINT_TO_POINTER (MM_PLUGIN_SUPPORTS_PORT_DEFER_UNTIL_SUGGESTED),
            NULL);
        g_simple_async_result_complete_in_idle (async_result);
        goto out;
    }

    /* Build flags depending on what probing needed */
    if (!g_str_has_prefix (g_udev_device_get_name (port), "cdc-wdm")) {
        /* Serial ports... */
        probe_run_flags = MM_PORT_PROBE_NONE;
        if (self->priv->at)
            probe_run_flags |= MM_PORT_PROBE_AT;
        else if (self->priv->single_at)
            probe_run_flags |= MM_PORT_PROBE_AT;
        if (need_vendor_probing)
            probe_run_flags |= (MM_PORT_PROBE_AT | MM_PORT_PROBE_AT_VENDOR);
        if (need_product_probing)
            probe_run_flags |= (MM_PORT_PROBE_AT | MM_PORT_PROBE_AT_PRODUCT);
        if (self->priv->qcdm)
            probe_run_flags |= MM_PORT_PROBE_QCDM;
        if (self->priv->icera_probe || self->priv->allowed_icera || self->priv->forbidden_icera)
            probe_run_flags |= (MM_PORT_PROBE_AT | MM_PORT_PROBE_AT_ICERA);
    } else {
        /* cdc-wdm ports... */
        probe_run_flags = MM_PORT_PROBE_QMI;
    }

    g_assert (probe_run_flags != MM_PORT_PROBE_NONE);

    /* If a modem is already available and the plugin says that only one AT port is
     * expected, check if we alredy got the single AT port. And if so, we know this
     * port being probed won't be AT. */
    if (self->priv->single_at &&
        mm_port_probe_list_has_at_port (mm_device_peek_port_probe_list (device)) &&
        !mm_port_probe_is_at (probe)) {
        mm_dbg ("(%s) [%s] not setting up AT probing tasks: "
                "modem already has the expected single AT port",
                self->priv->name,
                g_udev_device_get_name (port));

        /* Assuming it won't be an AT port. We still run the probe anyway, in
         * case we need to check for other port types (e.g. QCDM) */
        mm_port_probe_set_result_at (probe, FALSE);
    }

    /* Setup async call context */
    ctx = g_new (PortProbeRunContext, 1);
    ctx->self = g_object_ref (self);
    ctx->device = g_object_ref (device);
    ctx->result = g_object_ref (async_result);
    ctx->flags = probe_run_flags;

    /* Launch the probe */
    probe_list_str = mm_port_probe_flag_build_string_from_mask (ctx->flags);
    mm_dbg ("(%s) [%s] probe required: '%s'",
            self->priv->name,
            g_udev_device_get_name (port),
            probe_list_str);
    g_free (probe_list_str);

    mm_port_probe_run (probe,
                       ctx->flags,
                       self->priv->send_delay,
                       self->priv->remove_echo,
                       self->priv->custom_at_probe,
                       self->priv->custom_init,
                       (GAsyncReadyCallback)port_probe_run_ready,
                       ctx);

out:
    g_object_unref (async_result);
}

/*****************************************************************************/

MMPluginSupportsHint
mm_plugin_discard_port_early (MMPlugin *self,
                              MMDevice *device,
                              GUdevDevice *port)
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
                        MMDevice *device,
                        GError   **error)
{
    MMBaseModem *modem = NULL;
    GList *port_probes, *l;

    port_probes = mm_device_peek_port_probe_list (device);

    /* Let the plugin create the modem from the port probe results */
    modem = MM_PLUGIN_GET_CLASS (self)->create_modem (MM_PLUGIN (self),
                                                      mm_device_get_path (device),
                                                      mm_device_get_drivers (device),
                                                      mm_device_get_vendor (device),
                                                      mm_device_get_product (device),
                                                      port_probes,
                                                      error);
    if (modem) {
        /* Grab each port */
        for (l = port_probes; l; l = g_list_next (l)) {
            GError *inner_error = NULL;
            MMPortProbe *probe = MM_PORT_PROBE (l->data);
            gboolean grabbed;

            /* If grabbing a port fails, just warn. We'll decide if the modem is
             * valid or not when all ports get organized */

            /* We apply again the subsystem filter, as the port may have been
             * probed and accepted by the generic plugin, which is overwritten
             * by the specific one when needed. */
            if (apply_subsystem_filter (self, mm_port_probe_peek_port (probe))) {
                grabbed = FALSE;
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_UNSUPPORTED,
                                           "unsupported subsystem: '%s'",
                                           mm_port_probe_get_port_subsys (probe));
            }
#if !defined WITH_QMI
            else if (mm_port_probe_get_port_type (probe) == MM_PORT_TYPE_NET &&
                     g_str_equal (mm_device_utils_get_port_driver (mm_port_probe_peek_port (probe)),
                                  "qmi_wwan")) {
                grabbed = FALSE;
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_UNSUPPORTED,
                                           "ignoring QMI net port");
            }
#endif
            else if (MM_PLUGIN_GET_CLASS (self)->grab_port)
                grabbed = MM_PLUGIN_GET_CLASS (self)->grab_port (MM_PLUGIN (self),
                                                                 modem,
                                                                 probe,
                                                                 &inner_error);
            else
                grabbed = mm_base_modem_grab_port (modem,
                                                   mm_port_probe_get_port_subsys (probe),
                                                   mm_port_probe_get_port_name (probe),
                                                   mm_port_probe_get_port_type (probe),
                                                   MM_AT_PORT_FLAG_NONE,
                                                   &inner_error);
            if (!grabbed) {
                mm_warn ("Could not grab port (%s/%s): '%s'",
                         mm_port_probe_get_port_subsys (MM_PORT_PROBE (l->data)),
                         mm_port_probe_get_port_name (MM_PORT_PROBE (l->data)),
                         inner_error ? inner_error->message : "unknown error");
                g_clear_error (&inner_error);
            }
        }

        /* If organizing ports fails, consider the modem invalid */
        if (!mm_base_modem_organize_ports (modem, error))
            g_clear_object (&modem);
    }

    return modem;
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

    G_OBJECT_CLASS (mm_plugin_parent_class)->finalize (object);
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
}
