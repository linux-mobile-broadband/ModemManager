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
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
 * Copyright (C) 2019 Purism SPC
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-voice.h"
#include "mm-call-list.h"
#include "mm-log-object.h"

#define CALL_LIST_POLLING_CONTEXT_TAG "voice-call-list-polling-context-tag"
#define IN_CALL_EVENT_CONTEXT_TAG     "voice-in-call-event-context-tag"

static GQuark call_list_polling_context_quark;
static GQuark in_call_event_context_quark;

/*****************************************************************************/

void
mm_iface_modem_voice_bind_simple_status (MMIfaceModemVoice *self,
                                         MMSimpleStatus *status)
{
}

/*****************************************************************************/

gboolean
mm_iface_modem_voice_authorize_outgoing_call (MMIfaceModemVoice  *self,
                                              MMBaseCall         *call,
                                              GError            **error)
{
    MmGdbusModemVoice *skeleton = NULL;
    MMBaseSim         *sim = NULL;
    gboolean           emergency_only = FALSE;
    gboolean           call_allowed = FALSE;
    GError            *inner_error = NULL;
    guint              i;
    const gchar       *number;

    static const gchar *always_valid_emergency_numbers[] = { "112", "911" };
    static const gchar *no_sim_valid_emergency_numbers[] = { "000", "08", "110", "999", "118", "119" };

    g_assert (mm_base_call_get_direction (call) == MM_CALL_DIRECTION_OUTGOING);
    number = mm_base_call_get_number (call);
    g_assert (number);

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_DBUS_SKELETON, &skeleton,
                  NULL);

    if (!skeleton) {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "voice operations unsupported");
        goto out;
    }

    g_object_get (skeleton,
                  "emergency-only", &emergency_only,
                  NULL);

    /* Identification of emergency numbers. 3GPP TS 22.101
     *
     *   a) 112 and 911 shall always be available.
     *   b) Any emergency call number stored on a SIM/USIM when the SIM/USIM is
     *      present.
     *   c) 000, 08, 110, 999, 118 and 119 when a SIM/USIM is not present.
     *   d) Additional emergency call numbers that may have been downloaded by
     *      the serving network when the SIM/USIM is present.
     *
     * In ModemManager we're not flagging any call as being "emergency" or
     * "normal", but we can right away limit non-emergency calls if we're in
     * "emergency-only" mode.
     */

    /* If we're not in emergency mode, the call (emergency or normal) is always allowed */
    if (!emergency_only) {
        mm_obj_dbg (self, "voice call to %s allowed", number);
        call_allowed = TRUE;
        goto out;
    }

    for (i = 0; i < G_N_ELEMENTS (always_valid_emergency_numbers); i++) {
        if (g_strcmp0 (number, always_valid_emergency_numbers[i]) == 0) {
            mm_obj_dbg (self, "voice call to %s allowed: emergency call number always valid", number);
            call_allowed = TRUE;
            goto out;
        }
    }

    /* Check if we have a SIM */
    g_object_get (self,
                  MM_IFACE_MODEM_SIM, &sim,
                  NULL);
    if (!sim) {
        /* If no SIM available, some additional numbers may be valid emergency numbers */
        for (i = 0; i < G_N_ELEMENTS (no_sim_valid_emergency_numbers); i++) {
            if (g_strcmp0 (number, no_sim_valid_emergency_numbers[i]) == 0) {
                mm_obj_dbg (self, "voice call to %s allowed: emergency call number valid when no SIM", number);
                call_allowed = TRUE;
                goto out;
            }
        }

        mm_obj_dbg (self, "voice call to %s NOT allowed: not a valid emergency call number when no SIM", number);
        goto out;
    }

    /* Check if the number is programmed in EF_ECC */
    if (mm_base_sim_is_emergency_number (sim, number)) {
        mm_obj_dbg (self, "voice call to %s allowed: emergency call number programmed in the SIM", number);
        call_allowed = TRUE;
    } else
        mm_obj_dbg (self, "voice call to %s NOT allowed: not a valid emergency call number programmed in the SIM", number);

 out:

    if (inner_error)
        g_propagate_error (error, inner_error);
    else if (!call_allowed)
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNAUTHORIZED,
                     "only emergency calls allowed");

    g_clear_object (&skeleton);
    g_clear_object (&sim);
    return call_allowed;
}

/*****************************************************************************/

/* new calls will inherit audio settings if the modem is already in-call state */
static void update_audio_settings_in_call (MMIfaceModemVoice *self,
                                           MMBaseCall        *call);

static MMBaseCall *
create_incoming_call (MMIfaceModemVoice *self,
                      const gchar       *number)
{
    MMBaseCall *call;

    g_assert (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->create_call != NULL);

    call = MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->create_call (self, MM_CALL_DIRECTION_INCOMING, number);
    update_audio_settings_in_call (self, call);
    return call;
}

static MMBaseCall *
create_outgoing_call_from_properties (MMIfaceModemVoice  *self,
                                      MMCallProperties   *properties,
                                      GError            **error)
{
    MMBaseCall  *call;
    const gchar *number;

    /* Don't create CALL from properties if either number is missing */
    number = mm_call_properties_get_number (properties) ;
    if (!number) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create call: mandatory parameter 'number' is missing");
        return NULL;
    }

    /* Create a call object as defined by the interface */
    g_assert (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->create_call != NULL);
    call = MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->create_call (self, MM_CALL_DIRECTION_OUTGOING, number);
    update_audio_settings_in_call (self, call);
    return call;
}

/*****************************************************************************/
/* Common helper to match call info against a known call object */

static gboolean
match_single_call_info (MMIfaceModemVoice *self,
                        const MMCallInfo  *call_info,
                        MMBaseCall        *call)
{
    MMCallState      state;
    MMCallDirection  direction;
    const gchar     *number;
    guint            idx;
    gboolean         match_direction_and_state = FALSE;
    gboolean         match_number = FALSE;
    gboolean         match_index = FALSE;
    gboolean         match_terminated = FALSE;

    /* try to look for a matching call by direction/number/index */
    state     = mm_base_call_get_state     (call);
    direction = mm_base_call_get_direction (call);
    number    = mm_base_call_get_number    (call);
    idx       = mm_base_call_get_index     (call);

    /* Match index */
    if (call_info->index && (call_info->index == idx))
        match_index = TRUE;

    /* Match direction and state.
     * We cannot apply this match if both call info and call have an index set
     * and they're different already. */
    if ((call_info->direction == direction) &&
        (call_info->state == state) &&
        (!call_info->index || !idx || match_index))
        match_direction_and_state = TRUE;

    /* Match number */
    if (call_info->number && number &&
        g_strcmp0 (call_info->number, number) == 0)
        match_number = TRUE;

    /* Match special terminated event.
     * We cannot apply this match if the call is part of a multiparty
     * call, because we don't know which of the calls in the multiparty
     * is the one that finished. Must rely on other reports that do
     * provide call index. */
    if ((call_info->state == MM_CALL_STATE_TERMINATED) &&
        (call_info->direction == MM_CALL_DIRECTION_UNKNOWN) &&
        !call_info->index &&
        !call_info->number &&
        !mm_base_call_get_multiparty (call))
        match_terminated = TRUE;

    /* If no clear match, nothing to do */
    if (!match_direction_and_state &&
        !match_number &&
        !match_index &&
        !match_terminated)
        return FALSE;

    mm_obj_dbg (self, "call info matched (matched direction/state %s, matched number %s"
                ", matched index %s, matched terminated %s) with call at '%s'",
                match_direction_and_state ? "yes" : "no",
                match_number ? "yes" : "no",
                match_index ? "yes" : "no",
                match_terminated ? "yes" : "no",
                mm_base_call_get_path (call));

    /* Early detect if a known incoming call that was created
     * from a plain CRING URC (i.e. without caller number)
     * needs to have the number provided.
     */
    if (call_info->number && !number) {
        mm_obj_dbg (self, "  number set: %s", call_info->number);
        mm_base_call_set_number (call, call_info->number);
    }

    /* Early detect if a known incoming/outgoing call does
     * not have a known call index yet.
     */
    if (call_info->index && !idx) {
        mm_obj_dbg (self, "  index set: %u", call_info->index);
        mm_base_call_set_index (call, call_info->index);
    }

    /* Update state if it changed */
    if (call_info->state != state) {
        mm_obj_dbg (self, "  state updated: %s", mm_call_state_get_string (call_info->state));
        mm_base_call_change_state (call, call_info->state, MM_CALL_STATE_REASON_UNKNOWN);
    }

    /* refresh if incoming and new state is not terminated */
    if ((call_info->state != MM_CALL_STATE_TERMINATED) &&
        (direction == MM_CALL_DIRECTION_INCOMING)) {
        mm_obj_dbg (self, "  incoming refreshed");
        mm_base_call_incoming_refresh (call);
    }

    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MMIfaceModemVoice *self;
    const MMCallInfo  *call_info;
} ReportCallForeachContext;

