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
 * Copyright (C) 2019 James Wah
 * Copyright (C) 2020 Marinus Enzinger <marinus@enzingerm.de>
 * Copyright (C) 2023 Shane Parslow
 * Copyright (C) 2024 Thomas Vogt
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"

#include "mm-bearer-xmm7360.h"
#include "mm-broadband-modem-xmm7360.h"
#include "mm-broadband-modem-xmm7360-rpc.h"
#include "mm-port-serial-xmmrpc-xmm7360.h"
#include "mm-bind.h"

G_DEFINE_TYPE (MMBearerXmm7360, mm_bearer_xmm7360, MM_TYPE_BASE_BEARER)

struct _MMBearerXmm7360Private {
    gboolean is_connected;
};

/*****************************************************************************/
/* Connect */

typedef struct {
    MMPort *data;
    MMPortSerialXmmrpcXmm7360 *port;
    guint unsol_handler_id;
    gboolean is_attach_allowed;
    gboolean is_attached;
    guint attach_attempts;
    guint attach_allowed_timeout_id;
    Xmm7360RpcResponse *ps_connect_response;
    GInetAddress *ip;
    GPtrArray *dns;
} ConnectContext;

static void
connect_context_free (ConnectContext *ctx)
{
    if (ctx->ps_connect_response)
        xmm7360_rpc_response_free (ctx->ps_connect_response);
    if (ctx->attach_allowed_timeout_id)
        g_source_remove (ctx->attach_allowed_timeout_id);
    if (ctx->unsol_handler_id) {
        mm_port_serial_xmmrpc_xmm7360_enable_unsolicited_msg_handler (
            ctx->port,
            ctx->unsol_handler_id,
            FALSE);
    }
    g_clear_object (&ctx->ip);
    if (ctx->dns)
        g_ptr_array_free (ctx->dns, TRUE);
    mm_port_serial_close (MM_PORT_SERIAL (ctx->port));
    g_clear_object (&ctx->port);
    g_clear_object (&ctx->data);
    g_slice_free (ConnectContext, ctx);
}

static MMBearerConnectResult *
connect_finish (MMBaseBearer *self,
                GAsyncResult *res,
                GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
xmm7360_connect_apn_ready (MMBroadbandModemXmm7360 *modem,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;
    MMBearerXmm7360 *self;
    MMBearerConnectResult *connect_result;

    self = g_task_get_source_object (task);

    connect_result = g_task_propagate_pointer (G_TASK (res), &error);
    if (error) {
        g_task_return_error (task, error);
    } else {
        self->priv->is_connected = TRUE;
        g_task_return_pointer (task,
                               connect_result,
                               (GDestroyNotify)mm_bearer_connect_result_unref);
    }
    g_object_unref (task);
}

static void
ps_connect_setup_ready (MMBroadbandModemXmm7360 *modem,
                        GAsyncResult *res,
                        GTask *task)
{
    GError *error = NULL;
    ConnectContext *ctx;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    g_autoptr(MMBearerIpConfig) ip_config = NULL;
    g_autofree gchar *ipaddr = NULL;
    g_auto(GStrv) dnsaddrs = NULL;
    GInetAddress *dns;
    guint i;
    guint n;

    ctx = g_task_get_task_data (task);

    response = mm_broadband_modem_xmm7360_rpc_command_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* we get static IPs to set */
    ip_config = mm_bearer_ip_config_new ();
    ipaddr = g_inet_address_to_string (ctx->ip);
    dnsaddrs = g_new0 (gchar *, ctx->dns->len + 1);
    for (i = 0, n = 0; i < ctx->dns->len; i++) {
        dns = g_ptr_array_index (ctx->dns, i);
        if (g_inet_address_get_family (dns) == G_SOCKET_FAMILY_IPV4
            && !g_inet_address_get_is_any (dns)) {
            dnsaddrs[n++] = g_inet_address_to_string (dns);
        }
    }
    mm_bearer_ip_config_set_method (ip_config, MM_BEARER_IP_METHOD_STATIC);
    mm_bearer_ip_config_set_address (ip_config, ipaddr);
    mm_bearer_ip_config_set_dns (ip_config, (const gchar **)dnsaddrs);

    g_task_return_pointer (task,
                           mm_bearer_connect_result_new (ctx->data, ip_config, NULL),
                           (GDestroyNotify)mm_bearer_connect_result_unref);
    g_object_unref (task);
}

static void
ps_connect_to_datachannel_ready (MMBroadbandModemXmm7360 *modem,
                                 GAsyncResult *res,
                                 GTask *task)
{
    GError *error = NULL;
    ConnectContext *ctx;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    g_autoptr(GByteArray) connect_setup_body = NULL;

    ctx = g_task_get_task_data (task);

    response = mm_broadband_modem_xmm7360_rpc_command_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    connect_setup_body = g_byte_array_new ();
    g_byte_array_append (connect_setup_body,
                         ctx->ps_connect_response->body->data,
                         ctx->ps_connect_response->body->len - 6);
    g_byte_array_append (connect_setup_body,
                         response->body->data,
                         response->body->len);
    xmm7360_byte_array_append_asn_int4 (connect_setup_body, 0);

    mm_broadband_modem_xmm7360_rpc_command_full (modem,
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_UTA_RPC_PS_CONNECT_SETUP_REQ,
                                                 FALSE,
                                                 connect_setup_body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)ps_connect_setup_ready,
                                                 task);
}

