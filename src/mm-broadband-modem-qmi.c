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
 * Copyright (C) 2012 Google Inc.
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "mm-broadband-modem-qmi.h"

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-sim-qmi.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmi, mm_broadband_modem_qmi, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

/*****************************************************************************/

static gboolean
ensure_qmi_client (MMBroadbandModemQmi *self,
                   QmiService service,
                   QmiClient **o_client,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    QmiClient *client;

    client = mm_qmi_port_peek_client (mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self)), service);
    if (!client) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't peek client for service '%s'",
                                             qmi_service_get_string (service));
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

/*****************************************************************************/
/* Capabilities loading (Modem interface) */

static MMModemCapability
modem_load_current_capabilities_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    MMModemCapability caps;
    gchar *caps_str;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_CAPABILITY_NONE;

    caps = ((MMModemCapability) GPOINTER_TO_UINT (
                g_simple_async_result_get_op_res_gpointer (
                    G_SIMPLE_ASYNC_RESULT (res))));
    caps_str = mm_modem_capability_build_string_from_mask (caps);
    mm_dbg ("loaded current capabilities: %s", caps_str);
    g_free (caps_str);
    return caps;
}

static MMModemCapability
qmi_network_to_modem_capability (QmiDmsRadioInterface network)
{
    switch (network) {
    case QMI_DMS_RADIO_INTERFACE_CDMA20001X:
        return MM_MODEM_CAPABILITY_CDMA_EVDO;
    case QMI_DMS_RADIO_INTERFACE_EVDO:
        return MM_MODEM_CAPABILITY_CDMA_EVDO;
    case QMI_DMS_RADIO_INTERFACE_GSM:
        return MM_MODEM_CAPABILITY_GSM_UMTS;
    case QMI_DMS_RADIO_INTERFACE_UMTS:
        return MM_MODEM_CAPABILITY_GSM_UMTS;
    case QMI_DMS_RADIO_INTERFACE_LTE:
        return MM_MODEM_CAPABILITY_LTE;
    default:
        mm_warn ("Unhandled QMI radio interface received (%u)",
                 (guint)network);
        return MM_MODEM_CAPABILITY_NONE;
    }
}

static void
dms_get_capabilities_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetCapabilitiesOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Capabilities: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        guint i;
        guint mask = MM_MODEM_CAPABILITY_NONE;
        GArray *radio_interface_list;

        qmi_message_dms_get_capabilities_output_get_info (
            output,
            NULL, /* info_max_tx_channel_rate */
            NULL, /* info_max_rx_channel_rate */
            NULL, /* info_data_service_capability */
            NULL, /* info_sim_capability */
            &radio_interface_list,
            NULL);

        for (i = 0; i < radio_interface_list->len; i++) {
            mask |= qmi_network_to_modem_capability (g_array_index (radio_interface_list,
                                                                    QmiDmsRadioInterface,
                                                                    i));
        }

        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (mask),
                                                   NULL);
    }

    if (output)
        qmi_message_dms_get_capabilities_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_current_capabilities (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_current_capabilities);

    mm_dbg ("loading current capabilities...");
    qmi_client_dms_get_capabilities (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_capabilities_ready,
                                     result);
}

/*****************************************************************************/
/* Manufacturer loading (Modem interface) */

static gchar *
modem_load_manufacturer_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    gchar *manufacturer;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    manufacturer = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded manufacturer: %s", manufacturer);
    return manufacturer;
}

static void
dms_get_manufacturer_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetManufacturerOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_manufacturer_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_manufacturer_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Manufacturer: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_manufacturer_output_get_manufacturer (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_manufacturer_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_manufacturer (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_manufacturer);

    mm_dbg ("loading manufacturer...");
    qmi_client_dms_get_manufacturer (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_manufacturer_ready,
                                     result);
}

/*****************************************************************************/
/* Model loading (Modem interface) */

static gchar *
modem_load_model_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    gchar *model;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    model = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded model: %s", model);
    return model;
}

static void
dms_get_model_ready (QmiClientDms *client,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetModelOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_model_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_model_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Model: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_model_output_get_model (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_model_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_model (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_model);

    mm_dbg ("loading model...");
    qmi_client_dms_get_model (QMI_CLIENT_DMS (client),
                              NULL,
                              5,
                              NULL,
                              (GAsyncReadyCallback)dms_get_model_ready,
                              result);
}

/*****************************************************************************/
/* Revision loading (Modem interface) */

static gchar *
modem_load_revision_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    gchar *revision;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    revision = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded revision: %s", revision);
    return revision;
}

static void
dms_get_revision_ready (QmiClientDms *client,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetRevisionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_revision_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_revision_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Revision: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_revision_output_get_revision (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_revision_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_revision (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_revision);

    mm_dbg ("loading revision...");
    qmi_client_dms_get_revision (QMI_CLIENT_DMS (client),
                                 NULL,
                                 5,
                                 NULL,
                                 (GAsyncReadyCallback)dms_get_revision_ready,
                                 result);
}

/*****************************************************************************/
/* Equipment Identifier loading (Modem interface) */

static gchar *
modem_load_equipment_identifier_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    gchar *equipment_identifier;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    equipment_identifier = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded equipment identifier: %s", equipment_identifier);
    return equipment_identifier;
}

