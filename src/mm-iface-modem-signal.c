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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-signal.h"
#include "mm-log.h"

#define SUPPORT_CHECKED_TAG "signal-support-checked-tag"
#define SUPPORTED_TAG       "signal-supported-tag"
#define REFRESH_CONTEXT_TAG "signal-refresh-context-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;
static GQuark refresh_context_quark;

/*****************************************************************************/

void
mm_iface_modem_signal_bind_simple_status (MMIfaceModemSignal *self,
                                          MMSimpleStatus *status)
{
}

/*****************************************************************************/

typedef struct {
    guint rate;
    guint timeout_source;
} RefreshContext;

static void
refresh_context_free (RefreshContext *ctx)
{
    if (ctx->timeout_source)
        g_source_remove (ctx->timeout_source);
    g_slice_free (RefreshContext, ctx);
}

static void
clear_values (MMIfaceModemSignal *self)
{
    MmGdbusModemSignal *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    mm_gdbus_modem_signal_set_cdma (skeleton, NULL);
    mm_gdbus_modem_signal_set_evdo (skeleton, NULL);
    mm_gdbus_modem_signal_set_gsm  (skeleton, NULL);
    mm_gdbus_modem_signal_set_umts (skeleton, NULL);
    mm_gdbus_modem_signal_set_lte  (skeleton, NULL);
    g_object_unref (skeleton);
}

static void
load_values_ready (MMIfaceModemSignal *self,
                   GAsyncResult *res)
{
    GVariant *dictionary;
    GError *error = NULL;
    MMSignal *cdma = NULL;
    MMSignal *evdo = NULL;
    MMSignal *gsm = NULL;
    MMSignal *umts = NULL;
    MMSignal *lte = NULL;
    MmGdbusModemSignal *skeleton;

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->load_values_finish (
            self,
            res,
            &cdma,
            &evdo,
            &gsm,
            &umts,
            &lte,
            &error)) {
        mm_warn ("Couldn't load extended signal information: %s", error->message);
        g_error_free (error);
        clear_values (self);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        mm_warn ("Cannot update extended signal information: "
                 "Couldn't get interface skeleton");
        return;
    }

    if (cdma) {
        dictionary = mm_signal_get_dictionary (cdma);
        mm_gdbus_modem_signal_set_cdma (skeleton, dictionary);
        g_variant_unref (dictionary);
        g_object_unref (cdma);
    }

    if (evdo) {
        dictionary = mm_signal_get_dictionary (evdo);
        mm_gdbus_modem_signal_set_evdo (skeleton, dictionary);
        g_variant_unref (dictionary);
        g_object_unref (evdo);
    }

    if (gsm) {
        dictionary = mm_signal_get_dictionary (gsm);
        mm_gdbus_modem_signal_set_gsm (skeleton, dictionary);
        g_variant_unref (dictionary);
        g_object_unref (gsm);
    }

    if (umts) {
        dictionary = mm_signal_get_dictionary (umts);
        mm_gdbus_modem_signal_set_umts (skeleton, dictionary);
        g_variant_unref (dictionary);
        g_object_unref (umts);
    }

    if (lte) {
        dictionary = mm_signal_get_dictionary (lte);
        mm_gdbus_modem_signal_set_lte (skeleton, dictionary);
        g_variant_unref (dictionary);
        g_object_unref (lte);
    }

    /* Flush right away */
    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));

    g_object_unref (skeleton);
}

static gboolean
refresh_context_cb (MMIfaceModemSignal *self)
{
    MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->load_values (
        self,
        NULL,
        (GAsyncReadyCallback)load_values_ready,
        NULL);
    return G_SOURCE_CONTINUE;
}

static void
teardown_refresh_context (MMIfaceModemSignal *self)
{
    mm_dbg ("Extended signal information reporting disabled");
    clear_values (self);
    if (G_UNLIKELY (!refresh_context_quark))
        refresh_context_quark  = g_quark_from_static_string (REFRESH_CONTEXT_TAG);
    g_object_set_qdata (G_OBJECT (self), refresh_context_quark, NULL);
}

