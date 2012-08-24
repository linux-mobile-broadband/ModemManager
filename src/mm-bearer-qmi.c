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
 * Copyright (C) 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>
#include <libqmi-glib.h>

#include "mm-bearer-qmi.h"
#include "mm-utils.h"
#include "mm-serial-enums-types.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBearerQmi, mm_bearer_qmi, MM_TYPE_BEARER);

struct _MMBearerQmiPrivate {
    /* State kept while connected */
    QmiClientWds *client;
    MMPort *data;
    guint32 packet_data_handle;
};

/*****************************************************************************/
/* Connect */

typedef struct {
    MMPort *data;
    MMBearerIpConfig *ipv4_config;
    MMBearerIpConfig *ipv6_config;
} ConnectResult;

static void
connect_result_free (ConnectResult *result)
{
    if (result->ipv4_config)
        g_object_unref (result->ipv4_config);
    if (result->ipv6_config)
        g_object_unref (result->ipv6_config);
    g_object_unref (result->data);
    g_slice_free (ConnectResult, result);
}

typedef enum {
    CONNECT_STEP_FIRST,
    CONNECT_STEP_OPEN_QMI_PORT,
    CONNECT_STEP_WDS_CLIENT,
    CONNECT_STEP_START_NETWORK,
    CONNECT_STEP_LAST
} ConnectStep;

typedef struct {
    MMBearerQmi *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    ConnectStep step;
    MMPort *data;
    MMQmiPort *qmi;
    QmiClientWds *client;
    guint32 packet_data_handle;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->client)
        g_object_unref (ctx->client);
    g_object_unref (ctx->data);
    g_object_unref (ctx->qmi);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (ConnectContext, ctx);
}

static gboolean
connect_finish (MMBearer *self,
                GAsyncResult *res,
                MMPort **data,
                MMBearerIpConfig **ipv4_config,
                MMBearerIpConfig **ipv6_config,
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

static void connect_context_step (ConnectContext *ctx);

static void
start_network_ready (QmiClientWds *client,
                     GAsyncResult *res,
                     ConnectContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsStartNetworkOutput *output;

    output = qmi_client_wds_start_network_finish (client, res, &error);
    if (!output) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_wds_start_network_output_get_result (output, &error)) {
        /* No-effect errors should be ignored. The modem will keep the
         * connection active as long as there is a WDS client which requested
         * to start the network. If ModemManager crashed while a connection was
         * active, we would be leaving an unreleased WDS client around and the
         * modem would just keep connected. */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_error_free (error);

            /* Fall down to a successful connection */
        } else {
            mm_info ("error: couldn't start network: %s", error->message);
            if (g_error_matches (error,
                                 QMI_PROTOCOL_ERROR,
                                 QMI_PROTOCOL_ERROR_CALL_FAILED)) {
                guint16 cer;
                guint16 verbose_cer_type;
                guint16 verbose_cer_reason;

                if (qmi_message_wds_start_network_output_get_call_end_reason (
                        output,
                        &cer,
                        NULL))
                    mm_info ("call end reason: %u", cer);

                if (qmi_message_wds_start_network_output_get_verbose_call_end_reason (
                        output,
                        &verbose_cer_type,
                        &verbose_cer_reason,
                        NULL))
                    mm_info ("verbose call end reason: %u, %u",
                             verbose_cer_type,
                             verbose_cer_reason);
            }

            g_simple_async_result_take_error (ctx->result, error);
            connect_context_complete_and_free (ctx);
            qmi_message_wds_start_network_output_unref (output);
            return;
        }
    }

    qmi_message_wds_start_network_output_get_packet_data_handle (output, &ctx->packet_data_handle, NULL);
    qmi_message_wds_start_network_output_unref (output);

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static QmiMessageWdsStartNetworkInput *
build_start_network_input (MMBearerQmi *self)
{
    QmiMessageWdsStartNetworkInput *input;
    MMBearerProperties *properties = NULL;
    const gchar *str;
    QmiWdsIpFamily ip_type;

    g_object_get (self,
                  MM_BEARER_CONFIG, &properties,
                  NULL);

    if (!properties)
        return NULL;

    input = qmi_message_wds_start_network_input_new ();

    str = mm_bearer_properties_get_apn (properties);
    if (str)
        qmi_message_wds_start_network_input_set_apn (input, str, NULL);

    str = mm_bearer_properties_get_user (properties);
    if (str)
        qmi_message_wds_start_network_input_set_username (input, str, NULL);

    str = mm_bearer_properties_get_password (properties);
    if (str)
        qmi_message_wds_start_network_input_set_password (input, str, NULL);

    switch (mm_bearer_properties_get_ip_type (properties)) {
    case MM_BEARER_IP_FAMILY_IPV4:
        ip_type = QMI_WDS_IP_FAMILY_IPV4;
        break;
    case MM_BEARER_IP_FAMILY_IPV6:
        ip_type = QMI_WDS_IP_FAMILY_IPV6;
        break;
    case MM_BEARER_IP_FAMILY_IPV4V6:
        /* dual stack, we assume unspecified */
    case MM_BEARER_IP_FAMILY_UNKNOWN:
    default:
        ip_type = QMI_WDS_IP_FAMILY_UNSPECIFIED;
        break;
    }

    qmi_message_wds_start_network_input_set_ip_family_preference (input, ip_type, NULL);

    return input;
}