static void
dms_get_ids_ready (QmiClientDms *client,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetIdsOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_ids_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_ids_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get IDs: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str = NULL;

        /* In order:
         * If we have a IMEI, use it...
         * Otherwise, if we have a ESN, use it...
         * Otherwise, if we have a MEID, use it...
         * Otherwise, 'unknown'
         */

        if (qmi_message_dms_get_ids_output_get_imei (output, &str, NULL)) {
            if (str && (str[0] == '\0' || str[0] == '0'))
                str = NULL;
        }

        if (!str &&
            qmi_message_dms_get_ids_output_get_esn (output, &str, NULL)) {
            if (str && (str[0] == '\0' || str[0] == '0'))
                str = NULL;
        }

        if (!str &&
            qmi_message_dms_get_ids_output_get_meid (output, &str, NULL)) {
            if (str && (str[0] == '\0' || str[0] == '0'))
                str = NULL;
        }

        if (!str)
            str = "unknown";

        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_ids_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_equipment_identifier (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_equipment_identifier);

    mm_dbg ("loading equipment identifier...");
    qmi_client_dms_get_ids (QMI_CLIENT_DMS (client),
                            NULL,
                            5,
                            NULL,
                            (GAsyncReadyCallback)dms_get_ids_ready,
                            result);
}

/*****************************************************************************/
/* Device identifier loading (Modem interface) */

static gchar *
modem_load_device_identifier_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    gchar *device_identifier;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    device_identifier = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded device identifier: %s", device_identifier);
    return device_identifier;
}

static void
modem_load_device_identifier (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *device_identifier;

    mm_dbg ("loading device identifier...");
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_device_identifier);

    /* Just use dummy ATI/ATI1 replies, all the other internal info should be
     * enough for uniqueness */
    device_identifier = mm_broadband_modem_create_device_identifier (MM_BROADBAND_MODEM (self), "", "");
    g_simple_async_result_set_op_res_gpointer (result,
                                               device_identifier,
                                               (GDestroyNotify)g_free);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/
/* Own Numbers loading (Modem interface) */

static GStrv
modem_load_own_numbers_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    gchar **own_numbers;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    own_numbers = g_new0 (gchar *, 2);
    own_numbers[0] = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded own numbers: %s", own_numbers[0]);
    return own_numbers;
}

static void
dms_get_msisdn_ready (QmiClientDms *client,
                      GAsyncResult *res,
                      GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetMsisdnOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_msisdn_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_msisdn_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get MSISDN: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str = NULL;

        qmi_message_dms_get_msisdn_output_get_msisdn (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_msisdn_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_own_numbers (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_own_numbers);

    mm_dbg ("loading own numbers...");
    qmi_client_dms_get_msisdn (QMI_CLIENT_DMS (client),
                               NULL,
                               5,
                               NULL,
                               (GAsyncReadyCallback)dms_get_msisdn_ready,
                               result);
}

/*****************************************************************************/
/* Check if unlock required (Modem interface) */

static MMModemLock
modem_load_unlock_required_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCK_UNKNOWN;

    return (MMModemLock) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                               G_SIMPLE_ASYNC_RESULT (res)));
}

static MMModemLock
uim_pin_status_to_modem_lock (QmiDmsUimPinStatus status,
                              gboolean pin1) /* TRUE for PIN1, FALSE for PIN2 */
{
    switch (status) {
    case QMI_DMS_UIM_PIN_STATUS_NOT_INITIALIZED:
        return MM_MODEM_LOCK_UNKNOWN;
    case QMI_DMS_UIM_PIN_STATUS_ENABLED_NOT_VERIFIED:
        return pin1 ? MM_MODEM_LOCK_SIM_PIN : MM_MODEM_LOCK_SIM_PIN2;
    case QMI_DMS_UIM_PIN_STATUS_ENABLED_VERIFIED:
        return MM_MODEM_LOCK_NONE;
    case QMI_DMS_UIM_PIN_STATUS_DISABLED:
        return MM_MODEM_LOCK_NONE;
    case QMI_DMS_UIM_PIN_STATUS_BLOCKED:
        return pin1 ? MM_MODEM_LOCK_SIM_PUK : MM_MODEM_LOCK_SIM_PUK2;
    case QMI_DMS_UIM_PIN_STATUS_PERMANENTLY_BLOCKED:
        return MM_MODEM_LOCK_UNKNOWN;
    case QMI_DMS_UIM_PIN_STATUS_UNBLOCKED:
        /* This state is possibly given when after an Unblock() operation has been performed.
         * We'll assume the PIN is verified after this. */
        return MM_MODEM_LOCK_NONE;
    case QMI_DMS_UIM_PIN_STATUS_CHANGED:
        /* This state is possibly given when after an ChangePin() operation has been performed.
         * We'll assume the PIN is verified after this. */
        return MM_MODEM_LOCK_NONE;
    default:
        return MM_MODEM_LOCK_UNKNOWN;
    }
}