static void
report_call_foreach (MMBaseCall               *call,
                     ReportCallForeachContext *ctx)
{
    /* Do nothing if already matched */
    if (!ctx->call_info)
        return;

    /* fully ignore already terminated calls */
    if (mm_base_call_get_state (call) == MM_CALL_STATE_TERMINATED)
        return;

    /* Reset call info in context if the call info matches an existing call */
    if (match_single_call_info (ctx->self, ctx->call_info, call))
        ctx->call_info = NULL;
}

void
mm_iface_modem_voice_report_call (MMIfaceModemVoice *self,
                                  const MMCallInfo  *call_info)
{
    ReportCallForeachContext  ctx = { 0 };
    MMBaseCall               *call = NULL;
    MMCallList               *list = NULL;

    /* When reporting single call, the only mandatory parameter is the state:
     *   - index is optional (e.g. unavailable when receiving +CLIP URCs)
     *   - number is optional (e.g. unavailable when receiving +CRING URCs)
     *   - direction is optional (e.g. unavailable when receiving some vendor-specific URCs)
     */
    g_assert (call_info->state != MM_CALL_STATE_UNKNOWN);

    /* Early debugging of the call state update */
    mm_obj_dbg (self, "call at index %u: direction %s, state %s, number %s",
                call_info->index,
                mm_call_direction_get_string (call_info->direction),
                mm_call_state_get_string (call_info->state),
                call_info->number ? call_info->number : "n/a");

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);

    if (!list) {
        mm_obj_warn (self, "cannot process call state update: missing call list");
        return;
    }

    /* Iterate over all known calls and try to match a known one */
    ctx.self = self;
    ctx.call_info = call_info;
    mm_call_list_foreach (list, (MMCallListForeachFunc)report_call_foreach, &ctx);

    /* If call info matched with an existing one, the context call info would have been reseted */
    if (!ctx.call_info)
        goto out;

    /* If call info didn't match with any known call, it may be because we're being
     * reported a NEW incoming call. If that's not the case, we'll ignore the report. */
    if ((call_info->direction != MM_CALL_DIRECTION_INCOMING) ||
        ((call_info->state != MM_CALL_STATE_WAITING) && (call_info->state != MM_CALL_STATE_RINGING_IN))) {
        mm_obj_dbg (self, "unhandled call state update reported: direction: %s, state %s",
                    mm_call_direction_get_string (call_info->direction),
                    mm_call_state_get_string (call_info->state));
        goto out;
    }

    mm_obj_dbg (self, "creating new incoming call...");
    call = create_incoming_call (self, call_info->number);

    /* Set the state */
    mm_base_call_change_state (call, call_info->state, MM_CALL_STATE_REASON_INCOMING_NEW);

    /* Set the index, if known */
    if (call_info->index)
        mm_base_call_set_index (call, call_info->index);

    /* Start its validity timeout */
    mm_base_call_incoming_refresh (call);

    /* Only export once properly created */
    mm_base_call_export (call);
    mm_call_list_add_call (list, call);
    g_object_unref (call);

 out:
    g_object_unref (list);
}

/*****************************************************************************/
/* Full current call list reporting
 *
 * This method receives as input a list with all the currently active calls,
 * including the specific state they're in.
 *
 * This method should:
 *  - Check whether we're reporting a new call (i.e. not in our internal call
 *    list yet). We'll create a new call object if so.
 *  - Check whether any of the known calls has changed state, and if so,
 *    update it.
 *  - Check whether any of the known calls is NOT given in the input list of
 *    call infos, which would mean the call is terminated.
 */

typedef struct {
    MMIfaceModemVoice *self;
    GList             *call_info_list;
} ReportAllCallsForeachContext;

static void
report_all_calls_foreach (MMBaseCall                   *call,
                          ReportAllCallsForeachContext *ctx)
{
    MMCallState state;
    GList *l;

    /* fully ignore already terminated calls */
    state = mm_base_call_get_state (call);
    if (state == MM_CALL_STATE_TERMINATED)
        return;

    /* Iterate over the call info list */
    for (l = ctx->call_info_list; l; l = g_list_next (l)) {
        MMCallInfo *call_info = (MMCallInfo *)(l->data);

        /* if match found, delete item from list and halt iteration right away */
        if (match_single_call_info (ctx->self, call_info, call)) {
            ctx->call_info_list = g_list_delete_link (ctx->call_info_list, l);
            return;
        }
    }

    /* not found in list! this call is now terminated */
    mm_obj_dbg (ctx->self, "call '%s' with direction %s, state %s, number '%s', index %u"
                " not found in list, terminating",
                mm_base_call_get_path (call),
                mm_call_direction_get_string (mm_base_call_get_direction (call)),
                mm_call_state_get_string (state),
                mm_base_call_get_number (call),
                mm_base_call_get_index (call));
    mm_base_call_change_state (call, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_UNKNOWN);
}

void
mm_iface_modem_voice_report_all_calls (MMIfaceModemVoice *self,
                                       GList             *call_info_list)
{
    ReportAllCallsForeachContext  ctx = { 0 };
    MMCallList                   *list = NULL;
    GList                        *l;

    /* Early debugging of the full list of calls */
    mm_obj_dbg (self, "reported %u ongoing calls", g_list_length (call_info_list));
    for (l = call_info_list; l; l = g_list_next (l)) {
        MMCallInfo *call_info = (MMCallInfo *)(l->data);

        /* When reporting full list of calls, index and state are mandatory */
        g_assert (call_info->index != 0);
        g_assert (call_info->state != MM_CALL_STATE_UNKNOWN);

        mm_obj_dbg (self, "call at index %u: direction %s, state %s, number %s",
                    call_info->index,
                    mm_call_direction_get_string (call_info->direction),
                    mm_call_state_get_string (call_info->state),
                    call_info->number ? call_info->number : "n/a");
    }

    /* Retrieve list of known calls */
    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        mm_obj_warn (self, "cannot report all calls: missing call list");
        return;
    }

    /* Iterate over all the calls already known to us.
     * Whenever a known call is updated, it will be removed from the call info list */
    ctx.self = self;
    ctx.call_info_list = g_list_copy (call_info_list);
    mm_call_list_foreach (list, (MMCallListForeachFunc)report_all_calls_foreach, &ctx);

    /* Once processed, the call info list will have all calls that were unknown to
     * us, i.e. the new calls to create. We really only expect new incoming calls, so
     * we'll warn if we get any outgoing call reported here. */
    for (l = ctx.call_info_list; l; l = g_list_next (l)) {
        MMCallInfo *call_info = (MMCallInfo *)(l->data);

        /* Ignore unknown terminated calls, because these be due to an already
         * processed event. */
        if (call_info->state == MM_CALL_STATE_TERMINATED)
            continue;

        if (call_info->direction == MM_CALL_DIRECTION_OUTGOING) {
            mm_obj_warn (self, "unexpected outgoing call to number '%s' reported in call list: state %s",
                         call_info->number ? call_info->number : "n/a",
                         mm_call_state_get_string (call_info->state));
            continue;
        }

        if (call_info->direction == MM_CALL_DIRECTION_INCOMING) {
            MMBaseCall *call;

            /* We only expect either RINGING-IN or WAITING states */
            if ((call_info->state != MM_CALL_STATE_RINGING_IN) &&
                (call_info->state != MM_CALL_STATE_WAITING)) {
                    mm_obj_warn (self, "unexpected incoming call to number '%s' reported in call list: state %s",
                                 call_info->number ? call_info->number : "n/a",
                                 mm_call_state_get_string (call_info->state));
                    continue;
            }

            mm_obj_dbg (self, "creating new incoming call...");
            call = create_incoming_call (self, call_info->number);

            /* Set the state and the index */
            mm_base_call_change_state (call, call_info->state, MM_CALL_STATE_REASON_INCOMING_NEW);
            mm_base_call_set_index (call, call_info->index);

            /* Start its validity timeout */
            mm_base_call_incoming_refresh (call);

            /* Only export once properly created */
            mm_base_call_export (call);
            mm_call_list_add_call (list, call);
            g_object_unref (call);
            continue;
        }

        mm_obj_warn (self, "unexpected call to number '%s' reported in call list: state %s, direction unknown",
                     call_info->number ? call_info->number : "n/a",
                     mm_call_state_get_string (call_info->state));
    }
    g_list_free (ctx.call_info_list);
    g_object_unref (list);
}

/*****************************************************************************/
/* Incoming DTMF reception, not associated to a specific call */

typedef struct {
    guint        index;
    const gchar *dtmf;
} ReceivedDtmfContext;

static void
received_dtmf_foreach (MMBaseCall          *call,
                       ReceivedDtmfContext *ctx)
{
    if ((!ctx->index || (ctx->index == mm_base_call_get_index (call))) &&
        (mm_base_call_get_state (call) == MM_CALL_STATE_ACTIVE))
        mm_base_call_received_dtmf (call, ctx->dtmf);
}

void
mm_iface_modem_voice_received_dtmf (MMIfaceModemVoice *self,
                                    guint              index,
                                    const gchar       *dtmf)
{
    MMCallList          *list = NULL;
    ReceivedDtmfContext  ctx = {
        .index = index,
        .dtmf  = dtmf
    };

    /* Retrieve list of known calls */
    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        mm_obj_warn (self, "cannot report received DTMF: missing call list");
        return;
    }

    mm_call_list_foreach (list, (MMCallListForeachFunc)received_dtmf_foreach, &ctx);
    g_object_unref (list);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemVoice *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemVoice *self;
    gchar *path;
} HandleDeleteContext;

