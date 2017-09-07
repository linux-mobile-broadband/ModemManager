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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "mm-modem-helpers-mbim.h"
#include "mm-broadband-modem-mbim.h"
#include "mm-bearer-mbim.h"
#include "mm-sim-mbim.h"
#include "mm-sms-mbim.h"

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-error-helpers.h"
#include "mm-modem-helpers.h"
#include "mm-bearer-list.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-messaging.h"
#include "mm-iface-modem-signal.h"
#include "mm-sms-part-3gpp.h"

#if defined WITH_QMI
# include <libqmi-glib.h>
#endif

static void iface_modem_init           (MMIfaceModem          *iface);
static void iface_modem_3gpp_init      (MMIfaceModem3gpp      *iface);
static void iface_modem_location_init  (MMIfaceModemLocation  *iface);
static void iface_modem_messaging_init (MMIfaceModemMessaging *iface);
static void iface_modem_signal_init    (MMIfaceModemSignal    *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbim, mm_broadband_modem_mbim, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_MESSAGING, iface_modem_messaging_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_SIGNAL, iface_modem_signal_init))

typedef enum {
    PROCESS_NOTIFICATION_FLAG_NONE                 = 0,
    PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY       = 1 << 0,
    PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES = 1 << 1,
    PROCESS_NOTIFICATION_FLAG_SMS_READ             = 1 << 2,
    PROCESS_NOTIFICATION_FLAG_CONNECT              = 1 << 3,
    PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO      = 1 << 4,
    PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE       = 1 << 5,
} ProcessNotificationFlag;

struct _MMBroadbandModemMbimPrivate {
    /* Queried and cached capabilities */
    MbimCellularClass caps_cellular_class;
    MbimDataClass caps_data_class;
    MbimSmsCaps caps_sms;
    guint caps_max_sessions;
    gchar *caps_device_id;
    gchar *caps_firmware_info;

    /* Process unsolicited notifications */
    guint notification_id;
    ProcessNotificationFlag setup_flags;
    ProcessNotificationFlag enable_flags;

    /* 3GPP registration helpers */
    gchar *current_operator_id;
    gchar *current_operator_name;

    /* Access technology updates */
    MbimDataClass available_data_classes;
    MbimDataClass highest_available_data_class;

    MbimSubscriberReadyState last_ready_state;

    /* For notifying when the mbim-proxy connection is dead */
    gulong mbim_device_removed_id;

    /* Previously observed SIM-PIN remaining retries */
    guint sim_pin_retries;
};

/*****************************************************************************/

static gboolean
peek_device (gpointer self,
             MbimDevice **o_device,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    MMPortMbim *port;

    port = mm_base_modem_peek_port_mbim (MM_BASE_MODEM (self));
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

/*****************************************************************************/
/* Current Capabilities loading (Modem interface) */

static MMModemCapability
modem_load_current_capabilities_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_CAPABILITY_NONE;
    }
    return (MMModemCapability)value;
}

static void
device_caps_query_ready (MbimDevice *device,
                         GAsyncResult *res,
                         GTask *task)
{
    MMBroadbandModemMbim *self;
    MMModemCapability mask;
    MbimMessage *response;
    GError *error = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_device_caps_response_parse (
            response,
            NULL, /* device_type */
            &self->priv->caps_cellular_class,
            NULL, /* voice_class */
            NULL, /* sim_class */
            &self->priv->caps_data_class,
            &self->priv->caps_sms,
            NULL, /* ctrl_caps */
            &self->priv->caps_max_sessions,
            NULL, /* custom_data_class */
            &self->priv->caps_device_id,
            &self->priv->caps_firmware_info,
            NULL, /* hardware_info */
            &error)) {
        /* Build mask of modem capabilities */
        mask = 0;
        if (self->priv->caps_cellular_class & MBIM_CELLULAR_CLASS_GSM)
            mask |= MM_MODEM_CAPABILITY_GSM_UMTS;

#if 0  /* Disable until we add MBIM CDMA support */
        if (self->priv->caps_cellular_class & MBIM_CELLULAR_CLASS_CDMA)
            mask |= MM_MODEM_CAPABILITY_CDMA_EVDO;
#endif

        if (self->priv->caps_data_class & MBIM_DATA_CLASS_LTE)
            mask |= MM_MODEM_CAPABILITY_LTE;
        g_task_return_int (task, mask);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_load_current_capabilities (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_dbg ("loading current capabilities...");
    message = mbim_message_device_caps_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)device_caps_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Manufacturer loading (Modem interface) */

static gchar *
modem_load_manufacturer_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_load_manufacturer (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GTask *task;
    gchar *manufacturer;

    manufacturer = g_strdup (mm_base_modem_get_plugin (MM_BASE_MODEM (self)));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, manufacturer, g_free);
    g_object_unref (task);
}

/*****************************************************************************/
/* Model loading (Modem interface) */

static gchar *
modem_load_model_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_load_model (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    gchar *model;
    GTask *task;

    model = g_strdup_printf ("MBIM [%04X:%04X]",
                             (mm_base_modem_get_vendor_id (MM_BASE_MODEM (self)) & 0xFFFF),
                             (mm_base_modem_get_product_id (MM_BASE_MODEM (self)) & 0xFFFF));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, model, g_free);
    g_object_unref (task);
}

/*****************************************************************************/
/* Revision loading (Modem interface) */

static gchar *
modem_load_revision_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_load_revision (MMIfaceModem *_self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->caps_firmware_info)
        g_task_return_pointer (task,
                               g_strdup (self->priv->caps_firmware_info),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Firmware revision information not given in device capabilities");
    g_object_unref (task);
}

/*****************************************************************************/
/* Equipment Identifier loading (Modem interface) */

static gchar *
modem_load_equipment_identifier_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_load_equipment_identifier (MMIfaceModem *_self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->caps_device_id)
        g_task_return_pointer (task,
                               g_strdup (self->priv->caps_device_id),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Device ID not given in device capabilities");
    g_object_unref (task);
}

/*****************************************************************************/
/* Device identifier loading (Modem interface) */

static gchar *
modem_load_device_identifier_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_load_device_identifier (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    gchar *device_identifier;
    GTask *task;

    /* Just use dummy ATI/ATI1 replies, all the other internal info should be
     * enough for uniqueness */
    device_identifier = mm_broadband_modem_create_device_identifier (MM_BROADBAND_MODEM (self), "", "");

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_pointer (task, device_identifier, g_free);
    g_object_unref (task);
}

/*****************************************************************************/
/* Supported modes loading (Modem interface) */

static GArray *
modem_load_supported_modes_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_load_supported_modes (MMIfaceModem *_self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GArray *combinations;
    MMModemModeCombination mode;
    MMModemMode all;
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->caps_data_class == 0) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Data class not given in device capabilities");
        g_object_unref (task);
        return;
    }

    all = 0;

    /* 3GPP... */
    if (self->priv->caps_data_class & (MBIM_DATA_CLASS_GPRS |
                                       MBIM_DATA_CLASS_EDGE))
        all |= MM_MODEM_MODE_2G;
    if (self->priv->caps_data_class & (MBIM_DATA_CLASS_UMTS |
                                       MBIM_DATA_CLASS_HSDPA |
                                       MBIM_DATA_CLASS_HSUPA))
        all |= MM_MODEM_MODE_3G;
    if (self->priv->caps_data_class & MBIM_DATA_CLASS_LTE)
        all |= MM_MODEM_MODE_4G;

    /* 3GPP2... */
    if (self->priv->caps_data_class & MBIM_DATA_CLASS_1XRTT)
        all |= MM_MODEM_MODE_2G;
    if (self->priv->caps_data_class & (MBIM_DATA_CLASS_1XEVDO |
                                       MBIM_DATA_CLASS_1XEVDO_REVA |
                                       MBIM_DATA_CLASS_1XEVDV |
                                       MBIM_DATA_CLASS_3XRTT |
                                       MBIM_DATA_CLASS_1XEVDO_REVB))
        all |= MM_MODEM_MODE_3G;
    if (self->priv->caps_data_class & MBIM_DATA_CLASS_UMB)
        all |= MM_MODEM_MODE_4G;

    /* Build a mask with all supported modes */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
    mode.allowed = all;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);

    g_task_return_pointer (task, combinations, (GDestroyNotify)g_array_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported IP families (Modem interface) */

static MMBearerIpFamily
modem_load_supported_ip_families_finish (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_BEARER_IP_FAMILY_NONE;
    }
    return (MMBearerIpFamily)value;
}

static void
modem_load_supported_ip_families (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    /* Assume IPv4 + IPv6 + IPv4v6 supported */
    g_task_return_int (task,
                       MM_BEARER_IP_FAMILY_IPV4 |
                       MM_BEARER_IP_FAMILY_IPV6 |
                       MM_BEARER_IP_FAMILY_IPV4V6);
    g_object_unref (task);
}

/*****************************************************************************/
/* Unlock required loading (Modem interface) */

typedef struct {
    guint n_ready_status_checks;
    MbimDevice *device;
} LoadUnlockRequiredContext;

static void
load_unlock_required_context_free (LoadUnlockRequiredContext *ctx)
{
    g_object_unref (ctx->device);
    g_slice_free (LoadUnlockRequiredContext, ctx);
}

static MMModemLock
modem_load_unlock_required_finish (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCK_UNKNOWN;
    }
    return (MMModemLock)value;
}

