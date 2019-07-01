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
    PROP_SKIP_INCOMING_TIMEOUT,
    PROP_SUPPORTS_DIALING_TO_RINGING,
    PROP_SUPPORTS_RINGING_TO_ACTIVE,
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
    /* Features */
    gboolean skip_incoming_timeout;
    gboolean supports_dialing_to_ringing;
    gboolean supports_ringing_to_active;

    guint incoming_timeout;
    GRegex *in_call_events;

    /* The port used for audio while call is ongoing, if known */
    MMPort *audio_port;

    /* Ongoing call index */
    guint index;
};

/*****************************************************************************/
/* In-call unsolicited events
 * Once a call is started, we may need to detect special URCs to trigger call
 * state changes, e.g. "NO CARRIER" when the remote party hangs up. */

static void
in_call_event_received (MMPortSerialAt *port,
                        GMatchInfo     *info,
                        MMBaseCall     *self)
{
    gchar *str;

    str = g_match_info_fetch (info, 1);
    if (!str)
        return;

    if (!strcmp (str, "NO DIALTONE"))
        mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
    else if (!strcmp (str, "NO CARRIER") || !strcmp (str, "BUSY") || !strcmp (str, "NO ANSWER"))
        mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);

    g_free (str);
}

static gboolean
common_setup_cleanup_unsolicited_events (MMBaseCall  *self,
                                         gboolean     enable,
                                         GError     **error)
{
    MMBaseModem    *modem = NULL;
    MMPortSerialAt *ports[2];
    gint            i;

    if (G_UNLIKELY (!self->priv->in_call_events))
        self->priv->in_call_events = g_regex_new ("\\r\\n(NO CARRIER|BUSY|NO ANSWER|NO DIALTONE)\\r\\n$",
                                                  G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);

    g_object_get (self,
                  MM_BASE_CALL_MODEM, &modem,
                  NULL);

    ports[0] = mm_base_modem_peek_port_primary   (modem);
    ports[1] = mm_base_modem_peek_port_secondary (modem);

    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        mm_port_serial_at_add_unsolicited_msg_handler (ports[i],
                                                       self->priv->in_call_events,
                                                       enable ? (MMPortSerialAtUnsolicitedMsgFn) in_call_event_received : NULL,
                                                       enable ? self : NULL,
                                                       NULL);
    }

    g_object_unref (modem);
    return TRUE;
}

static gboolean
setup_unsolicited_events (MMBaseCall  *self,
                          GError     **error)
{
    return common_setup_cleanup_unsolicited_events (self, TRUE, error);
}

static gboolean
cleanup_unsolicited_events (MMBaseCall  *self,
                            GError     **error)
{
    return common_setup_cleanup_unsolicited_events (self, FALSE, error);
}

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
    mm_info ("incoming call timed out: no response");
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

static void
update_audio_settings (MMBaseCall        *self,
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
call_started (HandleStartContext *ctx)
{
    mm_info ("call is started");

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
start_setup_audio_channel_ready (MMBaseCall         *self,
                                 GAsyncResult       *res,
                                 HandleStartContext *ctx)
{
    MMPort            *audio_port = NULL;
    MMCallAudioFormat *audio_format = NULL;
    GError            *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->setup_audio_channel_finish (self, res, &audio_port, &audio_format, &error)) {
        mm_warn ("Couldn't setup audio channel: '%s'", error->message);
        mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_AUDIO_SETUP_FAILED);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_start_context_free (ctx);
        return;
    }

    if (audio_port || audio_format) {
        update_audio_settings (self, audio_port, audio_format);
        g_clear_object (&audio_port);
        g_clear_object (&audio_format);
    }

    call_started (ctx);
}

