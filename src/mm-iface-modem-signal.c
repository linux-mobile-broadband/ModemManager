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
 * Copyright (C) 2013-2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Intel Corporation
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-signal.h"
#include "mm-log-object.h"

#define SUPPORT_CHECKED_TAG "signal-support-checked-tag"
#define SUPPORTED_TAG       "signal-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "signal-private-tag"
static GQuark private_quark;

typedef struct {
    /* polling-based reporting enabled */
    guint    rate;
    guint    timeout_source;
    /* threshold-based reporting enabled */
    guint    rssi_threshold;
    gboolean error_rate_threshold;
} Private;

static void
private_free (Private *priv)
{
    if (priv->timeout_source)
        g_source_remove (priv->timeout_source);
    g_slice_free (Private, priv);
}

static Private *
get_private (MMIfaceModemSignal *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);
        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/

void
mm_iface_modem_signal_bind_simple_status (MMIfaceModemSignal *self,
                                          MMSimpleStatus *status)
{
}

/*****************************************************************************/

static void
internal_signal_update (MMIfaceModemSignal *self,
                        MMSignal           *cdma,
                        MMSignal           *evdo,
                        MMSignal           *gsm,
                        MMSignal           *umts,
                        MMSignal           *lte,
                        MMSignal           *nr5g)
{
    g_autoptr(GVariant)                   dict_cdma = NULL;
    g_autoptr(GVariant)                   dict_evdo = NULL;
    g_autoptr(GVariant)                   dict_gsm = NULL;
    g_autoptr(GVariant)                   dict_umts = NULL;
    g_autoptr(GVariant)                   dict_lte = NULL;
    g_autoptr(GVariant)                   dict_nr5g = NULL;
    g_autoptr(MmGdbusModemSignalSkeleton) skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        mm_obj_warn (self, "cannot update extended signal information: couldn't get interface skeleton");
        return;
    }

    if (cdma) {
        mm_obj_dbg (self, "cdma extended signal information updated");
        dict_cdma = mm_signal_get_dictionary (cdma);
    }
    mm_gdbus_modem_signal_set_cdma (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_cdma);

    if (evdo) {
        mm_obj_dbg (self, "evdo extended signal information updated");
        dict_evdo = mm_signal_get_dictionary (evdo);
    }
    mm_gdbus_modem_signal_set_evdo (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_evdo);

    if (gsm) {
        mm_obj_dbg (self, "gsm extended signal information updated");
        dict_gsm = mm_signal_get_dictionary (gsm);
    }
    mm_gdbus_modem_signal_set_gsm (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_gsm);

    if (umts) {
        mm_obj_dbg (self, "umts extended signal information updated");
        dict_umts = mm_signal_get_dictionary (umts);
    }
    mm_gdbus_modem_signal_set_umts (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_umts);

    if (lte) {
        mm_obj_dbg (self, "lte extended signal information updated");
        dict_lte = mm_signal_get_dictionary (lte);
    }
    mm_gdbus_modem_signal_set_lte (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_lte);

    if (nr5g) {
        mm_obj_dbg (self, "5gnr extended signal information updated");
        dict_nr5g = mm_signal_get_dictionary (nr5g);
    }
    mm_gdbus_modem_signal_set_nr5g (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_nr5g);

    /* Flush right away */
    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
}

void
mm_iface_modem_signal_update (MMIfaceModemSignal *self,
                              MMSignal           *cdma,
                              MMSignal           *evdo,
                              MMSignal           *gsm,
                              MMSignal           *umts,
                              MMSignal           *lte,
                              MMSignal           *nr5g)
{
    Private *priv;

    priv = get_private (self);
    if (!priv->rate && !priv->rssi_threshold && !priv->error_rate_threshold) {
        mm_obj_dbg (self, "skipping extended signal information update...");
        return;
    }

    internal_signal_update (self, cdma, evdo, gsm, umts, lte, nr5g);
}

/*****************************************************************************/

