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

struct _MMSimMbimPrivate {
    gboolean  preload;
    GError   *preload_error;
    gchar    *imsi;
    gchar    *iccid;
    GError   *iccid_error;
};

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

/*****************************************************************************/
/* Preload subscriber info */

static gboolean
preload_subscriber_info_finish (MMSimMbim     *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
subscriber_ready_status_ready (MbimDevice   *device,
                               GAsyncResult *res,
                               GTask        *task)
{
    MMSimMbim              *self;
    g_autoptr(MbimMessage)  response = NULL;
    g_autofree gchar       *raw_iccid = NULL;

    self = g_task_get_source_object (task);

    g_assert (!self->priv->preload_error);
    g_assert (!self->priv->imsi);
    g_assert (!self->priv->iccid);
    g_assert (!self->priv->iccid_error);

    response = mbim_device_command_finish (device, res, &self->priv->preload_error);
    if (response && mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &self->priv->preload_error)) {
        if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
            if (!mbim_message_ms_basic_connect_v3_subscriber_ready_status_response_parse (
                    response,
                    NULL, /* ready_state */
                    NULL, /* flags */
                    &self->priv->imsi,
                    &raw_iccid,
                    NULL, /* ready_info */
                    NULL, /* telephone_numbers_count */
                    NULL, /* telephone_numbers */
                    &self->priv->preload_error))
                g_prefix_error (&self->priv->preload_error, "Failed processing MBIMEx v3.0 subscriber ready status response: ");
            else
                mm_obj_dbg (self, "processed MBIMEx v3.0 subscriber ready status response");
        } else {
            if (!mbim_message_subscriber_ready_status_response_parse (
                    response,
                    NULL, /* ready_state */
                    &self->priv->imsi,
                    &raw_iccid,
                    NULL, /* ready_info */
                    NULL, /* telephone_numbers_count */
                    NULL, /* telephone_numbers */
                    &self->priv->preload_error))
                g_prefix_error (&self->priv->preload_error, "Failed processing subscriber ready status response: ");
            else
                mm_obj_dbg (self, "processed subscriber ready status response");
        }

        if (raw_iccid)
            self->priv->iccid = mm_3gpp_parse_iccid (raw_iccid, &self->priv->iccid_error);
    }

    /* At this point we just complete, as all the info and errors have already
     * been stored */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
preload_subscriber_info (MMSimMbim           *self,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    GTask                  *task;
    MbimDevice             *device;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    /* only preload one single time; the info of the SIM should not
     * change during runtime, unless we're handling hotplug events */
    if (self->priv->preload) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
    self->priv->preload = TRUE;

    message = mbim_message_subscriber_ready_status_query_new (NULL);
    mbim_device_command (device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)subscriber_ready_status_ready,
                         task);
}

/*****************************************************************************/
/* Load SIM identifier */

static gchar *
load_sim_identifier_finish (MMBaseSim     *_self,
                            GAsyncResult  *res,
                            GError       **error)
{
    MMSimMbim *self = MM_SIM_MBIM (_self);

    if (!preload_subscriber_info_finish (self, res, error))
        return NULL;

    g_assert (self->priv->preload);
    if (self->priv->iccid_error) {
        g_propagate_error (error, g_error_copy (self->priv->iccid_error));
        return NULL;
    }
    if (self->priv->preload_error) {
        g_propagate_error (error, g_error_copy (self->priv->preload_error));
        return NULL;
    }
    if (!self->priv->iccid) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "SIM iccid not available");
        return NULL;
    }
    return g_strdup (self->priv->iccid);
}

static void
load_sim_identifier (MMBaseSim           *self,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    preload_subscriber_info (MM_SIM_MBIM (self), callback, user_data);
}

/*****************************************************************************/
/* Load IMSI */

static gchar *
load_imsi_finish (MMBaseSim     *_self,
                  GAsyncResult  *res,
                  GError       **error)
{
    MMSimMbim *self = MM_SIM_MBIM (_self);

    if (!preload_subscriber_info_finish (self, res, error))
        return NULL;

    g_assert (self->priv->preload);
    if (self->priv->preload_error) {
        g_propagate_error (error, g_error_copy (self->priv->preload_error));
        return NULL;
    }
    g_assert (self->priv->imsi);
    return g_strdup (self->priv->imsi);
}

static void
load_imsi (MMBaseSim           *self,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    preload_subscriber_info (MM_SIM_MBIM (self), callback, user_data);
}

