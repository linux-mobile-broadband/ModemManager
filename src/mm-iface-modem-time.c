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
#include "mm-log.h"

#define SUPPORT_CHECKED_TAG              "time-support-checked-tag"
#define SUPPORTED_TAG                    "time-supported-tag"
#define NETWORK_TIMEZONE_CANCELLABLE_TAG "time-network-timezone-cancellable"

static GQuark support_checked_quark;
static GQuark supported_quark;
static GQuark network_timezone_cancellable_quark;

#define TIMEZONE_POLL_INTERVAL_SEC 5
#define TIMEZONE_POLL_RETRIES 6

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
load_network_time_ready (MMIfaceModemTime *self,
                         GAsyncResult *res,
                         HandleGetNetworkTimeContext *ctx)
{
    gchar *time_str;
    GError *error = NULL;

    time_str = MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_time_finish (self,
                                                                                   res,
                                                                                   &error);
    if (error)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_time_complete_get_network_time (ctx->skeleton,
                                                       ctx->invocation,
                                                       time_str);
    g_free (time_str);
    handle_get_network_time_context_free (ctx);
}

static gboolean
handle_get_network_time (MmGdbusModemTime *skeleton,
                         GDBusMethodInvocation *invocation,
                         MMIfaceModemTime *self)
{
    HandleGetNetworkTimeContext *ctx;
    MMModemState state;

    if (!MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_time ||
        !MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_time_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot load network time: "
                                               "operation not supported");
        return TRUE;
    }

    state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);
    /* If we're not yet registered, we cannot get the network time */
    if (state < MM_MODEM_STATE_REGISTERED) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot load network time: "
                                               "not registered yet");
        return TRUE;
    }

    ctx = g_new (HandleGetNetworkTimeContext, 1);
    ctx->invocation = g_object_ref (invocation);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->self = g_object_ref (self);

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_time (
        self,
        (GAsyncReadyCallback)load_network_time_ready,
        ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MMIfaceModemTime *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gulong cancelled_id;
    gulong state_changed_id;
    guint network_timezone_poll_id;
    guint network_timezone_poll_retries;
} UpdateNetworkTimezoneContext;

static gboolean timezone_poll_cb (UpdateNetworkTimezoneContext *ctx);

static void
update_network_timezone_context_complete_and_free (UpdateNetworkTimezoneContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
update_network_timezone_finish (MMIfaceModemTime *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cancelled (GCancellable *cancellable,
           UpdateNetworkTimezoneContext *ctx)
{
    /* If waiting to get registered, disconnect signal */
    if (ctx->state_changed_id)
        g_signal_handler_disconnect (ctx->self,
                                     ctx->state_changed_id);

    /* If waiting in the timeout loop, remove the timeout */
    else if (ctx->network_timezone_poll_id)
        g_source_remove (ctx->network_timezone_poll_id);

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Network timezone loading cancelled");
    update_network_timezone_context_complete_and_free (ctx);
}

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
                             GAsyncResult *res,
                             UpdateNetworkTimezoneContext *ctx)
{
    GError *error = NULL;
    MMNetworkTimezone *tz;

    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "Finished network timezone loading, "
                                         "but cancelled meanwhile");
        update_network_timezone_context_complete_and_free (ctx);
        return;
    }

    /* Finish the async operation */
    tz = MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_timezone_finish (self,
                                                                                 res,
                                                                                 &error);
    if (error) {
        /* Retry? */
        ctx->network_timezone_poll_retries--;

        /* Fatal if no more retries, or if specific error is not RETRY */
        if (ctx->network_timezone_poll_retries == 0 ||
            !g_error_matches (error,
                              MM_CORE_ERROR,
                              MM_CORE_ERROR_RETRY)) {
            g_simple_async_result_take_error (ctx->result, error);
            update_network_timezone_context_complete_and_free (ctx);
            return;
        }

        /* Otherwise, reconnect cancellable and relaunch timeout to query a bit
         * later */
        ctx->cancelled_id = g_cancellable_connect (ctx->cancellable,
                                                   G_CALLBACK (cancelled),
                                                   ctx,
                                                   NULL);
        ctx->network_timezone_poll_id = g_timeout_add_seconds (TIMEZONE_POLL_INTERVAL_SEC,
                                                               (GSourceFunc)timezone_poll_cb,
                                                               ctx);

        g_error_free (error);
        return;
    }

    /* Got final result properly, update the property in the skeleton */
    update_network_timezone_dictionary (ctx->self, tz);
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    update_network_timezone_context_complete_and_free (ctx);
    g_object_unref (tz);
}

