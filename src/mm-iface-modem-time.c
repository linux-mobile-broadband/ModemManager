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
 * Copyright (C) 2012 Google, Inc.
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-time.h"
#include "mm-log-object.h"

#define SUPPORT_CHECKED_TAG          "time-support-checked-tag"
#define SUPPORTED_TAG                "time-supported-tag"
#define NETWORK_TIMEZONE_CONTEXT_TAG "time-network-timezone-context"

static GQuark support_checked_quark;
static GQuark supported_quark;
static GQuark network_timezone_context_quark;

/*****************************************************************************/

void
mm_iface_modem_time_bind_simple_status (MMIfaceModemTime *self,
                                        MMSimpleStatus *status)
{
}

/*****************************************************************************/

typedef struct {
    GDBusMethodInvocation *invocation;
    MmGdbusModemTime *skeleton;
    MMIfaceModemTime *self;
} HandleGetNetworkTimeContext;

static void
handle_get_network_time_context_free (HandleGetNetworkTimeContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
load_network_time_ready (MMIfaceModemTime            *self,
                         GAsyncResult                *res,
                         HandleGetNetworkTimeContext *ctx)
{
    gchar *time_str;
    GError *error = NULL;

    time_str = MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_time_finish (self, res, &error);
    if (error)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_time_complete_get_network_time (ctx->skeleton, ctx->invocation, time_str);
    g_free (time_str);
    handle_get_network_time_context_free (ctx);
}

static void
handle_get_network_time_auth_ready (MMBaseModem                 *self,
                                    GAsyncResult                *res,
                                    HandleGetNetworkTimeContext *ctx)
{
    MMModemState  state;
    GError       *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_get_network_time_context_free (ctx);
        return;
    }

    state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);
    /* If we're not yet registered, we cannot get the network time */
    if (state < MM_MODEM_STATE_REGISTERED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot load network time: "
                                               "not registered yet");
        handle_get_network_time_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_time ||
        !MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_time_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot load network time: "
                                               "operation not supported");
        handle_get_network_time_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_time (
        ctx->self,
        (GAsyncReadyCallback)load_network_time_ready,
        ctx);
}

