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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "fake-call.h"

G_DEFINE_TYPE (MMFakeCall, mm_fake_call, MM_TYPE_BASE_CALL)

/*****************************************************************************/
/* Start the CALL */

static gboolean
call_start_finish (MMBaseCall *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
call_start_ready (GTask *task)
{
    MMFakeCall *self;

    self = g_task_get_source_object (task);

    self->priv->idle_id = 0;

    if (self->priv->start_error_msg) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "%s",
                                 self->priv->start_error_msg);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
call_start (MMBaseCall          *_self,
            GCancellable        *cancellable,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
    MMFakeCall *self = MM_FAKE_CALL (_self);
    GTask      *task;

    g_assert_cmpint (self->priv->idle_id, ==, 0);

    task = g_task_new (self, NULL, callback, user_data);
    self->priv->idle_id = g_idle_add ((GSourceFunc) call_start_ready, task);
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

static gboolean
call_accept_ready (GTask *task)
{
    MMFakeCall *self;

    self = g_task_get_source_object (task);

    self->priv->idle_id = 0;

    if (self->priv->accept_error_msg) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "%s",
                                 self->priv->accept_error_msg);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
call_accept (MMBaseCall          *_self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    MMFakeCall *self = MM_FAKE_CALL (_self);
    GTask    *task;

    g_assert_cmpint (self->priv->idle_id, ==, 0);

    task = g_task_new (self, NULL, callback, user_data);
    self->priv->idle_id = g_idle_add ((GSourceFunc) call_accept_ready, task);
}

/*****************************************************************************/
/* Deflect the call */

static gboolean
call_deflect_finish (MMBaseCall    *self,
                     GAsyncResult  *res,
                     GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
call_deflect_ready (GTask *task)
{
    MMFakeCall *self;

    self = g_task_get_source_object (task);

    self->priv->idle_id = 0;

    if (self->priv->deflect_error_msg) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "%s",
                                 self->priv->deflect_error_msg);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
call_deflect (MMBaseCall          *_self,
              const gchar         *number,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    MMFakeCall *self = MM_FAKE_CALL (_self);
    GTask    *task;

    g_assert_cmpint (self->priv->idle_id, ==, 0);

    task = g_task_new (self, NULL, callback, user_data);
    self->priv->idle_id = g_idle_add ((GSourceFunc) call_deflect_ready, task);
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

static gboolean
call_hangup_ready (GTask *task)
{
    MMFakeCall *self;

    self = g_task_get_source_object (task);

    self->priv->idle_id = 0;

    if (self->priv->hangup_error_msg) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "%s",
                                 self->priv->hangup_error_msg);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
call_hangup (MMBaseCall          *_self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    MMFakeCall *self = MM_FAKE_CALL (_self);
    GTask      *task;

    g_assert_cmpint (self->priv->idle_id, ==, 0);

    task = g_task_new (self, NULL, callback, user_data);
    self->priv->idle_id = g_idle_add ((GSourceFunc) call_hangup_ready, task);
}

/*****************************************************************************/
/* Send DTMF tone to call */

static gssize
call_send_dtmf_finish (MMBaseCall *self,
                       GAsyncResult *res,
                       GError **error)
{
    return g_task_propagate_int (G_TASK (res), error);
}

static gboolean
call_send_dtmf_ready (GTask *task)
{
    MMFakeCall *self;

    self = g_task_get_source_object (task);

    self->priv->idle_id = 0;

    if (self->priv->dtmf_error_msg) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "%s",
                                 self->priv->dtmf_error_msg);
    } else
        g_task_return_int (task, self->priv->dtmf_num_accepted);

    self->priv->dtmf_in_send = FALSE;
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
call_send_dtmf (MMBaseCall *_self,
                const gchar *dtmf,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    MMFakeCall *self = MM_FAKE_CALL (_self);
    GTask      *task;

    g_assert_cmpint (self->priv->idle_id, ==, 0);

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->dtmf_sent)
        self->priv->dtmf_sent = g_string_new ("");
    self->priv->dtmf_num_accepted = MIN (self->priv->dtmf_accept_len, strlen (dtmf));
    g_string_append_len (self->priv->dtmf_sent, dtmf, self->priv->dtmf_num_accepted);

    self->priv->idle_id = g_idle_add ((GSourceFunc) call_send_dtmf_ready, task);
    self->priv->dtmf_in_send = TRUE;
}

/*****************************************************************************/
/* Stop DTMF tone */

static gboolean
call_stop_dtmf_finish (MMBaseCall *self,
                       GAsyncResult *res,
                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
call_stop_dtmf_ready (GTask *task)
{
    MMFakeCall *self;

    self = g_task_get_source_object (task);

    self->priv->idle_id = 0;
    self->priv->dtmf_stop_called = TRUE;

    if (self->priv->dtmf_stop_error_msg) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "%s",
                                 self->priv->dtmf_stop_error_msg);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
call_stop_dtmf (MMBaseCall *_self,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    MMFakeCall *self = MM_FAKE_CALL (_self);
    GTask      *task;

    g_assert_cmpint (self->priv->idle_id, ==, 0);
    g_assert_false (self->priv->dtmf_in_send);

    task = g_task_new (self, NULL, callback, user_data);

    self->priv->idle_id = g_idle_add ((GSourceFunc) call_stop_dtmf_ready, task);
}

void
mm_fake_call_enable_dtmf_stop (MMFakeCall *self,
                               gboolean    enable)
{
    MMBaseCallClass *base_call_class = MM_BASE_CALL_GET_CLASS (self);

    base_call_class->stop_dtmf        = enable ? call_stop_dtmf : NULL;
    base_call_class->stop_dtmf_finish = enable ? call_stop_dtmf_finish : NULL;
}

/*****************************************************************************/

MMFakeCall *
mm_fake_call_new (GDBusConnection   *connection,
                  MMIfaceModemVoice *voice,
                  MMCallDirection    direction,
                  const gchar       *number,
                  const guint        dtmf_tone_duration)
{
    MMFakeCall *call;

    call = MM_FAKE_CALL (g_object_new (MM_TYPE_FAKE_CALL,
                                       MM_BASE_CALL_CONNECTION,                  connection,
                                       MM_BASE_CALL_IFACE_MODEM_VOICE,           voice,
                                       MM_CALL_DIRECTION,                        direction,
                                       MM_CALL_NUMBER,                           number,
                                       MM_CALL_DTMF_TONE_DURATION,               dtmf_tone_duration,
                                       MM_BASE_CALL_SKIP_INCOMING_TIMEOUT,       TRUE,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, TRUE,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  TRUE,
                                       NULL));
    return call;
}

static void
mm_fake_call_init (MMFakeCall *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_FAKE_CALL, MMFakeCallPrivate);
}

static void
dispose (GObject *object)
{
    MMFakeCall *self = MM_FAKE_CALL (object);

    if (self->priv->idle_id)
        g_source_remove (self->priv->idle_id);
    self->priv->idle_id = 0;

    if (self->priv->dtmf_sent) {
        g_string_free (self->priv->dtmf_sent, TRUE);
        self->priv->dtmf_sent = NULL;
    }

    G_OBJECT_CLASS (mm_fake_call_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    G_OBJECT_CLASS (mm_fake_call_parent_class)->finalize (object);
}

static void
mm_fake_call_class_init (MMFakeCallClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMFakeCallPrivate));

    object_class->dispose = dispose;
    object_class->finalize = finalize;

    base_call_class->start            = call_start;
    base_call_class->start_finish     = call_start_finish;
    base_call_class->accept           = call_accept;
    base_call_class->accept_finish    = call_accept_finish;
    base_call_class->deflect          = call_deflect;
    base_call_class->deflect_finish   = call_deflect_finish;
    base_call_class->hangup           = call_hangup;
    base_call_class->hangup_finish    = call_hangup_finish;
    base_call_class->send_dtmf        = call_send_dtmf;
    base_call_class->send_dtmf_finish = call_send_dtmf_finish;
}
