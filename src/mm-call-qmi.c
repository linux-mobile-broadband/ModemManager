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
 * Copyright (C) 2021 Joel Selvaraj <jo@jsfamily.in>
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

#include "mm-broadband-modem-qmi.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-voice.h"
#include "mm-call-qmi.h"
#include "mm-base-modem.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMCallQmi, mm_call_qmi, MM_TYPE_BASE_CALL)

/*****************************************************************************/

static gboolean
ensure_qmi_client (MMCallQmi            *self,
                   QmiService            service,
                   QmiClient           **o_client,
                   GAsyncReadyCallback   callback,
                   gpointer              user_data)
{
    MMBaseModem *modem = NULL;
    QmiClient   *client;
    MMPortQmi   *port;

    g_object_get (self,
                  MM_BASE_CALL_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    port = mm_broadband_modem_qmi_peek_port_qmi (MM_BROADBAND_MODEM_QMI (modem));
    g_object_unref (modem);

    if (!port) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 ensure_qmi_client,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't peek QMI port");
        return FALSE;
    }

    client = mm_port_qmi_peek_client (port,
                                      service,
                                      MM_PORT_QMI_FLAG_DEFAULT);
    if (!client) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 ensure_qmi_client,
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
/* Start the call */

static gboolean
call_start_finish (MMBaseCall    *self,
                   GAsyncResult  *res,
                   GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
voice_dial_call_ready (QmiClientVoice *client,
                       GAsyncResult   *res,
                       GTask          *task)
{
    QmiMessageVoiceDialCallOutput *output;
    GError                        *error = NULL;
    MMBaseCall                    *self;

    self = MM_BASE_CALL (g_task_get_source_object (task));

    output = qmi_client_voice_dial_call_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_dial_call_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't create call: ");
        g_task_return_error (task, error);
    } else {
        guint8 call_id = 0;

        qmi_message_voice_dial_call_output_get_call_id (output, &call_id, NULL);
        if (call_id == 0) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_INVALID_ARGS,
                                     "Invalid call index");
        } else {
            mm_base_call_set_index (self, call_id);
            g_task_return_boolean (task, TRUE);
        }
    }

    if (output)
        qmi_message_voice_dial_call_output_unref (output);

    g_object_unref (task);
}

