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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-port-enums-types.h"
#include "mm-log.h"
#include "mm-plugin-huawei.h"
#include "mm-broadband-modem-huawei.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim.h"
#endif

G_DEFINE_TYPE (MMPluginHuawei, mm_plugin_huawei, MM_TYPE_PLUGIN)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION

/*****************************************************************************/
/* Custom init */

#define TAG_FIRST_INTERFACE_CONTEXT "first-interface-context"

/* Maximum time to wait for the first interface 0 to appear and get probed.
 * If it doesn't appear in this time, we'll decide which will be considered the
 * first interface. */
#define MAX_WAIT_TIME 5

typedef struct {
    MMPortProbe *probe;
    guint        first_usbif;
    guint        timeout_id;
    gboolean     custom_init_run;
} FirstInterfaceContext;

static void
first_interface_context_free (FirstInterfaceContext *ctx)
{
    if (ctx->timeout_id)
        g_source_remove (ctx->timeout_id);
    g_object_unref (ctx->probe);
    g_slice_free (FirstInterfaceContext, ctx);
}

#define TAG_HUAWEI_PCUI_PORT       "huawei-pcui-port"
#define TAG_HUAWEI_MODEM_PORT      "huawei-modem-port"
#define TAG_HUAWEI_NDIS_PORT       "huawei-ndis-port"
#define TAG_HUAWEI_DIAG_PORT       "huawei-diag-port"
#define TAG_GETPORTMODE_SUPPORTED  "getportmode-supported"
#define TAG_AT_PORT_FLAGS          "at-port-flags"

typedef struct {
    MMPortSerialAt *port;
    gboolean        curc_done;
    guint           curc_retries;
    gboolean        getportmode_done;
    guint           getportmode_retries;
} HuaweiCustomInitContext;

static void
huawei_custom_init_context_free (HuaweiCustomInitContext *ctx)
{
    g_object_unref (ctx->port);
    g_slice_free (HuaweiCustomInitContext, ctx);
}

static gboolean
huawei_custom_init_finish (MMPortProbe *probe,
                          GAsyncResult *result,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void huawei_custom_init_step (GTask *task);

static gboolean
cache_port_mode (MMDevice *device,
                 const gchar *reply,
                 const gchar *type,
                 const gchar *tag)
{
    gchar  *p;
    gulong  i;

    /* Get the USB interface number of the PCUI port */
    p = strstr (reply, type);
    if (p) {
        errno = 0;
        /* shift by 1 so NULL return from g_object_get_data() means no tag */
        i = 1 + strtol (p + strlen (type), NULL, 10);
        if (i > 0 && i < 256 && errno == 0) {
            g_object_set_data (G_OBJECT (device), tag, GINT_TO_POINTER ((gint) i));
            return TRUE;
        }
    }
    return FALSE;
}

static void
getportmode_ready (MMPortSerialAt *port,
                   GAsyncResult   *res,
                   GTask          *task)
{
    MMPortProbe             *probe;
    HuaweiCustomInitContext *ctx;
    const gchar             *response;
    GError                  *error = NULL;

    probe = g_task_get_source_object (task);
    ctx   = g_task_get_task_data (task);

    response = mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        mm_obj_dbg (probe, "couldn't get port mode: '%s'", error->message);

        /* If any error occurred that was not ERROR or COMMAND NOT SUPPORT then
         * retry the command.
         */
        if (!g_error_matches (error,
                              MM_MOBILE_EQUIPMENT_ERROR,
                              MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN))
            goto out;

        /* Port mode not supported */
    } else {
        MMDevice *device;
        guint     n_cached_port_modes = 0;

        mm_obj_dbg (probe, "port mode layout retrieved");

        /* Results are cached in the parent device object */
        device = mm_port_probe_peek_device (probe);
        n_cached_port_modes += cache_port_mode (device, response, "PCUI:", TAG_HUAWEI_PCUI_PORT);
        n_cached_port_modes += cache_port_mode (device, response, "MDM:",  TAG_HUAWEI_MODEM_PORT);
        n_cached_port_modes += cache_port_mode (device, response, "NDIS:", TAG_HUAWEI_NDIS_PORT);
        n_cached_port_modes += cache_port_mode (device, response, "DIAG:", TAG_HUAWEI_DIAG_PORT);
        /* GETPORTMODE response format in newer devices... (e.g. E3372) */
        n_cached_port_modes += cache_port_mode (device, response, "pcui:",  TAG_HUAWEI_PCUI_PORT);
        n_cached_port_modes += cache_port_mode (device, response, "modem:", TAG_HUAWEI_MODEM_PORT);

        if (n_cached_port_modes > 0)
            g_object_set_data (G_OBJECT (device), TAG_GETPORTMODE_SUPPORTED, GUINT_TO_POINTER (TRUE));

        /* Mark port as being AT already */
        mm_port_probe_set_result_at (probe, TRUE);
    }

    ctx->getportmode_done = TRUE;

out:
    if (error)
        g_error_free (error);

    huawei_custom_init_step (task);
}