/*****************************************************************************/
/* Load EID */

#define UICC_STATUS_OK 144
#define EID_APDU_HEADER 5

typedef enum {
    ESIM_CHECK_STEP_FIRST,
    ESIM_CHECK_STEP_UICC_OPEN_CHANNEL,
    ESIM_CHECK_STEP_UICC_GET_APDU,
    ESIM_CHECK_STEP_UICC_CLOSE_CHANNEL,
    ESIM_CHECK_STEP_LAST
} EsimCheckStep;

typedef struct {
    EsimCheckStep step;
    guint32       channel;
    guint32       channel_grp;
    gchar        *eid;
    GError       *saved_error;
} EsimCheckContext;

static void
esim_check_context_free (EsimCheckContext *ctx)
{
    g_assert (!ctx->saved_error);
    g_free (ctx->eid);
    g_free (ctx);
}

static gchar *
load_eid_finish (MMBaseSim     *self,
                 GAsyncResult  *res,
                 GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void esim_check_step (MbimDevice *device,
                             GTask      *task);

static void
check_uicc_close_channel_ready (MbimDevice   *device,
                                GAsyncResult *res,
                                GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    g_autoptr(GError)       error = NULL;
    guint32                 status;
    EsimCheckContext       *ctx;

    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_uicc_low_level_access_close_channel_response_parse (response, &status, &error)) {
        /* if we have a saved error, prefer that one */
        if (!ctx->saved_error)
            ctx->saved_error = g_steal_pointer (&error);
    } else if (status != UICC_STATUS_OK) {
        /* if we have a saved error, prefer that one */
        if (!ctx->saved_error)
            ctx->saved_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                            "UICC close channel failed");
    }

    /* go on to next step */
    ctx->step++;
    esim_check_step (device, task);
}

static void
check_uicc_apdu_ready (MbimDevice   *device,
                       GAsyncResult *res,
                       GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;
    guint32                 status;
    guint32                 apdu_response_size;
    const guint8           *apdu_response = NULL;
    EsimCheckContext       *ctx;

    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_uicc_low_level_access_apdu_response_parse (
            response,
            &status,
            &apdu_response_size,
            &apdu_response,
            &error))
        ctx->saved_error = error;
    else
        ctx->eid = mm_decode_eid ((const gchar *)(apdu_response + EID_APDU_HEADER),
                                  apdu_response_size - EID_APDU_HEADER);

    /* always go on to the close channel step, even on error */
    ctx->step++;
    esim_check_step (device, task);
}

static void
check_uicc_open_channel_ready (MbimDevice   *device,
                               GAsyncResult *res,
                               GTask        *task)
{
    g_autoptr(MbimMessage)  response = NULL;
    GError                 *error = NULL;
    guint32                 status;
    guint32                 channel;
    EsimCheckContext       *ctx;

    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_ms_uicc_low_level_access_open_channel_response_parse (
            response,
            &status,
            &channel,
            NULL,
            NULL,
            &error)) {
        ctx->saved_error = error;
        ctx->step = ESIM_CHECK_STEP_LAST;
    } else if (status != UICC_STATUS_OK) {
        ctx->saved_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                        "UICC open channel failed");;
        ctx->step = ESIM_CHECK_STEP_LAST;
    } else {
        /* channel is open, from now on we'll need to always explicitly close,
         * even on errors */
        ctx->channel = channel;
        ctx->step++;
    }

    esim_check_step (device, task);
}

