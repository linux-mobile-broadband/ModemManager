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
 * Copyright (C) 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>

#include "mm-common-telit.h"
#include "mm-log-object.h"
#include "mm-serial-parsers.h"

/*****************************************************************************/

#define TAG_GETPORTCFG_SUPPORTED   "getportcfg-supported"

#define TAG_TELIT_MODEM_PORT       "ID_MM_TELIT_PORT_TYPE_MODEM"
#define TAG_TELIT_AUX_PORT         "ID_MM_TELIT_PORT_TYPE_AUX"
#define TAG_TELIT_NMEA_PORT        "ID_MM_TELIT_PORT_TYPE_NMEA"

#define TELIT_GE910_FAMILY_PID     0x0022

/* The following number of retries of the port responsiveness
 * check allows having up to 30 seconds of wait, that should
 * be fine for most of the modems */
#define TELIT_PORT_CHECK_RETRIES   6

gboolean
telit_grab_port (MMPlugin *self,
                 MMBaseModem *modem,
                 MMPortProbe *probe,
                 GError **error)
{
    MMKernelDevice *port;
    MMDevice *device;
    MMPortType ptype;
    MMPortSerialAtFlag pflags = MM_PORT_SERIAL_AT_FLAG_NONE;
    const gchar *subsys;

    port = mm_port_probe_peek_port (probe);
    ptype = mm_port_probe_get_port_type (probe);
    device = mm_port_probe_peek_device (probe);
    subsys = mm_port_probe_get_port_subsys (probe);

    /* Just skip custom port identification for subsys different than tty */
    if (!g_str_equal (subsys, "tty"))
        goto out;

    /* AT#PORTCFG (if supported) can be used for identifying the port layout */
    if (g_object_get_data (G_OBJECT (device), TAG_GETPORTCFG_SUPPORTED) != NULL) {
        guint usbif;

        usbif = mm_kernel_device_get_property_as_int_hex (port, "ID_USB_INTERFACE_NUM");
        if (usbif == GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), TAG_TELIT_MODEM_PORT))) {
            mm_obj_dbg (self, "AT port '%s/%s' flagged as primary",
                        mm_port_probe_get_port_subsys (probe),
                        mm_port_probe_get_port_name (probe));
            pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
        } else if (usbif == GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), TAG_TELIT_AUX_PORT))) {
            mm_obj_dbg (self, "AT port '%s/%s' flagged as secondary",
                        mm_port_probe_get_port_subsys (probe),
                        mm_port_probe_get_port_name (probe));
            pflags = MM_PORT_SERIAL_AT_FLAG_SECONDARY;
        } else if (usbif == GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), TAG_TELIT_NMEA_PORT))) {
            mm_obj_dbg (self, "port '%s/%s' flagged as NMEA",
                        mm_port_probe_get_port_subsys (probe),
                        mm_port_probe_get_port_name (probe));
            ptype = MM_PORT_TYPE_GPS;
        } else
            ptype = MM_PORT_TYPE_IGNORED;
    }

out:
    return mm_base_modem_grab_port (modem,
                                    port,
                                    ptype,
                                    pflags,
                                    error);
}

/*****************************************************************************/
/* Custom init */

typedef struct {
    MMPortSerialAt *port;
    gboolean getportcfg_done;
    guint getportcfg_retries;
    guint port_responsive_retries;
} TelitCustomInitContext;