static void
pin_query_ready (MbimDevice *device,
                 GAsyncResult *res,
                 GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimPinType pin_type;
    MbimPinState pin_state;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_pin_response_parse (
            response,
            &pin_type,
            &pin_state,
            NULL,
            &error)) {
        MMModemLock unlock_required;

        if (pin_state == MBIM_PIN_STATE_UNLOCKED)
            unlock_required = MM_MODEM_LOCK_NONE;
        else
            unlock_required = mm_modem_lock_from_mbim_pin_type (pin_type);

        g_task_return_int (task, unlock_required);
    }
    /* VZ20M reports an error when SIM-PIN is required... */
    else if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_PIN_REQUIRED)) {
        g_error_free (error);
        g_task_return_int (task, MBIM_PIN_TYPE_PIN1);
    }
    else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static gboolean wait_for_sim_ready (GTask *task);

static void
unlock_required_subscriber_ready_state_ready (MbimDevice *device,
                                              GAsyncResult *res,
                                              GTask *task)
{
    LoadUnlockRequiredContext *ctx;
    MMBroadbandModemMbim *self;
    MbimMessage *response;
    GError *error = NULL;
    MbimSubscriberReadyState ready_state = MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_subscriber_ready_status_response_parse (
            response,
            &ready_state,
            NULL, /* subscriber_id */
            NULL, /* sim_iccid */
            NULL, /* ready_info */
            NULL, /* telephone_numbers_count */
            NULL, /* telephone_numbers */
            &error)) {
        switch (ready_state) {
        case MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED:
        case MBIM_SUBSCRIBER_READY_STATE_INITIALIZED:
        case MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED:
            /* Don't set error */
            break;
        case MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED:
            /* This is an error, but we still want to retry.
             * The MC7710 may use this while the SIM is not ready yet. */
            break;
        case MBIM_SUBSCRIBER_READY_STATE_BAD_SIM:
            error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG);
            break;
        case MBIM_SUBSCRIBER_READY_STATE_FAILURE:
        case MBIM_SUBSCRIBER_READY_STATE_NOT_ACTIVATED:
        default:
            error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE);
            break;
        }
    }

    self->priv->last_ready_state = ready_state;

    /* Fatal errors are reported right away */
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
    }
    /* Need to retry? */
    else if (ready_state == MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED ||
             ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) {
        if (--ctx->n_ready_status_checks == 0) {
            /* All retries consumed, issue error */
            if (ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED)
                g_task_return_error (
                    task,
                    mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED));
            else
                g_task_return_new_error (task,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Error waiting for SIM to get initialized");
            g_object_unref (task);
        } else {
            /* Retry */
            g_timeout_add_seconds (1, (GSourceFunc)wait_for_sim_ready, task);
        }
    }
    /* Initialized but locked? */
    else if (ready_state == MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED) {
        MbimMessage *message;

        /* Query which lock is to unlock */
        message = mbim_message_pin_query_new (NULL);
        mbim_device_command (device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)pin_query_ready,
                             task);
        mbim_message_unref (message);
    }
    /* Initialized but locked? */
    else if (ready_state == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
    } else
        g_assert_not_reached ();

    if (response)
        mbim_message_unref (response);
}

static gboolean
wait_for_sim_ready (GTask *task)
{
    LoadUnlockRequiredContext *ctx;
    MbimMessage *message;

    ctx = g_task_get_task_data (task);
    message = mbim_message_subscriber_ready_status_query_new (NULL);
    mbim_device_command (ctx->device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)unlock_required_subscriber_ready_state_ready,
                         task);
    mbim_message_unref (message);
    return G_SOURCE_REMOVE;
}

static void
modem_load_unlock_required (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    LoadUnlockRequiredContext *ctx;
    MbimDevice *device;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    ctx = g_slice_new (LoadUnlockRequiredContext);
    ctx->device = g_object_ref (device);
    ctx->n_ready_status_checks = 10;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)load_unlock_required_context_free);

    wait_for_sim_ready (task);
}

/*****************************************************************************/
/* Unlock retries loading (Modem interface) */

