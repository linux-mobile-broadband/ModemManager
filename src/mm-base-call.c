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

#include "mm-base-call.h"
#include "mm-broadband-modem.h"
#include "mm-auth-provider.h"
#include "mm-iface-modem-voice.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-bind.h"

static void log_object_iface_init (MMLogObjectInterface *iface);
static void bind_iface_init (MMBindInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMBaseCall, mm_base_call, MM_GDBUS_TYPE_CALL_SKELETON, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_BIND, bind_iface_init))

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_BIND_TO,
    PROP_IFACE_MODEM_VOICE,
    PROP_SKIP_INCOMING_TIMEOUT,
    PROP_SUPPORTS_DIALING_TO_RINGING,
    PROP_SUPPORTS_RINGING_TO_ACTIVE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseCallPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    guint            dbus_id;

    /* The authorization provider */
    MMAuthProvider *authp;
    GCancellable   *authp_cancellable;

    /* The object this Call is bound to */
    GObject *bind_to;

    /* The voice interface which owns this call */
    MMIfaceModemVoice *iface;
    /* The path where the call object is exported */
    gchar *path;
    /* Features */
    gboolean skip_incoming_timeout;
    gboolean supports_dialing_to_ringing;
    gboolean supports_ringing_to_active;

    guint incoming_timeout;

    /* The port used for audio while call is ongoing, if known */
    MMPort *audio_port;

    /* Ongoing call index */
    guint index;

    /* Start cancellable, used when the call state transition to
     * 'terminated' is coming asynchronously (e.g. via in-call state
     * update notifications) */
    GCancellable *start_cancellable;

    /* DTMF support */
    GQueue *dtmf_queue;
};

/*****************************************************************************/
/* Incoming calls are reported via RING URCs. If the caller stops the call
 * attempt before it has been answered, the only thing we would see is that the
 * URCs are no longer received. So, we will start a timeout whenever a new RING
 * URC is received, and we refresh the timeout any time a new URC arrives. If
 * the timeout is expired (meaning no URCs were received in the last N seconds)
 * then we assume the call attempt is finished and we transition to TERMINATED.
 */

#define INCOMING_TIMEOUT_SECS 10

static gboolean
incoming_timeout_cb (MMBaseCall *self)
{
    self->priv->incoming_timeout = 0;
    mm_obj_msg (self, "incoming call timed out: no response");
    mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);
    return G_SOURCE_REMOVE;
}

void
mm_base_call_incoming_refresh (MMBaseCall *self)
{
    if (self->priv->skip_incoming_timeout)
        return;

    if (self->priv->incoming_timeout)
        g_source_remove (self->priv->incoming_timeout);
    self->priv->incoming_timeout = g_timeout_add_seconds (INCOMING_TIMEOUT_SECS, (GSourceFunc)incoming_timeout_cb, self);
}

/*****************************************************************************/
/* Update audio settings */

void
mm_base_call_change_audio_settings (MMBaseCall        *self,
                                    MMPort            *audio_port,
                                    MMCallAudioFormat *audio_format)
{
    if (!audio_port && self->priv->audio_port && mm_port_get_connected (self->priv->audio_port))
        mm_port_set_connected (self->priv->audio_port, FALSE);
    g_clear_object (&self->priv->audio_port);

    if (audio_port) {
        self->priv->audio_port = g_object_ref (audio_port);
        mm_port_set_connected (self->priv->audio_port, TRUE);
    }

    mm_gdbus_call_set_audio_port   (MM_GDBUS_CALL (self), audio_port ? mm_port_get_device (audio_port) : NULL);
    mm_gdbus_call_set_audio_format (MM_GDBUS_CALL (self), mm_call_audio_format_get_dictionary (audio_format));
}

/*****************************************************************************/
/* Start call (DBus call handling) */

typedef struct {
    MMBaseCall *self;
    GDBusMethodInvocation *invocation;
} HandleStartContext;

