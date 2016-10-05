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
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-bearer-iridium.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"

/* Allow up to 200s to get a proper IP connection */
#define BEARER_IRIDIUM_IP_TIMEOUT_DEFAULT 200

G_DEFINE_TYPE (MMBearerIridium, mm_bearer_iridium, MM_TYPE_BASE_BEARER);

/*****************************************************************************/
/* Connect */

typedef struct {
    MMBearerIridium *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMPortSerialAt *primary;
    GError *saved_error;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
    if (ctx->primary)
        g_object_unref (ctx->primary);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static MMBearerConnectResult *
connect_finish (MMBaseBearer *self,
                GAsyncResult *res,
                GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
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
    result = mm_base_modem_at_command_full_finish (modem, res, NULL);
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
    MMBearerIpConfig *config;

    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_full_finish (modem, res, &(ctx->saved_error));
    if (ctx->saved_error) {
        /* Try to get more information why it failed */
        mm_base_modem_at_command_full (
            modem,
            ctx->primary,
            "+CEER",
            3,
            FALSE,
            FALSE, /* raw */
            NULL, /* cancellable */
            (GAsyncReadyCallback)connect_report_ready,
            ctx);
        return;
    }

    /* Port is connected; update the state */
    mm_port_set_connected (MM_PORT (ctx->primary), TRUE);

    /* Build IP config; always PPP based */
    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_PPP);

    /* Set operation result */
    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        mm_bearer_connect_result_new (MM_PORT (ctx->primary), config, NULL),
        (GDestroyNotify)mm_bearer_connect_result_unref);
    g_object_unref (config);

    connect_context_complete_and_free (ctx);
}

static void
service_type_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    ConnectContext *ctx)
{
    GError *error = NULL;

    /* If cancelled, complete */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "Connection setup operation has been cancelled");
        connect_context_complete_and_free (ctx);
        return;
    }

    /* Errors setting the service type will be critical */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    /* We just use the default number to dial in the Iridium network. Also note
     * that we won't specify a specific port to use; Iridium modems only expose
     * one. */
    mm_base_modem_at_command_full (
        modem,
        ctx->primary,
        "ATDT008816000025",
        60,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)dial_ready,
        ctx);
}

static void
connect (MMBaseBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    ConnectContext *ctx;
    MMBaseModem *modem  = NULL;

    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem);

    /* Don't bother to get primary and check if connected and all that; we
     * already do this check when sending the ATDT call */

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_new0 (ConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->primary = mm_base_modem_get_port_primary (modem);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    /* Bearer service type set to 9600bps (V.110), which behaves better than the
     * default 9600bps (V.32). */
    mm_base_modem_at_command_full (
        modem,
        ctx->primary,
        "+CBST=71,0,1",
        3,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)service_type_ready,
        ctx);

    g_object_unref (modem);
}

/*****************************************************************************/

MMBaseBearer *
mm_bearer_iridium_new (MMBroadbandModemIridium *modem,
                       MMBearerProperties *config)
{
    MMBaseBearer *bearer;

    /* The Iridium bearer inherits from MMBaseBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_get() here */
    bearer = g_object_new (MM_TYPE_BEARER_IRIDIUM,
                           MM_BASE_BEARER_MODEM, modem,
                           MM_BASE_BEARER_CONFIG, config,
                           "ip-timeout", BEARER_IRIDIUM_IP_TIMEOUT_DEFAULT,
                           NULL);

    /* Only export valid bearers */
    mm_base_bearer_export (bearer);

    return bearer;
}

static void
mm_bearer_iridium_init (MMBearerIridium *self)
{
}

static void
mm_bearer_iridium_class_init (MMBearerIridiumClass *klass)
{
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);

    /* Virtual methods */
    base_bearer_class->connect = connect;
    base_bearer_class->connect_finish = connect_finish;
    base_bearer_class->load_connection_status = NULL;
    base_bearer_class->load_connection_status_finish = NULL;
}
