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
 * Copyright (C) 2012 Ammonit Measurement GmbH
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-bearer-iridium.h"
#include "mm-base-modem-at.h"
#include "mm-utils.h"
#include "mm-log.h"

/* Allow up to 200s to get a proper IP connection */
#define MM_BEARER_IRIDIUM_IP_TIMEOUT_DEFAULT 200

G_DEFINE_TYPE (MMBearerIridium, mm_bearer_iridium, MM_TYPE_BEARER);

/*****************************************************************************/
/* Connect */

typedef struct {
    MMPort *data;
    MMCommonBearerIpConfig *ipv4_config;
    MMCommonBearerIpConfig *ipv6_config;
} ConnectResult;

static void
connect_result_free (ConnectResult *result)
{
    if (result->ipv4_config)
        g_object_unref (result->ipv4_config);
    if (result->ipv6_config)
        g_object_unref (result->ipv6_config);
    g_object_unref (result->data);
    g_free (result);
}

typedef struct {
    MMBearerIridium *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    GError *saved_error;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
connect_finish (MMBearer *self,
                GAsyncResult *res,
                MMPort **data,
                MMCommonBearerIpConfig **ipv4_config,
                MMCommonBearerIpConfig **ipv6_config,
                GError **error)
{
    ConnectResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = (ConnectResult *) g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *data = MM_PORT (g_object_ref (result->data));
    *ipv4_config = (result->ipv4_config ? g_object_ref (result->ipv4_config) : NULL);
    *ipv6_config = (result->ipv6_config ? g_object_ref (result->ipv6_config) : NULL);

    return TRUE;
}

static void
connect_report_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      ConnectContext *ctx)
{
    const gchar *result;

    /* If cancelled, complete */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "Connection setup operation has been cancelled");
        connect_context_complete_and_free (ctx);
        return;
    }

    /* If we got a proper extended reply, build the new error to be set */
    result = mm_base_modem_at_command_finish (modem, res, NULL);
    if (result &&
        g_str_has_prefix (result, "+CEER: ") &&
        strlen (result) > 7) {
        g_simple_async_result_set_error (ctx->result,
                                         ctx->saved_error->domain,
                                         ctx->saved_error->code,
                                         "%s", &result[7]);
        g_error_free (ctx->saved_error);
        ctx->saved_error = NULL;
        connect_context_complete_and_free (ctx);
        return;
    }

    /* Take the original error as it was */
    g_simple_async_result_take_error (ctx->result,
                                      ctx->saved_error);
    ctx->saved_error = NULL;
    connect_context_complete_and_free (ctx);
}

static void
dial_ready (MMBaseModem *modem,
            GAsyncResult *res,
            ConnectContext *ctx)
{
    MMCommonBearerIpConfig *config;
    ConnectResult *result;

    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_finish (modem, res, &(ctx->saved_error));
    if (ctx->saved_error) {
        /* Try to get more information why it failed */
        mm_base_modem_at_command (
            modem,
            "+CEER",
            3,
            FALSE,
            NULL, /* cancellable */
            (GAsyncReadyCallback)connect_report_ready,
            ctx);
        return;
    }

    /* Port is connected; update the state */
    mm_port_set_connected (MM_PORT (mm_base_modem_get_port_primary (modem)), TRUE);

    /* Build IP config; always PPP based */
    config = mm_common_bearer_ip_config_new ();
    mm_common_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_PPP);

    /* Build result */
    result = g_new0 (ConnectResult, 1);
    result->data = g_object_ref (mm_base_modem_get_port_primary (modem));
    result->ipv4_config = config;
    result->ipv6_config = g_object_ref (config);

    /* Set operation result */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               result,
                                               (GDestroyNotify)connect_result_free);
    connect_context_complete_and_free (ctx);
}

static void
connect (MMBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    ConnectContext *ctx;
    MMBaseModem *modem  = NULL;

    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem);

    /* Don't bother to get primary and check if connected and all that; we
     * already do this check when sending the ATDT call */

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_new0 (ConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    /* We just use the default number to dial in the Iridium network. Also note
     * that we won't specify a specific port to use; Iridium modems only expose
     * one. */
    mm_base_modem_at_command (
        modem,
        "ATDT008816000025",
        60,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)dial_ready,
        ctx);

    g_object_unref (modem);
}

/*****************************************************************************/
/* Disconnect */

static gboolean
disconnect_finish (MMBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
primary_flash_ready (MMSerialPort *port,
                     GError *error,
                     GSimpleAsyncResult *result)
{
    if (error) {
        /* Ignore "NO CARRIER" response when modem disconnects and any flash
         * failures we might encounter. Other errors are hard errors.
         */
        if (!g_error_matches (error,
                              MM_CONNECTION_ERROR,
                              MM_CONNECTION_ERROR_NO_CARRIER) &&
            !g_error_matches (error,
                              MM_SERIAL_ERROR,
                              MM_SERIAL_ERROR_FLASH_FAILED)) {
            /* Fatal */
            g_simple_async_result_set_from_error (result, error);
            g_simple_async_result_complete (result);
            g_object_unref (result);
            return;
        }
        mm_dbg ("Port flashing failed (not fatal): %s", error->message);
    }

    /* Port is disconnected; update the state. Note: implementations may
     * already have set the port as disconnected (e.g the 3GPP one) */
    mm_port_set_connected (MM_PORT (port), FALSE);

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete (result);
    g_object_unref (result);
}

static void
disconnect (MMBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMAtSerialPort *primary;
    MMBaseModem *modem = NULL;
    GSimpleAsyncResult *result;

    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);
    primary = mm_base_modem_get_port_primary (modem);
    g_object_unref (modem);

    if (!mm_port_get_connected (MM_PORT (primary))) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect Iridium: this bearer is not connected");
        return;
    }

    /* Just flash the primary port */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        disconnect);
    mm_serial_port_flash (MM_SERIAL_PORT (primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_ready,
                          result);
}

/*****************************************************************************/

static gboolean
cmp_properties (MMBearer *self,
                MMCommonBearerProperties *properties)
{
    return (mm_common_bearer_properties_get_apn (properties) == NULL &&
            mm_common_bearer_properties_get_ip_type (properties) == NULL &&
            mm_common_bearer_properties_get_number (properties) == NULL &&
            mm_common_bearer_properties_get_rm_protocol (properties) == MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN &&
            mm_common_bearer_properties_get_allow_roaming (properties));
}

/*****************************************************************************/

MMBearer *
mm_bearer_iridium_new (MMBroadbandModemIridium *modem)
{
    MMBearer *bearer;

    /* The Iridium bearer inherits from MMBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_get() here */
    bearer = g_object_new (MM_TYPE_BEARER_IRIDIUM,
                           MM_BEARER_MODEM, modem,
                           "ip-timeout", MM_BEARER_IRIDIUM_IP_TIMEOUT_DEFAULT,
                           NULL);

    /* Only export valid bearers */
    mm_bearer_export (bearer);

    return bearer;
}

static void
mm_bearer_iridium_init (MMBearerIridium *self)
{
}

static void
mm_bearer_iridium_class_init (MMBearerIridiumClass *klass)
{
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    /* Virtual methods */
    bearer_class->cmp_properties = cmp_properties;
    bearer_class->connect = connect;
    bearer_class->connect_finish = connect_finish;
    bearer_class->disconnect = disconnect;
    bearer_class->disconnect_finish = disconnect_finish;
}
