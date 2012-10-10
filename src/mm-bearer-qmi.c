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
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-bearer-qmi.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-serial-enums-types.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBearerQmi, mm_bearer_qmi, MM_TYPE_BEARER);

#define GLOBAL_PACKET_DATA_HANDLE 0xFFFFFFFF

struct _MMBearerQmiPrivate {
    /* State kept while connected */
    QmiClientWds *client_ipv4;
    QmiClientWds *client_ipv6;
    MMPort *data;
    guint32 packet_data_handle_ipv4;
    guint32 packet_data_handle_ipv6;
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
    CONNECT_STEP_IPV4,
    CONNECT_STEP_WDS_CLIENT_IPV4,
    CONNECT_STEP_IP_FAMILY_IPV4,
    CONNECT_STEP_START_NETWORK_IPV4,
    CONNECT_STEP_IPV6,
    CONNECT_STEP_WDS_CLIENT_IPV6,
    CONNECT_STEP_IP_FAMILY_IPV6,
    CONNECT_STEP_START_NETWORK_IPV6,
    CONNECT_STEP_LAST
} ConnectStep;

typedef struct {
    MMBearerQmi *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    ConnectStep step;
    MMPort *data;
    MMQmiPort *qmi;
    gchar *user;
    gchar *password;
    gchar *apn;
    QmiWdsAuthentication auth;
    gboolean no_ip_family_preference;
    gboolean default_ip_family_set;

    gboolean ipv4;
    gboolean running_ipv4;
    QmiClientWds *client_ipv4;
    guint32 packet_data_handle_ipv4;
    GError *error_ipv4;

    gboolean ipv6;
    gboolean running_ipv6;
    QmiClientWds *client_ipv6;
    guint32 packet_data_handle_ipv6;
    GError *error_ipv6;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_free (ctx->apn);
    g_free (ctx->user);
    g_free (ctx->password);
    if (ctx->error_ipv4)
        g_error_free (ctx->error_ipv4);
    if (ctx->error_ipv6)
        g_error_free (ctx->error_ipv6);
    if (ctx->client_ipv4)
        g_object_unref (ctx->client_ipv4);
    if (ctx->client_ipv6)
        g_object_unref (ctx->client_ipv6);
    g_object_unref (ctx->data);
    g_object_unref (ctx->qmi);
    g_object_unref (ctx->cancellable);
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

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);
    g_assert (!(ctx->running_ipv4 && ctx->running_ipv6));

    output = qmi_client_wds_start_network_finish (client, res, &error);
    if (output &&
        !qmi_message_wds_start_network_output_get_result (output, &error)) {
        /* No-effect errors should be ignored. The modem will keep the
         * connection active as long as there is a WDS client which requested
         * to start the network. If ModemManager crashed while a connection was
         * active, we would be leaving an unreleased WDS client around and the
         * modem would just keep connected. */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_error_free (error);
            error = NULL;
            if (ctx->running_ipv4)
                ctx->packet_data_handle_ipv4 = GLOBAL_PACKET_DATA_HANDLE;
            else
                ctx->packet_data_handle_ipv6 = GLOBAL_PACKET_DATA_HANDLE;

            /* Fall down to a successful connection */
        } else {
            mm_info ("error: couldn't start network: %s", error->message);
            if (g_error_matches (error,
                                 QMI_PROTOCOL_ERROR,
                                 QMI_PROTOCOL_ERROR_CALL_FAILED)) {
                QmiWdsCallEndReason cer;
                QmiWdsVerboseCallEndReasonType verbose_cer_type;
                gint16 verbose_cer_reason;

                if (qmi_message_wds_start_network_output_get_call_end_reason (
                        output,
                        &cer,
                        NULL))
                    mm_info ("call end reason (%u): '%s'",
                             cer,
                             qmi_wds_call_end_reason_get_string (cer));

                if (qmi_message_wds_start_network_output_get_verbose_call_end_reason (
                        output,
                        &verbose_cer_type,
                        &verbose_cer_reason,
                        NULL))
                    mm_info ("verbose call end reason (%u,%d): [%s] %s",
                             verbose_cer_type,
                             verbose_cer_reason,
                             qmi_wds_verbose_call_end_reason_type_get_string (verbose_cer_type),
                             qmi_wds_verbose_call_end_reason_get_string (verbose_cer_type, verbose_cer_reason));
            }
        }
    }

    if (error) {
        if (ctx->running_ipv4)
            ctx->error_ipv4 = error;
        else
            ctx->error_ipv6 = error;
    } else {
        if (ctx->running_ipv4)
            qmi_message_wds_start_network_output_get_packet_data_handle (output, &ctx->packet_data_handle_ipv4, NULL);
        else
            qmi_message_wds_start_network_output_get_packet_data_handle (output, &ctx->packet_data_handle_ipv6, NULL);
    }

    if (output)
        qmi_message_wds_start_network_output_unref (output);

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static QmiMessageWdsStartNetworkInput *
build_start_network_input (ConnectContext *ctx)
{
    QmiMessageWdsStartNetworkInput *input;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);
    g_assert (!(ctx->running_ipv4 && ctx->running_ipv6));

    input = qmi_message_wds_start_network_input_new ();

    if (ctx->apn)
        qmi_message_wds_start_network_input_set_apn (input, ctx->apn, NULL);

    if (ctx->auth != QMI_WDS_AUTHENTICATION_NONE) {
        qmi_message_wds_start_network_input_set_authentication_preference (input, ctx->auth, NULL);

        if (ctx->user)
            qmi_message_wds_start_network_input_set_username (input, ctx->user, NULL);
        if (ctx->password)
            qmi_message_wds_start_network_input_set_password (input, ctx->password, NULL);
    }

    /* Only add the IP family preference TLV if explicitly requested a given
     * family. This TLV may be newer than the Start Network command itself, so
     * we'll just allow the case where none is specified. Also, don't add this
     * TLV if we already set a default IP family preference with "WDS Set IP
     * Family" */
    if (!ctx->no_ip_family_preference &&
        !ctx->default_ip_family_set) {
        qmi_message_wds_start_network_input_set_ip_family_preference (
            input,
            (ctx->running_ipv6 ? QMI_WDS_IP_FAMILY_IPV6 : QMI_WDS_IP_FAMILY_IPV4),
            NULL);
    }

    return input;
}

