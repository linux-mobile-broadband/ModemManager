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
 * Copyright (C) 2011 Google, Inc.
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-base-sim.h"
#include "mm-bearer-list.h"
#include "mm-private-boxed-types.h"
#include "mm-log-object.h"
#include "mm-context.h"
#if defined WITH_QMI
# include "mm-broadband-modem-qmi.h"
#endif
#if defined WITH_MBIM
# include "mm-broadband-modem-mbim.h"
#endif

#define SIGNAL_QUALITY_RECENT_TIMEOUT_SEC 60

#define SIGNAL_CHECK_INITIAL_RETRIES      5
#define SIGNAL_CHECK_INITIAL_TIMEOUT_SEC  3
#define SIGNAL_CHECK_TIMEOUT_SEC          30

#define STATE_UPDATE_CONTEXT_TAG          "state-update-context-tag"
#define SIGNAL_QUALITY_UPDATE_CONTEXT_TAG "signal-quality-update-context-tag"
#define SIGNAL_CHECK_CONTEXT_TAG          "signal-check-context-tag"
#define RESTART_INITIALIZE_IDLE_TAG       "restart-initialize-tag"

static GQuark state_update_context_quark;
static GQuark signal_quality_update_context_quark;
static GQuark signal_check_context_quark;
static GQuark restart_initialize_idle_quark;

/*****************************************************************************/

gboolean
mm_iface_modem_check_for_sim_swap_finish (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
explicit_check_for_sim_swap_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish (self, res, &error)) {
        mm_obj_warn (self, "SIM swap check failed: %s", error->message);
        g_task_return_error (task, error);
    } else {
        mm_obj_dbg (self, "SIM swap check completed");
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

void
mm_iface_modem_check_for_sim_swap (MMIfaceModem *self,
                                   guint slot_index,
                                   const gchar *iccid,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /*  Slot index 0 is used when slot is not known, assumingly on a modem with just one SIM slot */
    if (slot_index != 0) {
        MmGdbusModem *skeleton;
        guint primary_slot;

        g_object_get (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                      NULL);

        if (!skeleton) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't get interface skeleton");
            g_object_unref (task);
            return;
        }

        primary_slot = mm_gdbus_modem_get_primary_sim_slot (MM_GDBUS_MODEM (skeleton));
        g_object_unref (skeleton);

        /* Check that it's really the primary slot whose iccid has changed */
        if (primary_slot && primary_slot != slot_index) {
            mm_obj_dbg (self, "checking for SIM swap ignored: status changed in slot %u, but primary is %u", slot_index, primary_slot);
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
    }

    if (MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap &&
        MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish) {
        mm_obj_dbg (self, "start checking for SIM swap in slot %u", slot_index);
        MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap (
            self,
            iccid,
            (GAsyncReadyCallback)explicit_check_for_sim_swap_ready,
            task);
        return;
    }

    g_task_return_boolean (task, FALSE);
    g_object_unref (task);
}

/*****************************************************************************/

void
mm_iface_modem_bind_simple_status (MMIfaceModem *self,
                                   MMSimpleStatus *status)
{
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    g_object_bind_property (skeleton, "state",
                            status, MM_SIMPLE_PROPERTY_STATE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "signal-quality",
                            status, MM_SIMPLE_PROPERTY_SIGNAL_QUALITY,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "current-bands",
                            status, MM_SIMPLE_PROPERTY_CURRENT_BANDS,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "access-technologies",
                            status, MM_SIMPLE_PROPERTY_ACCESS_TECHNOLOGIES,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_unref (skeleton);
}

/*****************************************************************************/
/* Helper method to wait for a final state */

#define MODEM_STATE_IS_INTERMEDIATE(state)       \
    (state == MM_MODEM_STATE_INITIALIZING  ||    \
     state == MM_MODEM_STATE_DISABLING     ||    \
     state == MM_MODEM_STATE_ENABLING      ||    \
     state == MM_MODEM_STATE_DISCONNECTING ||    \
     state == MM_MODEM_STATE_CONNECTING)

typedef struct {
    MMModemState final_state;
    gulong state_changed_id;
    guint state_changed_wait_id;
} WaitForFinalStateContext;

static void
wait_for_final_state_context_complete (GTask *task,
                                       MMModemState state,
                                       GError *error)
{
    MMIfaceModem *self;
    WaitForFinalStateContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* The callback associated with 'task' may update the modem state.
     * Disconnect the signal handler for modem state changes before completing
     * 'task' in order to prevent state_changed from being invoked, which
     * invokes wait_for_final_state_context_complete again. */
    if (ctx->state_changed_id) {
        g_signal_handler_disconnect (self, ctx->state_changed_id);
        ctx->state_changed_id = 0;
    }

    /* Remove any outstanding timeout on waiting for state change. */
    if (ctx->state_changed_wait_id) {
        g_source_remove (ctx->state_changed_wait_id);
        ctx->state_changed_wait_id = 0;
    }

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_int (task, state);

    g_object_unref (task);
}

MMModemState
mm_iface_modem_wait_for_final_state_finish (MMIfaceModem *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_STATE_UNKNOWN;
    }
    return (MMModemState)value;
}

static gboolean
state_changed_wait_expired (GTask *task)
{
    GError *error;

    error = g_error_new (MM_CORE_ERROR,
                         MM_CORE_ERROR_RETRY,
                         "Too much time waiting to get to a final state");
    wait_for_final_state_context_complete (task, MM_MODEM_STATE_UNKNOWN, error);
    return G_SOURCE_REMOVE;
}

static void
state_changed (MMIfaceModem *self,
               GParamSpec *spec,
               GTask *task)
{
    WaitForFinalStateContext *ctx;
    MMModemState state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);

    /* Are we in a final state already? */
    if (MODEM_STATE_IS_INTERMEDIATE (state))
        return;

    ctx = g_task_get_task_data (task);

    /* If we want a specific final state and this is not the one we were
     * looking for, then skip */
    if (ctx->final_state != MM_MODEM_STATE_UNKNOWN &&
        state != MM_MODEM_STATE_UNKNOWN &&
        state != ctx->final_state)
        return;

    /* Done! */
    wait_for_final_state_context_complete (task, state, NULL);
}

void
mm_iface_modem_wait_for_final_state (MMIfaceModem *self,
                                     MMModemState final_state,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMModemState state = MM_MODEM_STATE_UNKNOWN;
    WaitForFinalStateContext *ctx;
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);

    /* Are we in a final state already? */
    if (!MODEM_STATE_IS_INTERMEDIATE (state)) {
        /* Is this the state we actually wanted? */
        if (final_state == MM_MODEM_STATE_UNKNOWN ||
            (state != MM_MODEM_STATE_UNKNOWN && state == final_state)) {
            g_task_return_int (task, state);
            g_object_unref (task);
            return;
        }

        /* Otherwise, we'll need to wait for the exact one we want */
    }

    ctx = g_new0 (WaitForFinalStateContext, 1);
    ctx->final_state = final_state;

    g_task_set_task_data (task, ctx, g_free);

    /* Want to get notified when modem state changes */
    ctx->state_changed_id = g_signal_connect (self,
                                              "notify::" MM_IFACE_MODEM_STATE,
                                              G_CALLBACK (state_changed),
                                              task);
    /* But we don't want to wait forever */
    ctx->state_changed_wait_id = g_timeout_add_seconds (10,
                                                        (GSourceFunc)state_changed_wait_expired,
                                                        task);
}

/*****************************************************************************/
/* Helper to return an error when the modem is in failed state and so it
 * cannot process a given method invocation
 */

static gboolean
abort_invocation_if_state_not_reached (MMIfaceModem          *self,
                                       GDBusMethodInvocation *invocation,
                                       MMModemState           minimum_required)
{
    MMModemState state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);

    if (state >= minimum_required)
        return FALSE;

    g_dbus_method_invocation_return_error (invocation,
                                           MM_CORE_ERROR,
                                           MM_CORE_ERROR_WRONG_STATE,
                                           "modem in %s state",
                                           mm_modem_state_get_string (state));
    return TRUE;
}

/*****************************************************************************/
/* Helper method to load unlock required, considering retries */

#define MAX_RETRIES 6

typedef struct {
    guint retries;
    guint pin_check_timeout_id;
} InternalLoadUnlockRequiredContext;

static MMModemLock
internal_load_unlock_required_finish (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCK_UNKNOWN;
    }
    return (MMModemLock)value;
}

static void internal_load_unlock_required_context_step (GTask *task);

static gboolean
load_unlock_required_again (GTask *task)
{
    InternalLoadUnlockRequiredContext *ctx;

    ctx = g_task_get_task_data (task);
    ctx->pin_check_timeout_id = 0;
    /* Retry the step */
    internal_load_unlock_required_context_step (task);
    return G_SOURCE_REMOVE;
}

static void
load_unlock_required_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask *task)
{
    InternalLoadUnlockRequiredContext *ctx;
    GError *error = NULL;
    MMModemLock lock;

    ctx = g_task_get_task_data (task);

    lock = MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_required_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "couldn't check if unlock required: %s", error->message);

        /* For several kinds of errors, just return them directly */
        if (error->domain == MM_SERIAL_ERROR ||
            g_error_matches (error,
                             G_IO_ERROR,
                             G_IO_ERROR_CANCELLED) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        /* For the remaining ones, retry if possible */
        if (ctx->retries < MAX_RETRIES) {
            ctx->retries++;
            mm_obj_dbg (self, "retrying (%u) unlock required check", ctx->retries);

            g_assert (ctx->pin_check_timeout_id == 0);
            ctx->pin_check_timeout_id = g_timeout_add_seconds (2,
                                                               (GSourceFunc)load_unlock_required_again,
                                                               task);
            g_error_free (error);
            return;
        }

        /* If reached max retries and still reporting error... default to SIM error */
        g_error_free (error);
        g_task_return_new_error (task,
                                 MM_MOBILE_EQUIPMENT_ERROR,
                                 MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE,
                                 "Couldn't get SIM lock status after %u retries",
                                 MAX_RETRIES);
        g_object_unref (task);
        return;
    }

    /* Got the lock value, return it */
    g_task_return_int (task, lock);
    g_object_unref (task);
}

static void
internal_load_unlock_required_context_step (GTask *task)
{
    MMIfaceModem *self;
    InternalLoadUnlockRequiredContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    g_assert (ctx->pin_check_timeout_id == 0);
    MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_required (
        self,
        (ctx->retries == MAX_RETRIES), /* last_attempt? */
        (GAsyncReadyCallback)load_unlock_required_ready,
        task);
}

static void
internal_load_unlock_required (MMIfaceModem *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    InternalLoadUnlockRequiredContext *ctx;
    GTask *task;

    ctx = g_new0 (InternalLoadUnlockRequiredContext, 1);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_required ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_required_finish) {
        /* Just assume that no lock is required */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    internal_load_unlock_required_context_step (task);
}

/*****************************************************************************/

static void
bearer_list_updated (MMBearerList *bearer_list,
                     GParamSpec *pspec,
                     MMIfaceModem *self)
{
    MmGdbusModem *skeleton;
    gchar **paths;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    paths = mm_bearer_list_get_paths (bearer_list);
    mm_gdbus_modem_set_bearers (skeleton, (const gchar *const *)paths);
    g_strfreev (paths);

    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
    g_object_unref (skeleton);
}

/*****************************************************************************/

static MMModemState get_consolidated_subsystem_state (MMIfaceModem *self);

typedef struct {
    MMBaseBearer *self;
    guint others_connected;
} CountOthersConnectedContext;

static void
bearer_list_count_others_connected (MMBaseBearer *bearer,
                                    CountOthersConnectedContext *ctx)
{
    /* We can safely compare pointers here */
    if (bearer != ctx->self &&
        mm_base_bearer_get_status (bearer) == MM_BEARER_STATUS_CONNECTED) {
        ctx->others_connected++;
    }
}

static void
bearer_status_changed (MMBaseBearer *bearer,
                       GParamSpec *pspec,
                       MMIfaceModem *self)
{
    CountOthersConnectedContext ctx;
    MMBearerList *list = NULL;
    MMModemState state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);
    if (!list)
        return;

    if (state == MM_MODEM_STATE_DISABLING ||
        state == MM_MODEM_STATE_ENABLING) {
        /* Don't log modem bearer-specific status changes if we're disabling
         * or enabling */
        g_object_unref (list);
        return;
    }

    ctx.self = bearer;
    ctx.others_connected = 0;

    /* We now count how many *other* bearers are connected */
    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)bearer_list_count_others_connected,
                            &ctx);

    /* If no other bearers are connected, change modem state */
    if (!ctx.others_connected) {
        MMModemState new_state = MM_MODEM_STATE_UNKNOWN;

        switch (mm_base_bearer_get_status (bearer)) {
        case MM_BEARER_STATUS_CONNECTED:
            new_state = MM_MODEM_STATE_CONNECTED;
            break;
        case MM_BEARER_STATUS_CONNECTING:
            new_state = MM_MODEM_STATE_CONNECTING;
            break;
        case MM_BEARER_STATUS_DISCONNECTING:
            new_state = MM_MODEM_STATE_DISCONNECTING;
            break;
        case MM_BEARER_STATUS_DISCONNECTED:
            new_state = get_consolidated_subsystem_state (self);
            break;
        default:
            g_assert_not_reached ();
        }

        mm_iface_modem_update_state (self,
                                     new_state,
                                     MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);
    }

    g_object_unref (list);
}

typedef struct {
    MMBearerList *list;
} CreateBearerContext;

static void
create_bearer_context_free (CreateBearerContext *ctx)
{
    if (ctx->list)
        g_object_unref (ctx->list);
    g_slice_free (CreateBearerContext, ctx);
}

MMBaseBearer *
mm_iface_modem_create_bearer_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
create_bearer_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     GTask *task)
{
    CreateBearerContext *ctx;
    MMBaseBearer *bearer;
    GError *error = NULL;

    bearer = MM_IFACE_MODEM_GET_INTERFACE (self)->create_bearer_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    if (!mm_bearer_list_add_bearer (ctx->list, bearer, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        g_object_unref (bearer);
        return;
    }

    /* If bearer properly created and added to the list, follow its
     * status */
    g_signal_connect (bearer,
                      "notify::"  MM_BASE_BEARER_STATUS,
                      (GCallback)bearer_status_changed,
                      self);
    g_task_return_pointer (task, bearer, g_object_unref);
    g_object_unref (task);
}

void
mm_iface_modem_create_bearer (MMIfaceModem *self,
                              MMBearerProperties *properties,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    CreateBearerContext *ctx;
    GTask *task;

    ctx = g_slice_new (CreateBearerContext);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)create_bearer_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &ctx->list,
                  NULL);
    if (!ctx->list) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Cannot add new bearer: bearer list not found");
        g_object_unref (task);
        return;
    }

    if (mm_bearer_list_get_count (ctx->list) == mm_bearer_list_get_max (ctx->list)) {
        g_task_return_new_error (
            task,
            MM_CORE_ERROR,
            MM_CORE_ERROR_TOO_MANY,
            "Cannot add new bearer: already reached maximum (%u)",
            mm_bearer_list_get_count (ctx->list));
        g_object_unref (task);
        return;
    }

    MM_IFACE_MODEM_GET_INTERFACE (self)->create_bearer (
        self,
        properties,
        (GAsyncReadyCallback)create_bearer_ready,
        task);
}

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    GVariant *dictionary;
} HandleCreateBearerContext;

static void
handle_create_bearer_context_free (HandleCreateBearerContext *ctx)
{
    g_variant_unref (ctx->dictionary);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_create_bearer_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            HandleCreateBearerContext *ctx)
{
    MMBaseBearer *bearer;
    GError *error = NULL;

    bearer = mm_iface_modem_create_bearer_finish (self, res, &error);
    if (!bearer)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem_complete_create_bearer (ctx->skeleton,
                                               ctx->invocation,
                                               mm_base_bearer_get_path (bearer));
        g_object_unref (bearer);
    }

    handle_create_bearer_context_free (ctx);
}

static void
handle_create_bearer_auth_ready (MMBaseModem *self,
                                 GAsyncResult *res,
                                 HandleCreateBearerContext *ctx)
{
    MMBearerProperties *properties;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_bearer_context_free (ctx);
        return;
    }

    if (abort_invocation_if_state_not_reached (ctx->self, ctx->invocation, MM_MODEM_STATE_LOCKED)) {
        handle_create_bearer_context_free (ctx);
        return;
    }

    properties = mm_bearer_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!properties) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_create_bearer_context_free (ctx);
        return;
    }

    mm_iface_modem_create_bearer (
        ctx->self,
        properties,
        (GAsyncReadyCallback)handle_create_bearer_ready,
        ctx);
    g_object_unref (properties);
}

static gboolean
handle_create_bearer (MmGdbusModem *skeleton,
                      GDBusMethodInvocation *invocation,
                      GVariant *dictionary,
                      MMIfaceModem *self)
{
    HandleCreateBearerContext *ctx;

    ctx = g_new (HandleCreateBearerContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_create_bearer_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    gchar *cmd;
    guint timeout;
} HandleCommandContext;

static void
handle_command_context_free (HandleCommandContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->cmd);
    g_free (ctx);
}

static void
command_ready (MMIfaceModem *self,
               GAsyncResult *res,
               HandleCommandContext *ctx)
{
    GError *error = NULL;
    const gchar *result;

    result = MM_IFACE_MODEM_GET_INTERFACE (self)->command_finish (self,
                                                                  res,
                                                                  &error);
    if (error)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_command (ctx->skeleton, ctx->invocation, result);

    handle_command_context_free (ctx);
}

static void
handle_command_auth_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           HandleCommandContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_command_context_free (ctx);
        return;
    }

#if ! defined WITH_AT_COMMAND_VIA_DBUS
    /* If we are not in Debug mode, report an error */
    if (!mm_context_get_debug ()) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNAUTHORIZED,
                                               "Cannot send AT command to modem: "
                                               "operation only allowed in debug mode");
        handle_command_context_free (ctx);
        return;
    }