static MMUnlockRetries *
modem_load_unlock_retries_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
pin_query_unlock_retries_ready (MbimDevice *device,
                                GAsyncResult *res,
                                GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimPinType pin_type;
    guint32 remaining_attempts;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_pin_response_parse (
            response,
            &pin_type,
            NULL,
            &remaining_attempts,
            &error)) {
        MMBroadbandModemMbim *self;
        MMModemLock lock;
        MMUnlockRetries *retries;

        self = g_task_get_source_object (task);
        lock = mm_modem_lock_from_mbim_pin_type (pin_type);
        retries = mm_unlock_retries_new ();

        /* If PIN1 is disabled and we have tried to enable it with a wrong PIN,
         * the modem would have indicated the number of remaining attempts for
         * PIN1 (unless PUK1 is engaged) in the response to the failed
         * MBIM_CID_PIN set operation. Thus, MMSimMbim would have updated
         * MMIfaceModem's MMUnlockRetries with information about PIN1.
         *
         * However, a MBIM_CID_PIN query may be issued (e.g. MMBaseSim calls
         * mm_iface_modem_update_lock_info()) after the MBIM_CID_PIN set
         * operation to query the number of remaining attempts for a PIN type.
         * Unfortunately, we can't specify a particular PIN type in a
         * MBIM_CID_PIN query. The modem may not reply with information about
         * PIN1 if PIN1 is disabled. When that happens, we would like to
         * preserve our knowledge about the number of remaining attempts for
         * PIN1. Here we thus carry over any existing information on PIN1 from
         * MMIfaceModem's MMUnlockRetries if the MBIM_CID_PIN query reports
         * something other than PIN1. */
        if (lock != MM_MODEM_LOCK_SIM_PIN &&
            self->priv->sim_pin_retries != MM_UNLOCK_RETRIES_UNKNOWN) {
            mm_unlock_retries_set (retries,
                                   MM_MODEM_LOCK_SIM_PIN,
                                   self->priv->sim_pin_retries);
        }

        /* According to the MBIM specification, RemainingAttempts is set to
         * 0xffffffff if the device does not support this information. */
        if (remaining_attempts != G_MAXUINT32)
            mm_unlock_retries_set (retries, lock, remaining_attempts);
        else
            remaining_attempts = MM_UNLOCK_RETRIES_UNKNOWN;

        if (lock == MM_MODEM_LOCK_SIM_PIN)
            self->priv->sim_pin_retries = remaining_attempts;

        g_task_return_pointer (task, retries, g_object_unref);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_load_unlock_retries (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_pin_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_query_unlock_retries_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Own numbers loading */

static GStrv
modem_load_own_numbers_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
own_numbers_subscriber_ready_state_ready (MbimDevice *device,
                                          GAsyncResult *res,
                                          GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    gchar **telephone_numbers;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_subscriber_ready_status_response_parse (
            response,
            NULL, /* ready_state */
            NULL, /* subscriber_id */
            NULL, /* sim_iccid */
            NULL, /* ready_info */
            NULL, /* telephone_numbers_count */
            &telephone_numbers,
            &error)) {
        g_task_return_pointer (task, telephone_numbers, (GDestroyNotify)g_strfreev);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_load_own_numbers (MMIfaceModem *self,
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
                         (GAsyncReadyCallback)own_numbers_subscriber_ready_state_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Initial power state loading */

static MMModemPowerState
modem_load_power_state_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_POWER_STATE_UNKNOWN;
    }
    return (MMModemPowerState)value;
}

static void
radio_state_query_ready (MbimDevice *device,
                         GAsyncResult *res,
                         GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimRadioSwitchState hardware_radio_state;
    MbimRadioSwitchState software_radio_state;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_radio_state_response_parse (
            response,
            &hardware_radio_state,
            &software_radio_state,
            &error)) {
        MMModemPowerState state;

        if (hardware_radio_state == MBIM_RADIO_SWITCH_STATE_OFF ||
            software_radio_state == MBIM_RADIO_SWITCH_STATE_OFF)
            state = MM_MODEM_POWER_STATE_LOW;
        else
            state = MM_MODEM_POWER_STATE_ON;
        g_task_return_int (task, state);
    } else
        g_task_return_error (task, error);
    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_load_power_state (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_radio_state_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)radio_state_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Power up (Modem interface) */

typedef enum {
    POWER_UP_CONTEXT_STEP_FIRST,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    POWER_UP_CONTEXT_STEP_QMI_DEVICE_NEW,
    POWER_UP_CONTEXT_STEP_QMI_DEVICE_OPEN,
    POWER_UP_CONTEXT_STEP_ALLOCATE_QMI_CLIENT_DMS,
    POWER_UP_CONTEXT_STEP_FCC_AUTH,
    POWER_UP_CONTEXT_STEP_RELEASE_QMI_CLIENT_DMS,
    POWER_UP_CONTEXT_STEP_RETRY,
#endif
    POWER_UP_CONTEXT_STEP_LAST,
} PowerUpContextStep;

typedef struct {
    MbimDevice *device;
    PowerUpContextStep step;
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    QmiDevice *qmi_device;
    QmiClient *qmi_client;
    GError *saved_error;
#endif
} PowerUpContext;

static void
power_up_context_free (PowerUpContext *ctx)
{
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (ctx->qmi_device) {
        if (ctx->qmi_client) {
            qmi_device_release_client (ctx->qmi_device,
                                       ctx->qmi_client,
                                       QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                                       3, NULL, NULL, NULL);
            g_object_unref (ctx->qmi_client);
        }
        g_object_unref (ctx->qmi_device);
    }
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
#endif
    g_object_unref (ctx->device);
    g_slice_free (PowerUpContext, ctx);
}

static gboolean
power_up_finish (MMIfaceModem *self,
                 GAsyncResult *res,
                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void power_up_context_step (GTask *task);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
release_qmi_client_dms_ready (QmiDevice *dev,
                              GAsyncResult *res,
                              GTask *task)
{
    PowerUpContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    /* Non-fatal error */
    if (!qmi_device_release_client_finish (dev, res, &error)) {
        mm_dbg ("error: couldn't release client: %s", error->message);
        g_error_free (error);
    }

    ctx->step++;
    power_up_context_step (task);
}

static void
set_radio_state_release_qmi_client_dms (GTask *task)
{
    PowerUpContext *ctx;

    ctx = g_task_get_task_data (task);
    qmi_device_release_client (ctx->qmi_device,
                               ctx->qmi_client,
                               QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                               10,
                               NULL,
                               (GAsyncReadyCallback)release_qmi_client_dms_ready,
                               task);
}

static void
set_fcc_authentication_ready (QmiClientDms *client,
                              GAsyncResult *res,
                              GTask *task)
{
    PowerUpContext *ctx;
    QmiMessageDmsSetFccAuthenticationOutput *output;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    output = qmi_client_dms_set_fcc_authentication_finish (client, res, &error);
    if (!output || !qmi_message_dms_set_fcc_authentication_output_get_result (output, &error)) {
        mm_dbg ("error: couldn't set FCC auth: %s", error->message);
        g_error_free (error);
        g_assert (ctx->saved_error);
        g_task_return_error (task, ctx->saved_error);
        ctx->saved_error = NULL;
        g_object_unref (task);
        goto out;
    }

    ctx->step++;
    power_up_context_step (task);

out:
    if (output)
        qmi_message_dms_set_fcc_authentication_output_unref (output);
}

static void
set_radio_state_fcc_auth (GTask *task)
{
    PowerUpContext *ctx;

    ctx = g_task_get_task_data (task);
    qmi_client_dms_set_fcc_authentication (QMI_CLIENT_DMS (ctx->qmi_client),
                                           NULL,
                                           10,
                                           NULL, /* cancellable */
                                           (GAsyncReadyCallback)set_fcc_authentication_ready,
                                           task);
}

static void
qmi_client_dms_ready (QmiDevice *dev,
                      GAsyncResult *res,
                      GTask *task)
{
    PowerUpContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    ctx->qmi_client = qmi_device_allocate_client_finish (dev, res, &error);
    if (!ctx->qmi_client) {
        mm_dbg ("error: couldn't create DMS client: %s", error->message);
        g_error_free (error);
        g_assert (ctx->saved_error);
        g_task_return_error (task, ctx->saved_error);
        ctx->saved_error = NULL;
        g_object_unref (task);
        return;
    }

    ctx->step++;
    power_up_context_step (task);
}

static void
set_radio_state_allocate_qmi_client_dms (GTask *task)
{
    PowerUpContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (ctx->qmi_device);
    qmi_device_allocate_client (ctx->qmi_device,
                                QMI_SERVICE_DMS,
                                QMI_CID_NONE,
                                10,
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) qmi_client_dms_ready,
                                task);
}

static void
device_open_ready (QmiDevice *dev,
                   GAsyncResult *res,
                   GTask *task)
{
    PowerUpContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    if (!qmi_device_open_finish (dev, res, &error)) {
        mm_dbg ("error: couldn't open QmiDevice: %s", error->message);
        g_error_free (error);
        g_assert (ctx->saved_error);
        g_task_return_error (task, ctx->saved_error);
        ctx->saved_error = NULL;
        g_object_unref (task);
        return;
    }

    ctx->step++;
    power_up_context_step (task);
}

static void
set_radio_state_qmi_device_open (GTask *task)
{
    PowerUpContext *ctx;

    /* Open the device */
    ctx = g_task_get_task_data (task);
    g_assert (ctx->qmi_device);
    qmi_device_open (ctx->qmi_device,
                     (QMI_DEVICE_OPEN_FLAGS_PROXY | QMI_DEVICE_OPEN_FLAGS_MBIM),
                     15,
                     NULL, /* cancellable */
                     (GAsyncReadyCallback)device_open_ready,
                     task);
}

static void
qmi_device_new_ready (GObject *unused,
                      GAsyncResult *res,
                      GTask *task)
{
    PowerUpContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);
    ctx->qmi_device = qmi_device_new_finish (res, &error);
    if (!ctx->qmi_device) {
        mm_dbg ("error: couldn't create QmiDevice: %s", error->message);
        g_error_free (error);
        g_assert (ctx->saved_error);
        g_task_return_error (task, ctx->saved_error);
        ctx->saved_error = NULL;
        g_object_unref (task);
        return;
    }

    ctx->step++;
    power_up_context_step (task);
}

static void
set_radio_state_qmi_device_new (GTask *task)
{
    PowerUpContext *ctx;
    GFile *file;

    ctx = g_task_get_task_data (task);
    file = mbim_device_get_file (ctx->device);
    qmi_device_new (file,
                    NULL, /* cancellable */
                    (GAsyncReadyCallback) qmi_device_new_ready,
                    task);
    g_object_unref (file);
}

#endif

static void
radio_state_set_up_ready (MbimDevice *device,
                          GAsyncResult *res,
                          GTask *task)
{
    PowerUpContext *ctx;
    MbimMessage *response;
    GError *error = NULL;
    MbimRadioSwitchState hardware_radio_state;
    MbimRadioSwitchState software_radio_state;

    ctx = g_task_get_task_data (task);
    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_radio_state_response_parse (
            response,
            &hardware_radio_state,
            &software_radio_state,
            &error)) {
        if (hardware_radio_state == MBIM_RADIO_SWITCH_STATE_OFF)
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot power-up: hardware radio switch is OFF");
        else if (software_radio_state == MBIM_RADIO_SWITCH_STATE_OFF)
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot power-up: sotware radio switch is OFF");
    }

    if (response)
        mbim_message_unref (response);

    /* Nice! we're done, quick exit */
    if (!error) {
        ctx->step = POWER_UP_CONTEXT_STEP_LAST;
        power_up_context_step (task);
        return;
    }

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    /* Only the first attempt isn't fatal */
    if (ctx->step == POWER_UP_CONTEXT_STEP_FIRST) {
        /* Warn and keep, will retry */
        mm_warn ("%s", error->message);
        g_assert (!ctx->saved_error);
        ctx->saved_error = error;
        ctx->step++;
        power_up_context_step (task);
        return;
    }
#endif

    /* Fatal */
    g_task_return_error (task, error);
    g_object_unref (task);
}

static void
set_radio_state_up (GTask *task)
{
    PowerUpContext *ctx;
    MbimMessage *message;

    ctx = g_task_get_task_data (task);
    message = mbim_message_radio_state_set_new (MBIM_RADIO_SWITCH_STATE_ON, NULL);
    mbim_device_command (ctx->device,
                         message,
                         20,
                         NULL,
                         (GAsyncReadyCallback)radio_state_set_up_ready,
                         task);
    mbim_message_unref (message);
}

