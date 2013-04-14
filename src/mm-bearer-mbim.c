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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-serial-enums-types.h"
#include "mm-bearer-mbim.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBearerMbim, mm_bearer_mbim, MM_TYPE_BEARER);

struct _MMBearerMbimPrivate {
    gpointer dummy;
};

/*****************************************************************************/

static gboolean
peek_ports (gpointer self,
            MbimDevice **o_device,
            MMPort **o_data,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMBaseModem *modem = NULL;

    g_object_get (G_OBJECT (self),
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    if (o_device) {
        MMMbimPort *port;

        port = mm_base_modem_peek_port_mbim (modem);
        if (!port) {
            g_simple_async_report_error_in_idle (G_OBJECT (self),
                                                 callback,
                                                 user_data,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_FAILED,
                                                 "Couldn't peek MBIM port");
            g_object_unref (modem);
            return FALSE;
        }

        *o_device = mm_mbim_port_peek_device (port);
    }

    if (o_data) {
        MMPort *port;

        /* Grab a data port */
        port = mm_base_modem_peek_best_data_port (modem, MM_PORT_TYPE_NET);
        if (!port) {
            g_simple_async_report_error_in_idle (G_OBJECT (self),
                                                 callback,
                                                 user_data,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_NOT_FOUND,
                                                 "No valid data port found to launch connection");
            g_object_unref (modem);
            return FALSE;
        }

        *o_data = port;
    }

    g_object_unref (modem);
    return TRUE;
}

/*****************************************************************************/
/* Connect */

typedef enum {
    CONNECT_STEP_FIRST,
    CONNECT_STEP_PROVISIONED_CONTEXTS,
    CONNECT_STEP_LAST
} ConnectStep;

typedef struct {
    MMBearerMbim *self;
    MbimDevice *device;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMBearerProperties *properties;
    ConnectStep step;
    MMPort *data;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->device);
    g_object_unref (ctx->self);
    g_slice_free (ConnectContext, ctx);
}

