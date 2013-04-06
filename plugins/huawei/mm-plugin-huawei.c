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

#include "mm-serial-enums-types.h"
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

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/
/* Custom init */

#define TAG_FIRST_INTERFACE_CONTEXT "first-interface-context"

/* Maximum time to wait for the first interface 0 to appear and get probed.
 * If it doesn't appear in this time, we'll decide which will be considered the
 * first interface. */
#define MAX_WAIT_TIME 5

typedef struct {
    guint first_usbif;
    guint timeout_id;
    gboolean custom_init_run;
} FirstInterfaceContext;

static void
first_interface_context_free (FirstInterfaceContext *ctx)
{
    if (ctx->timeout_id)
        g_source_remove (ctx->timeout_id);
    g_slice_free (FirstInterfaceContext, ctx);
}

#define TAG_HUAWEI_PCUI_PORT       "huawei-pcui-port"
#define TAG_HUAWEI_MODEM_PORT      "huawei-modem-port"
#define TAG_HUAWEI_NDIS_PORT       "huawei-ndis-port"
#define TAG_HUAWEI_DIAG_PORT       "huawei-diag-port"
#define TAG_GETPORTMODE_SUPPORTED  "getportmode-supported"
#define TAG_AT_PORT_FLAGS          "at-port-flags"

typedef struct {
    MMPortProbe *probe;
    MMAtSerialPort *port;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    gboolean curc_done;
    guint curc_retries;
    gboolean getportmode_done;
    guint getportmode_retries;
} HuaweiCustomInitContext;

static void
huawei_custom_init_context_complete_and_free (HuaweiCustomInitContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->port);
    g_object_unref (ctx->probe);
    g_object_unref (ctx->result);
    g_slice_free (HuaweiCustomInitContext, ctx);
}

static gboolean
huawei_custom_init_finish (MMPortProbe *probe,
                          GAsyncResult *result,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void huawei_custom_init_step (HuaweiCustomInitContext *ctx);

static void
cache_port_mode (MMDevice *device,
                 const gchar *reply,
                 const gchar *type,
                 const gchar *tag)
{
    gchar *p;
    glong i;

    /* Get the USB interface number of the PCUI port */
    p = strstr (reply, type);
    if (p) {
        errno = 0;
        /* shift by 1 so NULL return from g_object_get_data() means no tag */
        i = 1 + strtol (p + strlen (type), NULL, 10);
        if (i > 0 && i < 256 && errno == 0)
            g_object_set_data (G_OBJECT (device), tag, GINT_TO_POINTER ((gint) i));
    }
}

static void
getportmode_ready (MMAtSerialPort *port,
                   GString *response,
                   GError *error,
                   HuaweiCustomInitContext *ctx)
{
    if (error) {
        mm_dbg ("(Huawei) couldn't get port mode: '%s'",
                error->message);

        /* If any error occurred that was not ERROR or COMMAND NOT SUPPORT then
         * retry the command.
         */
        if (!g_error_matches (error,
                              MM_MOBILE_EQUIPMENT_ERROR,
                              MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN)) {
            /* Retry */
            huawei_custom_init_step (ctx);
            return;
        }

        /* Port mode not supported */
    } else {
        MMDevice *device;

        mm_dbg ("(Huawei) port mode layout retrieved");

        /* Results are cached in the parent device object */
        device = mm_port_probe_peek_device (ctx->probe);
        cache_port_mode (device, response->str, "PCUI:", TAG_HUAWEI_PCUI_PORT);
        cache_port_mode (device, response->str, "MDM:",  TAG_HUAWEI_MODEM_PORT);
        cache_port_mode (device, response->str, "NDIS:", TAG_HUAWEI_NDIS_PORT);
        cache_port_mode (device, response->str, "DIAG:", TAG_HUAWEI_DIAG_PORT);
        g_object_set_data (G_OBJECT (device), TAG_GETPORTMODE_SUPPORTED, GUINT_TO_POINTER (TRUE));

        /* Mark port as being AT already */
        mm_port_probe_set_result_at (ctx->probe, TRUE);
    }

    ctx->getportmode_done = TRUE;
    huawei_custom_init_step (ctx);
}

static void
curc_ready (MMAtSerialPort *port,
            GString *response,
            GError *error,
            HuaweiCustomInitContext *ctx)
{
    if (error) {
        /* Retry if we get a timeout error */
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            huawei_custom_init_step (ctx);
            return;
        }

        mm_dbg ("(Huawei) couldn't turn off unsolicited messages in"
                "secondary ports: '%s'",
                error->message);
    }

    mm_dbg ("(Huawei) unsolicited messages in secondary ports turned off");

    ctx->curc_done = TRUE;
    huawei_custom_init_step (ctx);
}