static void
qmi_port_allocate_client_ready (MMQmiPort *qmi,
                                GAsyncResult *res,
                                ConnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_qmi_port_allocate_client_finish (qmi, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    ctx->client = QMI_CLIENT_WDS (mm_qmi_port_get_client (qmi, QMI_SERVICE_WDS));

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
qmi_port_open_ready (MMQmiPort *qmi,
                     GAsyncResult *res,
                     ConnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_qmi_port_open_finish (qmi, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
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

    case CONNECT_STEP_OPEN_QMI_PORT:
        if (!mm_qmi_port_is_open (ctx->qmi)) {
            mm_qmi_port_open (ctx->qmi,
                              ctx->cancellable,
                              (GAsyncReadyCallback)qmi_port_open_ready,
                              ctx);
            return;
        }

        /* If already open, just fall down */
        ctx->step++;

    case CONNECT_STEP_WDS_CLIENT: {
        QmiClient *client;

        client = mm_qmi_port_get_client (ctx->qmi, QMI_SERVICE_WDS);
        if (!client) {
            mm_qmi_port_allocate_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         ctx->cancellable,
                                         (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                         ctx);
            return;
        }

        ctx->client = QMI_CLIENT_WDS (client);
    }

    case CONNECT_STEP_START_NETWORK: {
        QmiMessageWdsStartNetworkInput *input;

        input = build_start_network_input (ctx->self);
        qmi_client_wds_start_network (ctx->client,
                                      input,
                                      10,
                                      ctx->cancellable,
                                      (GAsyncReadyCallback)start_network_ready,
                                      ctx);
        if (input)
            qmi_message_wds_start_network_input_unref (input);
        return;
    }

    case CONNECT_STEP_LAST: {
        MMBearerIpConfig *config;
        ConnectResult *result;

        /* Port is connected; update the state */
        mm_port_set_connected (MM_PORT (ctx->data), TRUE);

        /* Keep connection related data */
        g_assert (ctx->self->priv->data == NULL);
        ctx->self->priv->data = g_object_ref (ctx->data);
        g_assert (ctx->self->priv->client == NULL);
        ctx->self->priv->client = g_object_ref (ctx->client);
        g_assert (ctx->self->priv->packet_data_handle == 0);
        ctx->self->priv->packet_data_handle = ctx->packet_data_handle;

        /* Build IP config; always DHCP based */
        config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_DHCP);

        /* Build result */
        result = g_slice_new0 (ConnectResult);
        result->data = g_object_ref (ctx->data);
        result->ipv4_config = config;
        result->ipv6_config = g_object_ref (config);

        /* Set operation result */
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   result,
                                                   (GDestroyNotify)connect_result_free);
        connect_context_complete_and_free (ctx);
      }
    }
}

static MMQmiPort *
get_qmi_port_for_data_port (MMBaseModem *modem,
                            MMPort *data)
{
    /* TODO: match QMI and WWAN ports */
    return mm_base_modem_get_port_qmi (modem);
}

static void
connect (MMBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    ConnectContext *ctx;
    MMBaseModem *modem  = NULL;
    MMPort *data;
    MMQmiPort *qmi;

    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem);

    /* Grab a data port */
    data = mm_base_modem_get_best_data_port (modem);
    if (!data) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_NOT_FOUND,
            "No valid data port found to launch connection");
        g_object_unref (modem);
        return;
    }

    /* Each data port has a single QMI port associated */
    qmi = get_qmi_port_for_data_port (modem, data);
    if (!qmi) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_NOT_FOUND,
            "No QMI port found associated to data port (%s/%s)",
            mm_port_subsys_get_string (mm_port_get_subsys (data)),
            mm_port_get_device (data));
        g_object_unref (data);
        g_object_unref (modem);
        return;
    }

    mm_dbg ("Launching connection with QMI port (%s/%s) and data port (%s/%s)",
            mm_port_subsys_get_string (mm_port_get_subsys (MM_PORT (qmi))),
            mm_port_get_device (MM_PORT (qmi)),
            mm_port_subsys_get_string (mm_port_get_subsys (data)),
            mm_port_get_device (data));

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_slice_new0 (ConnectContext);
    ctx->self = g_object_ref (self);
    ctx->qmi = qmi;
    ctx->data = data;
    ctx->cancellable = g_object_ref (cancellable);
    ctx->step = CONNECT_STEP_FIRST;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    /* Run! */
    connect_context_step (ctx);
    g_object_unref (modem);
}