#endif

    /* If command is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->command ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->command_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot send AT command to modem: "
                                               "operation not supported");
        handle_command_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_GET_INTERFACE (self)->command (ctx->self,
                                                  ctx->cmd,
                                                  ctx->timeout,
                                                  (GAsyncReadyCallback)command_ready,
                                                  ctx);
}

static gboolean
handle_command (MmGdbusModem *skeleton,
                GDBusMethodInvocation *invocation,
                const gchar *cmd,
                guint timeout,
                MMIfaceModem *self)
{
    HandleCommandContext *ctx;

    ctx = g_new (HandleCommandContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->cmd = g_strdup (cmd);
    ctx->timeout = timeout;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_command_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    MMBearerList *list;
    gchar *bearer_path;
    MMBaseBearer *bearer;
} HandleDeleteBearerContext;

static void
handle_delete_bearer_context_free (HandleDeleteBearerContext *ctx)
{
    if (ctx->bearer)
        g_object_unref (ctx->bearer);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    if (ctx->list)
        g_object_unref (ctx->list);
    g_free (ctx->bearer_path);
    g_free (ctx);
}

static void
delete_bearer_disconnect_ready (MMBaseBearer *bearer,
                                GAsyncResult *res,
                                HandleDeleteBearerContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_bearer_disconnect_finish (bearer, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_bearer_context_free (ctx);
        return;
    }

    if (!mm_bearer_list_delete_bearer (ctx->list, ctx->bearer_path, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_delete_bearer (ctx->skeleton, ctx->invocation);
    handle_delete_bearer_context_free (ctx);
}

static void
handle_delete_bearer_auth_ready (MMBaseModem *self,
                                 GAsyncResult *res,
                                 HandleDeleteBearerContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_delete_bearer_context_free (ctx);
        return;
    }

    if (abort_invocation_if_state_not_reached (ctx->self, ctx->invocation, MM_MODEM_STATE_LOCKED)) {
        handle_delete_bearer_context_free (ctx);
        return;
    }

    if (!g_str_has_prefix (ctx->bearer_path, MM_DBUS_BEARER_PREFIX)) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_INVALID_ARGS,
                                               "Cannot delete bearer: invalid path '%s'",
                                               ctx->bearer_path);
        handle_delete_bearer_context_free (ctx);
        return;
    }

    ctx->bearer = mm_bearer_list_find_by_path (ctx->list, ctx->bearer_path);
    if (!ctx->bearer) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_INVALID_ARGS,
                                               "Cannot delete bearer: no bearer found with path '%s'",
                                               ctx->bearer_path);
        handle_delete_bearer_context_free (ctx);
        return;
    }

    mm_base_bearer_disconnect (ctx->bearer,
                               (GAsyncReadyCallback)delete_bearer_disconnect_ready,
                               ctx);
}

static gboolean
handle_delete_bearer (MmGdbusModem *skeleton,
                      GDBusMethodInvocation *invocation,
                      const gchar *bearer,
                      MMIfaceModem *self)
{
    HandleDeleteBearerContext *ctx;

    ctx = g_new (HandleDeleteBearerContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->bearer_path = g_strdup (bearer);
    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &ctx->list,
                  NULL);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_delete_bearer_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static gboolean
handle_list_bearers (MmGdbusModem *skeleton,
                     GDBusMethodInvocation *invocation,
                     MMIfaceModem *self)
{
    GStrv paths;
    MMBearerList *list = NULL;

    if (abort_invocation_if_state_not_reached (self, invocation, MM_MODEM_STATE_LOCKED))
        return TRUE;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);
    if (!list) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_FAILED,
                                               "Bearer list not found");
        return TRUE;
    }

    paths = mm_bearer_list_get_paths (list);
    mm_gdbus_modem_complete_list_bearers (skeleton,
                                          invocation,
                                          (const gchar *const *)paths);

    g_strfreev (paths);
    g_object_unref (list);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem          *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem          *self;
    guint                  requested_sim_slot;
} HandleSetPrimarySimSlotContext;

static void
handle_set_primary_sim_slot_context_free (HandleSetPrimarySimSlotContext *ctx)
{
    g_clear_object (&ctx->skeleton);
    g_clear_object (&ctx->invocation);
    g_clear_object (&ctx->self);
    g_free (ctx);
}

static void
set_primary_sim_slot_ready (MMIfaceModem                   *self,
                            GAsyncResult                   *res,
                            HandleSetPrimarySimSlotContext *ctx)
{
    g_autoptr(GError) error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_primary_sim_slot_finish (self, res, &error)) {
        /* If the implementation returns EXISTS, we're already in the requested SIM slot,
         * so we can safely return a success on the operation and skip the reprobing */
        if (!g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_EXISTS)) {
            mm_obj_warn (self, "couldn't process primary SIM update request: %s", error->message);
            g_dbus_method_invocation_take_error (ctx->invocation, g_steal_pointer (&error));
            handle_set_primary_sim_slot_context_free (ctx);
            return;
        }
        mm_obj_dbg (self, "ignoring SIM update request: %s", error->message);
    } else {
        /* Notify about the SIM swap, which will disable and reprobe the device.
         * There is no need to update the PrimarySimSlot property, as this value will be
         * reloaded automatically during the reprobe. */
        mm_base_modem_process_sim_event (MM_BASE_MODEM (self));
    }

    mm_gdbus_modem_complete_set_primary_sim_slot (ctx->skeleton, ctx->invocation);
    handle_set_primary_sim_slot_context_free (ctx);
}

static void
handle_set_primary_sim_slot_auth_ready (MMBaseModem                    *self,
                                        GAsyncResult                   *res,
                                        HandleSetPrimarySimSlotContext *ctx)
{
    GError             *error = NULL;
    const gchar *const *sim_slot_paths;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_primary_sim_slot_context_free (ctx);
        return;
    }

    /* If SIM switching is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_primary_sim_slot ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_primary_sim_slot_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot switch sim: "
                                               "operation not supported");
        handle_set_primary_sim_slot_context_free (ctx);
        return;
    }

    /* Validate SIM slot number */
    sim_slot_paths = mm_gdbus_modem_get_sim_slots (ctx->skeleton);
    if (ctx->requested_sim_slot > g_strv_length ((gchar **)sim_slot_paths)) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_INVALID_ARGS,
                                               "Cannot switch sim: requested SIM slot number is out of bounds");
        handle_set_primary_sim_slot_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_GET_INTERFACE (self)->set_primary_sim_slot (MM_IFACE_MODEM (self),
                                                               ctx->requested_sim_slot,
                                                               (GAsyncReadyCallback)set_primary_sim_slot_ready,
                                                               ctx);
}

static gboolean
handle_set_primary_sim_slot (MmGdbusModem          *skeleton,
                             GDBusMethodInvocation *invocation,
                             guint                  sim_slot,
                             MMIfaceModem          *self)
{
    HandleSetPrimarySimSlotContext *ctx;

    ctx = g_new0 (HandleSetPrimarySimSlotContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->requested_sim_slot = sim_slot;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_primary_sim_slot_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

void
mm_iface_modem_update_access_technologies (MMIfaceModem *self,
                                           MMModemAccessTechnology new_access_tech,
                                           guint32 mask)
{
    MmGdbusModem *skeleton = NULL;
    MMModemAccessTechnology old_access_tech;
    MMModemAccessTechnology built_access_tech;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    /* Don't process updates if the interface is shut down */
    if (!skeleton)
        return;

    old_access_tech = mm_gdbus_modem_get_access_technologies (skeleton);

    /* Build the new access tech */
    built_access_tech = old_access_tech;
    built_access_tech &= ~mask;
    built_access_tech |= new_access_tech;

    if (built_access_tech != old_access_tech) {
        gchar *old_access_tech_string;
        gchar *new_access_tech_string;

        mm_gdbus_modem_set_access_technologies (skeleton, built_access_tech);

        /* Log */
        old_access_tech_string = mm_modem_access_technology_build_string_from_mask (old_access_tech);
        new_access_tech_string = mm_modem_access_technology_build_string_from_mask (built_access_tech);
        mm_obj_dbg (self, "access technology changed (%s -> %s)",
                    old_access_tech_string,
                    new_access_tech_string);
        g_free (old_access_tech_string);
        g_free (new_access_tech_string);
    }

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    guint recent_timeout_source;
} SignalQualityUpdateContext;

static void
signal_quality_update_context_free (SignalQualityUpdateContext *ctx)
{
    if (ctx->recent_timeout_source)
        g_source_remove (ctx->recent_timeout_source);
    g_free (ctx);
}

static gboolean
expire_signal_quality (MMIfaceModem *self)
{
    MmGdbusModem *skeleton = NULL;
    SignalQualityUpdateContext *ctx;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    if (skeleton) {
        GVariant *old;
        guint signal_quality = 0;
        gboolean recent = FALSE;

        old = mm_gdbus_modem_get_signal_quality (skeleton);
        g_variant_get (old,
                       "(ub)",
                       &signal_quality,
                       &recent);

        /* If value is already not recent, we're done */
        if (recent) {
            mm_obj_dbg (self, "signal quality value not updated in %us, marking as not being recent",
                        SIGNAL_QUALITY_RECENT_TIMEOUT_SEC);
            mm_gdbus_modem_set_signal_quality (skeleton,
                                               g_variant_new ("(ub)",
                                                              signal_quality,
                                                              FALSE));
        }

        g_object_unref (skeleton);
    }

    /* Remove source id */
    ctx = g_object_get_qdata (G_OBJECT (self), signal_quality_update_context_quark);
    ctx->recent_timeout_source = 0;
    return G_SOURCE_REMOVE;
}

static void
update_signal_quality (MMIfaceModem *self,
                       guint signal_quality,
                       gboolean expire)
{
    SignalQualityUpdateContext *ctx;
    MmGdbusModem *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    /* Don't process updates if the interface is shut down */
    if (!skeleton)
        return;

    if (G_UNLIKELY (!signal_quality_update_context_quark))
        signal_quality_update_context_quark = (g_quark_from_static_string (
                                                   SIGNAL_QUALITY_UPDATE_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), signal_quality_update_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_new0 (SignalQualityUpdateContext, 1);
        g_object_set_qdata_full (
            G_OBJECT (self),
            signal_quality_update_context_quark,
            ctx,
            (GDestroyNotify)signal_quality_update_context_free);
    }

    /* Note: we always set the new value, even if the signal quality level
     * is the same, in order to provide an up to date 'recent' flag.
     * The only exception being if 'expire' is FALSE; in that case we assume
     * the value won't expire and therefore can be considered obsolete
     * already. */
    mm_gdbus_modem_set_signal_quality (skeleton,
                                       g_variant_new ("(ub)",
                                                      signal_quality,
                                                      expire));

    mm_obj_dbg (self, "signal quality updated (%u)", signal_quality);

    /* Remove any previous expiration refresh timeout */
    if (ctx->recent_timeout_source) {
        g_source_remove (ctx->recent_timeout_source);
        ctx->recent_timeout_source = 0;
    }

    /* If we got a new expirable value, setup new timeout */
    if (expire)
        ctx->recent_timeout_source = (g_timeout_add_seconds (
                                          SIGNAL_QUALITY_RECENT_TIMEOUT_SEC,
                                          (GSourceFunc)expire_signal_quality,
                                          self));

    g_object_unref (skeleton);
}

void
mm_iface_modem_update_signal_quality (MMIfaceModem *self,
                                      guint signal_quality)
{
    update_signal_quality (self, signal_quality, TRUE);
}

/*****************************************************************************/
/* Signal info (quality and access technology) polling */

typedef enum {
    SIGNAL_CHECK_STEP_NONE,
    SIGNAL_CHECK_STEP_FIRST,
    SIGNAL_CHECK_STEP_SIGNAL_QUALITY,
    SIGNAL_CHECK_STEP_ACCESS_TECHNOLOGIES,
    SIGNAL_CHECK_STEP_LAST,
} SignalCheckStep;

typedef struct {
    gboolean enabled;
    guint    timeout_source;

    /* We first attempt an initial loading, and once it's done we
     * setup polling */
    guint    initial_retries;
    gboolean initial_check_done;

    /* Values polled in this iteration */
    guint                   signal_quality;
    MMModemAccessTechnology access_technologies;
    guint                   access_technologies_mask;

    /* If both signal and access tech polling are either unsupported
     * or disabled, we'll automatically stop polling */
    gboolean signal_quality_polling_supported;
    gboolean signal_quality_polling_disabled;
    gboolean access_technology_polling_supported;
    gboolean access_technology_polling_disabled;

    /* Steps triggered when polling active */
    SignalCheckStep running_step;
} SignalCheckContext;

static void
signal_check_context_free (SignalCheckContext *ctx)
{
    if (ctx->timeout_source)
        g_source_remove (ctx->timeout_source);
    g_slice_free (SignalCheckContext, ctx);
}

static SignalCheckContext *
get_signal_check_context (MMIfaceModem *self)
{
    SignalCheckContext *ctx;

    if (G_UNLIKELY (!signal_check_context_quark))
        signal_check_context_quark = (g_quark_from_static_string (
                                          SIGNAL_CHECK_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), signal_check_context_quark);
    if (!ctx) {
        /* Create context and attach it to the object */
        ctx = g_slice_new0 (SignalCheckContext);
        ctx->running_step = SIGNAL_CHECK_STEP_NONE;

        /* Initially assume supported if load_access_technologies() is
         * implemented. If the plugin reports an UNSUPPORTED error we'll clear
         * this flag and no longer poll. */
        ctx->access_technology_polling_supported = (MM_IFACE_MODEM_GET_INTERFACE (self)->load_access_technologies &&
                                                    MM_IFACE_MODEM_GET_INTERFACE (self)->load_access_technologies_finish);

        /* Initially assume supported if load_signal_quality() is
         * implemented. If the plugin reports an UNSUPPORTED error we'll clear
         * this flag and no longer poll. */
        ctx->signal_quality_polling_supported = (MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality &&
                                                 MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality_finish);

        /* Get plugin-specific setup for the polling logic */
        g_object_get (self,
                      MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED,      &ctx->signal_quality_polling_disabled,
                      MM_IFACE_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED, &ctx->access_technology_polling_disabled,
                      NULL);

        g_object_set_qdata_full (G_OBJECT (self), signal_check_context_quark,
                                 ctx, (GDestroyNotify) signal_check_context_free);
    }

    g_assert (ctx);
    return ctx;
}

static void     periodic_signal_check_disable (MMIfaceModem *self,
                                               gboolean      clear);
static gboolean periodic_signal_check_cb      (MMIfaceModem *self);
static void     peridic_signal_check_step     (MMIfaceModem *self);

static void
access_technologies_check_ready (MMIfaceModem *self,
                                 GAsyncResult *res)
{
    GError             *error = NULL;
    SignalCheckContext *ctx;

    ctx = get_signal_check_context (self);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_access_technologies_finish (
            self,
            res,
            &ctx->access_technologies,
            &ctx->access_technologies_mask,
            &error)) {
        /* Did the plugin report that polling access technology is unsupported? */
        if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED)) {
            mm_obj_dbg (self, "polling to refresh access technologies is unsupported");
            ctx->access_technology_polling_supported = FALSE;
        }
        /* Ignore logging any message if the error is in 'in-progress' */
        else if (!g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS))
            mm_obj_dbg (self, "couldn't refresh access technologies: %s", error->message);
        g_error_free (error);
    }
    /* We may have been disabled while this command was running. */
    else if (ctx->enabled)
        mm_iface_modem_update_access_technologies (self, ctx->access_technologies, ctx->access_technologies_mask);

    /* Go on */
    ctx->running_step++;
    peridic_signal_check_step (self);
}

static void
signal_quality_check_ready (MMIfaceModem *self,
                            GAsyncResult *res)
{
    GError             *error = NULL;
    SignalCheckContext *ctx;

    ctx = get_signal_check_context (self);

    ctx->signal_quality = MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality_finish (self, res, &error);
    if (error) {
        /* Did the plugin report that polling signal quality is unsupported? */
        if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED)) {
            mm_obj_dbg (self, "polling to refresh signal quality is unsupported");
            ctx->signal_quality_polling_supported = FALSE;
        }
        /* Ignore logging any message if the error is in 'in-progress' */
        else if (!g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS))
            mm_obj_dbg (self, "couldn't refresh signal quality: %s", error->message);
        g_error_free (error);
    }
    /* We may have been disabled while this command was running. */
    else if (ctx->enabled)
        update_signal_quality (self, ctx->signal_quality, TRUE);

    /* Go on */
    ctx->running_step++;
    peridic_signal_check_step (self);
}

static void
peridic_signal_check_step (MMIfaceModem *self)
{
    SignalCheckContext *ctx;

    ctx = get_signal_check_context (self);

    switch (ctx->running_step) {
    case SIGNAL_CHECK_STEP_NONE:
        g_assert_not_reached ();

    case SIGNAL_CHECK_STEP_FIRST:
        ctx->running_step++;
        /* fall-through */

    case SIGNAL_CHECK_STEP_SIGNAL_QUALITY:
        if (ctx->enabled && ctx->signal_quality_polling_supported &&
            (!ctx->initial_check_done || !ctx->signal_quality_polling_disabled)) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality (
                self, (GAsyncReadyCallback)signal_quality_check_ready, NULL);
            return;
        }
        ctx->running_step++;
        /* fall-through */

    case SIGNAL_CHECK_STEP_ACCESS_TECHNOLOGIES:
        if (ctx->enabled && ctx->access_technology_polling_supported &&
            (!ctx->initial_check_done || !ctx->access_technology_polling_disabled)) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_access_technologies (
                self, (GAsyncReadyCallback)access_technologies_check_ready, NULL);
            return;
        }
        ctx->running_step++;
        /* fall-through */

    case SIGNAL_CHECK_STEP_LAST:
        /* Flag as sequence finished */
        ctx->running_step = SIGNAL_CHECK_STEP_NONE;

        /* If we have been disabled while we were running the steps, we don't
         * do anything else. */
        if (!ctx->enabled) {
            mm_obj_dbg (self, "periodic signal quality and access technology checks not rescheduled: disabled");
            return;
        }

        /* Schedule when we poll next time.
         * Initially we poll at a higher frequency until we get valid signal
         * quality and access technology values. As soon as we get them, OR if
         * we made too many retries at a high frequency, we fallback to the
         * slower polling. */
        if (!ctx->initial_check_done) {
            gboolean signal_quality_ready;
            gboolean access_technology_ready;

            /* Signal quality is ready if unsupported or if we got a valid
             * value reported */
            signal_quality_ready = (!ctx->signal_quality_polling_supported || (ctx->signal_quality != 0));

            /* Access technology is ready if unsupported or if we got a valid
             * value reported */
            access_technology_ready = (!ctx->access_technology_polling_supported ||
                                       ((ctx->access_technologies & ctx->access_technologies_mask) != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN));

            ctx->initial_check_done = ((signal_quality_ready && access_technology_ready) || (--ctx->initial_retries == 0));
        }

        /* After running the initial check, if both signal quality and access tech
         * loading are either disabled or unsupported, we'll stop polling completely,
         * because they may be loaded asynchronously by unsolicited messages */
        if (ctx->initial_check_done &&
            (!ctx->signal_quality_polling_supported    || ctx->signal_quality_polling_disabled) &&
            (!ctx->access_technology_polling_supported || ctx->access_technology_polling_disabled)) {
            mm_obj_dbg (self, "periodic signal quality and access technology checks not rescheduled: unneeded or unsupported");
            periodic_signal_check_disable (self, FALSE);
            return;
        }

        mm_obj_dbg (self, "periodic signal quality and access technology checks scheduled");
        g_assert (!ctx->timeout_source);
        ctx->timeout_source = g_timeout_add_seconds (ctx->initial_check_done ? SIGNAL_CHECK_TIMEOUT_SEC : SIGNAL_CHECK_INITIAL_TIMEOUT_SEC,
                                                     (GSourceFunc) periodic_signal_check_cb,
                                                     self);
        return;

    default:
        g_assert_not_reached ();
    }
}