gboolean
telit_custom_init_finish (MMPortProbe *probe,
                          GAsyncResult *result,
                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void telit_custom_init_step (GTask *task);

static gboolean
cache_port_mode (MMPortProbe *probe,
                 MMDevice    *device,
                 const gchar *reply)
{
    GRegex *r = NULL;
    GRegexCompileFlags flags = G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW;
    GMatchInfo *match_info = NULL;
    GError *error = NULL;
    gboolean ret = FALSE;
    guint portcfg_current;

    /* #PORTCFG: <requested>,<active> */
    r = g_regex_new ("#PORTCFG:\\s*(\\d+),(\\d+)", flags, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, reply, strlen (reply), 0, 0, &match_info, &error))
        goto out;

    if (!mm_get_uint_from_match_info (match_info, 2, &portcfg_current)) {
        mm_obj_dbg (probe, "unrecognized #PORTCFG <active> value");
        goto out;
    }

    /* Reference for port configurations:
     * HE910/UE910/UL865 Families Ports Arrangements User Guide
     * GE910 Family Ports Arrangements User Guide
     */
    switch (portcfg_current) {
    case 0:
    case 1:
    case 4:
    case 5:
    case 7:
    case 9:
    case 10:
    case 11:
        g_object_set_data (G_OBJECT (device), TAG_TELIT_MODEM_PORT, GUINT_TO_POINTER (0x00));
        if (mm_device_get_product (device) == TELIT_GE910_FAMILY_PID)
            g_object_set_data (G_OBJECT (device), TAG_TELIT_AUX_PORT, GUINT_TO_POINTER (0x02));
        else
            g_object_set_data (G_OBJECT (device), TAG_TELIT_AUX_PORT, GUINT_TO_POINTER (0x06));
        break;
    case 2:
    case 3:
    case 6:
        g_object_set_data (G_OBJECT (device), TAG_TELIT_MODEM_PORT, GUINT_TO_POINTER (0x00));
        break;
    case 8:
    case 12:
        g_object_set_data (G_OBJECT (device), TAG_TELIT_MODEM_PORT, GUINT_TO_POINTER (0x00));
        if (mm_device_get_product (device) == TELIT_GE910_FAMILY_PID) {
            g_object_set_data (G_OBJECT (device), TAG_TELIT_AUX_PORT, GUINT_TO_POINTER (0x02));
            g_object_set_data (G_OBJECT (device), TAG_TELIT_NMEA_PORT, GUINT_TO_POINTER (0x04));
        } else {
            g_object_set_data (G_OBJECT (device), TAG_TELIT_AUX_PORT, GUINT_TO_POINTER (0x06));
            g_object_set_data (G_OBJECT (device), TAG_TELIT_NMEA_PORT, GUINT_TO_POINTER (0x0a));
        }
        break;
    default:
        /* portcfg value not supported */
        goto out;
    }
    ret = TRUE;

out:
    g_match_info_free (match_info);
    g_regex_unref (r);
    if (error != NULL) {
        mm_obj_dbg (probe, "error while matching #PORTCFG: %s", error->message);
        g_error_free (error);
    }
    return ret;
}

static void
getportcfg_ready (MMPortSerialAt *port,
                  GAsyncResult *res,
                  GTask *task)
{
    const gchar *response;
    GError *error = NULL;
    MMPortProbe *probe;
    TelitCustomInitContext *ctx;

    ctx = g_task_get_task_data (task);
    probe = g_task_get_source_object (task);

    response = mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        mm_obj_dbg (probe, "couldn't get telit port mode: '%s'", error->message);

        /* If ERROR or COMMAND NOT SUPPORT occur then do not retry the
         * command.
         */
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN))
            ctx->getportcfg_done = TRUE;
    } else {
        MMDevice *device;

        device = mm_port_probe_peek_device (probe);

        /* Results are cached in the parent device object */
        if (g_object_get_data (G_OBJECT (device), TAG_GETPORTCFG_SUPPORTED) == NULL) {
            mm_obj_dbg (probe, "retrieving telit port mode layout");
            if (cache_port_mode (probe, device, response)) {
                g_object_set_data (G_OBJECT (device), TAG_GETPORTCFG_SUPPORTED, GUINT_TO_POINTER (TRUE));
                ctx->getportcfg_done = TRUE;
            }
        }

        /* Port answered to #PORTCFG, so mark it as being AT already */
        mm_port_probe_set_result_at (probe, TRUE);
    }

    if (error)
        g_error_free (error);

    telit_custom_init_step (task);
}

