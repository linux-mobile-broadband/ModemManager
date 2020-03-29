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
 * Copyright (C) 2013 Google, Inc.
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-oma.h"
#include "mm-log-object.h"

#define SUPPORT_CHECKED_TAG "oma-support-checked-tag"
#define SUPPORTED_TAG       "oma-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/

void
mm_iface_modem_oma_bind_simple_status (MMIfaceModemOma *self,
                                       MMSimpleStatus *status)
{
}

/*****************************************************************************/
/* Manage the list of pending network-initiated sessions */

static void
add_or_remove_pending_network_initiated_session (MMIfaceModemOma *self,
                                                 gboolean add,
                                                 MMOmaSessionType session_type,
                                                 guint session_id)
{
    MmGdbusModemOma *skeleton;
    GVariant *variant;
    GArray *array;
    guint i;

    g_assert (session_type != MM_OMA_SESSION_TYPE_UNKNOWN);

    g_object_get (self,
                  MM_IFACE_MODEM_OMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    variant = mm_gdbus_modem_oma_get_pending_network_initiated_sessions (skeleton);
    array = mm_common_oma_pending_network_initiated_sessions_variant_to_garray (variant);

    for (i = 0; i < array->len; i++) {
        MMOmaPendingNetworkInitiatedSession *session;

        session = &g_array_index (array, MMOmaPendingNetworkInitiatedSession, i);
        if (session->session_id == session_id)
            break;
    }

    /* If not in the array, and we want to add it, add it */
    if (add && i == array->len) {
        MMOmaPendingNetworkInitiatedSession session;

        session.session_type = session_type;
        session.session_id = session_id;
        g_array_append_val (array, session);

        mm_gdbus_modem_oma_set_pending_network_initiated_sessions (
            skeleton,
            mm_common_oma_pending_network_initiated_sessions_garray_to_variant (array));
    }
    /* If found in the array, and we want to remove it, remove it */
    else if (!add && i < array->len) {
        g_array_remove_index (array, i);
        mm_gdbus_modem_oma_set_pending_network_initiated_sessions (
            skeleton,
            mm_common_oma_pending_network_initiated_sessions_garray_to_variant (array));
    }

    g_object_unref (skeleton);
    g_array_unref (array);
}

void
mm_iface_modem_oma_add_pending_network_initiated_session (MMIfaceModemOma *self,
                                                          MMOmaSessionType session_type,
                                                          guint session_id)
{
    add_or_remove_pending_network_initiated_session (self, TRUE, session_type, session_id);
}

/*****************************************************************************/
/* New session state reported */

void
mm_iface_modem_oma_update_session_state (MMIfaceModemOma *self,
                                         MMOmaSessionState new_session_state,
                                         MMOmaSessionStateFailedReason session_state_failed_reason)
{
    MmGdbusModemOma *skeleton;
    MMOmaSessionState old_session_state;

    /* Make sure proper state vs failed reasons are given */
    g_return_if_fail ((new_session_state != MM_OMA_SESSION_STATE_FAILED &&
                       session_state_failed_reason == MM_OMA_SESSION_STATE_FAILED_REASON_UNKNOWN) ||
                      (new_session_state == MM_OMA_SESSION_STATE_FAILED));

    g_object_get (self,
                  MM_IFACE_MODEM_OMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    old_session_state = mm_gdbus_modem_oma_get_session_state (skeleton);
    if (old_session_state != new_session_state) {
        mm_obj_info (self, "OMA session state changed (%s -> %s)",
                     mm_oma_session_state_get_string (old_session_state),
                     mm_oma_session_state_get_string (new_session_state));

        /* Flush current change before signaling the state change,
         * so that clients get the proper state already in the
         * state-changed callback */
        mm_gdbus_modem_oma_set_session_state (skeleton, new_session_state);
        g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
        mm_gdbus_modem_oma_emit_session_state_changed (
            skeleton,
            old_session_state,
            new_session_state,
            session_state_failed_reason);
    }
}

/*****************************************************************************/
/* Handle Setup() */

typedef struct {
    MmGdbusModemOma *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemOma *self;
    guint32 features;
} HandleSetupContext;

static void
handle_setup_context_free (HandleSetupContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetupContext, ctx);
}

static void
setup_ready (MMIfaceModemOma *self,
             GAsyncResult *res,
             HandleSetupContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->setup_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* Update current features in the interface */
        mm_gdbus_modem_oma_set_features (ctx->skeleton, ctx->features);
        mm_gdbus_modem_oma_complete_setup (ctx->skeleton, ctx->invocation);
    }

    handle_setup_context_free (ctx);
}

