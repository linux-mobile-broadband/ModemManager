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
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-sim.h"
#include "mm-bearer-list.h"
#include "mm-log.h"
#include "mm-context.h"

#define SIGNAL_QUALITY_RECENT_TIMEOUT_SEC        60
#define SIGNAL_QUALITY_INITIAL_CHECK_TIMEOUT_SEC 3
#define SIGNAL_QUALITY_CHECK_TIMEOUT_SEC         30
#define ACCESS_TECHNOLOGIES_CHECK_TIMEOUT_SEC    30

#define STATE_UPDATE_CONTEXT_TAG              "state-update-context-tag"
#define SIGNAL_QUALITY_UPDATE_CONTEXT_TAG     "signal-quality-update-context-tag"
#define SIGNAL_QUALITY_CHECK_CONTEXT_TAG      "signal-quality-check-context-tag"
#define ACCESS_TECHNOLOGIES_CHECK_CONTEXT_TAG "access-technologies-check-context-tag"
#define RESTART_INITIALIZE_IDLE_TAG           "restart-initialize-tag"

static GQuark state_update_context_quark;
static GQuark signal_quality_update_context_quark;
static GQuark signal_quality_check_context_quark;
static GQuark access_technologies_check_context_quark;
static GQuark restart_initialize_idle_quark;

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

    g_object_bind_property (skeleton, "bands",
                            status, MM_SIMPLE_PROPERTY_BANDS,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "access-technologies",
                            status, MM_SIMPLE_PROPERTY_ACCESS_TECHNOLOGIES,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_unref (skeleton);
}

/*****************************************************************************/
/* Helper method to wait for a final state */

#define MODEM_STATE_IS_INTERMEDIATE(state)      \
    (state == MM_MODEM_STATE_INITIALIZING ||    \
     state == MM_MODEM_STATE_INITIALIZING ||    \
     state == MM_MODEM_STATE_INITIALIZING ||    \
     state == MM_MODEM_STATE_INITIALIZING ||    \
     state == MM_MODEM_STATE_INITIALIZING)

typedef struct {
    MMIfaceModem *self;
    MMModemState final_state;
    GSimpleAsyncResult *result;
    gulong state_changed_id;
    guint state_changed_wait_id;
} WaitForFinalStateContext;

static void
wait_for_final_state_context_complete_and_free (WaitForFinalStateContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_signal_handler_disconnect (ctx->self, ctx->state_changed_id);
    g_source_remove (ctx->state_changed_wait_id);
    g_object_unref (ctx->self);
    g_slice_free (WaitForFinalStateContext, ctx);
}

MMModemState
mm_iface_modem_wait_for_final_state_finish (MMIfaceModem *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_STATE_UNKNOWN;

    return (MMModemState)GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static gboolean
state_changed_wait_expired (WaitForFinalStateContext *ctx)
{
    g_simple_async_result_set_error (
        ctx->result,
        MM_CORE_ERROR,
        MM_CORE_ERROR_RETRY,
        "Too much time waiting to get to a final state");
    wait_for_final_state_context_complete_and_free (ctx);
    return FALSE;
}

static void
state_changed (MMIfaceModem *self,
               GParamSpec *spec,
               WaitForFinalStateContext *ctx)
{
    MMModemState state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);

    /* Are we in a final state already? */
    if (MODEM_STATE_IS_INTERMEDIATE (state))
        return;

    /* If we want a specific final state and this is not the one we were
     * looking for, then skip */
    if (ctx->final_state != MM_MODEM_STATE_UNKNOWN &&
        state != MM_MODEM_STATE_UNKNOWN &&
        state != ctx->final_state)
        return;

    /* Done! */
    g_simple_async_result_set_op_res_gpointer (ctx->result, GUINT_TO_POINTER (state), NULL);
    wait_for_final_state_context_complete_and_free (ctx);
}

void
mm_iface_modem_wait_for_final_state (MMIfaceModem *self,
                                     MMModemState final_state,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MMModemState state = MM_MODEM_STATE_UNKNOWN;
    WaitForFinalStateContext *ctx;
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_wait_for_final_state);

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);

    /* Are we in a final state already? */
    if (!MODEM_STATE_IS_INTERMEDIATE (state)) {
        /* Is this the state we actually wanted? */
        if (final_state == MM_MODEM_STATE_UNKNOWN ||
            (state != MM_MODEM_STATE_UNKNOWN && state == final_state)) {
            g_simple_async_result_set_op_res_gpointer (result, GUINT_TO_POINTER (state), NULL);
            g_simple_async_result_complete_in_idle (result);
            g_object_unref (result);
            return;
        }

        /* Otherwise, we'll need to wait for the exact one we want */
    }

    ctx = g_slice_new0 (WaitForFinalStateContext);
    ctx->result = result;
    ctx->self = g_object_ref (self);
    ctx->final_state = final_state;

    /* Want to get notified when modem state changes */
    ctx->state_changed_id = g_signal_connect (ctx->self,
                                              "notify::" MM_IFACE_MODEM_STATE,
                                              G_CALLBACK (state_changed),
                                              ctx);
    /* But we don't want to wait forever */
    ctx->state_changed_wait_id = g_timeout_add_seconds (10,
                                                        (GSourceFunc)state_changed_wait_expired,
                                                        ctx);
}

/*****************************************************************************/

static MMModemState get_current_consolidated_state (MMIfaceModem *self, MMModemState modem_state);

typedef struct {
    MMBearer *self;
    guint others_connected;
} CountOthersConnectedContext;

static void
bearer_list_count_others_connected (MMBearer *bearer,
                                    CountOthersConnectedContext *ctx)
{
    /* We can safely compare pointers here */
    if (bearer != ctx->self &&
        mm_bearer_get_status (bearer) == MM_BEARER_STATUS_CONNECTED) {
        ctx->others_connected++;
    }
}

static void
bearer_status_changed (MMBearer *bearer,
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

        switch (mm_bearer_get_status (bearer)) {
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
            new_state = get_current_consolidated_state (self, MM_MODEM_STATE_UNKNOWN);
            break;
        }

        mm_iface_modem_update_state (self,
                                     new_state,
                                     MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);
    }

    g_object_unref (list);
}

typedef struct {
    MMIfaceModem *self;
    MMBearerList *list;
    GSimpleAsyncResult *result;
} CreateBearerContext;

static void
create_bearer_context_complete_and_free (CreateBearerContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    if (ctx->list)
        g_object_unref (ctx->list);
    g_slice_free (CreateBearerContext, ctx);
}

MMBearer *
mm_iface_modem_create_bearer_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
create_bearer_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     CreateBearerContext *ctx)
{
    MMBearer *bearer;
    GError *error = NULL;

    bearer = MM_IFACE_MODEM_GET_INTERFACE (self)->create_bearer_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        create_bearer_context_complete_and_free (ctx);
        return;
    }

    if (!mm_bearer_list_add_bearer (ctx->list, bearer, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        create_bearer_context_complete_and_free (ctx);
        g_object_unref (bearer);
        return;
    }

    /* If bearer properly created and added to the list, follow its
     * status */
    g_signal_connect (bearer,
                      "notify::"  MM_BEARER_STATUS,
                      (GCallback)bearer_status_changed,
                      self);
    g_simple_async_result_set_op_res_gpointer (ctx->result, bearer, g_object_unref);
    create_bearer_context_complete_and_free (ctx);
}

void
mm_iface_modem_create_bearer (MMIfaceModem *self,
                              MMBearerProperties *properties,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    CreateBearerContext *ctx;

    ctx = g_slice_new (CreateBearerContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_create_bearer);
    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &ctx->list,
                  NULL);
    if (!ctx->list) {
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Cannot add new bearer: bearer list not found");
        create_bearer_context_complete_and_free (ctx);
        return;
    }

    if (mm_bearer_list_get_count (ctx->list) == mm_bearer_list_get_max (ctx->list)) {
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_TOO_MANY,
            "Cannot add new bearer: already reached maximum (%u)",
            mm_bearer_list_get_count (ctx->list));
        create_bearer_context_complete_and_free (ctx);
        return;
    }

    MM_IFACE_MODEM_GET_INTERFACE (self)->create_bearer (
        self,
        properties,
        (GAsyncReadyCallback)create_bearer_ready,
        ctx);
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
    MMBearer *bearer;
    GError *error = NULL;

    bearer = mm_iface_modem_create_bearer_finish (self, res, &error);
    if (!bearer)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem_complete_create_bearer (ctx->skeleton,
                                               ctx->invocation,
                                               mm_bearer_get_path (bearer));
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
} HandleDeleteBearerContext;