static GByteArray *
pack_uta_rpc_ps_connect_to_datachannel_req (void)
{
    static const Xmm7360RpcMsgArg args[] = {
        /* size is length of string + null byte */
        { XMM7360_RPC_MSG_ARG_TYPE_STRING, { .string = "/sioscc/PCIE/IOSM/IPS/0" }, 24 },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static void
ps_connect_ready (MMBroadbandModemXmm7360 *modem,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;
    ConnectContext *ctx;
    Xmm7360RpcResponse *response = NULL;
    g_autoptr(GByteArray) body = NULL;

    ctx = g_task_get_task_data (task);

    response = mm_broadband_modem_xmm7360_rpc_command_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        xmm7360_rpc_response_free (response);
        return;
    }

    ctx->ps_connect_response = response;

    body = pack_uta_rpc_ps_connect_to_datachannel_req ();

    mm_broadband_modem_xmm7360_rpc_command_full (modem,
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_UTA_RPC_PS_CONNECT_TO_DATACHANNEL_REQ,
                                                 FALSE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)ps_connect_to_datachannel_ready,
                                                 task);
}

static GByteArray *
pack_uta_ms_call_ps_connect_req (void)
{
    static const Xmm7360RpcMsgArg args[] = {
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 6 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static void
get_dns_ready (MMBroadbandModemXmm7360 *modem,
               GAsyncResult *res,
               GTask *task)
{
    GError *error = NULL;
    ConnectContext *ctx;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    Xmm7360RpcMsgArg *arg;
    guint i;
    g_autoptr(GByteArray) body = NULL;

    ctx = g_task_get_task_data (task);

    response = mm_broadband_modem_xmm7360_rpc_command_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (i = 1; i < 17; i += 2) {
        /* iterate over pairs of args: IP [i] and IP format (v4(1) or v6(2) or MISSING(0)) [i+1] */
        arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, i + 1);
        if (XMM7360_RPC_MSG_ARG_GET_INT (arg) == 1) {
            arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, i);
            g_assert (arg->size >= 4);
            g_ptr_array_add (ctx->dns, g_inet_address_new_from_bytes ((const guint8 *)arg->value.string,
                                                                      G_SOCKET_FAMILY_IPV4));
        } else if (XMM7360_RPC_MSG_ARG_GET_INT (arg) == 2) {
            arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, i);
            g_assert (arg->size >= 16);
            g_ptr_array_add (ctx->dns, g_inet_address_new_from_bytes ((const guint8 *)arg->value.string,
                                                                      G_SOCKET_FAMILY_IPV6));
        }
    }

    body = pack_uta_ms_call_ps_connect_req ();

    mm_broadband_modem_xmm7360_rpc_command_full (modem,
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_UTA_MS_CALL_PS_CONNECT_REQ,
                                                 TRUE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)ps_connect_ready,
                                                 task);
}

