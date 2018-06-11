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
 * Copyright (C) 2015 - Marco Bascetta <marco.bascetta@sadel.it>
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-voice.h"
#include "mm-call-list.h"
#include "mm-log.h"

#define SUPPORT_CHECKED_TAG "voice-support-checked-tag"
#define SUPPORTED_TAG       "voice-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/

void
mm_iface_modem_voice_bind_simple_status (MMIfaceModemVoice *self,
                                         MMSimpleStatus *status)
{
}

/*****************************************************************************/

MMBaseCall *
mm_iface_modem_voice_create_call (MMIfaceModemVoice *self)
{
    g_assert (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->create_call != NULL);

    return MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->create_call (self);
}

MMBaseCall *
mm_iface_modem_voice_create_incoming_call (MMIfaceModemVoice *self)
{
    MMBaseCall *call = NULL;
    MMCallList *list = NULL;

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);

    if (list) {
        call = mm_call_list_get_new_incoming (list);

        if (!call) {
            mm_dbg ("Creating new incoming call...");

            call = mm_base_call_new (MM_BASE_MODEM (self));
            g_object_set (call,
                          "state",          MM_CALL_STATE_RINGING_IN,
                          "state-reason",   MM_CALL_STATE_REASON_INCOMING_NEW,
                          "direction",      MM_CALL_DIRECTION_INCOMING,
                          NULL);

            /* Only export once properly created */
            mm_base_call_export (call);
            mm_call_list_add_call (list, call);
            g_object_unref (call);
        }

        g_object_unref (list);
    }

    return call;
}

gboolean
mm_iface_modem_voice_update_incoming_call_number (MMIfaceModemVoice *self,
                                                  gchar *number,
                                                  guint type,
                                                  guint validity)
{
    gboolean    updated = FALSE;
    MMBaseCall  *call   = NULL;
    MMCallList  *list   = NULL;

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);

    if (list) {
        call = mm_call_list_get_new_incoming (list);

        if (call) {
            g_object_set (call, "number", number, NULL);
            mm_gdbus_call_set_number (MM_GDBUS_CALL (call), number);

            /*
             * TODO: Maybe also this parameters should be used:
             *  - type
             *  - validity
             */

            updated = TRUE;
        } else {
            mm_dbg ("Incoming call does not exist yet");
        }
    }

    return updated;
}

gboolean
mm_iface_modem_voice_call_dialing_to_ringing (MMIfaceModemVoice *self)
{
    gboolean    updated = FALSE;
    MMBaseCall  *call   = NULL;
    MMCallList  *list   = NULL;

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);

    if (list) {
        call = mm_call_list_get_first_outgoing_dialing_call (list);

        if (call) {
            mm_base_call_change_state (call, MM_CALL_STATE_RINGING_OUT, MM_CALL_STATE_REASON_OUTGOING_STARTED);
            updated = TRUE;
        } else {
            mm_dbg ("Outgoing dialing call does not exist");
        }
    }

    return updated;
}

gboolean
mm_iface_modem_voice_call_ringing_to_active (MMIfaceModemVoice *self)
{
    gboolean    updated = FALSE;
    MMBaseCall  *call   = NULL;
    MMCallList  *list   = NULL;

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);

    if (list) {
        call = mm_call_list_get_first_ringing_call (list);

        if (call) {
            mm_base_call_change_state (call, MM_CALL_STATE_ACTIVE, MM_CALL_STATE_REASON_ACCEPTED);
            updated = TRUE;
        } else {
            mm_dbg ("Ringing call does not exist");
        }
    }

    return updated;
}

gboolean
mm_iface_modem_voice_network_hangup (MMIfaceModemVoice *self)
{
    gboolean    updated = FALSE;
    MMBaseCall  *call   = NULL;
    MMCallList  *list   = NULL;

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);

    if (list) {
        call = mm_call_list_get_first_non_terminated_call (list);

        if (call) {
            mm_base_call_change_state (call, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_TERMINATED);
            updated = TRUE;
        } else {
            mm_dbg ("No call to hangup");
        }
    }

    return updated;
}

