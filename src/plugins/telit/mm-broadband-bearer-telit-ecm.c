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
 * Copyright (C) 2025 Daniele Palmas <dnlplm@gmail.com>
 */

#include <config.h>

#include "mm-broadband-bearer-telit-ecm.h"
#include "mm-broadband-modem-telit.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-log.h"
#include "mm-bind.h"

G_DEFINE_TYPE (MMBroadbandBearerTelitEcm, mm_broadband_bearer_telit_ecm, MM_TYPE_BROADBAND_BEARER)

/*****************************************************************************/
/* Common helper functions                                                   */

static gboolean
parse_ecm_read_response (const gchar  *response,
                         guint        *state,
                         GError      **error)
{
    g_autoptr(GRegex)     r = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;

    /* #ECM: <Did>,<State> where:
     *       <Did> always 0
     *       <State> 0: disabled
     *               1: enabled */
    r = g_regex_new ("\\#ECM:\\s*0,(\\d+)?",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match (r, response, 0, &match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Invalid #ECM response: %s", response);
        return FALSE;
    }

    if (!mm_get_uint_from_match_info (match_info, 1, state)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Failed to match state in #ECM response: %s", response);
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/
/* 3GPP Get Config                                                           */

typedef struct {
    MMPort           *data;
    MMBearerIpFamily  ip_family;
} GetIpConfig3gppContext;

static void
get_ip_config_context_free (GetIpConfig3gppContext *ctx)
{
    g_object_unref (ctx->data);
    g_slice_free (GetIpConfig3gppContext, ctx);
}

static gboolean
get_ip_config_3gpp_finish (MMBroadbandBearer  *self,
                           GAsyncResult       *res,
                           MMBearerIpConfig  **ipv4_config,
                           MMBearerIpConfig  **ipv6_config,
                           GError            **error)
{
    MMBearerConnectResult *configs;
    MMBearerIpConfig *ipv4, *ipv6;

    configs = g_task_propagate_pointer (G_TASK (res), error);
    if (!configs)
        return FALSE;

    ipv4 = mm_bearer_connect_result_peek_ipv4_config (configs);
    ipv6 = mm_bearer_connect_result_peek_ipv6_config (configs);
    g_assert (ipv4 || ipv6);
    if (ipv4_config && ipv4)
        *ipv4_config = g_object_ref (ipv4);
    if (ipv6_config && ipv6)
        *ipv6_config = g_object_ref (ipv6);

    mm_bearer_connect_result_unref (configs);
    return TRUE;
}

static void
get_hwaddress_ready (MMPortNet    *port,
                     GAsyncResult *res,
                     GTask        *task)
{
    GetIpConfig3gppContext *ctx;
    GByteArray             *hwaddr;
    MMBearerIpConfig       *ipv4_config = NULL;
    MMBearerIpConfig       *ipv6_config = NULL;
    GError                 *error = NULL;
    MMBearerConnectResult  *connect_result;

    ctx = g_task_get_task_data (task);

    hwaddr = mm_port_net_get_hwaddress_finish (port, res, &error);
    if (!hwaddr) {
        g_task_return_error (task, error);
        goto out;
    }

    if (ctx->ip_family & MM_BEARER_IP_FAMILY_IPV4 ||
            ctx->ip_family & MM_BEARER_IP_FAMILY_IPV4V6) {
        ipv4_config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (ipv4_config, MM_BEARER_IP_METHOD_DHCP);
    }

    if (ctx->ip_family & MM_BEARER_IP_FAMILY_IPV6 ||
            ctx->ip_family & MM_BEARER_IP_FAMILY_IPV4V6) {
        g_autofree gchar *lladdr;

        ipv6_config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_DHCP);

        lladdr = g_strdup_printf ("fe80::%02x%02x:%02xff:fe%02x:%02x%02x",
                                  hwaddr->data[0] ^ 2, hwaddr->data[1],
                                  hwaddr->data[2], hwaddr->data[3],
                                  hwaddr->data[4], hwaddr->data[5]);

        mm_bearer_ip_config_set_address (ipv6_config, lladdr);
        mm_bearer_ip_config_set_prefix (ipv6_config, 64);
    }

    if (!ipv4_config && !ipv6_config) {
        error = g_error_new_literal (MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't generate IP config: invalid IP family");
        g_task_return_error (task, error);
        goto out;
    }

    connect_result = mm_bearer_connect_result_new (MM_PORT (ctx->data),
                                                   ipv4_config,
                                                   ipv6_config);
    g_task_return_pointer (task,
                           connect_result,
                           (GDestroyNotify)mm_bearer_connect_result_unref);

out:
    g_object_unref (task);
    g_clear_object (&ipv4_config);
    g_clear_object (&ipv6_config);
}

static void
get_ip_config_3gpp (MMBroadbandBearer   *self,
                    MMBroadbandModem    *modem,
                    MMPortSerialAt      *primary,
                    MMPortSerialAt      *secondary,
                    MMPort              *data,
                    guint                cid,
                    MMBearerIpFamily     ip_family,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GetIpConfig3gppContext *ctx;
    GTask                  *task;

    ctx = g_slice_new0 (GetIpConfig3gppContext);
    ctx->data = g_object_ref (data);
    ctx->ip_family = ip_family;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)get_ip_config_context_free);

    mm_port_net_get_hwaddress (MM_PORT_NET (ctx->data),
                               NULL,
                               (GAsyncReadyCallback) get_hwaddress_ready,
                               task);
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

    result = MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_telit_ecm_parent_class)->connect_3gpp_finish (self, res, &error);
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
    MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_telit_ecm_parent_class)->connect_3gpp (
        self,
        ctx->modem,
        ctx->primary,
        ctx->secondary,
        g_task_get_cancellable (task),
        (GAsyncReadyCallback) parent_connect_3gpp_ready,
        task);
}