static void
dms_uim_get_pin_status_ready (QmiClientDms *client,
                              GAsyncResult *res,
                              GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimGetPinStatusOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_uim_get_pin_status_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_get_pin_status_output_get_result (output, &error)) {
        /* When no SIM inserted, an internal error when checking PIN status
         * needs to be fatal so that we mark the modem unusable. */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_INTERNAL)) {
            g_simple_async_result_set_error (simple,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE,
                                             "Couldn't get PIN status: %s",
                                             error->message);
            g_error_free (error);
        } else {
            g_prefix_error (&error, "Couldn't get PIN status: ");
            g_simple_async_result_take_error (simple, error);
        }
    } else {
        MMModemLock lock = MM_MODEM_LOCK_UNKNOWN;
        QmiDmsUimPinStatus current_status;

        if (qmi_message_dms_uim_get_pin_status_output_get_pin1_status (
                output,
                &current_status,
                NULL, /* verify_retries_left */
                NULL, /* unblock_retries_left */
                NULL))
            lock = uim_pin_status_to_modem_lock (current_status, TRUE);

        if (lock == MM_MODEM_LOCK_NONE &&
            qmi_message_dms_uim_get_pin_status_output_get_pin2_status (
                output,
                &current_status,
                NULL, /* verify_retries_left */
                NULL, /* unblock_retries_left */
                NULL))
            lock = uim_pin_status_to_modem_lock (current_status, FALSE);

        g_simple_async_result_set_op_res_gpointer (simple, GUINT_TO_POINTER (lock), NULL);
    }

    if (output)
        qmi_message_dms_uim_get_pin_status_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_unlock_required (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_unlock_required);

    mm_dbg ("loading unlock required...");
    qmi_client_dms_uim_get_pin_status (QMI_CLIENT_DMS (client),
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)dms_uim_get_pin_status_ready,
                                       result);
}

/*****************************************************************************/
/* Check if unlock retries (Modem interface) */

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return MM_UNLOCK_RETRIES (g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res))));
}

static void
retry_count_dms_uim_get_pin_status_ready (QmiClientDms *client,
                                          GAsyncResult *res,
                                          GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimGetPinStatusOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_uim_get_pin_status_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_get_pin_status_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get unlock retries: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        MMUnlockRetries *retries;
        guint8 verify_retries_left;
        guint8 unblock_retries_left;

        retries = mm_unlock_retries_new ();

        if (qmi_message_dms_uim_get_pin_status_output_get_pin1_status (
                output,
                NULL, /* current_status */
                &verify_retries_left,
                &unblock_retries_left,
                NULL)) {
            mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN, verify_retries_left);
            mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK, unblock_retries_left);
        }

        if (qmi_message_dms_uim_get_pin_status_output_get_pin2_status (
                output,
                NULL, /* current_status */
                &verify_retries_left,
                &unblock_retries_left,
                NULL)) {
            mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN2, verify_retries_left);
            mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK2, unblock_retries_left);
        }

        g_simple_async_result_set_op_res_gpointer (simple, retries, g_object_unref);
    }

    if (output)
        qmi_message_dms_uim_get_pin_status_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_unlock_retries (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_unlock_retries);

    mm_dbg ("loading unlock retries...");
    qmi_client_dms_uim_get_pin_status (QMI_CLIENT_DMS (client),
                                       NULL,
                                       5,
                                       NULL,
                                       (GAsyncReadyCallback)retry_count_dms_uim_get_pin_status_ready,
                                       result);
}

/*****************************************************************************/
/* Load supported bands (Modem interface) */

static GArray *
modem_load_supported_bands_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return (GArray *) g_array_ref (g_simple_async_result_get_op_res_gpointer (
                                       G_SIMPLE_ASYNC_RESULT (res)));
}

typedef struct {
    QmiDmsBandCapability qmi_band;
    MMModemBand mm_band;
} BandsMap;