static void
handle_start_context_free (HandleStartContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_start_ready (MMBaseCall         *self,
                    GAsyncResult       *res,
                    HandleStartContext *ctx)
{
    GError *error = NULL;

    g_clear_object (&ctx->self->priv->start_cancellable);

    if (!MM_BASE_CALL_GET_CLASS (self)->start_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't start call: %s", error->message);

        /* When cancelled via the start cancellable, it's because we got an early in-call error
         * before the call attempt was reported as started. */
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
            g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_CANCELLED)) {
            g_clear_error (&error);
            error = mm_connection_error_for_code (MM_CONNECTION_ERROR_NO_DIALTONE, self);
        }

        /* Convert errors into call state updates */
        if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_DIALTONE))
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
        else if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_BUSY)      ||
                 g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_ANSWER) ||
                 g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_CARRIER))
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
        else
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_UNKNOWN);

        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_start_context_free (ctx);
        return;
    }

    mm_obj_msg (self, "call is started");

    /* If dialing to ringing supported, leave it dialing */
    if (!ctx->self->priv->supports_dialing_to_ringing) {
        /* If ringing to active supported, set it ringing */
        if (ctx->self->priv->supports_ringing_to_active)
            mm_base_call_change_state (ctx->self, MM_CALL_STATE_RINGING_OUT, MM_CALL_STATE_REASON_OUTGOING_STARTED);
        else
            /* Otherwise, active right away */
            mm_base_call_change_state (ctx->self, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_OUTGOING_STARTED);
    }
    mm_gdbus_call_complete_start (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    handle_start_context_free (ctx);
}

static void
handle_start_auth_ready (MMAuthProvider *authp,
                         GAsyncResult *res,
                         HandleStartContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_base_call_change_state (ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_UNKNOWN);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_start_context_free (ctx);
        return;
    }

    /* We can only start call created by the user */
    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    if (state != MM_CALL_STATE_UNKNOWN) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "This call was not in unknown state, cannot start it");
        handle_start_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to start voice call...");

    /* Disallow non-emergency calls when in emergency-only state */
    if (!mm_iface_modem_voice_authorize_outgoing_call (ctx->self->priv->iface, ctx->self, &error)) {
        mm_base_call_change_state (ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_UNKNOWN);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_start_context_free (ctx);
        return;
    }

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->start ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->start_finish) {
        mm_base_call_change_state (ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_UNKNOWN);
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Starting call is not supported by this modem");
        handle_start_context_free (ctx);
        return;
    }

    mm_base_call_change_state (ctx->self, MM_CALL_STATE_DIALING, MM_CALL_STATE_REASON_OUTGOING_STARTED);

    /* Setup start cancellable to get notified of termination asynchronously */
    g_assert (!ctx->self->priv->start_cancellable);
    ctx->self->priv->start_cancellable = g_cancellable_new ();

    MM_BASE_CALL_GET_CLASS (ctx->self)->start (ctx->self,
                                               ctx->self->priv->start_cancellable,
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

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_VOICE,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)handle_start_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Accept call (DBus call handling) */

typedef struct {
    MMBaseCall *self;
    GDBusMethodInvocation *invocation;
} HandleAcceptContext;

static void
handle_accept_context_free (HandleAcceptContext *ctx)
{
    g_object_unref (ctx->invocation);
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
        mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_accept_context_free (ctx);
        return;
    }

    mm_obj_msg (self, "call is accepted");

    if (ctx->self->priv->incoming_timeout) {
        g_source_remove (ctx->self->priv->incoming_timeout);
        ctx->self->priv->incoming_timeout = 0;
    }
    mm_base_call_change_state (ctx->self, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
    mm_gdbus_call_complete_accept (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    handle_accept_context_free (ctx);
}

static void
handle_accept_auth_ready (MMAuthProvider *authp,
                          GAsyncResult *res,
                          HandleAcceptContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_accept_context_free (ctx);
        return;
    }

    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    /* We can only accept incoming call in ringing state */
    if (state != MM_CALL_STATE_RINGING_IN) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "This call was not ringing, cannot accept");
        handle_accept_context_free (ctx);
        return;
    }

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->accept ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->accept_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Accepting call is not supported by this modem");
        handle_accept_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to accept voice call...");

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

    mm_auth_provider_authorize (self->priv->authp,
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             self->priv->authp_cancellable,
                             (GAsyncReadyCallback)handle_accept_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Deflect call (DBus call handling) */

typedef struct {
    MMBaseCall            *self;
    GDBusMethodInvocation *invocation;
    gchar                 *number;
} HandleDeflectContext;

static void
handle_deflect_context_free (HandleDeflectContext *ctx)
{
    g_free (ctx->number);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleDeflectContext, ctx);
}

