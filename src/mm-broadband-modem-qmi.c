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
#include "mm-iface-modem.h"
#include "mm-sim-qmi.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmi, mm_broadband_modem_qmi, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

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
        g_prefix_error (&error, "Couldn't get PIN status: ");
        g_simple_async_result_take_error (simple, error);
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

    /* Create QMI-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    /* Other actions */
    iface->factory_reset = modem_factory_reset;
    iface->factory_reset_finish = modem_factory_reset_finish;
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