static void
handle_start_ready (MMBaseCall         *self,
                    GAsyncResult       *res,
                    HandleStartContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->start_finish (self, res, &error)) {
        mm_warn ("Couldn't start call : '%s'", error->message);
        /* Convert errors into call state updates */
        if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_DIALTONE))
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
        else if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_BUSY)      ||
                 g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_ANSWER) ||
                 g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_CARRIER))
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
        else
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_UNKNOWN);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_start_context_free (ctx);
        return;
    }

    /* If there is an audio setup method, run it now */
    if (MM_BASE_CALL_GET_CLASS (self)->setup_audio_channel) {
        mm_info ("setting up audio channel...");
        MM_BASE_CALL_GET_CLASS (self)->setup_audio_channel (self,
                                                            (GAsyncReadyCallback) start_setup_audio_channel_ready,
                                                            ctx);
        return;
    }

    /* Otherwise, we're done */
    call_started (ctx);
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

    mm_info ("user request to start call");

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

    mm_base_call_change_state (ctx->self, MM_CALL_STATE_DIALING, MM_CALL_STATE_REASON_OUTGOING_STARTED);

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
call_accepted (HandleAcceptContext *ctx)
{
    mm_info ("call is accepted");

    if (ctx->self->priv->incoming_timeout) {
        g_source_remove (ctx->self->priv->incoming_timeout);
        ctx->self->priv->incoming_timeout = 0;
    }
    mm_base_call_change_state (ctx->self, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
    mm_gdbus_call_complete_accept (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    handle_accept_context_free (ctx);
}

static void
accept_setup_audio_channel_ready (MMBaseCall          *self,
                                  GAsyncResult        *res,
                                  HandleAcceptContext *ctx)
{
    MMPort            *audio_port = NULL;
    MMCallAudioFormat *audio_format = NULL;
    GError            *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->setup_audio_channel_finish (self, res, &audio_port, &audio_format, &error)) {
        mm_warn ("Couldn't setup audio channel: '%s'", error->message);
        mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_AUDIO_SETUP_FAILED);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_accept_context_free (ctx);
        return;
    }

    if (audio_port || audio_format) {
        update_audio_settings (self, audio_port, audio_format);
        g_clear_object (&audio_port);
        g_clear_object (&audio_format);
    }

    call_accepted (ctx);
}

static void
handle_accept_ready (MMBaseCall *self,
                     GAsyncResult *res,
                     HandleAcceptContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->accept_finish (self, res, &error)) {
        mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_accept_context_free (ctx);
        return;
    }

    /* If there is an audio setup method, run it now */
    if (MM_BASE_CALL_GET_CLASS (self)->setup_audio_channel) {
        mm_info ("setting up audio channel...");
        MM_BASE_CALL_GET_CLASS (self)->setup_audio_channel (self,
                                                            (GAsyncReadyCallback) accept_setup_audio_channel_ready,
                                                            ctx);
        return;
    }

    /* Otherwise, we're done */
    call_accepted (ctx);
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
                                               "This call was not ringing, cannot accept");
        handle_accept_context_free (ctx);
        return;
    }

    mm_info ("user request to accept call");

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
/* Deflect call (DBus call handling) */

typedef struct {
    MMBaseCall            *self;
    MMBaseModem           *modem;
    GDBusMethodInvocation *invocation;
    gchar                 *number;
} HandleDeflectContext;

static void
handle_deflect_context_free (HandleDeflectContext *ctx)
{
    g_free (ctx->number);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
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
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_deflect_context_free (ctx);
        return;
    }

    mm_info ("call is deflected to '%s'", ctx->number);
    mm_base_call_change_state (ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_DEFLECTED);
    mm_gdbus_call_complete_deflect (MM_GDBUS_CALL (ctx->self), ctx->invocation);
    handle_deflect_context_free (ctx);
}