static gboolean
periodic_signal_check_cb (MMIfaceModem *self)
{
    SignalCheckContext *ctx;

    ctx = get_signal_check_context (self);
    g_assert (ctx->enabled);

    /* Start the sequence */
    ctx->running_step             = SIGNAL_CHECK_STEP_FIRST;
    ctx->signal_quality           = 0;
    ctx->access_technologies      = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    ctx->access_technologies_mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    peridic_signal_check_step (self);

    /* Remove the timeout and clear the source id */
    if (ctx->timeout_source)
        ctx->timeout_source = 0;
    return G_SOURCE_REMOVE;
}

void
mm_iface_modem_refresh_signal (MMIfaceModem *self)
{
    SignalCheckContext *ctx;

    /* Don't refresh polling if we're not enabled */
    ctx = get_signal_check_context (self);
    if (!ctx->enabled) {
        mm_obj_dbg (self, "periodic signal check refresh ignored: checks not enabled");
        return;
    }

    /* Don't refresh if we're already doing it */
    if (ctx->running_step != SIGNAL_CHECK_STEP_NONE) {
        mm_obj_dbg (self, "periodic signal check refresh ignored: check already running");
        return;
    }

    mm_obj_dbg (self, "periodic signal check refresh requested");

    /* Remove the scheduled timeout as we're going to refresh
     * right away */
    if (ctx->timeout_source) {
        g_source_remove (ctx->timeout_source);
        ctx->timeout_source = 0;
    }

    /* Reset refresh rate and initial retries when we're asked to refresh signal
     * so that we poll at a higher frequency */
    ctx->initial_retries    = SIGNAL_CHECK_INITIAL_RETRIES;
    ctx->initial_check_done = FALSE;

    /* Start sequence */
    periodic_signal_check_cb (self);
}

static void
periodic_signal_check_disable (MMIfaceModem *self,
                               gboolean      clear)
{
    SignalCheckContext *ctx;

    ctx = get_signal_check_context (self);
    if (!ctx->enabled)
        return;

    /* Clear access technology and signal quality */
    if (clear) {
        update_signal_quality (self, 0, FALSE);
        mm_iface_modem_update_access_technologies (self,
                                                   MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,
                                                   MM_MODEM_ACCESS_TECHNOLOGY_ANY);
    }

    /* Remove scheduled timeout */
    if (ctx->timeout_source) {
        g_source_remove (ctx->timeout_source);
        ctx->timeout_source = 0;
    }

    ctx->enabled = FALSE;
    mm_obj_dbg (self, "periodic signal checks disabled");
}

static void
periodic_signal_check_enable (MMIfaceModem *self)
{
    SignalCheckContext *ctx;

    ctx = get_signal_check_context (self);

    /* If polling access technology and signal quality not supported, don't even
     * bother trying. */
    if (!ctx->signal_quality_polling_supported && !ctx->access_technology_polling_supported) {
        mm_obj_dbg (self, "not enabling periodic signal checks: unsupported");
        return;
    }

    /* Log and flag as enabled */
    if (!ctx->enabled) {
        mm_obj_dbg (self, "periodic signal checks enabled");
        ctx->enabled = TRUE;
    }

    /* And refresh, which will trigger the first check at high frequency */
    mm_iface_modem_refresh_signal (self);
}

/*****************************************************************************/

static void
bearer_list_count_connected (MMBaseBearer *bearer,
                             guint *count)
{
    if (mm_base_bearer_get_status (bearer) == MM_BEARER_STATUS_CONNECTED)
        (*count)++;
}

static void
__iface_modem_update_state_internal (MMIfaceModem *self,
                                     MMModemState new_state,
                                     MMModemStateChangeReason reason,
                                     MMModemStateFailedReason failed_reason)
{
    MMModemState old_state = MM_MODEM_STATE_UNKNOWN;
    MmGdbusModem *skeleton = NULL;
    MMBearerList *bearer_list = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &old_state,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);

    if (!skeleton || !bearer_list) {
        if (skeleton)
            g_object_unref (skeleton);
        if (bearer_list)
            g_object_unref (bearer_list);
        return;
    }

    /* While connected we don't want registration status changes to change
     * the modem's state away from CONNECTED. */
    if ((new_state == MM_MODEM_STATE_ENABLED   ||
         new_state == MM_MODEM_STATE_SEARCHING ||
         new_state == MM_MODEM_STATE_REGISTERED) &&
        bearer_list &&
        old_state > MM_MODEM_STATE_REGISTERED) {
        guint connected = 0;

        mm_bearer_list_foreach (bearer_list,
                                (MMBearerListForeachFunc)bearer_list_count_connected,
                                &connected);
        if (connected > 0)
            /* Don't update state */
            new_state = old_state;
    }

    /* Enabled may really be searching or registered */
    if (new_state == MM_MODEM_STATE_ENABLED)
        new_state = get_consolidated_subsystem_state (self);

    /* Update state only if different */
    if (new_state != old_state) {
        mm_obj_info (self, "state changed (%s -> %s)",
                     mm_modem_state_get_string (old_state),
                     mm_modem_state_get_string (new_state));

        /* The property in the interface is bound to the property
         * in the skeleton, so just updating here is enough */
        g_object_set (self,
                      MM_IFACE_MODEM_STATE, new_state,
                      NULL);

        /* Signal status change */
        if (skeleton) {
            /* Set failure reason */
            if (failed_reason != mm_gdbus_modem_get_state_failed_reason (skeleton))
                mm_gdbus_modem_set_state_failed_reason (skeleton, failed_reason);

            /* Flush current change before signaling the state change,
             * so that clients get the proper state already in the
             * state-changed callback */
            g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
            mm_gdbus_modem_emit_state_changed (skeleton,
                                               old_state,
                                               new_state,
                                               reason);
        }

        /* If we go to a registered/connected state (from unregistered), setup
         * signal quality and access technologies periodic retrieval */
        if (new_state >= MM_MODEM_STATE_REGISTERED && old_state < MM_MODEM_STATE_REGISTERED)
            periodic_signal_check_enable (self);
        /* If we go from a registered/connected state to unregistered,
         * cleanup signal quality retrieval */
        else if (old_state >= MM_MODEM_STATE_REGISTERED && new_state < MM_MODEM_STATE_REGISTERED)
            periodic_signal_check_disable (self, TRUE);
    }

    if (skeleton)
        g_object_unref (skeleton);
    if (bearer_list)
        g_object_unref (bearer_list);
}

void
mm_iface_modem_update_state (MMIfaceModem *self,
                             MMModemState new_state,
                             MMModemStateChangeReason reason)
{
    if (new_state == MM_MODEM_STATE_FAILED) {
        mm_iface_modem_update_failed_state (self, MM_MODEM_STATE_FAILED_REASON_UNKNOWN);
        return;
    }

    __iface_modem_update_state_internal (self, new_state, reason, MM_MODEM_STATE_FAILED_REASON_NONE);
}

void
mm_iface_modem_update_failed_state (MMIfaceModem *self,
                                    MMModemStateFailedReason failed_reason)
{
    __iface_modem_update_state_internal (self, MM_MODEM_STATE_FAILED, MM_MODEM_STATE_CHANGE_REASON_FAILURE, failed_reason);
}

/*****************************************************************************/

typedef struct {
    gchar *subsystem;
    MMModemState state;
} SubsystemState;

static void
subsystem_state_array_free (GArray *array)
{
    guint i;

    for (i = 0; i < array->len; i++) {
        SubsystemState *s;

        s = &g_array_index (array, SubsystemState, i);
        g_free (s->subsystem);
    }

    g_array_free (array, TRUE);
}

static MMModemState
get_consolidated_subsystem_state (MMIfaceModem *self)
{
    /* Use as initial state ENABLED, which is the minimum one expected. Do not
     * use the old modem state as initial state, as that would disallow reporting
     * e.g. ENABLED if the old state was REGISTERED (as ENABLED < REGISTERED). */
    MMModemState consolidated = MM_MODEM_STATE_ENABLED;
    GArray *subsystem_states;

    if (G_UNLIKELY (!state_update_context_quark))
        state_update_context_quark = (g_quark_from_static_string (
                                          STATE_UPDATE_CONTEXT_TAG));

    subsystem_states = g_object_get_qdata (G_OBJECT (self),
                                           state_update_context_quark);

    /* Build consolidated state, expected fixes are:
     *  - Enabled (meaning unregistered) --> Searching|Registered
     *  - Searching --> Registered
     */
    if (subsystem_states) {
        guint i;

        for (i = 0; i < subsystem_states->len; i++) {
            SubsystemState *s;

            s = &g_array_index (subsystem_states, SubsystemState, i);
            if (s->state > consolidated)
                consolidated = s->state;
        }
    }

    return consolidated;
}

static MMModemState
get_updated_consolidated_state (MMIfaceModem *self,
                                MMModemState modem_state,
                                const gchar *subsystem,
                                MMModemState subsystem_state)
{
    guint i;
    GArray *subsystem_states;

    /* Reported subsystem states will be REGISTRATION-related. This means
     * that we would only expect a subset of the states being reported for
     * the subsystem. Warn if we get others */
    g_warn_if_fail (subsystem_state == MM_MODEM_STATE_ENABLED ||
                    subsystem_state == MM_MODEM_STATE_SEARCHING ||
                    subsystem_state == MM_MODEM_STATE_REGISTERED);

    if (G_UNLIKELY (!state_update_context_quark))
        state_update_context_quark = (g_quark_from_static_string (
                                          STATE_UPDATE_CONTEXT_TAG));

    subsystem_states = g_object_get_qdata (G_OBJECT (self),
                                           state_update_context_quark);
    if (!subsystem_states) {
        subsystem_states = g_array_sized_new (FALSE,
                                              FALSE,
                                              sizeof (SubsystemState),
                                              2);
        g_object_set_qdata_full (G_OBJECT (self),
                                 state_update_context_quark,
                                 subsystem_states,
                                 (GDestroyNotify)subsystem_state_array_free);
    }

    /* Store new subsystem state */
    for (i = 0; i < subsystem_states->len; i++) {
        SubsystemState *s;

        s = &g_array_index (subsystem_states, SubsystemState, i);
        if (g_str_equal (s->subsystem, subsystem)) {
            s->state = subsystem_state;
            break;
        }
    }

    /* If not found, insert new element */
    if (i == subsystem_states->len) {
        SubsystemState s;

        mm_obj_dbg (self, "will start keeping track of state for subsystem '%s'", subsystem);
        s.subsystem = g_strdup (subsystem);
        s.state = subsystem_state;
        g_array_append_val (subsystem_states, s);
    }

    return get_consolidated_subsystem_state (self);
}

void
mm_iface_modem_update_subsystem_state (MMIfaceModem *self,
                                       const gchar *subsystem,
                                       MMModemState new_state,
                                       MMModemStateChangeReason reason)
{
    MMModemState consolidated;
    MMModemState state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);

    /* We may have different subsystems being handled (e.g. 3GPP and CDMA), and
     * the registration status value is unique, so if we get subsystem-specific
     * state updates, we'll need to merge all to get a consolidated one. */
    consolidated = get_updated_consolidated_state (self, state, subsystem, new_state);

    /* Don't update registration-related states while disabling/enabling */
    if (state == MM_MODEM_STATE_ENABLING ||
        state == MM_MODEM_STATE_DISABLING)
        return;

    mm_iface_modem_update_state (self, consolidated, reason);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    gboolean enable;
} HandleEnableContext;

static void
handle_enable_context_free (HandleEnableContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
enable_ready (MMBaseModem *self,
              GAsyncResult *res,
              HandleEnableContext *ctx)
{
    GError *error = NULL;

    if (ctx->enable) {
        if (!mm_base_modem_enable_finish (self, res, &error))
            g_dbus_method_invocation_take_error (ctx->invocation, error);
        else
            mm_gdbus_modem_complete_enable (ctx->skeleton, ctx->invocation);
    } else {
        if (!mm_base_modem_disable_finish (self, res, &error))
            g_dbus_method_invocation_take_error (ctx->invocation, error);
        else
            mm_gdbus_modem_complete_enable (ctx->skeleton, ctx->invocation);
    }

    handle_enable_context_free (ctx);
}

static void
handle_enable_auth_ready (MMBaseModem *self,
                          GAsyncResult *res,
                          HandleEnableContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_enable_context_free (ctx);
        return;
    }

    if (abort_invocation_if_state_not_reached (ctx->self, ctx->invocation, MM_MODEM_STATE_LOCKED)) {
        handle_enable_context_free (ctx);
        return;
    }

    if (ctx->enable)
        mm_base_modem_enable (self,
                              (GAsyncReadyCallback)enable_ready,
                              ctx);
    else
        mm_base_modem_disable (self,
                               (GAsyncReadyCallback)enable_ready,
                               ctx);
}

static gboolean
handle_enable (MmGdbusModem *skeleton,
               GDBusMethodInvocation *invocation,
               gboolean enable,
               MMIfaceModem *self)
{
    HandleEnableContext *ctx;

    ctx = g_new (HandleEnableContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->enable = enable;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_enable_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    MMModemPowerState power_state;
} HandleSetPowerStateContext;

static void
handle_set_power_state_context_free (HandleSetPowerStateContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetPowerStateContext, ctx);
}

static void
set_power_state_ready (MMIfaceModem *self,
                       GAsyncResult *res,
                       HandleSetPowerStateContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_power_state_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_set_power_state (ctx->skeleton, ctx->invocation);
    handle_set_power_state_context_free (ctx);
}

static void
handle_set_power_state_auth_ready (MMBaseModem *self,
                                   GAsyncResult *res,
                                   HandleSetPowerStateContext *ctx)
{
    MMModemState modem_state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_power_state_context_free (ctx);
        return;
    }

    /* Only 'off', 'low' or 'up' expected */
    if (ctx->power_state != MM_MODEM_POWER_STATE_LOW &&
        ctx->power_state != MM_MODEM_POWER_STATE_ON &&
        ctx->power_state != MM_MODEM_POWER_STATE_OFF) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_INVALID_ARGS,
                                               "Cannot set '%s' power state",
                                               mm_modem_power_state_get_string (ctx->power_state));
        handle_set_power_state_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    /* Going into LOW or ON only allowed in disabled state */
    if ((ctx->power_state == MM_MODEM_POWER_STATE_LOW ||
         ctx->power_state == MM_MODEM_POWER_STATE_ON) &&
        modem_state != MM_MODEM_STATE_DISABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set power state: not in disabled state");
        handle_set_power_state_context_free (ctx);
        return;
    }

    /* Going into OFF, only allowed if locked, disabled or failed */
    if (ctx->power_state == MM_MODEM_POWER_STATE_OFF &&
        modem_state != MM_MODEM_STATE_FAILED &&
        modem_state != MM_MODEM_STATE_LOCKED &&
        modem_state != MM_MODEM_STATE_DISABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set power state: modem either enabled or initializing");
        handle_set_power_state_context_free (ctx);
        return;
    }

    mm_iface_modem_set_power_state (MM_IFACE_MODEM (self),
                                    ctx->power_state,
                                    (GAsyncReadyCallback)set_power_state_ready,
                                    ctx);
}

static gboolean
handle_set_power_state (MmGdbusModem *skeleton,
                        GDBusMethodInvocation *invocation,
                        guint32 power_state,
                        MMIfaceModem *self)
{
    HandleSetPowerStateContext *ctx;

    ctx = g_slice_new (HandleSetPowerStateContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->power_state = (MMModemPowerState)power_state;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_power_state_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
} HandleResetContext;

static void
handle_reset_context_free (HandleResetContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_reset_ready (MMIfaceModem *self,
                    GAsyncResult *res,
                    HandleResetContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->reset_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_reset (ctx->skeleton, ctx->invocation);

    handle_reset_context_free (ctx);
}

static void
handle_reset_auth_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         HandleResetContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_reset_context_free (ctx);
        return;
    }

    /* If reseting is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->reset ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->reset_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot reset the modem: operation not supported");
        handle_reset_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_GET_INTERFACE (self)->reset (MM_IFACE_MODEM (self),
                                                (GAsyncReadyCallback)handle_reset_ready,
                                                ctx);
}

static gboolean
handle_reset (MmGdbusModem *skeleton,
              GDBusMethodInvocation *invocation,
              MMIfaceModem *self)
{
    HandleResetContext *ctx;

    ctx = g_new (HandleResetContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_reset_auth_ready,
                             ctx);

    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    gchar *code;
} HandleFactoryResetContext;

static void
handle_factory_reset_context_free (HandleFactoryResetContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->code);
    g_free (ctx);
}

static void
handle_factory_reset_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            HandleFactoryResetContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_factory_reset (ctx->skeleton, ctx->invocation);

    handle_factory_reset_context_free (ctx);
}

static void
handle_factory_reset_auth_ready (MMBaseModem *self,
                                 GAsyncResult *res,
                                 HandleFactoryResetContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_factory_reset_context_free (ctx);
        return;
    }

    /* If reseting is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot reset the modem to factory defaults: "
                                               "operation not supported");
        handle_factory_reset_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset (MM_IFACE_MODEM (self),
                                                        ctx->code,
                                                        (GAsyncReadyCallback)handle_factory_reset_ready,
                                                        ctx);
}

static gboolean
handle_factory_reset (MmGdbusModem *skeleton,
                      GDBusMethodInvocation *invocation,
                      const gchar *code,
                      MMIfaceModem *self)
{
    HandleFactoryResetContext *ctx;

    ctx = g_new (HandleFactoryResetContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->code = g_strdup (code);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_factory_reset_auth_ready,
                             ctx);

    return TRUE;
}

/*****************************************************************************/
/* Current capabilities setting
 *
 * Setting capabilities allowed also in FAILED state. Just imagine a
 * 3GPP+3GPP2 modem in 3GPP-only mode without SIM, we should allow
 * changing caps to 3GPP2, which doesn't require SIM
 */

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    MMModemCapability capabilities;
} HandleSetCurrentCapabilitiesContext;

