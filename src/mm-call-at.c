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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2019 Purism SPC
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

#include "mm-call-at.h"
#include "mm-base-modem-at.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-log-object.h"
#include "mm-bind.h"

G_DEFINE_TYPE (MMCallAt, mm_call_at, MM_TYPE_BASE_CALL)

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED,
} FeatureSupport;

struct _MMCallAtPrivate {
    /* The modem which owns this call */
    MMBaseModem *modem;

    /* DTMF support */
    FeatureSupport vtd_supported;
};

/*****************************************************************************/
/* Start the CALL */

static gboolean
call_start_finish (MMBaseCall *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
call_start_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;
    const gchar *response = NULL;

    response = mm_base_modem_at_command_finish (modem, res, &error);

    /* check response for error */
    if (response && response[0])
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't start the call: Unhandled response '%s'", response);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
call_start (MMBaseCall          *_self,
            GCancellable        *cancellable,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
    MMCallAt       *self = MM_CALL_AT (_self);
    GError         *error = NULL;
    GTask          *task;
    gchar          *cmd;
    MMIfacePortAt  *port;

    task = g_task_new (self, NULL, callback, user_data);

    port = mm_base_modem_peek_best_at_port (self->priv->modem, &error);
    if (!port) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    cmd = g_strdup_printf ("ATD%s;", mm_gdbus_call_get_number (MM_GDBUS_CALL (self)));
    mm_base_modem_at_command_full (self->priv->modem,
                                   port,
                                   cmd,
                                   90,
                                   FALSE, /* no cached */
                                   FALSE, /* no raw */
                                   cancellable,
                                   (GAsyncReadyCallback)call_start_ready,
                                   task);
    g_free (cmd);
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
call_accept_ready (MMBaseModem  *modem,
                   GAsyncResult *res,
                   GTask        *task)
{
    GError      *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (modem, res, &error);

    /* check response for error */
    if (response && response[0])
        g_set_error (&error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't accept the call: Unhandled response '%s'", response);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
call_accept (MMBaseCall          *_self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    MMCallAt *self = MM_CALL_AT (_self);
    GTask    *task;

    task = g_task_new (self, NULL, callback, user_data);
    mm_base_modem_at_command (self->priv->modem,
                              "ATA",
                              2,
                              FALSE,
                              (GAsyncReadyCallback)call_accept_ready,
                              task);
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

static void
call_deflect_ready (MMBaseModem  *modem,
                    GAsyncResult *res,
                    GTask        *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
call_deflect (MMBaseCall          *_self,
              const gchar         *number,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    MMCallAt *self = MM_CALL_AT (_self);
    GTask    *task;
    gchar    *cmd;

    task = g_task_new (self, NULL, callback, user_data);

    cmd = g_strdup_printf ("+CTFR=%s", number);
    mm_base_modem_at_command (self->priv->modem,
                              cmd,
                              20,
                              FALSE,
                              (GAsyncReadyCallback)call_deflect_ready,
                              task);
    g_free (cmd);
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
chup_ready (MMBaseModem  *modem,
            GAsyncResult *res,
            GTask        *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
chup_fallback (GTask *task)
{
    MMCallAt *self;

    self = MM_CALL_AT (g_task_get_source_object (task));
    mm_base_modem_at_command (self->priv->modem,
                              "+CHUP",
                              2,
                              FALSE,
                              (GAsyncReadyCallback)chup_ready,
                              task);
}

static void
chld_hangup_ready (MMBaseModem  *modem,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMBaseCall *self;
    GError     *error = NULL;

    self = g_task_get_source_object (task);

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't hangup single call with call id '%u': %s",
                     mm_base_call_get_index (MM_BASE_CALL (self)), error->message);
        g_error_free (error);
        chup_fallback (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
call_hangup (MMBaseCall          *_self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    MMCallAt *self = MM_CALL_AT (_self);
    GTask    *task;
    guint     index;

    task = g_task_new (self, NULL, callback, user_data);

    /* Try to hangup the single call id */
    index = mm_base_call_get_index (MM_BASE_CALL (self));
    if (index) {
        gchar *cmd;

        cmd = g_strdup_printf ("+CHLD=1%u", index);
        mm_base_modem_at_command (self->priv->modem,
                                  cmd,
                                  2,
                                  FALSE,
                                  (GAsyncReadyCallback)chld_hangup_ready,
                                  task);
        g_free (cmd);
        return;
    }

    /* otherwise terminate all */
    chup_fallback (task);
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

static void
call_send_dtmf_ready (MMBaseModem  *modem,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBaseCall *self;
    GError     *error = NULL;

    self = g_task_get_source_object (task);

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_obj_dbg (self, "couldn't send dtmf: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* We sent one character */
    g_task_return_int (task, 1);
    g_object_unref (task);
}

static void
send_dtmf_digit (MMCallAt    *self,
                 GTask       *task,
                 const gchar  dtmf_digit)
{
    g_autofree gchar *cmd = NULL;

    cmd = g_strdup_printf ("AT+VTS=%c", dtmf_digit);
    mm_base_modem_at_command (self->priv->modem,
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)call_send_dtmf_ready,
                              task);
}

static void
call_dtmf_vtd_ready (MMBaseModem  *modem,
                     GAsyncResult *res,
                     GTask        *task)
{
    MMCallAt          *self;
    g_autoptr(GError)  error = NULL;
    gchar              dtmf_digit;

    self = g_task_get_source_object (task);

    mm_base_modem_at_command_finish (modem, res, &error);
    self->priv->vtd_supported = error ? FEATURE_NOT_SUPPORTED : FEATURE_SUPPORTED;

    dtmf_digit = (gchar) GPOINTER_TO_UINT (g_task_get_task_data (task));
    send_dtmf_digit (self, task, dtmf_digit);
}

static void
call_send_dtmf (MMBaseCall *_self,
                const gchar *dtmf,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    MMCallAt         *self = MM_CALL_AT (_self);
    GTask            *task;
    g_autofree gchar *cmd = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->vtd_supported == FEATURE_NOT_SUPPORTED) {
        send_dtmf_digit (self, task, dtmf[0]);
        return;
    }

    g_task_set_task_data (task, GUINT_TO_POINTER (dtmf[0]), NULL);

    /* Otherwise try to set duration */
    cmd = g_strdup_printf ("AT+VTD=%u", mm_base_call_get_dtmf_tone_duration (_self));
    mm_base_modem_at_command (self->priv->modem,
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)call_dtmf_vtd_ready,
                              task);
}

/*****************************************************************************/

MMBaseCall *
mm_call_at_new (MMBaseModem     *modem,
                GObject         *bind_to,
                MMCallDirection  direction,
                const gchar     *number,
                const guint      dtmf_tone_duration,
                gboolean         skip_incoming_timeout,
                gboolean         supports_dialing_to_ringing,
                gboolean         supports_ringing_to_active)
{
    MMBaseCall *call;

    call = MM_BASE_CALL (g_object_new (MM_TYPE_CALL_AT,
                                       MM_BASE_CALL_IFACE_MODEM_VOICE,           modem,
                                       MM_BIND_TO,                               bind_to,
                                       MM_CALL_DIRECTION,                        direction,
                                       MM_CALL_NUMBER,                           number,
                                       MM_CALL_DTMF_TONE_DURATION,               dtmf_tone_duration,
                                       MM_BASE_CALL_SKIP_INCOMING_TIMEOUT,       skip_incoming_timeout,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, supports_dialing_to_ringing,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  supports_ringing_to_active,
                                       NULL));
    MM_CALL_AT (call)->priv->modem = g_object_ref (modem);
    return call;
}

static void
mm_call_at_init (MMCallAt *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CALL_AT, MMCallAtPrivate);
}

static void
dispose (GObject *object)
{
    MMCallAt *self = MM_CALL_AT (object);

    g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_call_at_parent_class)->dispose (object);
}

static void
mm_call_at_class_init (MMCallAtClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCallAtPrivate));

    object_class->dispose = dispose;

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