static void
handle_delete_bearer_context_free (HandleDeleteBearerContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    if (ctx->list)
        g_object_unref (ctx->list);
    g_free (ctx->bearer_path);
    g_free (ctx);
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

    if (!ctx->list)
        mm_gdbus_modem_complete_delete_bearer (ctx->skeleton, ctx->invocation);
    else if (!mm_bearer_list_delete_bearer (ctx->list, ctx->bearer_path, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_delete_bearer (ctx->skeleton, ctx->invocation);

    handle_delete_bearer_context_free (ctx);
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
        mm_info ("Modem %s: access technology changed (%s -> %s)",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (self)),
                 old_access_tech_string,
                 new_access_tech_string);
        g_free (old_access_tech_string);
        g_free (new_access_tech_string);
    }

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    guint timeout_source;
    gboolean running;
} AccessTechnologiesCheckContext;

static void
access_technologies_check_context_free (AccessTechnologiesCheckContext *ctx)
{
    if (ctx->timeout_source)
        g_source_remove (ctx->timeout_source);
    g_free (ctx);
}

static void
access_technologies_check_ready (MMIfaceModem *self,
                                 GAsyncResult *res)
{
    GError *error = NULL;
    MMModemAccessTechnology access_technologies = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    guint mask = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    AccessTechnologiesCheckContext *ctx;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_access_technologies_finish (
            self,
            res,
            &access_technologies,
            &mask,
            &error)) {
        /* Ignore issues when the operation is unsupported, don't even log */
        if (!g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED))
            mm_dbg ("Couldn't refresh access technologies: '%s'", error->message);
        g_error_free (error);
    } else
        mm_iface_modem_update_access_technologies (self, access_technologies, mask);

    /* Remove the running tag. Note that the context may have been removed by
     * mm_iface_modem_shutdown when this function is invoked as a callback of
     * load_access_technologies. */
    ctx = g_object_get_qdata (G_OBJECT (self), access_technologies_check_context_quark);
    if (ctx)
        ctx->running = FALSE;
}

static gboolean
periodic_access_technologies_check (MMIfaceModem *self)
{
    AccessTechnologiesCheckContext *ctx;

    ctx = g_object_get_qdata (G_OBJECT (self), access_technologies_check_context_quark);

    /* Only launch a new one if not one running already OR if the last one run
     * was more than 15s ago. */
    if (!ctx->running) {
        ctx->running = TRUE;
        MM_IFACE_MODEM_GET_INTERFACE (self)->load_access_technologies (
            self,
            (GAsyncReadyCallback)access_technologies_check_ready,
            NULL);
    }

    return TRUE;
}

static void
periodic_access_technologies_check_disable (MMIfaceModem *self)
{
    if (G_UNLIKELY (!access_technologies_check_context_quark))
        access_technologies_check_context_quark = (g_quark_from_static_string (
                                                       ACCESS_TECHNOLOGIES_CHECK_CONTEXT_TAG));

    /* Clear access technology */
    mm_iface_modem_update_access_technologies (self,
                                               MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,
                                               MM_MODEM_ACCESS_TECHNOLOGY_ANY);

    /* Overwriting the data will free the previous context */
    g_object_set_qdata (G_OBJECT (self),
                        access_technologies_check_context_quark,
                        NULL);

    mm_dbg ("Periodic access technology checks disabled");
}

static void
periodic_access_technologies_check_enable (MMIfaceModem *self)
{
    AccessTechnologiesCheckContext *ctx;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_access_technologies ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->load_access_technologies_finish) {
        /* If loading access technology not supported, don't even bother setting up
         * a timeout */
        return;
    }

    if (G_UNLIKELY (!access_technologies_check_context_quark))
        access_technologies_check_context_quark = (g_quark_from_static_string (
                                                       ACCESS_TECHNOLOGIES_CHECK_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), access_technologies_check_context_quark);

    /* If context is already there, we're already enabled */
    if (ctx) {
        periodic_access_technologies_check (self);
        return;
    }

    /* Create context and keep it as object data */
    mm_dbg ("Periodic access technology checks enabled");
    ctx = g_new0 (AccessTechnologiesCheckContext, 1);
    ctx->timeout_source = g_timeout_add_seconds (ACCESS_TECHNOLOGIES_CHECK_TIMEOUT_SEC,
                                                 (GSourceFunc)periodic_access_technologies_check,
                                                 self);
    g_object_set_qdata_full (G_OBJECT (self),
                             access_technologies_check_context_quark,
                             ctx,
                             (GDestroyNotify)access_technologies_check_context_free);

    /* Get first access technology value */
    periodic_access_technologies_check (self);
}

/*****************************************************************************/

typedef struct {
    time_t last_update;
    guint recent_timeout_source;
} SignalQualityUpdateContext;

static void
signal_quality_update_context_free (SignalQualityUpdateContext *ctx)
{
    if (ctx->recent_timeout_source)
        g_source_remove (ctx->recent_timeout_source);
    g_free (ctx);
}