static const BandsMap bands_map [] = {
    /* CDMA bands */
    {
        (QMI_DMS_BAND_CAPABILITY_BAND_CLASS_0_A_SYSTEM | QMI_DMS_BAND_CAPABILITY_BAND_CLASS_0_B_SYSTEM),
        MM_MODEM_BAND_CDMA_BC0_CELLULAR_800
    },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_1_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC1_PCS_1900       },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_2,            MM_MODEM_BAND_CDMA_BC2_TACS           },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_3_A_SYSTEM,   MM_MODEM_BAND_CDMA_BC3_JTACS          },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_4_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC4_KOREAN_PCS     },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_5_ALL_BLOCKS, MM_MODEM_BAND_CDMA_BC5_NMT450         },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_6,            MM_MODEM_BAND_CDMA_BC6_IMT2000        },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_7,            MM_MODEM_BAND_CDMA_BC7_CELLULAR_700   },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_8,            MM_MODEM_BAND_CDMA_BC8_1800           },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_9,            MM_MODEM_BAND_CDMA_BC9_900            },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_10,           MM_MODEM_BAND_CDMA_BC10_SECONDARY_800 },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_11,           MM_MODEM_BAND_CDMA_BC11_PAMR_400      },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_12,           MM_MODEM_BAND_CDMA_BC12_PAMR_800      },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_14,           MM_MODEM_BAND_CDMA_BC14_PCS2_1900     },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_15,           MM_MODEM_BAND_CDMA_BC15_AWS           },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_16,           MM_MODEM_BAND_CDMA_BC16_US_2500       },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_17,           MM_MODEM_BAND_CDMA_BC17_US_FLO_2500   },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_18,           MM_MODEM_BAND_CDMA_BC18_US_PS_700     },
    { QMI_DMS_BAND_CAPABILITY_BAND_CLASS_19,           MM_MODEM_BAND_CDMA_BC19_US_LOWER_700  },

    /* GSM bands */
    { QMI_DMS_BAND_CAPABILITY_GSM_DCS,  MM_MODEM_BAND_DCS  },
    { QMI_DMS_BAND_CAPABILITY_GSM_EGSM, MM_MODEM_BAND_EGSM },
    { QMI_DMS_BAND_CAPABILITY_GSM_PCS,  MM_MODEM_BAND_PCS  },
    { QMI_DMS_BAND_CAPABILITY_GSM_850,  MM_MODEM_BAND_G850 },

    /* UMTS/WCDMA bands */
    { QMI_DMS_BAND_CAPABILITY_WCDMA_2100,       MM_MODEM_BAND_U2100 },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_DCS_1800,   MM_MODEM_BAND_U1800 },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_1700_US,    MM_MODEM_BAND_U17IV },
    { QMI_DMS_BAND_CAPABILITY_QWCDMA_800,       MM_MODEM_BAND_U800  },
    {
        (QMI_DMS_BAND_CAPABILITY_WCDMA_850_US | QMI_DMS_BAND_CAPABILITY_WCDMA_850_JAPAN),
        MM_MODEM_BAND_U850
    },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_900,        MM_MODEM_BAND_U900  },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_1700_JAPAN, MM_MODEM_BAND_U17IX },
    { QMI_DMS_BAND_CAPABILITY_WCDMA_2600,       MM_MODEM_BAND_U2600 }

    /* NOTE. The following bands were unmatched:
     *
     * - QMI_DMS_BAND_CAPABILITY_GSM_PGSM
     * - QMI_DMS_BAND_CAPABILITY_GSM_450
     * - QMI_DMS_BAND_CAPABILITY_GSM_480
     * - QMI_DMS_BAND_CAPABILITY_GSM_750
     * - QMI_DMS_BAND_CAPABILITY_GSM_RAILWAILS
     * - QMI_DMS_BAND_CAPABILITY_WCDMA_1500
     * - MM_MODEM_BAND_CDMA_BC13_IMT2000_2500
     * - MM_MODEM_BAND_U1900
     */
};

static void
add_qmi_bands (GArray *mm_bands,
               QmiDmsBandCapability qmi_bands)
{
    static QmiDmsBandCapability qmi_bands_expected = 0;
    QmiDmsBandCapability not_expected;
    guint i;

    g_assert (mm_bands != NULL);

    /* Build mask of expected bands only once */
    if (G_UNLIKELY (qmi_bands_expected == 0)) {
        for (i = 0; i < G_N_ELEMENTS (bands_map); i++) {
            qmi_bands_expected |= bands_map[i].qmi_band;
        }
    }

    /* Log about the bands that cannot be represented in ModemManager */
    not_expected = ((qmi_bands_expected ^ qmi_bands) & qmi_bands);
    if (not_expected) {
        gchar *aux;

        aux = qmi_dms_band_capability_build_string_from_mask (not_expected);
        mm_dbg ("Cannot add the following bands: '%s'", aux);
        g_free (aux);
    }

    /* And add the expected ones */
    for (i = 0; i < G_N_ELEMENTS (bands_map); i++) {
        if (qmi_bands & bands_map[i].qmi_band)
            g_array_append_val (mm_bands, bands_map[i].mm_band);
    }
}

typedef struct {
    QmiDmsLteBandCapability qmi_band;
    MMModemBand mm_band;
} LteBandsMap;

static const LteBandsMap lte_bands_map [] = {
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_1,  MM_MODEM_BAND_EUTRAN_I       },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_2,  MM_MODEM_BAND_EUTRAN_II      },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_3,  MM_MODEM_BAND_EUTRAN_III     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_4,  MM_MODEM_BAND_EUTRAN_IV      },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_5,  MM_MODEM_BAND_EUTRAN_V       },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_6,  MM_MODEM_BAND_EUTRAN_VI      },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_7,  MM_MODEM_BAND_EUTRAN_VII     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_8,  MM_MODEM_BAND_EUTRAN_VIII    },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_9,  MM_MODEM_BAND_EUTRAN_IX      },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_10, MM_MODEM_BAND_EUTRAN_X       },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_11, MM_MODEM_BAND_EUTRAN_XI      },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_12, MM_MODEM_BAND_EUTRAN_XII     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_13, MM_MODEM_BAND_EUTRAN_XIII    },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_14, MM_MODEM_BAND_EUTRAN_XIV     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_17, MM_MODEM_BAND_EUTRAN_XVII    },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_18, MM_MODEM_BAND_EUTRAN_XVIII   },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_19, MM_MODEM_BAND_EUTRAN_XIX     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_20, MM_MODEM_BAND_EUTRAN_XX      },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_21, MM_MODEM_BAND_EUTRAN_XXI     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_24, MM_MODEM_BAND_EUTRAN_XXIV    },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_25, MM_MODEM_BAND_EUTRAN_XXV     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_33, MM_MODEM_BAND_EUTRAN_XXXIII  },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_34, MM_MODEM_BAND_EUTRAN_XXXIV   },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_35, MM_MODEM_BAND_EUTRAN_XXXV    },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_36, MM_MODEM_BAND_EUTRAN_XXXVI   },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_37, MM_MODEM_BAND_EUTRAN_XXXVII  },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_38, MM_MODEM_BAND_EUTRAN_XXXVIII },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_39, MM_MODEM_BAND_EUTRAN_XXXIX   },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_40, MM_MODEM_BAND_EUTRAN_XL      },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_41, MM_MODEM_BAND_EUTRAN_XLI     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_42, MM_MODEM_BAND_EUTRAN_XLI     },
    { QMI_DMS_LTE_BAND_CAPABILITY_BAND_43, MM_MODEM_BAND_EUTRAN_XLIII   }

    /* NOTE. The following bands were unmatched:
     *
     * - MM_MODEM_BAND_EUTRAN_XXII
     * - MM_MODEM_BAND_EUTRAN_XXIII
     * - MM_MODEM_BAND_EUTRAN_XXVI
     */
};