static void
handle_deflect_ready (MMBaseCall           *self,
                      GAsyncResult         *res,
                      HandleDeflectContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->deflect_finish (self, res, &error)) {
        mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_deflect_context_free (ctx);
        return;
    }

    mm_obj_msg (self, "call is deflected to '%s'", ctx->number);
    mm_base_call_change_state (ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_DEFLECTED);
    mm_gdbus_call_complete_deflect (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    handle_deflect_context_free (ctx);
}

static void
handle_deflect_auth_ready (MMAuthProvider       *authp,
                           GAsyncResult         *res,
                           HandleDeflectContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_deflect_context_free (ctx);
        return;
    }

    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    /* We can only deflect incoming call in ringing or waiting state */
    if (state != MM_CALL_STATE_RINGING_IN && state != MM_CALL_STATE_WAITING) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "This call was not ringing/waiting, cannot deflect");
        handle_deflect_context_free (ctx);
        return;
    }

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->deflect ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->deflect_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Deflecting call is not supported by this modem");
        handle_deflect_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to deflect voice call...");
    MM_BASE_CALL_GET_CLASS (ctx->self)->deflect (ctx->self,
                                                 ctx->number,
                                                 (GAsyncReadyCallback)handle_deflect_ready,
                                                 ctx);
}

static gboolean
handle_deflect (MMBaseCall            *self,
                GDBusMethodInvocation *invocation,
                const gchar           *number)
{
    HandleDeflectContext *ctx;

    ctx = g_slice_new0 (HandleDeflectContext);
    ctx->self       = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    ctx->number     = g_strdup (number);

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_VOICE,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)handle_deflect_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Join multiparty call (DBus call handling) */

typedef struct {
    MMBaseCall            *self;
    GDBusMethodInvocation *invocation;
} HandleJoinMultipartyContext;

static void
handle_join_multiparty_context_free (HandleJoinMultipartyContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
modem_voice_join_multiparty_ready (MMIfaceModemVoice           *modem,
                                   GAsyncResult                *res,
                                   HandleJoinMultipartyContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_voice_join_multiparty_finish (modem, res, &error))
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_call_complete_join_multiparty (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    handle_join_multiparty_context_free (ctx);
}

static void
handle_join_multiparty_auth_ready (MMAuthProvider              *authp,
                                   GAsyncResult                *res,
                                   HandleJoinMultipartyContext *ctx)
{
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_join_multiparty_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to join multiparty voice call...");

    /* This action is provided in the Call API, but implemented in the Modem.Voice interface
     * logic, because the action affects not only one call object, but all call objects that
     * are part of the multiparty call. */
    mm_iface_modem_voice_join_multiparty (ctx->self->priv->iface,
                                          ctx->self,
                                          (GAsyncReadyCallback)modem_voice_join_multiparty_ready,
                                          ctx);
}

static gboolean
handle_join_multiparty (MMBaseCall            *self,
                        GDBusMethodInvocation *invocation)
{
    HandleJoinMultipartyContext *ctx;

    ctx = g_new0 (HandleJoinMultipartyContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_VOICE,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)handle_join_multiparty_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Leave multiparty call (DBus call handling) */

typedef struct {
    MMBaseCall            *self;
    GDBusMethodInvocation *invocation;
} HandleLeaveMultipartyContext;

static void
handle_leave_multiparty_context_free (HandleLeaveMultipartyContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
modem_voice_leave_multiparty_ready (MMIfaceModemVoice            *modem,
                                    GAsyncResult                 *res,
                                    HandleLeaveMultipartyContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_voice_leave_multiparty_finish (modem, res, &error))
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_call_complete_leave_multiparty (MM_GDBUS_CALL (ctx->self), ctx->invocation);

    handle_leave_multiparty_context_free (ctx);
}

static void
handle_leave_multiparty_auth_ready (MMAuthProvider               *authp,
                                    GAsyncResult                 *res,
                                    HandleLeaveMultipartyContext *ctx)
{
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_leave_multiparty_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to leave multiparty voice call...");

    /* This action is provided in the Call API, but implemented in the Modem.Voice interface
     * logic, because the action affects not only one call object, but all call objects that
     * are part of the multiparty call. */
    mm_iface_modem_voice_leave_multiparty (ctx->self->priv->iface,
                                           ctx->self,
                                           (GAsyncReadyCallback)modem_voice_leave_multiparty_ready,
                                           ctx);
}

static gboolean
handle_leave_multiparty (MMBaseCall            *self,
                         GDBusMethodInvocation *invocation)
{
    HandleLeaveMultipartyContext *ctx;

    ctx = g_new0 (HandleLeaveMultipartyContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_VOICE,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)handle_leave_multiparty_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Hangup call (DBus call handling) */

typedef struct {
    MMBaseCall *self;
    GDBusMethodInvocation *invocation;
} HandleHangupContext;

static void
handle_hangup_context_free (HandleHangupContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_hangup_ready (MMBaseCall *self,
                     GAsyncResult *res,
                     HandleHangupContext *ctx)
{
    GError *error = NULL;

    /* we set it as terminated even if we got an error reported */
    mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);

    if (!MM_BASE_CALL_GET_CLASS (self)->hangup_finish (self, res, &error))
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* note: timeouts are already removed when setting state as TERMINATED */
        mm_gdbus_call_complete_hangup (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    }

    handle_hangup_context_free (ctx);
}

static void
handle_hangup_auth_ready (MMAuthProvider *authp,
                          GAsyncResult *res,
                          HandleHangupContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_hangup_context_free (ctx);
        return;
    }

    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    /* We can only hangup call in a valid state */
    if (state == MM_CALL_STATE_TERMINATED || state == MM_CALL_STATE_UNKNOWN) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "This call was not active, cannot hangup");
        handle_hangup_context_free (ctx);
        return;
    }

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->hangup ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->hangup_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Hanging up call is not supported by this modem");
        handle_hangup_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to hangup voice call...");
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

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_VOICE,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)handle_hangup_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Send dtmf (DBus call handling) */

