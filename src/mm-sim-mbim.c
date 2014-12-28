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

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-error-helpers.h"
#include "mm-log.h"
#include "mm-sim-mbim.h"

G_DEFINE_TYPE (MMSimMbim, mm_sim_mbim, MM_TYPE_BASE_SIM)

/*****************************************************************************/

static gboolean
peek_device (gpointer self,
             MbimDevice **o_device,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    MMBaseModem *modem = NULL;
    MMPortMbim *port;

    g_object_get (G_OBJECT (self),
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    port = mm_base_modem_peek_port_mbim (modem);
    g_object_unref (modem);

    if (!port) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't peek MBIM port");
        return FALSE;
    }

    *o_device = mm_port_mbim_peek_device (port);
    return TRUE;
}

/*****************************************************************************/
/* Load SIM identifier */

static gchar *
load_sim_identifier_finish (MMBaseSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    return g_strdup ((gchar *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
simid_subscriber_ready_state_ready (MbimDevice *device,
                                    GAsyncResult *res,
                                    GSimpleAsyncResult *simple)
{
    MbimMessage *response;
    GError *error = NULL;
    gchar *sim_iccid;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_subscriber_ready_status_response_parse (
            response,
            NULL, /* ready_state */
            NULL, /* subscriber_id */
            &sim_iccid,
            NULL, /* ready_info */
            NULL, /* telephone_numbers_count */
            NULL, /* telephone_numbers */
            &error))
        g_simple_async_result_set_op_res_gpointer (simple, sim_iccid, (GDestroyNotify)g_free);
    else
        g_simple_async_result_take_error (simple, error);

    if (response)
        mbim_message_unref (response);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_sim_identifier (MMBaseSim *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GSimpleAsyncResult *result;

    if (!peek_device (self, &device, callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, load_sim_identifier);

    message = mbim_message_subscriber_ready_status_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)simid_subscriber_ready_state_ready,
                         result);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Load IMSI */

static gchar *
load_imsi_finish (MMBaseSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;
    return g_strdup ((gchar *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
imsi_subscriber_ready_state_ready (MbimDevice *device,
                                   GAsyncResult *res,
                                   GSimpleAsyncResult *simple)
{
    MbimMessage *response;
    GError *error = NULL;
    gchar *subscriber_id;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_subscriber_ready_status_response_parse (
            response,
            NULL, /* ready_state */
            &subscriber_id,
            NULL, /* sim_iccid */
            NULL, /* ready_info */
            NULL, /* telephone_numbers_count */
            NULL, /* telephone_numbers */
            &error))
        g_simple_async_result_set_op_res_gpointer (simple, subscriber_id, (GDestroyNotify)g_free);
    else
        g_simple_async_result_take_error (simple, error);

    if (response)
        mbim_message_unref (response);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_imsi (MMBaseSim *self,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GSimpleAsyncResult *result;

    if (!peek_device (self, &device, callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, load_imsi);

    message = mbim_message_subscriber_ready_status_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)imsi_subscriber_ready_state_ready,
                         result);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Load operator identifier */

static gchar *
load_operator_identifier_finish (MMBaseSim *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    MbimProvider *provider;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    provider = (MbimProvider *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    return g_strdup (provider->provider_id);
}

static void
load_operator_identifier_ready (MbimDevice *device,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimProvider *provider;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_home_provider_response_parse (
            response,
            &provider,
            &error))
        g_simple_async_result_set_op_res_gpointer (simple, provider, (GDestroyNotify)mbim_provider_free);
    else
        g_simple_async_result_take_error (simple, error);

    if (response)
        mbim_message_unref (response);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_operator_identifier (MMBaseSim *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GSimpleAsyncResult *result;

    if (!peek_device (self, &device, callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, load_operator_identifier);

    message = mbim_message_home_provider_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)load_operator_identifier_ready,
                         result);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Load operator name */

static gchar *
load_operator_name_finish (MMBaseSim *self,
                           GAsyncResult *res,
                           GError **error)
{
    MbimProvider *provider;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    provider = (MbimProvider *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    return g_strdup (provider->provider_name);
}

static void
load_operator_name_ready (MbimDevice *device,
                          GAsyncResult *res,
                          GSimpleAsyncResult *simple)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimProvider *provider;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_home_provider_response_parse (
            response,
            &provider,
            &error))
        g_simple_async_result_set_op_res_gpointer (simple, provider, (GDestroyNotify)mbim_provider_free);
    else
        g_simple_async_result_take_error (simple, error);

    if (response)
        mbim_message_unref (response);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
load_operator_name (MMBaseSim *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GSimpleAsyncResult *result;

    if (!peek_device (self, &device, callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, load_operator_name);

    message = mbim_message_home_provider_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)load_operator_name_ready,
                         result);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Send PIN */

static gboolean
send_pin_finish (MMBaseSim *self,
                 GAsyncResult *res,
                 GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
pin_set_enter_ready (MbimDevice *device,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    MbimMessage *response;
    MbimPinType pin_type;
    MbimPinState pin_state;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        /* Sending PIN failed, build a better error to report */
        if (mbim_message_pin_response_parse (
                response,
                &pin_type,
                &pin_state,
                NULL,
                NULL)) {
            /* Create the errors ourselves */
            if (pin_type == MBIM_PIN_TYPE_PIN1 && pin_state == MBIM_PIN_STATE_LOCKED) {
                g_error_free (error);
                error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD);
            } else if (pin_type == MBIM_PIN_TYPE_PUK1 && pin_state == MBIM_PIN_STATE_LOCKED) {
                g_error_free (error);
                error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK);
            }
        }
    }

    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
    if (response)
        mbim_message_unref (response);
}

static void
send_pin (MMBaseSim *self,
          const gchar *pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GSimpleAsyncResult *result;
    GError *error = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, send_pin);

    mm_dbg ("Sending PIN...");
    message = (mbim_message_pin_set_new (
                   MBIM_PIN_TYPE_PIN1,
                   MBIM_PIN_OPERATION_ENTER,
                   pin,
                   "",
                   &error));
    if (!message) {
        g_simple_async_result_take_error (result, error);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_set_enter_ready,
                         result);
    mbim_message_unref (message);
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
puk_set_enter_ready (MbimDevice *device,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    MbimMessage *response;
    MbimPinType pin_type;
    MbimPinState pin_state;
    guint32 remaining_attempts;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        /* Sending PUK failed, build a better error to report */
        if (mbim_message_pin_response_parse (
                response,
                &pin_type,
                &pin_state,
                &remaining_attempts,
                NULL)) {
            /* Create the errors ourselves */
            if (pin_type == MBIM_PIN_TYPE_PUK1 && pin_state == MBIM_PIN_STATE_LOCKED) {
                g_error_free (error);
                if (remaining_attempts == 0)
                    error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG);
                else
                    error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD);
            }
        }
    }

    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
    if (response)
        mbim_message_unref (response);
}

static void
send_puk (MMBaseSim *self,
          const gchar *puk,
          const gchar *new_pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GSimpleAsyncResult *result;
    GError *error = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, send_puk);

    mm_dbg ("Sending PUK...");
    message = (mbim_message_pin_set_new (
                   MBIM_PIN_TYPE_PUK1,
                   MBIM_PIN_OPERATION_ENTER,
                   puk,
                   new_pin,
                   &error));
    if (!message) {
        g_simple_async_result_take_error (result, error);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)puk_set_enter_ready,
                         result);
    mbim_message_unref (message);
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
pin_set_enable_ready (MbimDevice *device,
                      GAsyncResult *res,
                      GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    MbimMessage *response;

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);
        mbim_message_unref (response);
    }

    if (error) {
        if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PIN_REQUIRED)) {
            g_error_free (error);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_WRONG_STATE,
                                 "Need to be unlocked to allow enabling/disabling PIN");
        }

        g_simple_async_result_take_error (simple, error);
    } else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
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
    MbimDevice *device;
    MbimMessage *message;
    GSimpleAsyncResult *result;
    GError *error = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, send_puk);

    mm_dbg ("%s PIN ...", enabled ? "Enabling" : "Disabling");
    message = (mbim_message_pin_set_new (
                   MBIM_PIN_TYPE_PIN1,
                   enabled ? MBIM_PIN_OPERATION_ENABLE : MBIM_PIN_OPERATION_DISABLE,
                   pin,
                   "",
                   &error));
    if (!message) {
        g_simple_async_result_take_error (result, error);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_set_enable_ready,
                         result);
    mbim_message_unref (message);
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
pin_set_change_ready (MbimDevice *device,
                      GAsyncResult *res,
                      GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    MbimMessage *response;

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);
        mbim_message_unref (response);
    }

    if (error) {
        if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PIN_REQUIRED)) {
            g_error_free (error);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_WRONG_STATE,
                                 "Need to be unlocked to allow changing PIN");
        }

        g_simple_async_result_take_error (simple, error);
    } else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
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
    MbimDevice *device;
    MbimMessage *message;
    GSimpleAsyncResult *result;
    GError *error = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self), callback, user_data, send_puk);

    mm_dbg ("Changing PIN");
    message = (mbim_message_pin_set_new (
                   MBIM_PIN_TYPE_PIN1,
                   MBIM_PIN_OPERATION_CHANGE,
                   old_pin,
                   new_pin,
                   &error));
    if (!message) {
        g_simple_async_result_take_error (result, error);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_set_change_ready,
                         result);
    mbim_message_unref (message);
}

/*****************************************************************************/

MMBaseSim *
mm_sim_mbim_new_finish (GAsyncResult  *res,
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
mm_sim_mbim_new (MMBaseModem *modem,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_MBIM,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                NULL);
}

static void
mm_sim_mbim_init (MMSimMbim *self)
{
}

static void
mm_sim_mbim_class_init (MMSimMbimClass *klass)
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
    base_sim_class->enable_pin = enable_pin;
    base_sim_class->enable_pin_finish = enable_pin_finish;
    base_sim_class->change_pin = change_pin;
    base_sim_class->change_pin_finish = change_pin_finish;
}