static void
add_qmi_lte_bands (GArray *mm_bands,
                   QmiDmsLteBandCapability qmi_bands)
{
    /* All QMI LTE bands have a counterpart in ModemManager, no need to check
     * for unexpected ones */
    guint i;

    g_assert (mm_bands != NULL);

    for (i = 0; i < G_N_ELEMENTS (lte_bands_map); i++) {
        if (qmi_bands & lte_bands_map[i].qmi_band)
            g_array_append_val (mm_bands, lte_bands_map[i].mm_band);
    }
}

static void
dms_get_band_capabilities_ready (QmiClientDms *client,
                                 GAsyncResult *res,
                                 GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetBandCapabilitiesOutput *output;
    GError *error = NULL;

    output = qmi_client_dms_get_band_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_band_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get band capabilities: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        GArray *mm_bands;
        QmiDmsBandCapability qmi_bands;
        QmiDmsLteBandCapability qmi_lte_bands;

        mm_bands = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

        qmi_message_dms_get_band_capabilities_output_get_band_capability (
            output,
            &qmi_bands,
            NULL);

        add_qmi_bands (mm_bands, qmi_bands);

        if (qmi_message_dms_get_band_capabilities_output_get_lte_band_capability (
                output,
                &qmi_lte_bands,
                NULL)) {
            add_qmi_lte_bands (mm_bands, qmi_lte_bands);
        }

        if (mm_bands->len == 0) {
            g_array_unref (mm_bands);
            g_simple_async_result_set_error (simple,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't parse the list of supported bands");
        } else {
            g_simple_async_result_set_op_res_gpointer (simple,
                                                       mm_bands,
                                                       (GDestroyNotify)g_array_unref);
        }
    }

    if (output)
        qmi_message_dms_get_band_capabilities_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_supported_bands (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_supported_bands);

    mm_dbg ("loading band capabilities...");
    qmi_client_dms_get_band_capabilities (QMI_CLIENT_DMS (client),
                                          NULL,
                                          5,
                                          NULL,
                                          (GAsyncReadyCallback)dms_get_band_capabilities_ready,
                                          result);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_qmi_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New QMI SIM */
    mm_sim_qmi_new (MM_BASE_MODEM (self),
                    NULL, /* cancellable */
                    callback,
                    user_data);
}

/*****************************************************************************/
/* Factory reset (Modem interface) */

static gboolean
modem_factory_reset_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
dms_restore_factory_defaults_ready (QmiClientDms *client,
                                    GAsyncResult *res,
                                    GSimpleAsyncResult *simple)
{
    QmiMessageDmsRestoreFactoryDefaultsOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_restore_factory_defaults_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_restore_factory_defaults_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't restore factory defaults: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (output)
        qmi_message_dms_restore_factory_defaults_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_factory_reset (MMIfaceModem *self,
                     const gchar *code,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    QmiMessageDmsRestoreFactoryDefaultsInput *input;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;
    GError *error = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_factory_reset);

    input = qmi_message_dms_restore_factory_defaults_input_new ();
    if (!qmi_message_dms_restore_factory_defaults_input_set_service_programming_code (
            input,
            code,
            &error)) {
        qmi_message_dms_restore_factory_defaults_input_unref (input);
        g_simple_async_result_take_error (result, error);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_dbg ("performing a factory reset...");
    qmi_client_dms_restore_factory_defaults (QMI_CLIENT_DMS (client),
                                             input,
                                             10,
                                             NULL,
                                             (GAsyncReadyCallback)dms_restore_factory_defaults_ready,
                                             result);
}

/*****************************************************************************/
/* Facility locks status loading (3GPP interface) */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    QmiClient *client;
    guint current;
    MMModem3gppFacility facilities;
    MMModem3gppFacility locks;
} LoadEnabledFacilityLocksContext;

static void get_next_facility_lock_status (LoadEnabledFacilityLocksContext *ctx);

static void
load_enabled_facility_locks_context_complete_and_free (LoadEnabledFacilityLocksContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static MMModem3gppFacility
modem_3gpp_load_enabled_facility_locks_finish (MMIfaceModem3gpp *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_3GPP_FACILITY_NONE;

    return ((MMModem3gppFacility) GPOINTER_TO_UINT (
                g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res))));
}