static void
power_up_context_step (GTask *task)
{
    PowerUpContext *ctx;

    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case POWER_UP_CONTEXT_STEP_FIRST:
        set_radio_state_up (task);
        return;

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

    case POWER_UP_CONTEXT_STEP_QMI_DEVICE_NEW:
        set_radio_state_qmi_device_new (task);
        return;

    case POWER_UP_CONTEXT_STEP_QMI_DEVICE_OPEN:
        set_radio_state_qmi_device_open (task);
        return;

    case POWER_UP_CONTEXT_STEP_ALLOCATE_QMI_CLIENT_DMS:
        set_radio_state_allocate_qmi_client_dms (task);
        return;

    case POWER_UP_CONTEXT_STEP_FCC_AUTH:
        set_radio_state_fcc_auth (task);
        return;

    case POWER_UP_CONTEXT_STEP_RELEASE_QMI_CLIENT_DMS:
        set_radio_state_release_qmi_client_dms (task);
        return;

    case POWER_UP_CONTEXT_STEP_RETRY:
        set_radio_state_up (task);
        return;

#endif

    case POWER_UP_CONTEXT_STEP_LAST:
        /* Good! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
modem_power_up (MMIfaceModem *self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    PowerUpContext *ctx;
    MbimDevice *device;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    ctx = g_slice_new0 (PowerUpContext);
    ctx->device = g_object_ref (device);
    ctx->step = POWER_UP_CONTEXT_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)power_up_context_free);

    power_up_context_step (task);
}

/*****************************************************************************/
/* Power down (Modem interface) */

static gboolean
power_down_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
radio_state_set_down_ready (MbimDevice *device,
                            GAsyncResult *res,
                            GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);
        mbim_message_unref (response);
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_power_down (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_radio_state_set_new (MBIM_RADIO_SWITCH_STATE_OFF, NULL);
    mbim_device_command (device,
                         message,
                         20,
                         NULL,
                         (GAsyncReadyCallback)radio_state_set_down_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

typedef struct {
    guint32 session_id;
    gboolean found;
} FindSessionId;

static void
bearer_list_session_id_foreach (MMBaseBearer *bearer,
                                gpointer user_data)
{
    FindSessionId *ctx = user_data;

    if (!ctx->found &&
        MM_IS_BEARER_MBIM (bearer) &&
        mm_bearer_mbim_get_session_id (MM_BEARER_MBIM (bearer)) == ctx->session_id)
        ctx->found = TRUE;
}

static gint
find_next_bearer_session_id (MMBroadbandModemMbim *self)
{
    MMBearerList *bearer_list;
    guint i;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);

    if (!bearer_list)
        return 0;

    for (i = 0; i <= 255; i++) {
        FindSessionId ctx;

        ctx.session_id = i;
        ctx.found = FALSE;

        mm_bearer_list_foreach (bearer_list,
                                bearer_list_session_id_foreach,
                                &ctx);

        if (!ctx.found) {
            g_object_unref (bearer_list);
            return (gint)i;
        }
    }

    /* no valid session id found */
    g_object_unref (bearer_list);
    return -1;
}

static void
modem_create_bearer (MMIfaceModem *_self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    MMBaseBearer *bearer;
    GTask *task;
    gint session_id;

    task = g_task_new (self, NULL, callback, user_data);

    /* Find a new session ID */
    session_id = find_next_bearer_session_id (self);
    if (session_id < 0) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Not enough session IDs");
        g_object_unref (task);
        return;
    }

    /* We just create a MMBearerMbim */
    mm_dbg ("Creating MBIM bearer in MBIM modem");
    bearer = mm_bearer_mbim_new (self,
                                 properties,
                                 (guint)session_id);
    g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

/*****************************************************************************/
/* Create SIM (Modem interface) */

static MMBaseSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_mbim_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New MBIM SIM */
    mm_sim_mbim_new (MM_BASE_MODEM (self),
                    NULL, /* cancellable */
                    callback,
                    user_data);
}

/*****************************************************************************/
/* First enabling step */

static gboolean
enabling_started_finish (MMBroadbandModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parent_enabling_started_ready (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GTask *task)
{
    GError *error = NULL;

    if (!MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->enabling_started_finish (
            self,
            res,
            &error)) {
        /* Don't treat this as fatal. Parent enabling may fail if it cannot grab a primary
         * AT port, which isn't really an issue in MBIM-based modems */
        mm_dbg ("Couldn't start parent enabling: %s", error->message);
        g_error_free (error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
enabling_started (MMBroadbandModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->enabling_started (
        self,
        (GAsyncReadyCallback)parent_enabling_started_ready,
        task);
}

/*****************************************************************************/
/* First initialization step */

typedef struct {
    MMPortMbim *mbim;
} InitializationStartedContext;

static void
initialization_started_context_free (InitializationStartedContext *ctx)
{
    if (ctx->mbim)
        g_object_unref (ctx->mbim);
    g_slice_free (InitializationStartedContext, ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_initialization_started_ready (MMBroadbandModem *self,
                                     GAsyncResult *res,
                                     GTask *task)
{
    gpointer parent_ctx;
    GError *error = NULL;

    parent_ctx = MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->initialization_started_finish (
        self,
        res,
        &error);
    if (error) {
        /* Don't treat this as fatal. Parent initialization may fail if it cannot grab a primary
         * AT port, which isn't really an issue in MBIM-based modems */
        mm_dbg ("Couldn't start parent initialization: %s", error->message);
        g_error_free (error);
    }

    /* Just parent's pointer passed here */
    g_task_return_pointer (task, parent_ctx, NULL);
    g_object_unref (task);
}

static void
parent_initialization_started (GTask *task)
{
    MMBroadbandModem *self;

    self = g_task_get_source_object (task);
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_parent_class)->initialization_started (
        self,
        (GAsyncReadyCallback)parent_initialization_started_ready,
        task);
}

static void
mbim_device_removed_cb (MbimDevice *device,
                        MMBroadbandModemMbim *self)
{
    /* We have to do a full re-probe here because simply reopening the device
     * and restarting mbim-proxy will leave us without MBIM notifications. */
    mm_info ("Connection to mbim-proxy for %s lost, reprobing",
             mbim_device_get_path_display (device));

    g_signal_handler_disconnect (device,
                                 self->priv->mbim_device_removed_id);
    self->priv->mbim_device_removed_id = 0;

    mm_base_modem_set_reprobe (MM_BASE_MODEM (self), TRUE);
    mm_base_modem_set_valid (MM_BASE_MODEM (self), FALSE);
}

static void
track_mbim_device_removed (MMBroadbandModemMbim *self,
                           MMPortMbim *mbim)
{
    MbimDevice *device;

    device = mm_port_mbim_peek_device (mbim);
    g_assert (device);

    /* Register removal handler so we can handle mbim-proxy crashes */
    self->priv->mbim_device_removed_id = g_signal_connect (
        device,
        MBIM_DEVICE_SIGNAL_REMOVED,
        G_CALLBACK (mbim_device_removed_cb),
        self);
}

static void
untrack_mbim_device_removed (MMBroadbandModemMbim *self,
                             MMPortMbim *mbim)
{
    MbimDevice *device;

    if (self->priv->mbim_device_removed_id == 0)
        return;

    device = mm_port_mbim_peek_device (mbim);
    if (!device)
        return;

    g_signal_handler_disconnect (device, self->priv->mbim_device_removed_id);
    self->priv->mbim_device_removed_id = 0;
}

static void
mbim_port_open_ready (MMPortMbim *mbim,
                      GAsyncResult *res,
                      GTask *task)
{
    MMBroadbandModemMbim *self;
    GError *error = NULL;

    if (!mm_port_mbim_open_finish (mbim, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Make sure we know if mbim-proxy dies on us, and then do the parent's
     * initialization */
    self = MM_BROADBAND_MODEM_MBIM (g_task_get_source_object (task));
    track_mbim_device_removed (self, mbim);
    parent_initialization_started (task);
}

static void
initialization_started (MMBroadbandModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    InitializationStartedContext *ctx;
    GTask *task;

    ctx = g_slice_new0 (InitializationStartedContext);
    ctx->mbim = mm_base_modem_get_port_mbim (MM_BASE_MODEM (self));

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_started_context_free);

    /* This may happen if we unplug the modem unexpectedly */
    if (!ctx->mbim) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot initialize: MBIM port went missing");
        g_object_unref (task);
        return;
    }

    if (mm_port_mbim_is_open (ctx->mbim)) {
        /* Nothing to be done, just connect to a signal and launch parent's
         * callback */
        track_mbim_device_removed (MM_BROADBAND_MODEM_MBIM (self), ctx->mbim);
        parent_initialization_started (task);
        return;
    }

    /* Now open our MBIM port */
    mm_port_mbim_open (ctx->mbim,
                       NULL,
                       (GAsyncReadyCallback)mbim_port_open_ready,
                       task);
}

/*****************************************************************************/
/* IMEI loading (3GPP interface) */

static gchar *
modem_3gpp_load_imei_finish (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_3gpp_load_imei (MMIfaceModem3gpp *_self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->caps_device_id)
        g_task_return_pointer (task,
                               g_strdup (self->priv->caps_device_id),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Device doesn't report a valid IMEI");
    g_object_unref (task);
}

/*****************************************************************************/
/* Facility locks status loading (3GPP interface) */

static MMModem3gppFacility
modem_3gpp_load_enabled_facility_locks_finish (MMIfaceModem3gpp *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_3GPP_FACILITY_NONE;
    }
    return (MMModem3gppFacility)value;
}

static void
pin_list_query_ready (MbimDevice *device,
                      GAsyncResult *res,
                      GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimPinDesc *pin_desc_pin1;
    MbimPinDesc *pin_desc_pin2;
    MbimPinDesc *pin_desc_device_sim_pin;
    MbimPinDesc *pin_desc_device_first_sim_pin;
    MbimPinDesc *pin_desc_network_pin;
    MbimPinDesc *pin_desc_network_subset_pin;
    MbimPinDesc *pin_desc_service_provider_pin;
    MbimPinDesc *pin_desc_corporate_pin;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_pin_list_response_parse (
            response,
            &pin_desc_pin1,
            &pin_desc_pin2,
            &pin_desc_device_sim_pin,
            &pin_desc_device_first_sim_pin,
            &pin_desc_network_pin,
            &pin_desc_network_subset_pin,
            &pin_desc_service_provider_pin,
            &pin_desc_corporate_pin,
            NULL, /* pin_desc_subsidy_lock */
            NULL, /* pin_desc_custom */
            &error)) {
        MMModem3gppFacility mask = MM_MODEM_3GPP_FACILITY_NONE;

        if (pin_desc_pin1->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_SIM;
        mbim_pin_desc_free (pin_desc_pin1);

        if (pin_desc_pin2->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_FIXED_DIALING;
        mbim_pin_desc_free (pin_desc_pin2);

        if (pin_desc_device_sim_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_PH_SIM;
        mbim_pin_desc_free (pin_desc_device_sim_pin);

        if (pin_desc_device_first_sim_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_PH_FSIM;
        mbim_pin_desc_free (pin_desc_device_first_sim_pin);

        if (pin_desc_network_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_NET_PERS;
        mbim_pin_desc_free (pin_desc_network_pin);

        if (pin_desc_network_subset_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_NET_SUB_PERS;
        mbim_pin_desc_free (pin_desc_network_subset_pin);

        if (pin_desc_service_provider_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_PROVIDER_PERS;
        mbim_pin_desc_free (pin_desc_service_provider_pin);

        if (pin_desc_corporate_pin->pin_mode == MBIM_PIN_MODE_ENABLED)
            mask |= MM_MODEM_3GPP_FACILITY_CORP_PERS;
        mbim_pin_desc_free (pin_desc_corporate_pin);

        g_task_return_int (task, mask);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_3gpp_load_enabled_facility_locks (MMIfaceModem3gpp *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_pin_list_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)pin_list_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Common unsolicited events setup and cleanup */

static void
basic_connect_notification_signal_state (MMBroadbandModemMbim *self,
                                         MbimMessage *notification)
{
    guint32 rssi;

    if (mbim_message_signal_state_notification_parse (
            notification,
            &rssi,
            NULL, /* error_rate */
            NULL, /* signal_strength_interval */
            NULL, /* rssi_threshold */
            NULL, /* error_rate_threshold */
            NULL)) {
        guint32 quality;

        /* Normalize the quality. 99 means unknown, we default it to 0 */
        quality = CLAMP (rssi == 99 ? 0 : rssi, 0, 31) * 100 / 31;

        mm_dbg ("Signal state indication: %u --> %u%%", rssi, quality);
        mm_iface_modem_update_signal_quality (MM_IFACE_MODEM (self), quality);
    }
}

static void
update_access_technologies (MMBroadbandModemMbim *self)
{
    MMModemAccessTechnology act;

    act = mm_modem_access_technology_from_mbim_data_class (self->priv->highest_available_data_class);
    if (act == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
        act = mm_modem_access_technology_from_mbim_data_class (self->priv->available_data_classes);

    mm_iface_modem_3gpp_update_access_technologies (MM_IFACE_MODEM_3GPP (self), act);
}

static void
update_registration_info (MMBroadbandModemMbim *self,
                          MbimRegisterState state,
                          MbimDataClass available_data_classes,
                          gchar *operator_id_take,
                          gchar *operator_name_take)
{
    MMModem3gppRegistrationState reg_state;

    reg_state = mm_modem_3gpp_registration_state_from_mbim_register_state (state);

    if (reg_state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        reg_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        if (self->priv->current_operator_id && operator_id_take &&
            g_str_equal (self->priv->current_operator_id, operator_id_take)) {
            g_free (operator_id_take);
        } else {
            g_free (self->priv->current_operator_id);
            self->priv->current_operator_id = operator_id_take;
        }

        if (self->priv->current_operator_name && operator_name_take &&
            g_str_equal (self->priv->current_operator_name, operator_name_take)) {
            g_free (operator_name_take);
        } else {
            g_free (self->priv->current_operator_name);
            self->priv->current_operator_name = operator_name_take;
        }
    } else {
        if (self->priv->current_operator_id) {
            g_free (self->priv->current_operator_id);
            self->priv->current_operator_id = NULL;
        }
        if (self->priv->current_operator_name) {
            g_free (self->priv->current_operator_name);
            self->priv->current_operator_name = NULL;
        }
        g_free (operator_id_take);
        g_free (operator_name_take);
    }

    mm_iface_modem_3gpp_update_ps_registration_state (
        MM_IFACE_MODEM_3GPP (self),
        reg_state);

    self->priv->available_data_classes = available_data_classes;
    update_access_technologies (self);
}

static void
basic_connect_notification_register_state (MMBroadbandModemMbim *self,
                                           MbimMessage *notification)
{
    MbimRegisterState register_state;
    MbimDataClass available_data_classes;
    gchar *provider_id;
    gchar *provider_name;

    if (mbim_message_register_state_notification_parse (
            notification,
            NULL, /* nw_error */
            &register_state,
            NULL, /* register_mode */
            &available_data_classes,
            NULL, /* current_cellular_class */
            &provider_id,
            &provider_name,
            NULL, /* roaming_text */
            NULL, /* registration_flag */
            NULL)) {
        update_registration_info (self,
                                  register_state,
                                  available_data_classes,
                                  provider_id,
                                  provider_name);
    }
}

typedef struct {
    guint32 session_id;
} ReportDisconnectedStatusContext;

static void
bearer_list_report_disconnected_status (MMBaseBearer *bearer,
                                        gpointer user_data)
{
    ReportDisconnectedStatusContext *ctx = user_data;

    if (MM_IS_BEARER_MBIM (bearer) &&
        mm_bearer_mbim_get_session_id (MM_BEARER_MBIM (bearer)) == ctx->session_id) {
        mm_dbg ("Bearer '%s' was disconnected.", mm_base_bearer_get_path (bearer));
        mm_base_bearer_report_connection_status (bearer, MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
    }
}

static void
basic_connect_notification_connect (MMBroadbandModemMbim *self,
                                    MbimMessage *notification)
{
    guint32 session_id;
    MbimActivationState activation_state;
    const MbimUuid *context_type;
    MMBearerList *bearer_list;

    if (!mbim_message_connect_notification_parse (
            notification,
            &session_id,
            &activation_state,
            NULL, /* voice_call_state */
            NULL, /* ip_type */
            &context_type,
            NULL, /* nw_error */
            NULL)) {
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);

    if (!bearer_list)
        return;

    if (mbim_uuid_to_context_type (context_type) == MBIM_CONTEXT_TYPE_INTERNET &&
        activation_state == MBIM_ACTIVATION_STATE_DEACTIVATED) {
      ReportDisconnectedStatusContext ctx;

      mm_dbg ("Session ID '%u' was deactivated.", session_id);
      ctx.session_id = session_id;
      mm_bearer_list_foreach (bearer_list,
                              (MMBearerListForeachFunc)bearer_list_report_disconnected_status,
                              &ctx);
    }

    g_object_unref (bearer_list);
}

static void
basic_connect_notification_subscriber_ready_status (MMBroadbandModemMbim *self,
                                                    MbimMessage *notification)
{
    MbimSubscriberReadyState ready_state;
    gchar **telephone_numbers;

    if (!mbim_message_subscriber_ready_status_notification_parse (
            notification,
            &ready_state,
            NULL, /* subscriber_id */
            NULL, /* sim_iccid */
            NULL, /* ready_info */
            NULL, /* telephone_numbers_count */
            &telephone_numbers,
            NULL)) {
        return;
    }

    if (ready_state == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED)
        mm_iface_modem_update_own_numbers (MM_IFACE_MODEM (self), telephone_numbers);

    if ((self->priv->last_ready_state != MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED &&
         ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) ||
        (self->priv->last_ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED &&
               ready_state != MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED)) {
        /* SIM has been removed or reinserted, re-probe to ensure correct interfaces are exposed */
        mm_dbg ("SIM hot swap detected");
        mm_broadband_modem_update_sim_hot_swap_detected (MM_BROADBAND_MODEM (self));
    }

    self->priv->last_ready_state = ready_state;
    g_strfreev (telephone_numbers);
}

static void
basic_connect_notification_packet_service (MMBroadbandModemMbim *self,
                                           MbimMessage *notification)
{
    MbimPacketServiceState packet_service_state;
    MbimDataClass highest_available_data_class;
    gchar *str;

    if (!mbim_message_packet_service_notification_parse (
            notification,
            NULL, /* nw_error */
            &packet_service_state,
            &highest_available_data_class,
            NULL, /* uplink_speed */
            NULL, /* downlink_speed */
            NULL)) {
        return;
    }

    str = mbim_data_class_build_string_from_mask (highest_available_data_class);
    mm_dbg ("Packet service state: '%s', data class: '%s'",
            mbim_packet_service_state_get_string (packet_service_state), str);
    g_free (str);

    if (packet_service_state == MBIM_PACKET_SERVICE_STATE_ATTACHED) {
      self->priv->highest_available_data_class = highest_available_data_class;
    } else if (packet_service_state == MBIM_PACKET_SERVICE_STATE_DETACHED) {
      self->priv->highest_available_data_class = 0;
    }

    update_access_technologies (self);
}

static void add_sms_part (MMBroadbandModemMbim *self,
                          const MbimSmsPduReadRecord *pdu);

static void
sms_notification_read_sms (MMBroadbandModemMbim *self,
                           MbimMessage *notification)
{
    MbimSmsFormat format;
    guint32 messages_count;
    MbimSmsPduReadRecord **pdu_messages;
    guint i;

    if (!mbim_message_sms_read_notification_parse (
            notification,
            &format,
            &messages_count,
            &pdu_messages,
            NULL, /* cdma_messages */
            NULL) ||
        /* Only PDUs */
        format != MBIM_SMS_FORMAT_PDU) {
        return;
    }

    for (i = 0; i < messages_count; i++)
        add_sms_part (self, pdu_messages[i]);

    mbim_sms_pdu_read_record_array_free (pdu_messages);
}

static void
basic_connect_notification (MMBroadbandModemMbim *self,
                            MbimMessage *notification)
{
    switch (mbim_message_indicate_status_get_cid (notification)) {
    case MBIM_CID_BASIC_CONNECT_SIGNAL_STATE:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY)
            basic_connect_notification_signal_state (self, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_REGISTER_STATE:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES)
            basic_connect_notification_register_state (self, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_CONNECT:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_CONNECT)
            basic_connect_notification_connect (self, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO)
            basic_connect_notification_subscriber_ready_status (self, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_PACKET_SERVICE:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE)
            basic_connect_notification_packet_service (self, notification);
        break;
    default:
        /* Ignore */
        break;
    }
}

static void
alert_sms_read_query_ready (MbimDevice *device,
                            GAsyncResult *res,
                            MMBroadbandModemMbim *self)
{
    MbimMessage *response;
    GError *error = NULL;
    guint32 messages_count;
    MbimSmsPduReadRecord **pdu_messages;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_sms_read_response_parse (
            response,
            NULL,
            &messages_count,
            &pdu_messages,
            NULL, /* cdma_messages */
            &error)) {
        guint i;

        for (i = 0; i < messages_count; i++)
            add_sms_part (self, pdu_messages[i]);
        mbim_sms_pdu_read_record_array_free (pdu_messages);
    }

    if (error) {
        mm_dbg ("Flash message reading failed: %s", error->message);
        g_error_free (error);
    }

    if (response)
        mbim_message_unref (response);

    g_object_unref (self);
}

static void
sms_notification_read_alert_sms (MMBroadbandModemMbim *self,
                                 guint32 index)
{
    MMPortMbim *port;
    MbimDevice *device;
    MbimMessage *message;

    port = mm_base_modem_peek_port_mbim (MM_BASE_MODEM (self));
    if (!port)
        return;
    device = mm_port_mbim_peek_device (port);
    if (!device)
        return;

    mm_dbg ("Reading flash SMS at index '%u'", index);
    message = mbim_message_sms_read_query_new (MBIM_SMS_FORMAT_PDU,
                                               MBIM_SMS_FLAG_INDEX,
                                               index,
                                               NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)alert_sms_read_query_ready,
                         g_object_ref (self));
    mbim_message_unref (message);
}

static void
sms_notification (MMBroadbandModemMbim *self,
                  MbimMessage *notification)
{
    switch (mbim_message_indicate_status_get_cid (notification)) {
    case MBIM_CID_SMS_READ:
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ)
            sms_notification_read_sms (self, notification);
        break;

    case MBIM_CID_SMS_MESSAGE_STORE_STATUS: {
        MbimSmsStatusFlag flag;
        guint32 index;

        /* New flash/alert message? */
        if (self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ &&
            mbim_message_sms_message_store_status_notification_parse (
                notification,
                &flag,
                &index,
                NULL)) {
            mm_dbg ("Received flash message: '%s'", mbim_sms_status_flag_get_string (flag));
            if (flag == MBIM_SMS_STATUS_FLAG_NEW_MESSAGE)
                sms_notification_read_alert_sms (self, index);
        }
        break;
    }

    default:
        /* Ignore */
        break;
    }
}

static void
device_notification_cb (MbimDevice *device,
                        MbimMessage *notification,
                        MMBroadbandModemMbim *self)
{
    MbimService service;

    service = mbim_message_indicate_status_get_service (notification);
    mm_dbg ("Received notification (service '%s', command '%s')",
            mbim_service_get_string (service),
            mbim_cid_get_printable (service,
                                    mbim_message_indicate_status_get_cid (notification)));

    switch (service) {
    case MBIM_SERVICE_BASIC_CONNECT:
        basic_connect_notification (self, notification);
        break;
    case MBIM_SERVICE_SMS:
        sms_notification (self, notification);
        break;
    default:
        /* Ignore */
        break;
    }
}

static void
common_setup_cleanup_unsolicited_events_sync (MMBroadbandModemMbim *self,
                                              MbimDevice           *device,
                                              gboolean              setup)
{
    if (!device)
        return;

    mm_dbg ("Supported notifications: signal (%s), registration (%s), sms (%s), connect (%s), subscriber (%s), packet (%s)",
            self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY ? "yes" : "no",
            self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES ? "yes" : "no",
            self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ ? "yes" : "no",
            self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_CONNECT ? "yes" : "no",
            self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO ? "yes" : "no",
            self->priv->setup_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE ? "yes" : "no");

    if (setup) {
        /* Don't re-enable it if already there */
        if (!self->priv->notification_id)
            self->priv->notification_id =
                g_signal_connect (device,
                                  MBIM_DEVICE_SIGNAL_INDICATE_STATUS,
                                  G_CALLBACK (device_notification_cb),
                                  self);
    } else {
        /* Don't remove the signal if there are still listeners interested */
        if (self->priv->setup_flags == PROCESS_NOTIFICATION_FLAG_NONE &&
            self->priv->notification_id &&
            g_signal_handler_is_connected (device, self->priv->notification_id)) {
            g_signal_handler_disconnect (device, self->priv->notification_id);
            self->priv->notification_id = 0;
        }
    }
}

static gboolean
common_setup_cleanup_unsolicited_events_finish (MMBroadbandModemMbim  *self,
                                                GAsyncResult          *res,
                                                GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
common_setup_cleanup_unsolicited_events (MMBroadbandModemMbim *self,
                                         gboolean              setup,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data)
{
    GTask      *task;
    MbimDevice *device;

    if (!peek_device (self, &device, callback, user_data))
        return;

    common_setup_cleanup_unsolicited_events_sync (self, device, setup);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Setup/cleanup unsolicited events (3GPP interface) */

static gboolean
common_setup_cleanup_unsolicited_events_3gpp_finish (MMIfaceModem3gpp *self,
                                                     GAsyncResult *res,
                                                     GError **error)
{
    return common_setup_cleanup_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
cleanup_unsolicited_events_3gpp (MMIfaceModem3gpp *_self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    gboolean is_sim_hot_swap_configured = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, &is_sim_hot_swap_configured,
                  NULL);

    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY;
    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_CONNECT;
    if (is_sim_hot_swap_configured)
        self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE;
    common_setup_cleanup_unsolicited_events (self, FALSE, callback, user_data);
}

static void
setup_unsolicited_events_3gpp (MMIfaceModem3gpp *_self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY;
    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_CONNECT;
    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE;
    common_setup_cleanup_unsolicited_events (self, TRUE, callback, user_data);
}

/*****************************************************************************/
/* Cleanup/Setup unsolicited registration events */

static void
cleanup_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES;
    common_setup_cleanup_unsolicited_events (self, FALSE, callback, user_data);
}

static void
setup_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES;
    common_setup_cleanup_unsolicited_events (self, TRUE, callback, user_data);
}

/*****************************************************************************/
/* Enable/disable unsolicited events (common) */

static gboolean
common_enable_disable_unsolicited_events_finish (MMBroadbandModemMbim *self,
                                                 GAsyncResult *res,
                                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
subscribe_list_set_ready_cb (MbimDevice *device,
                             GAsyncResult *res,
                             GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response) {
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);
        mbim_message_unref (response);
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable_unsolicited_events (MMBroadbandModemMbim *self,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
    MbimMessage *request;
    MbimDevice *device;
    MbimEventEntry **entries;
    guint n_entries = 0;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    mm_dbg ("Enabled notifications: signal (%s), registration (%s), sms (%s), connect (%s), subscriber (%s), packet (%s)",
            self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY ? "yes" : "no",
            self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES ? "yes" : "no",
            self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ ? "yes" : "no",
            self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_CONNECT ? "yes" : "no",
            self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO ? "yes" : "no",
            self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE ? "yes" : "no");

    entries = g_new0 (MbimEventEntry *, 3);

    /* Basic connect service */
    if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_CONNECT ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO ||
        self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE) {
        entries[n_entries] = g_new (MbimEventEntry, 1);
        memcpy (&(entries[n_entries]->device_service_id), MBIM_UUID_BASIC_CONNECT, sizeof (MbimUuid));
        entries[n_entries]->cids_count = 0;
        entries[n_entries]->cids = g_new0 (guint32, 5);
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_SIGNAL_STATE;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_REGISTER_STATE;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_CONNECT)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_CONNECT;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS;
        if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE)
            entries[n_entries]->cids[entries[n_entries]->cids_count++] = MBIM_CID_BASIC_CONNECT_PACKET_SERVICE;
        n_entries++;
    }

    /* SMS service */
    if (self->priv->enable_flags & PROCESS_NOTIFICATION_FLAG_SMS_READ) {
        entries[n_entries] = g_new (MbimEventEntry, 1);
        memcpy (&(entries[n_entries]->device_service_id), MBIM_UUID_SMS, sizeof (MbimUuid));
        entries[n_entries]->cids_count = 2;
        entries[n_entries]->cids = g_new0 (guint32, 2);
        entries[n_entries]->cids[0] = MBIM_CID_SMS_READ;
        entries[n_entries]->cids[1] = MBIM_CID_SMS_MESSAGE_STORE_STATUS;
        n_entries++;
    }

    task = g_task_new (self, NULL, callback, user_data);

    request = (mbim_message_device_service_subscribe_list_set_new (
                   n_entries,
                   (const MbimEventEntry *const *)entries,
                   NULL));
    mbim_device_command (device,
                         request,
                         10,
                         NULL,
                         (GAsyncReadyCallback)subscribe_list_set_ready_cb,
                         task);
    mbim_message_unref (request);
    mbim_event_entry_array_free (entries);
}

/*****************************************************************************/
/* Enable/Disable unsolicited registration events */

static gboolean
modem_3gpp_common_enable_disable_unsolicited_registration_events_finish (MMIfaceModem3gpp *self,
                                                                         GAsyncResult *res,
                                                                         GError **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
modem_3gpp_disable_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                                    gboolean cs_supported,
                                                    gboolean ps_supported,
                                                    gboolean eps_supported,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}


static void
modem_3gpp_enable_unsolicited_registration_events (MMIfaceModem3gpp *_self,
                                                   gboolean cs_supported,
                                                   gboolean ps_supported,
                                                   gboolean eps_supported,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_REGISTRATION_UPDATES;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

/*****************************************************************************/
/* Setup SIM hot swap */

static gboolean
modem_setup_sim_hot_swap_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enable_subscriber_info_unsolicited_events_ready (MMBroadbandModemMbim *self,
                                                 GAsyncResult *res,
                                                 GTask *task)
{
    GError *error = NULL;

    if (!common_enable_disable_unsolicited_events_finish (self, res, &error)) {
        mm_dbg ("Failed to enable subscriber info events: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
setup_subscriber_info_unsolicited_events_ready (MMBroadbandModemMbim *self,
                                                GAsyncResult *res,
                                                GTask *task)
{
    GError *error = NULL;

    if (!common_setup_cleanup_unsolicited_events_finish (self, res, &error)) {
        mm_dbg ("Failed to set up subscriber info events: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    common_enable_disable_unsolicited_events (self,
                                              (GAsyncReadyCallback)enable_subscriber_info_unsolicited_events_ready,
                                              task);
}

static void
modem_setup_sim_hot_swap (MMIfaceModem *_self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    common_setup_cleanup_unsolicited_events (self,
                                             TRUE,
                                             (GAsyncReadyCallback)setup_subscriber_info_unsolicited_events_ready,
                                             task);
}

/*****************************************************************************/
/* Enable/Disable unsolicited events (3GPP interface) */

static gboolean
modem_3gpp_common_enable_disable_unsolicited_events_finish (MMIfaceModem3gpp *self,
                                                            GAsyncResult *res,
                                                            GError **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
modem_3gpp_disable_unsolicited_events (MMIfaceModem3gpp *_self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    gboolean is_sim_hot_swap_configured = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, &is_sim_hot_swap_configured,
                  NULL);

    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY;
    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_CONNECT;
    if (is_sim_hot_swap_configured)
        self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

static void
modem_3gpp_enable_unsolicited_events (MMIfaceModem3gpp *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SIGNAL_QUALITY;
    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_CONNECT;
    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SUBSCRIBER_INFO;
    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_PACKET_SERVICE;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

/*****************************************************************************/
/* Load operator name (3GPP interface) */

static gchar *
modem_3gpp_load_operator_name_finish (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_3gpp_load_operator_name (MMIfaceModem3gpp *_self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->current_operator_name)
        g_task_return_pointer (task,
                               g_strdup (self->priv->current_operator_name),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Current operator name is still unknown");
    g_object_unref (task);
}

/*****************************************************************************/
/* Load operator code (3GPP interface) */

static gchar *
modem_3gpp_load_operator_code_finish (MMIfaceModem3gpp *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_3gpp_load_operator_code (MMIfaceModem3gpp *_self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->current_operator_id)
        g_task_return_pointer (task,
                               g_strdup (self->priv->current_operator_id),
                               g_free);
    else
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Current operator MCC/MNC is still unknown");
    g_object_unref (task);
}

/*****************************************************************************/
/* Registration checks (3GPP interface) */

static gboolean
modem_3gpp_run_registration_checks_finish (MMIfaceModem3gpp *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
register_state_query_ready (MbimDevice *device,
                            GAsyncResult *res,
                            GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimRegisterState register_state;
    MbimDataClass available_data_classes;
    gchar *provider_id;
    gchar *provider_name;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_register_state_response_parse (
            response,
            NULL, /* nw_error */
            &register_state,
            NULL, /* register_mode */
            &available_data_classes,
            NULL, /* current_cellular_class */
            &provider_id,
            &provider_name,
            NULL, /* roaming_text */
            NULL, /* registration_flag */
            NULL)) {
        MMBroadbandModemMbim *self;

        self = g_task_get_source_object (task);
        update_registration_info (self,
                                  register_state,
                                  available_data_classes,
                                  provider_id,
                                  provider_name);

        g_task_return_boolean (task, TRUE);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_3gpp_run_registration_checks (MMIfaceModem3gpp *self,
                                    gboolean cs_supported,
                                    gboolean ps_supported,
                                    gboolean eps_supported,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    message = mbim_message_register_state_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)register_state_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/

static gboolean
modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
register_state_set_ready (MbimDevice *device,
                          GAsyncResult *res,
                          GTask *task)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimNwError nw_error;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_register_state_response_parse (
            response,
            &nw_error,
            NULL, /* &register_state */
            NULL, /* register_mode */
            NULL, /* available_data_classes */
            NULL, /* current_cellular_class */
            NULL, /* provider_id */
            NULL, /* provider_name */
            NULL, /* roaming_text */
            NULL, /* registration_flag */
            NULL)) {
        if (nw_error)
            error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error);
    }

    if (response)
        mbim_message_unref (response);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
modem_3gpp_register_in_network (MMIfaceModem3gpp *self,
                                const gchar *operator_id,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    if (operator_id && operator_id[0])
        message = (mbim_message_register_state_set_new (
                       operator_id,
                       MBIM_REGISTER_ACTION_MANUAL,
                       0, /* data_class, none preferred */
                       NULL));
    else
        message = (mbim_message_register_state_set_new (
                       "",
                       MBIM_REGISTER_ACTION_AUTOMATIC,
                       0, /* data_class, none preferred */
                       NULL));
    mbim_device_command (device,
                         message,
                         60,
                         NULL,
                         (GAsyncReadyCallback)register_state_set_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Scan networks (3GPP interface) */

static GList *
modem_3gpp_scan_networks_finish (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
visible_providers_query_ready (MbimDevice *device,
                               GAsyncResult *res,
                               GTask *task)
{
    MbimMessage *response;
    MbimProvider **providers;
    guint n_providers;
    GError *error = NULL;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_visible_providers_response_parse (response,
                                                       &n_providers,
                                                       &providers,
                                                       &error)) {
        GList *info_list;

        info_list = mm_3gpp_network_info_list_from_mbim_providers ((const MbimProvider *const *)providers,
                                                                   n_providers);
        mbim_provider_array_free (providers);

        g_task_return_pointer (task, info_list, (GDestroyNotify)mm_3gpp_network_info_list_free);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
modem_3gpp_scan_networks (MMIfaceModem3gpp *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    mm_dbg ("scanning networks...");
    message = mbim_message_visible_providers_query_new (MBIM_VISIBLE_PROVIDERS_ACTION_FULL_SCAN, NULL);
    mbim_device_command (device,
                         message,
                         300,
                         NULL,
                         (GAsyncReadyCallback)visible_providers_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Check support (Messaging interface) */

static gboolean
messaging_check_support_finish (MMIfaceModemMessaging *self,
                                GAsyncResult *res,
                                GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
messaging_check_support (MMIfaceModemMessaging *_self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* We only handle 3GPP messaging (PDU based) currently */
    if (self->priv->caps_sms & MBIM_SMS_CAPS_PDU_RECEIVE &&
        self->priv->caps_sms & MBIM_SMS_CAPS_PDU_SEND) {
        mm_dbg ("Messaging capabilities supported");
        g_task_return_boolean (task, TRUE);
    } else {
        mm_dbg ("Messaging capabilities not supported by this modem");
        g_task_return_boolean (task, FALSE);
    }
    g_object_unref (task);
}

/*****************************************************************************/
/* Load supported storages (Messaging interface) */

static gboolean
messaging_load_supported_storages_finish (MMIfaceModemMessaging *self,
                                          GAsyncResult *res,
                                          GArray **mem1,
                                          GArray **mem2,
                                          GArray **mem3,
                                          GError **error)
{
    MMSmsStorage supported;

    *mem1 = g_array_sized_new (FALSE, FALSE, sizeof (MMSmsStorage), 2);
    supported = MM_SMS_STORAGE_MT;
    g_array_append_val (*mem1, supported);
    *mem2 = g_array_ref (*mem1);
    *mem3 = g_array_ref (*mem1);
    return TRUE;
}

static void
messaging_load_supported_storages (MMIfaceModemMessaging *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* Load initial SMS parts */

static gboolean
load_initial_sms_parts_finish (MMIfaceModemMessaging *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
add_sms_part (MMBroadbandModemMbim *self,
              const MbimSmsPduReadRecord *pdu)
{
    MMSmsPart *part;
    GError *error = NULL;

    part = mm_sms_part_3gpp_new_from_binary_pdu (pdu->message_index,
                                                 pdu->pdu_data,
                                                 pdu->pdu_data_size,
                                                 &error);
    if (part) {
        mm_dbg ("Correctly parsed PDU (%d)", pdu->message_index);
        mm_iface_modem_messaging_take_part (MM_IFACE_MODEM_MESSAGING (self),
                                            part,
                                            mm_sms_state_from_mbim_message_status (pdu->message_status),
                                            MM_SMS_STORAGE_MT);
    } else {
        /* Don't treat the error as critical */
        mm_dbg ("Error parsing PDU (%d): %s",
                pdu->message_index,
                error->message);
        g_error_free (error);
    }
}

static void
sms_read_query_ready (MbimDevice *device,
                      GAsyncResult *res,
                      GTask *task)
{
    MMBroadbandModemMbim *self;
    MbimMessage *response;
    GError *error = NULL;
    guint32 messages_count;
    MbimSmsPduReadRecord **pdu_messages;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_sms_read_response_parse (
            response,
            NULL,
            &messages_count,
            &pdu_messages,
            NULL, /* cdma_messages */
            &error)) {
        guint i;

        for (i = 0; i < messages_count; i++)
            add_sms_part (self, pdu_messages[i]);
        mbim_sms_pdu_read_record_array_free (pdu_messages);
        g_task_return_boolean (task, TRUE);
    } else
        g_task_return_error (task, error);

    g_object_unref (task);

    if (response)
        mbim_message_unref (response);
}

static void
load_initial_sms_parts (MMIfaceModemMessaging *self,
                        MMSmsStorage storage,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    MbimDevice *device;
    MbimMessage *message;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    g_assert (storage == MM_SMS_STORAGE_MT);

    task = g_task_new (self, NULL, callback, user_data);

    mm_dbg ("loading SMS parts...");
    message = mbim_message_sms_read_query_new (MBIM_SMS_FORMAT_PDU,
                                               MBIM_SMS_FLAG_ALL,
                                               0, /* message index, unused */
                                               NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)sms_read_query_ready,
                         task);
    mbim_message_unref (message);
}

/*****************************************************************************/
/* Setup/Cleanup unsolicited event handlers (Messaging interface) */

static gboolean
common_setup_cleanup_unsolicited_events_messaging_finish (MMIfaceModemMessaging *self,
                                                          GAsyncResult *res,
                                                          GError **error)
{
    return common_setup_cleanup_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
cleanup_unsolicited_events_messaging (MMIfaceModemMessaging *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags &= ~PROCESS_NOTIFICATION_FLAG_SMS_READ;
    common_setup_cleanup_unsolicited_events (self, FALSE, callback, user_data);
}

static void
setup_unsolicited_events_messaging (MMIfaceModemMessaging *_self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->setup_flags |= PROCESS_NOTIFICATION_FLAG_SMS_READ;
    common_setup_cleanup_unsolicited_events (self, TRUE, callback, user_data);
}

/*****************************************************************************/
/* Enable/Disable unsolicited event handlers (Messaging interface) */

static gboolean
common_enable_disable_unsolicited_events_messaging_finish (MMIfaceModemMessaging *self,
                                                           GAsyncResult *res,
                                                           GError **error)
{
    return common_enable_disable_unsolicited_events_finish (MM_BROADBAND_MODEM_MBIM (self), res, error);
}

static void
disable_unsolicited_events_messaging (MMIfaceModemMessaging *_self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags &= ~PROCESS_NOTIFICATION_FLAG_SMS_READ;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

static void
enable_unsolicited_events_messaging (MMIfaceModemMessaging *_self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (_self);

    self->priv->enable_flags |= PROCESS_NOTIFICATION_FLAG_SMS_READ;
    common_enable_disable_unsolicited_events (self, callback, user_data);
}

/*****************************************************************************/
/* Create SMS (Messaging interface) */

static MMBaseSms *
messaging_create_sms (MMIfaceModemMessaging *self)
{
    return mm_sms_mbim_new (MM_BASE_MODEM (self));
}

/*****************************************************************************/

MMBroadbandModemMbim *
mm_broadband_modem_mbim_new (const gchar *device,
                             const gchar **drivers,
                             const gchar *plugin,
                             guint16 vendor_id,
                             guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, FALSE,
                         NULL);
}

static void
mm_broadband_modem_mbim_init (MMBroadbandModemMbim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_MBIM,
                                              MMBroadbandModemMbimPrivate);

    self->priv->sim_pin_retries = MM_UNLOCK_RETRIES_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMBroadbandModemMbim *self = MM_BROADBAND_MODEM_MBIM (object);
    MMPortMbim *mbim;

    mbim = mm_base_modem_peek_port_mbim (MM_BASE_MODEM (self));
    if (mbim) {
        /* Explicitly remove notification handler */
        self->priv->setup_flags = PROCESS_NOTIFICATION_FLAG_NONE;
        common_setup_cleanup_unsolicited_events_sync (self, mm_port_mbim_peek_device (mbim), FALSE);
        /* Disconnect signal handler for mbim-proxy disappearing, if it exists */
        untrack_mbim_device_removed (self, mbim);
        /* If we did open the MBIM port during initialization, close it now */
        if (mm_port_mbim_is_open (mbim))
            mm_port_mbim_close (mbim, NULL, NULL);
    }

    g_free (self->priv->caps_device_id);
    g_free (self->priv->caps_firmware_info);
    g_free (self->priv->current_operator_id);
    g_free (self->priv->current_operator_name);

    G_OBJECT_CLASS (mm_broadband_modem_mbim_parent_class)->finalize (object);
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
    iface->load_supported_modes = modem_load_supported_modes;
    iface->load_supported_modes_finish = modem_load_supported_modes_finish;
    iface->load_unlock_required = modem_load_unlock_required;
    iface->load_unlock_required_finish = modem_load_unlock_required_finish;
    iface->load_unlock_retries = modem_load_unlock_retries;
    iface->load_unlock_retries_finish = modem_load_unlock_retries_finish;
    iface->load_own_numbers = modem_load_own_numbers;
    iface->load_own_numbers_finish = modem_load_own_numbers_finish;
    iface->load_power_state = modem_load_power_state;
    iface->load_power_state_finish = modem_load_power_state_finish;
    iface->modem_power_up = modem_power_up;
    iface->modem_power_up_finish = power_up_finish;
    iface->modem_power_down = modem_power_down;
    iface->modem_power_down_finish = power_down_finish;
    iface->load_supported_ip_families = modem_load_supported_ip_families;
    iface->load_supported_ip_families_finish = modem_load_supported_ip_families_finish;

    /* Unneeded things */
    iface->modem_after_power_up = NULL;
    iface->modem_after_power_up_finish = NULL;
    iface->load_supported_charsets = NULL;
    iface->load_supported_charsets_finish = NULL;
    iface->setup_flow_control = NULL;
    iface->setup_flow_control_finish = NULL;
    iface->setup_charset = NULL;
    iface->setup_charset_finish = NULL;
    iface->load_signal_quality = NULL;
    iface->load_signal_quality_finish = NULL;
    iface->load_access_technologies = NULL;
    iface->load_access_technologies_finish = NULL;

    /* Create MBIM-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    /* Create MBIM-specific bearer */
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;

    /* SIM hot swapping */
    iface->setup_sim_hot_swap = modem_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = modem_setup_sim_hot_swap_finish;
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Initialization steps */
    iface->load_imei = modem_3gpp_load_imei;
    iface->load_imei_finish = modem_3gpp_load_imei_finish;
    iface->load_enabled_facility_locks = modem_3gpp_load_enabled_facility_locks;
    iface->load_enabled_facility_locks_finish = modem_3gpp_load_enabled_facility_locks_finish;

    iface->setup_unsolicited_events = setup_unsolicited_events_3gpp;
    iface->setup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_3gpp_finish;
    iface->cleanup_unsolicited_events = cleanup_unsolicited_events_3gpp;
    iface->cleanup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_3gpp_finish;
    iface->enable_unsolicited_events = modem_3gpp_enable_unsolicited_events;
    iface->enable_unsolicited_events_finish = modem_3gpp_common_enable_disable_unsolicited_events_finish;
    iface->disable_unsolicited_events = modem_3gpp_disable_unsolicited_events;
    iface->disable_unsolicited_events_finish = modem_3gpp_common_enable_disable_unsolicited_events_finish;
    iface->setup_unsolicited_registration_events = setup_unsolicited_registration_events;
    iface->setup_unsolicited_registration_events_finish = common_setup_cleanup_unsolicited_events_3gpp_finish;
    iface->cleanup_unsolicited_registration_events = cleanup_unsolicited_registration_events;
    iface->cleanup_unsolicited_registration_events_finish = common_setup_cleanup_unsolicited_events_3gpp_finish;
    iface->enable_unsolicited_registration_events = modem_3gpp_enable_unsolicited_registration_events;
    iface->enable_unsolicited_registration_events_finish = modem_3gpp_common_enable_disable_unsolicited_registration_events_finish;
    iface->disable_unsolicited_registration_events = modem_3gpp_disable_unsolicited_registration_events;
    iface->disable_unsolicited_registration_events_finish = modem_3gpp_common_enable_disable_unsolicited_registration_events_finish;
    iface->load_operator_code = modem_3gpp_load_operator_code;
    iface->load_operator_code_finish = modem_3gpp_load_operator_code_finish;
    iface->load_operator_name = modem_3gpp_load_operator_name;
    iface->load_operator_name_finish = modem_3gpp_load_operator_name_finish;
    iface->run_registration_checks = modem_3gpp_run_registration_checks;
    iface->run_registration_checks_finish = modem_3gpp_run_registration_checks_finish;
    iface->register_in_network = modem_3gpp_register_in_network;
    iface->register_in_network_finish = modem_3gpp_register_in_network_finish;
    iface->scan_networks = modem_3gpp_scan_networks;
    iface->scan_networks_finish = modem_3gpp_scan_networks_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface->load_capabilities = NULL;
    iface->load_capabilities_finish = NULL;
    iface->enable_location_gathering = NULL;
    iface->enable_location_gathering_finish = NULL;
}

static void
iface_modem_messaging_init (MMIfaceModemMessaging *iface)
{
    iface->check_support = messaging_check_support;
    iface->check_support_finish = messaging_check_support_finish;
    iface->load_supported_storages = messaging_load_supported_storages;
    iface->load_supported_storages_finish = messaging_load_supported_storages_finish;
    iface->setup_sms_format = NULL;
    iface->setup_sms_format_finish = NULL;
    iface->set_default_storage = NULL;
    iface->set_default_storage_finish = NULL;
    iface->load_initial_sms_parts = load_initial_sms_parts;
    iface->load_initial_sms_parts_finish = load_initial_sms_parts_finish;
    iface->setup_unsolicited_events = setup_unsolicited_events_messaging;
    iface->setup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_messaging_finish;
    iface->cleanup_unsolicited_events = cleanup_unsolicited_events_messaging;
    iface->cleanup_unsolicited_events_finish = common_setup_cleanup_unsolicited_events_messaging_finish;
    iface->enable_unsolicited_events = enable_unsolicited_events_messaging;
    iface->enable_unsolicited_events_finish = common_enable_disable_unsolicited_events_messaging_finish;
    iface->disable_unsolicited_events = disable_unsolicited_events_messaging;
    iface->disable_unsolicited_events_finish = common_enable_disable_unsolicited_events_messaging_finish;
    iface->create_sms = messaging_create_sms;
}

static void
iface_modem_signal_init (MMIfaceModemSignal *iface)
{
    iface->check_support = NULL;
    iface->check_support_finish = NULL;
    iface->load_values = NULL;
    iface->load_values_finish = NULL;
}

static void
mm_broadband_modem_mbim_class_init (MMBroadbandModemMbimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemMbimPrivate));

    object_class->finalize = finalize;

    broadband_modem_class->initialization_started = initialization_started;
    broadband_modem_class->initialization_started_finish = initialization_started_finish;
    broadband_modem_class->enabling_started = enabling_started;
    broadband_modem_class->enabling_started_finish = enabling_started_finish;
    /* Do not initialize the MBIM modem through AT commands */
    broadband_modem_class->enabling_modem_init = NULL;
    broadband_modem_class->enabling_modem_init_finish = NULL;
}