static time_t
get_last_signal_quality_update_time (MMIfaceModem *self)
{
    SignalQualityUpdateContext *ctx;

    if (G_UNLIKELY (!signal_quality_update_context_quark))
        signal_quality_update_context_quark = (g_quark_from_static_string (
                                                   SIGNAL_QUALITY_UPDATE_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), signal_quality_update_context_quark);

    return (ctx ? ctx->last_update : 0);
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
            mm_dbg ("Signal quality value not updated in %us, "
                    "marking as not being recent",
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
    return FALSE;
}

static void
update_signal_quality (MMIfaceModem *self,
                       guint signal_quality,
                       gboolean expire)
{
    SignalQualityUpdateContext *ctx;
    MmGdbusModem *skeleton = NULL;
    const gchar *dbus_path;

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

    /* Keep current timestamp */
    ctx->last_update = time (NULL);

    /* Note: we always set the new value, even if the signal quality level
     * is the same, in order to provide an up to date 'recent' flag.
     * The only exception being if 'expire' is FALSE; in that case we assume
     * the value won't expire and therefore can be considered obsolete
     * already. */
    mm_gdbus_modem_set_signal_quality (skeleton,
                                       g_variant_new ("(ub)",
                                                      signal_quality,
                                                      expire));

    dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
    mm_info ("Modem %s: signal quality updated (%u)",
             dbus_path,
             signal_quality);

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

typedef struct {
    guint interval;
    guint initial_retries;
    guint timeout_source;
    gboolean running;
} SignalQualityCheckContext;

static void
signal_quality_check_context_free (SignalQualityCheckContext *ctx)
{
    if (ctx->timeout_source)
        g_source_remove (ctx->timeout_source);
    g_free (ctx);
}

static gboolean periodic_signal_quality_check (MMIfaceModem *self);

static void
signal_quality_check_ready (MMIfaceModem *self,
                            GAsyncResult *res)
{
    GError *error = NULL;
    guint signal_quality;
    SignalQualityCheckContext *ctx;

    signal_quality = MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality_finish (self,
                                                                                      res,
                                                                                      &error);
    if (error) {
        mm_dbg ("Couldn't refresh signal quality: '%s'", error->message);
        g_error_free (error);
    } else
        update_signal_quality (self, signal_quality, TRUE);

    /* Remove the running tag. Note that the context may have been removed by
     * mm_iface_modem_shutdown when this function is invoked as a callback of
     * load_signal_quality. */
    ctx = g_object_get_qdata (G_OBJECT (self), signal_quality_check_context_quark);
    if (ctx) {
        if (ctx->interval == SIGNAL_QUALITY_INITIAL_CHECK_TIMEOUT_SEC &&
            (signal_quality != 0 || --ctx->initial_retries == 0)) {
            ctx->interval = SIGNAL_QUALITY_CHECK_TIMEOUT_SEC;
            if (ctx->timeout_source) {
                mm_dbg ("Periodic signal quality checks rescheduled (interval = %ds)", ctx->interval);
                g_source_remove(ctx->timeout_source);
                ctx->timeout_source = g_timeout_add_seconds (ctx->interval,
                                                             (GSourceFunc)periodic_signal_quality_check,
                                                             self);
            }
        }
        ctx->running = FALSE;
    }
}

static gboolean
periodic_signal_quality_check (MMIfaceModem *self)
{
    SignalQualityCheckContext *ctx;

    ctx = g_object_get_qdata (G_OBJECT (self), signal_quality_check_context_quark);

    /* Only launch a new one if not one running already OR if the last one run
     * was more than 15s ago. */
    if (!ctx->running ||
        (time (NULL) - get_last_signal_quality_update_time (self) > (ctx->interval / 2))) {
        ctx->running = TRUE;
        MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality (
            self,
            (GAsyncReadyCallback)signal_quality_check_ready,
            NULL);
    }

    return TRUE;
}

static void
periodic_signal_quality_check_disable (MMIfaceModem *self)
{
    if (G_UNLIKELY (!signal_quality_check_context_quark))
        signal_quality_check_context_quark = (g_quark_from_static_string (
                                                  SIGNAL_QUALITY_CHECK_CONTEXT_TAG));

    /* Clear signal quality */
    update_signal_quality (self, 0, FALSE);

    /* Overwriting the data will free the previous context */
    g_object_set_qdata (G_OBJECT (self),
                        signal_quality_check_context_quark,
                        NULL);

    mm_dbg ("Periodic signal quality checks disabled");
}

static void
periodic_signal_quality_check_enable (MMIfaceModem *self)
{
    SignalQualityCheckContext *ctx;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality_finish) {
        /* If loading signal quality not supported, don't even bother setting up
         * a timeout */
        return;
    }

    if (G_UNLIKELY (!signal_quality_check_context_quark))
        signal_quality_check_context_quark = (g_quark_from_static_string (
                                                  SIGNAL_QUALITY_CHECK_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), signal_quality_check_context_quark);

    /* If context is already there, we're already enabled */
    if (ctx) {
        periodic_signal_quality_check (self);
        return;
    }

    /* Create context and keep it as object data */
    ctx = g_new0 (SignalQualityCheckContext, 1);
    /* Schedule the signal quality check using a shorter period, up to 5
     * periods, initially until a non-zero signal quality value is obtained
     * and then switch back to the normal period. */
    ctx->interval = SIGNAL_QUALITY_INITIAL_CHECK_TIMEOUT_SEC;
    ctx->initial_retries = 5;
    mm_dbg ("Periodic signal quality checks enabled (interval = %ds)", ctx->interval);
    ctx->timeout_source = g_timeout_add_seconds (ctx->interval,
                                                 (GSourceFunc)periodic_signal_quality_check,
                                                 self);
    g_object_set_qdata_full (G_OBJECT (self),
                             signal_quality_check_context_quark,
                             ctx,
                             (GDestroyNotify)signal_quality_check_context_free);

    /* Get first signal quality value */
    periodic_signal_quality_check (self);
}

/*****************************************************************************/

static void
bearer_list_count_connected (MMBearer *bearer,
                             guint *count)
{
    if (mm_bearer_get_status (bearer) == MM_BEARER_STATUS_CONNECTED)
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
    if ((new_state == MM_MODEM_STATE_SEARCHING ||
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
        new_state = get_current_consolidated_state (self, new_state);

    /* Update state only if different */
    if (new_state != old_state) {
        const gchar *dbus_path;

        dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
        mm_info ("Modem%s%s: state changed (%s -> %s)",
                 dbus_path ? " " : "",
                 dbus_path ? dbus_path : "",
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

        /* If we go to registered state (from unregistered), setup signal
         * quality and access technologies periodic retrieval */
        if (new_state == MM_MODEM_STATE_REGISTERED &&
            old_state < MM_MODEM_STATE_REGISTERED) {
            periodic_signal_quality_check_enable (self);
            periodic_access_technologies_check_enable (self);
        }
        /* If we go from a registered/connected state to unregistered,
         * cleanup signal quality retrieval */
        else if (old_state >= MM_MODEM_STATE_REGISTERED &&
                 new_state < MM_MODEM_STATE_REGISTERED) {
            periodic_signal_quality_check_disable (self);
            periodic_access_technologies_check_disable (self);
        }
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
get_current_consolidated_state (MMIfaceModem *self, MMModemState modem_state)
{
    MMModemState consolidated = modem_state;
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
     * the subystem. Warn if we get others */
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

        mm_dbg ("Will start keeping track of state for subsystem '%s'",
                subsystem);
        s.subsystem = g_strdup (subsystem);
        s.state = subsystem_state;
        g_array_append_val (subsystem_states, s);
    }

    return get_current_consolidated_state (self, modem_state);
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

    /* Error if we're not in disabled state */
    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state != MM_MODEM_STATE_DISABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set power state: not in disabled state");
        handle_set_power_state_context_free (ctx);
        return;
    }

    /* Only 'low' or 'up' expected */
    if (ctx->power_state != MM_MODEM_POWER_STATE_LOW &&
        ctx->power_state != MM_MODEM_POWER_STATE_ON) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_INVALID_ARGS,
                                               "Cannot set '%s' power state",
                                               mm_modem_power_state_get_string (ctx->power_state));
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
    MMModemState modem_state;
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

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_DISABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot reset modem: not initialized/unlocked yet");
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
    MMModemState modem_state;
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

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_DISABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot reset the modem to factory defaults: "
                                               "not initialized/unlocked yet");
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
/* BANDS */

typedef struct {
    MMIfaceModem *self;
    MmGdbusModem *skeleton;
    GSimpleAsyncResult *result;
    GArray *bands_array;
} SetBandsContext;

static void
set_bands_context_complete_and_free (SetBandsContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    if (ctx->bands_array)
        g_array_unref (ctx->bands_array);
    g_free (ctx);
}

gboolean
mm_iface_modem_set_bands_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
set_bands_ready (MMIfaceModem *self,
                 GAsyncResult *res,
                 SetBandsContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_bands_finish (self, res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else {
        /* Never show just 'any' in the interface */
        if (ctx->bands_array->len == 1 &&
            g_array_index (ctx->bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
            GArray *supported_bands;

            supported_bands = (mm_common_bands_variant_to_garray (
                                   mm_gdbus_modem_get_supported_bands (ctx->skeleton)));
            mm_gdbus_modem_set_bands (ctx->skeleton,
                                      mm_common_bands_garray_to_variant (supported_bands));
            g_array_unref (supported_bands);
        } else
            mm_gdbus_modem_set_bands (ctx->skeleton,
                                      mm_common_bands_garray_to_variant (ctx->bands_array));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    set_bands_context_complete_and_free (ctx);
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
                                               (const MMModemBand *)supported_bands_array->data,
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
mm_iface_modem_set_bands (MMIfaceModem *self,
                          GArray *bands_array,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    SetBandsContext *ctx;
    GArray *supported_bands_array;
    GArray *current_bands_array;
    GError *error = NULL;
    gchar *bands_string;

    /* If setting allowed bands is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_bands ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_bands_finish) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Setting allowed bands not supported");
        return;
    }

    /* Setup context */
    ctx = g_new0 (SetBandsContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_set_bands);
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        set_bands_context_complete_and_free (ctx);
        return;
    }

    bands_string = mm_common_build_bands_string ((MMModemBand *)bands_array->data,
                                                 bands_array->len);

    /* Get list of supported bands */
    supported_bands_array = (mm_common_bands_variant_to_garray (
                                 mm_gdbus_modem_get_supported_bands (ctx->skeleton)));

    /* Set ctx->bands_array to target list of bands before comparing with current list
     * of bands. If input list of bands contains only ANY, target list of bands is set
     * to list of supported bands excluding ANY. */
    if (bands_array->len == 1 &&
        g_array_index (bands_array, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        guint i;

        for (i = 0; i < supported_bands_array->len; i++) {
            MMModemBand band = g_array_index (supported_bands_array, MMModemBand, i);

            if (band != MM_MODEM_BAND_ANY &&
                band != MM_MODEM_BAND_UNKNOWN) {
                if (!ctx->bands_array)
                    ctx->bands_array = g_array_sized_new (FALSE,
                                                          FALSE,
                                                          sizeof (MMModemBand),
                                                          supported_bands_array->len);

                g_array_insert_val (ctx->bands_array, i, band);
            }
        }
    }

    if (!ctx->bands_array)
        ctx->bands_array = g_array_ref (bands_array);

    /* Simply return if target list of bands equals to current list of bands */
    current_bands_array = (mm_common_bands_variant_to_garray (
                              mm_gdbus_modem_get_bands (ctx->skeleton)));
    if (mm_common_bands_garray_cmp (ctx->bands_array, current_bands_array)) {
        mm_dbg ("Requested list of bands (%s) is equal to the current ones, skipping re-set",
                bands_string);
        g_free (bands_string);
        g_array_unref (supported_bands_array);
        g_array_unref (current_bands_array);
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_bands_context_complete_and_free (ctx);
        return;
    }

    /* Done comparison with current list of bands. Always use input list of bands
     * when setting bands */
    if (ctx->bands_array != bands_array) {
        g_array_unref (ctx->bands_array);
        ctx->bands_array = g_array_ref (bands_array);
    }

    /* Validate input list of bands */
    if (!validate_bands (supported_bands_array,
                         ctx->bands_array,
                         &error)) {
        mm_dbg ("Requested list of bands (%s) cannot be handled",
                bands_string);
        g_free (bands_string);
        g_array_unref (supported_bands_array);
        g_array_unref (current_bands_array);
        g_simple_async_result_take_error (ctx->result, error);
        set_bands_context_complete_and_free (ctx);
        return;
    }

    mm_dbg ("Setting new list of bands: '%s'", bands_string);
    MM_IFACE_MODEM_GET_INTERFACE (self)->set_bands (
        self,
        ctx->bands_array,
        (GAsyncReadyCallback)set_bands_ready,
        ctx);

    g_array_unref (supported_bands_array);
    g_array_unref (current_bands_array);
    g_free (bands_string);
}

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    GVariant *bands;
} HandleSetBandsContext;

static void
handle_set_bands_context_free (HandleSetBandsContext *ctx)
{
    g_variant_unref (ctx->bands);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_set_bands_ready (MMIfaceModem *self,
                        GAsyncResult *res,
                        HandleSetBandsContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_bands_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_set_bands (ctx->skeleton, ctx->invocation);

    handle_set_bands_context_free (ctx);
}

static void
handle_set_bands_auth_ready (MMBaseModem *self,
                             GAsyncResult *res,
                             HandleSetBandsContext *ctx)
{
    GArray *bands_array;
    MMModemState modem_state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_bands_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_DISABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set allowed bands: "
                                               "not initialized/unlocked yet");
        handle_set_bands_context_free (ctx);
        return;
    }

    bands_array = mm_common_bands_variant_to_garray (ctx->bands);
    mm_iface_modem_set_bands (MM_IFACE_MODEM (self),
                              bands_array,
                              (GAsyncReadyCallback)handle_set_bands_ready,
                              ctx);
    g_array_unref (bands_array);
}

static gboolean
handle_set_bands (MmGdbusModem *skeleton,
                  GDBusMethodInvocation *invocation,
                  GVariant *bands_variant,
                  MMIfaceModem *self)
{
    HandleSetBandsContext *ctx;

    ctx = g_new (HandleSetBandsContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->bands = g_variant_ref (bands_variant);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_bands_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* ALLOWED MODES */

typedef struct {
    MMIfaceModem *self;
    MmGdbusModem *skeleton;
    GSimpleAsyncResult *result;
    MMModemMode allowed;
    MMModemMode preferred;
} SetAllowedModesContext;

static void
set_allowed_modes_context_complete_and_free (SetAllowedModesContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_set_allowed_modes_finish (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
set_allowed_modes_ready (MMIfaceModem *self,
                         GAsyncResult *res,
                         SetAllowedModesContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes_finish (self, res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else {
        mm_gdbus_modem_set_allowed_modes (ctx->skeleton, ctx->allowed);
        mm_gdbus_modem_set_preferred_mode (ctx->skeleton, ctx->preferred);
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    set_allowed_modes_context_complete_and_free (ctx);
}

void
mm_iface_modem_set_allowed_modes (MMIfaceModem *self,
                                  MMModemMode allowed,
                                  MMModemMode preferred,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    SetAllowedModesContext *ctx;
    MMModemMode supported;
    MMModemMode not_supported;

    /* If setting allowed modes is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes_finish) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Setting allowed modes not supported");
        return;
    }

    /* Setup context */
    ctx = g_new0 (SetAllowedModesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_set_allowed_modes);
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    /* Get list of supported modes */
    supported = mm_gdbus_modem_get_supported_modes (ctx->skeleton);

    /* Whenever we get 'any', just reset to be equal to the list of supported modes */
    if (allowed == MM_MODEM_MODE_ANY)
        allowed = supported;

    ctx->allowed = allowed;
    ctx->preferred = preferred;

    /* Check if we already are in the requested setup */
    if (mm_gdbus_modem_get_allowed_modes (ctx->skeleton) == allowed &&
        mm_gdbus_modem_get_preferred_mode (ctx->skeleton) == preferred) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    /* Check if any of the modes being allowed is not supported */
    not_supported = ((supported ^ allowed) & allowed);

    /* Ensure allowed is a subset of supported */
    if (not_supported) {
        gchar *not_supported_str;
        gchar *supported_str;

        not_supported_str = mm_modem_mode_build_string_from_mask (not_supported);
        supported_str = mm_modem_mode_build_string_from_mask (supported);
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Some of the allowed modes (%s) are not "
                                         "supported (%s)",
                                         not_supported_str,
                                         supported_str);
        g_free (supported_str);
        g_free (not_supported_str);

        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    /* Ensure preferred, if given, is a subset of allowed */
    if ((allowed ^ preferred) & preferred) {
        gchar *preferred_str;
        gchar *allowed_str;

        preferred_str = mm_modem_mode_build_string_from_mask (preferred);
        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "Preferred mode (%s) is not allowed (%s)",
                                         preferred_str,
                                         allowed_str);
        g_free (preferred_str);
        g_free (allowed_str);

        set_allowed_modes_context_complete_and_free (ctx);
        return;
    }

    MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes (self,
                                                            allowed,
                                                            preferred,
                                                            (GAsyncReadyCallback)set_allowed_modes_ready,
                                                            ctx);
}

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
    MMModemMode allowed;
    MMModemMode preferred;
} HandleSetAllowedModesContext;

static void
handle_set_allowed_modes_context_free (HandleSetAllowedModesContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_set_allowed_modes_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                HandleSetAllowedModesContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_allowed_modes_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_set_allowed_modes (ctx->skeleton, ctx->invocation);

    handle_set_allowed_modes_context_free (ctx);
}

static void
handle_set_allowed_modes_auth_ready (MMBaseModem *self,
                                     GAsyncResult *res,
                                     HandleSetAllowedModesContext *ctx)
{
    MMModemState modem_state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_allowed_modes_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (modem_state < MM_MODEM_STATE_DISABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set allowed modes: "
                                               "not initialized/unlocked yet");
        handle_set_allowed_modes_context_free (ctx);
        return;
    }

    mm_iface_modem_set_allowed_modes (MM_IFACE_MODEM (self),
                                      ctx->allowed,
                                      ctx->preferred,
                                      (GAsyncReadyCallback)handle_set_allowed_modes_ready,
                                      ctx);
}

static gboolean
handle_set_allowed_modes (MmGdbusModem *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint allowed,
                          guint preferred,
                          MMIfaceModem *self)
{
    HandleSetAllowedModesContext *ctx;

    ctx = g_new (HandleSetAllowedModesContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->allowed = allowed;
    ctx->preferred = preferred;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_allowed_modes_auth_ready,
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
        mm_warn ("Modem reinitialization failed: '%s'", error->message);
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
    return FALSE;
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

static void
update_unlock_retries (MMIfaceModem *self,
                       MMUnlockRetries *unlock_retries)
{
    MmGdbusModem *skeleton = NULL;
    GError *error = NULL;
    GVariant *previous_dictionary;
    MMUnlockRetries *previous_unlock_retries;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    previous_dictionary = mm_gdbus_modem_get_unlock_retries (skeleton);
    previous_unlock_retries = mm_unlock_retries_new_from_dictionary (previous_dictionary);

    if (error) {
        mm_warn ("Couldn't build previous unlock retries: '%s'", error->message);
        g_error_free (error);
    } else {
        /* If they are different, update */
        if (!mm_unlock_retries_cmp (unlock_retries, previous_unlock_retries)) {
            GVariant *new_dictionary;

            new_dictionary = mm_unlock_retries_get_dictionary (unlock_retries);
            mm_gdbus_modem_set_unlock_retries (skeleton, new_dictionary);
            g_variant_unref (new_dictionary);
        }
    }

    g_object_unref (previous_unlock_retries);
    g_object_unref (skeleton);
}

typedef enum {
    UPDATE_LOCK_INFO_CONTEXT_STEP_FIRST = 0,
    UPDATE_LOCK_INFO_CONTEXT_STEP_LOCK,
    UPDATE_LOCK_INFO_CONTEXT_STEP_RETRIES,
    UPDATE_LOCK_INFO_CONTEXT_STEP_AFTER_UNLOCK,
    UPDATE_LOCK_INFO_CONTEXT_STEP_LAST
} UpdateLockInfoContextStep;

typedef struct {
    MMIfaceModem *self;
    UpdateLockInfoContextStep step;
    guint pin_check_tries;
    guint pin_check_timeout_id;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
    MMModemLock lock;
    GError *saved_error;
} UpdateLockInfoContext;

static void
update_lock_info_context_complete_and_free (UpdateLockInfoContext *ctx)
{
    g_assert (ctx->saved_error == NULL);

    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_slice_free (UpdateLockInfoContext, ctx);
}

MMModemLock
mm_iface_modem_update_lock_info_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCK_UNKNOWN;

    return (MMModemLock) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void update_lock_info_context_step (UpdateLockInfoContext *ctx);

static void
load_unlock_retries_ready (MMIfaceModem *self,
                           GAsyncResult *res,
                           UpdateLockInfoContext *ctx)
{
    GError *error = NULL;
    MMUnlockRetries *unlock_retries;

    unlock_retries = MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_retries_finish (self, res, &error);
    if (!unlock_retries) {
        mm_warn ("Couldn't load unlock retries: '%s'", error->message);
        g_error_free (error);
    } else {
        /* Update the dictionary in the DBus interface */
        update_unlock_retries (self, unlock_retries);
        g_object_unref (unlock_retries);
    }

    /* Go on to next step */
    ctx->step++;
    update_lock_info_context_step (ctx);
}

static void
modem_after_sim_unlock_ready (MMIfaceModem *self,
                              GAsyncResult *res,
                              UpdateLockInfoContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_sim_unlock_finish (self, res, &error)) {
        mm_warn ("After SIM unlock failed setup: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    update_lock_info_context_step (ctx);
}

static gboolean
load_unlock_required_again (UpdateLockInfoContext *ctx)
{
    ctx->pin_check_timeout_id = 0;
    /* Retry the step */
    update_lock_info_context_step (ctx);
    return FALSE;
}

static void
load_unlock_required_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            UpdateLockInfoContext *ctx)
{
    GError *error = NULL;

    ctx->lock = MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_required_finish (self, res, &error);
    if (error) {
        /* Treat several SIM related, serial and other core errors as critical
         * and abort the checks. These will end up moving the modem to a FAILED
         * state. */
        if (error->domain == MM_SERIAL_ERROR ||
            g_error_matches (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_CANCELLED) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG)) {
            ctx->saved_error = error;
            ctx->step = UPDATE_LOCK_INFO_CONTEXT_STEP_LAST;
            update_lock_info_context_step (ctx);
            return;
        }

        mm_dbg ("Couldn't check if unlock required: '%s'", error->message);
        g_error_free (error);

        /* Retry up to 6 times */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE &&
            ++ctx->pin_check_tries < 6) {
            mm_dbg ("Retrying (%u) unlock required check", ctx->pin_check_tries);
            if (ctx->pin_check_timeout_id)
                g_source_remove (ctx->pin_check_timeout_id);
            ctx->pin_check_timeout_id = g_timeout_add_seconds (
                2,
                (GSourceFunc)load_unlock_required_again,
                ctx);
            return;
        }

        /* If reached max retries and still reporting error, set UNKNOWN */
        ctx->lock = MM_MODEM_LOCK_UNKNOWN;
    }

    /* Go on to next step */
    ctx->step++;
    update_lock_info_context_step (ctx);
}

static void
update_lock_info_context_step (UpdateLockInfoContext *ctx)
{
    switch (ctx->step) {
    case UPDATE_LOCK_INFO_CONTEXT_STEP_FIRST:
        /* We need the skeleton around */
        if (!ctx->skeleton) {
            ctx->saved_error = g_error_new (MM_CORE_ERROR,
                                            MM_CORE_ERROR_FAILED,
                                            "Couldn't get interface skeleton");
            ctx->step = UPDATE_LOCK_INFO_CONTEXT_STEP_LAST;
            update_lock_info_context_step (ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case UPDATE_LOCK_INFO_CONTEXT_STEP_LOCK:
        /* Don't re-ask if already known */
        if (ctx->lock == MM_MODEM_LOCK_UNKNOWN) {
            /* If we're already unlocked, we're done */
            if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required (
                    ctx->self,
                    (GAsyncReadyCallback)load_unlock_required_ready,
                    ctx);
                return;
            }

            /* Just assume that no lock is required */
            ctx->lock = MM_MODEM_LOCK_NONE;
        }

        /* Fall down to next step */
        ctx->step++;

    case UPDATE_LOCK_INFO_CONTEXT_STEP_RETRIES:
        /* Load unlock retries if possible */
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries (
                ctx->self,
                (GAsyncReadyCallback)load_unlock_retries_ready,
                ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case UPDATE_LOCK_INFO_CONTEXT_STEP_AFTER_UNLOCK:
        /* If we get that no lock is required, run the after SIM unlock step
         * in order to wait for the SIM to get ready */
        if (ctx->lock == MM_MODEM_LOCK_NONE ||
            ctx->lock == MM_MODEM_LOCK_SIM_PIN2 ||
            ctx->lock == MM_MODEM_LOCK_SIM_PUK2) {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_after_sim_unlock != NULL &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_after_sim_unlock_finish != NULL) {
                mm_dbg ("SIM is ready, running after SIM unlock step...");
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_after_sim_unlock (
                    ctx->self,
                    (GAsyncReadyCallback)modem_after_sim_unlock_ready,
                    ctx);
                return;
            }

            /* If no way to run after SIM unlock step, we're done */
            mm_dbg ("SIM is ready, and no need for the after SIM unlock step...");
        }

        /* Fall down to next step */
        ctx->step++;

    case UPDATE_LOCK_INFO_CONTEXT_STEP_LAST:
        if (ctx->saved_error) {
            /* Return saved error */
            g_simple_async_result_take_error (ctx->result, ctx->saved_error);
            ctx->saved_error = NULL;
        } else {
            /* Update lock status and modem status if needed */
            set_lock_status (ctx->self, ctx->skeleton, ctx->lock);

            g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                       GUINT_TO_POINTER (ctx->lock),
                                                       NULL);
        }

        update_lock_info_context_complete_and_free (ctx);
        return;

    default:
        g_assert_not_reached();
    }
}

void
mm_iface_modem_update_lock_info (MMIfaceModem *self,
                                 MMModemLock known_lock,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    UpdateLockInfoContext *ctx;

    ctx = g_slice_new0 (UpdateLockInfoContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_update_lock_info);
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);

    /* If the given lock is known, we will avoid re-asking for it */
    ctx->lock = known_lock;

    update_lock_info_context_step (ctx);
}

/*****************************************************************************/
/* Set power state sequence */

typedef struct {
    MMIfaceModem *self;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
    MMModemPowerState power_state;
    MMModemPowerState previous_power_state;
} SetPowerStateContext;

static void
set_power_state_context_complete_and_free (SetPowerStateContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_slice_free (SetPowerStateContext, ctx);
}

gboolean
mm_iface_modem_set_power_state_finish (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
modem_after_power_up_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            SetPowerStateContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_power_up_finish (self, res, &error);
    if (error)
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    set_power_state_context_complete_and_free (ctx);
}

static void
modem_power_up_ready (MMIfaceModem *self,
                      GAsyncResult *res,
                      SetPowerStateContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        set_power_state_context_complete_and_free (ctx);
        return;
    }

    mm_dbg ("Modem set in full-power mode...");
    mm_gdbus_modem_set_power_state (ctx->skeleton, ctx->power_state);

    /* If we have something to do just after power-up, do it */
    if (MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_power_up &&
        MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_power_up_finish) {
        MM_IFACE_MODEM_GET_INTERFACE (self)->modem_after_power_up (
            self,
            (GAsyncReadyCallback)modem_after_power_up_ready,
            ctx);
        return;
    }

    /* Otherwise, we're done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    set_power_state_context_complete_and_free (ctx);
}

static void
modem_power_down_ready (MMIfaceModem *self,
                        GAsyncResult *res,
                        SetPowerStateContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down_finish (self, res, &error);
    if (error)
        g_simple_async_result_take_error (ctx->result, error);
    else {
        mm_dbg ("Modem set in low-power mode...");
        mm_gdbus_modem_set_power_state (ctx->skeleton, ctx->power_state);
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    set_power_state_context_complete_and_free (ctx);
}

void
mm_iface_modem_set_power_state (MMIfaceModem *self,
                                MMModemPowerState power_state,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    SetPowerStateContext *ctx;

    ctx = g_slice_new0 (SetPowerStateContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_set_power_state);
    ctx->power_state = power_state;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        set_power_state_context_complete_and_free (ctx);
        return;
    }
    ctx->previous_power_state = mm_gdbus_modem_get_power_state (ctx->skeleton);

    /* Already done if we're in the desired power state */
    if (ctx->previous_power_state == ctx->power_state) {
        mm_dbg ("No need to change power state: already in '%s' power state",
                mm_modem_power_state_get_string (ctx->power_state));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        set_power_state_context_complete_and_free (ctx);
        return;
    }

    /* Supported transitions:
     * UNKNOWN|OFF|LOW --> ON
     * ON --> LOW
     */

    /* Going into low power mode? */
    if (ctx->power_state == MM_MODEM_POWER_STATE_LOW) {
        /* Error if unsupported */
        if (!MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down ||
            !MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down_finish) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Going into low-power mode is not supported by this modem");
            set_power_state_context_complete_and_free (ctx);
            return;
        }

        MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_down (MM_IFACE_MODEM (self),
                                                               (GAsyncReadyCallback)modem_power_down_ready,
                                                               ctx);
        return;
    }

    /* Going out of low power mode? */
    if (ctx->power_state == MM_MODEM_POWER_STATE_ON) {
        /* Error if unsupported */
        if (!MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up ||
            !MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up_finish) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Going into full-power mode is not supported by this modem");
            set_power_state_context_complete_and_free (ctx);
            return;
        }

        MM_IFACE_MODEM_GET_INTERFACE (self)->modem_power_up (MM_IFACE_MODEM (self),
                                                             (GAsyncReadyCallback)modem_power_up_ready,
                                                             ctx);
        return;
    }

    g_assert_not_reached ();
}

/*****************************************************************************/
/* MODEM DISABLING */

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_CURRENT_BANDS,
    DISABLING_STEP_ALLOWED_MODES,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModem *self;
    DisablingStep step;
    MMModemState previous_state;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_disable_finish (MMIfaceModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CURRENT_BANDS:
        /* Clear current bands */
        mm_gdbus_modem_set_bands (ctx->skeleton, mm_common_build_bands_unknown ());
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_ALLOWED_MODES:
        /* Clear allowed/preferred modes */
        mm_gdbus_modem_set_allowed_modes (ctx->skeleton, MM_MODEM_MODE_NONE);
        mm_gdbus_modem_set_preferred_mode (ctx->skeleton, MM_MODEM_MODE_NONE);
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_disable (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_disable);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  MM_IFACE_MODEM_STATE, &ctx->previous_state,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        disabling_context_complete_and_free (ctx);
        return;
    }

    interface_disabling_step (ctx);
}

/*****************************************************************************/
/* MODEM ENABLING */

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SET_POWER_STATE,
    ENABLING_STEP_FLOW_CONTROL,
    ENABLING_STEP_SUPPORTED_CHARSETS,
    ENABLING_STEP_CHARSET,
    ENABLING_STEP_ALLOWED_MODES,
    ENABLING_STEP_CURRENT_BANDS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModem *self;
    EnablingStep step;
    MMModemCharset supported_charsets;
    const MMModemCharset *current_charset;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MmGdbusModem *skeleton;
};

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
enabling_context_complete_and_free_if_cancelled (EnablingContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface enabling cancelled");
    enabling_context_complete_and_free (ctx);
    return TRUE;
}

gboolean
mm_iface_modem_enable_finish (MMIfaceModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
enabling_set_power_state_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                EnablingContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_set_power_state_finish (self, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_flow_control_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_GET_INTERFACE (self)->setup_flow_control_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
load_supported_charsets_ready (MMIfaceModem *self,
                               GAsyncResult *res,
                               EnablingContext *ctx)
{
    GError *error = NULL;

    ctx->supported_charsets =
        MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_charsets_finish (self, res, &error);
    if (error) {
        mm_warn ("couldn't load Supported Charsets: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_charset_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     EnablingContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->setup_charset_finish (self, res, &error)) {
        mm_dbg ("couldn't set charset '%s': '%s'",
                mm_modem_charset_to_string (*ctx->current_charset),
                error->message);
        g_error_free (error);

        /* Will retry step with some other charset type */
    } else
        /* Done, Go on to next step */
        ctx->step++;

    interface_enabling_step (ctx);
}

static void
load_allowed_modes_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          EnablingContext *ctx)
{
    MMModemMode allowed = MM_MODEM_MODE_NONE;
    MMModemMode preferred = MM_MODEM_MODE_NONE;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_allowed_modes_finish (self,
                                                                         res,
                                                                         &allowed,
                                                                         &preferred,
                                                                         &error)) {
        /* Errors when getting allowed/preferred won't be critical */
        mm_warn ("couldn't load current allowed/preferred modes: '%s'", error->message);
        g_error_free (error);

        /* If errors getting allowed modes, assume allowed=supported,
         * and none preferred */
        allowed = mm_gdbus_modem_get_supported_modes (ctx->skeleton);
        preferred = MM_MODEM_MODE_NONE;
    }

    mm_gdbus_modem_set_allowed_modes (ctx->skeleton, allowed);
    mm_gdbus_modem_set_preferred_mode (ctx->skeleton, preferred);

    /* Done, Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
load_current_bands_ready (MMIfaceModem *self,
                          GAsyncResult *res,
                          EnablingContext *ctx)
{
    GArray *current_bands;
    GArray *filtered_bands;
    GArray *supported_bands;
    GError *error = NULL;

    current_bands = MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_bands_finish (self, res, &error);

    supported_bands = (mm_common_bands_variant_to_garray (
                           mm_gdbus_modem_get_supported_bands (ctx->skeleton)));
    filtered_bands = mm_filter_current_bands (supported_bands, current_bands);
    if (current_bands)
        g_array_unref (current_bands);
    if (supported_bands)
        g_array_unref (supported_bands);

    if (filtered_bands) {
        mm_gdbus_modem_set_bands (ctx->skeleton,
                                  mm_common_bands_garray_to_variant (filtered_bands));
        g_array_unref (filtered_bands);
    } else
        mm_gdbus_modem_set_bands (ctx->skeleton, mm_common_build_bands_unknown ());

    /* Errors when getting current bands won't be critical */
    if (error) {
        mm_warn ("couldn't load current Bands: '%s'", error->message);
        g_error_free (error);
    }

    /* Done, Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
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
interface_enabling_step (EnablingContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (enabling_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SET_POWER_STATE:
        mm_iface_modem_set_power_state (ctx->self,
                                        MM_MODEM_POWER_STATE_ON,
                                        (GAsyncReadyCallback)enabling_set_power_state_ready,
                                        ctx);
        return;

    case ENABLING_STEP_FLOW_CONTROL:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_flow_control &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_flow_control_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_flow_control (
                ctx->self,
                (GAsyncReadyCallback)setup_flow_control_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SUPPORTED_CHARSETS:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_charsets &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_charsets_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_charsets (
                ctx->self,
                (GAsyncReadyCallback)load_supported_charsets_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_CHARSET:
        /* Only try to set charsets if we were able to load supported ones */
        if (ctx->supported_charsets > 0 &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_charset &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_charset_finish) {
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
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_charset (
                    ctx->self,
                    *ctx->current_charset,
                    (GAsyncReadyCallback)setup_charset_ready,
                    ctx);
                return;
            }

            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Failed to find a usable modem character set");
            enabling_context_complete_and_free (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ALLOWED_MODES:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_allowed_modes &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_allowed_modes_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_allowed_modes (
                    ctx->self,
                    (GAsyncReadyCallback)load_allowed_modes_ready,
                    ctx);
                return;
        }

        /* If no way to get allowed modes, assume allowed=supported,
         * and none preferred */
        mm_gdbus_modem_set_allowed_modes (ctx->skeleton,
                                          mm_gdbus_modem_get_supported_modes (ctx->skeleton));
        mm_gdbus_modem_set_preferred_mode (ctx->skeleton, MM_MODEM_MODE_NONE);
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_CURRENT_BANDS:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_bands &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_bands_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_bands (
                    ctx->self,
                    (GAsyncReadyCallback)load_current_bands_ready,
                    ctx);
                return;
        } else
            mm_gdbus_modem_set_bands (ctx->skeleton, mm_common_build_bands_unknown ());
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        enabling_context_complete_and_free (ctx);
        return;
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

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_enable);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        enabling_context_complete_and_free (ctx);
        return;
    }

    interface_enabling_step (ctx);
}

