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

#include "mm-base-call.h"
#include "mm-broadband-modem.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-voice.h"
#include "mm-base-modem-at.h"
#include "mm-base-modem.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMBaseCall, mm_base_call, MM_GDBUS_TYPE_CALL_SKELETON)

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_MODEM,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseCallPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    /* The modem which owns this call */
    MMBaseModem *modem;
    /* The path where the call object is exported */
    gchar *path;
};

/*****************************************************************************/
/* Start call (DBus call handling) */

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GDBusMethodInvocation *invocation;
} HandleStartContext;

static void
handle_start_context_free (HandleStartContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_start_ready (MMBaseCall *self,
                    GAsyncResult *res,
                    HandleStartContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->start_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        /* Transition from Unknown->Dialing */
        if (mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self)) == MM_CALL_STATE_UNKNOWN ) {
            /* Update state */
            mm_base_call_change_state (self, MM_CALL_STATE_DIALING, MM_CALL_STATE_REASON_OUTGOING_STARTED);
        }
        mm_gdbus_call_complete_start (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    }

    handle_start_context_free (ctx);
}

static void
handle_start_auth_ready (MMBaseModem *modem,
                         GAsyncResult *res,
                         HandleStartContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_start_context_free (ctx);
        return;
    }

    /* We can only start call created by the user */
    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    if (state != MM_CALL_STATE_UNKNOWN) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_FAILED,
                                               "This call was not in unknown state, cannot start it");
        handle_start_context_free (ctx);
        return;
    }

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->start ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->start_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Starting call is not supported by this modem");
        handle_start_context_free (ctx);
        return;
    }

    MM_BASE_CALL_GET_CLASS (ctx->self)->start (ctx->self,
                                               (GAsyncReadyCallback)handle_start_ready,
                                               ctx);
}

static gboolean
handle_start (MMBaseCall *self,
              GDBusMethodInvocation *invocation)
{
    HandleStartContext *ctx;

    ctx = g_new0 (HandleStartContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    g_object_get (self,
                  MM_BASE_CALL_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_start_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Accept call (DBus call handling) */

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GDBusMethodInvocation *invocation;
} HandleAcceptContext;

static void
handle_accept_context_free (HandleAcceptContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_accept_ready (MMBaseCall *self,
                   GAsyncResult *res,
                   HandleAcceptContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->accept_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        /* Transition from Unknown->Dialing */
        if (mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self)) == MM_CALL_STATE_RINGING_IN) {
            /* Update state */
            mm_base_call_change_state (self, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
        }
        mm_gdbus_call_complete_accept (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    }

    handle_accept_context_free (ctx);
}

static void
handle_accept_auth_ready (MMBaseModem *modem,
                          GAsyncResult *res,
                          HandleAcceptContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_accept_context_free (ctx);
        return;
    }

    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    /* We can only accept incoming call in ringing state */
    if (state != MM_CALL_STATE_RINGING_IN) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_FAILED,
                                               "This call was not ringing, cannot accept  ");
        handle_accept_context_free (ctx);
        return;
    }

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->accept ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->accept_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Accepting call is not supported by this modem");
        handle_accept_context_free (ctx);
        return;
    }

    MM_BASE_CALL_GET_CLASS (ctx->self)->accept (ctx->self,
                                                (GAsyncReadyCallback)handle_accept_ready,
                                                ctx);
}

static gboolean
handle_accept (MMBaseCall *self,
               GDBusMethodInvocation *invocation)
{
    HandleAcceptContext *ctx;

    ctx = g_new0 (HandleAcceptContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    g_object_get (self,
                  MM_BASE_CALL_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_accept_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

/* Hangup call (DBus call handling) */

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GDBusMethodInvocation *invocation;
} HandleHangupContext;

static void
handle_hangup_context_free (HandleHangupContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_hangup_ready (MMBaseCall *self,
                     GAsyncResult *res,
                     HandleHangupContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->hangup_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        /* Transition from Unknown->Dialing */
        if (mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self)) != MM_CALL_STATE_TERMINATED ||
            mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self)) != MM_CALL_STATE_UNKNOWN) {
            /* Update state */
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);
        }
        mm_gdbus_call_complete_hangup (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    }

    handle_hangup_context_free (ctx);
}