static GByteArray *
pack_uta_ms_call_ps_get_negotiated_dns_req (void)
{
    static const Xmm7360RpcMsgArg args[] = {
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static void
get_ip_addr_ready (MMBroadbandModemXmm7360 *modem,
                   GAsyncResult *res,
                   GTask *task)
{
    GError *error = NULL;
    ConnectContext *ctx;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    Xmm7360RpcMsgArg *arg;
    gint i;
    guint32 *ip_bytes;
    g_autoptr(GByteArray) body = NULL;

    ctx = g_task_get_task_data (task);

    response = mm_broadband_modem_xmm7360_rpc_command_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 1);
    if (arg->size < 12) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "The IP address field is invalid (too short)");
        g_object_unref (task);
        return;
    }

    /* the STRING arg contains three IP addresses, we only use the last non-zero address */
    ip_bytes = (guint32 *)arg->value.string;
    for (i = 2; i >= 0; i--) {
        if (ip_bytes[i] != 0) {
            ctx->ip = g_inet_address_new_from_bytes ((const guint8 *)&ip_bytes[i],
                                                     G_SOCKET_FAMILY_IPV4);
            break;
        }
    }

    if (ctx->ip == NULL) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "The IP address field is invalid (all zeros)");
        g_object_unref (task);
        return;
    }

    body = pack_uta_ms_call_ps_get_negotiated_dns_req ();

    mm_broadband_modem_xmm7360_rpc_command_full (modem,
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_UTA_MS_CALL_PS_GET_NEGOTIATED_DNS_REQ,
                                                 TRUE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)get_dns_ready,
                                                 task);
}

static GByteArray *
pack_uta_ms_call_ps_get_neg_ip_addr_req (void)
{
    static const Xmm7360RpcMsgArg args[] = {
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static gboolean
get_ip_config (GTask *task)
{
    ConnectContext *ctx;
    MMBroadbandModemXmm7360 *modem;
    g_autoptr(GByteArray) body = NULL;

    if (g_cancellable_is_cancelled (g_task_get_cancellable (task))) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                 "operation has been cancelled");
        g_object_unref (task);
        return G_SOURCE_REMOVE;
    }

    ctx = g_task_get_task_data (task);

    modem = g_task_get_source_object (task);

    body = pack_uta_ms_call_ps_get_neg_ip_addr_req ();

    mm_broadband_modem_xmm7360_rpc_command_full (modem,
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_UTA_MS_CALL_PS_GET_NEG_IP_ADDR_REQ,
                                                 TRUE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)get_ip_addr_ready,
                                                 task);

    return G_SOURCE_REMOVE;
}

static gboolean
attach_allowed_timeout_cb (GTask *task)
{
    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Connecting timed out (waiting for attach-allowed)");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void net_attach_command (GTask *task);

static void
net_attach_command_ready (MMBroadbandModemXmm7360 *modem,
                          GAsyncResult *res,
                          GTask *task)
{
    GError *error = NULL;
    ConnectContext *ctx;
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    Xmm7360RpcMsgArg *arg;
    gint status;

    ctx = g_task_get_task_data (task);

    response = mm_broadband_modem_xmm7360_rpc_command_finish (modem, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (response->content->len < 2) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "The response for net-attach is invalid (too short)");
        g_object_unref (task);
        return;
    }

    /* status code */
    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 1);
    g_assert (arg->type == XMM7360_RPC_MSG_ARG_TYPE_LONG);
    status = XMM7360_RPC_MSG_ARG_GET_INT (arg);

    if (status != (gint32)0xffffffff) {
        ctx->is_attached = TRUE;
        /* give the modem a second before requesting the IP */
        g_timeout_add_seconds (1, (GSourceFunc)get_ip_config, task);
    } else if (ctx->is_attach_allowed && ctx->attach_attempts < 2) {
        /* immediately try a second time if it should have been allowed */
        ctx->is_attach_allowed = FALSE;
        net_attach_command (task);
    } else if (ctx->attach_attempts < 3) {
        /* give up if we do not receive an attach-allowed unsolicited message within 5 seconds */
        ctx->attach_allowed_timeout_id = g_timeout_add_seconds (5, (GSourceFunc)attach_allowed_timeout_cb, task);
    } else {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Giving up on attach-net operation after three failed attempts");
        g_object_unref (task);
    }
}