static void
esim_check_step (MbimDevice *device,
                 GTask      *task)
{
    MMSimMbim              *self;
    EsimCheckContext       *ctx;
    g_autoptr(MbimMessage)  message = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    switch (ctx->step) {
    case ESIM_CHECK_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case ESIM_CHECK_STEP_UICC_OPEN_CHANNEL: {
        const guint8 app_id[] = {0xa0, 0x00, 0x00, 0x05, 0x59, 0x10, 0x10, 0xff,
                                 0xff, 0xff, 0xff, 0x89, 0x00, 0x00, 0x01, 0x00};

        /* Channel group is used to bundle all logical channels opened and for
         * future reference to close */
        ctx->channel_grp = 1;

        mm_obj_dbg (self, "opening UICC channel...");
        message = mbim_message_ms_uicc_low_level_access_open_channel_set_new (
                      sizeof (app_id),
                      app_id,
                      4, /* SelectP2Arg: Return File Control Parameters(FCP) template.
                          * Refer 11.1.13 of of the ETSI TS 102 221 technical specification.
                          */
                      ctx->channel_grp,
                      NULL);
        mbim_device_command (device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)check_uicc_open_channel_ready,
                             task);
        return;
    }

    case ESIM_CHECK_STEP_UICC_GET_APDU: {
        const guint8 apdu_cmd[] = {0x81, 0xe2, 0x91, 0x00, 0x06, 0xbf,
                                   0x3e, 0x03, 0x5c, 0x01, 0x5a, 0x00};

        mm_obj_dbg (self, "reading EID...");
        message = mbim_message_ms_uicc_low_level_access_apdu_set_new (
                      ctx->channel,
                      MBIM_UICC_SECURE_MESSAGING_NONE,
                      MBIM_UICC_CLASS_BYTE_TYPE_EXTENDED,
                      sizeof (apdu_cmd),
                      apdu_cmd,
                      NULL);
        mbim_device_command (device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)check_uicc_apdu_ready,
                             task);
        return;
    }

    case ESIM_CHECK_STEP_UICC_CLOSE_CHANNEL:
        mm_obj_dbg (self, "closing UICC channel...");
        message = mbim_message_ms_uicc_low_level_access_close_channel_set_new (
                      ctx->channel,
                      ctx->channel_grp,
                      NULL);
        mbim_device_command (device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)check_uicc_close_channel_ready,
                             task);
        return;

    case ESIM_CHECK_STEP_LAST:
        if (ctx->saved_error)
            g_task_return_error (task, g_steal_pointer (&ctx->saved_error));
        else if (ctx->eid)
            g_task_return_pointer (task, g_steal_pointer (&ctx->eid), g_free);
        else
            g_assert_not_reached ();
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

static void
load_eid (MMBaseSim           *self,
          GAsyncReadyCallback  callback,
          gpointer             user_data)
{
    MbimDevice       *device;
    GTask            *task;
    EsimCheckContext *ctx;

    if (!peek_device (self, &device, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_new0 (EsimCheckContext, 1);
    ctx->step = ESIM_CHECK_STEP_FIRST;
    g_task_set_task_data (task, ctx, (GDestroyNotify)esim_check_context_free);
    esim_check_step (device, task);
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
                         30,
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
                         30,
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

MMBaseSim *
mm_sim_mbim_new_initialized (MMBaseModem *modem,
                             guint        slot_number,
                             gboolean     active,
                             const gchar *sim_identifier,
                             const gchar *imsi,
                             const gchar *eid,
                             const gchar *operator_identifier,
                             const gchar *operator_name,
                             const GStrv  emergency_numbers)
{
    MMBaseSim *sim;

    sim = MM_BASE_SIM (g_object_new (MM_TYPE_SIM_MBIM,
                                     MM_BASE_SIM_MODEM,       modem,
                                     MM_BASE_SIM_SLOT_NUMBER, slot_number,
                                     "active",                active,
                                     "sim-identifier",        sim_identifier,
                                     "eid",                   eid,
                                     "operator-identifier",   operator_identifier,
                                     "operator-name",         operator_name,
                                     "emergency-numbers",     emergency_numbers,
                                     NULL));

    mm_base_sim_export (sim);
    return sim;
}

static void
mm_sim_mbim_init (MMSimMbim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_SIM_MBIM,
                                              MMSimMbimPrivate);
}

static void
finalize (GObject *object)
{
    MMSimMbim *self = MM_SIM_MBIM (object);

    g_clear_error (&self->priv->preload_error);
    g_free (self->priv->imsi);
    g_free (self->priv->iccid);
    g_clear_error (&self->priv->iccid_error);

    G_OBJECT_CLASS (mm_sim_mbim_parent_class)->finalize (object);
}

static void
mm_sim_mbim_class_init (MMSimMbimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseSimClass *base_sim_class = MM_BASE_SIM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSimMbimPrivate));

    object_class->finalize = finalize;
    base_sim_class->load_sim_identifier = load_sim_identifier;
    base_sim_class->load_sim_identifier_finish = load_sim_identifier_finish;
    base_sim_class->load_imsi = load_imsi;
    base_sim_class->load_imsi_finish = load_imsi_finish;
    base_sim_class->load_eid = load_eid;
    base_sim_class->load_eid_finish = load_eid_finish;
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
