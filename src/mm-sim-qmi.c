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

#include "mm-log.h"
#include "mm-sim-qmi.h"

G_DEFINE_TYPE (MMSimQmi, mm_sim_qmi, MM_TYPE_BASE_SIM)

/*****************************************************************************/

static gboolean
ensure_qmi_client (MMSimQmi *self,
                   QmiService service,
                   QmiClient **o_client,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    MMBaseModem *modem = NULL;
    QmiClient *client;
    MMPortQmi *port;

    g_object_get (self,
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    port = mm_base_modem_peek_port_qmi (modem);
    g_object_unref (modem);

    if (!port) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't peek QMI port");
        return FALSE;
    }

    client = mm_port_qmi_peek_client (port,
                                      service,
                                      MM_PORT_QMI_FLAG_DEFAULT);
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
/* Load SIM ID (ICCID) */

static gchar *
load_sim_identifier_finish (MMBaseSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    gchar *sim_identifier;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    sim_identifier = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded SIM identifier: %s", sim_identifier);
    return sim_identifier;
}

static void
dms_uim_get_iccid_ready (QmiClientDms *client,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimGetIccidOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_get_iccid_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_get_iccid_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get UIM ICCID: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str = NULL;

        qmi_message_dms_uim_get_iccid_output_get_iccid (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_uim_get_iccid_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_sim_identifier (MMBaseSim *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_SIM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_sim_identifier);

    mm_dbg ("loading SIM identifier...");
    qmi_client_dms_uim_get_iccid (QMI_CLIENT_DMS (client),
                                  NULL,
                                  5,
                                  NULL,
                                  (GAsyncReadyCallback)dms_uim_get_iccid_ready,
                                  result);
}

/*****************************************************************************/
/* Load IMSI */

static gchar *
load_imsi_finish (MMBaseSim *self,
                  GAsyncResult *res,
                  GError **error)
{
    gchar *imsi;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    imsi = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded IMSI: %s", imsi);
    return imsi;
}

static void
dms_uim_get_imsi_ready (QmiClientDms *client,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimGetImsiOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_get_imsi_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_get_imsi_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get UIM IMSI: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str = NULL;

        qmi_message_dms_uim_get_imsi_output_get_imsi (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_uim_get_imsi_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_imsi (MMBaseSim *self,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_SIM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_imsi);

    mm_dbg ("loading IMSI...");
    qmi_client_dms_uim_get_imsi (QMI_CLIENT_DMS (client),
                                 NULL,
                                 5,
                                 NULL,
                                 (GAsyncReadyCallback)dms_uim_get_imsi_ready,
                                 result);
}

/*****************************************************************************/
/* Load operator identifier */

static gboolean
get_home_network (QmiClientNas *client,
                  GAsyncResult *res,
                  guint16      *out_mcc,
                  guint16      *out_mnc,
                  gchar        **out_operator_name,
                  GError       **error)
{
    QmiMessageNasGetHomeNetworkOutput *output = NULL;
    gboolean success = FALSE;

    output = qmi_client_nas_get_home_network_finish (client, res, error);
    if (!output) {
        g_prefix_error (error, "QMI operation failed: ");
    } else if (!qmi_message_nas_get_home_network_output_get_result (output, error)) {
        g_prefix_error (error, "Couldn't get home network: ");
    } else {
        const gchar *name = NULL;

        qmi_message_nas_get_home_network_output_get_home_network (
            output,
            out_mcc,
            out_mnc,
            &name,
            NULL);
        if (out_operator_name)
            *out_operator_name = g_strdup (name);
        success = TRUE;
    }

    if (output)
        qmi_message_nas_get_home_network_output_unref (output);

    return success;
}

static gchar *
load_operator_identifier_finish (MMBaseSim *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    gchar *operator_identifier;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    operator_identifier = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded operator identifier: %s", operator_identifier);
    return operator_identifier;
}

static void
load_operator_identifier_ready (QmiClientNas *client,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    guint16 mcc, mnc;
    GError *error = NULL;

    if (get_home_network (client, res, &mcc, &mnc, NULL, &error)) {
        GString *aux;

        aux = g_string_new ("");
        /* MCC always 3 digits */
        g_string_append_printf (aux, "%.3" G_GUINT16_FORMAT, mcc);
        /* Guess about MNC, if < 100 assume it's 2 digits, no PCS info here */
        if (mnc >= 100)
            g_string_append_printf (aux, "%.3" G_GUINT16_FORMAT, mnc);
        else
            g_string_append_printf (aux, "%.2" G_GUINT16_FORMAT, mnc);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_string_free (aux, FALSE),
                                                   (GDestroyNotify)g_free);
    } else {
        g_simple_async_result_take_error (simple, error);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_operator_identifier (MMBaseSim *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_SIM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_operator_identifier);

    mm_dbg ("loading SIM operator identifier...");
    qmi_client_nas_get_home_network (QMI_CLIENT_NAS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)load_operator_identifier_ready,
                                     result);
}

/*****************************************************************************/
/* Load operator name */

static gchar *
load_operator_name_finish (MMBaseSim *self,
                           GAsyncResult *res,
                           GError **error)
{
    gchar *operator_name;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    operator_name = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded operator name: %s", operator_name);
    return operator_name;
}

static void
load_operator_name_ready (QmiClientNas *client,
                          GAsyncResult *res,
                          GSimpleAsyncResult *simple)
{
    gchar *operator_name = NULL;
    GError *error = NULL;

    if (get_home_network (client, res, NULL, NULL, &operator_name, &error)) {
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   operator_name,
                                                   (GDestroyNotify)g_free);
    } else {
        g_simple_async_result_take_error (simple, error);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_operator_name (MMBaseSim *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_SIM_QMI (self),
                            QMI_SERVICE_NAS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        load_operator_name);

    mm_dbg ("loading SIM operator name...");
    qmi_client_nas_get_home_network (QMI_CLIENT_NAS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)load_operator_name_ready,
                                     result);
}

/*****************************************************************************/
/* Send PIN */

static GError *
pin_qmi_error_to_mobile_equipment_error (GError *qmi_error)
{
    GError *me_error = NULL;

    if (g_error_matches (qmi_error,
                         QMI_PROTOCOL_ERROR,
                         QMI_PROTOCOL_ERROR_INCORRECT_PIN)) {
        me_error = g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR,
                                        MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD,
                                        qmi_error->message);
    } else if (g_error_matches (qmi_error,
                                QMI_PROTOCOL_ERROR,
                                QMI_PROTOCOL_ERROR_PIN_BLOCKED)) {
        me_error = g_error_new_literal (MM_MOBILE_EQUIPMENT_ERROR,
                                        MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK,
                                        qmi_error->message);
    }

    if (me_error) {
        g_error_free (qmi_error);
        return me_error;
    }

    return qmi_error;
}