static void
handle_delete_context_free (HandleDeleteContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->path);
    g_free (ctx);
}

static void
handle_delete_auth_ready (MMBaseModem *self,
                          GAsyncResult *res,
                          HandleDeleteContext *ctx)
{
    MMCallList *list = NULL;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot delete call: missing call list");
        handle_delete_context_free (ctx);
        return;
    }

    if (!mm_call_list_delete_call (list, ctx->path, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_voice_complete_delete_call (ctx->skeleton, ctx->invocation);

    handle_delete_context_free (ctx);
    g_object_unref (list);
}

static gboolean
handle_delete (MmGdbusModemVoice *skeleton,
               GDBusMethodInvocation *invocation,
               const gchar *path,
               MMIfaceModemVoice *self)
{
    HandleDeleteContext *ctx;

    ctx = g_new (HandleDeleteContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->path = g_strdup (path);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_delete_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemVoice *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemVoice *self;
    GVariant *dictionary;
} HandleCreateContext;

static void
handle_create_context_free (HandleCreateContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_variant_unref (ctx->dictionary);
    g_free (ctx);
}

static void
handle_create_auth_ready (MMBaseModem *self,
                          GAsyncResult *res,
                          HandleCreateContext *ctx)
{
    MMCallList *list = NULL;
    GError *error = NULL;
    MMCallProperties *properties;
    MMBaseCall *call;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    /* Parse input properties */
    properties = mm_call_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!properties) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    call = create_outgoing_call_from_properties (MM_IFACE_MODEM_VOICE (self), properties, &error);
    if (!call) {
        g_object_unref (properties);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_object_unref (properties);
        g_object_unref (call);
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot create CALL: missing CALL list");
        handle_create_context_free (ctx);
        return;
    }

    /* Only export once properly created */
    mm_base_call_export (call);
    mm_call_list_add_call (list, call);

    /* Complete the DBus call */
    mm_gdbus_modem_voice_complete_create_call (ctx->skeleton,
                                               ctx->invocation,
                                               mm_base_call_get_path (call));
    g_object_unref (call);

    g_object_unref (properties);
    g_object_unref (list);

    handle_create_context_free (ctx);
}

static gboolean
handle_create (MmGdbusModemVoice *skeleton,
               GDBusMethodInvocation *invocation,
               GVariant *dictionary,
               MMIfaceModemVoice *self)
{
    HandleCreateContext *ctx;

    ctx = g_new (HandleCreateContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_create_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static gboolean
handle_list (MmGdbusModemVoice *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModemVoice *self)
{
    GStrv paths;
    MMCallList *list = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot list CALL: missing CALL list");
        return TRUE;
    }

    paths = mm_call_list_get_paths (list);
    mm_gdbus_modem_voice_complete_list_calls (skeleton,
                                              invocation,
                                              (const gchar *const *)paths);
    g_strfreev (paths);
    g_object_unref (list);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemVoice     *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemVoice     *self;
    GList                 *active_calls;
    MMBaseCall            *next_call;
} HandleHoldAndAcceptContext;

static void
handle_hold_and_accept_context_free (HandleHoldAndAcceptContext *ctx)
{
    g_list_free_full (ctx->active_calls, g_object_unref);
    g_clear_object (&ctx->next_call);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleHoldAndAcceptContext, ctx);
}

static void
hold_and_accept_ready (MMIfaceModemVoice          *self,
                       GAsyncResult               *res,
                       HandleHoldAndAcceptContext *ctx)
{
    GError *error = NULL;
    GList  *l;

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hold_and_accept_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_hold_and_accept_context_free (ctx);
        return;
    }

    for (l = ctx->active_calls; l; l = g_list_next (l))
        mm_base_call_change_state (MM_BASE_CALL (l->data), MM_CALL_STATE_HELD, MM_CALL_STATE_REASON_UNKNOWN);
    if (ctx->next_call)
        mm_base_call_change_state (ctx->next_call, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);

    mm_gdbus_modem_voice_complete_hold_and_accept (ctx->skeleton, ctx->invocation);
    handle_hold_and_accept_context_free (ctx);
}

static void
prepare_hold_and_accept_foreach (MMBaseCall                 *call,
                                 HandleHoldAndAcceptContext *ctx)
{
    switch (mm_base_call_get_state (call)) {
    case MM_CALL_STATE_ACTIVE:
        ctx->active_calls = g_list_append (ctx->active_calls, g_object_ref (call));
        break;
    case MM_CALL_STATE_WAITING:
        g_clear_object (&ctx->next_call);
        ctx->next_call = g_object_ref (call);
        break;
    case MM_CALL_STATE_HELD:
        if (!ctx->next_call)
            ctx->next_call = g_object_ref (call);
        break;
    case MM_CALL_STATE_UNKNOWN:
    case MM_CALL_STATE_DIALING:
    case MM_CALL_STATE_RINGING_IN:
    case MM_CALL_STATE_RINGING_OUT:
    case MM_CALL_STATE_TERMINATED:
    default:
        break;
    }
}

static void
handle_hold_and_accept_auth_ready (MMBaseModem                *self,
                                   GAsyncResult               *res,
                                   HandleHoldAndAcceptContext *ctx)
{
    GError     *error = NULL;
    MMCallList *list = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_hold_and_accept_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hold_and_accept ||
        !MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hold_and_accept_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot hold and accept: unsupported");
        handle_hold_and_accept_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot hold and accept: missing call list");
        handle_hold_and_accept_context_free (ctx);
        return;
    }
    mm_call_list_foreach (list, (MMCallListForeachFunc)prepare_hold_and_accept_foreach, ctx);
    g_object_unref (list);

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hold_and_accept (MM_IFACE_MODEM_VOICE (self),
                                                                (GAsyncReadyCallback)hold_and_accept_ready,
                                                                ctx);
}

static gboolean
handle_hold_and_accept (MmGdbusModemVoice     *skeleton,
                        GDBusMethodInvocation *invocation,
                        MMIfaceModemVoice     *self)
{
    HandleHoldAndAcceptContext *ctx;

    ctx = g_slice_new0 (HandleHoldAndAcceptContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_hold_and_accept_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemVoice     *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemVoice     *self;
    GList                 *active_calls;
    MMBaseCall            *next_call;
} HandleHangupAndAcceptContext;

static void
handle_hangup_and_accept_context_free (HandleHangupAndAcceptContext *ctx)
{
    g_list_free_full (ctx->active_calls, g_object_unref);
    g_clear_object (&ctx->next_call);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleHangupAndAcceptContext, ctx);
}

static void
hangup_and_accept_ready (MMIfaceModemVoice            *self,
                         GAsyncResult                 *res,
                         HandleHangupAndAcceptContext *ctx)
{
    GError *error = NULL;
    GList  *l;

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hangup_and_accept_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_hangup_and_accept_context_free (ctx);
        return;
    }

    for (l = ctx->active_calls; l; l = g_list_next (l))
        mm_base_call_change_state (MM_BASE_CALL (l->data), MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);
    if (ctx->next_call)
        mm_base_call_change_state (ctx->next_call, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);

    mm_gdbus_modem_voice_complete_hangup_and_accept (ctx->skeleton, ctx->invocation);
    handle_hangup_and_accept_context_free (ctx);
}

static void
prepare_hangup_and_accept_foreach (MMBaseCall                   *call,
                                   HandleHangupAndAcceptContext *ctx)
{
    switch (mm_base_call_get_state (call)) {
    case MM_CALL_STATE_ACTIVE:
        ctx->active_calls = g_list_append (ctx->active_calls, g_object_ref (call));
        break;
    case MM_CALL_STATE_WAITING:
        g_clear_object (&ctx->next_call);
        ctx->next_call = g_object_ref (call);
        break;
    case MM_CALL_STATE_HELD:
        if (!ctx->next_call)
            ctx->next_call = g_object_ref (call);
        break;
    case MM_CALL_STATE_UNKNOWN:
    case MM_CALL_STATE_DIALING:
    case MM_CALL_STATE_RINGING_IN:
    case MM_CALL_STATE_RINGING_OUT:
    case MM_CALL_STATE_TERMINATED:
    default:
        break;
    }
}

static void
handle_hangup_and_accept_auth_ready (MMBaseModem                  *self,
                                     GAsyncResult                 *res,
                                     HandleHangupAndAcceptContext *ctx)
{
    GError     *error = NULL;
    MMCallList *list = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_hangup_and_accept_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hangup_and_accept ||
        !MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hangup_and_accept_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot hangup and accept: unsupported");
        handle_hangup_and_accept_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot hangup and accept: missing call list");
        handle_hangup_and_accept_context_free (ctx);
        return;
    }
    mm_call_list_foreach (list, (MMCallListForeachFunc)prepare_hangup_and_accept_foreach, ctx);
    g_object_unref (list);

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hangup_and_accept (MM_IFACE_MODEM_VOICE (self),
                                                                  (GAsyncReadyCallback)hangup_and_accept_ready,
                                                                  ctx);
}