static void
handle_set_current_capabilities_context_free (HandleSetCurrentCapabilitiesContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetCurrentCapabilitiesContext, ctx);
}

static void
set_current_capabilities_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                HandleSetCurrentCapabilitiesContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_capabilities_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* Capabilities updated: explicitly refresh signal and access technology */
        mm_iface_modem_refresh_signal (self);
        mm_gdbus_modem_complete_set_current_capabilities (ctx->skeleton, ctx->invocation);
    }

    handle_set_current_capabilities_context_free (ctx);
}

static void
handle_set_current_capabilities_auth_ready (MMBaseModem *self,
                                            GAsyncResult *res,
                                            HandleSetCurrentCapabilitiesContext *ctx)
{
    GError *error = NULL;
    gchar *capabilities_string;
    GArray *supported;
    gboolean matched = FALSE;
    guint i;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_current_capabilities_context_free (ctx);
        return;
    }

    /* Get list of supported capabilities */
    supported = mm_common_capability_combinations_variant_to_garray (
        mm_gdbus_modem_get_supported_capabilities (ctx->skeleton));

    /* Don't allow capability switching if only one item given in the supported list */
    if (supported->len == 1) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot change capabilities: only one combination supported");
        handle_set_current_capabilities_context_free (ctx);
        g_array_unref (supported);
        return;
    }

    /* Check if the given combination is supported */
    for (i = 0; !matched && i < supported->len; i++) {
        MMModemCapability supported_capability;

        supported_capability = g_array_index (supported, MMModemCapability, i);
        if (supported_capability == ctx->capabilities)
                matched = TRUE;
    }
    g_array_unref (supported);

    if (!matched) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "The given combination of capabilities is not supported");
        handle_set_current_capabilities_context_free (ctx);
        return;
    }

    /* Check if we already are in the requested setup */
    if (mm_gdbus_modem_get_current_capabilities (ctx->skeleton) == ctx->capabilities) {
        /* Nothing to do */
        mm_gdbus_modem_complete_set_current_capabilities (ctx->skeleton, ctx->invocation);
        handle_set_current_capabilities_context_free (ctx);
        return;
    }

    /* If setting current capabilities is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_capabilities ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_capabilities_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Setting current capabilities not supported");
        handle_set_current_capabilities_context_free (ctx);
        return;
    }

    capabilities_string = mm_modem_capability_build_string_from_mask (ctx->capabilities);
    mm_obj_dbg (self, "setting new list of capabilities: %s", capabilities_string);
    g_free (capabilities_string);

    MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_capabilities (
        MM_IFACE_MODEM (self),
        ctx->capabilities,
        (GAsyncReadyCallback)set_current_capabilities_ready,
        ctx);
}

static gboolean
handle_set_current_capabilities (MmGdbusModem *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 guint capabilities,
                                 MMIfaceModem *self)
{
    HandleSetCurrentCapabilitiesContext *ctx;

    ctx = g_slice_new (HandleSetCurrentCapabilitiesContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->capabilities = capabilities;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_current_capabilities_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Current bands setting */

#define AFTER_SET_LOAD_CURRENT_BANDS_RETRIES      5
#define AFTER_SET_LOAD_CURRENT_BANDS_TIMEOUT_SECS 1

typedef struct {
    MmGdbusModem *skeleton;
    GArray       *bands_array;
    GArray       *supported_bands_array; /* when ANY requested */
    guint         retries;
} SetCurrentBandsContext;

static void
set_current_bands_context_free (SetCurrentBandsContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    if (ctx->bands_array)
        g_array_unref (ctx->bands_array);
    if (ctx->supported_bands_array)
        g_array_unref (ctx->supported_bands_array);
    g_slice_free (SetCurrentBandsContext, ctx);
}

gboolean
mm_iface_modem_set_current_bands_finish (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_current_bands_complete_with_defaults (GTask *task)
{
    SetCurrentBandsContext *ctx;

    ctx = g_task_get_task_data (task);

    /* Never show just 'any' in the interface */
    if (ctx->bands_array->len == 1 && g_array_index (ctx->bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        g_assert (ctx->supported_bands_array);
        g_array_unref (ctx->bands_array);
        ctx->bands_array = g_array_ref (ctx->supported_bands_array);
    }

    mm_common_bands_garray_sort (ctx->bands_array);
    mm_gdbus_modem_set_current_bands (ctx->skeleton, mm_common_bands_garray_to_variant (ctx->bands_array));

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void set_current_bands_reload_schedule (GTask *task);

static void
after_set_load_current_bands_ready (MMIfaceModem *self,
                                    GAsyncResult *res,
                                    GTask        *task)
{
    SetCurrentBandsContext *ctx;
    GArray                 *current_bands;
    GError                 *error = NULL;
    GArray                 *requested_bands = NULL;

    ctx = g_task_get_task_data (task);

    current_bands = MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands_finish (self, res, &error);
    if (!current_bands) {
        /* If we can retry, do it */
        if (ctx->retries > 0) {
            mm_obj_dbg (self, "couldn't load current bands: %s (will retry)", error->message);
            g_clear_error (&error);
            set_current_bands_reload_schedule (task);
            goto out;
        }

        /* Errors when reloading bands won't be critical */
        mm_obj_warn (self, "couldn't load current bands: %s", error->message);
        g_clear_error (&error);
        set_current_bands_complete_with_defaults (task);
        goto out;
    }

    if ((ctx->bands_array->len == 1) && (g_array_index (ctx->bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY))
        requested_bands = g_array_ref (ctx->supported_bands_array);
    else
        requested_bands = g_array_ref (ctx->bands_array);

    /* Compare arrays */
    if (!mm_common_bands_garray_cmp (current_bands, requested_bands)) {
        gchar *requested_str;
        gchar *current_str;

        /* If we can retry, do it */
        if (ctx->retries > 0) {
            mm_obj_dbg (self, "reloaded current bands different to the requested ones (will retry)");
            set_current_bands_reload_schedule (task);
            goto out;
        }

        requested_str = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)requested_bands->data, requested_bands->len);
        current_str   = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)current_bands->data, current_bands->len);
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "reloaded current bands (%s) different to the requested ones (%s)",
                             current_str, requested_str);
        g_free (requested_str);
        g_free (current_str);
    }

    /* Store as current the last loaded ones and set operation result */
    mm_common_bands_garray_sort (current_bands);
    mm_gdbus_modem_set_current_bands (ctx->skeleton, mm_common_bands_garray_to_variant (current_bands));

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);

 out:
    g_array_unref (requested_bands);
    g_array_unref (current_bands);
}

static gboolean
set_current_bands_reload (GTask *task)
{
    MMIfaceModem           *self;
    SetCurrentBandsContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (ctx->retries > 0);
    ctx->retries--;

    MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands (
        self,
        (GAsyncReadyCallback)after_set_load_current_bands_ready,
        task);

    return G_SOURCE_REMOVE;
}

static void
set_current_bands_reload_schedule (GTask *task)
{
    g_timeout_add_seconds (AFTER_SET_LOAD_CURRENT_BANDS_TIMEOUT_SECS,
                           (GSourceFunc) set_current_bands_reload,
                           task);
}

static void
set_current_bands_ready (MMIfaceModem *self,
                         GAsyncResult *res,
                         GTask *task)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_bands_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands &&
        MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands_finish) {
        set_current_bands_reload (task);
        return;
    }

    /* Default to the ones we requested */
    set_current_bands_complete_with_defaults (task);
}

static gboolean
validate_bands (const GArray *supported_bands_array,
                const GArray *bands_array,
                GError **error)
{
    /* When the array has more than one element, there MUST NOT include ANY or
     * UNKNOWN */
    if (bands_array->len > 1) {
        guint i;

        for (i = 0; i < bands_array->len; i++) {
            MMModemBand band;

            band = g_array_index (bands_array, MMModemBand, i);
            if (band == MM_MODEM_BAND_UNKNOWN ||
                band == MM_MODEM_BAND_ANY) {
                g_set_error (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_INVALID_ARGS,
                             "Wrong list of bands: "
                             "'%s' should have been the only element in the list",
                             mm_modem_band_get_string (band));
                return FALSE;
            }

            if (supported_bands_array->len > 1 ||
                (g_array_index (supported_bands_array, MMModemBand, 0) != MM_MODEM_BAND_ANY &&
                 g_array_index (supported_bands_array, MMModemBand, 0) != MM_MODEM_BAND_UNKNOWN)) {
                gboolean found = FALSE;
                guint j;

                /* The band given in allowed MUST be available in supported */
                for (j = 0; !found && j < supported_bands_array->len; j++) {
                    if (band == g_array_index (supported_bands_array, MMModemBand, j))
                        found = TRUE;
                }

                if (!found) {
                    gchar *supported_bands_str;

                    supported_bands_str = (mm_common_build_bands_string (
                                               (const MMModemBand *)(gconstpointer)supported_bands_array->data,
                                               supported_bands_array->len));
                    g_set_error (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_INVALID_ARGS,
                                 "Given allowed band (%s) is not supported (%s)",
                                 mm_modem_band_get_string (band),
                                 supported_bands_str);
                    g_free (supported_bands_str);
                    return FALSE;
                }
            }
        }
    }
    return TRUE;
}

void
mm_iface_modem_set_current_bands (MMIfaceModem *self,
                                  GArray *bands_array,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    SetCurrentBandsContext *ctx;
    GArray *current_bands_array;
    GError *error = NULL;
    gchar *bands_string;
    GTask *task;

    /* If setting allowed bands is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_bands ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_bands_finish) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 mm_iface_modem_set_current_bands,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Setting allowed bands not supported");
        return;
    }

    /* Setup context */
    ctx = g_slice_new0 (SetCurrentBandsContext);
    ctx->retries = AFTER_SET_LOAD_CURRENT_BANDS_RETRIES;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_current_bands_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    bands_string = mm_common_build_bands_string ((const MMModemBand *)(gpointer)bands_array->data, bands_array->len);

    /* Get list of supported bands */
    ctx->supported_bands_array = (mm_common_bands_variant_to_garray (
                                      mm_gdbus_modem_get_supported_bands (ctx->skeleton)));

    /* Set ctx->bands_array to target list of bands before comparing with current list
     * of bands. If input list of bands contains only ANY, target list of bands is set
     * to list of supported bands excluding ANY. */
    if (bands_array->len == 1 &&
        g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        guint i;

        for (i = 0; i < ctx->supported_bands_array->len; i++) {
            MMModemBand band = g_array_index (ctx->supported_bands_array, MMModemBand, i);

            if (band != MM_MODEM_BAND_ANY &&
                band != MM_MODEM_BAND_UNKNOWN) {
                if (!ctx->bands_array)
                    ctx->bands_array = g_array_sized_new (FALSE,
                                                          FALSE,
                                                          sizeof (MMModemBand),
                                                          ctx->supported_bands_array->len);
                g_array_append_val (ctx->bands_array, band);
            }
        }
    }

    if (!ctx->bands_array)
        ctx->bands_array = g_array_ref (bands_array);

    /* Simply return if target list of bands equals to current list of bands */
    current_bands_array = (mm_common_bands_variant_to_garray (
                              mm_gdbus_modem_get_current_bands (ctx->skeleton)));
    if (mm_common_bands_garray_cmp (ctx->bands_array, current_bands_array)) {
        mm_obj_dbg (self, "requested list of bands (%s) is equal to the current ones, skipping re-set", bands_string);
        g_free (bands_string);
        g_array_unref (current_bands_array);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Done comparison with current list of bands. Always use input list of bands
     * when setting bands */
    if (ctx->bands_array != bands_array) {
        g_array_unref (ctx->bands_array);
        ctx->bands_array = g_array_ref (bands_array);
    }

    /* Validate input list of bands */
    if (!validate_bands (ctx->supported_bands_array,
                         ctx->bands_array,
                         &error)) {
        mm_obj_dbg (self, "requested list of bands (%s) cannot be handled", bands_string);
        g_free (bands_string);
        g_array_unref (current_bands_array);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "setting new list of bands: %s", bands_string);
    MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_bands (
        self,
        ctx->bands_array,
        (GAsyncReadyCallback)set_current_bands_ready,
        task);

    g_array_unref (current_bands_array);
    g_free (bands_string);
}

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    GVariant *bands;
} HandleSetCurrentBandsContext;

static void
handle_set_current_bands_context_free (HandleSetCurrentBandsContext *ctx)
{
    g_variant_unref (ctx->bands);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetCurrentBandsContext, ctx);
}

static void
handle_set_current_bands_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                HandleSetCurrentBandsContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_current_bands_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* Bands updated: explicitly refresh signal and access technology */
        mm_iface_modem_refresh_signal (self);
        mm_gdbus_modem_complete_set_current_bands (ctx->skeleton, ctx->invocation);
    }

    handle_set_current_bands_context_free (ctx);
}

static void
handle_set_current_bands_auth_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     HandleSetCurrentBandsContext *ctx)
{
    GArray *bands_array;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_current_bands_context_free (ctx);
        return;
    }

    if (abort_invocation_if_state_not_reached (ctx->self, ctx->invocation, MM_MODEM_STATE_DISABLED)) {
        handle_set_current_bands_context_free (ctx);
        return;
    }

    bands_array = mm_common_bands_variant_to_garray (ctx->bands);
    mm_iface_modem_set_current_bands (MM_IFACE_MODEM (self),
                                      bands_array,
                                      (GAsyncReadyCallback)handle_set_current_bands_ready,
                                      ctx);
    g_array_unref (bands_array);
}

static gboolean
handle_set_current_bands (MmGdbusModem *skeleton,
                          GDBusMethodInvocation *invocation,
                          GVariant *bands_variant,
                          MMIfaceModem *self)
{
    HandleSetCurrentBandsContext *ctx;

    ctx = g_slice_new (HandleSetCurrentBandsContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->bands = g_variant_ref (bands_variant);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_current_bands_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Set current modes */

#define AFTER_SET_LOAD_CURRENT_MODES_RETRIES      5
#define AFTER_SET_LOAD_CURRENT_MODES_TIMEOUT_SECS 1

typedef struct {
    MmGdbusModem *skeleton;
    MMModemMode   allowed;
    MMModemMode   preferred;
    guint         retries;
} SetCurrentModesContext;

static void
set_current_modes_context_free (SetCurrentModesContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_slice_free (SetCurrentModesContext, ctx);
}

gboolean
mm_iface_modem_set_current_modes_finish (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_current_modes_reload_schedule (GTask *task);

static void
after_set_load_current_modes_ready (MMIfaceModem *self,
                                    GAsyncResult *res,
                                    GTask *task)
{
    SetCurrentModesContext *ctx;
    MMModemMode allowed = MM_MODEM_MODE_NONE;
    MMModemMode preferred = MM_MODEM_MODE_NONE;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_modes_finish (self,
                                                                         res,
                                                                         &allowed,
                                                                         &preferred,
                                                                         &error)) {
        /* If we can retry, do it */
        if (ctx->retries > 0) {
            mm_obj_dbg (self, "couldn't load current allowed/preferred modes: %s", error->message);
            g_error_free (error);
            set_current_modes_reload_schedule (task);
            return;
        }

        /* Errors when getting allowed/preferred won't be critical */
        mm_obj_warn (self, "couldn't load current allowed/preferred modes: %s", error->message);
        g_clear_error (&error);

        /* If errors getting allowed modes, default to the ones we asked for */
        mm_gdbus_modem_set_current_modes (ctx->skeleton, g_variant_new ("(uu)", ctx->allowed, ctx->preferred));
        goto out;
    }

    /* Store as current the last loaded ones and set operation result */
    mm_gdbus_modem_set_current_modes (ctx->skeleton, g_variant_new ("(uu)", allowed, preferred));

    /* Compare modes. If the requested one was ANY, we won't consider an error if the
     * result differs. */
    if (((allowed != ctx->allowed) || (preferred != ctx->preferred)) && (ctx->allowed != MM_MODEM_MODE_ANY)) {
        gchar *requested_allowed_str;
        gchar *requested_preferred_str;
        gchar *current_allowed_str;
        gchar *current_preferred_str;

        /* If we can retry, do it */
        if (ctx->retries > 0) {
            mm_obj_dbg (self, "reloaded current modes different to the requested ones (will retry)");
            set_current_modes_reload_schedule (task);
            return;
        }

        requested_allowed_str = mm_modem_mode_build_string_from_mask (ctx->allowed);
        requested_preferred_str = mm_modem_mode_build_string_from_mask (ctx->preferred);
        current_allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        current_preferred_str = mm_modem_mode_build_string_from_mask (preferred);

        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "reloaded modes (allowed '%s' and preferred '%s') different "
                             "to the requested ones (allowed '%s' and preferred '%s')",
                             requested_allowed_str, requested_preferred_str,
                             current_allowed_str, current_preferred_str);

        g_free (requested_allowed_str);
        g_free (requested_preferred_str);
        g_free (current_allowed_str);
        g_free (current_preferred_str);
    }

out:
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
set_current_modes_reload (GTask *task)
{
    MMIfaceModem           *self;
    SetCurrentModesContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (ctx->retries > 0);
    ctx->retries--;

    MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_modes (
        self,
        (GAsyncReadyCallback)after_set_load_current_modes_ready,
        task);

    return G_SOURCE_REMOVE;
}

static void
set_current_modes_reload_schedule (GTask *task)
{
    g_timeout_add_seconds (AFTER_SET_LOAD_CURRENT_MODES_TIMEOUT_SECS,
                           (GSourceFunc) set_current_modes_reload,
                           task);
}

static void
set_current_modes_ready (MMIfaceModem *self,
                         GAsyncResult *res,
                         GTask *task)
{
    SetCurrentModesContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_modes_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_modes &&
        MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_modes_finish) {
        set_current_modes_reload (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Default to the ones we requested */
    mm_gdbus_modem_set_current_modes (ctx->skeleton,
                                      g_variant_new ("(uu)",
                                                     ctx->allowed,
                                                     ctx->preferred));

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_iface_modem_set_current_modes (MMIfaceModem *self,
                                  MMModemMode allowed,
                                  MMModemMode preferred,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GArray *supported;
    SetCurrentModesContext *ctx;
    MMModemMode current_allowed = MM_MODEM_MODE_ANY;
    MMModemMode current_preferred = MM_MODEM_MODE_NONE;
    guint i;
    GTask *task;

    /* If setting allowed modes is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_modes ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_modes_finish) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 mm_iface_modem_set_current_modes,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Setting allowed modes not supported");
        return;
    }

    /* Setup context */
    ctx = g_slice_new0 (SetCurrentModesContext);
    ctx->retries = AFTER_SET_LOAD_CURRENT_MODES_RETRIES;
    ctx->allowed = allowed;
    ctx->preferred = preferred;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_current_modes_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    /* Get list of supported modes */
    supported = mm_common_mode_combinations_variant_to_garray (
        mm_gdbus_modem_get_supported_modes (ctx->skeleton));

    /* Don't allow mode switching if only one item given in the supported list */
    if (supported->len == 1) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Cannot change modes: only one combination supported");
        g_object_unref (task);
        g_array_unref (supported);
        return;
    }

    if (allowed == MM_MODEM_MODE_ANY &&
        preferred == MM_MODEM_MODE_NONE) {
        /* Allow allowed=ANY & preferred=NONE, all plugins should support it */
    } else {
        gboolean matched = FALSE;

        /* Check if the given combination is supported */
        for (i = 0; !matched && i < supported->len; i++) {
            MMModemModeCombination *supported_mode;

            supported_mode = &g_array_index (supported, MMModemModeCombination, i);
            if ((supported_mode->allowed == MM_MODEM_MODE_ANY &&
                 supported_mode->preferred == MM_MODEM_MODE_NONE) ||
                (supported_mode->allowed == allowed &&
                 supported_mode->preferred == preferred)) {
                matched = TRUE;
            }
        }

        if (!matched) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "The given combination of allowed and preferred modes is not supported");
            g_object_unref (task);
            g_array_unref (supported);
            return;
        }
    }

    g_array_unref (supported);

    /* Check if we already are in the requested setup */
    g_variant_get (mm_gdbus_modem_get_current_modes (ctx->skeleton),
                   "(uu)",
                   &current_allowed,
                   &current_preferred);
    if (current_allowed == allowed &&
        current_preferred == preferred) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Ensure preferred, if given, is a subset of allowed */
    if ((allowed ^ preferred) & preferred) {
        gchar *preferred_str;
        gchar *allowed_str;

        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_UNSUPPORTED,
                                 "Preferred mode (%s) is not allowed (%s)",
                                 preferred_str,
                                 allowed_str);
        g_object_unref (task);
        g_free (preferred_str);
        g_free (allowed_str);
        return;
    }

    ctx->allowed = allowed;
    ctx->preferred = preferred;
    MM_IFACE_MODEM_GET_INTERFACE (self)->set_current_modes (self,
                                                            allowed,
                                                            preferred,
                                                            (GAsyncReadyCallback)set_current_modes_ready,
                                                            task);
}

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    MMModemMode allowed;
    MMModemMode preferred;
} HandleSetCurrentModesContext;