static GByteArray *
pack_uta_ms_net_attach_req (void)
{
    static const Xmm7360RpcMsgArg args[] = {
        { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_SHORT, { .s = 0xffff } },
        { XMM7360_RPC_MSG_ARG_TYPE_SHORT, { .s = 0xffff } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
        { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
    };
    return xmm7360_rpc_args_to_byte_array (args);
}

static void
net_attach_command (GTask *task)
{
    ConnectContext *ctx;
    MMBroadbandModemXmm7360 *modem;
    g_autoptr(GByteArray) body = NULL;

    ctx = g_task_get_task_data (task);
    ctx->attach_attempts++;

    modem = g_task_get_source_object (task);

    body = pack_uta_ms_net_attach_req ();

    mm_broadband_modem_xmm7360_rpc_command_full (modem,
                                                 ctx->port,
                                                 XMM7360_RPC_CALL_UTA_MS_NET_ATTACH_REQ,
                                                 TRUE,
                                                 body,
                                                 3,
                                                 FALSE,
                                                 NULL,  /* cancellable */
                                                 (GAsyncReadyCallback)net_attach_command_ready,
                                                 task);
}

static gboolean
connect_unsol_handler (MMPortSerialXmmrpcXmm7360 *port,
                       Xmm7360RpcResponse *response,
                       GTask *task)
{
    Xmm7360RpcMsgArg *arg;
    ConnectContext *ctx;

    if (response->unsol_id != XMM7360_RPC_UNSOL_UTA_MS_NET_IS_ATTACH_ALLOWED_IND_CB) {
        return FALSE;
    }

    if (response->content->len <= 2) {
        mm_obj_dbg (port, "Ignoring invalid is-attach-allowed message (too short)");
        return TRUE;
    }

    ctx = g_task_get_task_data (task);

    if (ctx->is_attached) {
        /* already attached, ignore the attach-allowed status message */
        return TRUE;
    }

    arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 2);
    if (XMM7360_RPC_MSG_ARG_GET_INT (arg)) {
        ctx->is_attach_allowed = TRUE;
        if (ctx->attach_allowed_timeout_id) {
            g_source_remove (ctx->attach_allowed_timeout_id);
            ctx->attach_allowed_timeout_id = 0;
            /* attach-net is allowed now, retry */
            net_attach_command (task);
        }
    }

    return TRUE;
}

static void
xmm7360_connect (MMBroadbandModemXmm7360 *modem,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    GError *error = NULL;
    GTask *task;
    ConnectContext *ctx;

    task = g_task_new (modem, cancellable, callback, user_data);

    ctx = g_slice_new0 (ConnectContext);
    ctx->port = mm_broadband_modem_xmm7360_get_port_xmmrpc (modem);
    ctx->dns = g_ptr_array_new_with_free_func ((GDestroyNotify)g_object_unref);
    g_task_set_task_data (task, ctx, (GDestroyNotify)connect_context_free);

    /* Grab a data port */
    ctx->data = mm_base_modem_get_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!ctx->data) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                 "No valid data port found to launch connection");
        g_object_unref (task);
        return;
    }

    /* Open XMMRPC port for initialization */
    if (!mm_port_serial_open (MM_PORT_SERIAL (ctx->port), &error)) {
        g_prefix_error (&error, "Couldn't open XMMRPC port for connection setup: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx->unsol_handler_id = mm_port_serial_xmmrpc_xmm7360_add_unsolicited_msg_handler (
        ctx->port,
        (MMPortSerialXmmrpcXmm7360UnsolicitedMsgFn)connect_unsol_handler,
        task,
        NULL);

    net_attach_command (task);
}

static void
connect (MMBaseBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    GTask                         *task;
    g_autoptr(MMBaseModem)         modem = NULL;

    /* Get the owner modem object */
    g_object_get (self,
                  MM_BASE_BEARER_MODEM,  &modem,
                  NULL);
    g_assert (modem != NULL);

    task = g_task_new (self, cancellable, callback, user_data);

    /* the connect routine is independent of the bearer object */
    xmm7360_connect (MM_BROADBAND_MODEM_XMM7360 (modem),
                     cancellable,
                     (GAsyncReadyCallback)xmm7360_connect_apn_ready,
                     task);
}