static void
ecm_check_ready (MMBaseModem  *modem,
                 GAsyncResult *res,
                 GTask        *task)
{
    MMBroadbandBearer *self;
    ConnectContext    *ctx;
    GError            *error = NULL;
    const gchar       *response;
    guint              state;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!parse_ecm_read_response (response, &state, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (state) {
        /* ECM is already active, disconnect first. */
        mm_obj_dbg (self, "ECM active, tearing down existing connection...");
        MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp (
            MM_BROADBAND_BEARER (self),
            ctx->modem,
            ctx->primary,
            ctx->secondary,
            NULL, /* data port */
            0, /* This should be the cid, but #ECMD does not need that */
            (GAsyncReadyCallback) disconnect_3gpp_ready,
            task);
        return;
    }

    /* Execute the regular connection flow if ECM is inactive. */
    MM_BROADBAND_BEARER_CLASS (mm_broadband_bearer_telit_ecm_parent_class)->connect_3gpp (
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
    mm_3gpp_normalize_ip_family (&ctx->ip_family, TRUE);

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) connect_context_free);

    /* Check whether ECM is already active */
    mm_base_modem_at_command (MM_BASE_MODEM (modem),
                              "#ECM?",
                              3,
                              FALSE, /* allow_cached */
                              (GAsyncReadyCallback) ecm_check_ready,
                              task);
}

/*****************************************************************************/
/* Dial context and task                                                     */

typedef struct {
    MMPortSerialAt   *primary;
    guint             cid;
    MMPort           *data;
} DialContext;

static void
dial_task_free (DialContext *ctx)
{
    g_object_unref (ctx->primary);
    g_clear_object (&ctx->data);
    g_slice_free (DialContext, ctx);
}

static GTask *
dial_task_new (MMBroadbandBearerTelitEcm *self,
               MMBroadbandModem          *modem,
               MMPortSerialAt            *primary,
               guint                      cid,
               GCancellable              *cancellable,
               GAsyncReadyCallback        callback,
               gpointer                   user_data)
{
    DialContext *ctx;
    GTask       *task;

    ctx          = g_slice_new0 (DialContext);
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
dial_3gpp_finish (MMBroadbandBearer  *self,
                  GAsyncResult       *res,
                  GError            **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
ecm_verify_ready (MMBaseModem  *modem,
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
        guint state = 0;

        if (!parse_ecm_read_response (response, &state, &error)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        if (state != 1) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Failed ECM check, state = %u", state);
            g_object_unref (task);
            return;
        }

        g_task_return_pointer (task, g_object_ref (ctx->data), g_object_unref);
    }

    g_object_unref (task);
}

static void
ecm_activate_ready (MMBaseModem  *modem,
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
                              "#ECM?",
                              3,
                              FALSE, /* allow_cached */
                              (GAsyncReadyCallback) ecm_verify_ready,
                              task);
}

static void
dial_3gpp (MMBroadbandBearer   *self,
           MMBaseModem         *modem,
           MMPortSerialAt      *primary,
           guint                cid,
           GCancellable        *cancellable,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    GTask            *task;
    g_autofree gchar *cmd = NULL;

    task = dial_task_new (MM_BROADBAND_BEARER_TELIT_ECM (self),
                          MM_BROADBAND_MODEM (modem),
                          primary,
                          cid,
                          cancellable,
                          callback,
                          user_data);
    if (!task)
        return;

    cmd = g_strdup_printf ("#ECM=%u,0", cid);
    mm_base_modem_at_command (modem,
                              cmd,
                              MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                              FALSE, /* allow_cached */
                              (GAsyncReadyCallback) ecm_activate_ready,
                              task);
}

/*****************************************************************************/
/* 3GPP Disconnect sequence                                                  */

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer  *self,
                        GAsyncResult       *res,
                        GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ecm_deactivate_ready (MMBaseModem  *modem,
                      GAsyncResult *res,
                      GTask        *task)
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

    task = g_task_new (self, NULL, callback, user_data);

    /* #ECMD command does not support cid selection and terminates the data
     * connection in every cid. */
    mm_base_modem_at_command (MM_BASE_MODEM (modem),
                              "#ECMD=0",
                              MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
                              FALSE, /* allow_cached */
                              (GAsyncReadyCallback) ecm_deactivate_ready,
                              task);
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_telit_ecm_new_finish (GAsyncResult  *res,
                                          GError       **error)
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
mm_broadband_bearer_telit_ecm_new (MMBroadbandModemTelit *modem,
                                   MMBearerProperties    *config,
                                   GCancellable          *cancellable,
                                   GAsyncReadyCallback    callback,
                                   gpointer               user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_TELIT_ECM,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BIND_TO, G_OBJECT (modem),
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_telit_ecm_init (MMBroadbandBearerTelitEcm *self)
{
}

static void
mm_broadband_bearer_telit_ecm_class_init (MMBroadbandBearerTelitEcmClass *klass)
{
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    /* No need to redefine load_connection_status, since the generic AT+CGACT? can be used */
    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
    broadband_bearer_class->get_ip_config_3gpp = get_ip_config_3gpp;
    broadband_bearer_class->get_ip_config_3gpp_finish = get_ip_config_3gpp_finish;
}