static MMBearerConnectResult *
connect_finish (MMBearer *self,
                GAsyncResult *res,
                GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void connect_context_step (ConnectContext *ctx);

static void
provisioned_contexts_query (MbimDevice *device,
                            GAsyncResult *res,
                            ConnectContext *ctx)
{
    GError *error = NULL;
    MbimMessage *response;
    guint32 provisioned_contexts_count;
    MbimProvisionedContextElement **provisioned_contexts;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_command_done_get_result (response, &error) &&
        mbim_message_provisioned_contexts_response_parse (
            response,
            &provisioned_contexts_count,
            &provisioned_contexts,
            &error)) {
        guint32 i;

        mm_dbg ("Provisioned contexts found (%u):", provisioned_contexts_count);
        for (i = 0; i < provisioned_contexts_count; i++) {
            MbimProvisionedContextElement *el = provisioned_contexts[i];
            gchar *uuid_str;

            uuid_str = mbim_uuid_get_printable (&el->context_type);
            mm_dbg ("[%u] context type: %s", el->context_id, mbim_context_type_get_string (mbim_uuid_to_context_type (&el->context_type)));
            mm_dbg ("             uuid: %s", uuid_str);
            mm_dbg ("    access string: %s", el->access_string ? el->access_string : "");
            mm_dbg ("         username: %s", el->user_name ? el->user_name : "");
            mm_dbg ("         password: %s", el->password ? el->password : "");
            mm_dbg ("      compression: %s", mbim_compression_get_string (el->compression));
            mm_dbg ("             auth: %s", mbim_auth_protocol_get_string (el->auth_protocol));
            g_free (uuid_str);
        }

        mbim_provisioned_context_element_array_free (provisioned_contexts);
    } else {
        mm_dbg ("Error listing provisioned contexts: %s", error->message);
        g_error_free (error);
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
connect_context_step (ConnectContext *ctx)
{
    /* If cancelled, complete */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "Connection setup operation has been cancelled");
        connect_context_complete_and_free (ctx);
        return;
    }

    switch (ctx->step) {
    case CONNECT_STEP_FIRST:
        /* Fall down */
        ctx->step++;

    case CONNECT_STEP_PROVISIONED_CONTEXTS: {
        MbimMessage *message;

        message = mbim_message_provisioned_contexts_query_new (NULL);
        mbim_device_command (ctx->device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)provisioned_contexts_query,
                             ctx);
        mbim_message_unref (message);
        return;
    }

    case CONNECT_STEP_LAST:
        /* /\* If one of IPv4 or IPv6 succeeds, we're connected *\/ */
        /* if (ctx->packet_data_handle_ipv4 || ctx->packet_data_handle_ipv6) { */
        /*     MMBearerIpConfig *config; */

        /*     /\* Port is connected; update the state *\/ */
        /*     mm_port_set_connected (MM_PORT (ctx->data), TRUE); */

        /*     /\* Keep connection related data *\/ */
        /*     g_assert (ctx->self->priv->data == NULL); */
        /*     ctx->self->priv->data = g_object_ref (ctx->data); */

        /*     g_assert (ctx->self->priv->packet_data_handle_ipv4 == 0); */
        /*     g_assert (ctx->self->priv->client_ipv4 == NULL); */
        /*     if (ctx->packet_data_handle_ipv4) { */
        /*         ctx->self->priv->packet_data_handle_ipv4 = ctx->packet_data_handle_ipv4; */
        /*         ctx->self->priv->client_ipv4 = g_object_ref (ctx->client_ipv4); */
        /*     } */

        /*     g_assert (ctx->self->priv->packet_data_handle_ipv6 == 0); */
        /*     g_assert (ctx->self->priv->client_ipv6 == NULL); */
        /*     if (ctx->packet_data_handle_ipv6) { */
        /*         ctx->self->priv->packet_data_handle_ipv6 = ctx->packet_data_handle_ipv6; */
        /*         ctx->self->priv->client_ipv6 = g_object_ref (ctx->client_ipv6); */
        /*     } */

        /*     /\* Build IP config; always DHCP based *\/ */
        /*     config = mm_bearer_ip_config_new (); */
        /*     mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_DHCP); */

        /*     /\* Set operation result *\/ */
        /*     g_simple_async_result_set_op_res_gpointer ( */
        /*         ctx->result, */
        /*         mm_bearer_connect_result_new ( */
        /*             ctx->data, */
        /*             ctx->packet_data_handle_ipv4 ? config : NULL, */
        /*             ctx->packet_data_handle_ipv6 ? config : NULL), */
        /*         (GDestroyNotify)mm_bearer_connect_result_unref); */
        /*     g_object_unref (config); */
        /* } else { */
        /*     GError *error; */

        /*     /\* No connection, set error. If both set, IPv4 error preferred *\/ */
        /*     if (ctx->error_ipv4) { */
        /*         error = ctx->error_ipv4; */
        /*         ctx->error_ipv4 = NULL; */
        /*     } else { */
        /*         error = ctx->error_ipv6; */
        /*         ctx->error_ipv6 = NULL; */
        /*     } */

        /*     g_simple_async_result_take_error (ctx->result, error); */
        /* } */

        g_simple_async_result_set_error (ctx->result, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Oops");
        connect_context_complete_and_free (ctx);
        return;
    }
}

static void
_connect (MMBearer *self,
          GCancellable *cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    ConnectContext *ctx;
    MMPort *data;
    MbimDevice *device;

    if (!peek_ports (self, &device, &data, callback, user_data))
        return;

    mm_dbg ("Launching connection with data port (%s/%s)",
            mm_port_subsys_get_string (mm_port_get_subsys (data)),
            mm_port_get_device (data));

    ctx = g_slice_new0 (ConnectContext);
    ctx->self = g_object_ref (self);
    ctx->device = g_object_ref (device);;
    ctx->data = g_object_ref (data);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->step = CONNECT_STEP_FIRST;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             _connect);

    g_object_get (self,
                  MM_BEARER_CONFIG, &ctx->properties,
                  NULL);

    /* Run! */
    connect_context_step (ctx);
}

/*****************************************************************************/

MMBearer *
mm_bearer_mbim_new (MMBroadbandModemMbim *modem,
                    MMBearerProperties *config)
{
    MMBearer *bearer;

    /* The Mbim bearer inherits from MMBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new() here */
    bearer = g_object_new (MM_TYPE_BEARER_MBIM,
                           MM_BEARER_MODEM, modem,
                           MM_BEARER_CONFIG, config,
                           NULL);

    /* Only export valid bearers */
    mm_bearer_export (bearer);

    return bearer;
}

static void
mm_bearer_mbim_init (MMBearerMbim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_MBIM,
                                              MMBearerMbimPrivate);
}

static void
dispose (GObject *object)
{
    G_OBJECT_CLASS (mm_bearer_mbim_parent_class)->dispose (object);
}

static void
mm_bearer_mbim_class_init (MMBearerMbimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerMbimPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;

    bearer_class->connect = _connect;
    bearer_class->connect_finish = connect_finish;
}