static void
call_start (MMBaseCall          *self,
            GCancellable        *cancellable,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
    QmiClient                    *client = NULL;
    GTask                        *task;
    QmiMessageVoiceDialCallInput *input;

    /* Ensure Voice client */
    if (!ensure_qmi_client (MM_CALL_QMI (self),
                            QMI_SERVICE_VOICE, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    input = qmi_message_voice_dial_call_input_new ();
    qmi_message_voice_dial_call_input_set_calling_number (
        input,
        mm_gdbus_call_get_number (MM_GDBUS_CALL (self)),
        NULL);

    mm_obj_dbg (self, "starting call");
    qmi_client_voice_dial_call (QMI_CLIENT_VOICE (client),
                                input,
                                90,
                                NULL,
                                (GAsyncReadyCallback) voice_dial_call_ready,
                                task);
    qmi_message_voice_dial_call_input_unref (input);
}

/*****************************************************************************/
/* Accept the call */

static gboolean
call_accept_finish (MMBaseCall    *self,
                    GAsyncResult  *res,
                    GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
voice_answer_call_ready (QmiClientVoice *client,
                         GAsyncResult   *res,
                         GTask          *task)
{
    QmiMessageVoiceAnswerCallOutput *output;
    GError *error = NULL;

    output = qmi_client_voice_answer_call_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_answer_call_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't accept call: ");
        g_task_return_error (task, error);
    } else {
        g_task_return_boolean (task, TRUE);
    }

    if (output)
        qmi_message_voice_answer_call_output_unref (output);

    g_object_unref (task);
}

static void
call_accept (MMBaseCall          *self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    QmiClient                       *client = NULL;
    GTask                           *task;
    guint8                           call_id;
    QmiMessageVoiceAnswerCallInput  *input;

    /* Ensure Voice client */
    if (!ensure_qmi_client (MM_CALL_QMI (self),
                            QMI_SERVICE_VOICE, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    call_id = mm_base_call_get_index (self);
    if (call_id == 0) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_INVALID_ARGS,
                                 "Invalid call index");
        g_object_unref (task);
        return;
    }

    input = qmi_message_voice_answer_call_input_new ();
    qmi_message_voice_answer_call_input_set_call_id (
        input,
        call_id,
        NULL);

    mm_obj_dbg (self, "Accepting call with id: %u", call_id);
    qmi_client_voice_answer_call (QMI_CLIENT_VOICE (client),
                                  input,
                                  5,
                                  NULL,
                                  (GAsyncReadyCallback) voice_answer_call_ready,
                                  task);
    qmi_message_voice_answer_call_input_unref (input);
}

/*****************************************************************************/
/* Hangup the call */

static gboolean
call_hangup_finish (MMBaseCall    *self,
                    GAsyncResult  *res,
                    GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
voice_end_call_ready (QmiClientVoice *client,
                      GAsyncResult   *res,
                      GTask          *task)
{
    QmiMessageVoiceEndCallOutput *output;
    GError                       *error = NULL;

    output = qmi_client_voice_end_call_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_end_call_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't hangup call: ");
        g_task_return_error (task, error);
    } else {
        g_task_return_boolean (task, TRUE);
    }

    if (output)
        qmi_message_voice_end_call_output_unref (output);

    g_object_unref (task);
}

static void
call_hangup (MMBaseCall          *self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    QmiClient                    *client = NULL;
    GTask                        *task;
    guint8                        call_id;
    QmiMessageVoiceEndCallInput  *input;

    /* Ensure Voice client */
    if (!ensure_qmi_client (MM_CALL_QMI (self),
                            QMI_SERVICE_VOICE, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    call_id = mm_base_call_get_index (self);
    if (call_id == 0) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_INVALID_ARGS,
                                 "Invalid call index");
        g_object_unref (task);
        return;
    }

    input = qmi_message_voice_end_call_input_new ();
    qmi_message_voice_end_call_input_set_call_id (
        input,
        call_id,
        NULL);

    mm_obj_dbg (self, "Hanging up call with id: %u", call_id);
    qmi_client_voice_end_call (QMI_CLIENT_VOICE (client),
                               input,
                               5,
                               NULL,
                               (GAsyncReadyCallback) voice_end_call_ready,
                               task);
    qmi_message_voice_end_call_input_unref (input);
}

/*****************************************************************************/
/* Send DTMF character */

typedef struct {
    QmiClient *client;
    guint8     call_id;
} SendDtmfContext;

static void
send_dtmf_context_free (SendDtmfContext *ctx)
{
    g_clear_object (&ctx->client);
    g_slice_free (SendDtmfContext, ctx);
}

static gboolean
call_send_dtmf_finish (MMBaseCall    *self,
                       GAsyncResult  *res,
                       GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
voice_stop_continuous_dtmf_ready (QmiClientVoice *client,
                                  GAsyncResult   *res,
                                  GTask          *task)
{
    g_autoptr (QmiMessageVoiceStopContinuousDtmfOutput)  output = NULL;
    GError                                              *error = NULL;

    output = qmi_client_voice_stop_continuous_dtmf_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_stop_continuous_dtmf_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't send DTMF character: ");
        g_task_return_error (task, error);
    } else {
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);
}

static gboolean
voice_stop_continuous_dtmf (GTask *task)
{
    SendDtmfContext                                    *ctx;
    GError                                             *error = NULL;
    g_autoptr (QmiMessageVoiceStopContinuousDtmfInput)  input = NULL;

    ctx = g_task_get_task_data (task);

    input = qmi_message_voice_stop_continuous_dtmf_input_new ();
    qmi_message_voice_stop_continuous_dtmf_input_set_data (input, ctx->call_id, &error);

    qmi_client_voice_stop_continuous_dtmf (QMI_CLIENT_VOICE (ctx->client),
                                           input,
                                           5,
                                           NULL,
                                           (GAsyncReadyCallback) voice_stop_continuous_dtmf_ready,
                                           task);

    return G_SOURCE_REMOVE;
}

static void
voice_start_continuous_dtmf_ready (QmiClientVoice *client,
                                   GAsyncResult   *res,
                                   GTask          *task)
{
    g_autoptr(QmiMessageVoiceStartContinuousDtmfOutput)  output = NULL;
    GError                                              *error = NULL;

    output = qmi_client_voice_start_continuous_dtmf_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!qmi_message_voice_start_continuous_dtmf_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't send DTMF character: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Disable DTMF press after 500 ms */
    g_timeout_add (500, (GSourceFunc) voice_stop_continuous_dtmf, task);
}

static void
call_send_dtmf (MMBaseCall          *self,
                const gchar         *dtmf,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    GTask                                               *task;
    GError                                              *error = NULL;
    SendDtmfContext                                     *ctx;
    QmiClient                                           *client = NULL;
    guint8                                               call_id;
    g_autoptr (QmiMessageVoiceStartContinuousDtmfInput)  input = NULL;

    /* Ensure Voice client */
    if (!ensure_qmi_client (MM_CALL_QMI (self),
                            QMI_SERVICE_VOICE, &client,
                            callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    call_id = mm_base_call_get_index (self);
    if (call_id == 0) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_INVALID_ARGS,
                                 "Invalid call index");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SendDtmfContext);
    ctx->client = g_object_ref (client);
    ctx->call_id = call_id;

    g_task_set_task_data (task, ctx, (GDestroyNotify) send_dtmf_context_free);

    /* Send DTMF character as ASCII number */
    input = qmi_message_voice_start_continuous_dtmf_input_new ();
    qmi_message_voice_start_continuous_dtmf_input_set_data (input,
                                                            call_id,
                                                            (guint8) dtmf[0],
                                                            &error);
    if (error) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_INVALID_ARGS,
                                 "DTMF data build failed");
        g_object_unref (task);
        return;
    }

    qmi_client_voice_start_continuous_dtmf (QMI_CLIENT_VOICE (client),
                                            input,
                                            5,
                                            NULL,
                                            (GAsyncReadyCallback) voice_start_continuous_dtmf_ready,
                                            task);
}

/*****************************************************************************/

MMBaseCall *
mm_call_qmi_new (MMBaseModem     *modem,
                 MMCallDirection  direction,
                 const gchar     *number)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_CALL_QMI,
                                       MM_BASE_CALL_MODEM, modem,
                                       "direction",        direction,
                                       "number",           number,
                                       MM_BASE_CALL_SKIP_INCOMING_TIMEOUT,       TRUE,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, TRUE,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  TRUE,
                                       NULL));
}

static void
mm_call_qmi_init (MMCallQmi *self)
{
}

static void
mm_call_qmi_class_init (MMCallQmiClass *klass)
{
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    base_call_class->start = call_start;
    base_call_class->start_finish = call_start_finish;
    base_call_class->accept = call_accept;
    base_call_class->accept_finish = call_accept_finish;
    base_call_class->hangup = call_hangup;
    base_call_class->hangup_finish = call_hangup_finish;
    base_call_class->send_dtmf = call_send_dtmf;
    base_call_class->send_dtmf_finish = call_send_dtmf_finish;
}
