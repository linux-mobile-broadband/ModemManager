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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <arpa/inet.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <libqmi-glib.h>

#include "mm-log.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-shared-qmi.h"

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-qmi-private-tag"
static GQuark private_quark;

typedef struct {
    /* Location helpers */
    MMIfaceModemLocation  *iface_modem_location_parent;
    MMModemLocationSource  enabled_sources;
    QmiClient             *pds_client;
    gulong                 pds_location_event_report_indication_id;
} Private;

static void
private_free (Private *priv)
{
    if (priv->pds_location_event_report_indication_id)
        g_signal_handler_disconnect (priv->pds_client, priv->pds_location_event_report_indication_id);
    if (priv->pds_client)
        g_object_unref (priv->pds_client);
    g_slice_free (Private, priv);
}

static Private *
get_private (MMSharedQmi *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);

        /* Setup parent class' MMIfaceModemLocation */
        g_assert (MM_SHARED_QMI_GET_INTERFACE (self)->peek_parent_location_interface);
        priv->iface_modem_location_parent = MM_SHARED_QMI_GET_INTERFACE (self)->peek_parent_location_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/
/* Location: Set SUPL server */

gboolean
mm_shared_qmi_location_set_supl_server_finish (MMIfaceModemLocation  *self,
                                               GAsyncResult          *res,
                                               GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_agps_config_ready (QmiClientPds *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    QmiMessagePdsSetAgpsConfigOutput *output;
    GError                           *error = NULL;

    output = qmi_client_pds_set_agps_config_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_agps_config_output_get_result (output, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);

    qmi_message_pds_set_agps_config_output_unref (output);
}

static gboolean
parse_as_ip_port (const gchar *supl,
                  guint32     *out_ip,
                  guint32     *out_port)
{
    gboolean   valid = FALSE;
    gchar    **split;
    guint      port;
    guint32    ip;

    split = g_strsplit (supl, ":", -1);
    if (g_strv_length (split) != 2)
        goto out;

    if (!mm_get_uint_from_str (split[1], &port))
        goto out;
    if (port == 0 || port > G_MAXUINT16)
        goto out;
    if (inet_pton (AF_INET, split[0], &ip) <= 0)
        goto out;

    *out_ip = ip;
    *out_port = port;
    valid = TRUE;

out:
    g_strfreev (split);
    return valid;
}

static gboolean
parse_as_url (const gchar  *supl,
              GArray      **out_url)
{
    gchar *utf16;
    gsize  utf16_len;

    utf16 = g_convert (supl, -1, "UTF-16BE", "UTF-8", NULL, &utf16_len, NULL);
    *out_url = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (guint8), utf16_len),
                                    utf16,
                                    utf16_len);
    g_free (utf16);
    return TRUE;
}

void
mm_shared_qmi_location_set_supl_server (MMIfaceModemLocation *self,
                                        const gchar          *supl,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data)
{
    QmiClient                       *client = NULL;
    QmiMessagePdsSetAgpsConfigInput *input;
    guint32                          ip;
    guint32                          port;
    GArray                          *url;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_PDS, &client,
                                      callback, user_data))
        return;

    input = qmi_message_pds_set_agps_config_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_pds_set_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_UMTS, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_pds_set_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_CDMA, NULL);

    if (parse_as_ip_port (supl, &ip, &port))
        qmi_message_pds_set_agps_config_input_set_location_server_address (input, ip, port, NULL);
    else if (parse_as_url (supl, &url)) {
        qmi_message_pds_set_agps_config_input_set_location_server_url (input, url, NULL);
        g_array_unref (url);
    } else
        g_assert_not_reached ();

    qmi_client_pds_set_agps_config (
        QMI_CLIENT_PDS (client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)set_agps_config_ready,
        g_task_new (self, NULL, callback, user_data));
    qmi_message_pds_set_agps_config_input_unref (input);
}

/*****************************************************************************/
/* Location: Load SUPL server */