static QmiDmsUimFacility
get_qmi_facility_from_mm_facility (MMModem3gppFacility mm)
{
    switch (mm) {
    case MM_MODEM_3GPP_FACILITY_PH_SIM:
        /* Not really sure about this one; it may be PH_FSIM? */
        return QMI_DMS_UIM_FACILITY_PF;

    case MM_MODEM_3GPP_FACILITY_NET_PERS:
        return QMI_DMS_UIM_FACILITY_PN;

    case MM_MODEM_3GPP_FACILITY_NET_SUB_PERS:
        return QMI_DMS_UIM_FACILITY_PU;

    case MM_MODEM_3GPP_FACILITY_PROVIDER_PERS:
        return QMI_DMS_UIM_FACILITY_PP;

    case MM_MODEM_3GPP_FACILITY_CORP_PERS:
        return QMI_DMS_UIM_FACILITY_PC;

    default:
        /* Never try to ask for a facility we cannot translate */
        g_assert_not_reached ();
    }
}

static void
dms_uim_get_ck_status_ready (QmiClientDms *client,
                             GAsyncResult *res,
                             LoadEnabledFacilityLocksContext *ctx)
{
    gchar *facility_str;
    QmiMessageDmsUimGetCkStatusOutput *output;

    facility_str = mm_modem_3gpp_facility_build_string_from_mask (1 << ctx->current);
    output = qmi_client_dms_uim_get_ck_status_finish (client, res, NULL);
    if (!output ||
        !qmi_message_dms_uim_get_ck_status_output_get_result (output, NULL)) {
        /* On errors, we'll just assume disabled */
        mm_dbg ("Couldn't query facility '%s' status, assuming disabled", facility_str);
        ctx->locks &= ~(1 << ctx->current);
    } else {
        QmiDmsUimFacilityState state;
        guint8 verify_retries_left;
        guint8 unblock_retries_left;

        qmi_message_dms_uim_get_ck_status_output_get_ck_status (
            output,
            &state,
            &verify_retries_left,
            &unblock_retries_left,
            NULL);

        mm_dbg ("Facility '%s' is: '%s'",
                facility_str,
                qmi_dms_uim_facility_state_get_string (state));

        if (state == QMI_DMS_UIM_FACILITY_STATE_ACTIVATED ||
            state == QMI_DMS_UIM_FACILITY_STATE_BLOCKED) {
            ctx->locks |= (1 << ctx->current);
        }
    }

    if (output)
        qmi_message_dms_uim_get_ck_status_output_unref (output);
    g_free (facility_str);

    /* And go on with the next one */
    ctx->current++;
    get_next_facility_lock_status (ctx);
}

static void
get_next_facility_lock_status (LoadEnabledFacilityLocksContext *ctx)
{
    guint i;

    for (i = ctx->current; i < sizeof (MMModem3gppFacility) * 8; i++) {
        guint32 facility = 1 << i;

        /* Found the next one to query! */
        if (ctx->facilities & facility) {
            QmiMessageDmsUimGetCkStatusInput *input;

            /* Keep the current one */
            ctx->current = i;

            /* Query current */
            input = qmi_message_dms_uim_get_ck_status_input_new ();
            qmi_message_dms_uim_get_ck_status_input_set_facility (
                input,
                get_qmi_facility_from_mm_facility (facility),
                NULL);
            qmi_client_dms_uim_get_ck_status (QMI_CLIENT_DMS (ctx->client),
                                              input,
                                              5,
                                              NULL,
                                              (GAsyncReadyCallback)dms_uim_get_ck_status_ready,
                                              ctx);
            qmi_message_dms_uim_get_ck_status_input_unref (input);
            return;
        }
    }

    /* No more facilities to query, all done */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (ctx->locks),
                                               NULL);
    load_enabled_facility_locks_context_complete_and_free (ctx);
}

static void
modem_3gpp_load_enabled_facility_locks (MMIfaceModem3gpp *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    LoadEnabledFacilityLocksContext *ctx;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    ctx = g_new (LoadEnabledFacilityLocksContext, 1);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             modem_3gpp_load_enabled_facility_locks);
    /* Set initial list of facilities to query */
    ctx->facilities = (MM_MODEM_3GPP_FACILITY_PH_SIM |
                       MM_MODEM_3GPP_FACILITY_NET_PERS |
                       MM_MODEM_3GPP_FACILITY_NET_SUB_PERS |
                       MM_MODEM_3GPP_FACILITY_PROVIDER_PERS |
                       MM_MODEM_3GPP_FACILITY_CORP_PERS);
    ctx->locks = MM_MODEM_3GPP_FACILITY_NONE;
    ctx->current = 0;

    get_next_facility_lock_status (ctx);
}

/*****************************************************************************/
/* Scan networks (3GPP interface) */