static void
handle_hangup_auth_ready (MMBaseModem *modem,
                          GAsyncResult *res,
                          HandleHangupContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_hangup_context_free (ctx);
        return;
    }

    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    /* We can only hangup call in a valid state */
    if (state == MM_CALL_STATE_TERMINATED || state == MM_CALL_STATE_UNKNOWN) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_FAILED,
                                               "This call was not active, cannot hangup");
        handle_hangup_context_free (ctx);
        return;
    }

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->hangup ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->hangup_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Hanguping call is not supported by this modem");
        handle_hangup_context_free (ctx);
        return;
    }

    MM_BASE_CALL_GET_CLASS (ctx->self)->hangup (ctx->self,
                                                (GAsyncReadyCallback)handle_hangup_ready,
                                                ctx);
}

static gboolean
handle_hangup (MMBaseCall *self,
               GDBusMethodInvocation *invocation)
{
    HandleHangupContext *ctx;

    ctx = g_new0 (HandleHangupContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    g_object_get (self,
                  MM_BASE_CALL_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_hangup_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Send dtmf (DBus call handling) */

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GDBusMethodInvocation *invocation;
    gchar *dtmf;
} HandleSendDtmfContext;

static void
handle_send_dtmf_context_free (HandleSendDtmfContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free(ctx->dtmf);
    g_free (ctx);
}

static void
handle_send_dtmf_ready (MMBaseCall *self,
                        GAsyncResult *res,
                        HandleSendDtmfContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->send_dtmf_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        mm_gdbus_call_complete_send_dtmf (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    }

    handle_send_dtmf_context_free (ctx);
}

static void
handle_send_dtmf_auth_ready (MMBaseModem *modem,
                             GAsyncResult *res,
                             HandleSendDtmfContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_send_dtmf_context_free (ctx);
        return;
    }

    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->send_dtmf ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->send_dtmf_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Sending dtmf is not supported by this modem");
        handle_send_dtmf_context_free (ctx);
        return;
    }

    /* We can only send_dtmf when call is in ACTIVE state */
    if (state != MM_CALL_STATE_ACTIVE ){
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_FAILED,
                                               "This call was not active, cannot send dtmf");
        handle_send_dtmf_context_free (ctx);
        return;
    }

    MM_BASE_CALL_GET_CLASS (ctx->self)->send_dtmf (ctx->self, ctx->dtmf,
                                                   (GAsyncReadyCallback)handle_send_dtmf_ready,
                                                   ctx);
}

