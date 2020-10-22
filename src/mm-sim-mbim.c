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

#include "mm-broadband-modem-mbim.h"
#include "mm-error-helpers.h"
#include "mm-iface-modem.h"
#include "mm-log-object.h"
#include "mm-modem-helpers-mbim.h"
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

    port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (modem));
    g_object_unref (modem);

    if (!port) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 peek_device,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't peek MBIM port");
        return FALSE;
    }

    *o_device = mm_port_mbim_peek_device (port);
    return TRUE;
}

static void
update_modem_unlock_retries (MMSimMbim *self,
                             MbimPinType pin_type,
                             guint32 remaining_attempts)
{
    MMBaseModem *modem = NULL;
    MMUnlockRetries *unlock_retries;

    g_object_get (G_OBJECT (self),
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    unlock_retries = mm_unlock_retries_new ();
    mm_unlock_retries_set (unlock_retries,
                           mm_modem_lock_from_mbim_pin_type (pin_type),
                           remaining_attempts);
    mm_iface_modem_update_unlock_retries (MM_IFACE_MODEM (modem),
                                          unlock_retries);
    g_object_unref (unlock_retries);
    g_object_unref (modem);
}

/*****************************************************************************/
/* Load SIM identifier */

static gchar *
load_sim_identifier_finish (MMBaseSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
simid_subscriber_ready_state_ready (MbimDevice *device,
                                    GAsyncResult *res,
                                    GTask *task)
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
        g_task_return_pointer (task, sim_iccid, g_free);
    else
        g_task_return_error (task, error);
    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
load_sim_identifier (MMBaseSim *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_subscriber_ready_status_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)simid_subscriber_ready_state_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Load IMSI */

static gchar *
load_imsi_finish (MMBaseSim *self,
                  GAsyncResult *res,
                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
imsi_subscriber_ready_state_ready (MbimDevice *device,
                                   GAsyncResult *res,
                                   GTask *task)
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
        g_task_return_pointer (task, subscriber_id, g_free);
    else
        g_task_return_error (task, error);
    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
load_imsi (MMBaseSim *self,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_subscriber_ready_status_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)imsi_subscriber_ready_state_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Load operator identifier */

static gchar *
load_operator_identifier_finish (MMBaseSim *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_operator_identifier_ready (MbimDevice *device,
                                GAsyncResult *res,
                                GTask *task)
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
            &error)) {
        g_task_return_pointer (task, g_strdup (provider->provider_id), g_free);
        mbim_provider_free (provider);
    } else
        g_task_return_error (task, error);
    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
load_operator_identifier (MMBaseSim *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_home_provider_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)load_operator_identifier_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Load operator name */

static gchar *
load_operator_name_finish (MMBaseSim *self,
                           GAsyncResult *res,
                           GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_operator_name_ready (MbimDevice *device,
                          GAsyncResult *res,
                          GTask *task)
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
            &error)) {
        g_task_return_pointer (task, g_strdup (provider->provider_name), g_free);
        mbim_provider_free (provider);
    } else
        g_task_return_error (task, error);
    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
load_operator_name (MMBaseSim *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_home_provider_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)load_operator_name_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Send PIN */

static gboolean
send_pin_finish (MMBaseSim *self,
                 GAsyncResult *res,
                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pin_set_enter_ready (MbimDevice *device,
                     GAsyncResult *res,
                     GTask *task)
{
    MMSimMbim *self;
    GError *error = NULL;
    MbimMessage *response;
    gboolean success;
    MbimPinType pin_type;
    MbimPinState pin_state;
    guint32 remaining_attempts;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        success = mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);

        if (mbim_message_pin_response_parse (response,
                                             &pin_type,
                                             &pin_state,
                                             &remaining_attempts,
                                             NULL)) {
            update_modem_unlock_retries (self, pin_type, remaining_attempts);

            if (!success) {
                /* Sending PIN failed, build a better error to report */
                if (pin_type == MBIM_PIN_TYPE_PIN1 && pin_state == MBIM_PIN_STATE_LOCKED) {
                    g_error_free (error);
                    error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD, self);
                } else if (pin_type == MBIM_PIN_TYPE_PUK1 && pin_state == MBIM_PIN_STATE_LOCKED) {
                    g_error_free (error);
                    error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK, self);
                }
            }
        }

        mbim_message_unref (response);
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
send_pin (MMBaseSim *self,
          const gchar *pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;
    GError *error = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "sending PIN...");
    message = (mbim_message_pin_set_new (
                   MBIM_PIN_TYPE_PIN1,
                   MBIM_PIN_OPERATION_ENTER,
                   pin,
                   "",
                   &error));
    if (!message) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_set_enter_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Send PUK */