static gboolean
send_pin_finish (MMBaseSim *self,
                 GAsyncResult *res,
                 GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
dms_uim_verify_pin_ready (QmiClientDms *client,
                          GAsyncResult *res,
                          GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimVerifyPinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_verify_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_verify_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't verify PIN: ");
        g_simple_async_result_take_error (simple,
                                          pin_qmi_error_to_mobile_equipment_error (error));
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (output)
        qmi_message_dms_uim_verify_pin_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
send_pin (MMBaseSim *self,
          const gchar *pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    QmiMessageDmsUimVerifyPinInput *input;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_SIM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        send_pin);

    mm_dbg ("Sending PIN...");
    input = qmi_message_dms_uim_verify_pin_input_new ();
    qmi_message_dms_uim_verify_pin_input_set_info (
        input,
        QMI_DMS_UIM_PIN_ID_PIN,
        pin,
        NULL);
    qmi_client_dms_uim_verify_pin (QMI_CLIENT_DMS (client),
                                   input,
                                   5,
                                   NULL,
                                   (GAsyncReadyCallback)dms_uim_verify_pin_ready,
                                   result);
    qmi_message_dms_uim_verify_pin_input_unref (input);
}

/*****************************************************************************/
/* Send PUK */

static gboolean
send_puk_finish (MMBaseSim *self,
                 GAsyncResult *res,
                 GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
dms_uim_unblock_pin_ready (QmiClientDms *client,
                           GAsyncResult *res,
                           GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimUnblockPinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_unblock_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_unblock_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't unblock PIN: ");
        g_simple_async_result_take_error (simple,
                                          pin_qmi_error_to_mobile_equipment_error (error));
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (output)
        qmi_message_dms_uim_unblock_pin_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
send_puk (MMBaseSim *self,
          const gchar *puk,
          const gchar *new_pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    QmiMessageDmsUimUnblockPinInput *input;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_SIM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        send_puk);

    mm_dbg ("Sending PUK...");

    input = qmi_message_dms_uim_unblock_pin_input_new ();
    qmi_message_dms_uim_unblock_pin_input_set_info (
        input,
        QMI_DMS_UIM_PIN_ID_PIN,
        puk,
        new_pin,
        NULL);
    qmi_client_dms_uim_unblock_pin (QMI_CLIENT_DMS (client),
                                    input,
                                    5,
                                    NULL,
                                    (GAsyncReadyCallback)dms_uim_unblock_pin_ready,
                                    result);
    qmi_message_dms_uim_unblock_pin_input_unref (input);
}

/*****************************************************************************/
/* Change PIN */

static gboolean
change_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
dms_uim_change_pin_ready (QmiClientDms *client,
                           GAsyncResult *res,
                           GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimChangePinOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_change_pin_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_change_pin_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't change PIN: ");
        g_simple_async_result_take_error (simple,
                                          pin_qmi_error_to_mobile_equipment_error (error));
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (output)
        qmi_message_dms_uim_change_pin_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
change_pin (MMBaseSim *self,
            const gchar *old_pin,
            const gchar *new_pin,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    QmiMessageDmsUimChangePinInput *input;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_SIM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        change_pin);

    mm_dbg ("Changing PIN...");

    input = qmi_message_dms_uim_change_pin_input_new ();
    qmi_message_dms_uim_change_pin_input_set_info (
        input,
        QMI_DMS_UIM_PIN_ID_PIN,
        old_pin,
        new_pin,
        NULL);
    qmi_client_dms_uim_change_pin (QMI_CLIENT_DMS (client),
                                   input,
                                   5,
                                   NULL,
                                   (GAsyncReadyCallback)dms_uim_change_pin_ready,
                                   result);
    qmi_message_dms_uim_change_pin_input_unref (input);
}

/*****************************************************************************/
/* Enable PIN */

static gboolean
enable_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
dms_uim_set_pin_protection_ready (QmiClientDms *client,
                                  GAsyncResult *res,
                                  GSimpleAsyncResult *simple)
{
    QmiMessageDmsUimSetPinProtectionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_uim_set_pin_protection_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_uim_set_pin_protection_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't enable PIN: ");
        g_simple_async_result_take_error (simple,
                                          pin_qmi_error_to_mobile_equipment_error (error));
    } else {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (output)
        qmi_message_dms_uim_set_pin_protection_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
enable_pin (MMBaseSim *self,
            const gchar *pin,
            gboolean enabled,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    QmiMessageDmsUimSetPinProtectionInput *input;
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_SIM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        enable_pin);

    mm_dbg ("%s PIN...",
            enabled ? "Enabling" : "Disabling");

    input = qmi_message_dms_uim_set_pin_protection_input_new ();
    qmi_message_dms_uim_set_pin_protection_input_set_info (
        input,
        QMI_DMS_UIM_PIN_ID_PIN,
        enabled,
        pin,
        NULL);
    qmi_client_dms_uim_set_pin_protection (QMI_CLIENT_DMS (client),
                                           input,
                                           5,
                                           NULL,
                                           (GAsyncReadyCallback)dms_uim_set_pin_protection_ready,
                                           result);
    qmi_message_dms_uim_set_pin_protection_input_unref (input);
}

/*****************************************************************************/

MMBaseSim *
mm_sim_qmi_new_finish (GAsyncResult  *res,
                       GError       **error)
{
    GObject *source;
    GObject *sim;

    source = g_async_result_get_source_object (res);
    sim = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!sim)
        return NULL;

    /* Only export valid SIMs */
    mm_base_sim_export (MM_BASE_SIM (sim));

    return MM_BASE_SIM (sim);
}

void
mm_sim_qmi_new (MMBaseModem *modem,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_QMI,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                NULL);
}

static void
mm_sim_qmi_init (MMSimQmi *self)
{
}

static void
mm_sim_qmi_class_init (MMSimQmiClass *klass)
{
    MMBaseSimClass *base_sim_class = MM_BASE_SIM_CLASS (klass);

    base_sim_class->load_sim_identifier = load_sim_identifier;
    base_sim_class->load_sim_identifier_finish = load_sim_identifier_finish;
    base_sim_class->load_imsi = load_imsi;
    base_sim_class->load_imsi_finish = load_imsi_finish;
    base_sim_class->load_operator_identifier = load_operator_identifier;
    base_sim_class->load_operator_identifier_finish = load_operator_identifier_finish;
    base_sim_class->load_operator_name = load_operator_name;
    base_sim_class->load_operator_name_finish = load_operator_name_finish;
    base_sim_class->send_pin = send_pin;
    base_sim_class->send_pin_finish = send_pin_finish;
    base_sim_class->send_puk = send_puk;
    base_sim_class->send_puk_finish = send_puk_finish;
    base_sim_class->change_pin = change_pin;
    base_sim_class->change_pin_finish = change_pin_finish;
    base_sim_class->enable_pin = enable_pin;
    base_sim_class->enable_pin_finish = enable_pin_finish;
}