static void
set_ip_family_ready (QmiClientWds *client,
                     GAsyncResult *res,
                     ConnectContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsSetIpFamilyOutput *output;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);
    g_assert (!(ctx->running_ipv4 && ctx->running_ipv6));

    output = qmi_client_wds_set_ip_family_finish (client, res, &error);
    if (output) {
        qmi_message_wds_set_ip_family_output_get_result (output, &error);
        qmi_message_wds_set_ip_family_output_unref (output);
    }

    if (error) {
        /* Ensure we add the IP family preference TLV */
        mm_dbg ("Couldn't set IP family preference: '%s'", error->message);
        g_error_free (error);
        ctx->default_ip_family_set = FALSE;
    } else {
        /* No need to add IP family preference */
        ctx->default_ip_family_set = TRUE;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
qmi_port_allocate_client_ready (MMQmiPort *qmi,
                                GAsyncResult *res,
                                ConnectContext *ctx)
{
    GError *error = NULL;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);
    g_assert (!(ctx->running_ipv4 && ctx->running_ipv6));

    if (!mm_qmi_port_allocate_client_finish (qmi, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    if (ctx->running_ipv4)
        ctx->client_ipv4 = QMI_CLIENT_WDS (mm_qmi_port_get_client (qmi,
                                                                   QMI_SERVICE_WDS,
                                                                   MM_QMI_PORT_FLAG_WDS_IPV4));
    else
        ctx->client_ipv6 = QMI_CLIENT_WDS (mm_qmi_port_get_client (qmi,
                                                                   QMI_SERVICE_WDS,
                                                                   MM_QMI_PORT_FLAG_WDS_IPV6));

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

        g_assert (ctx->ipv4 || ctx->ipv6);

        /* Fall down */
        ctx->step++;

    case CONNECT_STEP_OPEN_QMI_PORT:
        if (!mm_qmi_port_is_open (ctx->qmi)) {
            mm_qmi_port_open (ctx->qmi,
                              TRUE,
                              ctx->cancellable,
                              (GAsyncReadyCallback)qmi_port_open_ready,
                              ctx);
            return;
        }

        /* If already open, just fall down */
        ctx->step++;

    case CONNECT_STEP_IPV4:
        /* If no IPv4 setup needed, jump to IPv6 */
        if (!ctx->ipv4) {
            ctx->step = CONNECT_STEP_IPV6;
            connect_context_step (ctx);
            return;
        }

        /* Start IPv4 setup */
        mm_dbg ("Running IPv4 connection setup");
        ctx->running_ipv4 = TRUE;
        ctx->running_ipv6 = FALSE;
        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_WDS_CLIENT_IPV4: {
        QmiClient *client;

        client = mm_qmi_port_get_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         MM_QMI_PORT_FLAG_WDS_IPV4);
        if (!client) {
            mm_dbg ("Allocating IPv4-specific WDS client");
            mm_qmi_port_allocate_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         MM_QMI_PORT_FLAG_WDS_IPV4,
                                         ctx->cancellable,
                                         (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                         ctx);
            return;
        }

        ctx->client_ipv4 = QMI_CLIENT_WDS (client);
        /* Just fall down */
        ctx->step++;
    }

    case CONNECT_STEP_IP_FAMILY_IPV4:
        /* If client is new enough, select IP family */
        if (!ctx->no_ip_family_preference &&
            qmi_client_check_version (QMI_CLIENT (ctx->client_ipv4), 1, 9)) {
            QmiMessageWdsSetIpFamilyInput *input;

            mm_dbg ("Setting default IP family to: IPv4");
            input = qmi_message_wds_set_ip_family_input_new ();
            qmi_message_wds_set_ip_family_input_set_preference (input, QMI_WDS_IP_FAMILY_IPV4, NULL);
            qmi_client_wds_set_ip_family (ctx->client_ipv4,
                                          input,
                                          10,
                                          ctx->cancellable,
                                          (GAsyncReadyCallback)set_ip_family_ready,
                                          ctx);
            qmi_message_wds_set_ip_family_input_unref (input);
            return;
        }

        ctx->default_ip_family_set = FALSE;

        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_START_NETWORK_IPV4: {
        QmiMessageWdsStartNetworkInput *input;

        mm_dbg ("Starting IPv4 connection...");
        input = build_start_network_input (ctx);
        qmi_client_wds_start_network (ctx->client_ipv4,
                                      input,
                                      10,
                                      ctx->cancellable,
                                      (GAsyncReadyCallback)start_network_ready,
                                      ctx);
        qmi_message_wds_start_network_input_unref (input);
        return;
    }

    case CONNECT_STEP_IPV6:
        /* If no IPv6 setup needed, jump to last */
        if (!ctx->ipv6) {
            ctx->step = CONNECT_STEP_LAST;
            connect_context_step (ctx);
            return;
        }

        /* Start IPv6 setup */
        mm_dbg ("Running IPv6 connection setup");
        ctx->running_ipv4 = FALSE;
        ctx->running_ipv6 = TRUE;
        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_WDS_CLIENT_IPV6: {
        QmiClient *client;

        client = mm_qmi_port_get_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         MM_QMI_PORT_FLAG_WDS_IPV6);
        if (!client) {
            mm_dbg ("Allocating IPv6-specific WDS client");
            mm_qmi_port_allocate_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         MM_QMI_PORT_FLAG_WDS_IPV6,
                                         ctx->cancellable,
                                         (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                         ctx);
            return;
        }

        ctx->client_ipv6 = QMI_CLIENT_WDS (client);
        /* Just fall down */
        ctx->step++;
    }

    case CONNECT_STEP_IP_FAMILY_IPV6:

        g_assert (ctx->no_ip_family_preference == FALSE);

        /* If client is new enough, select IP family */
        if (qmi_client_check_version (QMI_CLIENT (ctx->client_ipv6), 1, 9)) {
            QmiMessageWdsSetIpFamilyInput *input;

            mm_dbg ("Setting default IP family to: IPv6");
            input = qmi_message_wds_set_ip_family_input_new ();
            qmi_message_wds_set_ip_family_input_set_preference (input, QMI_WDS_IP_FAMILY_IPV6, NULL);
            qmi_client_wds_set_ip_family (ctx->client_ipv6,
                                          input,
                                          10,
                                          ctx->cancellable,
                                          (GAsyncReadyCallback)set_ip_family_ready,
                                          ctx);
            qmi_message_wds_set_ip_family_input_unref (input);
            return;
        }

        ctx->default_ip_family_set = FALSE;

        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_START_NETWORK_IPV6: {
        QmiMessageWdsStartNetworkInput *input;

        mm_dbg ("Starting IPv6 connection...");
        input = build_start_network_input (ctx);
        qmi_client_wds_start_network (ctx->client_ipv6,
                                      input,
                                      10,
                                      ctx->cancellable,
                                      (GAsyncReadyCallback)start_network_ready,
                                      ctx);
        qmi_message_wds_start_network_input_unref (input);
        return;
    }

    case CONNECT_STEP_LAST:
        /* If one of IPv4 or IPv6 succeeds, we're connected */
        if (ctx->packet_data_handle_ipv4 || ctx->packet_data_handle_ipv6) {
            MMBearerIpConfig *config;
            ConnectResult *result;

            /* Port is connected; update the state */
            mm_port_set_connected (MM_PORT (ctx->data), TRUE);

            /* Keep connection related data */
            g_assert (ctx->self->priv->data == NULL);
            ctx->self->priv->data = g_object_ref (ctx->data);

            g_assert (ctx->self->priv->packet_data_handle_ipv4 == 0);
            g_assert (ctx->self->priv->client_ipv4 == NULL);
            if (ctx->packet_data_handle_ipv4) {
                ctx->self->priv->packet_data_handle_ipv4 = ctx->packet_data_handle_ipv4;
                ctx->self->priv->client_ipv4 = g_object_ref (ctx->client_ipv4);
            }

            g_assert (ctx->self->priv->packet_data_handle_ipv6 == 0);
            g_assert (ctx->self->priv->client_ipv6 == NULL);
            if (ctx->packet_data_handle_ipv6) {
                ctx->self->priv->packet_data_handle_ipv6 = ctx->packet_data_handle_ipv6;
                ctx->self->priv->client_ipv6 = g_object_ref (ctx->client_ipv6);
            }

            /* Build IP config; always DHCP based */
            config = mm_bearer_ip_config_new ();
            mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_DHCP);

            /* Build result */
            result = g_slice_new0 (ConnectResult);
            result->data = g_object_ref (ctx->data);
            if (ctx->packet_data_handle_ipv4)
                result->ipv4_config = g_object_ref (config);
            if (ctx->packet_data_handle_ipv6)
                result->ipv6_config = g_object_ref (config);

            g_object_unref (config);

            /* Set operation result */
            g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                       result,
                                                       (GDestroyNotify)connect_result_free);
        } else {
            GError *error;

            /* No connection, set error. If both set, IPv4 error preferred */
            if (ctx->error_ipv4) {
                error = ctx->error_ipv4;
                ctx->error_ipv4 = NULL;
            } else {
                error = ctx->error_ipv6;
                ctx->error_ipv6 = NULL;
            }

            g_simple_async_result_take_error (ctx->result, error);
        }

        connect_context_complete_and_free (ctx);
        return;
    }
}

