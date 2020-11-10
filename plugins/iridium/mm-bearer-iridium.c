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

/* Allow up to 200s to get a proper IP connection */
#define BEARER_IRIDIUM_IP_TIMEOUT_DEFAULT 200

G_DEFINE_TYPE (MMBearerIridium, mm_bearer_iridium, MM_TYPE_BASE_BEARER)

/*****************************************************************************/
/* Connect */

typedef struct {
    MMPortSerialAt *primary;
    GError *saved_error;
} ConnectContext;

static void
connect_context_free (ConnectContext *ctx)
{
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
    if (ctx->primary)
        g_object_unref (ctx->primary);
    g_free (ctx);
}

static MMBearerConnectResult *
connect_finish (MMBaseBearer *self,
                GAsyncResult *res,
                GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
connect_report_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      GTask *task)
{
    ConnectContext *ctx;
    const gchar *result;

    /* If cancelled, complete */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* If we got a proper extended reply, build the new error to be set */
    result = mm_base_modem_at_command_full_finish (modem, res, NULL);
    if (result && g_str_has_prefix (result, "+CEER: ") && strlen (result) > 7) {
        g_task_return_new_error (task,
                                 ctx->saved_error->domain,
                                 ctx->saved_error->code,
                                 "%s", &result[7]);
    } else {
        /* Otherwise, take the original error as it was */
        g_task_return_error (task, ctx->saved_error);
        ctx->saved_error = NULL;
    }
    g_object_unref (task);
}

static void
dial_ready (MMBaseModem *modem,
            GAsyncResult *res,
            GTask *task)
{
    ConnectContext *ctx;
    MMBearerIpConfig *config;

    ctx = g_task_get_task_data (task);

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
            task);
        return;
    }

    /* Port is connected; update the state */
    mm_port_set_connected (MM_PORT (ctx->primary), TRUE);

    /* Build IP config; always PPP based */
    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_PPP);

    /* Return operation result */
    g_task_return_pointer (
        task,
        mm_bearer_connect_result_new (MM_PORT (ctx->primary), config, NULL),
        (GDestroyNotify)mm_bearer_connect_result_unref);
    g_object_unref (task);
    g_object_unref (config);
}

static void
service_type_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    GTask *task)
{
    ConnectContext *ctx;
    GError *error = NULL;

    /* If cancelled, complete */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Errors setting the service type will be critical */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* We just use the default number to dial in the Iridium network. Also note
     * that we won't specify a specific port to use; Iridium modems only expose
     * one. */
    mm_base_modem_at_command_full (
        modem,
        ctx->primary,
        "ATDT008816000025",
        MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)dial_ready,
        task);
}

static void
connect (MMBaseBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    ConnectContext *ctx;
    GTask *task;
    MMBaseModem *modem  = NULL;

    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem);

    /* Don't bother to get primary and check if connected and all that; we
     * already do this check when sending the ATDT call */

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_new0 (ConnectContext, 1);
    ctx->primary = mm_base_modem_get_port_primary (modem);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) connect_context_free);

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
        task);

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