static void
handle_setup_auth_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         HandleSetupContext *ctx)
{
    GError *error = NULL;
    MMModemState modem_state;
    gchar *str;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_setup_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot setup OMA: "
                                               "device not yet enabled");
        handle_setup_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->setup ||
        !MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->setup_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot setup OMA: "
                                               "operation not supported");
        handle_setup_context_free (ctx);
        return;
    }

    str = mm_oma_feature_build_string_from_mask (ctx->features);
    mm_obj_dbg (self, "setting up OMA features: '%s'", str);
    g_free (str);

    MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->setup (
        ctx->self,
        ctx->features,
        (GAsyncReadyCallback)setup_ready,
        ctx);
}

static gboolean
handle_setup (MmGdbusModemOma *skeleton,
              GDBusMethodInvocation *invocation,
              guint32 features,
              MMIfaceModemOma *self)
{
    HandleSetupContext *ctx;

    ctx = g_slice_new0 (HandleSetupContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->features = features;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_setup_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Handle StartClientInitiatedSession() */

typedef struct {
    MmGdbusModemOma *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemOma *self;
    MMOmaSessionType session_type;
} HandleStartClientInitiatedSessionContext;

static void
handle_start_client_initiated_session_context_free (HandleStartClientInitiatedSessionContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleStartClientInitiatedSessionContext, ctx);
}

static void
start_client_initiated_session_ready (MMIfaceModemOma *self,
                                      GAsyncResult *res,
                                      HandleStartClientInitiatedSessionContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->start_client_initiated_session_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* Update interface info */
        mm_gdbus_modem_oma_set_session_type (ctx->skeleton, ctx->session_type);
        mm_iface_modem_oma_update_session_state (self, MM_OMA_SESSION_STATE_STARTED, MM_OMA_SESSION_STATE_FAILED_REASON_UNKNOWN);
        mm_gdbus_modem_oma_complete_start_client_initiated_session (ctx->skeleton, ctx->invocation);
    }

    handle_start_client_initiated_session_context_free (ctx);
}

static void
handle_start_client_initiated_session_auth_ready (MMBaseModem *self,
                                                  GAsyncResult *res,
                                                  HandleStartClientInitiatedSessionContext *ctx)
{
    GError *error = NULL;
    MMModemState modem_state;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_start_client_initiated_session_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot start client-initiated OMA session: "
                                               "device not yet enabled");
        handle_start_client_initiated_session_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->start_client_initiated_session ||
        !MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->start_client_initiated_session_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot start client-initiated OMA session: "
                                               "operation not supported");
        handle_start_client_initiated_session_context_free (ctx);
        return;
    }

    if (ctx->session_type != MM_OMA_SESSION_TYPE_CLIENT_INITIATED_DEVICE_CONFIGURE &&
        ctx->session_type != MM_OMA_SESSION_TYPE_CLIENT_INITIATED_PRL_UPDATE &&
        ctx->session_type != MM_OMA_SESSION_TYPE_CLIENT_INITIATED_HANDS_FREE_ACTIVATION) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot start client-initiated OMA session: "
                                               "invalid session type specified (%s)",
                                               mm_oma_session_type_get_string (ctx->session_type));
        handle_start_client_initiated_session_context_free (ctx);
        return;
    }

    mm_obj_dbg (self, "starting client-initiated OMA session (%s)",
                mm_oma_session_type_get_string (ctx->session_type));
    MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->start_client_initiated_session (
        ctx->self,
        ctx->session_type,
        (GAsyncReadyCallback)start_client_initiated_session_ready,
        ctx);
}