static void
curc_ready (MMPortSerialAt *port,
            GAsyncResult   *res,
            GTask          *task)
{
    MMPortProbe             *probe;
    HuaweiCustomInitContext *ctx;
    g_autoptr(GError)        error = NULL;

    probe = g_task_get_source_object (task);
    ctx   = g_task_get_task_data (task);

    mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        /* Retry if we get a timeout error */
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT))
            goto out;

        mm_obj_dbg (probe, "couldn't turn off unsolicited messages in secondary ports: %s", error->message);
    }

    mm_obj_dbg (probe, "unsolicited messages in secondary ports turned off");

    ctx->curc_done = TRUE;

out:
    huawei_custom_init_step (task);
}

static void
try_next_usbif (MMPortProbe *probe,
                MMDevice    *device)
{
    FirstInterfaceContext *fi_ctx;
    GList *l;
    guint closest;

    fi_ctx = g_object_get_data (G_OBJECT (device), TAG_FIRST_INTERFACE_CONTEXT);
    g_assert (fi_ctx != NULL);

    /* Look for the next closest one among the list of interfaces in the device,
     * and enable that one as being first */
    closest = G_MAXUINT;
    for (l = mm_device_peek_port_probe_list (device); l; l = g_list_next (l)) {
        MMPortProbe *iter = MM_PORT_PROBE (l->data);

        /* Only expect ttys for next probing attempt */
        if (g_str_equal (mm_port_probe_get_port_subsys (iter), "tty")) {
            guint usbif;

            usbif = mm_kernel_device_get_property_as_int_hex (mm_port_probe_peek_port (iter), "ID_USB_INTERFACE_NUM");
            if (usbif == fi_ctx->first_usbif) {
                /* This is the one we just probed, which wasn't yet removed, so just skip it */
            } else if (usbif > fi_ctx->first_usbif &&
                       usbif < closest) {
                closest = usbif;
            }
        }
    }

    if (closest == G_MAXUINT) {
        /* No more ttys to try! Just return something */
        closest = 0;
        mm_obj_dbg (probe, "no more ports to run initial probing");
    } else
        mm_obj_dbg (probe, "will try initial probing with interface '%d' instead", closest);

    fi_ctx->first_usbif = closest;
}