static gboolean
send_puk_finish (MMBaseSim *self,
                 GAsyncResult *res,
                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
puk_set_enter_ready (MbimDevice *device,
                     GAsyncResult *res,
                     GTask *task)
{
    MMSimMbim *self;
    GError *error = NULL;
    MbimMessage *response;
    gboolean success;
    MbimPinType pin_type;
    MbimPinState pin_state;
    guint32 remaining_attempts;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        success = mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);

        if (mbim_message_pin_response_parse (response,
                                             &pin_type,
                                             &pin_state,
                                             &remaining_attempts,
                                             NULL)) {
            update_modem_unlock_retries (self, pin_type, remaining_attempts);

            if (!success) {
                /* Sending PUK failed, build a better error to report */
                if (pin_type == MBIM_PIN_TYPE_PUK1 && pin_state == MBIM_PIN_STATE_LOCKED) {
                    g_error_free (error);
                    if (remaining_attempts == 0)
                        error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG, self);
                    else
                        error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD, self);
                }
            }
        }

        mbim_message_unref (response);
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
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
    GTask *task;
    GError *error = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "sending PUK...");
    message = (mbim_message_pin_set_new (
                   MBIM_PIN_TYPE_PUK1,
                   MBIM_PIN_OPERATION_ENTER,
                   puk,
                   new_pin,
                   &error));
    if (!message) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)puk_set_enter_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Enable PIN */

static gboolean
enable_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pin_set_enable_ready (MbimDevice *device,
                      GAsyncResult *res,
                      GTask *task)
{
    MMSimMbim *self;
    GError *error = NULL;
    MbimMessage *response;
    MbimPinType pin_type;
    guint32 remaining_attempts;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);

        if (mbim_message_pin_response_parse (response,
                                             &pin_type,
                                             NULL,
                                             &remaining_attempts,
                                             NULL))
            update_modem_unlock_retries (self, pin_type, remaining_attempts);

        mbim_message_unref (response);
    }

    if (error) {
        if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PIN_REQUIRED)) {
            g_error_free (error);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_WRONG_STATE,
                                 "Need to be unlocked to allow enabling/disabling PIN");
        }

        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
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
    GTask *task;
    GError *error = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "%s PIN ...", enabled ? "enabling" : "disabling");
    message = (mbim_message_pin_set_new (
                   MBIM_PIN_TYPE_PIN1,
                   enabled ? MBIM_PIN_OPERATION_ENABLE : MBIM_PIN_OPERATION_DISABLE,
                   pin,
                   "",
                   &error));
    if (!message) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_set_enable_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Change PIN */

static gboolean
change_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
pin_set_change_ready (MbimDevice *device,
                      GAsyncResult *res,
                      GTask *task)
{
    MMSimMbim *self;
    GError *error = NULL;
    MbimMessage *response;
    MbimPinType pin_type;
    guint32 remaining_attempts;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);

        if (mbim_message_pin_response_parse (response,
                                             &pin_type,
                                             NULL,
                                             &remaining_attempts,
                                             NULL))
            update_modem_unlock_retries (self, pin_type, remaining_attempts);

        mbim_message_unref (response);
    }

    if (error) {
        if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PIN_REQUIRED)) {
            g_error_free (error);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_WRONG_STATE,
                                 "Need to be unlocked to allow changing PIN");
        }

        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
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
    GTask *task;
    GError *error = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_obj_dbg (self, "changing PIN...");
    message = (mbim_message_pin_set_new (
                   MBIM_PIN_TYPE_PIN1,
                   MBIM_PIN_OPERATION_CHANGE,
                   old_pin,
                   new_pin,
                   &error));
    if (!message) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_set_change_ready,
                         task);
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
                                "active", TRUE, /* by default always active */
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
