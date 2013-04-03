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
 * Copyright (C) 2013 Altair Semiconductor
 *
 * Author: Ori Inbar <ori.inbar@altair-semi.com>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-altair-lte.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

#define CONNECTION_CHECK_TIMEOUT_SEC 5
#define STATCM_TAG "%STATCM:"

G_DEFINE_TYPE (MMBroadbandBearerAltairLte, mm_broadband_bearer_altair_lte, MM_TYPE_BROADBAND_BEARER);

/*****************************************************************************/
/* 3GPP Connect sequence */

typedef struct {
    MMBroadbandBearerAltairLte *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMPort *data;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
} DetailedConnectContext;

static DetailedConnectContext *
detailed_connect_context_new (MMBroadbandBearer *self,
                              MMBroadbandModem *modem,
                              MMAtSerialPort *primary,
                              MMPort *data,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    DetailedConnectContext *ctx;

    ctx = g_new0 (DetailedConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->data = data;
    /* NOTE:
     * We don't currently support cancelling AT commands, so we'll just check
     * whether the operation is to be cancelled at each step. */
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             detailed_connect_context_new);
    return ctx;
}

static void
detailed_connect_context_complete_and_free (DetailedConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    if (ctx->data)
        g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static MMBearerConnectResult *
connect_3gpp_finish (MMBroadbandBearer *self,
                     GAsyncResult *res,
                     GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
connect_3gpp_connect_ready (MMBaseModem *modem,
                            GAsyncResult *res,
                            DetailedConnectContext *ctx)
{
    const gchar *result;
    GError *error = NULL;
    MMBearerIpConfig *config;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (modem),
                                              res,
                                              &error);
    if (!result) {
        mm_warn ("connect failed: %s", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    mm_dbg ("Connected");

    config = mm_bearer_ip_config_new ();

    mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_DHCP);

    /* Set operation result */
    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        mm_bearer_connect_result_new (ctx->data,
                                      config,
                                      config),
        (GDestroyNotify)mm_bearer_connect_result_unref);

    g_object_unref (config);

    detailed_connect_context_complete_and_free (ctx);
}

static void
connect_3gpp_apnsettings_ready (MMBaseModem *modem,
                                GAsyncResult *res,
                                DetailedConnectContext *ctx)
{
    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (modem),
                                              res,
                                              &error);
    if (!result) {
        mm_warn ("setting APN failed: %s", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    mm_dbg ("APN set - connecting bearer");
    mm_base_modem_at_command (
        ctx->modem,
        "%DPDNACT=1",
        10, /* timeout */
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)connect_3gpp_connect_ready, /* callback */
        ctx); /* user_data */
}

static void
connect_3gpp (MMBroadbandBearer *bearer,
              MMBroadbandModem *modem,
              MMAtSerialPort *primary,
              MMAtSerialPort *secondary,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    DetailedConnectContext *ctx;
    gchar *command, *apn;
    MMBearerProperties *config;

    ctx = detailed_connect_context_new (
        bearer,
        modem,
        primary,
        /* Get a 'net' data port */
        mm_base_modem_get_best_data_port (MM_BASE_MODEM (modem),
                                          MM_PORT_TYPE_NET),
        cancellable,
        callback,
        user_data);

    if (!ctx->data) {
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: no available net port available");
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    config = mm_bearer_peek_config (MM_BEARER (bearer));
    apn = mm_at_serial_port_quote_string (mm_bearer_properties_get_apn (config));
    command = g_strdup_printf ("%%APNN=%s",apn);
    g_free (apn);
    mm_base_modem_at_command (
        ctx->modem,
        command,
        6, /* timeout */
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)connect_3gpp_apnsettings_ready,
        ctx); /* user_data */
    g_free (command);
}

/*****************************************************************************/
/* 3GPP Disconnect sequence */

typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMPort *data;
    GSimpleAsyncResult *result;
} DetailedDisconnectContext;

static DetailedDisconnectContext *
detailed_disconnect_context_new (MMBroadbandBearer *self,
                                 MMBroadbandModem *modem,
                                 MMAtSerialPort *primary,
                                 MMAtSerialPort *secondary,
                                 MMPort *data,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DetailedDisconnectContext *ctx;

    ctx = g_new0 (DetailedDisconnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->secondary = (secondary ? g_object_ref (secondary) : NULL);
    ctx->data = g_object_ref (data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             detailed_disconnect_context_new);
    return ctx;
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
detailed_disconnect_context_complete_and_free (DetailedDisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
disconnect_3gpp_check_status (MMBaseModem *modem,
                              GAsyncResult *res,
                              DetailedDisconnectContext *ctx)
{

    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (modem),
                                              res,
                                              &error);
    if (!result) {
        mm_warn ("Disconnect failed: %s", error->message);
        g_simple_async_result_take_error (ctx->result, error);
    }
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    detailed_disconnect_context_complete_and_free (ctx);
}

static void
disconnect_3gpp (MMBroadbandBearer *self,
                 MMBroadbandModem *modem,
                 MMAtSerialPort *primary,
                 MMAtSerialPort *secondary,
                 MMPort *data,
                 guint cid,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    DetailedDisconnectContext *ctx;

    ctx = detailed_disconnect_context_new (self, modem, primary, secondary,
                                           data, callback, user_data);

    mm_base_modem_at_command (
        ctx->modem,
        "%DPDNACT=0",
        10, /* timeout */
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)disconnect_3gpp_check_status,
        ctx); /* user_data */
}

/*****************************************************************************/

MMBearer *
mm_broadband_bearer_altair_lte_new_finish (GAsyncResult *res,
                                           GError **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_bearer_export (MM_BEARER (bearer));

    return MM_BEARER (bearer);
}

void
mm_broadband_bearer_altair_lte_new (MMBroadbandModemAltairLte *modem,
                                    MMBearerProperties *config,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_ALTAIR_LTE,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM, modem,
        MM_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_altair_lte_init (MMBroadbandBearerAltairLte *self)
{

}

static void
mm_broadband_bearer_altair_lte_class_init (MMBroadbandBearerAltairLteClass *klass)
{
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