/*****************************************************************************/
/* MODEM INITIALIZATION */

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CURRENT_CAPABILITIES,
    INITIALIZATION_STEP_MODEM_CAPABILITIES,
    INITIALIZATION_STEP_BEARERS,
    INITIALIZATION_STEP_MANUFACTURER,
    INITIALIZATION_STEP_MODEL,
    INITIALIZATION_STEP_REVISION,
    INITIALIZATION_STEP_EQUIPMENT_ID,
    INITIALIZATION_STEP_DEVICE_ID,
    INITIALIZATION_STEP_SUPPORTED_MODES,
    INITIALIZATION_STEP_SUPPORTED_BANDS,
    INITIALIZATION_STEP_POWER_STATE,
    INITIALIZATION_STEP_UNLOCK_REQUIRED,
    INITIALIZATION_STEP_SIM,
    INITIALIZATION_STEP_OWN_NUMBERS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem *self;
    InitializationStep step;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MmGdbusModem *skeleton;
    GError *fatal_error;
};

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_assert (ctx->fatal_error == NULL);
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
initialization_context_complete_and_free_if_cancelled (InitializationContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface initialization cancelled");
    initialization_context_complete_and_free (ctx);
    return TRUE;
}

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY)                                \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem *self,                            \
                         GAsyncResult *res,                             \
                         InitializationContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
        gchar *val;                                                     \
                                                                        \
        val = MM_IFACE_MODEM_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_modem_set_##NAME (ctx->skeleton, val);                 \
        g_free (val);                                                   \
                                                                        \
        if (error) {                                                    \
            mm_warn ("couldn't load %s: '%s'", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (ctx);                            \
    }

#undef UINT_REPLY_READY_FN
#define UINT_REPLY_READY_FN(NAME,DISPLAY,FATAL)                         \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem *self,                            \
                         GAsyncResult *res,                             \
                         InitializationContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        mm_gdbus_modem_set_##NAME (                                     \
            ctx->skeleton,                                              \
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error)); \
                                                                        \
        if (error) {                                                    \
            if (FATAL) {                                                \
                g_propagate_error (&ctx->fatal_error, error);           \
                g_prefix_error (&ctx->fatal_error, "couldn't load %s: ", DISPLAY); \
                /* Jump to the last step */                             \
                ctx->step = INITIALIZATION_STEP_LAST;                   \
            } else {                                                    \
                mm_warn ("couldn't load %s: '%s'", DISPLAY, error->message); \
                g_error_free (error);                                   \
                /* Go on to next step */                                \
                ctx->step++;                                            \
            }                                                           \
        } else {                                                        \
            /* Go on to next step */                                    \
            ctx->step++;                                                \
        }                                                               \
                                                                        \
        interface_initialization_step (ctx);                            \
    }

