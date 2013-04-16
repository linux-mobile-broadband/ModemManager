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
 * Copyright (C) 2012 Lanedo GmbH
 */

#include <string.h>
#include <stdlib.h>
#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-plugin-sierra.h"
#include "mm-broadband-modem-sierra.h"
#include "mm-broadband-modem-sierra-icera.h"

#if defined WITH_QMI
#include "mm-broadband-modem-qmi.h"
#endif

#if defined WITH_MBIM
#include "mm-broadband-modem-mbim.h"
#endif

G_DEFINE_TYPE (MMPluginSierra, mm_plugin_sierra, MM_TYPE_PLUGIN)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/
/* Custom init */

#define TAG_SIERRA_APP_PORT       "sierra-app-port"
#define TAG_SIERRA_APP1_PPP_OK    "sierra-app1-ppp-ok"

typedef struct {
    MMPortProbe *probe;
    MMAtSerialPort *port;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    guint retries;
} SierraCustomInitContext;

static void
sierra_custom_init_context_complete_and_free (SierraCustomInitContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->port);
    g_object_unref (ctx->probe);
    g_object_unref (ctx->result);
    g_slice_free (SierraCustomInitContext, ctx);
}

static gboolean
sierra_custom_init_finish (MMPortProbe *probe,
                           GAsyncResult *result,
                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void sierra_custom_init_step (SierraCustomInitContext *ctx);

static void
gcap_ready (MMAtSerialPort *port,
            GString *response,
            GError *error,
            SierraCustomInitContext *ctx)
{
    if (error) {
        /* If consumed all tries and the last error was a timeout, assume the
         * port is not AT */
        if (ctx->retries == 0 &&
            g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            mm_port_probe_set_result_at (ctx->probe, FALSE);
        }
        /* If reported a hard parse error, this port is definitely not an AT
         * port, skip trying. */
        else if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_PARSE_FAILED)) {
            mm_port_probe_set_result_at (ctx->probe, FALSE);
            ctx->retries = 0;
        }

        /* Just retry... */
        sierra_custom_init_step (ctx);
        return;
    }

    /* A valid reply to ATI tells us this is an AT port already */
    mm_port_probe_set_result_at (ctx->probe, TRUE);

    /* Sierra APPx ports have limited AT command parsers that just reply with
     * "OK" to most commands.  These can sometimes be used for PPP while the
     * main port is used for status and control, but older modems tend to crash
     * or fail PPP.  So we whitelist modems that are known to allow PPP on the
     * secondary APP ports.
     */
    if (strstr (response->str, "APP1")) {
        g_object_set_data (G_OBJECT (ctx->probe), TAG_SIERRA_APP_PORT, GUINT_TO_POINTER (TRUE));

        /* PPP-on-APP1-port whitelist */
        if (strstr (response->str, "C885") || strstr (response->str, "USB 306") || strstr (response->str, "MC8790"))
            g_object_set_data (G_OBJECT (ctx->probe), TAG_SIERRA_APP1_PPP_OK, GUINT_TO_POINTER (TRUE));

        /* For debugging: let users figure out if their device supports PPP
         * on the APP1 port or not.
         */
        if (getenv ("MM_SIERRA_APP1_PPP_OK")) {
            mm_dbg ("Sierra: APP1 PPP OK '%s'", response->str);
            g_object_set_data (G_OBJECT (ctx->probe), TAG_SIERRA_APP1_PPP_OK, GUINT_TO_POINTER (TRUE));
        }
    } else if (strstr (response->str, "APP2") ||
               strstr (response->str, "APP3") ||
               strstr (response->str, "APP4")) {
        /* Additional APP ports don't support most AT commands, so they cannot
         * be used as the primary port.
         */
        g_object_set_data (G_OBJECT (ctx->probe), TAG_SIERRA_APP_PORT, GUINT_TO_POINTER (TRUE));
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    sierra_custom_init_context_complete_and_free (ctx);
}