gchar *
mm_shared_qmi_location_load_supl_server_finish (MMIfaceModemLocation  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
get_agps_config_ready (QmiClientPds *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    QmiMessagePdsGetAgpsConfigOutput *output = NULL;
    GError                           *error = NULL;
    guint32                           ip = 0;
    guint32                           port = 0;
    GArray                           *url = NULL;
    gchar                            *str = NULL;

    output = qmi_client_pds_get_agps_config_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        goto out;
    }

    if (!qmi_message_pds_get_agps_config_output_get_result (output, &error))
        goto out;

    /* Prefer IP/PORT to URL */
    if (qmi_message_pds_get_agps_config_output_get_location_server_address (
            output,
            &ip,
            &port,
            NULL) &&
        ip != 0 &&
        port != 0) {
        struct in_addr a = { .s_addr = ip };
        gchar buf[INET_ADDRSTRLEN + 1];

        memset (buf, 0, sizeof (buf));

        if (!inet_ntop (AF_INET, &a, buf, sizeof (buf) - 1)) {
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot convert numeric IP address (%u) to string", ip);
            goto out;
        }

        str = g_strdup_printf ("%s:%u", buf, port);
        goto out;
    }

    if (qmi_message_pds_get_agps_config_output_get_location_server_url (
            output,
            &url,
            NULL) &&
        url->len > 0) {
        str = g_convert (url->data, url->len, "UTF-8", "UTF-16BE", NULL, NULL, NULL);
    }

    if (!str)
        str = g_strdup ("");

out:
    if (error)
        g_task_return_error (task, error);
    else {
        g_assert (str);
        g_task_return_pointer (task, str, g_free);
    }
    g_object_unref (task);

    if (output)
        qmi_message_pds_get_agps_config_output_unref (output);
}

void
mm_shared_qmi_location_load_supl_server (MMIfaceModemLocation *self,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data)
{
    QmiClient                       *client = NULL;
    QmiMessagePdsGetAgpsConfigInput *input;

    if (!mm_shared_qmi_ensure_client (MM_SHARED_QMI (self),
                                      QMI_SERVICE_PDS, &client,
                                      callback, user_data))
        return;

    input = qmi_message_pds_get_agps_config_input_new ();

    /* For multimode devices, prefer UMTS by default */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (self)))
        qmi_message_pds_get_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_UMTS, NULL);
    else if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (self)))
        qmi_message_pds_get_agps_config_input_set_network_mode (input, QMI_PDS_NETWORK_MODE_CDMA, NULL);

    qmi_client_pds_get_agps_config (
        QMI_CLIENT_PDS (client),
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)get_agps_config_ready,
        g_task_new (self, NULL, callback, user_data));
    qmi_message_pds_get_agps_config_input_unref (input);
}

/*****************************************************************************/
/* Location: internal helper: stop gps engine */