static gboolean
handle_hangup_and_accept (MmGdbusModemVoice     *skeleton,
                          GDBusMethodInvocation *invocation,
                          MMIfaceModemVoice     *self)
{
    HandleHangupAndAcceptContext *ctx;

    ctx = g_slice_new0 (HandleHangupAndAcceptContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_hangup_and_accept_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemVoice     *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemVoice     *self;
    GList                 *calls;
} HandleHangupAllContext;

static void
handle_hangup_all_context_free (HandleHangupAllContext *ctx)
{
    g_list_free_full (ctx->calls, g_object_unref);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleHangupAllContext, ctx);
}

static void
hangup_all_ready (MMIfaceModemVoice      *self,
                  GAsyncResult           *res,
                  HandleHangupAllContext *ctx)
{
    GError *error = NULL;
    GList  *l;

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hangup_all_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_hangup_all_context_free (ctx);
        return;
    }

    for (l = ctx->calls; l; l = g_list_next (l))
        mm_base_call_change_state (MM_BASE_CALL (l->data), MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);

    mm_gdbus_modem_voice_complete_hangup_all (ctx->skeleton, ctx->invocation);
    handle_hangup_all_context_free (ctx);
}

static void
prepare_hangup_all_foreach (MMBaseCall             *call,
                            HandleHangupAllContext *ctx)
{
    /* The implementation of this operation will usually be done with +CHUP, and we
     * know that +CHUP is implemented in different ways by different manufacturers.
     *
     * The 3GPP TS27.007 spec for +CHUP states that the "Execution command causes
     * the TA to hangup the current call of the MT." This sentence leaves a bit of open
     * interpretation to the implementors, because a current call can be considered only
     * the active ones, or otherwise any call (active, held or waiting).
     *
     * And so, the u-blox TOBY-L4 takes one interpretation and "In case of multiple
     * calls, all active calls will be released, while waiting and held calls are not".
     *
     * And the Cinterion PLS-8 takes a different interpretation and cancels all calls,
     * including the waiting and held ones.
     *
     * In this logic, we're going to terminate exclusively the ACTIVE calls only, and we
     * will leave the possible termination of waiting/held calls to be reported via
     * call state updates, e.g. +CLCC polling or other plugin-specific method. In the
     * case of the Cinterion PLS-8, we'll detect the termination of the waiting and
     * held calls via ^SLCC URCs.
     */
    switch (mm_base_call_get_state (call)) {
    case MM_CALL_STATE_DIALING:
    case MM_CALL_STATE_RINGING_OUT:
    case MM_CALL_STATE_RINGING_IN:
    case MM_CALL_STATE_ACTIVE:
        ctx->calls = g_list_append (ctx->calls, g_object_ref (call));
        break;
    case MM_CALL_STATE_WAITING:
    case MM_CALL_STATE_HELD:
    case MM_CALL_STATE_UNKNOWN:
    case MM_CALL_STATE_TERMINATED:
    default:
        break;
    }
}

static void
handle_hangup_all_auth_ready (MMBaseModem            *self,
                              GAsyncResult           *res,
                              HandleHangupAllContext *ctx)
{
    GError     *error = NULL;
    MMCallList *list = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_hangup_all_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hangup_all ||
        !MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hangup_all_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot hangup all: unsupported");
        handle_hangup_all_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot hangup all: missing call list");
        handle_hangup_all_context_free (ctx);
        return;
    }
    mm_call_list_foreach (list, (MMCallListForeachFunc)prepare_hangup_all_foreach, ctx);
    g_object_unref (list);

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->hangup_all (MM_IFACE_MODEM_VOICE (self),
                                                           (GAsyncReadyCallback)hangup_all_ready,
                                                           ctx);
}

static gboolean
handle_hangup_all (MmGdbusModemVoice     *skeleton,
                   GDBusMethodInvocation *invocation,
                   MMIfaceModemVoice     *self)
{
    HandleHangupAllContext *ctx;

    ctx = g_slice_new0 (HandleHangupAllContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_hangup_all_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemVoice     *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemVoice     *self;
    GList                 *calls;
} HandleTransferContext;

static void
handle_transfer_context_free (HandleTransferContext *ctx)
{
    g_list_free_full (ctx->calls, g_object_unref);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleTransferContext, ctx);
}

static void
transfer_ready (MMIfaceModemVoice     *self,
                GAsyncResult          *res,
                HandleTransferContext *ctx)
{
    GError *error = NULL;
    GList  *l;

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->transfer_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_transfer_context_free (ctx);
        return;
    }

    for (l = ctx->calls; l; l = g_list_next (l))
        mm_base_call_change_state (MM_BASE_CALL (l->data), MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TRANSFERRED);

    mm_gdbus_modem_voice_complete_transfer (ctx->skeleton, ctx->invocation);
    handle_transfer_context_free (ctx);
}

static void
prepare_transfer_foreach (MMBaseCall            *call,
                          HandleTransferContext *ctx)
{
    switch (mm_base_call_get_state (call)) {
    case MM_CALL_STATE_ACTIVE:
    case MM_CALL_STATE_HELD:
        ctx->calls = g_list_append (ctx->calls, g_object_ref (call));
        break;
    case MM_CALL_STATE_UNKNOWN:
    case MM_CALL_STATE_DIALING:
    case MM_CALL_STATE_WAITING:
    case MM_CALL_STATE_RINGING_IN:
    case MM_CALL_STATE_RINGING_OUT:
    case MM_CALL_STATE_TERMINATED:
    default:
        break;
    }
}

static void
handle_transfer_auth_ready (MMBaseModem           *self,
                            GAsyncResult          *res,
                            HandleTransferContext *ctx)
{
    GError     *error = NULL;
    MMCallList *list = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_transfer_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->transfer ||
        !MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->transfer_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot transfer: unsupported");
        handle_transfer_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot transfer: missing call list");
        handle_transfer_context_free (ctx);
        return;
    }
    mm_call_list_foreach (list, (MMCallListForeachFunc)prepare_transfer_foreach, ctx);
    g_object_unref (list);

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->transfer (MM_IFACE_MODEM_VOICE (self),
                                                         (GAsyncReadyCallback)transfer_ready,
                                                         ctx);
}

static gboolean
handle_transfer (MmGdbusModemVoice     *skeleton,
                 GDBusMethodInvocation *invocation,
                 MMIfaceModemVoice     *self)
{
    HandleTransferContext *ctx;

    ctx = g_slice_new0 (HandleTransferContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_transfer_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemVoice     *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemVoice     *self;
    gboolean               enable;
} HandleCallWaitingSetupContext;

static void
handle_call_waiting_setup_context_free (HandleCallWaitingSetupContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleCallWaitingSetupContext, ctx);
}

static void
call_waiting_setup_ready (MMIfaceModemVoice             *self,
                          GAsyncResult                  *res,
                          HandleCallWaitingSetupContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->call_waiting_setup_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_call_waiting_setup_context_free (ctx);
        return;
    }

    mm_gdbus_modem_voice_complete_call_waiting_setup (ctx->skeleton, ctx->invocation);
    handle_call_waiting_setup_context_free (ctx);
}

static void
handle_call_waiting_setup_auth_ready (MMBaseModem                   *self,
                                      GAsyncResult                  *res,
                                      HandleCallWaitingSetupContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_call_waiting_setup_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->call_waiting_setup ||
        !MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->call_waiting_setup_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot setup call waiting: unsupported");
        handle_call_waiting_setup_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->call_waiting_setup (MM_IFACE_MODEM_VOICE (self),
                                                                   ctx->enable,
                                                                   (GAsyncReadyCallback)call_waiting_setup_ready,
                                                                   ctx);
}

static gboolean
handle_call_waiting_setup (MmGdbusModemVoice     *skeleton,
                           GDBusMethodInvocation *invocation,
                           gboolean               enable,
                           MMIfaceModemVoice     *self)
{
    HandleCallWaitingSetupContext *ctx;

    ctx = g_slice_new0 (HandleCallWaitingSetupContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);
    ctx->enable     = enable;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_call_waiting_setup_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemVoice     *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemVoice     *self;
    gboolean               enable;
} HandleCallWaitingQueryContext;

static void
handle_call_waiting_query_context_free (HandleCallWaitingQueryContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleCallWaitingQueryContext, ctx);
}

static void
call_waiting_query_ready (MMIfaceModemVoice             *self,
                          GAsyncResult                  *res,
                          HandleCallWaitingQueryContext *ctx)
{
    GError   *error = NULL;
    gboolean  status = FALSE;

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->call_waiting_query_finish (self, res, &status, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_call_waiting_query_context_free (ctx);
        return;
    }

    mm_gdbus_modem_voice_complete_call_waiting_query (ctx->skeleton, ctx->invocation, status);
    handle_call_waiting_query_context_free (ctx);
}