/*****************************************************************************/
/* Disconnect */

typedef struct {
    MMBearerQmi *self;
    QmiClientWds *client;
    MMPort *data;
    guint32 packet_data_handle;
    GSimpleAsyncResult *result;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->data);
    g_object_unref (ctx->self);
    g_slice_free (DisconnectContext, ctx);
}

static gboolean
disconnect_finish (MMBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
reset_bearer_connection (MMBearerQmi *self)
{
    if (self->priv->data) {
        /* Port is disconnected; update the state */
        mm_port_set_connected (self->priv->data, FALSE);
        g_clear_object (&self->priv->data);
    }

    g_clear_object (&self->priv->client);
    self->priv->packet_data_handle = 0;
}

static void
stop_network_ready (QmiClientWds *client,
                    GAsyncResult *res,
                    DisconnectContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsStopNetworkOutput *output;

    output = qmi_client_wds_stop_network_finish (client, res, &error);
    if (!output) {
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_wds_stop_network_output_get_result (output, &error)) {
        qmi_message_wds_stop_network_output_unref (output);
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_context_complete_and_free (ctx);
        return;
    }

    /* Clear internal status */
    reset_bearer_connection (ctx->self);

    qmi_message_wds_stop_network_output_unref (output);
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    disconnect_context_complete_and_free (ctx);
}

static void
disconnect (MMBearer *_self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMBearerQmi *self = MM_BEARER_QMI (_self);
    QmiMessageWdsStopNetworkInput *input;
    DisconnectContext *ctx;

    if (!self->priv->packet_data_handle ||
        !self->priv->client ||
        !self->priv->data) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect QMI bearer: this bearer is not connected");
        return;
    }

    ctx = g_slice_new0 (DisconnectContext);
    ctx->self = g_object_ref (self);
    ctx->data = g_object_ref (self->priv->data);
    ctx->client = g_object_ref (self->priv->client);
    ctx->packet_data_handle = self->priv->packet_data_handle;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect);

    input = qmi_message_wds_stop_network_input_new ();
    qmi_message_wds_stop_network_input_set_packet_data_handle (input, ctx->packet_data_handle, NULL);
    qmi_client_wds_stop_network (ctx->client,
                                 input,
                                 10,
                                 NULL,
                                 (GAsyncReadyCallback)stop_network_ready,
                                 ctx);
}

/*****************************************************************************/

static void
report_disconnection (MMBearer *self)
{
    /* Cleanup all connection related data */
    reset_bearer_connection (MM_BEARER_QMI (self));

    /* Chain up parent's report_disconection() */
    MM_BEARER_CLASS (mm_bearer_qmi_parent_class)->report_disconnection (self);
}

/*****************************************************************************/

MMBearer *
mm_bearer_qmi_new (MMBroadbandModemQmi *modem,
                   MMBearerProperties *config)
{
    MMBearer *bearer;

    /* The Qmi bearer inherits from MMBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new() here */
    bearer = g_object_new (MM_TYPE_BEARER_QMI,
                           MM_BEARER_MODEM, modem,
                           MM_BEARER_CONFIG, config,
                           NULL);

    /* Only export valid bearers */
    mm_bearer_export (bearer);

    return bearer;
}

static void
mm_bearer_qmi_init (MMBearerQmi *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_QMI,
                                              MMBearerQmiPrivate);
}

static void
dispose (GObject *object)
{
    MMBearerQmi *self = MM_BEARER_QMI (object);

    g_clear_object (&self->priv->data);
    g_clear_object (&self->priv->client);

    G_OBJECT_CLASS (mm_bearer_qmi_parent_class)->dispose (object);
}

static void
mm_bearer_qmi_class_init (MMBearerQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerQmiPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;

    bearer_class->connect = connect;
    bearer_class->connect_finish = connect_finish;
    bearer_class->disconnect = disconnect;
    bearer_class->disconnect_finish = disconnect_finish;
    bearer_class->report_disconnection = report_disconnection;
}