static gboolean
handle_start_client_initiated_session (MmGdbusModemOma *skeleton,
                                       GDBusMethodInvocation *invocation,
                                       guint32 session_type,
                                       MMIfaceModemOma *self)
{
    HandleStartClientInitiatedSessionContext *ctx;

    ctx = g_slice_new0 (HandleStartClientInitiatedSessionContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->session_type = session_type;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_start_client_initiated_session_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Handle AcceptNetworkInitiatedSession() */

typedef struct {
    MmGdbusModemOma *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemOma *self;
    MMOmaSessionType session_type;
    guint session_id;
    gboolean accept;
} HandleAcceptNetworkInitiatedSessionContext;

static void
handle_accept_network_initiated_session_context_free (HandleAcceptNetworkInitiatedSessionContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleAcceptNetworkInitiatedSessionContext, ctx);
}

static void
accept_network_initiated_session_ready (MMIfaceModemOma *self,
                                        GAsyncResult *res,
                                        HandleAcceptNetworkInitiatedSessionContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->accept_network_initiated_session_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* If accepted or rejected, remove from pending */
        add_or_remove_pending_network_initiated_session (self, FALSE, ctx->session_type, ctx->session_id);

        /* If accepted, set as current */
        if (ctx->accept) {
            mm_gdbus_modem_oma_set_session_type (ctx->skeleton, ctx->session_type);
            mm_iface_modem_oma_update_session_state (self, MM_OMA_SESSION_STATE_STARTED, MM_OMA_SESSION_STATE_FAILED_REASON_UNKNOWN);
        }

        mm_gdbus_modem_oma_complete_accept_network_initiated_session (ctx->skeleton, ctx->invocation);
    }

    handle_accept_network_initiated_session_context_free (ctx);
}

static MMOmaSessionType
get_pending_network_initiated_session_type (MMIfaceModemOma *self,
                                            guint session_id)
{
    MMOmaSessionType session_type = MM_OMA_SESSION_TYPE_UNKNOWN;
    MmGdbusModemOma *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_OMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (skeleton) {
        GArray *array;
        guint i;

        array = (mm_common_oma_pending_network_initiated_sessions_variant_to_garray (
                     mm_gdbus_modem_oma_get_pending_network_initiated_sessions (skeleton)));
        for (i = 0; i < array->len && session_type == MM_OMA_SESSION_TYPE_UNKNOWN; i++) {
            MMOmaPendingNetworkInitiatedSession *session;

            session = &g_array_index (array, MMOmaPendingNetworkInitiatedSession, i);
            if (session->session_id == session_id)
                session_type = session->session_type;
        }

        g_array_unref (array);
        g_object_unref (skeleton);
    }

    return session_type;
}

static void
handle_accept_network_initiated_session_auth_ready (MMBaseModem *self,
                                                    GAsyncResult *res,
                                                    HandleAcceptNetworkInitiatedSessionContext *ctx)
{
    GError *error = NULL;
    MMModemState modem_state;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_accept_network_initiated_session_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot accept network-initiated OMA session: "
                                               "device not yet enabled");
        handle_accept_network_initiated_session_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->accept_network_initiated_session ||
        !MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->accept_network_initiated_session_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot accept network-initiated OMA session: "
                                               "operation not supported");
        handle_accept_network_initiated_session_context_free (ctx);
        return;
    }

    ctx->session_type = get_pending_network_initiated_session_type (ctx->self, ctx->session_id);
    if (ctx->session_type == MM_OMA_SESSION_TYPE_UNKNOWN) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot accept network-initiated OMA session: "
                                               "unknown session id (%u)",
                                               ctx->session_id);
        handle_accept_network_initiated_session_context_free (ctx);
        return;
    }

    mm_obj_dbg (self, "%s network-initiated OMA session (%s, %u)",
                ctx->accept ? "accepting" : "rejecting",
                mm_oma_session_type_get_string (ctx->session_type),
                ctx->session_id);
    MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->accept_network_initiated_session (
        ctx->self,
        ctx->session_id,
        ctx->accept,
        (GAsyncReadyCallback)accept_network_initiated_session_ready,
        ctx);
}