static void
handle_call_waiting_query_auth_ready (MMBaseModem                   *self,
                                      GAsyncResult                  *res,
                                      HandleCallWaitingQueryContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_call_waiting_query_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->call_waiting_query ||
        !MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->call_waiting_query_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot query call waiting: unsupported");
        handle_call_waiting_query_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->call_waiting_query (MM_IFACE_MODEM_VOICE (self),
                                                                   (GAsyncReadyCallback)call_waiting_query_ready,
                                                                   ctx);
}

static gboolean
handle_call_waiting_query (MmGdbusModemVoice     *skeleton,
                           GDBusMethodInvocation *invocation,
                           MMIfaceModemVoice     *self)
{
    HandleCallWaitingQueryContext *ctx;

    ctx = g_slice_new0 (HandleCallWaitingQueryContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_VOICE,
                             (GAsyncReadyCallback)handle_call_waiting_query_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Leave one of the calls from the multiparty call */

typedef struct {
    MMBaseCall *call;
    GList      *other_calls;
} LeaveMultipartyContext;

static void
leave_multiparty_context_free (LeaveMultipartyContext *ctx)
{
    g_list_free_full (ctx->other_calls, g_object_unref);
    g_object_unref (ctx->call);
    g_slice_free (LeaveMultipartyContext, ctx);
}

static void
prepare_leave_multiparty_foreach (MMBaseCall             *call,
                                  LeaveMultipartyContext *ctx)
{
    /* ignore call that is leaving */
    if ((call == ctx->call) || (g_strcmp0 (mm_base_call_get_path (call), mm_base_call_get_path (ctx->call)) == 0))
        return;

    /* ignore non-multiparty calls */
    if (!mm_base_call_get_multiparty (call))
        return;

    /* ignore calls not currently ongoing */
    switch (mm_base_call_get_state (call)) {
    case MM_CALL_STATE_ACTIVE:
    case MM_CALL_STATE_HELD:
        ctx->other_calls = g_list_append (ctx->other_calls, g_object_ref (call));
        break;
    case MM_CALL_STATE_UNKNOWN:
    case MM_CALL_STATE_DIALING:
    case MM_CALL_STATE_WAITING:
    case MM_CALL_STATE_RINGING_IN:
    case MM_CALL_STATE_RINGING_OUT:
    case MM_CALL_STATE_TERMINATED:
    default:
        break;
    }
}

gboolean
mm_iface_modem_voice_leave_multiparty_finish (MMIfaceModemVoice  *self,
                                              GAsyncResult       *res,
                                              GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
leave_multiparty_ready (MMIfaceModemVoice *self,
                        GAsyncResult      *res,
                        GTask             *task)
{
    GError                 *error = NULL;
    LeaveMultipartyContext *ctx;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->leave_multiparty_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* If there is only one remaining call that was part of the multiparty, consider that
     * one also no longer part of any multiparty, and put it on hold right away */
    if (g_list_length (ctx->other_calls) == 1) {
        mm_base_call_set_multiparty (MM_BASE_CALL (ctx->other_calls->data), FALSE);
        mm_base_call_change_state (MM_BASE_CALL (ctx->other_calls->data), MM_CALL_STATE_HELD, MM_CALL_STATE_REASON_UNKNOWN);
    }
    /* If there are still more than one calls in the multiparty, just change state of all
     * of them. */
    else {
        GList *l;

        for (l = ctx->other_calls; l; l = g_list_next (l))
            mm_base_call_change_state (MM_BASE_CALL (l->data), MM_CALL_STATE_HELD, MM_CALL_STATE_REASON_UNKNOWN);
    }

    /* The call that left would now be active */
    mm_base_call_set_multiparty (ctx->call, FALSE);
    mm_base_call_change_state (ctx->call, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_UNKNOWN);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_iface_modem_voice_leave_multiparty (MMIfaceModemVoice   *self,
                                       MMBaseCall          *call,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
    GTask                  *task;
    LeaveMultipartyContext *ctx;
    MMCallList             *list = NULL;
    MMCallState             call_state;

    task = g_task_new (self, NULL, callback, user_data);

    /* validate multiparty status */
    if (!mm_base_call_get_multiparty (call)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "this call is not part of a multiparty call");
        g_object_unref (task);
        return;
    }
    /* validate call state */
    call_state = mm_base_call_get_state (call);
    if ((call_state != MM_CALL_STATE_ACTIVE) && (call_state != MM_CALL_STATE_HELD)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "invalid call state (%s): must be either active or held",
                                 mm_call_state_get_string (call_state));
        g_object_unref (task);
        return;
    }

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->leave_multiparty ||
        !MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->leave_multiparty_finish) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Cannot leave multiparty: unsupported");
        g_object_unref (task);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "Cannot leave multiparty: missing call list");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (LeaveMultipartyContext);
    ctx->call = g_object_ref (call);
    g_task_set_task_data (task, ctx, (GDestroyNotify) leave_multiparty_context_free);

    mm_call_list_foreach (list, (MMCallListForeachFunc)prepare_leave_multiparty_foreach, ctx);
    g_object_unref (list);

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->leave_multiparty (self,
                                                                 call,
                                                                 (GAsyncReadyCallback)leave_multiparty_ready,
                                                                 task);
}

/*****************************************************************************/
/* Join calls into a multiparty call */

typedef struct {
    MMBaseCall *call;
    GList      *all_calls;
    gboolean    added;
} JoinMultipartyContext;

static void
join_multiparty_context_free (JoinMultipartyContext *ctx)
{
    g_list_free_full (ctx->all_calls, g_object_unref);
    g_object_unref (ctx->call);
    g_slice_free (JoinMultipartyContext, ctx);
}

static void
prepare_join_multiparty_foreach (MMBaseCall            *call,
                                 JoinMultipartyContext *ctx)
{
    /* always add call that is being added */
    if ((call == ctx->call) || (g_strcmp0 (mm_base_call_get_path (call), mm_base_call_get_path (ctx->call)) == 0))
        ctx->added = TRUE;

    /* ignore calls not currently ongoing */
    switch (mm_base_call_get_state (call)) {
    case MM_CALL_STATE_ACTIVE:
    case MM_CALL_STATE_HELD:
        ctx->all_calls = g_list_append (ctx->all_calls, g_object_ref (call));
        break;
    case MM_CALL_STATE_UNKNOWN:
    case MM_CALL_STATE_DIALING:
    case MM_CALL_STATE_RINGING_IN:
    case MM_CALL_STATE_RINGING_OUT:
    case MM_CALL_STATE_WAITING:
    case MM_CALL_STATE_TERMINATED:
    default:
        break;
    }
}

gboolean
mm_iface_modem_voice_join_multiparty_finish (MMIfaceModemVoice  *self,
                                             GAsyncResult       *res,
                                             GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
join_multiparty_ready (MMIfaceModemVoice *self,
                       GAsyncResult      *res,
                       GTask             *task)
{
    GError                *error = NULL;
    JoinMultipartyContext *ctx;
    GList                 *l;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->join_multiparty_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    for (l = ctx->all_calls; l; l = g_list_next (l)) {
        mm_base_call_set_multiparty (MM_BASE_CALL (l->data), TRUE);
        mm_base_call_change_state (MM_BASE_CALL (l->data), MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_UNKNOWN);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_iface_modem_voice_join_multiparty (MMIfaceModemVoice   *self,
                                      MMBaseCall          *call,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    GTask                 *task;
    JoinMultipartyContext *ctx;
    MMCallList            *list = NULL;
    MMCallState            call_state;

    task = g_task_new (self, NULL, callback, user_data);

    /* validate multiparty status */
    if (mm_base_call_get_multiparty (call)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "this call is already part of a multiparty call");
        g_object_unref (task);
        return;
    }
    /* validate call state */
    call_state = mm_base_call_get_state (call);
    if (call_state != MM_CALL_STATE_HELD) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "invalid call state (%s): must be held",
                                 mm_call_state_get_string (call_state));
        g_object_unref (task);
        return;
    }

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->join_multiparty ||
        !MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->join_multiparty_finish) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Cannot join multiparty: unsupported");
        g_object_unref (task);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "Cannot join multiparty: missing call list");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (JoinMultipartyContext);
    ctx->call = g_object_ref (call);
    g_task_set_task_data (task, ctx, (GDestroyNotify) join_multiparty_context_free);

    mm_call_list_foreach (list, (MMCallListForeachFunc)prepare_join_multiparty_foreach, ctx);
    g_object_unref (list);

    /* our logic makes sure that we would be adding the incoming call into the multiparty call */
    g_assert (ctx->added);

    /* NOTE: we do not give the call we want to join, because the join operation acts on all
     * active/held calls. */
    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->join_multiparty (self,
                                                                (GAsyncReadyCallback)join_multiparty_ready,
                                                                task);
}

/*****************************************************************************/
/* In-call setup operation
 *
 * It will setup URC handlers for all in-call URCs, and also setup the audio
 * channel if the plugin requires to do so.
 */

typedef enum {
    IN_CALL_SETUP_STEP_FIRST,
    IN_CALL_SETUP_STEP_UNSOLICITED_EVENTS,
    IN_CALL_SETUP_STEP_AUDIO_CHANNEL,
    IN_CALL_SETUP_STEP_LAST,
} InCallSetupStep;

