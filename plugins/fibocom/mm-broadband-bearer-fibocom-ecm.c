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
 * Copyright (C) 2022 Disruptive Technologies Research AS
 */

#include <config.h>

#include "mm-broadband-bearer-fibocom-ecm.h"
#include "mm-broadband-modem-fibocom.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBroadbandBearerFibocomEcm, mm_broadband_bearer_fibocom_ecm, MM_TYPE_BROADBAND_BEARER)

/*****************************************************************************/
/* Common helper functions                                                   */

static gboolean
parse_gtrndis_read_response (const gchar *response,
                             guint       *state,
                             guint       *cid,
                             GError     **error)
{
    g_autoptr(GRegex)     r = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;

    r = g_regex_new ("\\+GTRNDIS:\\s*(\\d+)(?:,(\\d+))?",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match (r, response, 0, &match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Invalid +GTRNDIS response: %s", response);
        return FALSE;
    }
    if (!mm_get_uint_from_match_info (match_info, 1, state)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Failed to match state in +GTRNDIS response: %s", response);
        return FALSE;
    }

    if (*state) {
        if (!mm_get_uint_from_match_info (match_info, 2, cid)) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Failed to match cid in +GTRNDIS response: %s", response);
            return FALSE;
        }
    } else {
        *cid = 0;
    }

    return TRUE;
}

/*****************************************************************************/
/* Connection status monitoring                                              */

static MMBearerConnectionStatus
load_connection_status_finish (MMBaseBearer  *bearer,
                               GAsyncResult  *res,
                               GError       **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_BEARER_CONNECTION_STATUS_UNKNOWN;
    }
    return (MMBearerConnectionStatus) value;
}

static void
gtrndis_query_ready (MMBaseModem  *modem,
                     GAsyncResult *res,
                     GTask        *task)
{
    MMBaseBearer *bearer;
    GError       *error = NULL;
    const gchar  *result;
    guint         state;
    guint         cid;

    bearer = g_task_get_source_object (task);
    result = mm_base_modem_at_command_finish (modem, res, &error);
    if (!result)
        g_task_return_error (task, error);
    else if (!parse_gtrndis_read_response (result, &state, &cid, &error))
        g_task_return_error (task, error);
    else if (!state || (gint) cid != mm_base_bearer_get_profile_id (bearer))
        g_task_return_int (task, MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
    else
        g_task_return_int (task, MM_BEARER_CONNECTION_STATUS_CONNECTED);

    g_object_unref (task);
}

static void
load_connection_status (MMBaseBearer        *bearer,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    GTask       *task;
    MMBaseModem *modem = NULL;

    task = g_task_new (bearer, NULL, callback, user_data);

    g_object_get (MM_BASE_BEARER (bearer),
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);

    mm_base_modem_at_command (modem,
                              "+GTRNDIS?",
                              3,
                              FALSE,
                              (GAsyncReadyCallback) gtrndis_query_ready,
                              task);
    g_object_unref (modem);
}

/*****************************************************************************/
/* 3GPP Connect                                                              */

typedef struct {
    MMBroadbandModem *modem;
    MMPortSerialAt   *primary;
    MMPortSerialAt   *secondary;
    MMBearerIpFamily  ip_family;
} ConnectContext;

static void
connect_context_free (ConnectContext *ctx)
{
    g_clear_object (&ctx->modem);
    g_clear_object (&ctx->primary);
    g_clear_object (&ctx->secondary);
    g_slice_free (ConnectContext, ctx);
}

static MMBearerConnectResult *
connect_3gpp_finish (MMBroadbandBearer  *self,
                     GAsyncResult       *res,
                     GError            **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_connect_3gpp_ready (MMBroadbandBearer *self,
                           GAsyncResult      *res,
                           GTask             *task)
{
    GError                *error = NULL;
    MMBearerConnectResult *result;

    result = MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_fibocom_ecm_parent_class)->connect_3gpp_finish (self, res, &error);
    if (result)
        g_task_return_pointer (task, result, (GDestroyNotify) mm_bearer_connect_result_unref);
    else
        g_task_return_error (task, error);
    g_object_unref (task);
}

static void
disconnect_3gpp_ready (MMBroadbandBearer *self,
                       GAsyncResult      *res,
                       GTask             *task)
{
    GError         *error = NULL;
    gboolean        result;
    ConnectContext *ctx;

    result = MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp_finish (self, res, &error);
    if (!result) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_fibocom_ecm_parent_class)->connect_3gpp (
        self,
        ctx->modem,
        ctx->primary,
        ctx->secondary,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback) parent_connect_3gpp_ready,
        task);
}

static void
gtrndis_check_ready (MMBaseModem  *modem,
                     GAsyncResult *res,
                     GTask        *task)
{
    MMBroadbandBearer *self;
    ConnectContext    *ctx;
    GError            *error = NULL;
    const gchar       *response;
    guint              state;
    guint              cid;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!parse_gtrndis_read_response (response, &state, &cid, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (state) {
        /* RNDIS is already active, disconnect first. */
        mm_obj_dbg (self, "RNDIS active, tearing down existing connection...");
        MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp (
            MM_BROADBAND_BEARER (self),
            ctx->modem,
            ctx->primary,
            ctx->secondary,
            NULL, /* data port */
            cid,
            (GAsyncReadyCallback) disconnect_3gpp_ready,
            task);
        return;
    }

    /* Execute the regular connection flow if RNDIS is inactive. */
    mm_obj_dbg (self, "RNDIS inactive");
    MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_fibocom_ecm_parent_class)->connect_3gpp (
        MM_BROADBAND_BEARER (self),
        ctx->modem,
        ctx->primary,
        ctx->secondary,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback) parent_connect_3gpp_ready,
        task);
}