static gboolean
handle_accept_network_initiated_session (MmGdbusModemOma *skeleton,
                                         GDBusMethodInvocation *invocation,
                                         guint32 session_id,
                                         gboolean accept,
                                         MMIfaceModemOma *self)
{
    HandleAcceptNetworkInitiatedSessionContext *ctx;

    ctx = g_slice_new0 (HandleAcceptNetworkInitiatedSessionContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->session_type = MM_OMA_SESSION_TYPE_UNKNOWN;
    ctx->session_id = session_id;
    ctx->accept = accept;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_accept_network_initiated_session_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Handle CancelSession() */

typedef struct {
    MmGdbusModemOma *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemOma *self;
} HandleCancelSessionContext;

static void
handle_cancel_session_context_free (HandleCancelSessionContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleCancelSessionContext, ctx);
}

static void
cancel_session_ready (MMIfaceModemOma *self,
                      GAsyncResult *res,
                      HandleCancelSessionContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->cancel_session_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* Clear interface info when cancelled */
        mm_gdbus_modem_oma_set_session_type (ctx->skeleton, MM_OMA_SESSION_TYPE_UNKNOWN);
        mm_iface_modem_oma_update_session_state (self, MM_OMA_SESSION_STATE_UNKNOWN, MM_OMA_SESSION_STATE_FAILED_REASON_UNKNOWN);

        mm_gdbus_modem_oma_complete_cancel_session (ctx->skeleton, ctx->invocation);
    }

    handle_cancel_session_context_free (ctx);
}

static void
handle_cancel_session_auth_ready (MMBaseModem *self,
                                  GAsyncResult *res,
                                  HandleCancelSessionContext *ctx)
{
    GError *error = NULL;
    MMModemState modem_state;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_cancel_session_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot cancel OMA session: "
                                               "device not yet enabled");
        handle_cancel_session_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->cancel_session ||
        !MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->cancel_session_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot cancel OMA session: "
                                               "operation not supported");
        handle_cancel_session_context_free (ctx);
        return;
    }

    mm_obj_dbg (self, "cancelling OMA session");
    MM_IFACE_MODEM_OMA_GET_INTERFACE (ctx->self)->cancel_session (
        ctx->self,
        (GAsyncReadyCallback)cancel_session_ready,
        ctx);
}