typedef enum {
    DTMF_STEP_FIRST,
    DTMF_STEP_START,
    DTMF_STEP_TIMEOUT,
    DTMF_STEP_STOP,
    DTMF_STEP_NEXT,
    DTMF_STEP_LAST,
} DtmfStep;

typedef struct {
    DtmfStep   step;
    GError    *saved_error;
    guint8     call_id;
    /* Array of DTMF runs; split by pauses */
    GPtrArray *dtmfs;
    /* index into dtmfs */
    guint      cur_dtmf;
    /* index into cur dtmf run string */
    gchar     *cur_tone;
    guint      timeout_id;
} SendDtmfContext;

static void
send_dtmf_context_clear_timeout (SendDtmfContext *ctx)
{
    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }
}

static void
send_dtmf_context_free (SendDtmfContext *ctx)
{
    send_dtmf_context_clear_timeout (ctx);
    g_ptr_array_free (ctx->dtmfs, TRUE);
    g_assert (!ctx->saved_error);
    g_slice_free (SendDtmfContext, ctx);
}

static void send_dtmf_task_step_next (GTask *task);

static void
stop_dtmf_ignore_ready (MMBaseCall     *self,
                        GAsyncResult   *res,
                        gpointer        unused)
{
    /* Ignore the result and error */
    MM_BASE_CALL_GET_CLASS (self)->stop_dtmf_finish (self, res, NULL);
}

static void
send_dtmf_task_cancel (GTask *task)
{
    MMBaseCall      *self;
    SendDtmfContext *ctx;

    ctx = g_task_get_task_data (task);
    self = g_task_get_source_object (task);

    send_dtmf_context_clear_timeout (ctx);
    if (ctx->step > DTMF_STEP_FIRST && ctx->step < DTMF_STEP_STOP) {
        if (MM_BASE_CALL_GET_CLASS (self)->stop_dtmf) {
            MM_BASE_CALL_GET_CLASS (self)->stop_dtmf (self,
                                                      (GAsyncReadyCallback)stop_dtmf_ignore_ready,
                                                      NULL);
        }
    }
    g_assert (ctx->step != DTMF_STEP_LAST);
    ctx->step = DTMF_STEP_LAST;
    send_dtmf_task_step_next (task);
}

static void
stop_dtmf_ready (MMBaseCall     *self,
                 GAsyncResult   *res,
                 GTask          *task)
{
    SendDtmfContext *ctx;
    GError          *error = NULL;
    gboolean         success;

    ctx = g_task_get_task_data (task);

    success = MM_BASE_CALL_GET_CLASS (self)->stop_dtmf_finish (self, res, &error);
    if (ctx->step == DTMF_STEP_STOP) {
        if (!success) {
            g_propagate_error (&ctx->saved_error, error);
            ctx->step = DTMF_STEP_LAST;
        } else
            ctx->step++;

        send_dtmf_task_step_next (task);
    }

    /* Balance stop_dtmf() */
    g_object_unref (task);
}