static gboolean
stop_gps_engine_finish (MMSharedQmi   *self,
                        GAsyncResult  *res,
                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_gps_service_state_stop_ready (QmiClientPds *client,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    MMSharedQmi                           *self;
    Private                               *priv;
    QmiMessagePdsSetGpsServiceStateOutput *output;
    GError                                *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }

        g_error_free (error);
    }

    qmi_message_pds_set_gps_service_state_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    if (priv->pds_client) {
        if (priv->pds_location_event_report_indication_id != 0) {
            g_signal_handler_disconnect (priv->pds_client, priv->pds_location_event_report_indication_id);
            priv->pds_location_event_report_indication_id = 0;
        }
        g_clear_object (&priv->pds_client);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
stop_gps_engine (MMSharedQmi         *self,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    priv = get_private (self);

    task = g_task_new (self, NULL, callback, user_data);

    if (priv->pds_client) {
        QmiMessagePdsSetGpsServiceStateInput *input;

        input = qmi_message_pds_set_gps_service_state_input_new ();
        qmi_message_pds_set_gps_service_state_input_set_state (input, FALSE, NULL);
        qmi_client_pds_set_gps_service_state (
            QMI_CLIENT_PDS (priv->pds_client),
            input,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_gps_service_state_stop_ready,
            task);
        qmi_message_pds_set_gps_service_state_input_unref (input);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helper: NMEA indication callback */

static void
pds_location_event_report_indication_cb (QmiClientPds                      *client,
                                         QmiIndicationPdsEventReportOutput *output,
                                         MMSharedQmi                       *self)
{
    QmiPdsPositionSessionStatus  session_status;
    const gchar                 *nmea;

    if (qmi_indication_pds_event_report_output_get_position_session_status (
            output,
            &session_status,
            NULL)) {
        mm_dbg ("[GPS] session status changed: '%s'",
                qmi_pds_position_session_status_get_string (session_status));
    }

    if (qmi_indication_pds_event_report_output_get_nmea_position (
            output,
            &nmea,
            NULL)) {
        mm_dbg ("[NMEA] %s", nmea);
        mm_iface_modem_location_gps_update (MM_IFACE_MODEM_LOCATION (self), nmea);
    }
}

/*****************************************************************************/
/* Location: internal helper: start gps engine */

static gboolean
start_gps_engine_finish (MMSharedQmi   *self,
                         GAsyncResult  *res,
                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_ser_location_ready (QmiClientPds *client,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMSharedQmi                       *self;
    Private                           *priv;
    QmiMessagePdsSetEventReportOutput *output;
    GError                            *error = NULL;

    output = qmi_client_pds_set_event_report_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_event_report_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set event report: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_set_event_report_output_unref (output);
        return;
    }

    qmi_message_pds_set_event_report_output_unref (output);

    self = g_task_get_source_object (task);
    priv = get_private (self);

    g_assert (!priv->pds_client);
    g_assert (priv->pds_location_event_report_indication_id == 0);
    priv->pds_client = g_object_ref (client);
    priv->pds_location_event_report_indication_id =
        g_signal_connect (priv->pds_client,
                          "event-report",
                          G_CALLBACK (pds_location_event_report_indication_cb),
                          self);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
pds_auto_tracking_state_start_ready (QmiClientPds *client,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    QmiMessagePdsSetEventReportInput        *input;
    QmiMessagePdsSetAutoTrackingStateOutput *output = NULL;
    GError                                  *error = NULL;

    output = qmi_client_pds_set_auto_tracking_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_auto_tracking_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set auto-tracking state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_auto_tracking_state_output_unref (output);
            return;
        }
        g_error_free (error);
    }

    qmi_message_pds_set_auto_tracking_state_output_unref (output);

    /* Only gather standard NMEA traces */
    input = qmi_message_pds_set_event_report_input_new ();
    qmi_message_pds_set_event_report_input_set_nmea_position_reporting (input, TRUE, NULL);
    qmi_client_pds_set_event_report (
        client,
        input,
        5,
        NULL,
        (GAsyncReadyCallback)pds_ser_location_ready,
        task);
    qmi_message_pds_set_event_report_input_unref (input);
}

static void
pds_gps_service_state_start_ready (QmiClientPds *client,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    QmiMessagePdsSetAutoTrackingStateInput *input;
    QmiMessagePdsSetGpsServiceStateOutput  *output;
    GError                                 *error = NULL;

    output = qmi_client_pds_set_gps_service_state_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_gps_service_state_output_get_result (output, &error)) {
        if (!g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_prefix_error (&error, "Couldn't set GPS service state: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            qmi_message_pds_set_gps_service_state_output_unref (output);
            return;
        }
        g_error_free (error);
    }

    qmi_message_pds_set_gps_service_state_output_unref (output);

    /* Enable auto-tracking for a continuous fix */
    input = qmi_message_pds_set_auto_tracking_state_input_new ();
    qmi_message_pds_set_auto_tracking_state_input_set_state (input, TRUE, NULL);
    qmi_client_pds_set_auto_tracking_state (
        client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_auto_tracking_state_start_ready,
        task);
    qmi_message_pds_set_auto_tracking_state_input_unref (input);
}

static void
start_gps_engine (MMSharedQmi         *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    QmiClient *client;
    GTask     *task;

    task = g_task_new (self, NULL, callback, user_data);

    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        QmiMessagePdsSetGpsServiceStateInput *input;

        input = qmi_message_pds_set_gps_service_state_input_new ();
        qmi_message_pds_set_gps_service_state_input_set_state (input, TRUE, NULL);
        qmi_client_pds_set_gps_service_state (
            QMI_CLIENT_PDS (client),
            input,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_gps_service_state_start_ready,
            task);
        qmi_message_pds_set_gps_service_state_input_unref (input);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: internal helper: select operation mode (assisted/standalone) */

typedef enum {
    GPS_OPERATION_MODE_UNKNOWN,
    GPS_OPERATION_MODE_STANDALONE,
    GPS_OPERATION_MODE_ASSISTED,
} GpsOperationMode;

typedef struct {
    QmiClient        *client;
    GpsOperationMode  mode;
} SetGpsOperationModeContext;

static void
set_gps_operation_mode_context_free (SetGpsOperationModeContext *ctx)
{
    if (ctx->client)
        g_object_unref (ctx->client);
    g_slice_free (SetGpsOperationModeContext, ctx);
}

static gboolean
set_gps_operation_mode_finish (MMSharedQmi   *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pds_set_default_tracking_session_ready (QmiClientPds *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    SetGpsOperationModeContext                   *ctx;
    QmiMessagePdsSetDefaultTrackingSessionOutput *output;
    GError                                       *error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_pds_set_default_tracking_session_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_set_default_tracking_session_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't set default tracking session: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_set_default_tracking_session_output_unref (output);
        return;
    }

    qmi_message_pds_set_default_tracking_session_output_unref (output);

    mm_dbg ("A-GPS %s", ctx->mode == GPS_OPERATION_MODE_ASSISTED ? "enabled" : "disabled");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
pds_get_default_tracking_session_ready (QmiClientPds *client,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    MMSharedQmi                                  *self;
    SetGpsOperationModeContext                   *ctx;
    QmiMessagePdsSetDefaultTrackingSessionInput  *input;
    QmiMessagePdsGetDefaultTrackingSessionOutput *output;
    GError                                       *error = NULL;
    QmiPdsOperatingMode                           session_operation;
    guint8                                        data_timeout;
    guint32                                       interval;
    guint32                                       accuracy_threshold;

    output = qmi_client_pds_get_default_tracking_session_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_pds_get_default_tracking_session_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get default tracking session: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        qmi_message_pds_get_default_tracking_session_output_unref (output);
        return;
    }

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    qmi_message_pds_get_default_tracking_session_output_get_info (
        output,
        &session_operation,
        &data_timeout,
        &interval,
        &accuracy_threshold,
        NULL);

    qmi_message_pds_get_default_tracking_session_output_unref (output);

    if (ctx->mode == GPS_OPERATION_MODE_ASSISTED) {
        if (session_operation == QMI_PDS_OPERATING_MODE_MS_ASSISTED) {
            mm_dbg ("A-GPS already enabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_dbg ("Need to enable A-GPS");
        session_operation = QMI_PDS_OPERATING_MODE_MS_ASSISTED;
    } else if (ctx->mode == GPS_OPERATION_MODE_STANDALONE) {
        if (session_operation == QMI_PDS_OPERATING_MODE_STANDALONE) {
            mm_dbg ("A-GPS already disabled");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        mm_dbg ("Need to disable A-GPS");
        session_operation = QMI_PDS_OPERATING_MODE_STANDALONE;
    } else
        g_assert_not_reached ();

    input = qmi_message_pds_set_default_tracking_session_input_new ();
    qmi_message_pds_set_default_tracking_session_input_set_info (
        input,
        session_operation,
        data_timeout,
        interval,
        accuracy_threshold,
        NULL);
    qmi_client_pds_set_default_tracking_session (
        client,
        input,
        10,
        NULL, /* cancellable */
        (GAsyncReadyCallback)pds_set_default_tracking_session_ready,
        task);
    qmi_message_pds_set_default_tracking_session_input_unref (input);
}

static void
set_gps_operation_mode (MMSharedQmi         *self,
                        GpsOperationMode     mode,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    SetGpsOperationModeContext *ctx;
    GTask                      *task;
    QmiClient                  *client;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (SetGpsOperationModeContext);
    ctx->mode = mode;
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_gps_operation_mode_context_free);

    client = mm_shared_qmi_peek_client (MM_SHARED_QMI (self),
                                        QMI_SERVICE_PDS,
                                        MM_PORT_QMI_FLAG_DEFAULT,
                                        NULL);
    if (client) {
        ctx->client = g_object_ref (client);
        qmi_client_pds_get_default_tracking_session (
            QMI_CLIENT_PDS (ctx->client),
            NULL,
            10,
            NULL, /* cancellable */
            (GAsyncReadyCallback)pds_get_default_tracking_session_ready,
            task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't find any PDS client");
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: disable */

gboolean
mm_shared_qmi_disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                 GAsyncResult          *res,
                                                 GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
stop_gps_engine_ready (MMSharedQmi  *self,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    if (!stop_gps_engine_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv = get_private (self);

    priv->enabled_sources &= ~source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
set_gps_operation_mode_standalone_ready (MMSharedQmi  *self,
                                         GAsyncResult *res,
                                         GTask        *task)
{
    GError  *error = NULL;
    Private *priv;

    if (!set_gps_operation_mode_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    priv = get_private (self);

    priv->enabled_sources &= ~MM_MODEM_LOCATION_SOURCE_AGPS;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_qmi_disable_location_gathering (MMIfaceModemLocation  *_self,
                                          MMModemLocationSource  source,
                                          GAsyncReadyCallback    callback,
                                          gpointer               user_data)
{
    MMSharedQmi            *self;
    Private                *priv;
    GTask                  *task;
    MMModemLocationSource   tmp;

    self = MM_SHARED_QMI (_self);
    priv = get_private (self);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    /* NOTE: no parent disable_location_gathering() implementation */

    if (!(source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                    MM_MODEM_LOCATION_SOURCE_AGPS))) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    g_assert (!priv->pds_client);

    /* Disable A-GPS? */
    if (source == MM_MODEM_LOCATION_SOURCE_AGPS) {
        set_gps_operation_mode (self,
                                GPS_OPERATION_MODE_STANDALONE,
                                (GAsyncReadyCallback)set_gps_operation_mode_standalone_ready,
                                task);
        return;
    }

    /* If no more GPS sources enabled, stop GPS */
    tmp = priv->enabled_sources;
    tmp &= ~source;
    if (!(tmp & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW))) {
        stop_gps_engine (self,
                         (GAsyncReadyCallback)stop_gps_engine_ready,
                         task);
        return;
    }

    /* Otherwise, we have more GPS sources enabled, we shouldn't stop GPS, just
     * return */
    priv->enabled_sources &= ~source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Location: enable */

gboolean
mm_shared_qmi_enable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
start_gps_engine_ready (MMSharedQmi  *self,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    if (!start_gps_engine_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    priv = get_private (self);

    priv->enabled_sources |= source;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
set_gps_operation_mode_assisted_ready (MMSharedQmi  *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    GError  *error = NULL;
    Private *priv;

    if (!set_gps_operation_mode_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    priv = get_private (self);

    priv->enabled_sources |= MM_MODEM_LOCATION_SOURCE_AGPS;

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *_self,
                                        GAsyncResult         *res,
                                        GTask                *task)
{
    MMSharedQmi           *self = MM_SHARED_QMI (_self);
    Private               *priv;
    MMModemLocationSource  source;
    GError                *error = NULL;

    priv = get_private (self);

    if (!priv->iface_modem_location_parent->enable_location_gathering_finish (_self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));

    /* We only consider GPS related sources in this shared QMI implementation */
    if (!(source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_AGPS))) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Enabling A-GPS? */
    if (source == MM_MODEM_LOCATION_SOURCE_AGPS) {
        set_gps_operation_mode (self,
                                GPS_OPERATION_MODE_ASSISTED,
                                (GAsyncReadyCallback)set_gps_operation_mode_assisted_ready,
                                task);
        return;
    }

    /* Only start GPS engine if not done already */
    if (!(priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW))) {
        start_gps_engine (self,
                          (GAsyncReadyCallback)start_gps_engine_ready,
                          task);
        return;
    }

    /* GPS already started, we're done */
    priv->enabled_sources |= source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_qmi_enable_location_gathering (MMIfaceModemLocation  *self,
                                         MMModemLocationSource  source,
                                         GAsyncReadyCallback    callback,
                                         gpointer               user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering_finish);

    /* Chain up parent's gathering enable */
    priv->iface_modem_location_parent->enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        task);
}

/*****************************************************************************/
/* Location: load capabilities */

MMModemLocationSource
mm_shared_qmi_location_load_capabilities_finish (MMIfaceModemLocation  *self,
                                                 GAsyncResult          *res,
                                                 GError               **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCATION_SOURCE_NONE;
    }
    return (MMModemLocationSource)value;
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult         *res,
                                GTask                *task)
{
    MMModemLocationSource  sources;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_QMI (self));

    sources = priv->iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own checks */

    /* If we have support for the PDS client, GPS and A-GPS location is supported */
    if (mm_shared_qmi_peek_client (MM_SHARED_QMI (self), QMI_SERVICE_PDS, MM_PORT_QMI_FLAG_DEFAULT, NULL))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                    MM_MODEM_LOCATION_SOURCE_AGPS);

    /* So we're done, complete */
    g_task_return_int (task, sources);
    g_object_unref (task);
}

void
mm_shared_qmi_location_load_capabilities (MMIfaceModemLocation *self,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, NULL, callback, user_data);

    priv = get_private (MM_SHARED_QMI (self));
    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->load_capabilities);
    g_assert (priv->iface_modem_location_parent->load_capabilities_finish);

    priv->iface_modem_location_parent->load_capabilities (self,
                                                          (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                          task);
}

/*****************************************************************************/

QmiClient *
mm_shared_qmi_peek_client (MMSharedQmi    *self,
                           QmiService      service,
                           MMPortQmiFlag   flag,
                           GError        **error)
{
    g_assert (MM_SHARED_QMI_GET_INTERFACE (self)->peek_client);
    return MM_SHARED_QMI_GET_INTERFACE (self)->peek_client (self, service, flag, error);
}

gboolean
mm_shared_qmi_ensure_client (MMSharedQmi          *self,
                             QmiService            service,
                             QmiClient           **o_client,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data)
{
    GError    *error = NULL;
    QmiClient *client;

    client = mm_shared_qmi_peek_client (self, service, MM_PORT_QMI_FLAG_DEFAULT, &error);
    if (!client) {
        g_task_report_error (self, callback, user_data, mm_shared_qmi_ensure_client, error);
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

static void
shared_qmi_init (gpointer g_iface)
{
}

GType
mm_shared_qmi_get_type (void)
{
    static GType shared_qmi_type = 0;

    if (!G_UNLIKELY (shared_qmi_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedQmi),  /* class_size */
            shared_qmi_init,       /* base_init */
            NULL,                  /* base_finalize */
        };

        shared_qmi_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedQmi", &info, 0);
        g_type_interface_add_prerequisite (shared_qmi_type, MM_TYPE_IFACE_MODEM);
        g_type_interface_add_prerequisite (shared_qmi_type, MM_TYPE_IFACE_MODEM_LOCATION);
    }

    return shared_qmi_type;
}