static gboolean
setup_refresh_context (MMIfaceModemSignal *self,
                       gboolean update_rate,
                       guint new_rate,
                       GError **error)
{
    MmGdbusModemSignal *skeleton;
    RefreshContext *ctx;
    MMModemState modem_state;

    if (G_UNLIKELY (!refresh_context_quark))
        refresh_context_quark  = g_quark_from_static_string (REFRESH_CONTEXT_TAG);

    g_object_get (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (!skeleton) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't get interface skeleton");
        return FALSE;
    }

    if (update_rate)
        mm_gdbus_modem_signal_set_rate (skeleton, new_rate);
    else
        new_rate = mm_gdbus_modem_signal_get_rate (skeleton);
    g_object_unref (skeleton);

    /* User disabling? */
    if (new_rate == 0) {
        mm_dbg ("Extended signal information reporting disabled (rate: 0 seconds)");
        clear_values (self);
        g_object_set_qdata (G_OBJECT (self), refresh_context_quark, NULL);
        return TRUE;
    }

    if (modem_state < MM_MODEM_STATE_ENABLING) {
        mm_dbg ("Extended signal information reporting disabled (modem not yet enabled)");
        return TRUE;
    }

    /* Setup refresh context */
    ctx = g_object_get_qdata (G_OBJECT (self), refresh_context_quark);
    if (!ctx) {
        ctx = g_slice_new0 (RefreshContext);
        g_object_set_qdata_full (G_OBJECT (self),
                                 refresh_context_quark,
                                 ctx,
                                 (GDestroyNotify)refresh_context_free);
    }

    /* We're enabling, compare to old rate */
    if (ctx->rate == new_rate) {
        /* Already there */
        return TRUE;
    }

    /* Update refresh context */
    mm_dbg ("Extended signal information reporting enabled (rate: %u seconds)", new_rate);
    ctx->rate = new_rate;
    if (ctx->timeout_source)
        g_source_remove (ctx->timeout_source);
    ctx->timeout_source = g_timeout_add_seconds (ctx->rate, (GSourceFunc) refresh_context_cb, self);

    /* Also launch right away */
    refresh_context_cb (self);

    return TRUE;
}

/*****************************************************************************/

typedef struct {
    GDBusMethodInvocation *invocation;
    MmGdbusModemSignal *skeleton;
    MMIfaceModemSignal *self;
    guint rate;
} HandleSetupContext;

static void
handle_setup_context_free (HandleSetupContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetupContext, ctx);
}

static void
handle_setup_auth_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         HandleSetupContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else if (!setup_refresh_context (ctx->self, TRUE, ctx->rate, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_signal_complete_setup (ctx->skeleton, ctx->invocation);
    handle_setup_context_free (ctx);
}

static gboolean
handle_setup (MmGdbusModemSignal *skeleton,
              GDBusMethodInvocation *invocation,
              guint rate,
              MMIfaceModemSignal *self)
{
    HandleSetupContext *ctx;

    ctx = g_slice_new (HandleSetupContext);
    ctx->invocation = g_object_ref (invocation);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->self = g_object_ref (self);
    ctx->rate = rate;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_setup_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

gboolean
mm_iface_modem_signal_disable_finish (MMIfaceModemSignal *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_signal_disable (MMIfaceModemSignal *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_signal_disable);

    teardown_refresh_context (self);

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

/*****************************************************************************/

gboolean
mm_iface_modem_signal_enable_finish (MMIfaceModemSignal *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_signal_enable (MMIfaceModemSignal *self,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    GSimpleAsyncResult *result;
    GError *error = NULL;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_signal_enable);

    if (!setup_refresh_context (self, FALSE, 0, &error))
        g_simple_async_result_take_error (result, error);
    else
        g_simple_async_result_set_op_res_gboolean (result, TRUE);

    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
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
    MMIfaceModemSignal *self;
    MmGdbusModemSignal *skeleton;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
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
    g_slice_free (InitializationContext, ctx);
}


gboolean
mm_iface_modem_signal_initialize_finish (MMIfaceModemSignal *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
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
check_support_ready (MMIfaceModemSignal *self,
                     GAsyncResult *res,
                     InitializationContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->check_support_finish (self, res, &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_dbg ("Extended signal support check failed: '%s'", error->message);
            g_error_free (error);
        }
    } else {
        /* Signal is supported! */
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

            if (MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (ctx->self)->check_support &&
                MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (ctx->self)->check_support_finish) {
                MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (ctx->self)->check_support (
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
                                             "Extended Signal information not supported");
            initialization_context_complete_and_free (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-setup",
                          G_CALLBACK (handle_setup),
                          ctx->self);
        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_signal (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                   MM_GDBUS_MODEM_SIGNAL (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_signal_initialize (MMIfaceModemSignal *self,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemSignal *skeleton = NULL;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_signal_skeleton_new ();
        clear_values (self);
        g_object_set (self,
                      MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */

    ctx = g_slice_new0 (InitializationContext);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_signal_initialize);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    interface_initialization_step (ctx);
}

void
mm_iface_modem_signal_shutdown (MMIfaceModemSignal *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_signal (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_signal_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON,
                              "Signal DBus skeleton",
                              "DBus skeleton for the Signal interface",
                              MM_GDBUS_TYPE_MODEM_SIGNAL_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_signal_get_type (void)
{
    static GType iface_modem_signal_type = 0;

    if (!G_UNLIKELY (iface_modem_signal_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemSignal), /* class_size */
            iface_modem_signal_init,     /* base_init */
            NULL,                      /* base_finalize */
        };

        iface_modem_signal_type = g_type_register_static (G_TYPE_INTERFACE,
                                                          "MMIfaceModemSignal",
                                                          &info,
                                                          0);

        g_type_interface_add_prerequisite (iface_modem_signal_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_signal_type;
}