static gboolean
timezone_poll_cb (UpdateNetworkTimezoneContext *ctx)
{
    ctx->network_timezone_poll_id = 0;

    /* Before we launch the async loading of the network timezone,
     * we disconnect the cancellable signal. We don't want to get
     * signaled while waiting to finish this async method, we'll
     * check the cancellable afterwards instead. */
    g_cancellable_disconnect (ctx->cancellable,
                              ctx->cancelled_id);
    ctx->cancelled_id = 0;

    MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->load_network_timezone (
        ctx->self,
        (GAsyncReadyCallback)load_network_timezone_ready,
        ctx);

    return G_SOURCE_REMOVE;
}

static void
start_timezone_poll (UpdateNetworkTimezoneContext *ctx)
{
    /* Setup loop to query current timezone, don't do it right away.
     * Note that we're passing the context reference to the loop. */
    ctx->network_timezone_poll_retries = TIMEZONE_POLL_RETRIES;
    ctx->network_timezone_poll_id = g_timeout_add_seconds (TIMEZONE_POLL_INTERVAL_SEC,
                                                           (GSourceFunc)timezone_poll_cb,
                                                           ctx);
}

static void
state_changed (MMIfaceModemTime *self,
               GParamSpec *spec,
               UpdateNetworkTimezoneContext *ctx)
{
    MMModemState state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);

    /* We're waiting to get registered */
    if (state < MM_MODEM_STATE_REGISTERED)
        return;

    /* Got registered, disconnect signal */
    if (ctx->state_changed_id) {
        g_signal_handler_disconnect (self,
                                     ctx->state_changed_id);
        ctx->state_changed_id = 0;
    }

    /* Once we know we're registered, start timezone poll */
    start_timezone_poll (ctx);
}