gboolean
mm_iface_modem_voice_received_dtmf (MMIfaceModemVoice *self,
                                    gchar *dtmf)
{
    gboolean    updated = FALSE;
    MMCallList  *list   = NULL;

    g_object_get (MM_BASE_MODEM (self),
                  MM_IFACE_MODEM_VOICE_CALL_LIST, &list,
                  NULL);

    if (list) {
        updated = mm_call_list_send_dtmf_to_active_calls (list, dtmf);
    }

    return updated;
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
handle_delete_ready (MMCallList *list,
                     GAsyncResult *res,
                     HandleDeleteContext *ctx)
{
    GError *error = NULL;

    if (!mm_call_list_delete_call_finish (list, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_voice_complete_delete_call (ctx->skeleton, ctx->invocation);

    handle_delete_context_free (ctx);
}

static void
handle_delete_auth_ready (MMBaseModem *self,
                          GAsyncResult *res,
                          HandleDeleteContext *ctx)
{
    MMModemState modem_state = MM_MODEM_STATE_UNKNOWN;
    MMCallList *list = NULL;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot delete CALL: device not yet enabled");
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
                                               "Cannot delete CALL: missing CALL list");
        handle_delete_context_free (ctx);
        return;
    }

    mm_call_list_delete_call (list,
                            ctx->path,
                            (GAsyncReadyCallback)handle_delete_ready,
                            ctx);
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
    MMModemState modem_state = MM_MODEM_STATE_UNKNOWN;
    MMCallList *list = NULL;
    GError *error = NULL;
    MMCallProperties *properties;
    MMBaseCall *call;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_context_free (ctx);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot create CALL: device not yet enabled");
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

    call = mm_base_call_new_from_properties (MM_BASE_MODEM (self),
                                             properties,
                                             &error);
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

    /* Add it to the list */
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
    MMModemState modem_state;

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot list CALL messages: "
                                               "device not yet enabled");
        return TRUE;
    }

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

static void
update_message_list (MmGdbusModemVoice *skeleton,
                     MMCallList *list)
{
    gchar **paths;

    paths = mm_call_list_get_paths (list);
    mm_gdbus_modem_voice_set_calls (skeleton, (const gchar *const *)paths);
    g_strfreev (paths);
}

static void
call_added (MMCallList *list,
           const gchar *call_path,
           MmGdbusModemVoice *skeleton)
{
    mm_dbg ("Added CALL at '%s'", call_path);
    update_message_list (skeleton, list);
    mm_gdbus_modem_voice_emit_call_added (skeleton, call_path);
}

static void
call_deleted (MMCallList *list,
             const gchar *call_path,
             MmGdbusModemVoice *skeleton)
{
    mm_dbg ("Deleted CALL at '%s'", call_path);
    update_message_list (skeleton, list);
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
        /* Fall down to next step */
        ctx->step++;

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
        /* Fall down to next step */
        ctx->step++;

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
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* Clear CALL list */
        g_object_set (self,
                      MM_IFACE_MODEM_VOICE_CALL_LIST, NULL,
                      NULL);

        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
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
        mm_dbg ("Couldn't enable unsolicited events: '%s'", error->message);
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
    case ENABLING_STEP_FIRST: {
        MMCallList *list;

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

        g_object_unref (list);

        /* Fall down to next step */
        ctx->step++;
    }

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                task);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
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
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
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

    if (!MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->check_support_finish (self,
                                                                          res,
                                                                          &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_dbg ("Voice support check failed: '%s'", error->message);
            g_error_free (error);
        }
    } else {
        /* Voice is supported! */
        g_object_set_qdata (G_OBJECT (self),
                            supported_quark,
                            GUINT_TO_POINTER (TRUE));
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
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!support_checked_quark))
            support_checked_quark = (g_quark_from_static_string (
                                         SUPPORT_CHECKED_TAG));
        if (G_UNLIKELY (!supported_quark))
            supported_quark = (g_quark_from_static_string (
                                   SUPPORTED_TAG));

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CHECK_SUPPORT:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                   support_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (self),
                                support_checked_quark,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support it */
            g_object_set_qdata (G_OBJECT (self),
                                supported_quark,
                                GUINT_TO_POINTER (FALSE));

            if (MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->check_support &&
                MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_VOICE_GET_INTERFACE (self)->check_support (
                    self,
                    (GAsyncReadyCallback)check_support_ready,
                    task);
                return;
            }

            /* If there is no implementation to check support, assume we DON'T
             * support it. */
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                   supported_quark))) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "Voice not supported");
            g_object_unref (task);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-create-call",
                          G_CALLBACK (handle_create),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-delete-call",
                          G_CALLBACK (handle_delete),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-list-calls",
                          G_CALLBACK (handle_list),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_voice (MM_GDBUS_OBJECT_SKELETON (self),
                                                  MM_GDBUS_MODEM_VOICE (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
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