typedef struct {
    InCallSetupStep    step;
    MMPort            *audio_port;
    MMCallAudioFormat *audio_format;
} InCallSetupContext;

static void
in_call_setup_context_free (InCallSetupContext *ctx)
{
    g_clear_object (&ctx->audio_port);
    g_clear_object (&ctx->audio_format);
    g_slice_free (InCallSetupContext, ctx);
}

static gboolean
in_call_setup_finish (MMIfaceModemVoice  *self,
                      GAsyncResult       *res,
                      MMPort            **audio_port,   /* optional */
                      MMCallAudioFormat **audio_format, /* optional */
                      GError            **error)
{
    InCallSetupContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    if (audio_port) {
        *audio_port = ctx->audio_port;
        ctx->audio_port = NULL;
    }
    if (audio_format) {
        *audio_format = ctx->audio_format;
        ctx->audio_format = NULL;
    }

    return TRUE;
}

static void in_call_setup_context_step (GTask *task);

static void
setup_in_call_audio_channel_ready (MMIfaceModemVoice *self,
                                   GAsyncResult      *res,
                                   GTask             *task)
{
    InCallSetupContext *ctx;
    GError             *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_in_call_audio_channel_finish (self,
                                                                                        res,
                                                                                        &ctx->audio_port,
                                                                                        &ctx->audio_format,
                                                                                        &error)) {
        mm_obj_warn (self, "couldn't setup in-call audio channel: %s", error->message);
        g_clear_error (&error);
    }

    ctx->step++;
    in_call_setup_context_step (task);
}

static void
setup_in_call_unsolicited_events_ready (MMIfaceModemVoice *self,
                                        GAsyncResult      *res,
                                        GTask             *task)
{
    InCallSetupContext *ctx;
    GError             *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_in_call_unsolicited_events_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't setup in-call unsolicited events: %s", error->message);
        g_clear_error (&error);
    }

    ctx->step++;
    in_call_setup_context_step (task);
}

static void
in_call_setup_context_step (GTask *task)
{
    MMIfaceModemVoice  *self;
    InCallSetupContext *ctx;

    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case IN_CALL_SETUP_STEP_FIRST:
        ctx->step++;
        /* fall-through */
    case IN_CALL_SETUP_STEP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_in_call_unsolicited_events &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_in_call_unsolicited_events_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_in_call_unsolicited_events (
                self,
                (GAsyncReadyCallback) setup_in_call_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */
    case IN_CALL_SETUP_STEP_AUDIO_CHANNEL:
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_in_call_audio_channel &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_in_call_audio_channel_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_in_call_audio_channel (
                self,
                (GAsyncReadyCallback) setup_in_call_audio_channel_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */
    case IN_CALL_SETUP_STEP_LAST:
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

static void
in_call_setup (MMIfaceModemVoice   *self,
               GCancellable        *cancellable,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    GTask              *task;
    InCallSetupContext *ctx;

    ctx = g_slice_new0 (InCallSetupContext);
    ctx->step = IN_CALL_SETUP_STEP_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) in_call_setup_context_free);

    in_call_setup_context_step (task);
}

/*****************************************************************************/
/* In-call cleanup operation
 *
 * It will cleanup audio channel settings and remove all in-call URC handlers.
 */

typedef enum {
    IN_CALL_CLEANUP_STEP_FIRST,
    IN_CALL_CLEANUP_STEP_AUDIO_CHANNEL,
    IN_CALL_CLEANUP_STEP_UNSOLICITED_EVENTS,
    IN_CALL_CLEANUP_STEP_LAST,
} InCallCleanupStep;

typedef struct {
    InCallCleanupStep step;
} InCallCleanupContext;

static gboolean
in_call_cleanup_finish (MMIfaceModemVoice  *self,
                        GAsyncResult       *res,
                        GError            **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void in_call_cleanup_context_step (GTask *task);

static void
cleanup_in_call_unsolicited_events_ready (MMIfaceModemVoice *self,
                                          GAsyncResult      *res,
                                          GTask             *task)
{
    InCallCleanupContext *ctx;
    GError             *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_in_call_unsolicited_events_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't cleanup in-call unsolicited events: %s", error->message);
        g_clear_error (&error);
    }

    ctx->step++;
    in_call_cleanup_context_step (task);
}

static void
cleanup_in_call_audio_channel_ready (MMIfaceModemVoice *self,
                                     GAsyncResult      *res,
                                     GTask             *task)
{
    InCallCleanupContext *ctx;
    GError             *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_in_call_audio_channel_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't cleanup in-call audio channel: %s", error->message);
        g_clear_error (&error);
    }

    ctx->step++;
    in_call_cleanup_context_step (task);
}

