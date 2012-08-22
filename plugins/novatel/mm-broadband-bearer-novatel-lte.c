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
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-novatel-lte.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-utils.h"

#define CONNECTION_CHECK_TIMEOUT_SEC 5

G_DEFINE_TYPE (MMBroadbandBearerNovatelLte, mm_broadband_bearer_novatel_lte, MM_TYPE_BROADBAND_BEARER);

struct _MMBroadbandBearerNovatelLtePrivate {
    /* timeout id for checking whether we're still connected */
    guint connection_poller;
};

/*****************************************************************************/
/* 3GPP Connection sequence */

typedef struct {
    MMBroadbandBearerNovatelLte *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMPort *data;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    gint retries;
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
    ctx->data = g_object_ref (data);
    /* NOTE:
     * We don't currently support cancelling AT commands, so we'll just check
     * whether the operation is to be cancelled at each step. */
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             detailed_connect_context_new);
    ctx->retries = 4;
    return ctx;
}

static void
detailed_connect_context_complete_and_free (DetailedConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
connect_3gpp_finish (MMBroadbandBearer *self,
                     GAsyncResult *res,
                     MMPort **data,
                     MMBearerIpConfig **ipv4_config,
                     MMBearerIpConfig **ipv6_config,
                     GError **error)
{
    MMBearerIpConfig *config;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    config = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    /* In the default implementation, we assume only IPv4 is supported */
    *ipv4_config = g_object_ref (config);
    *ipv6_config = NULL;
    /* We used the input suggested data port */
    *data = NULL;

    return TRUE;
}

static gboolean connect_3gpp_qmistatus (DetailedConnectContext *ctx);

static void
poll_connection_ready (MMBaseModem *modem,
                       GAsyncResult *res,
                       MMBroadbandBearerNovatelLte *bearer)
{
    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_finish (modem, res, &error);
    if (!result) {
        mm_warn ("QMI connection status failed: %s", error->message);
        g_error_free (error);
        return;
    }

    result = mm_strip_tag (result, "$NWQMISTATUS:");
    if (g_strrstr (result, "QMI State: DISCONNECTED")) {
        mm_bearer_report_disconnection (MM_BEARER (bearer));
        g_source_remove (bearer->priv->connection_poller);
        bearer->priv->connection_poller = 0;
    }
}

static gboolean
poll_connection (MMBroadbandBearerNovatelLte *bearer)
{
    MMBaseModem *modem = NULL;

    g_object_get (MM_BEARER (bearer),
                  MM_BEARER_MODEM, &modem,
                  NULL);
    mm_base_modem_at_command (
        modem,
        "$NWQMISTATUS",
        3,
        FALSE,
        (GAsyncReadyCallback)poll_connection_ready,
        bearer);
    g_object_unref (modem);

    return TRUE;
}

static void
connect_3gpp_qmistatus_ready (MMBaseModem *modem,
                              GAsyncResult *res,
                              DetailedConnectContext *ctx)
{
    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (ctx->modem),
                                              res,
                                              &error);
    if (!result) {
        mm_warn ("QMI connection status failed: %s", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    result = mm_strip_tag (result, "$NWQMISTATUS:");
    if (g_strrstr (result, "QMI State: CONNECTED")) {
        MMBearerIpConfig *config;

        mm_dbg("Connected");
        ctx->self->priv->connection_poller = g_timeout_add_seconds (CONNECTION_CHECK_TIMEOUT_SEC,
                                                                    (GSourceFunc)poll_connection,
                                                                    ctx->self);
        config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_DHCP);
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   config,
                                                   (GDestroyNotify)g_object_unref);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    mm_dbg ("Error: '%s'", result);
    if (ctx->retries > 0) {
        ctx->retries--;
        mm_dbg ("Retrying status check in a second. %d retries left.",
                ctx->retries);
        g_timeout_add_seconds (1, (GSourceFunc)connect_3gpp_qmistatus, ctx);
        return;
    }

    /* Already exhausted all retries */
    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "%s", result);
    detailed_connect_context_complete_and_free (ctx);
}

static gboolean
connect_3gpp_qmistatus (DetailedConnectContext *ctx)
{
    mm_base_modem_at_command (
        ctx->modem,
        "$NWQMISTATUS",
        3, /* timeout */
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)connect_3gpp_qmistatus_ready, /* callback */
        ctx); /* user_data */

    return FALSE;
}