static gboolean
dtmf_timeout (GTask *task)
{
    SendDtmfContext *ctx;

    ctx = g_task_get_task_data (task);

    /* If this was a pause character; move past it */
    if (ctx->cur_tone[0] == MM_CALL_DTMF_PAUSE_CHAR)
        ctx->cur_tone++;

    send_dtmf_context_clear_timeout (ctx);
    ctx->step++;
    send_dtmf_task_step_next (task);

    return G_SOURCE_REMOVE;
}

static void
send_dtmf_ready (MMBaseCall   *self,
                 GAsyncResult *res,
                 GTask        *task)
{
    SendDtmfContext *ctx;
    GError          *error = NULL;
    gssize           num_sent;

    ctx = g_task_get_task_data (task);

    num_sent = MM_BASE_CALL_GET_CLASS (self)->send_dtmf_finish (self, res, &error);
    if (ctx->step == DTMF_STEP_START) {
        if (num_sent < 0) {
            g_propagate_error (&ctx->saved_error, error);
            ctx->step = DTMF_STEP_LAST;
        } else {
            g_assert (num_sent > 0);
            g_assert ((guint) num_sent <= strlen (ctx->cur_tone));
            ctx->cur_tone += num_sent;
            ctx->step++;
        }

        send_dtmf_task_step_next (task);
    }

    /* Balance send_dtmf() */
    g_object_unref (task);
}

static void
send_dtmf_task_step_next (GTask *task)
{
    SendDtmfContext *ctx;
    gboolean         need_stop;
    MMBaseCall      *self;
    gboolean         is_pause;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    is_pause = (ctx->cur_tone[0] == MM_CALL_DTMF_PAUSE_CHAR);
    need_stop = MM_BASE_CALL_GET_CLASS (self)->stop_dtmf &&
                MM_BASE_CALL_GET_CLASS (self)->stop_dtmf_finish;

    switch (ctx->step) {
    case DTMF_STEP_FIRST:
        ctx->step++;
        /* Fall through */
    case DTMF_STEP_START:
        if (!is_pause) {
            MM_BASE_CALL_GET_CLASS (self)->send_dtmf (self,
                                                      ctx->cur_tone,
                                                      (GAsyncReadyCallback)send_dtmf_ready,
                                                      g_object_ref (task));
            return;
        }
        /* Fall through */
    case DTMF_STEP_TIMEOUT:
        if (need_stop || is_pause) {
            guint duration;

            duration = is_pause ? 2000 : mm_base_call_get_dtmf_tone_duration (self);

            /* Disable DTMF press after DTMF tone duration elapses */
            ctx->timeout_id = g_timeout_add (duration,
                                             (GSourceFunc) dtmf_timeout,
                                             task);
            return;
        }
        /* Fall through */
    case DTMF_STEP_STOP:
        send_dtmf_context_clear_timeout (ctx);
        if (need_stop && !is_pause) {
            MM_BASE_CALL_GET_CLASS (self)->stop_dtmf (self,
                                                      (GAsyncReadyCallback)stop_dtmf_ready,
                                                      g_object_ref (task));
            return;
        }
        /* Fall through */
    case DTMF_STEP_NEXT:
        /* Advance to next DTMF run? */
        if (ctx->cur_tone[0] == '\0') {
            ctx->cur_dtmf++;
            if (ctx->cur_dtmf < ctx->dtmfs->len)
                ctx->cur_tone = g_ptr_array_index (ctx->dtmfs, ctx->cur_dtmf);
        }

        /* More to send? */
        if (ctx->cur_tone[0]) {
            ctx->step = DTMF_STEP_START;
            send_dtmf_task_step_next (task);
            return;
        }
        /* no more DTMF characters to send */
        /* Fall through */
    case DTMF_STEP_LAST:
        send_dtmf_context_clear_timeout (ctx);
        if (ctx->saved_error)
            g_task_return_error (task, g_steal_pointer (&ctx->saved_error));
        else
            g_task_return_boolean (task, TRUE);
        g_object_unref (task);

        /* Start the next tone if any are queued */
        g_queue_remove (self->priv->dtmf_queue, task);
        task = g_queue_peek_head (self->priv->dtmf_queue);
        if (task)
            send_dtmf_task_step_next (task);
        break;
    default:
        g_assert_not_reached ();
    }
}