static void
in_call_cleanup_context_step (GTask *task)
{
    MMIfaceModemVoice    *self;
    InCallCleanupContext *ctx;

    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case IN_CALL_CLEANUP_STEP_FIRST:
        ctx->step++;
        /* fall-through */
    case IN_CALL_CLEANUP_STEP_AUDIO_CHANNEL:
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_in_call_audio_channel &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_in_call_audio_channel_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_in_call_audio_channel (
                self,
                (GAsyncReadyCallback) cleanup_in_call_audio_channel_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */
    case IN_CALL_CLEANUP_STEP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_in_call_unsolicited_events &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_in_call_unsolicited_events_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_in_call_unsolicited_events (
                self,
                (GAsyncReadyCallback) cleanup_in_call_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */
    case IN_CALL_CLEANUP_STEP_LAST:
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

static void
in_call_cleanup (MMIfaceModemVoice   *self,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    GTask                *task;
    InCallCleanupContext *ctx;

    ctx = g_new0 (InCallCleanupContext, 1);
    ctx->step = IN_CALL_CLEANUP_STEP_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    in_call_cleanup_context_step (task);
}

/*****************************************************************************/
/* In-call event handling logic
 *
 * This procedure will run a in-call setup async function whenever we detect
 * that there is at least one call that is ongoing. This setup function will
 * try to setup in-call unsolicited events as well as any audio channel
 * requirements.
 *
 * The procedure will run a in-call cleanup async function whenever we detect
 * that there are no longer any ongoing calls. The cleanup function will
 * cleanup the audio channel and remove the in-call unsolicited event handlers.
 */

typedef struct {
    guint              check_id;
    GCancellable      *setup_cancellable;
    GCancellable      *cleanup_cancellable;
    gboolean           in_call_state;
    MMPort            *audio_port;
    MMCallAudioFormat *audio_format;
} InCallEventContext;

static void
in_call_event_context_free (InCallEventContext *ctx)
{
    if (ctx->check_id)
        g_source_remove (ctx->check_id);
    if (ctx->cleanup_cancellable) {
        g_cancellable_cancel (ctx->cleanup_cancellable);
        g_clear_object (&ctx->cleanup_cancellable);
    }
    if (ctx->setup_cancellable) {
        g_cancellable_cancel (ctx->setup_cancellable);
        g_clear_object (&ctx->setup_cancellable);
    }
    g_clear_object (&ctx->audio_port);
    g_clear_object (&ctx->audio_format);
    g_slice_free (InCallEventContext, ctx);
}

static InCallEventContext *
get_in_call_event_context (MMIfaceModemVoice *self)
{
    InCallEventContext *ctx;

    if (G_UNLIKELY (!in_call_event_context_quark))
        in_call_event_context_quark = g_quark_from_static_string (IN_CALL_EVENT_CONTEXT_TAG);

    ctx = g_object_get_qdata (G_OBJECT (self), in_call_event_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_slice_new0 (InCallEventContext);
        g_object_set_qdata_full (
            G_OBJECT (self),
            in_call_event_context_quark,
            ctx,
            (GDestroyNotify)in_call_event_context_free);
    }

    return ctx;
}

static void
call_list_foreach_audio_settings (MMBaseCall         *call,
                                  InCallEventContext *ctx)
{
    if (mm_base_call_get_state (call) != MM_CALL_STATE_TERMINATED)
        return;
    mm_base_call_change_audio_settings (call, ctx->audio_port, ctx->audio_format);
}

static void
update_audio_settings_in_ongoing_calls (MMIfaceModemVoice *self)
{
    MMCallList         *list = NULL;
    InCallEventContext *ctx;

    ctx = get_in_call_event_context (self);

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        mm_obj_warn (self, "cannot update audio settings in active calls: missing internal call list");
        return;
    }

    mm_call_list_foreach (list, (MMCallListForeachFunc) call_list_foreach_audio_settings, ctx);
    g_clear_object (&list);
}

static void
update_audio_settings_in_call (MMIfaceModemVoice *self,
                               MMBaseCall        *call)
{
    InCallEventContext *ctx;

    ctx = get_in_call_event_context (self);
    mm_base_call_change_audio_settings (call, ctx->audio_port, ctx->audio_format);
}

static void
call_list_foreach_count_in_call (MMBaseCall *call,
                                 gpointer    user_data)
{
    guint *n_calls_in_call = (guint *)user_data;

    switch (mm_base_call_get_state (call)) {
    case MM_CALL_STATE_DIALING:
    case MM_CALL_STATE_RINGING_OUT:
    case MM_CALL_STATE_HELD:
    case MM_CALL_STATE_ACTIVE:
        *n_calls_in_call = *n_calls_in_call + 1;
        break;
    case MM_CALL_STATE_RINGING_IN:
    case MM_CALL_STATE_WAITING:
        /* NOTE: ringing-in and waiting calls are NOT yet in-call, e.g. there must
         * be no audio settings enabled and we must not enable in-call URC handling
         * yet. */
    case MM_CALL_STATE_UNKNOWN:
    case MM_CALL_STATE_TERMINATED:
    default:
        break;
    }
}

static void
in_call_cleanup_ready (MMIfaceModemVoice *self,
                       GAsyncResult      *res)
{
    GError             *error = NULL;
    InCallEventContext *ctx;

    ctx = get_in_call_event_context (self);

    if (!in_call_cleanup_finish (self, res, &error)) {
        /* ignore cancelled operations */
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            mm_obj_warn (self, "cannot cleanup in-call modem state: %s", error->message);
        g_clear_error (&error);
    } else {
        mm_obj_dbg (self, "modem is no longer in-call state");
        ctx->in_call_state = FALSE;
        g_clear_object (&ctx->audio_port);
        g_clear_object (&ctx->audio_format);
    }

    g_clear_object (&ctx->cleanup_cancellable);
}

static void
in_call_setup_ready (MMIfaceModemVoice *self,
                     GAsyncResult      *res)
{
    GError             *error = NULL;
    InCallEventContext *ctx;

    ctx = get_in_call_event_context (self);

    if (!in_call_setup_finish (self, res, &ctx->audio_port, &ctx->audio_format, &error)) {
        /* ignore cancelled operations */
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            mm_obj_warn (self, "cannot setup in-call modem state: %s", error->message);
        g_clear_error (&error);
    } else {
        mm_obj_dbg (self, "modem is now in-call state");
        ctx->in_call_state = TRUE;
        update_audio_settings_in_ongoing_calls (self);
    }

    g_clear_object (&ctx->setup_cancellable);
}

static gboolean
call_list_check_in_call_events (MMIfaceModemVoice *self)
{
    InCallEventContext *ctx;
    MMCallList         *list = NULL;
    guint               n_calls_in_call = 0;

    ctx = get_in_call_event_context (self);
    ctx->check_id = 0;

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);
    if (!list) {
        mm_obj_warn (self, "cannot update in-call state: missing internal call list");
        goto out;
    }

    mm_call_list_foreach (list, (MMCallListForeachFunc) call_list_foreach_count_in_call, &n_calls_in_call);

    /* Need to setup in-call events? */
    if (n_calls_in_call > 0 && !ctx->in_call_state) {
        /* if setup already ongoing, do nothing */
        if (ctx->setup_cancellable)
            goto out;

        /* cancel ongoing cleanup if any */
        if (ctx->cleanup_cancellable) {
            g_cancellable_cancel (ctx->cleanup_cancellable);
            g_clear_object (&ctx->cleanup_cancellable);
        }

        /* run setup */
        mm_obj_dbg (self, "setting up in-call state...");
        ctx->setup_cancellable = g_cancellable_new ();
        in_call_setup (self, ctx->setup_cancellable, (GAsyncReadyCallback) in_call_setup_ready, NULL);
        goto out;
    }

    /* Need to cleanup in-call events? */
    if (n_calls_in_call == 0 && ctx->in_call_state) {
        /* if cleanup already ongoing, do nothing */
        if (ctx->cleanup_cancellable)
            goto out;

        /* cancel ongoing setup if any */
        if (ctx->setup_cancellable) {
            g_cancellable_cancel (ctx->setup_cancellable);
            g_clear_object (&ctx->setup_cancellable);
        }

        /* run cleanup */
        mm_obj_dbg (self, "cleaning up in-call state...");
        ctx->cleanup_cancellable = g_cancellable_new ();
        in_call_cleanup (self, ctx->cleanup_cancellable, (GAsyncReadyCallback) in_call_cleanup_ready, NULL);
        goto out;
    }

 out:
    g_clear_object (&list);
    return G_SOURCE_REMOVE;
}

static void
call_state_changed (MMIfaceModemVoice *self)
{
    InCallEventContext *ctx;

    ctx = get_in_call_event_context (self);
    if (ctx->check_id)
        return;

    /* Process check for in-call events in an idle, so that we can combine
     * together in the same check multiple call state updates happening
     * at the same time for different calls (e.g. when swapping active/held
     * calls). */
    ctx->check_id = g_idle_add ((GSourceFunc)call_list_check_in_call_events, self);
}

static void
setup_in_call_event_handling (MMCallList        *call_list,
                              const gchar       *call_path_added,
                              MMIfaceModemVoice *self)
{
    MMBaseCall *call;

    call = mm_call_list_get_call (call_list, call_path_added);
    g_assert (call);

    g_signal_connect_swapped (call,
                              "state-changed",
                              G_CALLBACK (call_state_changed),
                              self);
}

/*****************************************************************************/
/* Call list polling logic
 *
 * The call list polling is exclusively used to detect detailed call state
 * updates while a call is being established. Therefore, if there is no call
 * being established (i.e. all terminated, unknown or active), then there is
 * no polling to do.
 *
 * Any time we add a new call to the list, we'll setup polling if it's not
 * already running, and the polling logic itself will decide when the polling
 * should stop.
 */

#define CALL_LIST_POLLING_TIMEOUT_SECS 2

typedef struct {
    guint    polling_id;
    gboolean polling_ongoing;
} CallListPollingContext;

static void
call_list_polling_context_free (CallListPollingContext *ctx)
{
    if (ctx->polling_id)
        g_source_remove (ctx->polling_id);
    g_slice_free (CallListPollingContext, ctx);
}

static CallListPollingContext *
get_call_list_polling_context (MMIfaceModemVoice *self)
{
    CallListPollingContext *ctx;

    if (G_UNLIKELY (!call_list_polling_context_quark))
        call_list_polling_context_quark =  (g_quark_from_static_string (
                                                 CALL_LIST_POLLING_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), call_list_polling_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_slice_new0 (CallListPollingContext);

        g_object_set_qdata_full (
            G_OBJECT (self),
            call_list_polling_context_quark,
            ctx,
            (GDestroyNotify)call_list_polling_context_free);
    }

    return ctx;
}

static gboolean call_list_poll (MMIfaceModemVoice *self);

static void
load_call_list_ready (MMIfaceModemVoice *self,
                      GAsyncResult      *res)
{
    CallListPollingContext *ctx;
    GList                  *call_info_list = NULL;
    GError                 *error = NULL;

    ctx = get_call_list_polling_context (self);
    ctx->polling_ongoing = FALSE;

    g_assert (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->load_call_list_finish);
    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->load_call_list_finish (self, res, &call_info_list, &error)) {
        mm_obj_warn (self, "couldn't load call list: %s", error->message);
        g_error_free (error);
    } else {
        /* Always report the list even if NULL (it would mean no ongoing calls) */
        mm_iface_modem_voice_report_all_calls (self, call_info_list);
        mm_3gpp_call_info_list_free (call_info_list);
    }

    /* setup the polling again, but only if it hasn't been done already while
     * we reported calls (e.g. a new incoming call may have been detected that
     * also triggers the poll setup) */
    if (!ctx->polling_id)
        ctx->polling_id = g_timeout_add_seconds (CALL_LIST_POLLING_TIMEOUT_SECS,
                                                 (GSourceFunc) call_list_poll,
                                                 self);
}

static void
call_list_foreach_count_establishing (MMBaseCall *call,
                                      gpointer    user_data)
{
    guint *n_calls_establishing = (guint *)user_data;

    switch (mm_base_call_get_state (call)) {
    case MM_CALL_STATE_DIALING:
    case MM_CALL_STATE_RINGING_OUT:
    case MM_CALL_STATE_RINGING_IN:
    case MM_CALL_STATE_HELD:
    case MM_CALL_STATE_WAITING:
        *n_calls_establishing = *n_calls_establishing + 1;
        break;
    case MM_CALL_STATE_ACTIVE:
    case MM_CALL_STATE_TERMINATED:
    case MM_CALL_STATE_UNKNOWN:
    default:
        break;
    }
}

static gboolean
call_list_poll (MMIfaceModemVoice *self)
{
    CallListPollingContext *ctx;
    MMCallList             *list = NULL;
    guint                   n_calls_establishing = 0;

    ctx = get_call_list_polling_context (self);
    ctx->polling_id = 0;

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);

    if (!list) {
        mm_obj_warn (self, "Cannot poll call list: missing internal call list");
        goto out;
    }

    mm_call_list_foreach (list, (MMCallListForeachFunc) call_list_foreach_count_establishing, &n_calls_establishing);

    /* If there is at least ONE call being established, we need the call list */
    if (n_calls_establishing > 0) {
        mm_obj_dbg (self, "%u calls being established: call list polling required", n_calls_establishing);
        ctx->polling_ongoing = TRUE;
        g_assert (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->load_call_list);
        MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->load_call_list (self,
                                                                   (GAsyncReadyCallback)load_call_list_ready,
                                                                   NULL);
    } else
        mm_obj_dbg (self, "no calls being established: call list polling stopped");

