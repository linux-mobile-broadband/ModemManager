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
#include "mm-bind.h"

G_DEFINE_TYPE (MMCallQmi, mm_call_qmi, MM_TYPE_BASE_CALL)

struct _MMCallQmiPrivate {
    /* The modem which owns this call */
    MMBaseModem *modem;
};

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

    port = mm_broadband_modem_qmi_peek_port_qmi (MM_BROADBAND_MODEM_QMI (self->priv->modem));
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
/* DTMF handling */

static gboolean
get_client_and_call_id (MMCallQmi            *self,
                        GAsyncReadyCallback   callback,
                        gpointer              user_data,
                        QmiClient           **client,
                        guint                *call_id)
{
    g_return_val_if_fail (client, FALSE);
    g_return_val_if_fail (call_id, FALSE);

    /* Ensure Voice client */
    if (!ensure_qmi_client (self,
                            QMI_SERVICE_VOICE, client,
                            callback, user_data))
        return FALSE;

    *call_id = mm_base_call_get_index (MM_BASE_CALL (self));
    if (*call_id == 0) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 (gpointer) __func__,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_INVALID_ARGS,
                                 "Invalid call index");
        return FALSE;
    }

    return TRUE;
}

static gboolean
call_stop_dtmf_finish (MMBaseCall    *call,
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
        g_prefix_error (&error, "Couldn't stop DTMF character: ");
        g_task_return_error (task, error);
    } else {
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
call_stop_dtmf (MMBaseCall          *_self,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    MMCallQmi       *self = MM_CALL_QMI (_self);
    GTask           *task;
    QmiClient       *client = NULL;
    guint            call_id = 0;
    GError          *error = NULL;
    g_autoptr (QmiMessageVoiceStopContinuousDtmfInput) input = NULL;

    if (!get_client_and_call_id (self,
                                 callback,
                                 user_data,
                                 &client,
                                 &call_id))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    input = qmi_message_voice_stop_continuous_dtmf_input_new ();
    if (!qmi_message_voice_stop_continuous_dtmf_input_set_data (input,
                                                                call_id,
                                                                &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Stop sending DTMF tone */
    qmi_client_voice_stop_continuous_dtmf (QMI_CLIENT_VOICE (client),
                                           input,
                                           5,
                                           NULL,
                                           (GAsyncReadyCallback) voice_stop_continuous_dtmf_ready,
                                           task);
}

static gssize
call_send_dtmf_finish (MMBaseCall    *call,
                       GAsyncResult  *res,
                       GError       **error)
{
    return g_task_propagate_int (G_TASK (res), error);
}

static void
voice_start_continuous_dtmf_ready (QmiClientVoice *client,
                                   GAsyncResult   *res,
                                   GTask          *task)
{
    g_autoptr (QmiMessageVoiceStartContinuousDtmfOutput)  output = NULL;
    GError                                               *error = NULL;

    output = qmi_client_voice_start_continuous_dtmf_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_task_return_error (task, error);
    } else if (!qmi_message_voice_start_continuous_dtmf_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't send DTMF character: ");
        g_task_return_error (task, error);
    } else {
        g_task_return_int (task, 1);
    }
    g_object_unref (task);
}

static void
call_send_dtmf (MMBaseCall          *_self,
                const gchar         *dtmf,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    MMCallQmi       *self = MM_CALL_QMI (_self);
    GTask           *task;
    QmiClient       *client = NULL;
    guint            call_id = 0;
    GError          *error = NULL;
    g_autoptr (QmiMessageVoiceStartContinuousDtmfInput) input = NULL;

    if (!get_client_and_call_id (self,
                                 callback,
                                 user_data,
                                 &client,
                                 &call_id))
        return;

    task = g_task_new (self, NULL, callback, user_data);

    input = qmi_message_voice_start_continuous_dtmf_input_new ();
    if (!qmi_message_voice_start_continuous_dtmf_input_set_data (input,
                                                                 call_id,
                                                                 dtmf[0],
                                                                 &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Send DTMF character as ASCII number */
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
                 const gchar     *number,
                 const guint      dtmf_tone_duration)
{
    MMBaseCall  *call;

    call = MM_BASE_CALL (g_object_new (MM_TYPE_CALL_QMI,
                                       MM_BASE_CALL_IFACE_MODEM_VOICE,           modem,
                                       MM_BIND_TO,                               modem,
                                       MM_CALL_DIRECTION,                        direction,
                                       MM_CALL_NUMBER,                           number,
                                       MM_CALL_DTMF_TONE_DURATION,               dtmf_tone_duration,
                                       MM_BASE_CALL_SKIP_INCOMING_TIMEOUT,       TRUE,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, TRUE,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  TRUE,
                                       NULL));
    MM_CALL_QMI (call)->priv->modem = g_object_ref (modem);
    return call;
}

static void
mm_call_qmi_init (MMCallQmi *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CALL_QMI, MMCallQmiPrivate);
}

static void
dispose (GObject *object)
{
    MMCallQmi *self = MM_CALL_QMI (object);

    g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_call_qmi_parent_class)->dispose (object);
}

static void
mm_call_qmi_class_init (MMCallQmiClass *klass)
{
    GObjectClass    *object_class = G_OBJECT_CLASS (klass);
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCallQmiPrivate));

    object_class->dispose = dispose;

    base_call_class->start = call_start;
    base_call_class->start_finish = call_start_finish;
    base_call_class->accept = call_accept;
    base_call_class->accept_finish = call_accept_finish;
    base_call_class->hangup = call_hangup;
    base_call_class->hangup_finish = call_hangup_finish;
    base_call_class->send_dtmf = call_send_dtmf;
    base_call_class->send_dtmf_finish = call_send_dtmf_finish;
    base_call_class->stop_dtmf = call_stop_dtmf;
    base_call_class->stop_dtmf_finish = call_stop_dtmf_finish;
}