static void
connect (MMBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    MMBearerProperties *properties = NULL;
    ConnectContext *ctx;
    MMBaseModem *modem  = NULL;
    MMPort *data;
    MMQmiPort *qmi;
    GError *error = NULL;

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
    qmi = mm_base_modem_get_port_qmi_for_data (modem, data, &error);
    if (!qmi) {
        g_simple_async_report_take_gerror_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            error);
        g_object_unref (data);
        g_object_unref (modem);
        return;
    }

    g_object_unref (modem);

    mm_dbg ("Launching connection with QMI port (%s/%s) and data port (%s/%s)",
            mm_port_subsys_get_string (mm_port_get_subsys (MM_PORT (qmi))),
            mm_port_get_device (MM_PORT (qmi)),
            mm_port_subsys_get_string (mm_port_get_subsys (data)),
            mm_port_get_device (data));

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

    g_object_get (self,
                  MM_BEARER_CONFIG, &properties,
                  NULL);

    if (properties) {
        MMBearerAllowedAuth auth;

        ctx->apn = g_strdup (mm_bearer_properties_get_apn (properties));
        ctx->user = g_strdup (mm_bearer_properties_get_user (properties));
        ctx->password = g_strdup (mm_bearer_properties_get_password (properties));
        switch (mm_bearer_properties_get_ip_type (properties)) {
        case MM_BEARER_IP_FAMILY_IPV4:
            ctx->ipv4 = TRUE;
            ctx->ipv6 = FALSE;
            break;
        case MM_BEARER_IP_FAMILY_IPV6:
            ctx->ipv4 = FALSE;
            ctx->ipv6 = TRUE;
            break;
        case MM_BEARER_IP_FAMILY_IPV4V6:
            ctx->ipv4 = TRUE;
            ctx->ipv6 = TRUE;
            break;
        case MM_BEARER_IP_FAMILY_UNKNOWN:
        default:
            mm_dbg ("No specific IP family requested, defaulting to IPv4");
            ctx->no_ip_family_preference = TRUE;
            ctx->ipv4 = TRUE;
            ctx->ipv6 = FALSE;
            break;
        }

        auth = mm_bearer_properties_get_allowed_auth (properties);
        g_object_unref (properties);

        if (auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
            mm_dbg ("Using default (PAP) authentication method");
            ctx->auth = QMI_WDS_AUTHENTICATION_PAP;
        } else if (auth & (MM_BEARER_ALLOWED_AUTH_PAP |
                           MM_BEARER_ALLOWED_AUTH_CHAP |
                           MM_BEARER_ALLOWED_AUTH_NONE)) {
            /* Only PAP and/or CHAP or NONE are supported */
            ctx->auth = mm_bearer_allowed_auth_to_qmi_authentication (auth);
        } else {
            gchar *str;

            str = mm_bearer_allowed_auth_build_string_from_mask (auth);
            g_simple_async_result_set_error (
                ctx->result,
                MM_CORE_ERROR,
                MM_CORE_ERROR_UNSUPPORTED,
                "Cannot use any of the specified authentication methods (%s)",
                str);
            g_free (str);
            connect_context_complete_and_free (ctx);
            return;
        }
    }

    /* Run! */
    connect_context_step (ctx);
}