static void
handle_set_current_modes_context_free (HandleSetCurrentModesContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_set_current_modes_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                HandleSetCurrentModesContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_current_modes_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* Modes updated: explicitly refresh signal and access technology */
        mm_iface_modem_refresh_signal (self);
        mm_gdbus_modem_complete_set_current_modes (ctx->skeleton, ctx->invocation);
    }

    handle_set_current_modes_context_free (ctx);
}

static void
handle_set_current_modes_auth_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     HandleSetCurrentModesContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_current_modes_context_free (ctx);
        return;
    }

    if (abort_invocation_if_state_not_reached (ctx->self, ctx->invocation, MM_MODEM_STATE_DISABLED)) {
        handle_set_current_modes_context_free (ctx);
        return;
    }

    mm_iface_modem_set_current_modes (MM_IFACE_MODEM (self),
                                      ctx->allowed,
                                      ctx->preferred,
                                      (GAsyncReadyCallback)handle_set_current_modes_ready,
                                      ctx);
}

static gboolean
handle_set_current_modes (MmGdbusModem *skeleton,
                          GDBusMethodInvocation *invocation,
                          GVariant *variant,
                          MMIfaceModem *self)
{
    HandleSetCurrentModesContext *ctx;

    ctx = g_new (HandleSetCurrentModesContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    g_variant_get (variant,
                   "(uu)",
                   &ctx->allowed,
                   &ctx->preferred);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_current_modes_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static void
reinitialize_ready (MMBaseModem *self,
                    GAsyncResult *res)
{
    GError *error = NULL;

    mm_base_modem_initialize_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "reinitialization failed: %s", error->message);
        g_error_free (error);
    }
}

static gboolean
restart_initialize_idle (MMIfaceModem *self)
{
    g_object_set_qdata (G_OBJECT (self), restart_initialize_idle_quark, NULL);

    /* If no wait needed, just go on */
    mm_base_modem_initialize (MM_BASE_MODEM (self),
                              (GAsyncReadyCallback) reinitialize_ready,
                              NULL);
    return G_SOURCE_REMOVE;
}

static void
restart_initialize_idle_cancel (gpointer idp)
{
    g_source_remove (GPOINTER_TO_UINT (idp));
}

static void
set_lock_status (MMIfaceModem *self,
                 MmGdbusModem *skeleton,
                 MMModemLock lock)
{
    MMModemLock old_lock;

    old_lock = mm_gdbus_modem_get_unlock_required (skeleton);
    mm_gdbus_modem_set_unlock_required (skeleton, lock);
    if (lock == MM_MODEM_LOCK_UNKNOWN)
        mm_gdbus_modem_set_unlock_retries (skeleton, 0);

    /* We don't care about SIM-PIN2/SIM-PUK2 since the device is
     * operational without it. */
    if (lock == MM_MODEM_LOCK_NONE ||
        lock == MM_MODEM_LOCK_SIM_PIN2 ||
        lock == MM_MODEM_LOCK_SIM_PUK2) {
        /* Notify transition from INITIALIZING/LOCKED to DISABLED */
        if (old_lock != MM_MODEM_LOCK_NONE &&
            old_lock != MM_MODEM_LOCK_SIM_PIN2 &&
            old_lock != MM_MODEM_LOCK_SIM_PUK2) {
            /* Only restart initialization if leaving LOCKED.
             * If this is the case, we do NOT update the state yet, we wait
             * to be completely re-initialized to do so. */
            if (old_lock != MM_MODEM_LOCK_UNKNOWN) {
                guint id;

                if (G_UNLIKELY (!restart_initialize_idle_quark))
                    restart_initialize_idle_quark = (g_quark_from_static_string (RESTART_INITIALIZE_IDLE_TAG));

                id = g_idle_add ((GSourceFunc)restart_initialize_idle, self);
                g_object_set_qdata_full (G_OBJECT (self),
                                         restart_initialize_idle_quark,
                                         GUINT_TO_POINTER (id),
                                         (GDestroyNotify)restart_initialize_idle_cancel);
            }
        }
    } else {
        if (old_lock == MM_MODEM_LOCK_UNKNOWN) {
            /* Notify transition from INITIALIZING to LOCKED */
            mm_iface_modem_update_state (self,
                                         MM_MODEM_STATE_LOCKED,
                                         MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
        }
    }
}

MMModemLock
mm_iface_modem_get_unlock_required (MMIfaceModem *self)
{
    MmGdbusModem *skeleton = NULL;
    MMModemLock   lock;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (skeleton) {
        lock = mm_gdbus_modem_get_unlock_required (skeleton);
        g_object_unref (skeleton);
    } else
        lock = MM_MODEM_LOCK_UNKNOWN;

    return lock;
}

MMUnlockRetries *
mm_iface_modem_get_unlock_retries (MMIfaceModem *self)
{
    MmGdbusModem *skeleton = NULL;
    MMUnlockRetries *unlock_retries;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (skeleton) {
        GVariant *dictionary;

        dictionary = mm_gdbus_modem_get_unlock_retries (skeleton);
        unlock_retries = mm_unlock_retries_new_from_dictionary (dictionary);
        g_object_unref (skeleton);
    } else
        unlock_retries = mm_unlock_retries_new ();

    return unlock_retries;
}

void
mm_iface_modem_update_unlock_retries (MMIfaceModem *self,
                                      MMUnlockRetries *unlock_retries)
{
    MmGdbusModem *skeleton = NULL;
    GVariant *previous_dictionary;
    MMUnlockRetries *previous_unlock_retries;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    previous_dictionary = mm_gdbus_modem_get_unlock_retries (skeleton);
    previous_unlock_retries = mm_unlock_retries_new_from_dictionary (previous_dictionary);

    /* If they are different, update */
    if (!mm_unlock_retries_cmp (unlock_retries, previous_unlock_retries)) {
        GVariant *new_dictionary;

        new_dictionary = mm_unlock_retries_get_dictionary (unlock_retries);
        mm_gdbus_modem_set_unlock_retries (skeleton, new_dictionary);
        g_variant_unref (new_dictionary);
    }

    g_object_unref (previous_unlock_retries);
    g_object_unref (skeleton);
}

typedef enum {
    UPDATE_LOCK_INFO_CONTEXT_STEP_FIRST = 0,
    UPDATE_LOCK_INFO_CONTEXT_STEP_LOCK,
    UPDATE_LOCK_INFO_CONTEXT_STEP_AFTER_UNLOCK,
    UPDATE_LOCK_INFO_CONTEXT_STEP_RETRIES,
    UPDATE_LOCK_INFO_CONTEXT_STEP_LAST
} UpdateLockInfoContextStep;

typedef struct {
    UpdateLockInfoContextStep step;
    MmGdbusModem *skeleton;
    MMModemLock lock;
    GError *saved_error;
} UpdateLockInfoContext;

static void
update_lock_info_context_free (UpdateLockInfoContext *ctx)
{
    g_assert (ctx->saved_error == NULL);

    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_slice_free (UpdateLockInfoContext, ctx);
}

MMModemLock
mm_iface_modem_update_lock_info_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    GError *inner_error = NULL;
    gssize value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCK_UNKNOWN;
    }
    return (MMModemLock)value;
}

static void update_lock_info_context_step (GTask *task);

static void
load_unlock_retries_ready (MMIfaceModem *self,
                           GAsyncResult *res,
                           GTask *task)
{
    UpdateLockInfoContext *ctx;
    GError *error = NULL;
    MMUnlockRetries *unlock_retries;

    unlock_retries = MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_retries_finish (self, res, &error);
    if (!unlock_retries) {
        mm_obj_warn (self, "couldn't load unlock retries: %s", error->message);
        g_error_free (error);
    } else {
        /* Update the dictionary in the DBus interface */
        mm_iface_modem_update_unlock_retries (self, unlock_retries);
        g_object_unref (unlock_retries);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    update_lock_info_context_step (task);
}

static void
modem_after_sim_unlock_ready (MMIfaceModem *self,
                              GAsyncResult *res,
                              GTask *task)
{
    UpdateLockInfoContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_sim_unlock_finish (self, res, &error)) {
        mm_obj_warn (self, "after SIM unlock failed: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    update_lock_info_context_step (task);
}

static void
internal_load_unlock_required_ready (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GTask *task)
{
    UpdateLockInfoContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->lock = internal_load_unlock_required_finish (self, res, &error);
    if (error) {
        /* Treat several SIM related, serial and other core errors as critical
         * and abort the checks. These will end up moving the modem to a FAILED
         * state. */
        if (error->domain == MM_SERIAL_ERROR ||
            g_error_matches (error,
                             G_IO_ERROR,
                             G_IO_ERROR_CANCELLED)) {
            ctx->saved_error = error;
            ctx->step = UPDATE_LOCK_INFO_CONTEXT_STEP_LAST;
            update_lock_info_context_step (task);
            return;
        }

        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG)) {
            /* SIM errors are only critical in 3GPP-only devices */
            if (!mm_iface_modem_is_cdma (self)) {
                ctx->saved_error = error;
                ctx->step = UPDATE_LOCK_INFO_CONTEXT_STEP_LAST;
                update_lock_info_context_step (task);
                return;
            }

            /* For mixed 3GPP+3GPP2 devices, skip SIM errors */
            mm_obj_dbg (self, "skipping SIM error in 3GPP2-capable device, assuming no lock is needed");
            g_error_free (error);
            ctx->lock = MM_MODEM_LOCK_NONE;
        } else {
            mm_obj_dbg (self, "couldn't check if unlock required: %s", error->message);
            g_error_free (error);
            ctx->lock = MM_MODEM_LOCK_UNKNOWN;
        }
    }

    /* Go on to next step */
    ctx->step++;
    update_lock_info_context_step (task);
}

static void
update_lock_info_context_step (GTask *task)
{
    MMIfaceModem *self;
    UpdateLockInfoContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case UPDATE_LOCK_INFO_CONTEXT_STEP_FIRST:
        /* We need the skeleton around */
        if (!ctx->skeleton) {
            ctx->saved_error = g_error_new (MM_CORE_ERROR,
                                            MM_CORE_ERROR_FAILED,
                                            "Couldn't get interface skeleton");
            ctx->step = UPDATE_LOCK_INFO_CONTEXT_STEP_LAST;
            update_lock_info_context_step (task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case UPDATE_LOCK_INFO_CONTEXT_STEP_LOCK:
        /* Don't re-ask if already known */
        if (ctx->lock == MM_MODEM_LOCK_UNKNOWN) {
            /* If we're already unlocked, we're done */
            internal_load_unlock_required (
                self,
                (GAsyncReadyCallback)internal_load_unlock_required_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case UPDATE_LOCK_INFO_CONTEXT_STEP_AFTER_UNLOCK:
        /* If we get that no lock is required, run the after SIM unlock step
         * in order to wait for the SIM to get ready.  Skip waiting on
         * CDMA-only modems where we don't support a SIM. */
        if (!mm_iface_modem_is_cdma_only (self) &&
            (ctx->lock == MM_MODEM_LOCK_NONE ||
             ctx->lock == MM_MODEM_LOCK_SIM_PIN2 ||
             ctx->lock == MM_MODEM_LOCK_SIM_PUK2)) {
            if (MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_sim_unlock != NULL &&
                MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_sim_unlock_finish != NULL) {
                mm_obj_dbg (self, "SIM is ready, running after SIM unlock step...");
                MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_sim_unlock (
                    self,
                    (GAsyncReadyCallback)modem_after_sim_unlock_ready,
                    task);
                return;
            }

            /* If no way to run after SIM unlock step, we're done */
            mm_obj_dbg (self, "SIM is ready, and no need for the after SIM unlock step...");
        }
        ctx->step++;
        /* fall-through */

    case UPDATE_LOCK_INFO_CONTEXT_STEP_RETRIES:
        /* Load unlock retries if possible */
        if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_retries &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_retries_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_retries (
                self,
                (GAsyncReadyCallback)load_unlock_retries_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case UPDATE_LOCK_INFO_CONTEXT_STEP_LAST:
        if (ctx->saved_error) {
            set_lock_status (self, ctx->skeleton, MM_MODEM_LOCK_UNKNOWN);
            /* Return saved error */
            g_task_return_error (task, ctx->saved_error);
            ctx->saved_error = NULL;
        } else {
            /* Update lock status and modem status if needed */
            set_lock_status (self, ctx->skeleton, ctx->lock);
            g_task_return_int (task, ctx->lock);
        }

        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

void
mm_iface_modem_update_lock_info (MMIfaceModem *self,
                                 MMModemLock known_lock,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    UpdateLockInfoContext *ctx;
    GTask *task;

    ctx = g_slice_new0 (UpdateLockInfoContext);

    /* If the given lock is known, we will avoid re-asking for it */
    ctx->lock = known_lock;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)update_lock_info_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);

    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    update_lock_info_context_step (task);
}

/*****************************************************************************/
/* Set power state sequence */

typedef enum {
    SET_POWER_STATE_STEP_FIRST,
    SET_POWER_STATE_STEP_LOAD,
    SET_POWER_STATE_STEP_CHECK,
    SET_POWER_STATE_STEP_UPDATE,
    SET_POWER_STATE_STEP_FCC_UNLOCK,
    SET_POWER_STATE_STEP_AFTER_UPDATE,
    SET_POWER_STATE_STEP_LAST,
} SetPowerStateStep;

typedef struct {
    SetPowerStateStep  step;
    MmGdbusModem      *skeleton;
    GError            *saved_error;
    gboolean           fcc_unlock_attempted;
    MMModemPowerState  requested_power_state;
    MMModemPowerState  previous_cached_power_state;
    MMModemPowerState  previous_real_power_state;

    void     (*requested_power_setup)        (MMIfaceModem         *self,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data);
    gboolean (*requested_power_setup_finish) (MMIfaceModem         *self,
                                              GAsyncResult         *res,
                                              GError              **error);
} SetPowerStateContext;

static void
set_power_state_context_free (SetPowerStateContext *ctx)
{
    g_assert (!ctx->saved_error);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_slice_free (SetPowerStateContext, ctx);
}

gboolean
mm_iface_modem_set_power_state_finish (MMIfaceModem  *self,
                                       GAsyncResult  *res,
                                       GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_power_state_step (GTask *task);

static void
modem_after_power_up_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    SetPowerStateContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (!ctx->saved_error);
    MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_power_up_finish (self, res, &ctx->saved_error);
    if (ctx->saved_error)
        mm_obj_dbg (self, "failure running after power up step: %s", ctx->saved_error->message);

    ctx->step++;
    set_power_state_step (task);
}

static void
fcc_unlock_ready (MMIfaceModem *self,
                  GAsyncResult *res,
                  GTask        *task)
{
    SetPowerStateContext *ctx;
    g_autoptr(GError)     error = NULL;

    ctx = g_task_get_task_data (task);
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->fcc_unlock_finish (self, res, &error))
        mm_obj_dbg (self, "couldn't run FCC unlock: %s", error->message);

    /* always retry, even on reported error */
    ctx->step = SET_POWER_STATE_STEP_UPDATE;
    set_power_state_step (task);
}

static void
requested_power_setup_ready (MMIfaceModem *self,
                             GAsyncResult *res,
                             GTask        *task)
{
    SetPowerStateContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (!ctx->saved_error);
    if (!ctx->requested_power_setup_finish (self, res, &ctx->saved_error))
        mm_obj_dbg (self, "couldn't update power state: %s", ctx->saved_error->message);

    ctx->step++;
    set_power_state_step (task);
}

static void
set_power_state_load_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    SetPowerStateContext *ctx;
    g_autoptr(GError)     error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->previous_real_power_state = MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "couldn't reload current power state: %s", error->message);
        /* Default to the cached one */
        ctx->previous_real_power_state = ctx->previous_cached_power_state;
    }

    ctx->step++;
    set_power_state_step (task);
}