static void
update_network_timezone (MMIfaceModemTime *self,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    UpdateNetworkTimezoneContext *ctx;
    MMModemState state = MM_MODEM_STATE_UNKNOWN;

    /* If loading network timezone not supported, just finish here */
    if (!MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_timezone ||
        !MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->load_network_timezone_finish) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Loading network timezone is not supported");
        return;
    }

    ctx = g_new0 (UpdateNetworkTimezoneContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             update_network_timezone);

    /* Note: we don't expect to get cancelled by any other thread, so no
     * need to check if we're cancelled just after connecting to the
     * cancelled signal */
    ctx->cancelled_id = g_cancellable_connect (ctx->cancellable,
                                               G_CALLBACK (cancelled),
                                               ctx,
                                               NULL);

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &state,
                  NULL);

    /* Already registered? */
    if (state >= MM_MODEM_STATE_REGISTERED) {
        /* Once we know we're registered, start timezone poll */
        start_timezone_poll (ctx);
    } else {
        /* Want to get notified when modem state changes */
        ctx->state_changed_id = g_signal_connect (ctx->self,
                                                  "notify::" MM_IFACE_MODEM_STATE,
                                                  G_CALLBACK (state_changed),
                                                  ctx);
    }
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

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_CANCEL_NETWORK_TIMEZONE_UPDATE,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModemTime *self;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemTime *skeleton;
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
mm_iface_modem_time_disable_finish (MMIfaceModemTime *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModemTime *self,
                                  GAsyncResult *res,
                                  DisablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->disable_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        disabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

static void
cleanup_unsolicited_events_ready (MMIfaceModemTime *self,
                                  GAsyncResult *res,
                                  DisablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        disabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CANCEL_NETWORK_TIMEZONE_UPDATE: {
        if (G_LIKELY (network_timezone_cancellable_quark)) {
            GCancellable *cancellable = NULL;

            cancellable = g_object_get_qdata (G_OBJECT (ctx->self),
                                              network_timezone_cancellable_quark);

            /* If network timezone loading is currently running, abort it */
            if (cancellable) {
                g_cancellable_cancel (cancellable);
                g_object_set_qdata (G_OBJECT (ctx->self),
                                    network_timezone_cancellable_quark,
                                    NULL);
            }
        }

        /* Fall down to next step */
        ctx->step++;
    }

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->disable_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        /* Allow cleaning up unsolicited events */
        if (MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                ctx);
            return;
        }
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
mm_iface_modem_time_disable (MMIfaceModemTime *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_time_disable);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, &ctx->skeleton,
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

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_NETWORK_TIMEZONE_RETRIEVAL,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModemTime *self;
    EnablingStep step;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MmGdbusModemTime *skeleton;
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
mm_iface_modem_time_enable_finish (MMIfaceModemTime *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
update_network_timezone_ready (MMIfaceModemTime *self,
                               GAsyncResult *res)
{
    GError *error = NULL;

    if (!update_network_timezone_finish (self, res, &error)) {
        if (!g_error_matches (error,
                              MM_CORE_ERROR,
                              MM_CORE_ERROR_UNSUPPORTED))
            mm_dbg ("Couldn't update network timezone: '%s'", error->message);
        g_error_free (error);
    }

    /* Cleanup our cancellable in the context */
    g_object_set_qdata (G_OBJECT (self),
                        network_timezone_cancellable_quark,
                        NULL);
}

static void
setup_unsolicited_events_ready (MMIfaceModemTime *self,
                                GAsyncResult *res,
                                EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
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
enable_unsolicited_events_ready (MMIfaceModemTime *self,
                                 GAsyncResult *res,
                                 EnablingContext *ctx)
{
    GError *error = NULL;

    /* Not critical! */
    if (!MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error)) {
        mm_dbg ("Couldn't enable unsolicited events: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on with next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

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

    case ENABLING_STEP_SETUP_NETWORK_TIMEZONE_RETRIEVAL: {
        GCancellable *cancellable;

        /* We'll create a cancellable which is valid as long as we're updating
         * network timezone, and we set it as context */
        cancellable = g_cancellable_new ();
        if (G_UNLIKELY (!network_timezone_cancellable_quark))
            network_timezone_cancellable_quark = (g_quark_from_static_string (
                                                      NETWORK_TIMEZONE_CANCELLABLE_TAG));
        g_object_set_qdata_full (G_OBJECT (ctx->self),
                                 network_timezone_cancellable_quark,
                                 cancellable,
                                 (GDestroyNotify)g_object_unref);

        update_network_timezone (ctx->self,
                                 cancellable,
                                 (GAsyncReadyCallback)update_network_timezone_ready,
                                 NULL);

        /* NOTE!!!! We'll leave the timezone network update operation
         * running, we don't wait for it to finish */

        /* Fall down to next step */
        ctx->step++;
    }

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->setup_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        /* Allow setting up unsolicited events */
        if (MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->enable_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                ctx);
            return;
        }
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
mm_iface_modem_time_enable (MMIfaceModemTime *self,
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
                                             mm_iface_modem_time_enable);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_TIME_DBUS_SKELETON, &ctx->skeleton,
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

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModemTime *self;
    MmGdbusModemTime *skeleton;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    InitializationStep step;
};

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
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

static void
check_support_ready (MMIfaceModemTime *self,
                     GAsyncResult *res,
                     InitializationContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_TIME_GET_INTERFACE (self)->check_support_finish (self,
                                                                         res,
                                                                         &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_dbg ("Time support check failed: '%s'", error->message);
            g_error_free (error);
        }
    } else {
        /* Time is supported! */
        g_object_set_qdata (G_OBJECT (self),
                            supported_quark,
                            GUINT_TO_POINTER (TRUE));
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
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   support_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                support_checked_quark,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support it */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                supported_quark,
                                GUINT_TO_POINTER (FALSE));

            if (MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->check_support &&
                MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->check_support_finish) {
                MM_IFACE_MODEM_TIME_GET_INTERFACE (ctx->self)->check_support (
                    ctx->self,
                    (GAsyncReadyCallback)check_support_ready,
                    ctx);
                return;
            }

            /* If there is no implementation to check support, assume we DON'T
             * support it. */
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   supported_quark))) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Time not supported");
            initialization_context_complete_and_free (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-get-network-time",
                          G_CALLBACK (handle_get_network_time),
                          ctx->self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_time (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                 MM_GDBUS_MODEM_TIME (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_time_initialize_finish (MMIfaceModemTime *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_time_initialize (MMIfaceModemTime *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemTime *skeleton = NULL;

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
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_time_initialize);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    interface_initialization_step (ctx);
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