static void
huawei_custom_init_step (GTask *task)
{
    MMPortProbe             *probe;
    HuaweiCustomInitContext *ctx;
    FirstInterfaceContext   *fi_ctx;
    MMKernelDevice          *port;

    probe = g_task_get_source_object (task);
    ctx   = g_task_get_task_data (task);

    /* If cancelled, end */
    if (g_task_return_error_if_cancelled (task)) {
        mm_obj_dbg (probe, "no need to keep on running custom init");
        g_object_unref (task);
        return;
    }

    if (!ctx->curc_done) {
        if (ctx->curc_retries == 0) {
            /* All retries consumed, probably not an AT port */
            mm_port_probe_set_result_at (probe, FALSE);
            /* Try with next */
            try_next_usbif (probe, mm_port_probe_peek_device (probe));
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }

        ctx->curc_retries--;
        /* Turn off unsolicited messages on secondary ports until needed */
        mm_port_serial_at_command (
            ctx->port,
            "AT^CURC=0",
            3,
            FALSE, /* raw */
            FALSE, /* allow_cached */
            g_task_get_cancellable (task),
            (GAsyncReadyCallback)curc_ready,
            task);
        return;
    }

    /* Try to get a port map from the modem */
    port = mm_port_probe_peek_port (probe);
    if (!ctx->getportmode_done && !mm_kernel_device_get_global_property_as_boolean (port, "ID_MM_HUAWEI_DISABLE_GETPORTMODE")) {
        if (ctx->getportmode_retries == 0) {
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }

        ctx->getportmode_retries--;
        mm_port_serial_at_command (
            ctx->port,
            "AT^GETPORTMODE",
            3,
            FALSE, /* raw */
            FALSE, /* allow_cached */
            g_task_get_cancellable (task),
            (GAsyncReadyCallback)getportmode_ready,
            task);
        return;
    }

    /* All done it seems */
    fi_ctx = g_object_get_data (G_OBJECT (mm_port_probe_peek_device (probe)), TAG_FIRST_INTERFACE_CONTEXT);
    g_assert (fi_ctx != NULL);
    fi_ctx->custom_init_run = TRUE;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
first_interface_missing_timeout_cb (MMDevice *device)
{
    FirstInterfaceContext *fi_ctx;

    fi_ctx = g_object_get_data (G_OBJECT (device), TAG_FIRST_INTERFACE_CONTEXT);
    g_assert (fi_ctx != NULL);
    try_next_usbif (fi_ctx->probe, device);

    /* Reload the timeout, just in case we end up not having the next interface to probe...
     * which is anyway very unlikely as we got it by looking at the real probe list, but anyway... */
    return G_SOURCE_CONTINUE;
}

static void
huawei_custom_init (MMPortProbe *probe,
                    MMPortSerialAt *port,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MMDevice *device;
    FirstInterfaceContext *fi_ctx;
    HuaweiCustomInitContext *ctx;
    GTask *task;

    device = mm_port_probe_peek_device (probe);

    /* The primary port (called the "modem" port in the Windows drivers) is
     * always USB interface 0, and we need to detect that interface first for
     * two reasons: (1) to disable unsolicited messages on other ports that
     * may fill up the buffer and crash the device, and (2) to attempt to get
     * the port layout for hints about what the secondary port is (called the
     * "pcui" port in Windows).  Thus we probe USB interface 0 first and defer
     * probing other interfaces until we've got if0, at which point we allow
     * the other ports to be probed too.
     */
    fi_ctx = g_object_get_data (G_OBJECT (device), TAG_FIRST_INTERFACE_CONTEXT);
    if (!fi_ctx) {
        /* This is the first time we ask for the context. Set it up. */
        fi_ctx = g_slice_new0 (FirstInterfaceContext);
        fi_ctx->probe = g_object_ref (probe);
        g_object_set_data_full (G_OBJECT (device),
                                TAG_FIRST_INTERFACE_CONTEXT,
                                fi_ctx,
                                (GDestroyNotify)first_interface_context_free);
        /* The timeout is controlled in the data set in 'device', and therefore
         * it should be safe to assume that the timeout will not get fired after
         * having disposed 'device' */
        fi_ctx->timeout_id = g_timeout_add_seconds (MAX_WAIT_TIME,
                                                    (GSourceFunc)first_interface_missing_timeout_cb,
                                                    device);

        /* By default, we'll ask the Huawei plugin to start probing usbif 0 */
        fi_ctx->first_usbif = 0;

        /* Custom init of the Huawei plugin is to be run only in the first
         * interface. We'll control here whether we did run it already or not. */
        fi_ctx->custom_init_run = FALSE;
    }

    ctx = g_slice_new (HuaweiCustomInitContext);
    ctx->port = g_object_ref (port);
    ctx->curc_done = FALSE;
    ctx->curc_retries = 3;
    ctx->getportmode_done = FALSE;
    ctx->getportmode_retries = 3;

    task = g_task_new (probe, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)huawei_custom_init_context_free);

    /* Custom init only to be run in the first interface */
    if (mm_kernel_device_get_property_as_int_hex (mm_port_probe_peek_port (probe),
                                                  "ID_USB_INTERFACE_NUM") != fi_ctx->first_usbif) {

        if (fi_ctx->custom_init_run)
            /* If custom init was run already, we can consider this as successfully run */
            g_task_return_boolean (task, TRUE);
        else
            /* Otherwise, we'll need to defer the probing a bit more */
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_RETRY,
                                     "Defer needed");

        g_object_unref (task);
        return;
    }

    /* We can run custom init in the first interface! clear the timeout as it is no longer needed */
    if (fi_ctx->timeout_id) {
        g_source_remove (fi_ctx->timeout_id);
        fi_ctx->timeout_id = 0;
    }

    huawei_custom_init_step (task);
}

/*****************************************************************************/