static GList *
modem_3gpp_scan_networks_finish (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    /* We return the GList as it is */
    return (GList *) g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static MMModem3gppNetworkAvailability
network_availability_from_qmi_nas_network_status (QmiNasNetworkStatus qmi)
{
    if (qmi & QMI_NAS_NETWORK_STATUS_CURRENT_SERVING)
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT;

    if (qmi & QMI_NAS_NETWORK_STATUS_AVAILABLE) {
        if (qmi & QMI_NAS_NETWORK_STATUS_FORBIDDEN)
            return MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN;
        return MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE;
    }

    return MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN;
}

static MM3gppNetworkInfo *
get_3gpp_network_info (QmiMessageNasNetworkScanOutputNetworkInformationElement *element)
{
    GString *aux;
    MM3gppNetworkInfo *info;

    info = g_new (MM3gppNetworkInfo, 1);
    info->status = network_availability_from_qmi_nas_network_status (element->network_status);

    aux = g_string_new ("");
    if (element->mcc >= 100)
        g_string_append_printf (aux, "%.3"G_GUINT16_FORMAT, element->mcc);
    else
        g_string_append_printf (aux, "%.2"G_GUINT16_FORMAT, element->mcc);
    if (element->mnc >= 100)
        g_string_append_printf (aux, "%.3"G_GUINT16_FORMAT, element->mnc);
    else
        g_string_append_printf (aux, "%.2"G_GUINT16_FORMAT, element->mnc);

    info->operator_code = g_string_free (aux, FALSE);
    info->operator_short = NULL;
    info->operator_long = g_strdup (element->description);
    info->access_tech = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    return info;
}

static MMModemAccessTechnology
access_technology_from_qmi_rat (QmiNasRadioInterface interface)
{
    switch (interface) {
    case QMI_NAS_RADIO_INTERFACE_CDMA_1X:
        return MM_MODEM_ACCESS_TECHNOLOGY_1XRTT;
    case QMI_NAS_RADIO_INTERFACE_CDMA_1XEVDO:
        return MM_MODEM_ACCESS_TECHNOLOGY_EVDO0;
    case QMI_NAS_RADIO_INTERFACE_GSM:
        return MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    case QMI_NAS_RADIO_INTERFACE_UMTS:
        return MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    case QMI_NAS_RADIO_INTERFACE_LTE:
        return MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    case QMI_NAS_RADIO_INTERFACE_TD_SCDMA:
    case QMI_NAS_RADIO_INTERFACE_AMPS:
    case QMI_NAS_RADIO_INTERFACE_NONE:
    default:
        return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    }
}

static MMModemAccessTechnology
get_3gpp_access_technology (GArray *array,
                            gboolean *array_used_flags,
                            guint16 mcc,
                            guint16 mnc)
{
    guint i;

    for (i = 0; i < array->len; i++) {
        QmiMessageNasNetworkScanOutputRadioAccessTechnologyElement *element;

        if (array_used_flags[i])
            continue;

        element = &g_array_index (array, QmiMessageNasNetworkScanOutputRadioAccessTechnologyElement, i);
        if (element->mcc == mcc &&
            element->mnc == mnc) {
            array_used_flags[i] = TRUE;
            return access_technology_from_qmi_rat (element->rat);
        }
    }

    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

static void
nas_network_scan_ready (QmiClientNas *client,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    QmiMessageNasNetworkScanOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_nas_network_scan_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_nas_network_scan_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't scan networks: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        GList *scan_result = NULL;
        GArray *info_array = NULL;

        if (qmi_message_nas_network_scan_output_get_network_information (output, &info_array, NULL)) {
            GArray *rat_array = NULL;
            gboolean *rat_array_used_flags = NULL;
            guint i;

            /* Get optional RAT array */
            qmi_message_nas_network_scan_output_get_radio_access_technology (output, &rat_array, NULL);
            if (rat_array)
                rat_array_used_flags = g_new0 (gboolean, rat_array->len);

            for (i = 0; i < info_array->len; i++) {
                QmiMessageNasNetworkScanOutputNetworkInformationElement *info_element;
                MM3gppNetworkInfo *info;

                info_element = &g_array_index (info_array, QmiMessageNasNetworkScanOutputNetworkInformationElement, i);

                info = get_3gpp_network_info (info_element);
                if (rat_array)
                    info->access_tech = get_3gpp_access_technology (rat_array,
                                                                    rat_array_used_flags,
                                                                    info_element->mcc,
                                                                    info_element->mnc);

                scan_result = g_list_append (scan_result, info);
            }

            g_free (rat_array_used_flags);
        }

        /* We *require* a callback in the async method, as we're not setting a
         * GDestroyNotify callback */
        g_simple_async_result_set_op_res_gpointer (simple, scan_result, NULL);
    }

    if (output)
        qmi_message_nas_network_scan_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_3gpp_scan_networks (MMIfaceModem3gpp *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    /* We will pass the GList in the GSimpleAsyncResult, so we must
     * ensure that there is a callback so that we get it properly
     * passed to the caller and deallocated afterwards */
    g_assert (callback != NULL);

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_3gpp_scan_networks);

    mm_dbg ("Scanning networks...");
    qmi_client_nas_network_scan (QMI_CLIENT_NAS (client),
                                 NULL,
                                 100,
                                 NULL,
                                 (GAsyncReadyCallback)nas_network_scan_ready,
                                 result);
}

/*****************************************************************************/
/* First initialization step */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMQmiPort *qmi;
    QmiService services[32];
    guint service_index;
} InitializationStartedContext;

static void
initialization_started_context_complete_and_free (InitializationStartedContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->qmi)
        g_object_unref (ctx->qmi);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    /* Just parent's pointer passed here */
    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void
parent_initialization_started_ready (MMBroadbandModem *self,
                                     GAsyncResult *res,
                                     InitializationStartedContext *ctx)
{
    gpointer parent_ctx;
    GError *error = NULL;

    parent_ctx = MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->initialization_started_finish (
        self,
        res,
        &error);
    if (error) {
        g_prefix_error (&error, "Couldn't start parent initialization: ");
        g_simple_async_result_take_error (ctx->result, error);
    } else
        g_simple_async_result_set_op_res_gpointer (ctx->result, parent_ctx, NULL);

    initialization_started_context_complete_and_free (ctx);
}