static gboolean
handle_cancel_session (MmGdbusModemOma *skeleton,
                       GDBusMethodInvocation *invocation,
                       MMIfaceModemOma *self)
{
    HandleCancelSessionContext *ctx;

    ctx = g_slice_new0 (HandleCancelSessionContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_cancel_session_auth_ready,
                             ctx);
    return TRUE;
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
    MmGdbusModemOma *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_oma_disable_finish (MMIfaceModemOma *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModemOma *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->disable_unsolicited_events_finish (self, res, &error);
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
cleanup_unsolicited_events_ready (MMIfaceModemOma *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
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
    MMIfaceModemOma *self;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->cleanup_unsolicited_events (
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
mm_iface_modem_oma_disable (MMIfaceModemOma *self,
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
                  MM_IFACE_MODEM_OMA_DBUS_SKELETON, &ctx->skeleton,
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
    ENABLING_STEP_LOAD_FEATURES,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModemOma *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_oma_enable_finish (MMIfaceModemOma *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
load_features_ready (MMIfaceModemOma *self,
                     GAsyncResult *res,
                     GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;
    MMOmaFeature features;

    features = MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->load_features_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Update in the interface */
    mm_gdbus_modem_oma_set_features (ctx->skeleton, features);

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (task);
}

static void
setup_unsolicited_events_ready (MMIfaceModemOma *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
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
enable_unsolicited_events_ready (MMIfaceModemOma *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    /* Not critical! */
    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error)) {
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
    MMIfaceModemOma *self;
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

    case ENABLING_STEP_LOAD_FEATURES:
        if (MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->load_features &&
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->load_features_finish) {
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->load_features (
                self,
                (GAsyncReadyCallback)load_features_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->enable_unsolicited_events (
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
mm_iface_modem_oma_enable (MMIfaceModemOma *self,
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
                  MM_IFACE_MODEM_OMA_DBUS_SKELETON, &ctx->skeleton,
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
    MmGdbusModemOma *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static void
check_support_ready (MMIfaceModemOma *self,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->check_support_finish (self, res, &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_obj_dbg (self, "OMA support check failed: %s", error->message);
            g_error_free (error);
        }
    } else {
        /* OMA is supported! */
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
    MMIfaceModemOma *self;
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

        ctx->step++;
        /* fall through */

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

            if (MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->check_support &&
                MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_OMA_GET_INTERFACE (self)->check_support (
                    self,
                    (GAsyncReadyCallback)check_support_ready,
                    task);
                return;
            }

            /* If there is no implementation to check support, assume we DON'T
             * support it. */
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                   supported_quark))) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "OMA not supported");
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-setup",
                          G_CALLBACK (handle_setup),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-start-client-initiated-session",
                          G_CALLBACK (handle_start_client_initiated_session),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-accept-network-initiated-session",
                          G_CALLBACK (handle_accept_network_initiated_session),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-cancel-session",
                          G_CALLBACK (handle_cancel_session),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_oma (MM_GDBUS_OBJECT_SKELETON (self),
                                                MM_GDBUS_MODEM_OMA (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_oma_initialize_finish (MMIfaceModemOma *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_oma_initialize (MMIfaceModemOma *self,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemOma *skeleton = NULL;
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_OMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_oma_skeleton_new ();
        g_object_set (self,
                      MM_IFACE_MODEM_OMA_DBUS_SKELETON, skeleton,
                      NULL);

        /* Set all initial property defaults */
        mm_gdbus_modem_oma_set_features (skeleton, MM_OMA_FEATURE_NONE);
        mm_gdbus_modem_oma_set_session_type (skeleton, MM_OMA_SESSION_TYPE_UNKNOWN);
        mm_gdbus_modem_oma_set_session_state (skeleton, MM_OMA_SESSION_STATE_UNKNOWN);
        mm_gdbus_modem_oma_set_pending_network_initiated_sessions (skeleton, mm_common_build_oma_pending_network_initiated_sessions_default ());
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
mm_iface_modem_oma_shutdown (MMIfaceModemOma *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_oma (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_OMA_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_oma_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_OMA_DBUS_SKELETON,
                              "OMA DBus skeleton",
                              "DBus skeleton for the OMA interface",
                              MM_GDBUS_TYPE_MODEM_OMA_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_oma_get_type (void)
{
    static GType iface_modem_oma_type = 0;

    if (!G_UNLIKELY (iface_modem_oma_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemOma), /* class_size */
            iface_modem_oma_init,     /* base_init */
            NULL,                      /* base_finalize */
        };

        iface_modem_oma_type = g_type_register_static (G_TYPE_INTERFACE,
                                                       "MMIfaceModemOma",
                                                       &info,
                                                       0);

        g_type_interface_add_prerequisite (iface_modem_oma_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_oma_type;
}
