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
#include <libmm-common.h>

#include "mm-modem-helpers-qmi.h"
#include "mm-iface-modem-messaging.h"
#include "mm-sms-qmi.h"
#include "mm-base-modem.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMSmsQmi, mm_sms_qmi, MM_TYPE_SMS);

/*****************************************************************************/

static gboolean
ensure_qmi_client (MMSmsQmi *self,
                   QmiService service,
                   QmiClient **o_client,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    MMBaseModem *modem = NULL;
    QmiClient *client;

    g_object_get (self,
                  MM_SMS_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    client = mm_qmi_port_peek_client (mm_base_modem_peek_port_qmi (modem),
                                      service,
                                      MM_QMI_PORT_FLAG_DEFAULT);
    if (!client)
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't peek client for service '%s'",
                                             qmi_service_get_string (service));
    else
        *o_client = client;

    g_object_unref (modem);
    return !!client;
}

/*****************************************************************************/
/* Store the SMS */

typedef struct {
    MMSms *self;
    MMBaseModem *modem;
    QmiClientWms *client;
    GSimpleAsyncResult *result;
    MMSmsStorage storage;
} SmsStoreContext;

static void
sms_store_context_complete_and_free (SmsStoreContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->client);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_slice_free (SmsStoreContext, ctx);
}

static gboolean
sms_store_finish (MMSms *self,
                  GAsyncResult *res,
                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
store_ready (QmiClientWms *client,
             GAsyncResult *res,
             SmsStoreContext *ctx)
{
    QmiMessageWmsRawWriteOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_wms_raw_write_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
    } else if (!qmi_message_wms_raw_write_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't write SMS part: ");
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        GList *parts;
        guint32 idx;

        qmi_message_wms_raw_write_output_get_memory_index (
            output,
            &idx,
            NULL);

        /* Set the index in the part we hold */
        parts = mm_sms_get_parts (ctx->self);
        mm_sms_part_set_index ((MMSmsPart *)parts->data, (guint)idx);

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    if (output)
        qmi_message_wms_raw_write_output_unref (output);

    sms_store_context_complete_and_free (ctx);
}

static void
sms_store (MMSms *self,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    GError *error = NULL;
    SmsStoreContext *ctx;
    GList *parts;
    QmiMessageWmsRawWriteInput *input;
    guint8 *pdu;
    guint pdulen = 0;
    guint msgstart = 0;
    GArray *array;
    QmiClient *client = NULL;

    parts = mm_sms_get_parts (self);

    /* We currently support storing *only* single part SMS */
    if (g_list_length (parts) != 1) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Cannot store SMS with %u parts",
                                             g_list_length (parts));
        return;
    }

    /* Ensure WMS client */
    if (!ensure_qmi_client (MM_SMS_QMI (self),
                            QMI_SERVICE_WMS, &client,
                            callback, user_data))
        return;

    /* Get PDU */
    pdu = mm_sms_part_get_submit_pdu ((MMSmsPart *)parts->data, &pdulen, &msgstart, &error);
    if (!pdu) {
        /* 'error' should already be set */
        g_simple_async_report_take_gerror_in_idle (G_OBJECT (self),
                                                   callback,
                                                   user_data,
                                                   error);
        return;
    }

    /* Convert to GArray */
    array = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (guint8), pdulen),
                                 pdu,
                                 pdulen);
    g_free (pdu);

    /* Setup the context */
    ctx = g_slice_new0 (SmsStoreContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             sms_store);
    ctx->self = g_object_ref (self);
    ctx->client = g_object_ref (client);
    g_object_get (self,
                  MM_SMS_MODEM, &ctx->modem,
                  NULL);
    g_object_get (ctx->modem,
                  MM_IFACE_MODEM_MESSAGING_SMS_MEM2_STORAGE, &ctx->storage,
                  NULL);

    /* Create input bundle and send the QMI request */
    input = qmi_message_wms_raw_write_input_new ();
    qmi_message_wms_raw_write_input_set_raw_message_data (
        input,
        mm_sms_storage_to_qmi_storage_type (ctx->storage),
        QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_POINT_TO_POINT,
        array,
        NULL);
    qmi_client_wms_raw_write (ctx->client,
                              input,
                              5,
                              NULL,
                              (GAsyncReadyCallback)store_ready,
                              ctx);
    qmi_message_wms_raw_write_input_unref (input);
    g_array_unref (array);
}

/*****************************************************************************/

MMSms *
mm_sms_qmi_new (MMBaseModem *modem)
{
    return MM_SMS (g_object_new (MM_TYPE_SMS_QMI,
                                 MM_SMS_MODEM, modem,
                                 NULL));
}

static void
mm_sms_qmi_init (MMSmsQmi *self)
{
}

static void
mm_sms_qmi_class_init (MMSmsQmiClass *klass)
{
    MMSmsClass *sms_class = MM_SMS_CLASS (klass);

    sms_class->store = sms_store;
    sms_class->store_finish = sms_store_finish;
}