static gboolean
send_dtmf_task_finish (MMBaseCall    *self,
                       GAsyncResult  *res,
                       GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static GTask *
send_dtmf_task_new (MMBaseCall           *self,
                    const gchar          *dtmf,
                    GAsyncReadyCallback   callback,
                    gpointer              user_data,
                    GError              **error)
{
    GTask            *task;
    SendDtmfContext  *ctx;
    guint8            call_id;

    call_id = mm_base_call_get_index (self);
    if (call_id == 0) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Invalid call index");
        return NULL;
    }

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (SendDtmfContext);
    ctx->call_id = call_id;
    /* Split DTMF into runs of DTMF characters interrupted by pauses */
    ctx->dtmfs = mm_dtmf_split (dtmf);
    g_assert (ctx->dtmfs->len > 0);
    ctx->cur_tone = g_ptr_array_index (ctx->dtmfs, ctx->cur_dtmf);
    g_task_set_task_data (task, ctx, (GDestroyNotify) send_dtmf_context_free);

    return task;
}

/*****************************************************************************/
/* Send DTMF D-Bus request handling */

typedef struct {
    MMBaseCall *self;
    GDBusMethodInvocation *invocation;
    gchar *dtmf;
} HandleSendDtmfContext;

static void
handle_send_dtmf_context_free (HandleSendDtmfContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->dtmf);
    g_free (ctx);
}

static void
handle_send_dtmf_ready (MMBaseCall *self,
                        GAsyncResult *res,
                        HandleSendDtmfContext *ctx)
{
    GError *error = NULL;

    if (!send_dtmf_task_finish (self, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        mm_gdbus_call_complete_send_dtmf (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    }

    handle_send_dtmf_context_free (ctx);
}

static void
handle_send_dtmf_auth_ready (MMAuthProvider *authp,
                             GAsyncResult *res,
                             HandleSendDtmfContext *ctx)
{
    MMCallState  state;
    GError      *error = NULL;
    GTask       *task;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_send_dtmf_context_free (ctx);
        return;
    }

    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    /* Ensure there are DTMF characters to send */
    if (!ctx->dtmf || !ctx->dtmf[0]) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                        "No DTMF characters given");
        handle_send_dtmf_context_free (ctx);
        return;
    }

    /* And that there aren't too many */
    if (strlen (ctx->dtmf) > 50) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                        "Too many DTMF characters");
        handle_send_dtmf_context_free (ctx);
        return;
    }

    /* Check if we do support doing DTMF at all */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->send_dtmf ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->send_dtmf_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Sending dtmf is not supported by this modem");
        handle_send_dtmf_context_free (ctx);
        return;
    }

    /* We can only send_dtmf when call is in ACTIVE state */
    if (state != MM_CALL_STATE_ACTIVE) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "This call was not active, cannot send dtmf");
        handle_send_dtmf_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to send DTMF...");
    task = send_dtmf_task_new (ctx->self,
                               ctx->dtmf,
                               (GAsyncReadyCallback)handle_send_dtmf_ready,
                               ctx,
                               &error);
    if (!task) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_send_dtmf_context_free (ctx);
        return;
    }

    g_queue_push_tail (ctx->self->priv->dtmf_queue, task);
    if (g_queue_get_length (ctx->self->priv->dtmf_queue) == 1)
        send_dtmf_task_step_next (task);
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
    ctx->dtmf = g_strdup (dtmf);

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_VOICE,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)handle_send_dtmf_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/