static void
connect_3gpp (MMBroadbandBearer   *self,
              MMBroadbandModem    *modem,
              MMPortSerialAt      *primary,
              MMPortSerialAt      *secondary,
              GCancellable        *cancellable,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    ConnectContext *ctx;
    GTask          *task;

    ctx            = g_slice_new0 (ConnectContext);
    ctx->modem     = g_object_ref (modem);
    ctx->primary   = g_object_ref (primary);
    ctx->secondary = secondary ? g_object_ref (secondary) : NULL;
    ctx->ip_family = mm_bearer_properties_get_ip_type (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
    mm_3gpp_normalize_ip_family (&ctx->ip_family);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) connect_context_free);

    /* First, we must check whether RNDIS is already active */
    mm_base_modem_at_command (MM_BASE_MODEM (modem),
                              "+GTRNDIS?",
                              3,
                              FALSE, /* allow_cached */
                              (GAsyncReadyCallback) gtrndis_check_ready,
                              task);
}

/*****************************************************************************/
/* Dial context and task                                                     */

typedef struct {
    MMBroadbandModem *modem;
    MMPortSerialAt   *primary;
    guint             cid;
    MMPort           *data;
} DialContext;

static void
dial_task_free (DialContext *ctx)
{
    g_object_unref (ctx->modem);
    g_object_unref (ctx->primary);
    if (ctx->data)
        g_object_unref (ctx->data);
    g_slice_free (DialContext, ctx);
}

static GTask *
dial_task_new (MMBroadbandBearerFibocomEcm *self,
               MMBroadbandModem         *modem,
               MMPortSerialAt           *primary,
               guint                     cid,
               GCancellable             *cancellable,
               GAsyncReadyCallback       callback,
               gpointer                  user_data)
{
    DialContext *ctx;
    GTask       *task;

    ctx          = g_slice_new0 (DialContext);
    ctx->modem   = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid     = cid;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) dial_task_free);

    ctx->data = mm_base_modem_get_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!ctx->data) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "No valid data port found to launch connection");
        g_object_unref (task);
        return NULL;
    }

    return task;
}

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence)                   */

static MMPort *
dial_3gpp_finish (MMBroadbandBearer *self,
                  GAsyncResult *res,
                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
gtrndis_verify_ready (MMBaseModem  *modem,
                      GAsyncResult *res,
                      GTask        *task)
{
    DialContext *ctx;
    GError      *error = NULL;
    const gchar *response;

    ctx = g_task_get_task_data (task);
    response = mm_base_modem_at_command_finish (modem, res, &error);

    if (!response)
        g_task_return_error (task, error);
    else {
        response = mm_strip_tag (response, "+GTRNDIS:");
        if (strtol (response, NULL, 10) != 1)
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Connection status verification failed");
        else
            g_task_return_pointer (task, g_object_ref (ctx->data), g_object_unref);
    }

    g_object_unref (task);
}

static void
gtrndis_activate_ready (MMBaseModem  *modem,
                        GAsyncResult *res,
                        GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (modem, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_base_modem_at_command (modem,
                              "+GTRNDIS?",
                              6, /* timeout [s] */
                              FALSE, /* allow_cached */
                              (GAsyncReadyCallback) gtrndis_verify_ready,
                              task);
}

static void
dial_3gpp (MMBroadbandBearer  *self,
           MMBaseModem        *modem,
           MMPortSerialAt     *primary,
           guint               cid,
           GCancellable       *cancellable,
           GAsyncReadyCallback callback,
           gpointer            user_data)
{
    GTask            *task;
    g_autofree gchar *cmd = NULL;

    task = dial_task_new (MM_BROADBAND_BEARER_FIBOCOM_ECM (self),
                          MM_BROADBAND_MODEM (modem),
                          primary,
                          cid,
                          cancellable,
                          callback,
                          user_data);
    if (!task)
        return;

    cmd = g_strdup_printf ("+GTRNDIS=1,%u", cid);
    mm_base_modem_at_command (modem,
                              cmd,
                              MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                              FALSE, /* allow_cached */
                              (GAsyncReadyCallback) gtrndis_activate_ready,
                              task);
}

/*****************************************************************************/
/* 3GPP Disconnect sequence                                                  */

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
gtrndis_deactivate_ready (MMBaseModem *modem,
                          GAsyncResult *res,
                          GTask *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (modem, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
disconnect_3gpp (MMBroadbandBearer *self,
                 MMBroadbandModem *modem,
                 MMPortSerialAt *primary,
                 MMPortSerialAt *secondary,
                 MMPort *data,
                 guint cid,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    GTask *task;
    g_autofree gchar *cmd = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    cmd = g_strdup_printf ("+GTRNDIS=0,%u", cid);
    mm_base_modem_at_command (MM_BASE_MODEM (modem),
                              cmd,
                              MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
                              FALSE, /* allow_cached */
                              (GAsyncReadyCallback) gtrndis_deactivate_ready,
                              task);
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_fibocom_ecm_new_finish (GAsyncResult *res,
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
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}

void
mm_broadband_bearer_fibocom_ecm_new (MMBroadbandModemFibocom *modem,
                                     MMBearerProperties *config,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_FIBOCOM_ECM,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_fibocom_ecm_init (MMBroadbandBearerFibocomEcm *self)
{
}

static void
mm_broadband_bearer_fibocom_ecm_class_init (MMBroadbandBearerFibocomEcmClass *klass)
{
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    base_bearer_class->load_connection_status = load_connection_status;
    base_bearer_class->load_connection_status_finish = load_connection_status_finish;

    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