static void
set_power_state_step (GTask *task)
{
    MMIfaceModem         *self;
    SetPowerStateContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    switch (ctx->step) {
    case SET_POWER_STATE_STEP_FIRST:
        ctx->step++;
        /* fall-through */

    case SET_POWER_STATE_STEP_LOAD:
        /* We cannot really rely on the power state value that we had cached before,
         * as the real power status of the modem may also be changed by rfkill. So,
         * before updating the current power state, re-check which is the real power
         * state. */
        if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state (self, (GAsyncReadyCallback)set_power_state_load_ready, task);
            return;
        }
        /* If there is no way to load power state, just keep on assuming the cached
         * one is also the real one */
        ctx->previous_real_power_state = ctx->previous_cached_power_state;
        ctx->step++;
        /* fall-through */

    case SET_POWER_STATE_STEP_CHECK:
        /* Already done if we're in the desired power state */
        if (ctx->previous_real_power_state == ctx->requested_power_state) {
            mm_obj_dbg (self, "no need to change power state: already '%s'",
                        mm_modem_power_state_get_string (ctx->requested_power_state));
            ctx->step = SET_POWER_STATE_STEP_LAST;
            set_power_state_step (task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case SET_POWER_STATE_STEP_UPDATE:
        mm_obj_dbg (self, "updating power state: '%s'...", mm_modem_power_state_get_string (ctx->requested_power_state));

        /* Error if unsupported */
        if (!ctx->requested_power_setup || !ctx->requested_power_setup_finish) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                     "Requested power transition is not supported by this modem");
            g_object_unref (task);
            return;
        }

        ctx->requested_power_setup (self, (GAsyncReadyCallback)requested_power_setup_ready, task);
        return;

    case SET_POWER_STATE_STEP_FCC_UNLOCK:
        /* The FCC unlock operation and update retry should only be done
         * if requested by the implementation with MM_CORE_ERROR_RETRY. */
        if ((ctx->requested_power_state == MM_MODEM_POWER_STATE_ON) &&
            ctx->saved_error &&
            g_error_matches (ctx->saved_error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY) &&
            !ctx->fcc_unlock_attempted &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->fcc_unlock &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->fcc_unlock_finish) {
            mm_obj_dbg (self, "attempting fcc unlock...");
            ctx->fcc_unlock_attempted = TRUE;
            g_clear_error (&ctx->saved_error);
            MM_IFACE_MODEM_GET_INTERFACE (self)->fcc_unlock (self, (GAsyncReadyCallback)fcc_unlock_ready, task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case SET_POWER_STATE_STEP_AFTER_UPDATE:
        if ((ctx->requested_power_state == MM_MODEM_POWER_STATE_ON) &&
            !ctx->saved_error &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_power_up &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_power_up_finish) {
            mm_obj_dbg (self, "running after power up operation...");
            MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_power_up (self, (GAsyncReadyCallback)modem_after_power_up_ready, task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case SET_POWER_STATE_STEP_LAST:
        if (ctx->saved_error) {
            /* If the real and cached ones are different, set the real one */
            if (ctx->previous_cached_power_state != ctx->previous_real_power_state)
                mm_gdbus_modem_set_power_state (ctx->skeleton, ctx->previous_real_power_state);
            g_task_return_error (task, g_steal_pointer (&ctx->saved_error));
        } else {
            mm_obj_info (self, "power state updated: %s", mm_modem_power_state_get_string (ctx->requested_power_state));
            mm_gdbus_modem_set_power_state (ctx->skeleton, ctx->requested_power_state);
            g_task_return_boolean (task, TRUE);
        }
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

void
mm_iface_modem_set_power_state (MMIfaceModem        *self,
                                MMModemPowerState    power_state,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    SetPowerStateContext *ctx;
    GTask                *task;

    ctx = g_slice_new0 (SetPowerStateContext);
    ctx->step = SET_POWER_STATE_STEP_FIRST;
    ctx->requested_power_state = power_state;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)set_power_state_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }
    ctx->previous_cached_power_state = mm_gdbus_modem_get_power_state (ctx->skeleton);

    /* Setup requested operation */
    switch (ctx->requested_power_state) {
    case MM_MODEM_POWER_STATE_OFF:
        ctx->requested_power_setup = MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_off;
        ctx->requested_power_setup_finish = MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_off_finish;
        break;
    case MM_MODEM_POWER_STATE_LOW:
        ctx->requested_power_setup = MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down;
        ctx->requested_power_setup_finish = MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down_finish;
        break;
    case MM_MODEM_POWER_STATE_ON:
        ctx->requested_power_setup = MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up;
        ctx->requested_power_setup_finish = MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up_finish;
        break;
    case MM_MODEM_POWER_STATE_UNKNOWN:
    default:
        g_assert_not_reached ();
    }

    set_power_state_step (task);
}

/*****************************************************************************/
/* MODEM DISABLING */

gboolean
mm_iface_modem_disable_finish (MMIfaceModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_disable (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GTask *task;

    /* Just complete, nothing to do */
    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/
/* MODEM ENABLING */

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (GTask *task);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SET_POWER_STATE,
    ENABLING_STEP_CHECK_FOR_SIM_SWAP,
    ENABLING_STEP_FLOW_CONTROL,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModem *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_enable_finish (MMIfaceModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enabling_set_power_state_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    if (!mm_iface_modem_set_power_state_finish (self, res, &error)) {
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
check_for_sim_swap_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish (self, res, &error)) {
        mm_obj_warn (self, "failed to check if SIM was swapped: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
setup_flow_control_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_GET_INTERFACE (self)->setup_flow_control_finish (self, res, &error);
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

static const MMModemCharset best_charsets[] = {
    MM_MODEM_CHARSET_UTF8,
    MM_MODEM_CHARSET_UCS2,
    MM_MODEM_CHARSET_8859_1,
    MM_MODEM_CHARSET_IRA,
    MM_MODEM_CHARSET_GSM,
    MM_MODEM_CHARSET_UNKNOWN
};

static void
interface_enabling_step (GTask *task)
{
    MMIfaceModem *self;
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
        /* fall-through */

    case ENABLING_STEP_SET_POWER_STATE:
        mm_iface_modem_set_power_state (self,
                                        MM_MODEM_POWER_STATE_ON,
                                        (GAsyncReadyCallback)enabling_set_power_state_ready,
                                        task);
        return;

    case ENABLING_STEP_CHECK_FOR_SIM_SWAP:
        if (MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->check_for_sim_swap (
                self,
                NULL,
                (GAsyncReadyCallback)check_for_sim_swap_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case ENABLING_STEP_FLOW_CONTROL:
        if (MM_IFACE_MODEM_GET_INTERFACE (self)->setup_flow_control &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_flow_control_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_flow_control (
                self,
                (GAsyncReadyCallback)setup_flow_control_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

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
mm_iface_modem_enable (MMIfaceModem *self,
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
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
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
/* MODEM INITIALIZATION */

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CURRENT_CAPABILITIES,
    INITIALIZATION_STEP_SUPPORTED_CAPABILITIES,
    INITIALIZATION_STEP_SUPPORTED_CHARSETS,
    INITIALIZATION_STEP_CHARSET,
    INITIALIZATION_STEP_BEARERS,
    INITIALIZATION_STEP_MANUFACTURER,
    INITIALIZATION_STEP_MODEL,
    INITIALIZATION_STEP_REVISION,
    INITIALIZATION_STEP_CARRIER_CONFIG,
    INITIALIZATION_STEP_HARDWARE_REVISION,
    INITIALIZATION_STEP_EQUIPMENT_ID,
    INITIALIZATION_STEP_DEVICE_ID,
    INITIALIZATION_STEP_SUPPORTED_MODES,
    INITIALIZATION_STEP_SUPPORTED_BANDS,
    INITIALIZATION_STEP_SUPPORTED_IP_FAMILIES,
    INITIALIZATION_STEP_POWER_STATE,
    INITIALIZATION_STEP_SIM_HOT_SWAP,
    INITIALIZATION_STEP_SIM_SLOTS,
    INITIALIZATION_STEP_UNLOCK_REQUIRED,
    INITIALIZATION_STEP_SIM,
    INITIALIZATION_STEP_SETUP_CARRIER_CONFIG,
    INITIALIZATION_STEP_OWN_NUMBERS,
    INITIALIZATION_STEP_CURRENT_MODES,
    INITIALIZATION_STEP_CURRENT_BANDS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    InitializationStep step;
    MmGdbusModem *skeleton;
    MMModemCharset supported_charsets;
    const MMModemCharset *current_charset;
    GError *fatal_error;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_assert (ctx->fatal_error == NULL);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY)                                \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem *self,                            \
                         GAsyncResult *res,                             \
                         GTask *task)                                   \
    {                                                                   \
        InitializationContext *ctx;                                     \
        GError *error = NULL;                                           \
        gchar *val;                                                     \
                                                                        \
        ctx = g_task_get_task_data (task);                              \
                                                                        \
        val = MM_IFACE_MODEM_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_modem_set_##NAME (ctx->skeleton, val);                 \
        g_free (val);                                                   \
                                                                        \
        if (error) {                                                    \
            mm_obj_warn (self, "couldn't load %s: %s", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (task);                           \
    }

#undef UINT_REPLY_READY_FN
#define UINT_REPLY_READY_FN(NAME,DISPLAY)                               \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem *self,                            \
                         GAsyncResult *res,                             \
                         GTask *task)                                   \
    {                                                                   \
        InitializationContext *ctx;                                     \
        GError *error = NULL;                                           \
                                                                        \
        ctx = g_task_get_task_data (task);                              \
                                                                        \
        mm_gdbus_modem_set_##NAME (                                     \
            ctx->skeleton,                                              \
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error)); \
                                                                        \
        if (error) {                                                    \
            mm_obj_warn (self, "couldn't load %s: %s", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (task);                           \
    }

static void
current_capabilities_internal_load_unlock_required_ready (MMIfaceModem *self,
                                                          GAsyncResult *res,
                                                          GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    internal_load_unlock_required_finish (self, res, &error);
    if (error) {
        /* These SIM errors indicate that there is NO valid SIM available. So,
         * remove all 3GPP caps from the current capabilities */
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG)) {
            MMModemCapability caps;

            mm_obj_dbg (self, "multimode device without SIM, no 3GPP capabilities");
            caps = mm_gdbus_modem_get_current_capabilities (ctx->skeleton);
            caps &= ~MM_MODEM_CAPABILITY_3GPP;

            /* CDMA-EVDO must still be around */
            g_assert (caps & MM_MODEM_CAPABILITY_CDMA_EVDO);
            mm_gdbus_modem_set_current_capabilities (ctx->skeleton, caps);
        }

        g_error_free (error);
    }

    /* Keep on */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_current_capabilities_ready (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    InitializationContext *ctx;
    MMModemCapability caps;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    caps = MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_capabilities_finish (self, res, &error);
    if (error) {
        g_propagate_error (&ctx->fatal_error, error);
        g_prefix_error (&ctx->fatal_error, "couldn't load current capabilities: ");
        /* Jump to the last step */
        ctx->step = INITIALIZATION_STEP_LAST;
        interface_initialization_step (task);
        return;
    }

    /* By default CS/PS/CDMA1X/EVDO network registration checks are the only
     * ones enabled, so fix them up based on capabilities, enabling EPS or 5GS
     * checks if required, and disabling CS/PS/CDMA1X/EVDO if required. */
    if (caps & MM_MODEM_CAPABILITY_LTE) {
        mm_obj_dbg (self, "setting EPS network as supported");
        g_object_set (G_OBJECT (self),
                      MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, TRUE,
                      NULL);
    }
    if (caps & MM_MODEM_CAPABILITY_5GNR) {
        mm_obj_dbg (self, "setting 5GS network as supported");
        g_object_set (G_OBJECT (self),
                      MM_IFACE_MODEM_3GPP_5GS_NETWORK_SUPPORTED, TRUE,
                      NULL);
    }
    if (!(caps & MM_MODEM_CAPABILITY_CDMA_EVDO)) {
        mm_obj_dbg (self, "setting CDMA1x/EVDO networks as unsupported");
        g_object_set (G_OBJECT (self),
                      MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED, FALSE,
                      MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED,   FALSE,
                      NULL);
    }
    if (!(caps & MM_MODEM_CAPABILITY_GSM_UMTS)) {
        mm_obj_dbg (self, "setting CS/PS networks as unsupported");
        g_object_set (G_OBJECT (self),
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, FALSE,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, FALSE,
                      NULL);
    }

    /* Update current caps right away, even if we may fix them during the
     * multimode device check. No big deal in updating them twice, as we're not
     * exposed in DBus yet. */
    mm_gdbus_modem_set_current_capabilities (ctx->skeleton, caps);

    /* If the device is a multimode device (3GPP+3GPP2) check whether we have a
     * SIM or not. */
    if ((caps & MM_MODEM_CAPABILITY_CDMA_EVDO) && (caps & MM_MODEM_CAPABILITY_3GPP)) {
        mm_obj_dbg (self, "checking if multimode device has a SIM...");
        internal_load_unlock_required (
            self,
            (GAsyncReadyCallback)current_capabilities_internal_load_unlock_required_ready,
            task);
        return;
    }

    ctx->step++;
    interface_initialization_step (task);
}

static void
load_supported_capabilities_ready (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    InitializationContext *ctx;
    GArray *supported_capabilities;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    supported_capabilities = MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_capabilities_finish (self, res, &error);
    if (error) {
        g_propagate_error (&ctx->fatal_error, error);
        g_prefix_error (&ctx->fatal_error, "couldn't load supported capabilities: ");
        /* Jump to the last step */
        ctx->step = INITIALIZATION_STEP_LAST;
        interface_initialization_step (task);
        return;
    }

    /* Update supported caps */
    mm_gdbus_modem_set_supported_capabilities (ctx->skeleton,
                                               mm_common_capability_combinations_garray_to_variant (supported_capabilities));
    g_array_unref (supported_capabilities);

    ctx->step++;
    interface_initialization_step (task);
}

STR_REPLY_READY_FN (manufacturer, "manufacturer")
STR_REPLY_READY_FN (model, "model")
STR_REPLY_READY_FN (revision, "revision")
STR_REPLY_READY_FN (hardware_revision, "hardware revision")
STR_REPLY_READY_FN (equipment_identifier, "equipment identifier")
STR_REPLY_READY_FN (device_identifier, "device identifier")

static void
load_supported_charsets_ready (MMIfaceModem *self,
                               GAsyncResult *res,
                               GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->supported_charsets =
        MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_charsets_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load supported charsets: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
setup_charset_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->setup_charset_finish (self, res, &error)) {
        mm_obj_dbg (self, "couldn't set charset '%s': %s",
                    mm_modem_charset_to_string (*ctx->current_charset),
                    error->message);
        g_error_free (error);

        /* Will retry step with some other charset type */
    } else
        /* Done, Go on to next step */
        ctx->step++;

    interface_initialization_step (task);
}

static void
load_supported_modes_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;
    GArray *modes_array;

    ctx = g_task_get_task_data (task);

    modes_array = MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_modes_finish (self, res, &error);
    if (modes_array != NULL) {
        mm_gdbus_modem_set_supported_modes (ctx->skeleton,
                                            mm_common_mode_combinations_garray_to_variant (modes_array));
        g_array_unref (modes_array);
    }

    if (error) {
        mm_obj_warn (self, "couldn't load supported modes: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_supported_bands_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;
    GArray *bands_array;

    ctx = g_task_get_task_data (task);

    bands_array = MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_bands_finish (self, res, &error);
    if (bands_array) {
        mm_common_bands_garray_sort (bands_array);
        mm_gdbus_modem_set_supported_bands (ctx->skeleton,
                                            mm_common_bands_garray_to_variant (bands_array));
        g_array_unref (bands_array);
    }

    if (error) {
        mm_obj_warn (self, "couldn't load supported bands: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_supported_ip_families_ready (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;
    MMBearerIpFamily ip_families;

    ctx = g_task_get_task_data (task);

    ip_families = MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_ip_families_finish (self, res, &error);

    if (ip_families != MM_BEARER_IP_FAMILY_NONE)
        mm_gdbus_modem_set_supported_ip_families (ctx->skeleton, ip_families);

    if (error) {
        mm_obj_warn (self, "couldn't load supported IP families: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

UINT_REPLY_READY_FN (power_state, "power state")

static void
setup_sim_hot_swap_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GTask        *task)
{
    InitializationContext *ctx;
    g_autoptr(GError)      error = NULL;

    ctx = g_task_get_task_data (task);

    MM_IFACE_MODEM_GET_INTERFACE (self)->setup_sim_hot_swap_finish (self, res, &error);
    if (error)
        mm_obj_warn (self, "SIM hot swap setup failed: %s", error->message);
    else {
        mm_obj_dbg (self, "SIM hot swap setup succeeded");
        g_object_set (self,
                      MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, TRUE,
                      NULL);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_sim_slots_ready (MMIfaceModem *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    InitializationContext *ctx;
    g_autoptr(GPtrArray)   sim_slots = NULL;
    g_autoptr(GError)      error = NULL;
    guint                  primary_sim_slot = 0;

    ctx  = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_sim_slots_finish (self,
                                                                     res,
                                                                     &sim_slots,
                                                                     &primary_sim_slot,
                                                                     &error))
        mm_obj_warn (self, "couldn't query SIM slots: %s", error->message);

    if (sim_slots) {
        MMBaseSim     *primary_sim = NULL;
        GPtrArray     *sim_slot_paths_array;
        g_auto(GStrv)  sim_slot_paths = NULL;
        guint          i;

        g_assert (primary_sim_slot);
        g_assert_cmpuint (primary_sim_slot, <=, sim_slots->len);

        sim_slot_paths_array = g_ptr_array_new ();
        for (i = 0; i < sim_slots->len; i++) {
            MMBaseSim   *sim;
            const gchar *sim_path;

            sim = MM_BASE_SIM (g_ptr_array_index (sim_slots, i));
            if (!sim) {
                g_ptr_array_add (sim_slot_paths_array, g_strdup ("/"));
                continue;
            }

            sim_path = mm_base_sim_get_path (sim);
            g_ptr_array_add (sim_slot_paths_array, g_strdup (sim_path));
        }
        g_ptr_array_add (sim_slot_paths_array, NULL);
        sim_slot_paths = (GStrv) g_ptr_array_free (sim_slot_paths_array, FALSE);

        mm_gdbus_modem_set_sim_slots (ctx->skeleton, (const gchar *const *)sim_slot_paths);
        mm_gdbus_modem_set_primary_sim_slot (ctx->skeleton, primary_sim_slot);

        /* If loading SIM slots is supported, we also expose already the primary active SIM object */
        if (primary_sim_slot) {
            primary_sim = g_ptr_array_index (sim_slots, primary_sim_slot - 1);
            if (primary_sim)
                g_object_bind_property (primary_sim, MM_BASE_SIM_PATH,
                                        ctx->skeleton, "sim",
                                        G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        }
        g_object_set (self,
                      MM_IFACE_MODEM_SIM,       primary_sim,
                      MM_IFACE_MODEM_SIM_SLOTS, sim_slots,
                      NULL);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
modem_update_lock_info_ready (MMIfaceModem *self,
                              GAsyncResult *res,
                              GTask *task)
{
    InitializationContext *ctx;

    ctx = g_task_get_task_data (task);

    /* NOTE: we already propagated the lock state, no need to do it again */
    mm_iface_modem_update_lock_info_finish (self, res, &ctx->fatal_error);
    if (ctx->fatal_error) {
        g_prefix_error (&ctx->fatal_error,
                        "Couldn't check unlock status: ");
        /* Jump to the last step */
        ctx->step = INITIALIZATION_STEP_LAST;
    } else
        /* Go on to next step */
        ctx->step++;

    interface_initialization_step (task);
}

static void
sim_new_ready (GAsyncInitable *initable,
               GAsyncResult *res,
               GTask *task)
{
    MMIfaceModem *self;
    InitializationContext *ctx;
    MMBaseSim *sim;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    sim = MM_IFACE_MODEM_GET_INTERFACE (self)->create_sim_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't create SIM: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* We may get error with !sim, when the implementation doesn't want to
     * handle any (e.g. CDMA) */
    if (sim) {
        g_object_bind_property (sim, MM_BASE_SIM_PATH,
                                ctx->skeleton, "sim",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_SIM, sim,
                      NULL);
        g_object_unref (sim);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
sim_reinit_ready (MMBaseSim *sim,
                  GAsyncResult *res,
                  GTask *task)
{
    MMIfaceModem          *self;
    InitializationContext *ctx;
    GError                *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_base_sim_initialize_finish (sim, res, &error)) {
        mm_obj_warn (self, "SIM re-initialization failed: %s",
                     error ? error->message : "Unknown error");
        g_clear_error (&error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
setup_carrier_config_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    InitializationContext *ctx;
    GError                *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->setup_carrier_config_finish (self, res, &error)) {
        mm_obj_warn (self, "couldn't setup carrier config: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_carrier_config_ready (MMIfaceModem *self,
                           GAsyncResult *res,
                           GTask        *task)
{
    InitializationContext *ctx;
    GError                *error = NULL;
    gchar                 *name = NULL;
    gchar                 *revision = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_carrier_config_finish (self, res, &name, &revision, &error)) {
        mm_obj_warn (self, "couldn't load carrier config: %s", error->message);
        g_error_free (error);
    } else {
        mm_gdbus_modem_set_carrier_configuration          (ctx->skeleton, name);
        mm_gdbus_modem_set_carrier_configuration_revision (ctx->skeleton, revision);
        g_free (name);
        g_free (revision);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

void
mm_iface_modem_update_own_numbers (MMIfaceModem *self,
                                   const GStrv own_numbers)
{
    MmGdbusModem *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (skeleton) {
        mm_gdbus_modem_set_own_numbers (skeleton, (const gchar * const *)own_numbers);
        g_object_unref (skeleton);
    }
}

static void
load_own_numbers_ready (MMIfaceModem *self,
                        GAsyncResult *res,
                        GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;
    GStrv str_list;

    ctx = g_task_get_task_data (task);

    str_list = MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load list of own numbers: %s", error->message);
        g_error_free (error);
    }

    if (str_list) {
        mm_gdbus_modem_set_own_numbers (ctx->skeleton, (const gchar *const *) str_list);
        g_strfreev (str_list);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_current_modes_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GTask *task)
{
    InitializationContext *ctx;
    MMModemMode allowed = MM_MODEM_MODE_NONE;
    MMModemMode preferred = MM_MODEM_MODE_NONE;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_modes_finish (self,
                                                                         res,
                                                                         &allowed,
                                                                         &preferred,
                                                                         &error)) {
        /* Errors when getting allowed/preferred won't be critical */
        mm_obj_warn (self, "couldn't load current allowed/preferred modes: %s", error->message);
        g_error_free (error);
    } else
        mm_gdbus_modem_set_current_modes (ctx->skeleton, g_variant_new ("(uu)", allowed, preferred));

    /* Done, Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_current_bands_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          GTask *task)
{
    InitializationContext *ctx;
    GArray *current_bands;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    current_bands = MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands_finish (self, res, &error);
    if (!current_bands) {
        /* Errors when getting current bands won't be critical */
        mm_obj_warn (self, "couldn't load current bands: %s", error->message);
        g_error_free (error);
    } else {
        GArray *filtered_bands;
        GArray *supported_bands;

        supported_bands = (mm_common_bands_variant_to_garray (
                               mm_gdbus_modem_get_supported_bands (ctx->skeleton)));
        filtered_bands = mm_filter_current_bands (supported_bands, current_bands);

        g_array_unref (current_bands);
        if (supported_bands)
            g_array_unref (supported_bands);

        if (filtered_bands) {
            mm_common_bands_garray_sort (filtered_bands);
            mm_gdbus_modem_set_current_bands (ctx->skeleton,
                                              mm_common_bands_garray_to_variant (filtered_bands));
            g_array_unref (filtered_bands);
        }
    }

    /* Done, Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModem *self;
    InitializationContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        /* Simply ignore any fatal error encountered as the initialization is cancelled anyway. */
        if (ctx->fatal_error) {
            g_error_free (ctx->fatal_error);
            ctx->fatal_error = NULL;
        }
        g_object_unref (task);
        return;
    }

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Load device if not done before */
        if (!mm_gdbus_modem_get_device (ctx->skeleton)) {
            gchar *device;

            g_object_get (self,
                          MM_BASE_MODEM_DEVICE, &device,
                          NULL);
            mm_gdbus_modem_set_device (ctx->skeleton, device);
            g_free (device);
        }
        /* Load driver if not done before */
        if (!mm_gdbus_modem_get_drivers (ctx->skeleton)) {
            gchar **drivers;

            g_object_get (self,
                          MM_BASE_MODEM_DRIVERS, &drivers,
                          NULL);
            mm_gdbus_modem_set_drivers (ctx->skeleton, (const gchar * const *)drivers);
            g_strfreev (drivers);
        }
        /* Load plugin if not done before */
        if (!mm_gdbus_modem_get_plugin (ctx->skeleton)) {
            gchar *plugin;

            g_object_get (self,
                          MM_BASE_MODEM_PLUGIN, &plugin,
                          NULL);
            mm_gdbus_modem_set_plugin (ctx->skeleton, plugin);
            g_free (plugin);
        }
        /* Load primary port if not done before */
        if (!mm_gdbus_modem_get_primary_port (ctx->skeleton)) {
            MMPort *primary = NULL;

#if defined WITH_QMI
            if (MM_IS_BROADBAND_MODEM_QMI (self))
                primary = MM_PORT (mm_broadband_modem_qmi_peek_port_qmi (MM_BROADBAND_MODEM_QMI (self)));
#endif
#if defined WITH_MBIM
            if (!primary && MM_IS_BROADBAND_MODEM_MBIM (self))
                primary = MM_PORT (mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (self)));
#endif
            if (!primary)
                primary = MM_PORT (mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)));

            g_assert (primary != NULL);
            mm_gdbus_modem_set_primary_port (ctx->skeleton, mm_port_get_device (primary));
        }
        /* Load ports if not done before */
        if (!mm_gdbus_modem_get_ports (ctx->skeleton)) {
            MMModemPortInfo *port_infos;
            guint n_port_infos;

            port_infos = mm_base_modem_get_port_infos (MM_BASE_MODEM (self), &n_port_infos);
            mm_gdbus_modem_set_ports (ctx->skeleton, mm_common_ports_array_to_variant (port_infos, n_port_infos));
            mm_modem_port_info_array_free (port_infos, n_port_infos);
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_CURRENT_CAPABILITIES:
        /* Current capabilities may change during runtime, i.e. if new firmware reloaded; but we'll
         * try to handle that by making sure the capabilities are cleared when the new firmware is
         * reloaded. So if we're asked to re-initialize, if we already have current capabilities loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_current_capabilities (ctx->skeleton) == MM_MODEM_CAPABILITY_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_capabilities &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_capabilities_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_capabilities (
                self,
                (GAsyncReadyCallback)load_current_capabilities_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_SUPPORTED_CAPABILITIES: {
        GArray *supported_capabilities;

        supported_capabilities = (mm_common_capability_combinations_variant_to_garray (
                                      mm_gdbus_modem_get_supported_capabilities (ctx->skeleton)));

        /* Supported capabilities are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (supported_capabilities->len == 0 ||
            g_array_index (supported_capabilities, MMModemCapability, 0) == MM_MODEM_CAPABILITY_NONE) {
            MMModemCapability current;

            if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_capabilities &&
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_capabilities_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_capabilities (
                    self,
                    (GAsyncReadyCallback)load_supported_capabilities_ready,
                    task);
                g_array_unref (supported_capabilities);
                return;
            }

            /* If no specific way of getting modem capabilities, default to the current ones */
            g_array_unref (supported_capabilities);
            supported_capabilities = g_array_sized_new (FALSE, FALSE, sizeof (MMModemCapability), 1);
            current = mm_gdbus_modem_get_current_capabilities (ctx->skeleton);
            g_array_append_val (supported_capabilities, current);
            mm_gdbus_modem_set_supported_capabilities (
                ctx->skeleton,
                mm_common_capability_combinations_garray_to_variant (supported_capabilities));
        }
        g_array_unref (supported_capabilities);

        ctx->step++;
    } /* fall-through */

    case INITIALIZATION_STEP_SUPPORTED_CHARSETS:
        if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_charsets &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_charsets_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_charsets (
                self,
                (GAsyncReadyCallback)load_supported_charsets_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_CHARSET:
        /* Only try to set charsets if we were able to load supported ones */
        if (ctx->supported_charsets > 0 &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_charset &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_charset_finish) {
            gboolean next_to_try = FALSE;

            while (!next_to_try) {
                if (!ctx->current_charset)
                    /* Switch the device's charset; we prefer UTF-8, but UCS2 will do too */
                    ctx->current_charset = &best_charsets[0];
                else
                    /* Try with the next one */
                    ctx->current_charset++;

                if (*ctx->current_charset == MM_MODEM_CHARSET_UNKNOWN)
                    break;

                if (ctx->supported_charsets & (*ctx->current_charset))
                    next_to_try = TRUE;
            }

            if (next_to_try) {
                MM_IFACE_MODEM_GET_INTERFACE (self)->setup_charset (
                    self,
                    *ctx->current_charset,
                    (GAsyncReadyCallback)setup_charset_ready,
                    task);
                return;
            }

            mm_obj_warn (self, "Failed to find usable modem character set, let it to UNKNOWN");
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_BEARERS: {
        MMBearerList *list = NULL;

        /* Bearers setup is meant to be loaded only once during the whole
         * lifetime of the modem. The list may have been created by the object
         * implementing the interface; if so use it. */
        g_object_get (self,
                      MM_IFACE_MODEM_BEARER_LIST, &list,
                      NULL);

        if (!list) {
            guint n;

            /* The maximum number of available/connected modems is guessed from
             * the size of the data ports list. */
            n = g_list_length (mm_base_modem_peek_data_ports (MM_BASE_MODEM (self)));
            mm_obj_dbg (self, "allowed up to %u bearers", n);

            /* Create new default list */
            list = mm_bearer_list_new (n, n);
            g_signal_connect (list,
                              "notify::" MM_BEARER_LIST_NUM_BEARERS,
                              G_CALLBACK (bearer_list_updated),
                              self);
            g_object_set (self,
                          MM_IFACE_MODEM_BEARER_LIST, list,
                          NULL);
        }

        if (mm_gdbus_modem_get_max_bearers (ctx->skeleton) == 0)
            mm_gdbus_modem_set_max_bearers (
                ctx->skeleton,
                mm_bearer_list_get_max (list));
        if (mm_gdbus_modem_get_max_active_bearers (ctx->skeleton) == 0)
            mm_gdbus_modem_set_max_active_bearers (
                ctx->skeleton,
                mm_bearer_list_get_max_active (list));
        g_object_unref (list);

        ctx->step++;
    } /* fall-through */

    case INITIALIZATION_STEP_MANUFACTURER:
        /* Manufacturer is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_manufacturer (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_manufacturer &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_manufacturer_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_manufacturer (
                self,
                (GAsyncReadyCallback)load_manufacturer_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_MODEL:
        /* Model is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_model (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_model &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_model_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_model (
                self,
                (GAsyncReadyCallback)load_model_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_REVISION:
        /* Revision is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_revision (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_revision &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_revision_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_revision (
                self,
                (GAsyncReadyCallback)load_revision_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_CARRIER_CONFIG:
        /* Current carrier config is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_carrier_configuration (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_carrier_config &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_carrier_config_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_carrier_config (self,
                                                                      (GAsyncReadyCallback)load_carrier_config_ready,
                                                                      task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_HARDWARE_REVISION:
        /* HardwareRevision is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_hardware_revision (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_hardware_revision &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_hardware_revision_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_hardware_revision (
                self,
                (GAsyncReadyCallback)load_hardware_revision_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_EQUIPMENT_ID:
        /* Equipment ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_equipment_identifier (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_equipment_identifier &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_equipment_identifier_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_equipment_identifier (
                self,
                (GAsyncReadyCallback)load_equipment_identifier_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_DEVICE_ID:
        /* Device ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_device_identifier (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_device_identifier &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_device_identifier_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_device_identifier (
                self,
                (GAsyncReadyCallback)load_device_identifier_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_SUPPORTED_MODES:
        if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_modes != NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_modes_finish != NULL) {
            GArray *supported_modes;
            MMModemModeCombination *mode = NULL;

            supported_modes = (mm_common_mode_combinations_variant_to_garray (
                                   mm_gdbus_modem_get_supported_modes (ctx->skeleton)));

            /* Supported modes are meant to be loaded only once during the whole
             * lifetime of the modem. Therefore, if we already have them loaded,
             * don't try to load them again. */
            if (supported_modes->len == 1)
                mode = &g_array_index (supported_modes, MMModemModeCombination, 0);
            if (supported_modes->len == 0 ||
                (mode && mode->allowed == MM_MODEM_MODE_ANY && mode->preferred == MM_MODEM_MODE_NONE)) {
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_modes (
                    self,
                    (GAsyncReadyCallback)load_supported_modes_ready,
                    task);
                g_array_unref (supported_modes);
                return;
            }

            g_array_unref (supported_modes);
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_SUPPORTED_BANDS: {
        GArray *supported_bands;

        supported_bands = (mm_common_bands_variant_to_garray (
                               mm_gdbus_modem_get_supported_bands (ctx->skeleton)));

        /* Supported bands are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (supported_bands->len == 0 ||
            g_array_index (supported_bands, MMModemBand, 0)  == MM_MODEM_BAND_UNKNOWN) {
            if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_bands &&
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_bands_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_bands (
                    self,
                    (GAsyncReadyCallback)load_supported_bands_ready,
                    task);
                g_array_unref (supported_bands);
                return;
            }

            /* Loading supported bands not implemented, default to UNKNOWN */
            mm_gdbus_modem_set_supported_bands (ctx->skeleton, mm_common_build_bands_unknown ());
            mm_gdbus_modem_set_current_bands (ctx->skeleton, mm_common_build_bands_unknown ());
        }
        g_array_unref (supported_bands);

        ctx->step++;
    } /* fall-through */

    case INITIALIZATION_STEP_SUPPORTED_IP_FAMILIES:
        /* Supported ip_families are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_ip_families != NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_ip_families_finish != NULL &&
            mm_gdbus_modem_get_supported_ip_families (ctx->skeleton) == MM_BEARER_IP_FAMILY_NONE) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_ip_families (
                self,
                (GAsyncReadyCallback)load_supported_ip_families_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_POWER_STATE:
        /* Initial power state is meant to be loaded only once. Therefore, if we
         * already have it loaded, don't try to load it again. */
        if (mm_gdbus_modem_get_power_state (ctx->skeleton) == MM_MODEM_POWER_STATE_UNKNOWN) {
            if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state &&
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_power_state (
                    self,
                    (GAsyncReadyCallback)load_power_state_ready,
                    task);
                return;
            }

            /* We don't know how to load current power state; assume ON */
            mm_gdbus_modem_set_power_state (ctx->skeleton, MM_MODEM_POWER_STATE_ON);
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_SIM_HOT_SWAP: {
        gboolean sim_hot_swap_configured = FALSE;

        g_object_get (self,
                      MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, &sim_hot_swap_configured,
                      NULL);
        if (!sim_hot_swap_configured &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_sim_hot_swap &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_sim_hot_swap_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_sim_hot_swap (
                MM_IFACE_MODEM (self),
                (GAsyncReadyCallback) setup_sim_hot_swap_ready,
                task);
                return;
        }
        ctx->step++;
    } /* fall-through */

        case INITIALIZATION_STEP_SIM_SLOTS:
        /* If the modem doesn't need any SIM (not implemented by plugin, or not
         * needed in CDMA-only modems), or if we don't know how to query
         * for SIM slots */
        if (!mm_gdbus_modem_get_sim_slots (ctx->skeleton) &&
            !mm_iface_modem_is_cdma_only (self) &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_sim_slots &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_sim_slots_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_sim_slots (MM_IFACE_MODEM (self),
                                                                 (GAsyncReadyCallback)load_sim_slots_ready,
                                                                 task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_UNLOCK_REQUIRED:
        /* Only check unlock required if we were previously not unlocked */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE) {
            mm_iface_modem_update_lock_info (self,
                                             MM_MODEM_LOCK_UNKNOWN, /* ask */
                                             (GAsyncReadyCallback)modem_update_lock_info_ready,
                                             task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_SIM:
        /* If the modem doesn't need any SIM (not implemented by plugin, or not
         * needed in CDMA-only modems) */
        if (!mm_iface_modem_is_cdma_only (self) &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->create_sim &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->create_sim_finish) {
            MMBaseSim *sim = NULL;

            g_object_get (self,
                          MM_IFACE_MODEM_SIM, &sim,
                          NULL);
            if (!sim) {
                MM_IFACE_MODEM_GET_INTERFACE (self)->create_sim (
                    MM_IFACE_MODEM (self),
                    (GAsyncReadyCallback)sim_new_ready,
                    task);
                return;
            }

            /* If already available the sim object, relaunch initialization.
             * This will try to load any missing property value that couldn't be
             * retrieved before due to having the SIM locked. */
            mm_base_sim_initialize (sim,
                                    g_task_get_cancellable (task),
                                    (GAsyncReadyCallback)sim_reinit_ready,
                                    task);
            g_object_unref (sim);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_SETUP_CARRIER_CONFIG:
        /* Setup and perform automatic carrier config switching as soon as the
         * SIM initialization has been performed, only applicable if there is
         * actually a SIM found with a valid IMSI read */
        if (!mm_iface_modem_is_cdma_only (self) &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_carrier_config &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->setup_carrier_config_finish) {
            MMBaseSim *sim = NULL;
            gchar     *carrier_config_mapping = NULL;

            g_object_get (self,
                          MM_IFACE_MODEM_SIM, &sim,
                          MM_IFACE_MODEM_CARRIER_CONFIG_MAPPING, &carrier_config_mapping,
                          NULL);

            /* If we have a SIM object, and carrier config switching is supported,
             * validate whether we're already using the best config or not. */
            if (!sim)
                mm_obj_dbg (self, "not setting up carrier config: SIM not found");
            else if (!carrier_config_mapping)
                mm_obj_dbg (self, "not setting up carrier config: mapping file not configured");
            else {
                const gchar *imsi;

                imsi = mm_gdbus_sim_get_imsi (MM_GDBUS_SIM (sim));
                if (imsi) {
                    MM_IFACE_MODEM_GET_INTERFACE (self)->setup_carrier_config (self,
                                                                               imsi,
                                                                               carrier_config_mapping,
                                                                               (GAsyncReadyCallback)setup_carrier_config_ready,
                                                                               task);
                    g_object_unref (sim);
                    g_free (carrier_config_mapping);
                    return;
                }
                mm_obj_warn (self, "couldn't setup carrier config: unknown IMSI");
            }
            g_clear_object (&sim);
            g_free (carrier_config_mapping);
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_OWN_NUMBERS:
        /* Own numbers is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_own_numbers (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers &&
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers (
                self,
                (GAsyncReadyCallback)load_own_numbers_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall-through */

    case INITIALIZATION_STEP_CURRENT_MODES: {
        MMModemMode allowed = MM_MODEM_MODE_ANY;
        MMModemMode preferred = MM_MODEM_MODE_NONE;
        GVariant *aux;

        aux = mm_gdbus_modem_get_current_modes (ctx->skeleton);
        if (aux)
            g_variant_get (aux, "(uu)", &allowed, &preferred);

        /* Current modes are only meant to be loaded once, so if we have them
         * loaded already, just skip re-loading */
        if (allowed == MM_MODEM_MODE_ANY && preferred == MM_MODEM_MODE_NONE) {
            GArray *supported;

            supported = (mm_common_mode_combinations_variant_to_garray (
                             mm_gdbus_modem_get_supported_modes (ctx->skeleton)));

            /* If there is only one item in the list of supported modes, we're done */
            if (supported && supported->len == 1) {
                MMModemModeCombination *supported_mode;

                supported_mode = &g_array_index (supported, MMModemModeCombination, 0);
                mm_gdbus_modem_set_current_modes (ctx->skeleton, g_variant_new ("(uu)", supported_mode->allowed, supported_mode->preferred));
            } else if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_modes &&
                       MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_modes_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_modes (
                    self,
                    (GAsyncReadyCallback)load_current_modes_ready,
                    task);
                if (supported)
                    g_array_unref (supported);
                return;
            }

            if (supported)
                g_array_unref (supported);
        }

        ctx->step++;
    } /* fall-through */

    case INITIALIZATION_STEP_CURRENT_BANDS: {
        GArray *current;

        current = (mm_common_bands_variant_to_garray (
                       mm_gdbus_modem_get_current_bands (ctx->skeleton)));

        /* Current bands are only meant to be loaded once, so if we have them
         * loaded already, just skip re-loading */
        if (!current || (current->len == 1 && g_array_index (current, MMModemBand, 0) == MM_MODEM_BAND_UNKNOWN)) {
            if (MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands &&
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands (
                    self,
                    (GAsyncReadyCallback)load_current_bands_ready,
                    task);
                if (current)
                    g_array_unref (current);
                return;
            }

            /* If no way to get current bands, default to what supported has */
            mm_gdbus_modem_set_current_bands (ctx->skeleton, mm_gdbus_modem_get_supported_bands (ctx->skeleton));
        }

        if (current)
            g_array_unref (current);

        ctx->step++;
    } /* fall-through */

    case INITIALIZATION_STEP_LAST:
        /* Setup all method handlers */
        g_object_connect (ctx->skeleton,
                          "signal::handle-set-current-capabilities", G_CALLBACK (handle_set_current_capabilities), self,
                          "signal::handle-set-power-state",          G_CALLBACK (handle_set_power_state),          self,
                          "signal::handle-reset",                    G_CALLBACK (handle_reset),                    self,
                          "signal::handle-factory-reset",            G_CALLBACK (handle_factory_reset),            self,
                          "signal::handle-create-bearer",            G_CALLBACK (handle_create_bearer),            self,
                          "signal::handle-command",                  G_CALLBACK (handle_command),                  self,
                          "signal::handle-delete-bearer",            G_CALLBACK (handle_delete_bearer),            self,
                          "signal::handle-list-bearers",             G_CALLBACK (handle_list_bearers),             self,
                          "signal::handle-enable",                   G_CALLBACK (handle_enable),                   self,
                          "signal::handle-set-current-bands",        G_CALLBACK (handle_set_current_bands),        self,
                          "signal::handle-set-current-modes",        G_CALLBACK (handle_set_current_modes),        self,
                          "signal::handle-set-primary-sim-slot",     G_CALLBACK (handle_set_primary_sim_slot),     self,
                          NULL);

        /* Finally, export the new interface, even if we got errors, but only if not
         * done already */
        if (!mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (self)))
            mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self),
                                                MM_GDBUS_MODEM (ctx->skeleton));

        if (ctx->fatal_error) {
            g_task_return_error (task, ctx->fatal_error);
            ctx->fatal_error = NULL;
        } else
            g_task_return_boolean (task, TRUE);

        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_initialize_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_initialize (MMIfaceModem *self,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModem *skeleton = NULL;
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem_set_sim (skeleton, NULL);
        mm_gdbus_modem_set_supported_capabilities (skeleton, mm_common_build_capability_combinations_none ());
        mm_gdbus_modem_set_current_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
        mm_gdbus_modem_set_max_bearers (skeleton, 0);
        mm_gdbus_modem_set_max_active_bearers (skeleton, 0);
        mm_gdbus_modem_set_manufacturer (skeleton, NULL);
        mm_gdbus_modem_set_model (skeleton, NULL);
        mm_gdbus_modem_set_revision (skeleton, NULL);
        mm_gdbus_modem_set_own_numbers (skeleton, NULL);
        mm_gdbus_modem_set_device_identifier (skeleton, NULL);
        mm_gdbus_modem_set_device (skeleton, NULL);
        mm_gdbus_modem_set_drivers (skeleton, NULL);
        mm_gdbus_modem_set_plugin (skeleton, NULL);
        mm_gdbus_modem_set_equipment_identifier (skeleton, NULL);
        mm_gdbus_modem_set_unlock_required (skeleton, MM_MODEM_LOCK_UNKNOWN);
        mm_gdbus_modem_set_unlock_retries (skeleton, 0);
        mm_gdbus_modem_set_access_technologies (skeleton, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_gdbus_modem_set_signal_quality (skeleton, g_variant_new ("(ub)", 0, FALSE));
        mm_gdbus_modem_set_supported_modes (skeleton, mm_common_build_mode_combinations_default ());
        mm_gdbus_modem_set_current_modes (skeleton, g_variant_new ("(uu)", MM_MODEM_MODE_ANY, MM_MODEM_MODE_NONE));
        mm_gdbus_modem_set_supported_bands (skeleton, mm_common_build_bands_unknown ());
        mm_gdbus_modem_set_current_bands (skeleton, mm_common_build_bands_unknown ());
        mm_gdbus_modem_set_supported_ip_families (skeleton, MM_BEARER_IP_FAMILY_NONE);
        mm_gdbus_modem_set_power_state (skeleton, MM_MODEM_POWER_STATE_UNKNOWN);
        mm_gdbus_modem_set_state_failed_reason (skeleton, MM_MODEM_STATE_FAILED_REASON_NONE);

        /* Bind our State property */
        g_object_bind_property (self, MM_IFACE_MODEM_STATE,
                                skeleton, "state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */
    ctx = g_new0 (InitializationContext, 1);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    interface_initialization_step (task);
}

void
mm_iface_modem_shutdown (MMIfaceModem *self)
{
    /* Make sure signal polling is disabled. No real need to clear values, as
     * we're shutting down the interface anyway. */
    periodic_signal_check_disable (self, FALSE);

    /* Remove SignalQualityUpdateContext object to make sure any pending
     * invocation of expire_signal_quality is canceled before the DBus skeleton
     * is removed. */
    if (G_LIKELY (signal_quality_update_context_quark))
        g_object_set_qdata (G_OBJECT (self),
                            signal_quality_update_context_quark,
                            NULL);

    /* Remove running restart initialization idle, if any */
    if (G_LIKELY (restart_initialize_idle_quark))
        g_object_set_qdata (G_OBJECT (self),
                            restart_initialize_idle_quark,
                            NULL);

    /* Remove SIM object */
    g_object_set (self,
                  MM_IFACE_MODEM_SIM, NULL,
                  NULL);
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

MMModemAccessTechnology
mm_iface_modem_get_access_technologies (MMIfaceModem *self)
{
    MMModemAccessTechnology access_tech = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    if (skeleton) {
        access_tech = mm_gdbus_modem_get_access_technologies (skeleton);
        g_object_unref (skeleton);
    }

    return access_tech;
}

/*****************************************************************************/

static gboolean
find_supported_mode (MMIfaceModem *self,
                     MMModemMode mode,
                     gboolean *only)
{
    gboolean matched = FALSE;
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    if (skeleton) {
        GArray *supported;
        guint i;
        guint n_unmatched = 0;

        supported = mm_common_mode_combinations_variant_to_garray (
            mm_gdbus_modem_get_supported_modes (skeleton));

        /* Check if the given mode is supported */
        for (i = 0; i < supported->len; i++) {
            MMModemModeCombination *supported_mode;

            supported_mode = &g_array_index (supported, MMModemModeCombination, i);
            if (supported_mode->allowed & mode) {
                matched = TRUE;
                if (supported_mode->allowed != mode)
                    n_unmatched++;
            } else
                n_unmatched++;

            if (matched && (only == NULL || n_unmatched > 0))
                break;
        }

        if (only)
            *only = (n_unmatched == 0);

        g_array_unref (supported);
        g_object_unref (skeleton);
    }

    return matched;
}

gboolean
mm_iface_modem_is_2g (MMIfaceModem *self)
{
    return find_supported_mode (self, MM_MODEM_MODE_2G, NULL);
}

gboolean
mm_iface_modem_is_2g_only (MMIfaceModem *self)
{
    gboolean only;

    return (find_supported_mode (self, MM_MODEM_MODE_2G, &only) ?
            only :
            FALSE);
}

gboolean
mm_iface_modem_is_3g (MMIfaceModem *self)
{
    return find_supported_mode (self, MM_MODEM_MODE_3G, NULL);
}

gboolean
mm_iface_modem_is_3g_only (MMIfaceModem *self)
{
    gboolean only;

    return (find_supported_mode (self, MM_MODEM_MODE_3G, &only) ?
            only :
            FALSE);
}

gboolean
mm_iface_modem_is_4g (MMIfaceModem *self)
{
    return find_supported_mode (self, MM_MODEM_MODE_4G, NULL);
}

gboolean
mm_iface_modem_is_4g_only (MMIfaceModem *self)
{
    gboolean only;

    return (find_supported_mode (self, MM_MODEM_MODE_4G, &only) ?
            only :
            FALSE);
}

gboolean
mm_iface_modem_is_5g (MMIfaceModem *self)
{
    return find_supported_mode (self, MM_MODEM_MODE_5G, NULL);
}

gboolean
mm_iface_modem_is_5g_only (MMIfaceModem *self)
{
    gboolean only;

    return (find_supported_mode (self, MM_MODEM_MODE_5G, &only) ?
            only :
            FALSE);
}

/*****************************************************************************/

MMModemCapability
mm_iface_modem_get_current_capabilities (MMIfaceModem *self)
{
    MMModemCapability current = MM_MODEM_CAPABILITY_NONE;
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    if (skeleton) {
        current = mm_gdbus_modem_get_current_capabilities (skeleton);
        g_object_unref (skeleton);
    }

    return current;
}

gboolean
mm_iface_modem_is_3gpp (MMIfaceModem *self)
{
    return (mm_iface_modem_get_current_capabilities (self) & MM_MODEM_CAPABILITY_3GPP);
}

gboolean
mm_iface_modem_is_3gpp_lte (MMIfaceModem *self)
{
    return (mm_iface_modem_get_current_capabilities (self) & MM_MODEM_CAPABILITY_LTE);
}

gboolean
mm_iface_modem_is_3gpp_5gnr (MMIfaceModem *self)
{
    return (mm_iface_modem_get_current_capabilities (self) & MM_MODEM_CAPABILITY_5GNR);
}

gboolean
mm_iface_modem_is_cdma (MMIfaceModem *self)
{
    return (mm_iface_modem_get_current_capabilities (self) & MM_MODEM_CAPABILITY_CDMA_EVDO);
}

gboolean
mm_iface_modem_is_3gpp_only (MMIfaceModem *self)
{
    MMModemCapability capabilities;

    capabilities = mm_iface_modem_get_current_capabilities (self);
    return (capabilities & MM_MODEM_CAPABILITY_3GPP) && !((MM_MODEM_CAPABILITY_3GPP ^ capabilities) & capabilities);
}

gboolean
mm_iface_modem_is_cdma_only (MMIfaceModem *self)
{
    return (mm_iface_modem_get_current_capabilities (self) == MM_MODEM_CAPABILITY_CDMA_EVDO);
}

/*****************************************************************************/

const gchar *
mm_iface_modem_get_model (MMIfaceModem *self)
{
    const gchar *model = NULL;
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    if (skeleton) {
        model = mm_gdbus_modem_get_model (skeleton);
        g_object_unref (skeleton);
    }

    return model;
}

const gchar *
mm_iface_modem_get_revision (MMIfaceModem *self)
{
    const gchar *revision = NULL;
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    if (skeleton) {
        revision = mm_gdbus_modem_get_revision (skeleton);
        g_object_unref (skeleton);
    }

    return revision;
}

gboolean
mm_iface_modem_get_carrier_config (MMIfaceModem  *self,
                                   const gchar  **name,
                                   const gchar  **revision)
{
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return FALSE;

    if (name)
        *name = mm_gdbus_modem_get_carrier_configuration (skeleton);
    if (revision)
        *revision = mm_gdbus_modem_get_carrier_configuration_revision (skeleton);
    g_object_unref (skeleton);
    return TRUE;
}

/*****************************************************************************/

static void
iface_modem_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_DBUS_SKELETON,
                              "Modem DBus skeleton",
                              "DBus skeleton for the Modem interface",
                              MM_GDBUS_TYPE_MODEM_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_SIM,
                              "SIM",
                              "SIM object",
                              MM_TYPE_BASE_SIM,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boxed (MM_IFACE_MODEM_SIM_SLOTS,
                             "SIM slots",
                             "SIM objects in SIM slots",
                             MM_TYPE_OBJECT_ARRAY,
                             G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_enum (MM_IFACE_MODEM_STATE,
                            "State",
                            "State of the modem",
                            MM_TYPE_MODEM_STATE,
                            MM_MODEM_STATE_UNKNOWN,
                            G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_BEARER_LIST,
                              "Bearer list",
                              "List of bearers handled by the modem",
                              MM_TYPE_BEARER_LIST,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED,
                               "Sim Hot Swap Supported",
                               "Whether the modem supports sim hot swap or not.",
                               FALSE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED,
                               "Sim Hot Swap Configured",
                               "Whether the sim hot swap support is configured correctly.",
                               FALSE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED,
                               "Periodic signal quality check disabled",
                               "Whether periodic signal quality check is disabled.",
                               FALSE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED,
                               "Periodic access technology check disabled",
                               "Whether periodic access technology check is disabled.",
                               FALSE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_IFACE_MODEM_CARRIER_CONFIG_MAPPING,
                              "Carrier config mapping table",
                              "Path to the file including the carrier mapping for the module",
                               NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    initialized = TRUE;
}

GType
mm_iface_modem_get_type (void)
{
    static GType iface_modem_type = 0;

    if (!G_UNLIKELY (iface_modem_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem), /* class_size */
            iface_modem_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_type = g_type_register_static (G_TYPE_INTERFACE,
                                                   "MMIfaceModem",
                                                   &info,
                                                   0);

        g_type_interface_add_prerequisite (iface_modem_type, MM_TYPE_BASE_MODEM);
    }

    return iface_modem_type;
}