static void
connect_3gpp_qmiconnect_ready (MMBaseModem *modem,
                               GAsyncResult *res,
                               DetailedConnectContext *ctx)
{
    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (modem),
                                              res,
                                              &error);
    if (!result) {
        mm_warn ("QMI connection failed: %s", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /*
     * The connection takes a bit of time to set up, but there's no
     * asynchronous notification from the modem when this has
     * happened. Instead, we need to poll the modem to see if it's
     * ready.
     */
    g_timeout_add_seconds (1, (GSourceFunc)connect_3gpp_qmistatus, ctx);
}

static void
connect_3gpp (MMBroadbandBearer *bearer,
              MMBroadbandModem *modem,
              MMAtSerialPort *primary,
              MMAtSerialPort *secondary,
              MMPort *data,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    DetailedConnectContext *ctx;
    gchar *command, *apn, *user, *password;
    MMBearerProperties *config;

    ctx = detailed_connect_context_new (bearer,
                                        modem,
                                        primary,
                                        data,
                                        cancellable,
                                        callback,
                                        user_data);

    config = mm_bearer_peek_config (MM_BEARER (bearer));
    apn = mm_at_serial_port_quote_string (mm_bearer_properties_get_apn (config));
    user = mm_at_serial_port_quote_string (mm_bearer_properties_get_user (config));
    password = mm_at_serial_port_quote_string (mm_bearer_properties_get_password (config));
    command = g_strdup_printf ("$NWQMICONNECT=,,,,,,%s,,,%s,%s",
                               apn, user, password);
    g_free (apn);
    g_free (user);
    g_free (password);
    mm_base_modem_at_command (
        ctx->modem,
        command,
        10, /* timeout */
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)connect_3gpp_qmiconnect_ready,
        ctx); /* user_data */
    g_free (command);
}

/*****************************************************************************/
/* 3GPP Disonnection sequence */

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

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disconnect_3gpp_status_complete (MMBaseModem *modem,
                                 GAsyncResult *res,
                                 DetailedDisconnectContext *ctx)
{
    const gchar *result;
    GError *error = NULL;

    result = mm_base_modem_at_command_finish (MM_BASE_MODEM (modem),
                                              res,
                                              &error);
    if (error) {
        mm_dbg("QMI connection status failed: %s", error->message);
        g_error_free (error);
    }

    result = mm_strip_tag (result, "$NWQMISTATUS:");
    if (result && g_strrstr (result, "QMI State: DISCONNECTED"))
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    else
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Error checking if disconnected (%s)",
                                         result ? result : "no result");

    detailed_disconnect_context_complete_and_free (ctx);
}

static void
disconnect_3gpp_check_status (MMBaseModem *modem,
                              GAsyncResult *res,
                              DetailedDisconnectContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (modem),
                                     res,
                                     &error);
    if (error) {
        mm_dbg("Disconnection error: %s", error->message);
        g_error_free (error);
    }

    mm_base_modem_at_command (
        ctx->modem,
        "$NWQMISTATUS",
        3, /* timeout */
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)disconnect_3gpp_status_complete,
        ctx); /* user_data */
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
    MMBroadbandBearerNovatelLte *bearer = MM_BROADBAND_BEARER_NOVATEL_LTE (self);

    if (bearer->priv->connection_poller) {
        g_source_remove (bearer->priv->connection_poller);
        bearer->priv->connection_poller = 0;
    }

    ctx = detailed_disconnect_context_new (self, modem, primary, secondary,
                                           data, callback, user_data);

    mm_base_modem_at_command (
        ctx->modem,
        "$NWQMIDISCONNECT",
        10, /* timeout */
        FALSE, /* allow_cached */
        (GAsyncReadyCallback)disconnect_3gpp_check_status,
        ctx); /* user_data */
}

/*****************************************************************************/

MMBearer *
mm_broadband_bearer_novatel_lte_new_finish (GAsyncResult *res,
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
mm_broadband_bearer_novatel_lte_new (MMBroadbandModemNovatelLte *modem,
                                     MMBearerProperties *config,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_NOVATEL_LTE,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM, modem,
        MM_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_novatel_lte_init (MMBroadbandBearerNovatelLte *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER_NOVATEL_LTE,
                                              MMBroadbandBearerNovatelLtePrivate);

    self->priv->connection_poller = 0;
}

static void
finalize (GObject *object)
{
    MMBroadbandBearerNovatelLte *self = MM_BROADBAND_BEARER_NOVATEL_LTE (object);

    if (self->priv->connection_poller)
        g_source_remove (self->priv->connection_poller);

    G_OBJECT_CLASS (mm_broadband_bearer_novatel_lte_parent_class)->finalize (object);
}

static void
mm_broadband_bearer_novatel_lte_class_init (MMBroadbandBearerNovatelLteClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerNovatelLtePrivate));

    object_class->finalize = finalize;

    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