static void
check_interface_reset (MMIfaceModemSignal *self)
{
    Private *priv;

    priv = get_private (self);

    if (!priv->rate && !priv->rssi_threshold && !priv->error_rate_threshold) {
        mm_obj_dbg (self, "reseting extended signal information...");
        mm_iface_modem_signal_update (self, NULL, NULL, NULL, NULL, NULL, NULL);
    }
}

/*****************************************************************************/

static void
load_values_ready (MMIfaceModemSignal *self,
                   GAsyncResult       *res)
{
    g_autoptr(GError)   error = NULL;
    g_autoptr(MMSignal) cdma = NULL;
    g_autoptr(MMSignal) evdo = NULL;
    g_autoptr(MMSignal) gsm = NULL;
    g_autoptr(MMSignal) umts = NULL;
    g_autoptr(MMSignal) lte = NULL;
    g_autoptr(MMSignal) nr5g = NULL;

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
        mm_obj_warn (self, "couldn't reload extended signal information: %s", error->message);
        return;
    }

    mm_iface_modem_signal_update (self, cdma, evdo, gsm, umts, lte, nr5g);
}

static gboolean
polling_context_cb (MMIfaceModemSignal *self)
{
    MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->load_values (
        self,
        NULL,
        (GAsyncReadyCallback)load_values_ready,
        NULL);
    return G_SOURCE_CONTINUE;
}

static gboolean
polling_restart (MMIfaceModemSignal  *self,
                 guint                rate,
                 GError             **error)
{
    Private *priv;

    priv = get_private (self);

    /* Update the rate in the interface if it changed */
    if (priv->rate != rate) {
        g_autoptr(MmGdbusModemSignalSkeleton) skeleton = NULL;

        g_object_get (self,
                      MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON, &skeleton,
                      NULL);
        if (!skeleton) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't get interface skeleton");
            return FALSE;
        }
        mm_gdbus_modem_signal_set_rate (MM_GDBUS_MODEM_SIGNAL (skeleton), rate);
        priv->rate = rate;
    }

    /* Polling disabled by user? */
    if (rate == 0) {
        mm_obj_dbg (self, "extended signal information polling disabled (rate: 0 seconds)");
        if (priv->timeout_source) {
            g_source_remove (priv->timeout_source);
            priv->timeout_source = 0;
        }
        check_interface_reset (self);
        return TRUE;
    }

    /* Restart polling */
    mm_obj_dbg (self, "extended signal information reporting enabled (rate: %u seconds)", rate);
    if (priv->timeout_source)
        g_source_remove (priv->timeout_source);
    priv->timeout_source = g_timeout_add_seconds (rate, (GSourceFunc) polling_context_cb, self);

    /* Also launch right away */
    polling_context_cb (self);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    GDBusMethodInvocation *invocation;
    MmGdbusModemSignal    *skeleton;
    MMIfaceModemSignal    *self;
    guint                  rate;
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

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_setup_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_ENABLED)) {
        handle_setup_context_free (ctx);
        return;
    }

    if (!polling_restart (ctx->self, ctx->rate, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_signal_complete_setup (ctx->skeleton, ctx->invocation);
    handle_setup_context_free (ctx);
}

static gboolean
handle_setup (MmGdbusModemSignal    *skeleton,
              GDBusMethodInvocation *invocation,
              guint                  rate,
              MMIfaceModemSignal    *self)
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

typedef struct {
    GDBusMethodInvocation *invocation;
    MmGdbusModemSignal    *skeleton;
    MMIfaceModemSignal    *self;
    GVariant              *settings;
    guint32                rssi_threshold;
    gboolean               error_rate_threshold;
} HandleSetupThresholdsContext;

static void
handle_setup_thresholds_context_free (HandleSetupThresholdsContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->self);
    if (ctx->settings)
        g_variant_unref (ctx->settings);
    g_slice_free (HandleSetupThresholdsContext, ctx);
}