static gboolean
handle_get_network_time (MmGdbusModemTime      *skeleton,
                         GDBusMethodInvocation *invocation,
                         MMIfaceModemTime      *self)
{
    HandleGetNetworkTimeContext *ctx;

    ctx = g_new (HandleGetNetworkTimeContext, 1);
    ctx->invocation = g_object_ref (invocation);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_TIME,
                             (GAsyncReadyCallback)handle_get_network_time_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Network timezone loading */

/*
 * As soon as we're registered in a network, we try to check timezone
 * information up to NETWORK_TIMEZONE_POLL_RETRIES times. As soon as one of
 * the retries succeeds, we stop polling as we don't expect the timezone
 * information to change while registered in the same network.
 */
#define NETWORK_TIMEZONE_POLL_INTERVAL_SEC 5
#define NETWORK_TIMEZONE_POLL_RETRIES 6

typedef struct {
    gulong state_changed_id;
    MMModemState state;
    guint network_timezone_poll_id;
    guint network_timezone_poll_retries;
} NetworkTimezoneContext;

static void
network_timezone_context_free (NetworkTimezoneContext *ctx)
{
    /* Note: no need to remove signal connection here, we have already done it
     * in stop_network_timezone() when the logic is disabled (or will be done
     * automatically when the last modem object reference is dropped) */
    if (ctx->network_timezone_poll_id)
        g_source_remove (ctx->network_timezone_poll_id);
    g_free (ctx);
}

static gboolean network_timezone_poll_cb (MMIfaceModemTime *self);

static void
update_network_timezone_dictionary (MMIfaceModemTime *self,
                                    MMNetworkTimezone *tz)
{
    MmGdbusModemTime *skeleton = NULL;
    GVariant *dictionary;

    g_object_get (self,
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    dictionary = mm_network_timezone_get_dictionary (tz);
    mm_gdbus_modem_time_set_network_timezone (skeleton, dictionary);
    if (dictionary)
        g_variant_unref (dictionary);

    g_object_unref (skeleton);
}

static void
load_network_timezone_ready (MMIfaceModemTime *self,
                             GAsyncResult *res)
{
    g_autoptr(GError)  error = NULL;
    MMNetworkTimezone *tz;

    /* Finish the async operation */
    tz = MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_timezone_finish (self, res, &error);
    if (!tz) {
        NetworkTimezoneContext *ctx;

        mm_obj_dbg (self, "couldn't load network timezone: %s", error->message);

        /* Note: may be NULL if the polling has been removed while processing the async operation */
        ctx = (NetworkTimezoneContext *) g_object_get_qdata (G_OBJECT (self), network_timezone_context_quark);
        if (!ctx)
            return;

        /* Retry? */
        ctx->network_timezone_poll_retries--;

        /* If no more retries, we don't do anything else */
        if (ctx->network_timezone_poll_retries == 0 ||
            !g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY)) {
            mm_obj_warn (self, "couldn't load network timezone from the current network");
            return;
        }

        /* Otherwise, relaunch timeout to query a bit later */
        ctx->network_timezone_poll_id = g_timeout_add_seconds (NETWORK_TIMEZONE_POLL_INTERVAL_SEC,
                                                               (GSourceFunc)network_timezone_poll_cb,
                                                               self);
        return;
    }

    /* Got final result properly, update the property in the skeleton */
    update_network_timezone_dictionary (self, tz);
    g_object_unref (tz);
}

static gboolean
network_timezone_poll_cb (MMIfaceModemTime *self)
{
    NetworkTimezoneContext *ctx;

    ctx = (NetworkTimezoneContext *) g_object_get_qdata (G_OBJECT (self), network_timezone_context_quark);
    ctx->network_timezone_poll_id = 0;

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_timezone (
        self,
        (GAsyncReadyCallback)load_network_timezone_ready,
        NULL);

    return G_SOURCE_REMOVE;
}

static void
start_network_timezone_poll (MMIfaceModemTime *self)
{
    NetworkTimezoneContext *ctx;

    ctx = (NetworkTimezoneContext *) g_object_get_qdata (G_OBJECT (self), network_timezone_context_quark);

    mm_obj_dbg (self, "network timezone polling started");
    ctx->network_timezone_poll_retries = NETWORK_TIMEZONE_POLL_RETRIES;
    ctx->network_timezone_poll_id = g_timeout_add_seconds (NETWORK_TIMEZONE_POLL_INTERVAL_SEC, (GSourceFunc)network_timezone_poll_cb, self);
}

static void
stop_network_timezone_poll (MMIfaceModemTime *self)
{
    NetworkTimezoneContext *ctx;

    ctx = (NetworkTimezoneContext *) g_object_get_qdata (G_OBJECT (self), network_timezone_context_quark);

    if (ctx->network_timezone_poll_id) {
        mm_obj_dbg (self, "network timezone polling stopped");
        g_source_remove (ctx->network_timezone_poll_id);
        ctx->network_timezone_poll_id = 0;
    }
}

static void
network_timezone_state_changed (MMIfaceModemTime *self)
{
    NetworkTimezoneContext *ctx;
    MMModemState old_state;

    ctx = (NetworkTimezoneContext *) g_object_get_qdata (G_OBJECT (self), network_timezone_context_quark);
    old_state = ctx->state;

    g_object_get (self, MM_IFACE_MODEM_STATE, &ctx->state, NULL);

    /* If going from unregistered to registered, start polling */
    if (ctx->state >= MM_MODEM_STATE_REGISTERED && old_state < MM_MODEM_STATE_REGISTERED) {
        start_network_timezone_poll (self);
        return;
    }

    /* If going from registered to unregistered, stop polling */
    if (ctx->state < MM_MODEM_STATE_REGISTERED && old_state >= MM_MODEM_STATE_REGISTERED) {
        stop_network_timezone_poll (self);
        return;
    }
}

static void
stop_network_timezone (MMIfaceModemTime *self)
{
    NetworkTimezoneContext *ctx;

    ctx = (NetworkTimezoneContext *) g_object_get_qdata (G_OBJECT (self), network_timezone_context_quark);
    if (ctx) {
        /* Remove signal connection and then trigger context free */
        if (ctx->state_changed_id) {
            g_signal_handler_disconnect (self, ctx->state_changed_id);
            ctx->state_changed_id = 0;
        }
        g_object_set_qdata (G_OBJECT (self), network_timezone_context_quark, NULL);
    }
}

static void
start_network_timezone (MMIfaceModemTime *self)
{
    NetworkTimezoneContext *ctx;

    /* If loading network timezone not supported, just finish here */
    if (!MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_timezone ||
        !MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_timezone_finish) {
        mm_obj_dbg (self, "loading network timezone is not supported");
        return;
    }

    if (G_UNLIKELY (!network_timezone_context_quark))
        network_timezone_context_quark = (g_quark_from_static_string (NETWORK_TIMEZONE_CONTEXT_TAG));

    /* Cleanup context properly if it already exists, including the signal handler */
    stop_network_timezone (self);

    ctx = g_new0 (NetworkTimezoneContext, 1);
    g_object_set_qdata_full (G_OBJECT (self),
                             network_timezone_context_quark,
                             ctx,
                             (GDestroyNotify)network_timezone_context_free);

    /* Want to get notified when modem state changes to enable/disable
     * the polling logic. This signal is connected as long as the network timezone
     * logic is enabled. */
    g_object_get (self, MM_IFACE_MODEM_STATE, &ctx->state, NULL);
    ctx->state_changed_id = g_signal_connect (self,
                                              "notify::" MM_IFACE_MODEM_STATE,
                                              G_CALLBACK (network_timezone_state_changed),
                                              NULL);

    /* If we're registered already, start timezone poll */
    if (ctx->state >= MM_MODEM_STATE_REGISTERED)
        start_network_timezone_poll (self);
}

/*****************************************************************************/

void
mm_iface_modem_time_update_network_time (MMIfaceModemTime *self,
                                         const gchar *network_time)
{
    MmGdbusModemTime *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    /* Notify about the updated network time */
    mm_gdbus_modem_time_emit_network_time_changed (skeleton, network_time);

    g_object_unref (skeleton);
}

void
mm_iface_modem_time_update_network_timezone (MMIfaceModemTime  *self,
                                             MMNetworkTimezone *tz)
{
    MmGdbusModemTime *skeleton;
    GVariant         *dictionary;

    g_object_get (self,
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    dictionary = mm_network_timezone_get_dictionary (tz);
    mm_gdbus_modem_time_set_network_timezone (skeleton, dictionary);
    if (dictionary)
        g_variant_unref (dictionary);

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (GTask *task);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_CANCEL_NETWORK_TIMEZONE_UPDATE,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    DisablingStep step;
    MmGdbusModemTime *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_time_disable_finish (MMIfaceModemTime *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModemTime *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->disable_unsolicited_events_finish (self, res, &error);
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
cleanup_unsolicited_events_ready (MMIfaceModemTime *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
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
    MMIfaceModemTime *self;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CANCEL_NETWORK_TIMEZONE_UPDATE:
        /* Stop and cleanup context */
        stop_network_timezone (self);
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->cleanup_unsolicited_events (
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
mm_iface_modem_time_disable (MMIfaceModemTime *self,
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
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, &ctx->skeleton,
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
    ENABLING_STEP_SETUP_NETWORK_TIMEZONE_RETRIEVAL,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModemTime *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_time_enable_finish (MMIfaceModemTime *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModemTime *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
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
enable_unsolicited_events_ready (MMIfaceModemTime *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    /* Not critical! */
    if (!MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error)) {
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
    MMIfaceModemTime *self;
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

    case ENABLING_STEP_SETUP_NETWORK_TIMEZONE_RETRIEVAL:
        /* We start it and schedule it to run asynchronously */
        start_network_timezone (self);
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->enable_unsolicited_events (
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
mm_iface_modem_time_enable (MMIfaceModemTime *self,
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
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, &ctx->skeleton,
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
    MmGdbusModemTime *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static void
check_support_ready (MMIfaceModemTime *self,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->check_support_finish (self,
                                                                         res,
                                                                         &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_obj_dbg (self, "time support check failed: %s", error->message);
            g_error_free (error);
        }
    } else {
        /* Time is supported! */
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
    MMIfaceModemTime *self;
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

            if (MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->check_support &&
                MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->check_support (
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
                                     "Time not supported");
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-get-network-time",
                          G_CALLBACK (handle_get_network_time),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_time (MM_GDBUS_OBJECT_SKELETON (self),
                                                 MM_GDBUS_MODEM_TIME (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_time_initialize_finish (MMIfaceModemTime *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_time_initialize (MMIfaceModemTime *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemTime *skeleton = NULL;
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_time_skeleton_new ();
        g_object_set (self,
                      MM_IFACE_MODEM_TIME_DBUS_SKELETON, skeleton,
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
mm_iface_modem_time_shutdown (MMIfaceModemTime *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_time (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_time_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_TIME_DBUS_SKELETON,
                              "Time DBus skeleton",
                              "DBus skeleton for the Time interface",
                              MM_GDBUS_TYPE_MODEM_TIME_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_time_get_type (void)
{
    static GType iface_modem_time_type = 0;

    if (!G_UNLIKELY (iface_modem_time_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemTime), /* class_size */
            iface_modem_time_init,     /* base_init */
            NULL,                      /* base_finalize */
        };

        iface_modem_time_type = g_type_register_static (G_TYPE_INTERFACE,
                                                        "MMIfaceModemTime",
                                                        &info,
                                                        0);

        g_type_interface_add_prerequisite (iface_modem_time_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_time_type;
}