static gboolean
handle_send_dtmf (MMBaseCall *self,
                  GDBusMethodInvocation *invocation,
                  const gchar *dtmf)
{
    HandleSendDtmfContext *ctx;

    ctx = g_new0 (HandleSendDtmfContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);

    ctx->dtmf = g_strdup(dtmf);
    g_object_get (self,
                  MM_BASE_CALL_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_send_dtmf_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

void
mm_base_call_export (MMBaseCall *self)
{
    static guint id = 0;
    gchar *path;

    path = g_strdup_printf (MM_DBUS_CALL_PREFIX "/%d", id++);
    g_object_set (self,
                  MM_BASE_CALL_PATH, path,
                  NULL);
    g_free (path);
}

void
mm_base_call_unexport (MMBaseCall *self)
{
    g_object_set (self,
                  MM_BASE_CALL_PATH, NULL,
                  NULL);
}

/*****************************************************************************/

static void
call_dbus_export (MMBaseCall *self)
{
    GError *error = NULL;

    /* Handle method invocations */
    g_signal_connect (self,
                      "handle-start",
                      G_CALLBACK (handle_start),
                      NULL);
    g_signal_connect (self,
                      "handle-accept",
                      G_CALLBACK (handle_accept),
                      NULL);
    g_signal_connect (self,
                      "handle-hangup",
                      G_CALLBACK (handle_hangup),
                      NULL);
    g_signal_connect (self,
                      "handle-send-dtmf",
                      G_CALLBACK (handle_send_dtmf),
                      NULL);


    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_warn ("couldn't export call at '%s': '%s'",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
}

static void
call_dbus_unexport (MMBaseCall *self)
{
    /* Only unexport if currently exported */
    if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
}

/*****************************************************************************/

const gchar *
mm_base_call_get_path (MMBaseCall *self)
{
    return self->priv->path;
}

void
mm_base_call_change_state(MMBaseCall *self, MMCallState new_state, MMCallStateReason reason)
{
    int old_state = mm_gdbus_call_get_state (MM_GDBUS_CALL (self));

    g_object_set (self,
                  "state",          new_state,
                  "state-reason",   reason,
                  NULL);

    mm_gdbus_call_set_state (MM_GDBUS_CALL (self), new_state);
    mm_gdbus_call_set_state_reason(MM_GDBUS_CALL (self), reason);

    mm_gdbus_call_emit_state_changed(MM_GDBUS_CALL (self),
                                     old_state,
                                     new_state,
                                     reason);
}

void mm_base_call_received_dtmf  (MMBaseCall *self, gchar *dtmf)
{
    mm_gdbus_call_emit_dtmf_received(MM_GDBUS_CALL (self), dtmf);
}

/*****************************************************************************/
/* Start the CALL */

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GSimpleAsyncResult *result;
} CallStartContext;

static void
call_start_context_complete_and_free (CallStartContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
call_start_finish (MMBaseCall *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
call_start_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  CallStartContext *ctx)
{
    GError *error = NULL;
    const gchar *response = NULL;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            /* something is wrong, serial timeout could never occurs */
        }

        if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_DIALTONE)) {
            /* Update state */
            mm_base_call_change_state(ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
        }

        if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_BUSY)          ||
            g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_ANSWER)     ||
            g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_CARRIER)    )
        {
            /* Update state */
            mm_base_call_change_state(ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
        }

        mm_dbg ("Couldn't start call : '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        call_start_context_complete_and_free (ctx);
        return;
    }

    /* check response for error */
    if (response && strlen (response) > 0 ) {
        g_set_error (&error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't start the call: "
                             "Modem response '%s'", response);
        /* Update state */
        mm_base_call_change_state(ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
    } else {
        /* Update state */
        mm_base_call_change_state(ctx->self, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
    }

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        call_start_context_complete_and_free (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    call_start_context_complete_and_free (ctx);
}

static void
call_start (MMBaseCall *self,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    CallStartContext *ctx;
    gchar *cmd;

    /* Setup the context */
    ctx = g_new0 (CallStartContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             call_start);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (self->priv->modem);

    cmd = g_strdup_printf ("ATD%s;", mm_gdbus_call_get_number (MM_GDBUS_CALL (self)) );
    mm_base_modem_at_command (ctx->modem,
                              cmd,
                              90,
                              FALSE,
                              (GAsyncReadyCallback)call_start_ready,
                              ctx);

    /* Update state */
    mm_base_call_change_state(self, MM_CALL_STATE_RINGING_OUT, MM_CALL_STATE_REASON_OUTGOING_STARTED);
    g_free (cmd);
}

/*****************************************************************************/

/* Accept the CALL */

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GSimpleAsyncResult *result;
} CallAcceptContext;

static void
call_accept_context_complete_and_free (CallAcceptContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
call_accept_finish (MMBaseCall *self,
                    GAsyncResult *res,
                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
call_accept_ready (MMBaseModem *modem,
                   GAsyncResult *res,
                   CallAcceptContext *ctx)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            g_simple_async_result_take_error (ctx->result, error);
            call_accept_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Couldn't accept call : '%s'", error->message);
        g_error_free (error);
        return;
    }

    /* check response for error */
    if( response && strlen(response) > 0 ) {
        g_set_error (&error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Couldn't accept the call: "
                                     "Unhandled response '%s'", response);

        /* Update state */
        mm_base_call_change_state(ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
    } else {

        /* Update state */
        mm_base_call_change_state(ctx->self, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
    }

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        call_accept_context_complete_and_free (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    call_accept_context_complete_and_free (ctx);
}

static void
call_accept (MMBaseCall *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    CallAcceptContext *ctx;
    gchar *cmd;

    /* Setup the context */
    ctx = g_new0 (CallAcceptContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             call_accept);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (self->priv->modem);

    cmd = g_strdup_printf ("ATA");
    mm_base_modem_at_command (ctx->modem,
                              cmd,
                              2,
                              FALSE,
                              (GAsyncReadyCallback)call_accept_ready,
                              ctx);
    g_free (cmd);
}

/*****************************************************************************/

/* Hangup the CALL */

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GSimpleAsyncResult *result;
} CallHangupContext;

static void
call_hangup_context_complete_and_free (CallHangupContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
call_hangup_finish (MMBaseCall *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
call_hangup_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  CallHangupContext *ctx)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            g_simple_async_result_take_error (ctx->result, error);
            call_hangup_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Couldn't hangup call : '%s'", error->message);
        g_error_free (error);
        return;
    }

    /* Update state */
    mm_base_call_change_state(ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        call_hangup_context_complete_and_free (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    call_hangup_context_complete_and_free (ctx);
}

static void
call_hangup (MMBaseCall *self,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
    CallHangupContext *ctx;
    gchar *cmd;

    /* Setup the context */
    ctx = g_new0 (CallHangupContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             call_hangup);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (self->priv->modem);

    cmd = g_strdup_printf ("+CHUP");
    mm_base_modem_at_command (ctx->modem,
                              cmd,
                              2,
                              FALSE,
                              (GAsyncReadyCallback)call_hangup_ready,
                              ctx);
    g_free (cmd);
}

/*****************************************************************************/
/* Send DTMF tone to call */

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GSimpleAsyncResult *result;
} CallSendDtmfContext;

static void
call_send_dtmf_context_complete_and_free (CallSendDtmfContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
call_send_dtmf_finish (MMBaseCall *self,
                       GAsyncResult *res,
                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
call_send_dtmf_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      CallSendDtmfContext *ctx)
{
    GError *error = NULL;
    const gchar *response = NULL;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_dbg ("Couldn't send_dtmf: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        call_send_dtmf_context_complete_and_free (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    call_send_dtmf_context_complete_and_free (ctx);
}

static void
call_send_dtmf (MMBaseCall *self,
                const gchar *dtmf,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    CallSendDtmfContext *ctx;
    gchar *cmd;

    /* Setup the context */
    ctx = g_new0 (CallSendDtmfContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             call_send_dtmf);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (self->priv->modem);

    cmd = g_strdup_printf ("AT+VTS=%c", dtmf[0]);
    mm_base_modem_at_command (ctx->modem,
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)call_send_dtmf_ready,
                              ctx);

    g_free (cmd);
}

/*****************************************************************************/
typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GSimpleAsyncResult *result;
} CallDeleteContext;

static void
call_delete (MMBaseCall *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    CallDeleteContext *ctx;

    ctx = g_new0 (CallDeleteContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             call_delete);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (self->priv->modem);

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
call_delete_finish (MMBaseCall *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

/*****************************************************************************/

gboolean
mm_base_call_delete_finish (MMBaseCall *self,
                            GAsyncResult *res,
                            GError **error)
{
    if (MM_BASE_CALL_GET_CLASS (self)->delete_finish) {
        gboolean deleted;

        deleted = MM_BASE_CALL_GET_CLASS (self)->delete_finish (self, res, error);
        if (deleted)
            /* We do change the state of this call back to UNKNOWN */
            mm_base_call_change_state(self, MM_CALL_STATE_UNKNOWN, MM_CALL_STATE_REASON_UNKNOWN);

        return deleted;
    }

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_base_call_delete (MMBaseCall *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    if (MM_BASE_CALL_GET_CLASS (self)->delete &&
        MM_BASE_CALL_GET_CLASS (self)->delete_finish) {
        MM_BASE_CALL_GET_CLASS (self)->delete (self, callback, user_data);
        return;
    }

    g_simple_async_report_error_in_idle (G_OBJECT (self),
                                         callback,
                                         user_data,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Deleting call is not supported by this modem");
}

/*****************************************************************************/

MMBaseCall *
mm_base_call_new (MMBaseModem *modem)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_BASE_CALL,
                                      MM_BASE_CALL_MODEM, modem,
                                      NULL));
}

MMBaseCall *
mm_base_call_new_from_properties (MMBaseModem *modem,
                                 MMCallProperties *properties,
                                 GError **error)
{
    MMBaseCall *self;
    const gchar *number;
    MMCallDirection direction;

    g_assert (MM_IS_IFACE_MODEM_VOICE (modem));

    number      = mm_call_properties_get_number (properties);
    direction   = mm_call_properties_get_direction(properties);

    /* Don't create CALL from properties if either number is missing */
    if ( !number ) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create call: mandatory parameter 'number' is missing");
        return NULL;
    }

    /* if no direction is specified force to outgoing */
    if(direction == MM_CALL_DIRECTION_UNKNOWN ) {
        direction = MM_CALL_DIRECTION_OUTGOING;
    }

    /* Create a call object as defined by the interface */
    self = mm_iface_modem_voice_create_call (MM_IFACE_MODEM_VOICE (modem));
    g_object_set (self,
                  "state",          mm_call_properties_get_state(properties),
                  "state-reason",   mm_call_properties_get_state_reason(properties),
                  "direction",      direction,
                  "number",         number,
                  NULL);

    /* Only export once properly created */
    mm_base_call_export (self);

    return self;
}

/*****************************************************************************/

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBaseCall *self = MM_BASE_CALL (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->path)
            call_dbus_unexport (self);
        else if (self->priv->connection)
            call_dbus_export (self);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->connection)
            call_dbus_unexport (self);
        else if (self->priv->path)
            call_dbus_export (self);
        break;
    case PROP_MODEM:
        g_clear_object (&self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
        if (self->priv->modem) {
            /* Bind the modem's connection (which is set when it is exported,
             * and unset when unexported) to the call's connection */
            g_object_bind_property (self->priv->modem, MM_BASE_MODEM_CONNECTION,
                                    self, MM_BASE_CALL_CONNECTION,
                                    G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBaseCall *self = MM_BASE_CALL (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_base_call_init (MMBaseCall *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BASE_CALL, MMBaseCallPrivate);
    /* Defaults */
}

static void
finalize (GObject *object)
{
    MMBaseCall *self = MM_BASE_CALL (object);

    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_base_call_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseCall *self = MM_BASE_CALL (object);

    if (self->priv->connection) {
        /* If we arrived here with a valid connection, make sure we unexport
         * the object */
        call_dbus_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_base_call_parent_class)->dispose (object);
}

static void
mm_base_call_class_init (MMBaseCallClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBaseCallPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    klass->start            = call_start;
    klass->start_finish     = call_start_finish;
    klass->accept           = call_accept;
    klass->accept_finish    = call_accept_finish;
    klass->hangup           = call_hangup;
    klass->hangup_finish    = call_hangup_finish;
    klass->delete           = call_delete;
    klass->delete_finish    = call_delete_finish;
    klass->send_dtmf        = call_send_dtmf;
    klass->send_dtmf_finish = call_send_dtmf_finish;


    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BASE_CALL_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_BASE_CALL_PATH,
                             "Path",
                             "DBus path of the call",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_BASE_CALL_MODEM,
                             "Modem",
                             "The Modem which owns this call",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);
}