static void
try_next_usbif (MMDevice *device)
{
    FirstInterfaceContext *fi_ctx;
    GList *l;
    gint closest;

    fi_ctx = g_object_get_data (G_OBJECT (device), TAG_FIRST_INTERFACE_CONTEXT);
    g_assert (fi_ctx != NULL);

    /* Look for the next closest one among the list of interfaces in the device,
     * and enable that one as being first */
    closest = G_MAXINT;
    for (l = mm_device_peek_port_probe_list (device); l; l = g_list_next (l)) {
        gint usbif;

        usbif = g_udev_device_get_property_as_int (mm_port_probe_peek_port (MM_PORT_PROBE (l->data)), "ID_USB_INTERFACE_NUM");
        if (usbif == fi_ctx->first_usbif) {
            g_warn_if_reached ();
        } else if (usbif > fi_ctx->first_usbif &&
                   usbif < closest) {
            closest = usbif;
        }
    }

    if (closest == G_MAXINT) {
        /* Retry with interface 0... */
        closest = 0;
    }

    mm_dbg ("(Huawei) Will try initial probing with interface '%d' instead", closest);

    fi_ctx->first_usbif = closest;
}

static void
huawei_custom_init_step (HuaweiCustomInitContext *ctx)
{
    FirstInterfaceContext *fi_ctx;

    /* If cancelled, end */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        mm_dbg ("(Huawei) no need to keep on running custom init in (%s)",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        huawei_custom_init_context_complete_and_free (ctx);
        return;
    }

    if (!ctx->curc_done) {
        if (ctx->curc_retries == 0) {
            /* All retries consumed, probably not an AT port */
            mm_port_probe_set_result_at (ctx->probe, FALSE);
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
            /* Try with next */
            try_next_usbif (mm_port_probe_peek_device (ctx->probe));
            huawei_custom_init_context_complete_and_free (ctx);
            return;
        }

        ctx->curc_retries--;
        /* Turn off unsolicited messages on secondary ports until needed */
        mm_at_serial_port_queue_command (
            ctx->port,
            "AT^CURC=0",
            3,
            FALSE, /* raw */
            ctx->cancellable,
            (MMAtSerialResponseFn)curc_ready,
            ctx);
        return;
    }

    /* Try to get a port map from the modem */
    if (!ctx->getportmode_done) {
        if (ctx->getportmode_retries == 0) {
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
            huawei_custom_init_context_complete_and_free (ctx);
            return;
        }

        ctx->getportmode_retries--;
        mm_at_serial_port_queue_command (
            ctx->port,
            "AT^GETPORTMODE",
            3,
            FALSE, /* raw */
            ctx->cancellable,
            (MMAtSerialResponseFn)getportmode_ready,
            ctx);
        return;
    }

    /* All done it seems */
    fi_ctx = g_object_get_data (G_OBJECT (mm_port_probe_peek_device (ctx->probe)), TAG_FIRST_INTERFACE_CONTEXT);
    g_assert (fi_ctx != NULL);
    fi_ctx->custom_init_run = TRUE;

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    huawei_custom_init_context_complete_and_free (ctx);
}

static gboolean
first_interface_missing_timeout_cb (MMDevice *device)
{
    try_next_usbif (device);

    /* Reload the timeout, just in case we end up not having the next interface to probe...
     * which is anyway very unlikely as we got it by looking at the real probe list, but anyway... */
    return TRUE;
}

static void
huawei_custom_init (MMPortProbe *probe,
                    MMAtSerialPort *port,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MMDevice *device;
    FirstInterfaceContext *fi_ctx;
    HuaweiCustomInitContext *ctx;

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
    ctx->result = g_simple_async_result_new (G_OBJECT (probe),
                                             callback,
                                             user_data,
                                             huawei_custom_init);
    ctx->probe = g_object_ref (probe);
    ctx->port = g_object_ref (port);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
    ctx->curc_done = FALSE;
    ctx->curc_retries = 3;
    ctx->getportmode_done = FALSE;
    ctx->getportmode_retries = 3;

    /* Custom init only to be run in the first interface */
    if (g_udev_device_get_property_as_int (mm_port_probe_peek_port (probe),
                                           "ID_USB_INTERFACE_NUM") != fi_ctx->first_usbif) {

        if (fi_ctx->custom_init_run)
            /* If custom init was run already, we can consider this as successfully run */
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        else
            /* Otherwise, we'll need to defer the probing a bit more */
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_RETRY,
                                             "Defer needed");

        huawei_custom_init_context_complete_and_free (ctx);
        return;
    }

    /* We can run custom init in the first interface! clear the timeout as it is no longer needed */
    g_source_remove (fi_ctx->timeout_id);
    fi_ctx->timeout_id = 0;

    huawei_custom_init_step (ctx);
}