static void
propagate_port_mode_results (MMPlugin *self,
                             GList    *probes)
{
    MMDevice *device;
    GList    *l;
    gboolean  primary_flagged = FALSE;

    g_assert (probes != NULL);

    /* Now we propagate the tags to the specific port probes */
    device = mm_port_probe_peek_device (MM_PORT_PROBE (probes->data));
    for (l = probes; l; l = g_list_next (l)) {
        MMPortSerialAtFlag  at_port_flags = MM_PORT_SERIAL_AT_FLAG_NONE;
        MMPortProbe        *probe;
        guint               usbif;
        const gchar        *description;

        probe = MM_PORT_PROBE (l->data);

        /* Tags only applicable to AT ports */
        if (!mm_port_probe_is_at (probe))
            goto next;

        /* Port type hints from AT^GETPORTMODE */
        usbif = mm_kernel_device_get_property_as_int_hex (mm_port_probe_peek_port (probe), "ID_USB_INTERFACE_NUM");
        if (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), TAG_GETPORTMODE_SUPPORTED))) {
            if (usbif + 1 == GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), TAG_HUAWEI_PCUI_PORT))) {
                at_port_flags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
                primary_flagged = TRUE;
            } else if (usbif + 1 == GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), TAG_HUAWEI_MODEM_PORT))) {
                at_port_flags = MM_PORT_SERIAL_AT_FLAG_PPP;
            } else if (!g_object_get_data (G_OBJECT (device), TAG_HUAWEI_MODEM_PORT) &&
                     usbif + 1 == GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), TAG_HUAWEI_NDIS_PORT))) {
                /* If NDIS reported only instead of MDM, use it */
                at_port_flags = MM_PORT_SERIAL_AT_FLAG_PPP;
            }
            goto next;
        }

        /* Port type hints from interface description */
        description = mm_kernel_device_get_interface_description (mm_port_probe_peek_port (probe));
        if (description) {
            gchar *lower_description;

            mm_obj_dbg (probe, "%s interface description: %s", mm_port_probe_get_port_name (probe), description);

            lower_description = g_ascii_strdown (description, -1);
            if (strstr (lower_description, "modem"))
                at_port_flags = MM_PORT_SERIAL_AT_FLAG_PPP;
            else if (strstr (lower_description, "pcui")) {
                at_port_flags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
                primary_flagged = TRUE;
            }
            g_free (lower_description);
            goto next;
        }

        /* If GETPORTMODE unsupported and no other port type hints, we assume
         * usbif 0 is the modem port */
        if (usbif == 0) {
            at_port_flags = MM_PORT_SERIAL_AT_FLAG_PPP;
            goto next;
        }

    next:
        g_object_set_data (G_OBJECT (l->data), TAG_AT_PORT_FLAGS, GUINT_TO_POINTER (at_port_flags));
    }

    if (primary_flagged)
        return;

    /* For devices exposing a cdc-wdm port, make sure it gets flagged as primary, if there is none
     * already */
    for (l = probes; l; l = g_list_next (l)) {
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (mm_port_probe_is_at (probe) &&
            g_str_has_prefix (mm_port_probe_get_port_subsys (probe), "usb") &&
            g_str_has_prefix (mm_port_probe_get_port_name (probe), "cdc-wdm")) {
            /* Flag as PRIMARY and do nothing else */
            g_object_set_data (G_OBJECT (probe), TAG_AT_PORT_FLAGS, GUINT_TO_POINTER (MM_PORT_SERIAL_AT_FLAG_PRIMARY));
            break;
        }
    }
}

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    propagate_port_mode_results (self, probes);

#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_obj_dbg (self, "QMI-powered Huawei modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (uid,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_obj_dbg (self, "MBIM-powered Huawei modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_new (uid,
                                                           drivers,
                                                           mm_plugin_get_name (self),
                                                           vendor,
                                                           product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_huawei_new (uid,
                                                         drivers,
                                                         mm_plugin_get_name (self),
                                                         vendor,
                                                         product));
}

static gboolean
grab_port (MMPlugin *self,
           MMBaseModem *modem,
           MMPortProbe *probe,
           GError **error)
{
    MMPortSerialAtFlag pflags;
    MMKernelDevice *port;
    MMPortType port_type;

    port_type = mm_port_probe_get_port_type (probe);
    port = mm_port_probe_peek_port (probe);

    pflags = (MMPortSerialAtFlag) GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (probe), TAG_AT_PORT_FLAGS));
    if (pflags != MM_PORT_SERIAL_AT_FLAG_NONE) {
        gchar *str;

        str = mm_port_serial_at_flag_build_string_from_mask (pflags);
        mm_obj_dbg (self, "(%s/%s) port will have AT flags '%s'",
                    mm_port_probe_get_port_subsys (probe),
                    mm_port_probe_get_port_name (probe),
                    str);
        g_free (str);
    }

    return mm_base_modem_grab_port (modem,
                                    port,
                                    port_type,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const guint16 vendor_ids[] = { 0x12d1, 0 };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (huawei_custom_init),
        .finish = G_CALLBACK (huawei_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_HUAWEI,
                      MM_PLUGIN_NAME,               MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_VENDOR_IDS, vendor_ids,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      MM_PLUGIN_ALLOWED_QMI,        TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,       TRUE,
                      MM_PLUGIN_CUSTOM_INIT,        &custom_init,
                      NULL));
}

static void
mm_plugin_huawei_init (MMPluginHuawei *self)
{
}

static void
mm_plugin_huawei_class_init (MMPluginHuaweiClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
