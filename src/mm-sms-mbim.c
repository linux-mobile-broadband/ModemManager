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
#include "mm-modem-helpers-mbim.h"
#include "mm-iface-modem-messaging.h"
#include "mm-sms-mbim.h"
#include "mm-base-modem.h"
#include "mm-log-object.h"
#include "mm-sms-part-3gpp.h"

G_DEFINE_TYPE (MMSmsMbim, mm_sms_mbim, MM_TYPE_BASE_SMS)

/*****************************************************************************/

static gboolean
peek_device (gpointer self,
             MbimDevice **o_device,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    MMBaseModem *modem = NULL;

    g_object_get (G_OBJECT (self),
                  MM_BASE_SMS_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    if (o_device) {
        MMPortMbim *port;

        port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (modem));
        if (!port) {
            g_task_report_new_error (self,
                                     callback,
                                     user_data,
                                     peek_device,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't peek MBIM port");
            g_object_unref (modem);
            return FALSE;
        }

        *o_device = mm_port_mbim_peek_device (port);
    }

    g_object_unref (modem);
    return TRUE;
}

/*****************************************************************************/
/* Send the SMS */

typedef struct {
    MMBaseModem *modem;
    MbimDevice *device;
    GList *current;
} SmsSendContext;

static void
sms_send_context_free (SmsSendContext *ctx)
{
    g_object_unref (ctx->device);
    g_object_unref (ctx->modem);
    g_slice_free (SmsSendContext, ctx);
}

static gboolean
sms_send_finish (MMBaseSms *self,
                 GAsyncResult *res,
                 GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void sms_send_next_part (GTask *task);

static void
sms_send_set_ready (MbimDevice *device,
                    GAsyncResult *res,
                    GTask *task)
{
    SmsSendContext *ctx;
    MbimMessage *response;
    GError *error = NULL;
    guint32 message_reference;

    ctx = g_task_get_task_data (task);
    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_sms_send_response_parse (
            response,
            &message_reference,
            &error)) {
        mm_sms_part_set_message_reference ((MMSmsPart *)ctx->current->data,
                                           message_reference);
    }

    if (response)
        mbim_message_unref (response);

    if (error) {
        g_prefix_error (&error, "Couldn't send SMS part: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on with next part */
    ctx->current = g_list_next (ctx->current);
    sms_send_next_part (task);
}

static void
sms_send_next_part (GTask *task)
{
    MMSmsMbim *self;
    SmsSendContext *ctx;
    MbimMessage *message;
    guint8 *pdu;
    guint pdulen = 0;
    guint msgstart = 0;
    GError *error = NULL;
    MbimSmsPduSendRecord send_record;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!ctx->current) {
        /* Done we are */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Get PDU */
    pdu = mm_sms_part_3gpp_get_submit_pdu ((MMSmsPart *)ctx->current->data, &pdulen, &msgstart, self, &error);
    if (!pdu) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    send_record.pdu_data_size = pdulen;
    send_record.pdu_data = pdu;

    message = mbim_message_sms_send_set_new (MBIM_SMS_FORMAT_PDU,
                                             &send_record,
                                             NULL,
                                             NULL);
    mbim_device_command (ctx->device,
                         message,
                         30,
                         NULL,
                         (GAsyncReadyCallback)sms_send_set_ready,
                         task);
    mbim_message_unref (message);
    g_free (pdu);
}

static void
sms_send (MMBaseSms *self,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    SmsSendContext *ctx;
    MbimDevice *device;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    /* Setup the context */
    ctx = g_slice_new0 (SmsSendContext);
    ctx->device = g_object_ref (device);
    g_object_get (self,
                  MM_BASE_SMS_MODEM, &ctx->modem,
                  NULL);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)sms_send_context_free);

    ctx->current = mm_base_sms_get_parts (self);;
    sms_send_next_part (task);
}

/*****************************************************************************/

typedef struct {
    MMBaseModem *modem;
    MbimDevice *device;
    GList *current;
    guint n_failed;
} SmsDeletePartsContext;

static void
sms_delete_parts_context_free (SmsDeletePartsContext *ctx)
{
    g_object_unref (ctx->device);
    g_object_unref (ctx->modem);
    g_slice_free (SmsDeletePartsContext, ctx);
}

static gboolean
sms_delete_finish (MMBaseSms *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void delete_next_part (GTask *task);

static void
sms_delete_set_ready (MbimDevice *device,
                      GAsyncResult *res,
                      GTask *task)
{
    MMSmsMbim *self;
    SmsDeletePartsContext *ctx;
    MbimMessage *response;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
        mbim_message_sms_delete_response_parse (response, &error);

    if (response)
        mbim_message_unref (response);

    if (error) {
        ctx->n_failed++;
        mm_obj_dbg (self, "couldn't delete SMS part with index %u: %s",
                    mm_sms_part_get_index ((MMSmsPart *)ctx->current->data),
                    error->message);
        g_error_free (error);
    }

    /* We reset the index, as there is no longer that part */
    mm_sms_part_set_index ((MMSmsPart *)ctx->current->data, SMS_PART_INVALID_INDEX);

    ctx->current = g_list_next (ctx->current);
    delete_next_part (task);
}

static void
delete_next_part (GTask *task)
{
    SmsDeletePartsContext *ctx;
    MbimMessage *message;

    ctx = g_task_get_task_data (task);
    /* Skip non-stored parts */
    while (ctx->current &&
           mm_sms_part_get_index ((MMSmsPart *)ctx->current->data) == SMS_PART_INVALID_INDEX)
        ctx->current = g_list_next (ctx->current);

    /* If all removed, we're done */
    if (!ctx->current) {
        if (ctx->n_failed > 0)
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't delete %u parts from this SMS",
                                     ctx->n_failed);
        else
            g_task_return_boolean (task, TRUE);

        g_object_unref (task);
        return;
    }

    message = mbim_message_sms_delete_set_new (MBIM_SMS_FLAG_INDEX,
                                               (guint32)mm_sms_part_get_index ((MMSmsPart *)ctx->current->data),
                                               NULL);
    mbim_device_command (ctx->device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)sms_delete_set_ready,
                         task);
    mbim_message_unref (message);

}

static void
sms_delete (MMBaseSms *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    SmsDeletePartsContext *ctx;
    MbimDevice *device;
    GTask *task;

    if (!peek_device (self, &device, callback, user_data))
        return;

    ctx = g_slice_new0 (SmsDeletePartsContext);
    ctx->device = g_object_ref (device);
    g_object_get (self,
                  MM_BASE_SMS_MODEM, &ctx->modem,
                  NULL);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)sms_delete_parts_context_free);

    /* Go on deleting parts */
    ctx->current = mm_base_sms_get_parts (self);
    delete_next_part (task);
}

/*****************************************************************************/

MMBaseSms *
mm_sms_mbim_new (MMBaseModem *modem)
{
    return MM_BASE_SMS (g_object_new (MM_TYPE_SMS_MBIM,
                                      MM_BASE_SMS_MODEM, modem,
                                      NULL));
}

static void
mm_sms_mbim_init (MMSmsMbim *self)
{
}

static void
mm_sms_mbim_class_init (MMSmsMbimClass *klass)
{
    MMBaseSmsClass *base_sms_class = MM_BASE_SMS_CLASS (klass);

    base_sms_class->store = NULL;
    base_sms_class->store_finish = NULL;
    base_sms_class->send = sms_send;
    base_sms_class->send_finish = sms_send_finish;
    base_sms_class->delete = sms_delete;
    base_sms_class->delete_finish = sms_delete_finish;
}