static void
handle_deflect_auth_ready (MMBaseModem          *modem,
                           GAsyncResult         *res,
                           HandleDeflectContext *ctx)
{
    MMCallState state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_deflect_context_free (ctx);
        return;
    }

    state = mm_gdbus_call_get_state (MM_GDBUS_CALL (ctx->self));

    /* We can only deflect incoming call in ringing or waiting state */
    if (state != MM_CALL_STATE_RINGING_IN && state != MM_CALL_STATE_WAITING) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_FAILED,
                                               "This call was not ringing/waiting, cannot deflect");
        handle_deflect_context_free (ctx);
        return;
    }

    mm_info ("user request to deflect call");

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->deflect ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->deflect_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Deflecting call is not supported by this modem");
        handle_deflect_context_free (ctx);
        return;
    }

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
    g_object_get (self,
                  MM_BASE_CALL_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_deflect_auth_ready,
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

    /* we set it as terminated even if we got an error reported */
    mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);

    if (!MM_BASE_CALL_GET_CLASS (self)->hangup_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* note: timeouts are already removed when setting state as TERMINATED */
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

    mm_info ("user request to hangup call");

    /* Check if we do support doing it */
    if (!MM_BASE_CALL_GET_CLASS (ctx->self)->hangup ||
        !MM_BASE_CALL_GET_CLASS (ctx->self)->hangup_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Hanging up call is not supported by this modem");
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
    g_free (ctx->dtmf);
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

    ctx->dtmf = g_strdup (dtmf);
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
                      "handle-deflect",
                      G_CALLBACK (handle_deflect),
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

/* Define the states in which we want to handle in-call events */
#define MM_CALL_STATE_IS_IN_CALL(state)         \
    (state == MM_CALL_STATE_DIALING ||          \
     state == MM_CALL_STATE_RINGING_IN  ||      \
     state == MM_CALL_STATE_RINGING_OUT ||      \
     state == MM_CALL_STATE_ACTIVE)

static void
cleanup_audio_channel_ready (MMBaseCall   *self,
                             GAsyncResult *res)
{
    GError *error = NULL;

    if (!MM_BASE_CALL_GET_CLASS (self)->cleanup_audio_channel_finish (self, res, &error)) {
        mm_warn ("audio channel cleanup failed: %s", error->message);
        g_error_free (error);
    }
}

void
mm_base_call_change_state (MMBaseCall        *self,
                           MMCallState        new_state,
                           MMCallStateReason  reason)
{
    MMCallState  old_state;
    GError      *error = NULL;

    old_state = mm_gdbus_call_get_state (MM_GDBUS_CALL (self));

    if (old_state == new_state)
        return;

    mm_info ("Call state changed: %s -> %s (%s)",
             mm_call_state_get_string (old_state),
             mm_call_state_get_string (new_state),
             mm_call_state_reason_get_string (reason));

    /* Setup/cleanup unsolicited events  based on state transitions to/from ACTIVE */
    if (!MM_CALL_STATE_IS_IN_CALL (old_state) && MM_CALL_STATE_IS_IN_CALL (new_state)) {
        mm_dbg ("Setting up in-call unsolicited events...");
        if (MM_BASE_CALL_GET_CLASS (self)->setup_unsolicited_events &&
            !MM_BASE_CALL_GET_CLASS (self)->setup_unsolicited_events (self, &error)) {
            mm_warn ("Couldn't setup in-call unsolicited events: %s", error->message);
            g_error_free (error);
        }
    } else if (MM_CALL_STATE_IS_IN_CALL (old_state) && !MM_CALL_STATE_IS_IN_CALL (new_state)) {
        mm_dbg ("Cleaning up in-call unsolicited events...");
        if (MM_BASE_CALL_GET_CLASS (self)->cleanup_unsolicited_events &&
            !MM_BASE_CALL_GET_CLASS (self)->cleanup_unsolicited_events (self, &error)) {
            mm_warn ("Couldn't cleanup in-call unsolicited events: %s", error->message);
            g_error_free (error);
        }
        if (MM_BASE_CALL_GET_CLASS (self)->cleanup_audio_channel) {
            mm_info ("cleaning up audio channel...");
            update_audio_settings (self, NULL, NULL);
            MM_BASE_CALL_GET_CLASS (self)->cleanup_audio_channel (self,
                                                                  (GAsyncReadyCallback) cleanup_audio_channel_ready,
                                                                  NULL);
        }
        /* reset index */
        self->priv->index = 0;
        /* cleanup incoming timeout, if any */
        if (self->priv->incoming_timeout) {
            g_source_remove (self->priv->incoming_timeout);
            self->priv->incoming_timeout = 0;
        }
    }

    mm_gdbus_call_set_state (MM_GDBUS_CALL (self), new_state);
    mm_gdbus_call_set_state_reason (MM_GDBUS_CALL (self), reason);
    mm_gdbus_call_emit_state_changed (MM_GDBUS_CALL (self), old_state, new_state, reason);
}

void
mm_base_call_received_dtmf (MMBaseCall *self,
                            const gchar *dtmf)
{
    mm_gdbus_call_emit_dtmf_received (MM_GDBUS_CALL (self), dtmf);
}

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
call_start (MMBaseCall *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GTask *task;
    gchar *cmd;

    task = g_task_new (self, NULL, callback, user_data);

    cmd = g_strdup_printf ("ATD%s;", mm_gdbus_call_get_number (MM_GDBUS_CALL (self)));
    mm_base_modem_at_command (self->priv->modem,
                              cmd,
                              90,
                              FALSE,
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
call_accept (MMBaseCall          *self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    GTask *task;

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
call_deflect (MMBaseCall          *self,
              const gchar         *number,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    GTask *task;
    gchar *cmd;

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
    MMBaseCall *self;

    self = g_task_get_source_object (task);
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
        mm_warn ("couldn't hangup single call with call id '%u': %s",
                 self->priv->index, error->message);
        g_error_free (error);
        chup_fallback (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
call_hangup (MMBaseCall          *self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* Try to hangup the single call id */
    if (self->priv->index) {
        gchar *cmd;

        cmd = g_strdup_printf ("+CHLD=1%u", self->priv->index);
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

static gboolean
call_send_dtmf_finish (MMBaseCall *self,
                       GAsyncResult *res,
                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
call_send_dtmf_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_dbg ("Couldn't send_dtmf: '%s'", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
call_send_dtmf (MMBaseCall *self,
                const gchar *dtmf,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    GTask *task;
    gchar *cmd;

    task = g_task_new (self, NULL, callback, user_data);

    cmd = g_strdup_printf ("AT+VTS=%c", dtmf[0]);
    mm_base_modem_at_command (self->priv->modem,
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)call_send_dtmf_ready,
                              task);

    g_free (cmd);
}

/*****************************************************************************/

MMBaseCall *
mm_base_call_new (MMBaseModem     *modem,
                  MMCallDirection  direction,
                  const gchar     *number,
                  gboolean         skip_incoming_timeout,
                  gboolean         supports_dialing_to_ringing,
                  gboolean         supports_ringing_to_active)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_BASE_CALL,
                                       MM_BASE_CALL_MODEM, modem,
                                       "direction",        direction,
                                       "number",           number,
                                       MM_BASE_CALL_SKIP_INCOMING_TIMEOUT,       skip_incoming_timeout,
                                       MM_BASE_CALL_SUPPORTS_DIALING_TO_RINGING, supports_dialing_to_ringing,
                                       MM_BASE_CALL_SUPPORTS_RINGING_TO_ACTIVE,  supports_ringing_to_active,
                                       NULL));
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
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
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
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BASE_CALL, MMBaseCallPrivate);
}

static void
finalize (GObject *object)
{
    MMBaseCall *self = MM_BASE_CALL (object);

    if (self->priv->in_call_events)
        g_regex_unref (self->priv->in_call_events);
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

    klass->start                      = call_start;
    klass->start_finish               = call_start_finish;
    klass->accept                     = call_accept;
    klass->accept_finish              = call_accept_finish;
    klass->deflect                    = call_deflect;
    klass->deflect_finish             = call_deflect_finish;
    klass->hangup                     = call_hangup;
    klass->hangup_finish              = call_hangup_finish;
    klass->send_dtmf                  = call_send_dtmf;
    klass->send_dtmf_finish           = call_send_dtmf_finish;
    klass->setup_unsolicited_events   = setup_unsolicited_events;
    klass->cleanup_unsolicited_events = cleanup_unsolicited_events;


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