static void
setup_thresholds_ready (MMIfaceModemSignal           *self,
                        GAsyncResult                 *res,
                        HandleSetupThresholdsContext *ctx)
{
    GError  *error = NULL;
    Private *priv;

    priv = get_private (MM_IFACE_MODEM_SIGNAL (self));

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (ctx->self)->setup_thresholds_finish (ctx->self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* Update the properties with the latest threshold setting */
        mm_gdbus_modem_signal_set_rssi_threshold (ctx->skeleton, ctx->rssi_threshold);
        mm_gdbus_modem_signal_set_error_rate_threshold (ctx->skeleton, ctx->error_rate_threshold);
        priv->rssi_threshold = ctx->rssi_threshold;
        priv->error_rate_threshold = ctx->error_rate_threshold;
        check_interface_reset (self);

        mm_gdbus_modem_signal_complete_setup_thresholds (ctx->skeleton, ctx->invocation);
    }

    handle_setup_thresholds_context_free (ctx);
}

static gboolean
select_new_threshold_settings (HandleSetupThresholdsContext  *ctx,
                               GError                       **error)
{
    GError       *inner_error = NULL;
    GVariantIter  iter;
    gchar        *key;
    GVariant     *value;

    if (!g_variant_is_of_type (ctx->settings, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot get threshold settings from dictionary: "
                     "invalid variant type received");
        return FALSE;
    }

    g_variant_iter_init (&iter, ctx->settings);
    while (!inner_error && g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, "rssi-threshold"))
            ctx->rssi_threshold = g_variant_get_uint32 (value);
        else if (g_str_equal (key, "error-rate-threshold"))
            ctx->error_rate_threshold = g_variant_get_boolean (value);
        else {
            /* Set inner error, will stop the loop */
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_INVALID_ARGS,
                                       "Invalid settings dictionary, unexpected key '%s'",
                                       key);
        }

        g_free (key);
        g_variant_unref (value);
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }
    return TRUE;
}

static void
handle_setup_thresholds_auth_ready (MMBaseModem                  *self,
                                    GAsyncResult                 *res,
                                    HandleSetupThresholdsContext *ctx)
{
    GError  *error = NULL;
    Private *priv;

    priv = get_private (MM_IFACE_MODEM_SIGNAL (self));

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (ctx->self)->setup_thresholds ||
        !MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (ctx->self)->setup_thresholds_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot setup thresholds: operation not supported");
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_ENABLED)) {
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    /* Process received settings */
    if (!select_new_threshold_settings (ctx, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    /* Same settings? */
    if ((ctx->rssi_threshold == priv->rssi_threshold) &&
        (ctx->error_rate_threshold == priv->error_rate_threshold)) {
        mm_gdbus_modem_signal_complete_setup_thresholds (ctx->skeleton, ctx->invocation);
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (ctx->self)->setup_thresholds (
        ctx->self,
        ctx->rssi_threshold,
        ctx->error_rate_threshold,
        (GAsyncReadyCallback)setup_thresholds_ready,
        ctx);
}

static gboolean
handle_setup_thresholds (MmGdbusModemSignal    *skeleton,
                         GDBusMethodInvocation *invocation,
                         GVariant              *settings,
                         MMIfaceModemSignal    *self)
{
    HandleSetupThresholdsContext *ctx;

    ctx = g_slice_new0 (HandleSetupThresholdsContext);
    ctx->invocation = g_object_ref (invocation);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->self = g_object_ref (self);
    ctx->settings = g_variant_ref (settings);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_setup_thresholds_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

gboolean
mm_iface_modem_signal_disable_finish (MMIfaceModemSignal  *self,
                                      GAsyncResult        *res,
                                      GError             **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_signal_disable (MMIfaceModemSignal  *self,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
    GTask *task;

    mm_iface_modem_signal_update (self, NULL, NULL, NULL, NULL, NULL, NULL);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/

gboolean
mm_iface_modem_signal_enable_finish (MMIfaceModemSignal  *self,
                                     GAsyncResult        *res,
                                     GError             **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_signal_enable (MMIfaceModemSignal  *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
    GTask   *task;
    GError  *error = NULL;
    Private *priv;

    priv = get_private (self);
    task = g_task_new (self, cancellable, callback, user_data);

    /* same rate as we had before */
    if (!polling_restart (self, priv->rate, &error))
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
        g_signal_connect (ctx->skeleton,
                          "handle-setup-thresholds",
                          G_CALLBACK (handle_setup_thresholds),
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
