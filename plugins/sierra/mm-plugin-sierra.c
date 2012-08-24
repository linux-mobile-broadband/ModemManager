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
#include <gmodule.h>

#include <libmm-common.h>

#include "mm-log.h"
#include "mm-plugin-sierra.h"
#include "mm-broadband-modem-sierra.h"
#include "mm-broadband-modem-sierra-icera.h"

G_DEFINE_TYPE (MMPluginSierra, mm_plugin_sierra, MM_TYPE_PLUGIN)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

/*****************************************************************************/
/* Custom init */

#define TAG_SIERRA_APP1_PORT   "sierra-app1-port"
#define TAG_SIERRA_APP_PPP_OK  "sierra-app-ppp-ok"

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
        /* Just retry... */
        sierra_custom_init_step (ctx);
        return;
    }

    /* A valid reply to +GCAP tells us this is an AT port already */
    mm_port_probe_set_result_at (ctx->probe, TRUE);

    if (strstr (response->str, "APP1")) {
        g_object_set_data (G_OBJECT (ctx->probe), TAG_SIERRA_APP1_PORT, GUINT_TO_POINTER (TRUE));

        /* 885 can handle PPP on the APP ports, leaving the primary port open
         * for command and status while connected.  Older modems (ie 8775) say
         * they can but fail during PPP.
         */
        if (strstr (response->str, "C885"))
            g_object_set_data (G_OBJECT (ctx->probe), TAG_SIERRA_APP_PPP_OK, GUINT_TO_POINTER (TRUE));
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    sierra_custom_init_context_complete_and_free (ctx);
}

static void
sierra_custom_init_step (SierraCustomInitContext *ctx)
{
    /* If cancelled, end */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        mm_dbg ("(Sierra) no need to keep on running custom init in (%s)",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "Custom initialization cancelled");
        sierra_custom_init_context_complete_and_free (ctx);
        return;
    }

    if (ctx->retries == 0) {
        mm_dbg ("(Sierra) Couldn't get port type hints");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        sierra_custom_init_context_complete_and_free (ctx);
        return;
    }

    ctx->retries--;
    mm_at_serial_port_queue_command (
        ctx->port,
        "AT+GCAP",
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

static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *sysfs_path,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    GList *l;
    gboolean is_icera;

    for (l = probes, is_icera = FALSE; l && !is_icera; l = g_list_next (l)) {
        if (mm_port_probe_is_icera (MM_PORT_PROBE (l->data)))
            is_icera = TRUE;
    }

    if (is_icera)
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
    if (g_object_get_data (G_OBJECT (probe), TAG_SIERRA_APP1_PORT)) {
        if (g_object_get_data (G_OBJECT (probe), TAG_SIERRA_APP_PPP_OK))
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
    static const gchar *subsystems[] = { "tty", "net", NULL };
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
                      MM_PLUGIN_CUSTOM_INIT,        &custom_init,
                      MM_PLUGIN_ICERA_PROBE,        TRUE,
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