/*****************************************************************************/
/* Disconnect */

typedef enum {
    DISCONNECT_STEP_FIRST,
    DISCONNECT_STEP_STOP_NETWORK_IPV4,
    DISCONNECT_STEP_STOP_NETWORK_IPV6,
    DISCONNECT_STEP_LAST
} DisconnectStep;

typedef struct {
    MMBearerQmi *self;
    GSimpleAsyncResult *result;
    MMPort *data;
    DisconnectStep step;

    gboolean running_ipv4;
    QmiClientWds *client_ipv4;
    guint32 packet_data_handle_ipv4;
    GError *error_ipv4;

    gboolean running_ipv6;
    QmiClientWds *client_ipv6;
    guint32 packet_data_handle_ipv6;
    GError *error_ipv6;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->error_ipv4)
        g_error_free (ctx->error_ipv4);
    if (ctx->error_ipv6)
        g_error_free (ctx->error_ipv6);
    if (ctx->client_ipv4)
        g_object_unref (ctx->client_ipv4);
    if (ctx->client_ipv6)
        g_object_unref (ctx->client_ipv6);
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
reset_bearer_connection (MMBearerQmi *self,
                         gboolean reset_ipv4,
                         gboolean reset_ipv6)
{
    if (reset_ipv4) {
        self->priv->packet_data_handle_ipv4 = 0;
        g_clear_object (&self->priv->client_ipv4);
    }

    if (reset_ipv6) {
        self->priv->packet_data_handle_ipv6 = 0;
        g_clear_object (&self->priv->client_ipv6);
    }

    if (!self->priv->packet_data_handle_ipv4 &&
        !self->priv->packet_data_handle_ipv6) {
        if (self->priv->data) {
            /* Port is disconnected; update the state */
            mm_port_set_connected (self->priv->data, FALSE);
            g_clear_object (&self->priv->data);
        }
    }
}