void
mm_base_call_export (MMBaseCall *self)
{
    gchar *path;

    path = g_strdup_printf (MM_DBUS_CALL_PREFIX "/%d", self->priv->dbus_id);
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
    g_object_connect (self,
                      "signal::handle-start",            G_CALLBACK (handle_start),            NULL,
                      "signal::handle-accept",           G_CALLBACK (handle_accept),           NULL,
                      "signal::handle-deflect",          G_CALLBACK (handle_deflect),          NULL,
                      "signal::handle-join-multiparty",  G_CALLBACK (handle_join_multiparty),  NULL,
                      "signal::handle-leave-multiparty", G_CALLBACK (handle_leave_multiparty), NULL,
                      "signal::handle-hangup",           G_CALLBACK (handle_hangup),           NULL,
                      "signal::handle-send-dtmf",        G_CALLBACK (handle_send_dtmf),        NULL,
                      NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_obj_warn (self, "couldn't export call: %s", error->message);
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

const gchar *
mm_base_call_get_number (MMBaseCall *self)
{
    return mm_gdbus_call_get_number (MM_GDBUS_CALL (self));
}

void
mm_base_call_set_number (MMBaseCall  *self,
                         const gchar *number)
{
    return mm_gdbus_call_set_number (MM_GDBUS_CALL (self), number);
}

MMCallDirection
mm_base_call_get_direction (MMBaseCall *self)
{
    return (MMCallDirection) mm_gdbus_call_get_direction (MM_GDBUS_CALL (self));
}

MMCallState
mm_base_call_get_state (MMBaseCall *self)
{
    return (MMCallState) mm_gdbus_call_get_state (MM_GDBUS_CALL (self));
}

gboolean
mm_base_call_get_multiparty (MMBaseCall *self)
{
    return mm_gdbus_call_get_multiparty (MM_GDBUS_CALL (self));
}

void
mm_base_call_set_multiparty (MMBaseCall *self,
                             gboolean    multiparty)
{
    return mm_gdbus_call_set_multiparty (MM_GDBUS_CALL (self), multiparty);
}

guint
mm_base_call_get_dtmf_tone_duration (MMBaseCall *self)
{
    return mm_dtmf_duration_normalize (mm_gdbus_call_get_dtmf_tone_duration (MM_GDBUS_CALL (self)));
}

void
mm_base_call_set_dtmf_tone_duration (MMBaseCall *self,
                                     guint       duration_ms)
{
    return mm_gdbus_call_set_dtmf_tone_duration (MM_GDBUS_CALL (self),
                                                 mm_dtmf_duration_normalize (duration_ms));
}

/*****************************************************************************/
/* Current call index, only applicable while the call is ongoing
 * See 3GPP TS 22.030 [27], subclause 6.5.5.1.
 */

guint
mm_base_call_get_index (MMBaseCall *self)
{
    return self->priv->index;
}

void
mm_base_call_set_index (MMBaseCall *self,
                        guint       index)
{
    self->priv->index = index;
}

/*****************************************************************************/

void
mm_base_call_change_state (MMBaseCall        *self,
                           MMCallState        new_state,
                           MMCallStateReason  reason)
{
    MMCallState old_state;

    old_state = mm_gdbus_call_get_state (MM_GDBUS_CALL (self));

    if (old_state == new_state)
        return;

    mm_obj_msg (self, "call state changed: %s -> %s (%s)",
                mm_call_state_get_string (old_state),
                mm_call_state_get_string (new_state),
                mm_call_state_reason_get_string (reason));

    /* Setup/cleanup unsolicited events  based on state transitions to/from ACTIVE */
    if (new_state == MM_CALL_STATE_TERMINATED) {
        /* reset index */
        self->priv->index = 0;
        /* cleanup incoming timeout, if any */
        if (self->priv->incoming_timeout) {
            g_source_remove (self->priv->incoming_timeout);
            self->priv->incoming_timeout = 0;
        }
        /* cancel start if ongoing */
        g_cancellable_cancel (self->priv->start_cancellable);
    }

    mm_gdbus_call_set_state (MM_GDBUS_CALL (self), new_state);
    mm_gdbus_call_set_state_reason (MM_GDBUS_CALL (self), reason);
    mm_gdbus_call_emit_state_changed (MM_GDBUS_CALL (self), old_state, new_state, reason);
}

/*****************************************************************************/

void
mm_base_call_received_dtmf (MMBaseCall *self,
                            const gchar *dtmf)
{
    mm_gdbus_call_emit_dtmf_received (MM_GDBUS_CALL (self), dtmf);
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMBaseCall *self;

    self = MM_BASE_CALL (_self);
    return g_strdup_printf ("call%u", self->priv->dbus_id);
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
    case PROP_BIND_TO:
        g_clear_object (&self->priv->bind_to);
        self->priv->bind_to = g_value_dup_object (value);
        mm_bind_to (MM_BIND (self), MM_BASE_CALL_CONNECTION, self->priv->bind_to);
        break;
    case PROP_IFACE_MODEM_VOICE:
        g_clear_object (&self->priv->iface);
        self->priv->iface = g_value_dup_object (value);
        break;
    case PROP_SKIP_INCOMING_TIMEOUT:
        self->priv->skip_incoming_timeout = g_value_get_boolean (value);
        break;
    case PROP_SUPPORTS_DIALING_TO_RINGING:
        self->priv->supports_dialing_to_ringing = g_value_get_boolean (value);
        break;
    case PROP_SUPPORTS_RINGING_TO_ACTIVE:
        self->priv->supports_ringing_to_active = g_value_get_boolean (value);
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
    case PROP_BIND_TO:
        g_value_set_object (value, self->priv->bind_to);
        break;
    case PROP_IFACE_MODEM_VOICE:
        g_value_set_object (value, self->priv->iface);
        break;
    case PROP_SKIP_INCOMING_TIMEOUT:
        g_value_set_boolean (value, self->priv->skip_incoming_timeout);
        break;
    case PROP_SUPPORTS_DIALING_TO_RINGING:
        g_value_set_boolean (value, self->priv->supports_dialing_to_ringing);
        break;
    case PROP_SUPPORTS_RINGING_TO_ACTIVE:
        g_value_set_boolean (value, self->priv->supports_ringing_to_active);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_base_call_init (MMBaseCall *self)
{
    static guint id = 0;

    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BASE_CALL, MMBaseCallPrivate);

    /* Each call is given a unique id to build its own DBus path */
    self->priv->dbus_id = id++;

    /* Setup authorization provider */
    self->priv->authp = mm_auth_provider_get ();
    self->priv->authp_cancellable = g_cancellable_new ();

    self->priv->dtmf_queue = g_queue_new ();
}

static void
finalize (GObject *object)
{
    MMBaseCall *self = MM_BASE_CALL (object);

    g_assert (g_queue_get_length (self->priv->dtmf_queue) == 0);
    g_queue_free (g_steal_pointer (&self->priv->dtmf_queue));

    g_assert (!self->priv->start_cancellable);
    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_base_call_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseCall *self = MM_BASE_CALL (object);

    g_clear_object (&self->priv->audio_port);

    if (self->priv->incoming_timeout) {
        g_source_remove (self->priv->incoming_timeout);
        self->priv->incoming_timeout = 0;
    }

    if (self->priv->connection) {
        /* If we arrived here with a valid connection, make sure we unexport
         * the object */
        call_dbus_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    g_clear_object (&self->priv->iface);
    g_clear_object (&self->priv->bind_to);
    g_cancellable_cancel (self->priv->authp_cancellable);
    g_clear_object (&self->priv->authp_cancellable);

    g_queue_foreach (self->priv->dtmf_queue, (GFunc) send_dtmf_task_cancel, NULL);
    g_queue_clear (self->priv->dtmf_queue);

    G_OBJECT_CLASS (mm_base_call_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
bind_iface_init (MMBindInterface *iface)
{
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

    g_object_class_override_property (object_class, PROP_BIND_TO, MM_BIND_TO);

    properties[PROP_IFACE_MODEM_VOICE] =
        g_param_spec_object (MM_BASE_CALL_IFACE_MODEM_VOICE,
                             "Modem Voice Interface",
                             "The Modem voice interface which owns this call",
                             MM_TYPE_IFACE_MODEM_VOICE,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_IFACE_MODEM_VOICE, properties[PROP_IFACE_MODEM_VOICE]);

    properties[PROP_SKIP_INCOMING_TIMEOUT] =
        g_param_spec_boolean (MM_BASE_CALL_SKIP_INCOMING_TIMEOUT,
                              "Skip incoming timeout",
                              "There is no need to setup a timeout for incoming calls",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_SKIP_INCOMING_TIMEOUT, properties[PROP_SKIP_INCOMING_TIMEOUT]);

    properties[PROP_SUPPORTS_DIALING_TO_RINGING] =
        g_param_spec_boolean (MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING,
                              "Dialing to ringing",
                              "Whether the call implementation reports dialing to ringing state updates",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_SUPPORTS_DIALING_TO_RINGING, properties[PROP_SUPPORTS_DIALING_TO_RINGING]);

    properties[PROP_SUPPORTS_RINGING_TO_ACTIVE] =
        g_param_spec_boolean (MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,
                              "Ringing to active",
                              "Whether the call implementation reports ringing to active state updates",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_SUPPORTS_RINGING_TO_ACTIVE, properties[PROP_SUPPORTS_RINGING_TO_ACTIVE]);
}
