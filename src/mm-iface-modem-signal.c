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
#include "mm-log-object.h"

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
    g_autoptr(MmGdbusModemSignalSkeleton) skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    mm_gdbus_modem_signal_set_cdma (MM_GDBUS_MODEM_SIGNAL (skeleton), NULL);
    mm_gdbus_modem_signal_set_evdo (MM_GDBUS_MODEM_SIGNAL (skeleton), NULL);
    mm_gdbus_modem_signal_set_gsm  (MM_GDBUS_MODEM_SIGNAL (skeleton), NULL);
    mm_gdbus_modem_signal_set_umts (MM_GDBUS_MODEM_SIGNAL (skeleton), NULL);
    mm_gdbus_modem_signal_set_lte  (MM_GDBUS_MODEM_SIGNAL (skeleton), NULL);
    mm_gdbus_modem_signal_set_nr5g (MM_GDBUS_MODEM_SIGNAL (skeleton), NULL);
}

static void
load_values_ready (MMIfaceModemSignal *self,
                   GAsyncResult       *res)
{
    g_autoptr(GError)   error = NULL;
    g_autoptr(MMSignal) cdma = NULL;
    g_autoptr(GVariant) dict_cdma = NULL;
    g_autoptr(MMSignal) evdo = NULL;
    g_autoptr(GVariant) dict_evdo = NULL;
    g_autoptr(MMSignal) gsm = NULL;
    g_autoptr(GVariant) dict_gsm = NULL;
    g_autoptr(MMSignal) umts = NULL;
    g_autoptr(GVariant) dict_umts = NULL;
    g_autoptr(MMSignal) lte = NULL;
    g_autoptr(GVariant) dict_lte = NULL;
    g_autoptr(MMSignal) nr5g = NULL;
    g_autoptr(GVariant) dict_nr5g = NULL;
    g_autoptr(MmGdbusModemSignalSkeleton) skeleton = NULL;

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->load_values_finish (
            self,
            res,
            &cdma,
            &evdo,
            &gsm,
            &umts,
            &lte,
            &nr5g,
            &error)) {
        mm_obj_warn (self, "couldn't load extended signal information: %s", error->message);
        clear_values (self);
        return;
    }

    g_object_get (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        mm_obj_warn (self, "cannot update extended signal information: couldn't get interface skeleton");
        return;
    }

    if (cdma)
        dict_cdma = mm_signal_get_dictionary (cdma);
    mm_gdbus_modem_signal_set_cdma (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_cdma);

    if (evdo)
        dict_evdo = mm_signal_get_dictionary (evdo);
    mm_gdbus_modem_signal_set_evdo (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_evdo);

    if (gsm)
        dict_gsm = mm_signal_get_dictionary (gsm);
    mm_gdbus_modem_signal_set_gsm (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_gsm);

    if (umts)
        dict_umts = mm_signal_get_dictionary (umts);
    mm_gdbus_modem_signal_set_umts (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_umts);

    if (lte)
        dict_lte = mm_signal_get_dictionary (lte);
    mm_gdbus_modem_signal_set_lte (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_lte);

    if (nr5g)
        dict_nr5g = mm_signal_get_dictionary (nr5g);
    mm_gdbus_modem_signal_set_nr5g (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_nr5g);

    /* Flush right away */
    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
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
    clear_values (self);
    if (G_UNLIKELY (!refresh_context_quark))
        refresh_context_quark  = g_quark_from_static_string (REFRESH_CONTEXT_TAG);
    if (g_object_get_qdata (G_OBJECT (self), refresh_context_quark)) {
        mm_obj_dbg (self, "extended signal information reporting disabled");
        g_object_set_qdata (G_OBJECT (self), refresh_context_quark, NULL);
    }
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
        mm_obj_dbg (self, "extended signal information reporting disabled (rate: 0 seconds)");
        clear_values (self);
        g_object_set_qdata (G_OBJECT (self), refresh_context_quark, NULL);
        return TRUE;
    }

    if (modem_state < MM_MODEM_STATE_ENABLING) {
        mm_obj_dbg (self, "extended signal information reporting disabled (modem not yet enabled)");
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
    mm_obj_dbg (self, "extended signal information reporting enabled (rate: %u seconds)", new_rate);
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
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_signal_disable (MMIfaceModemSignal *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    GTask *task;

    teardown_refresh_context (self);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/

gboolean
mm_iface_modem_signal_enable_finish (MMIfaceModemSignal *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_signal_enable (MMIfaceModemSignal *self,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    GTask *task;
    GError *error = NULL;

    task = g_task_new (self, cancellable, callback, user_data);

    if (!setup_refresh_context (self, FALSE, 0, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
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
    MmGdbusModemSignal *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_slice_free (InitializationContext, ctx);
}

gboolean
mm_iface_modem_signal_initialize_finish (MMIfaceModemSignal *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
check_support_ready (MMIfaceModemSignal *self,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->check_support_finish (self, res, &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_obj_dbg (self, "extended signal support check failed: %s", error->message);
            g_error_free (error);
        }
    } else {
        /* Signal is supported! */
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
    MMIfaceModemSignal *self;
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

            if (MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->check_support &&
                MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->check_support (
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
                                     "Extended Signal information not supported");
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
        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_signal (MM_GDBUS_OBJECT_SKELETON (self),
                                                   MM_GDBUS_MODEM_SIGNAL (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
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
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_signal_skeleton_new ();
        g_object_set (self,
                      MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */

    ctx = g_slice_new0 (InitializationContext);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    interface_initialization_step (task);
}

void
mm_iface_modem_signal_shutdown (MMIfaceModemSignal *self)
{
    /* Teardown refresh context */
    teardown_refresh_context (self);

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