static void disconnect_context_step (DisconnectContext *ctx);

static void
stop_network_ready (QmiClientWds *client,
                    GAsyncResult *res,
                    DisconnectContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsStopNetworkOutput *output;

    output = qmi_client_wds_stop_network_finish (client, res, &error);
    if (output &&
        !qmi_message_wds_stop_network_output_get_result (output, &error)) {
        /* No effect error, we're already disconnected */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_error_free (error);
            error = NULL;
        }
    }

    if (error) {
        if (ctx->running_ipv4)
            ctx->error_ipv4 = error;
        else
            ctx->error_ipv6 = error;
    } else {
        /* Clear internal status */
        reset_bearer_connection (ctx->self,
                                 ctx->running_ipv4,
                                 ctx->running_ipv6);
    }

    if (output)
        qmi_message_wds_stop_network_output_unref (output);

    /* Keep on */
    ctx->step++;
    disconnect_context_step (ctx);
}

static void
disconnect_context_step (DisconnectContext *ctx)
{
    switch (ctx->step) {
    case DISCONNECT_STEP_FIRST:
        /* Fall down */
        ctx->step++;

    case DISCONNECT_STEP_STOP_NETWORK_IPV4:
        if (ctx->packet_data_handle_ipv4) {
            QmiMessageWdsStopNetworkInput *input;

            input = qmi_message_wds_stop_network_input_new ();
            qmi_message_wds_stop_network_input_set_packet_data_handle (input, ctx->packet_data_handle_ipv4, NULL);

            ctx->running_ipv4 = TRUE;
            ctx->running_ipv6 = FALSE;
            qmi_client_wds_stop_network (ctx->client_ipv4,
                                         input,
                                         10,
                                         NULL,
                                         (GAsyncReadyCallback)stop_network_ready,
                                         ctx);
            return;
        }

        /* Fall down */
        ctx->step++;

    case DISCONNECT_STEP_STOP_NETWORK_IPV6:
        if (ctx->packet_data_handle_ipv6) {
            QmiMessageWdsStopNetworkInput *input;

            input = qmi_message_wds_stop_network_input_new ();
            qmi_message_wds_stop_network_input_set_packet_data_handle (input, ctx->packet_data_handle_ipv6, NULL);

            ctx->running_ipv4 = FALSE;
            ctx->running_ipv6 = TRUE;
            qmi_client_wds_stop_network (ctx->client_ipv6,
                                         input,
                                         10,
                                         NULL,
                                         (GAsyncReadyCallback)stop_network_ready,
                                         ctx);
            return;
        }

        /* Fall down */
        ctx->step++;

    case DISCONNECT_STEP_LAST:
        if (!ctx->error_ipv4 && !ctx->error_ipv6)
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        else {
            GError *error;

            /* If both set, IPv4 error preferred */
            if (ctx->error_ipv4) {
                error = ctx->error_ipv4;
                ctx->error_ipv4 = NULL;
            } else {
                error = ctx->error_ipv6;
                ctx->error_ipv6 = NULL;
            }

            g_simple_async_result_take_error (ctx->result, error);
        }

        disconnect_context_complete_and_free (ctx);
        return;
    }
}

static void
disconnect (MMBearer *_self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMBearerQmi *self = MM_BEARER_QMI (_self);
    DisconnectContext *ctx;

    if ((!self->priv->packet_data_handle_ipv4 && !self->priv->packet_data_handle_ipv6) ||
        (!self->priv->client_ipv4 && !self->priv->client_ipv6) ||
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
    ctx->client_ipv4 = self->priv->client_ipv4 ? g_object_ref (self->priv->client_ipv4) : NULL;
    ctx->packet_data_handle_ipv4 = self->priv->packet_data_handle_ipv4;
    ctx->client_ipv6 = self->priv->client_ipv6 ? g_object_ref (self->priv->client_ipv6) : NULL;
    ctx->packet_data_handle_ipv6 = self->priv->packet_data_handle_ipv6;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect);
    ctx->step = DISCONNECT_STEP_FIRST;

    /* Run! */
    disconnect_context_step (ctx);
}

/*****************************************************************************/

static void
report_disconnection (MMBearer *self)
{
    /* Cleanup all connection related data */
    reset_bearer_connection (MM_BEARER_QMI (self), TRUE, TRUE);

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
    g_clear_object (&self->priv->client_ipv4);
    g_clear_object (&self->priv->client_ipv6);

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