/*****************************************************************************/
/* Disconnect */

static gboolean
disconnect_finish (MMBaseBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disconnect_sequence_ready (MMBroadbandModem *modem,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;
    g_autoptr(Xmm7360RpcResponse) response = NULL;

    response = mm_broadband_modem_xmm7360_rpc_sequence_finish (MM_BROADBAND_MODEM_XMM7360 (modem),
                                                               res,
                                                               &error);

    if (error) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Failed to complete disconnect sequence: %s", error->message);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static const MMBroadbandModemXmm7360RpcCommand disconnect_sequence[] = {
    {
        XMM7360_RPC_CALL_UTA_MS_CALL_PS_DEACTIVATE_REQ,
        TRUE,
        (Xmm7360RpcMsgArg[]) {
            { XMM7360_RPC_MSG_ARG_TYPE_BYTE, { .b = 0 } },
            { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
            { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0 } },
            { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
        },
        3,
        FALSE,
        FALSE,
        mm_broadband_modem_xmm7360_rpc_response_processor_continue_on_success
        /* response will be: L(0x0) L(0x0) L(0x5dffffff) L(0x0) (meaning unknown) */
    },
    {
        XMM7360_RPC_CALL_UTA_RPC_PS_CONNECT_RELEASE_REQ,
        FALSE,
        (Xmm7360RpcMsgArg[]) {
            /* the meaning of this value is unknown, but it is used by the Windows driver */
            { XMM7360_RPC_MSG_ARG_TYPE_LONG, { .l = 0x20017 } },
            { XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN },
        },
        3,
        FALSE,
        FALSE,
        mm_broadband_modem_xmm7360_rpc_response_processor_final
        /* response will be: L(0x0) (meaning unknown) */
    },
    { 0 }
};

static void
disconnect (MMBaseBearer *bearer,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GTask                               *task;
    g_autoptr(MMBaseModem)               modem = NULL;
    g_autoptr(MMPortSerialXmmrpcXmm7360) port = NULL;

    task = g_task_new (bearer, NULL, callback, user_data);

    /* Get the owner modem object */
    g_object_get (bearer, MM_BASE_BEARER_MODEM, &modem, NULL);
    g_assert (modem != NULL);

    port = mm_broadband_modem_xmm7360_get_port_xmmrpc (MM_BROADBAND_MODEM_XMM7360 (modem));

    mm_broadband_modem_xmm7360_rpc_sequence_full (MM_BROADBAND_MODEM_XMM7360 (modem),
                                                  port,
                                                  disconnect_sequence,
                                                  NULL, /* cancellable */
                                                  (GAsyncReadyCallback)disconnect_sequence_ready,
                                                  task);
}

/*****************************************************************************/

MMBaseBearer *
mm_bearer_xmm7360_new (MMBroadbandModemXmm7360 *modem,
                       MMBearerProperties  *config)
{
    MMBaseBearer *base_bearer;
    MMBearerXmm7360 *bearer;

    /* The Xmm7360 bearer inherits from MMBaseBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new here */
    bearer = g_object_new (MM_TYPE_BEARER_XMM7360,
                           MM_BASE_BEARER_MODEM, modem,
                           MM_BIND_TO, modem,
                           MM_BASE_BEARER_CONFIG, config,
                           NULL);

    base_bearer = MM_BASE_BEARER (bearer);
    /* Only export valid bearers */
    mm_base_bearer_export (base_bearer);

    return base_bearer;
}

static void
mm_bearer_xmm7360_init (MMBearerXmm7360 *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BEARER_XMM7360,
                                              MMBearerXmm7360Private);
}

static void
mm_bearer_xmm7360_class_init (MMBearerXmm7360Class *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerXmm7360Private));

    base_bearer_class->connect = connect;
    base_bearer_class->connect_finish = connect_finish;

    base_bearer_class->disconnect = disconnect;
    base_bearer_class->disconnect_finish = disconnect_finish;
}