static void
sierra_custom_init_step (SierraCustomInitContext *ctx)
{
    /* If cancelled, end */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        mm_dbg ("(Sierra) no need to keep on running custom init in '%s'",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        sierra_custom_init_context_complete_and_free (ctx);
        return;
    }

    if (ctx->retries == 0) {
        mm_dbg ("(Sierra) Couldn't get port type hints from '%s'",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        sierra_custom_init_context_complete_and_free (ctx);
        return;
    }

    ctx->retries--;
    mm_at_serial_port_queue_command (
        ctx->port,
        "ATI",
        3,
        FALSE, /* raw */
        ctx->cancellable,
        (MMAtSerialResponseFn)gcap_ready,
        ctx);
}

static void
sierra_custom_init (MMPortProbe *probe,
                    MMAtSerialPort *port,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    SierraCustomInitContext *ctx;

    ctx = g_slice_new (SierraCustomInitContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (probe),
                                             callback,
                                             user_data,
                                             sierra_custom_init);
    ctx->probe = g_object_ref (probe);
    ctx->port = g_object_ref (port);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
    ctx->retries = 3;

    sierra_custom_init_step (ctx);
}

/*****************************************************************************/

static gboolean
sierra_port_probe_list_is_icera (GList *probes)
{
    GList *l;

    for (l = probes; l; l = g_list_next (l)) {
        /* Only assume the Icera probing check is valid IF the port is not
         * secondary. This will skip the stupid ports which reply OK to every
         * AT command, even the one we use to check for Icera support */
        if (mm_port_probe_is_icera (MM_PORT_PROBE (l->data)) &&
            !g_object_get_data (G_OBJECT (l->data), TAG_SIERRA_APP_PORT))
            return TRUE;
    }

    return FALSE;
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
#if defined WITH_QMI
    if (mm_port_probe_list_has_qmi_port (probes)) {
        mm_dbg ("QMI-powered Sierra modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_qmi_new (sysfs_path,
                                                          drivers,
                                                          mm_plugin_get_name (self),
                                                          vendor,
                                                          product));
    }
#endif

#if defined WITH_MBIM
    if (mm_port_probe_list_has_mbim_port (probes)) {
        mm_dbg ("MBIM-powered Sierra modem found...");
        return MM_BASE_MODEM (mm_broadband_modem_mbim_new (sysfs_path,
                                                           drivers,
                                                           mm_plugin_get_name (self),
                                                           vendor,
                                                           product));
    }
#endif

    if (sierra_port_probe_list_is_icera (probes))
        return MM_BASE_MODEM (mm_broadband_modem_sierra_icera_new (sysfs_path,
                                                                   drivers,
                                                                   mm_plugin_get_name (self),
                                                                   vendor,
                                                                   product));

    return MM_BASE_MODEM (mm_broadband_modem_sierra_new (sysfs_path,
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
    MMAtPortFlag pflags = MM_AT_PORT_FLAG_NONE;
    MMPortType ptype;

    ptype = mm_port_probe_get_port_type (probe);

    /* Is it a GSM secondary port? */
    if (g_object_get_data (G_OBJECT (probe), TAG_SIERRA_APP_PORT)) {
        if (g_object_get_data (G_OBJECT (probe), TAG_SIERRA_APP1_PPP_OK))
            pflags = MM_AT_PORT_FLAG_PPP;
        else
            pflags = MM_AT_PORT_FLAG_SECONDARY;
    } else if (ptype == MM_PORT_TYPE_AT)
        pflags = MM_AT_PORT_FLAG_PRIMARY;

    return mm_base_modem_grab_port (modem,
                                    mm_port_probe_get_port_subsys (probe),
                                    mm_port_probe_get_port_name (probe),
                                    ptype,
                                    pflags,
                                    error);
}

/*****************************************************************************/

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", "usb", NULL };
    static const gchar *drivers[] = { "sierra", "sierra_net", NULL };
    static const MMAsyncMethod custom_init = {
        .async  = G_CALLBACK (sierra_custom_init),
        .finish = G_CALLBACK (sierra_custom_init_finish),
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_SIERRA,
                      MM_PLUGIN_NAME,               "Sierra",
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
                      MM_PLUGIN_ALLOWED_DRIVERS,    drivers,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
                      MM_PLUGIN_ALLOWED_QCDM,       TRUE,
                      MM_PLUGIN_ALLOWED_QMI,        TRUE,
                      MM_PLUGIN_ALLOWED_MBIM,       TRUE,
                      MM_PLUGIN_CUSTOM_INIT,        &custom_init,
                      MM_PLUGIN_ICERA_PROBE,        TRUE,
                      MM_PLUGIN_REMOVE_ECHO,        FALSE,
                      NULL));
}

static void
mm_plugin_sierra_init (MMPluginSierra *self)
{
}

static void
mm_plugin_sierra_class_init (MMPluginSierraClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);

    plugin_class->create_modem = create_modem;
    plugin_class->grab_port = grab_port;
}