static void
telit_custom_init_context_free (TelitCustomInitContext *ctx)
{
    g_object_unref (ctx->port);
    g_slice_free (TelitCustomInitContext, ctx);
}

static void
telit_custom_init_step (GTask *task)
{
    MMKernelDevice *port;
    MMPortProbe *probe;
    TelitCustomInitContext *ctx;

    ctx = g_task_get_task_data (task);
    probe = g_task_get_source_object (task);

    /* If cancelled, end */
    if (g_cancellable_is_cancelled (g_task_get_cancellable (task))) {
        mm_obj_dbg (probe, "no need to keep on running custom init");
        goto out;
    }

    /* Try to get a port configuration from the modem: usb interface 00
     * is always linked to an AT port
     */
    port = mm_port_probe_peek_port (probe);
    if (!ctx->getportcfg_done &&
        g_strcmp0 (mm_kernel_device_get_property (port, "ID_USB_INTERFACE_NUM"), "00") == 0) {

        if (ctx->getportcfg_retries == 0)
            goto out;
        ctx->getportcfg_retries--;

        mm_port_serial_at_command (
            ctx->port,
            "AT#PORTCFG?",
            2,
            FALSE, /* raw */
            FALSE, /* allow_cached */
            g_task_get_cancellable (task),
            (GAsyncReadyCallback)getportcfg_ready,
            task);
        return;
    }

out:
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void at_ready (MMPortSerialAt *port,
                      GAsyncResult   *res,
                      GTask          *task);

static void
wait_for_ready (GTask *task)
{
    TelitCustomInitContext *ctx;

    ctx = g_task_get_task_data (task);

    if (ctx->port_responsive_retries == 0) {
        telit_custom_init_step (task);
        return;
    }
    ctx->port_responsive_retries--;

    mm_port_serial_at_command (
        ctx->port,
        "AT",
        5,
        FALSE, /* raw */
        FALSE, /* allow_cached */
        g_task_get_cancellable (task),
        (GAsyncReadyCallback)at_ready,
        task);
}

static void
at_ready (MMPortSerialAt *port,
          GAsyncResult   *res,
          GTask          *task)
{
    MMPortProbe       *probe;
    g_autoptr(GError)  error = NULL;

    probe = g_task_get_source_object (task);

    mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        /* On a timeout or send error, wait */
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT) ||
            g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_SEND_FAILED)) {
            wait_for_ready (task);
            return;
        }
        /* On an unknown error, make it fatal */
        if (!mm_serial_parser_v1_is_known_error (error)) {
            mm_obj_warn (probe, "custom port initialization logic failed: %s", error->message);
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
    }

    /* When successful mark the port as AT and continue checking #PORTCFG */
    mm_obj_dbg (probe, "port is AT");
    mm_port_probe_set_result_at (probe, TRUE);
    telit_custom_init_step (task);
}

void
telit_custom_init (MMPortProbe *probe,
                   MMPortSerialAt *port,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    TelitCustomInitContext *ctx;
    GTask *task;
    gboolean wait_needed;

    ctx = g_slice_new (TelitCustomInitContext);
    ctx->port = g_object_ref (port);
    ctx->getportcfg_done = FALSE;
    ctx->getportcfg_retries = 3;
    ctx->port_responsive_retries = TELIT_PORT_CHECK_RETRIES;
    task = g_task_new (probe, cancellable, callback, user_data);
    g_task_set_check_cancellable (task, FALSE);
    g_task_set_task_data (task, ctx, (GDestroyNotify)telit_custom_init_context_free);

    /* Some Telit modems require an initial delay for the ports to be responsive
     * If no explicit tag is present, the modem does not need this step
     * and can directly look for #PORTCFG support */
    wait_needed = mm_kernel_device_get_global_property_as_boolean (mm_port_probe_peek_port (probe),
                                                                   "ID_MM_TELIT_PORT_DELAY");
    if (wait_needed) {
        mm_obj_dbg (probe, "Start polling for port responsiveness");
        wait_for_ready (task);
        return;
    }

    telit_custom_init_step (task);
}