/*****************************************************************************/

static void
propagate_port_mode_results (GList *probes)
{
    MMDevice *device;
    GList *l;

    g_assert (probes != NULL);
    device = mm_port_probe_peek_device (MM_PORT_PROBE (probes->data));

    /* Now we propagate the tags to the specific port probes */
    for (l = probes; l; l = g_list_next (l)) {
        MMAtPortFlag at_port_flags = MM_AT_PORT_FLAG_NONE;
        gint usbif;

        usbif = g_udev_device_get_property_as_int (mm_port_probe_peek_port (MM_PORT_PROBE (l->data)), "ID_USB_INTERFACE_NUM");

        if (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), TAG_GETPORTMODE_SUPPORTED))) {
            if (usbif + 1 == GPOINTER_TO_INT (g_object_get_data (G_OBJECT (device), TAG_HUAWEI_PCUI_PORT)))
                at_port_flags = MM_AT_PORT_FLAG_PRIMARY;
            else if (usbif + 1 == GPOINTER_TO_INT (g_object_get_data (G_OBJECT (device), TAG_HUAWEI_MODEM_PORT)))
                at_port_flags = MM_AT_PORT_FLAG_PPP;
            else if (!g_object_get_data (G_OBJECT (device), TAG_HUAWEI_MODEM_PORT) &&
                     usbif + 1 == GPOINTER_TO_INT (g_object_get_data (G_OBJECT (device), TAG_HUAWEI_NDIS_PORT)))
                /* If NDIS reported only instead of MDM, use it */
                at_port_flags = MM_AT_PORT_FLAG_PPP;
        } else if (usbif == 0 &&
                   mm_port_probe_is_at (MM_PORT_PROBE (l->data))) {
            /* If GETPORTMODE is not supported, we assume usbif 0 is the modem port */
            at_port_flags = MM_AT_PORT_FLAG_PPP;

            /* /\* TODO. */
            /*  * For CDMA modems we assume usbif0 is both primary and PPP, since */
            /*  * they don't have problems with talking on secondary ports. */
            /*  *\/ */
            /* if (caps & CAP_CDMA) */
            /*     pflags |= MM_AT_PORT_FLAG_PRIMARY; */
        }

        g_object_set_data (G_OBJECT (l->data), TAG_AT_PORT_FLAGS, GUINT_TO_POINTER (at_port_flags));
    }
}

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    propagate_port_mode_results (probes);

#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_dbg ("QMI-powered Huawei modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (sysfs_path,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_dbg ("MBIM-powered Huawei modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_new (sysfs_path,
                                                           drivers,
                                                           mm_plugin_get_name (self),
                                                           vendor,
                                                           product));
    }
#endif

    return MM_BASE_MODEM (mm_broadband_modem_huawei_new (sysfs_path,
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
    MMAtPortFlag pflags;
    GUdevDevice *port;

    port = mm_port_probe_peek_port (probe);
    if (g_udev_device_get_property_as_boolean (port, "ID_MM_HUAWEI_AT_PORT")) {
        mm_dbg ("(%s/%s)' Port flagged as primary",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe));
        pflags = MM_AT_PORT_FLAG_PRIMARY;
    } else if (g_udev_device_get_property_as_boolean (port, "ID_MM_HUAWEI_MODEM_PORT")) {
        mm_dbg ("(%s/%s) Port flagged as PPP",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe));
        pflags = MM_AT_PORT_FLAG_PPP;
    } else {
        gchar *str;

        pflags = (MMAtPortFlag) GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (probe), TAG_AT_PORT_FLAGS));
        str = mm_at_port_flag_build_string_from_mask (pflags);
        mm_dbg ("(%s/%s) Port will have AT flags '%s'",
                mm_port_probe_get_port_subsys (probe),
                mm_port_probe_get_port_name (probe),
                str);
        g_free (str);
    }

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    mm_port_probe_get_port_type (probe),
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
                      MM_PLUGIN_NAME,               "Huawei",
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