static void
parent_initialization_started (InitializationStartedContext *ctx)
{
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->initialization_started (
        ctx->self,
        (GAsyncReadyCallback)parent_initialization_started_ready,
        ctx);
}

static void allocate_next_client (InitializationStartedContext *ctx);

static void
qmi_port_allocate_client_ready (MMQmiPort *qmi,
                                GAsyncResult *res,
                                InitializationStartedContext *ctx)
{
    GError *error = NULL;

    if (!mm_qmi_port_allocate_client_finish (qmi, res, &error)) {
        mm_dbg ("Couldn't allocate client for service '%s': %s",
                qmi_service_get_string (ctx->services[ctx->service_index]),
                error->message);
        g_error_free (error);
    }

    ctx->service_index++;
    allocate_next_client (ctx);
}

static void
allocate_next_client (InitializationStartedContext *ctx)
{
    if (ctx->services[ctx->service_index] == QMI_SERVICE_UNKNOWN) {
        /* Done we are, launch parent's callback */
        parent_initialization_started (ctx);
        return;
    }

    /* Otherwise, allocate next client */
    mm_qmi_port_allocate_client (ctx->qmi,
                                 ctx->services[ctx->service_index],
                                 NULL,
                                 (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                 ctx);
}

static void
qmi_port_open_ready (MMQmiPort *qmi,
                     GAsyncResult *res,
                     InitializationStartedContext *ctx)
{
    GError *error = NULL;

    if (!mm_qmi_port_open_finish (qmi, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        initialization_started_context_complete_and_free (ctx);
        return;
    }

    allocate_next_client (ctx);
}

static void
initialization_started (MMBroadbandModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    InitializationStartedContext *ctx;

    ctx = g_new0 (InitializationStartedContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_started);
    ctx->qmi = mm_base_modem_get_port_qmi (MM_BASE_MODEM (self));
    g_assert (ctx->qmi);

    if (mm_qmi_port_is_open (ctx->qmi)) {
        /* Nothing to be done, just launch parent's callback */
        parent_initialization_started (ctx);
        return;
    }

    /* Setup services to open */
    ctx->services[0] = QMI_SERVICE_DMS;
    ctx->services[1] = QMI_SERVICE_WDS;
    ctx->services[2] = QMI_SERVICE_NAS;
    ctx->services[3] = QMI_SERVICE_UNKNOWN;

    /* Now open our QMI port */
    mm_qmi_port_open (ctx->qmi,
                      NULL,
                      (GAsyncReadyCallback)qmi_port_open_ready,
                      ctx);
}

/*****************************************************************************/

MMBroadbandModemQmi *
mm_broadband_modem_qmi_new (const gchar *device,
                            const gchar **drivers,
                            const gchar *plugin,
                            guint16 vendor_id,
                            guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_qmi_init (MMBroadbandModemQmi *self)
{
}

static void
finalize (GObject *object)
{
    MMQmiPort *qmi;
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (object);

    qmi = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    /* If we did open the QMI port during initialization, close it now */
    if (qmi &&
        mm_qmi_port_is_open (qmi)) {
        mm_qmi_port_close (qmi);
    }

    G_OBJECT_CLASS (mm_broadband_modem_qmi_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Initialization steps */
    iface->load_current_capabilities = modem_load_current_capabilities;
    iface->load_current_capabilities_finish = modem_load_current_capabilities_finish;
    iface->load_manufacturer = modem_load_manufacturer;
    iface->load_manufacturer_finish = modem_load_manufacturer_finish;
    iface->load_model = modem_load_model;
    iface->load_model_finish = modem_load_model_finish;
    iface->load_revision = modem_load_revision;
    iface->load_revision_finish = modem_load_revision_finish;
    iface->load_equipment_identifier = modem_load_equipment_identifier;
    iface->load_equipment_identifier_finish = modem_load_equipment_identifier_finish;
    iface->load_device_identifier = modem_load_device_identifier;
    iface->load_device_identifier_finish = modem_load_device_identifier_finish;
    iface->load_own_numbers = modem_load_own_numbers;
    iface->load_own_numbers_finish = modem_load_own_numbers_finish;
    iface->load_unlock_required = modem_load_unlock_required;
    iface->load_unlock_required_finish = modem_load_unlock_required_finish;
    iface->load_unlock_retries = modem_load_unlock_retries;
    iface->load_unlock_retries_finish = modem_load_unlock_retries_finish;
    iface->load_supported_bands = modem_load_supported_bands;
    iface->load_supported_bands_finish = modem_load_supported_bands_finish;

    /* Create QMI-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    /* Other actions */
    iface->factory_reset = modem_factory_reset;
    iface->factory_reset_finish = modem_factory_reset_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Initialization steps */
    iface->load_enabled_facility_locks = modem_3gpp_load_enabled_facility_locks;
    iface->load_enabled_facility_locks_finish = modem_3gpp_load_enabled_facility_locks_finish;

    /* Other actions */
    iface->scan_networks = modem_3gpp_scan_networks;
    iface->scan_networks_finish = modem_3gpp_scan_networks_finish;
}

static void
mm_broadband_modem_qmi_class_init (MMBroadbandModemQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    object_class->finalize = finalize;

    broadband_modem_class->initialization_started = initialization_started;
    broadband_modem_class->initialization_started_finish = initialization_started_finish;
}