UINT_REPLY_READY_FN (current_capabilities, "Current Capabilities", TRUE)
UINT_REPLY_READY_FN (modem_capabilities, "Modem Capabilities", FALSE)
STR_REPLY_READY_FN (manufacturer, "Manufacturer")
STR_REPLY_READY_FN (model, "Model")
STR_REPLY_READY_FN (revision, "Revision")
STR_REPLY_READY_FN (equipment_identifier, "Equipment Identifier")
STR_REPLY_READY_FN (device_identifier, "Device Identifier")

static void
load_own_numbers_ready (MMIfaceModem *self,
                        GAsyncResult *res,
                        InitializationContext *ctx)
{
    GError *error = NULL;
    GStrv str_list;

    str_list = MM_IFACE_MODEM_GET_INTERFACE (self)->load_own_numbers_finish (self, res, &error);
    if (error) {
        mm_warn ("couldn't load list of Own Numbers: '%s'", error->message);
        g_error_free (error);
    }

    if (str_list) {
        mm_gdbus_modem_set_own_numbers (ctx->skeleton, (const gchar *const *) str_list);
        g_strfreev (str_list);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
load_supported_modes_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            InitializationContext *ctx)
{
    GError *error = NULL;
    MMModemMode modes;

    modes = MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_modes_finish (self, res, &error);

    if (modes != MM_MODEM_MODE_NONE) {
        mm_gdbus_modem_set_supported_modes (ctx->skeleton, modes);
        mm_gdbus_modem_set_allowed_modes (ctx->skeleton, modes);
    }

    if (error) {
        mm_warn ("couldn't load Supported Modes: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
load_supported_bands_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            InitializationContext *ctx)
{
    GError *error = NULL;
    GArray *bands_array;

    bands_array = MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_bands_finish (self, res, &error);

    if (bands_array) {
        mm_gdbus_modem_set_supported_bands (ctx->skeleton,
                                            mm_common_bands_garray_to_variant (bands_array));
        g_array_unref (bands_array);
    }

    if (error) {
        mm_warn ("couldn't load Supported Bands: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

UINT_REPLY_READY_FN (power_state, "Power State", FALSE)

static void
modem_update_lock_info_ready (MMIfaceModem *self,
                              GAsyncResult *res,
                              InitializationContext *ctx)
{
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

    interface_initialization_step (ctx);
}

static void
sim_new_ready (GAsyncInitable *initable,
               GAsyncResult *res,
               InitializationContext *ctx)
{
    MMSim *sim;
    GError *error = NULL;

    sim = MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->create_sim_finish (ctx->self, res, &error);
    if (error) {
        mm_warn ("couldn't create SIM: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        initialization_context_complete_and_free (ctx);
        return;
    }

    /* We may get error with !sim, when the implementation doesn't want to
     * handle any (e.g. CDMA) */
    if (sim) {
        g_object_bind_property (sim, MM_SIM_PATH,
                                ctx->skeleton, "sim",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (ctx->self,
                      MM_IFACE_MODEM_SIM, sim,
                      NULL);
        g_object_unref (sim);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
sim_reinit_ready (MMSim *sim,
                  GAsyncResult *res,
                  InitializationContext *ctx)
{
    GError *error = NULL;

    if (!mm_sim_initialize_finish (sim, res, &error)) {
        mm_warn ("SIM re-initialization failed: '%s'",
                 error ? error->message : "Unknown error");
        g_clear_error (&error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization_step (InitializationContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (initialization_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Load device if not done before */
        if (!mm_gdbus_modem_get_device (ctx->skeleton)) {
            gchar *device;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_DEVICE, &device,
                          NULL);
            mm_gdbus_modem_set_device (ctx->skeleton, device);
            g_free (device);
        }
        /* Load driver if not done before */
        if (!mm_gdbus_modem_get_drivers (ctx->skeleton)) {
            gchar **drivers;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_DRIVERS, &drivers,
                          NULL);
            mm_gdbus_modem_set_drivers (ctx->skeleton, (const gchar * const *)drivers);
            g_strfreev (drivers);
        }
        /* Load plugin if not done before */
        if (!mm_gdbus_modem_get_plugin (ctx->skeleton)) {
            gchar *plugin;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_PLUGIN, &plugin,
                          NULL);
            mm_gdbus_modem_set_plugin (ctx->skeleton, plugin);
            g_free (plugin);
        }
        /* Load primary port if not done before */
        if (!mm_gdbus_modem_get_primary_port (ctx->skeleton)) {
            MMPort *primary = NULL;

#if defined WITH_QMI
            primary = MM_PORT (mm_base_modem_peek_port_qmi (MM_BASE_MODEM (ctx->self)));
#endif
#if defined WITH_MBIM
            if (!primary)
                primary = MM_PORT (mm_base_modem_peek_port_mbim (MM_BASE_MODEM (ctx->self)));
#endif
            if (!primary)
                primary = MM_PORT (mm_base_modem_peek_port_primary (MM_BASE_MODEM (ctx->self)));

            g_assert (primary != NULL);
            mm_gdbus_modem_set_primary_port (ctx->skeleton, mm_port_get_device (primary));
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CURRENT_CAPABILITIES:
        /* Current capabilities may change during runtime, i.e. if new firmware reloaded; but we'll
         * try to handle that by making sure the capabilities are cleared when the new firmware is
         * reloaded. So if we're asked to re-initialize, if we already have current capabilities loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_current_capabilities (ctx->skeleton) == MM_MODEM_CAPABILITY_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities (
                ctx->self,
                (GAsyncReadyCallback)load_current_capabilities_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_MODEM_CAPABILITIES:
        /* Modem capabilities are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_modem_capabilities (ctx->skeleton) == MM_MODEM_CAPABILITY_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities (
                ctx->self,
                (GAsyncReadyCallback)load_modem_capabilities_ready,
                ctx);
            return;
        }
       /* If no specific way of getting modem capabilities, assume they are
        * equal to the current capabilities */
        mm_gdbus_modem_set_modem_capabilities (
            ctx->skeleton,
            mm_gdbus_modem_get_current_capabilities (ctx->skeleton));
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_BEARERS: {
        MMBearerList *list = NULL;

        /* Bearers setup is meant to be loaded only once during the whole
         * lifetime of the modem. The list may have been created by the object
         * implementing the interface; if so use it. */
        g_object_get (ctx->self,
                      MM_IFACE_MODEM_BEARER_LIST, &list,
                      NULL);

        if (!list) {
            guint n;

            /* The maximum number of available/connected modems is guessed from
             * the size of the data ports list. */
            n = g_list_length (mm_base_modem_peek_data_ports (MM_BASE_MODEM (ctx->self)));
            mm_dbg ("Modem allows up to %u bearers", n);

            /* Create new default list */
            list = mm_bearer_list_new (n, n);
            g_object_set (ctx->self,
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

        /* Fall down to next step */
        ctx->step++;
    }

    case INITIALIZATION_STEP_MANUFACTURER:
        /* Manufacturer is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_manufacturer (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer (
                ctx->self,
                (GAsyncReadyCallback)load_manufacturer_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_MODEL:
        /* Model is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_model (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model (
                ctx->self,
                (GAsyncReadyCallback)load_model_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_REVISION:
        /* Revision is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_revision (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision (
                ctx->self,
                (GAsyncReadyCallback)load_revision_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_EQUIPMENT_ID:
        /* Equipment ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_equipment_identifier (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier (
                ctx->self,
                (GAsyncReadyCallback)load_equipment_identifier_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_DEVICE_ID:
        /* Device ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_device_identifier (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier (
                ctx->self,
                (GAsyncReadyCallback)load_device_identifier_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_SUPPORTED_MODES:
        g_assert (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes != NULL);
        g_assert (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes_finish != NULL);

        /* Supported modes are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_supported_modes (ctx->skeleton) == MM_MODEM_MODE_NONE) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes (
                ctx->self,
                (GAsyncReadyCallback)load_supported_modes_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_SUPPORTED_BANDS: {
        GArray *supported_bands;

        supported_bands = (mm_common_bands_variant_to_garray (
                               mm_gdbus_modem_get_supported_bands (ctx->skeleton)));

        /* Supported bands are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (supported_bands->len == 0 ||
            g_array_index (supported_bands, MMModemBand, 0)  == MM_MODEM_BAND_UNKNOWN) {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands (
                    ctx->self,
                    (GAsyncReadyCallback)load_supported_bands_ready,
                    ctx);
                g_array_unref (supported_bands);
                return;
            }

            /* Loading supported bands not implemented, default to UNKNOWN */
            mm_gdbus_modem_set_supported_bands (ctx->skeleton, mm_common_build_bands_unknown ());
            mm_gdbus_modem_set_bands (ctx->skeleton, mm_common_build_bands_unknown ());
        }
        g_array_unref (supported_bands);

        /* Fall down to next step */
        ctx->step++;
    }

    case INITIALIZATION_STEP_POWER_STATE:
        /* Initial power state is meant to be loaded only once. Therefore, if we
         * already have it loaded, don't try to load it again. */
        if (mm_gdbus_modem_get_power_state (ctx->skeleton) == MM_MODEM_POWER_STATE_UNKNOWN) {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_power_state &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_power_state_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_power_state (
                    ctx->self,
                    (GAsyncReadyCallback)load_power_state_ready,
                    ctx);
                return;
            }

            /* We don't know how to load current power state; assume ON */
            mm_gdbus_modem_set_power_state (ctx->skeleton, MM_MODEM_POWER_STATE_ON);
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_UNLOCK_REQUIRED:
        /* Only check unlock required if we were previously not unlocked */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE) {
            mm_iface_modem_update_lock_info (ctx->self,
                                             MM_MODEM_LOCK_UNKNOWN, /* ask */
                                             (GAsyncReadyCallback)modem_update_lock_info_ready,
                                             ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_SIM:
        /* If the modem doesn't need any SIM (not implemented by plugin, or not
         * needed in CDMA-only modems) */
        if (!mm_iface_modem_is_cdma_only (ctx->self) &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->create_sim &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->create_sim_finish) {
            MMSim *sim = NULL;

            g_object_get (ctx->self,
                          MM_IFACE_MODEM_SIM, &sim,
                          NULL);
            if (!sim) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->create_sim (
                    MM_IFACE_MODEM (ctx->self),
                    (GAsyncReadyCallback)sim_new_ready,
                    ctx);
                return;
            }

            /* If already available the sim object, relaunch initialization.
             * This will try to load any missing property value that couldn't be
             * retrieved before due to having the SIM locked. */
            mm_sim_initialize (sim,
                               NULL, /* TODO: cancellable */
                               (GAsyncReadyCallback)sim_reinit_ready,
                               ctx);
            g_object_unref (sim);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_OWN_NUMBERS:
        /* Own numbers is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_own_numbers (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_own_numbers &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_own_numbers_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_own_numbers (
                ctx->self,
                (GAsyncReadyCallback)load_own_numbers_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        if (ctx->fatal_error) {
            g_simple_async_result_take_error (ctx->result, ctx->fatal_error);
            ctx->fatal_error = NULL;
        } else {
            /* We are done without errors!
             * Handle method invocations */
            g_signal_connect (ctx->skeleton,
                              "handle-create-bearer",
                              G_CALLBACK (handle_create_bearer),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-command",
                              G_CALLBACK (handle_command),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-delete-bearer",
                              G_CALLBACK (handle_delete_bearer),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-list-bearers",
                              G_CALLBACK (handle_list_bearers),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-enable",
                              G_CALLBACK (handle_enable),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-set-power-state",
                              G_CALLBACK (handle_set_power_state),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-reset",
                              G_CALLBACK (handle_reset),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-factory-reset",
                              G_CALLBACK (handle_factory_reset),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-set-bands",
                              G_CALLBACK (handle_set_bands),
                              ctx->self);
            g_signal_connect (ctx->skeleton,
                              "handle-set-allowed-modes",
                              G_CALLBACK (handle_set_allowed_modes),
                              ctx->self);
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        }

        /* Finally, export the new interface, even if we got errors, but only if not
         * done already */
        if (!mm_gdbus_object_peek_modem (MM_GDBUS_OBJECT (ctx->self)))
            mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                MM_GDBUS_MODEM (ctx->skeleton));
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_initialize_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_initialize (MMIfaceModem *self,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModem *skeleton = NULL;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem_set_sim (skeleton, NULL);
        mm_gdbus_modem_set_current_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
        mm_gdbus_modem_set_modem_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
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
        mm_gdbus_modem_set_supported_modes (skeleton, MM_MODEM_MODE_NONE);
        mm_gdbus_modem_set_allowed_modes (skeleton, MM_MODEM_MODE_NONE);
        mm_gdbus_modem_set_preferred_mode (skeleton, MM_MODEM_MODE_NONE);
        mm_gdbus_modem_set_supported_bands (skeleton, mm_common_build_bands_unknown ());
        mm_gdbus_modem_set_bands (skeleton, mm_common_build_bands_unknown ());
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
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_initialize);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    interface_initialization_step (ctx);
}

void
mm_iface_modem_shutdown (MMIfaceModem *self)
{
    /* Remove SignalQualityCheckContext object to make sure any pending
     * invocation of periodic_signal_quality_check is cancelled before
     * SignalQualityUpdateContext is removed (as signal_quality_check_ready may
     * call update_signal_quality). */
    if (G_LIKELY (signal_quality_check_context_quark))
        g_object_set_qdata (G_OBJECT (self),
                            signal_quality_check_context_quark,
                            NULL);

    /* Remove SignalQualityUpdateContext object to make sure any pending
     * invocation of expire_signal_quality is canceled before the DBus skeleton
     * is removed. */
    if (G_LIKELY (signal_quality_update_context_quark))
        g_object_set_qdata (G_OBJECT (self),
                            signal_quality_update_context_quark,
                            NULL);

    /* Remove AccessTechnologiesCheckContext object to make sure any pending
     * invocation of periodic_access_technologies_check is canceled before the
     * DBus skeleton is removed. */
    if (G_LIKELY (access_technologies_check_context_quark))
        g_object_set_qdata (G_OBJECT (self),
                            access_technologies_check_context_quark,
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

MMModemMode
mm_iface_modem_get_supported_modes (MMIfaceModem *self)
{
    MMModemMode supported = MM_MODEM_MODE_NONE;
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    if (skeleton) {
        supported = mm_gdbus_modem_get_supported_modes (skeleton);
        g_object_unref (skeleton);
    }

    return supported;
}

gboolean
mm_iface_modem_is_2g (MMIfaceModem *self)
{
    return (mm_iface_modem_get_supported_modes (self) & MM_MODEM_MODE_2G);
}

gboolean
mm_iface_modem_is_2g_only (MMIfaceModem *self)
{
    MMModemMode supported;

    supported = mm_iface_modem_get_supported_modes (self);
    return !((MM_MODEM_MODE_2G ^ supported) & supported);
}

gboolean
mm_iface_modem_is_3g (MMIfaceModem *self)
{
    return (mm_iface_modem_get_supported_modes (self) & MM_MODEM_MODE_3G);
}

gboolean
mm_iface_modem_is_3g_only (MMIfaceModem *self)
{
    MMModemMode supported;

    supported = mm_iface_modem_get_supported_modes (self);
    return !((MM_MODEM_MODE_3G ^ supported) & supported);
}

gboolean
mm_iface_modem_is_4g (MMIfaceModem *self)
{
    return (mm_iface_modem_get_supported_modes (self) & MM_MODEM_MODE_4G);
}

gboolean
mm_iface_modem_is_4g_only (MMIfaceModem *self)
{
    MMModemMode supported;

    supported = mm_iface_modem_get_supported_modes (self);
    return !((MM_MODEM_MODE_4G ^ supported) & supported);
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
    return (mm_iface_modem_get_current_capabilities (self) & MM_MODEM_CAPABILITY_3GPP_LTE);
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
    return !((MM_MODEM_CAPABILITY_3GPP ^ capabilities) & capabilities);
}

gboolean
mm_iface_modem_is_3gpp_lte_only (MMIfaceModem *self)
{
    MMModemCapability capabilities;

    capabilities = mm_iface_modem_get_current_capabilities (self);
    return !((MM_MODEM_CAPABILITY_3GPP_LTE ^ capabilities) & capabilities);
}

gboolean
mm_iface_modem_is_cdma_only (MMIfaceModem *self)
{
    MMModemCapability capabilities;

    capabilities = mm_iface_modem_get_current_capabilities (self);
    return !((MM_MODEM_CAPABILITY_CDMA_EVDO ^ capabilities) & capabilities);
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
                              MM_TYPE_SIM,
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