out:
    g_clear_object (&list);
    return G_SOURCE_REMOVE;
}

static void
setup_call_list_polling (MMCallList        *call_list,
                         const gchar       *call_path_added,
                         MMIfaceModemVoice *self)
{
    CallListPollingContext *ctx;

    ctx = get_call_list_polling_context (self);

    if (!ctx->polling_id && !ctx->polling_ongoing)
        ctx->polling_id = g_timeout_add_seconds (CALL_LIST_POLLING_TIMEOUT_SECS,
                                                 (GSourceFunc) call_list_poll,
                                                 self);
}

/*****************************************************************************/

static void
update_call_list (MmGdbusModemVoice *skeleton,
                  MMCallList        *list)
{
    gchar **paths;

    paths = mm_call_list_get_paths (list);
    mm_gdbus_modem_voice_set_calls (skeleton, (const gchar *const *)paths);
    g_strfreev (paths);

    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
}

static void
call_added (MMCallList        *list,
            const gchar       *call_path,
            MmGdbusModemVoice *skeleton)
{
    update_call_list (skeleton, list);
    mm_gdbus_modem_voice_emit_call_added (skeleton, call_path);
}

static void
call_deleted (MMCallList        *list,
              const gchar       *call_path,
              MmGdbusModemVoice *skeleton)
{
    update_call_list (skeleton, list);
    mm_gdbus_modem_voice_emit_call_deleted (skeleton, call_path);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (GTask *task);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    DisablingStep step;
    MmGdbusModemVoice *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_voice_disable_finish (MMIfaceModemVoice *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModemVoice *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->disable_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
cleanup_unsolicited_events_ready (MMIfaceModemVoice *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
interface_disabling_step (GTask *task)
{
    MMIfaceModemVoice *self;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->cleanup_unsolicited_events (
                self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_voice_disable (MMIfaceModemVoice *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    DisablingContext *ctx;
    GTask *task;

    ctx = g_new0 (DisablingContext, 1);
    ctx->step = DISABLING_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)disabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_disabling_step (task);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (GTask *task);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModemVoice *skeleton;
    guint mem1_storage_index;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_voice_enable_finish (MMIfaceModemVoice *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModemVoice *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
enable_unsolicited_events_ready (MMIfaceModemVoice *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    /* Not critical! */
    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error)) {
        mm_obj_dbg (self, "couldn't enable unsolicited events: %s", error->message);
        g_error_free (error);
    }

    /* Go on with next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
interface_enabling_step (GTask *task)
{
    MMIfaceModemVoice *self;
    EnablingContext *ctx;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events to get notified of incoming calls */
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events to get notified of incoming calls */
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_voice_enable (MMIfaceModemVoice *self,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    EnablingContext *ctx;
    GTask *task;

    ctx = g_new0 (EnablingContext, 1);
    ctx->step = ENABLING_STEP_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)enabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_enabling_step (task);
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_SETUP_CALL_LIST,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModemVoice *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static void
check_support_ready (MMIfaceModemVoice *self,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->check_support_finish (self, res, &error)) {
        if (error) {
            mm_obj_dbg (self, "voice support check failed: %s", error->message);
            g_error_free (error);
        }
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "Voice not supported");
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModemVoice *self;
    InitializationContext *ctx;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_CHECK_SUPPORT:
        /* Always check voice support when we run initialization, because
         * the support may be different before and after SIM-PIN unlock. */
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->check_support &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->check_support_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->check_support (
                self,
                (GAsyncReadyCallback)check_support_ready,
                task);
            return;
        }

        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "Voice not supported");
        g_object_unref (task);
        return;

    case INITIALIZATION_STEP_SETUP_CALL_LIST: {
        MMCallList *list = NULL;

        g_object_get (self,
                      MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                      NULL);

        /* Create a new call list if not already available (this initialization
         * may be called multiple times) */
        if (!list) {
            list = mm_call_list_new (MM_BASE_MODEM (self));
            g_object_set (self,
                          MM_IFACE_MODEM_VOICE_CALL_LIST, list,
                          NULL);

            /* Connect to list's signals */
            g_signal_connect (list,
                              MM_CALL_ADDED,
                              G_CALLBACK (call_added),
                              ctx->skeleton);
            g_signal_connect (list,
                              MM_CALL_DELETED,
                              G_CALLBACK (call_deleted),
                              ctx->skeleton);

            /* Setup monitoring for in-call event handling */
            g_signal_connect (list,
                              MM_CALL_ADDED,
                              G_CALLBACK (setup_in_call_event_handling),
                              self);
        }

        /* Unless we're told not to, setup call list polling logic */
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->load_call_list &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->load_call_list_finish) {
            gboolean periodic_call_list_check_disabled = FALSE;

            /* Cleanup any previously configured handler, before checking if we need to
             * add a new one, because the PERIODIC_CALL_LIST_CHECK_DISABLED flag may
             * change before and after SIM-PIN unlock */
            g_signal_handlers_disconnect_by_func (list, G_CALLBACK (setup_call_list_polling), self);

            g_object_get (self,
                          MM_IFACE_MODEM_VOICE_PERIODIC_CALL_LIST_CHECK_DISABLED, &periodic_call_list_check_disabled,
                          NULL);
            if (!periodic_call_list_check_disabled) {
                mm_obj_dbg (self, "periodic call list polling will be used if supported");
                g_signal_connect (list,
                                  MM_CALL_ADDED,
                                  G_CALLBACK (setup_call_list_polling),
                                  self);
            }
        }
        g_object_unref (list);

        ctx->step++;
    } /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* Setup all method handlers */
        g_object_connect (ctx->skeleton,
                          "signal::handle-create-call",        G_CALLBACK (handle_create),             self,
                          "signal::handle-delete-call",        G_CALLBACK (handle_delete),             self,
                          "signal::handle-list-calls",         G_CALLBACK (handle_list),               self,
                          "signal::handle-hangup-and-accept",  G_CALLBACK (handle_hangup_and_accept),  self,
                          "signal::handle-hold-and-accept",    G_CALLBACK (handle_hold_and_accept),    self,
                          "signal::handle-hangup-all",         G_CALLBACK (handle_hangup_all),         self,
                          "signal::handle-transfer",           G_CALLBACK (handle_transfer),           self,
                          "signal::handle-call-waiting-setup", G_CALLBACK (handle_call_waiting_setup), self,
                          "signal::handle-call-waiting-query", G_CALLBACK (handle_call_waiting_query), self,
                          NULL);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_voice (MM_GDBUS_OBJECT_SKELETON (self),
                                                  MM_GDBUS_MODEM_VOICE (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_voice_initialize_finish (MMIfaceModemVoice *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
modem_state_to_emergency_only (GBinding     *binding,
                               const GValue *from_value,
                               GValue       *to_value)
{
    MMModemState state;

    /* If the modem is REGISTERED, we allow any kind of call, otherwise
     * only emergency calls */
    state = g_value_get_enum (from_value);
    g_value_set_boolean (to_value, (state < MM_MODEM_STATE_REGISTERED));
    return TRUE;
}

void
mm_iface_modem_voice_initialize (MMIfaceModemVoice *self,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemVoice *skeleton = NULL;
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_VOICE_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_voice_skeleton_new ();

        g_object_set (self,
                      MM_IFACE_MODEM_VOICE_DBUS_SKELETON, skeleton,
                      NULL);

        g_object_bind_property_full (self, MM_IFACE_MODEM_STATE,
                                     skeleton, "emergency-only",
                                     G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
                                     (GBindingTransformFunc) modem_state_to_emergency_only,
                                     NULL, NULL, NULL);
    }

    /* Perform async initialization here */

    ctx = g_new0 (InitializationContext, 1);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    interface_initialization_step (task);
}

void
mm_iface_modem_voice_shutdown (MMIfaceModemVoice *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_voice (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_VOICE_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_voice_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_VOICE_DBUS_SKELETON,
                              "Voice DBus skeleton",
                              "DBus skeleton for the Voice interface",
                              MM_GDBUS_TYPE_MODEM_VOICE_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_VOICE_CALL_LIST,
                              "CALL list",
                              "List of CALL objects managed in the interface",
                              MM_TYPE_CALL_LIST,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_VOICE_PERIODIC_CALL_LIST_CHECK_DISABLED,
                               "Periodic call list checks disabled",
                               "Whether periodic call list check are disabled.",
                               FALSE,
                               G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_voice_get_type (void)
{
    static GType iface_modem_voice_type = 0;

    if (!G_UNLIKELY (iface_modem_voice_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemVoice), /* class_size */
            iface_modem_voice_init,     /* base_init */
            NULL,                           /* base_finalize */
        };

        iface_modem_voice_type = g_type_register_static (G_TYPE_INTERFACE,
                                                         "MMIfaceModemVoice",
                                                         &info,
                                                         0);

        g_type_interface_add_prerequisite (iface_modem_voice_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_voice_type;
}
