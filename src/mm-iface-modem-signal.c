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
    /* interface enabled */
    gboolean enabled;
    /* polling-based reporting  */
    guint    rate;
    guint    timeout_source;
    /* threshold-based reporting */
    guint    rssi_threshold;
    gboolean error_rate_threshold;
    /* info logging control */
    GTimer   *info_log_timer;
} Private;

static void
private_free (Private *priv)
{
    if (priv->info_log_timer)
        g_timer_destroy (priv->info_log_timer);
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

/* Even if the signal refresh rate is higher than 300s, don't pollute the INFO
 * level log with so many updates, force a reduction of the rate to once every
 * 300s */
#define SIGNAL_QUALITY_PRINT_RATE_SECS 300

static void
info_log_signal_quality (MMIfaceModemSignal *self,
                         MMSignal           *info,
                         const gchar        *rat)
{
    Private          *priv = NULL;
    g_autofree gchar *printable = NULL;

    priv = get_private (self);

    if (G_UNLIKELY (!priv->info_log_timer))
        priv->info_log_timer = g_timer_new ();

    if (g_timer_elapsed (priv->info_log_timer, NULL) < SIGNAL_QUALITY_PRINT_RATE_SECS)
        return;
    g_timer_reset (priv->info_log_timer);

    printable = mm_signal_get_string (info);
    mm_obj_info (self, "%s: %s", rat, printable);
};

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
        info_log_signal_quality (self, gsm, "gsm");
        dict_gsm = mm_signal_get_dictionary (gsm);
    }
    mm_gdbus_modem_signal_set_gsm (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_gsm);

    if (umts) {
        mm_obj_dbg (self, "umts extended signal information updated");
        info_log_signal_quality (self, umts, "umts");
        dict_umts = mm_signal_get_dictionary (umts);
    }
    mm_gdbus_modem_signal_set_umts (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_umts);

    if (lte) {
        mm_obj_dbg (self, "lte extended signal information updated");
        info_log_signal_quality (self, lte, "lte");
        dict_lte = mm_signal_get_dictionary (lte);
    }
    mm_gdbus_modem_signal_set_lte (MM_GDBUS_MODEM_SIGNAL (skeleton), dict_lte);

    if (nr5g) {
        mm_obj_dbg (self, "5gnr extended signal information updated");
        info_log_signal_quality (self, nr5g, "5gnr");
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
    if (!priv->enabled || (!priv->rate && !priv->rssi_threshold && !priv->error_rate_threshold)) {
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

    if (!priv->enabled || (!priv->rate && !priv->rssi_threshold && !priv->error_rate_threshold)) {
        mm_obj_dbg (self, "reseting extended signal information...");
        internal_signal_update (self, NULL, NULL, NULL, NULL, NULL, NULL);
    }
}

/*****************************************************************************/
/* Polling setup management */

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
query_signal_values (MMIfaceModemSignal *self)
{
    MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->load_values (
        self,
        NULL,
        (GAsyncReadyCallback)load_values_ready,
        NULL);
    return G_SOURCE_CONTINUE;
}

static void
polling_restart (MMIfaceModemSignal *self)
{
    Private  *priv;
    gboolean  polling_setup;

    priv = get_private (self);
    polling_setup = (priv->enabled && priv->rate);

    if (polling_setup)
        mm_obj_info (self, "setting up extended signal information polling: rate %u seconds", priv->rate);
    else
        mm_obj_dbg (self, "cleaning up extended signal information polling");

    /* Stop polling */
    if (!polling_setup) {
        if (priv->timeout_source) {
            g_source_remove (priv->timeout_source);
            priv->timeout_source = 0;
        }
        return;
    }

    /* Start/restart polling */
    if (priv->timeout_source)
        g_source_remove (priv->timeout_source);
    priv->timeout_source = g_timeout_add_seconds (priv->rate, (GSourceFunc) query_signal_values, self);

    /* Also launch right away */
    query_signal_values (self);
}

/*****************************************************************************/
/* Thresholds setup management */

static gboolean
thresholds_restart_finish (MMIfaceModemSignal  *self,
                           GAsyncResult        *res,
                           GError             **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_thresholds_ready (MMIfaceModemSignal *self,
                        GAsyncResult       *res,
                        GTask              *task)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->setup_thresholds_finish (self, res, &error))
        g_task_return_error (task, error);
    else {
        /* launch a query right away */
        query_signal_values (self);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
thresholds_restart (MMIfaceModemSignal  *self,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GTask    *task;
    Private  *priv;
    gboolean  threshold_setup;

    task = g_task_new (self, NULL, callback, user_data);

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->setup_thresholds ||
        !MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->setup_thresholds_finish) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    priv = get_private (self);
    threshold_setup = (priv->enabled && (priv->rssi_threshold || priv->error_rate_threshold));

    mm_obj_dbg (self, "%s extended signal information thresholds: interface %s, rssi threshold %u dBm, error rate threshold %s",
                threshold_setup ? "setting up" : "cleaning up",
                priv->enabled ? "enabled" : "disabled",
                priv->rssi_threshold,
                priv->error_rate_threshold ? "enabled" : "disabled");

    MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->setup_thresholds (
        self,
        priv->rssi_threshold,
        priv->error_rate_threshold,
        (GAsyncReadyCallback)setup_thresholds_ready,
        task);
}

/*****************************************************************************/

typedef struct {
    GDBusMethodInvocation *invocation;
    MmGdbusModemSignal    *skeleton;
    guint                  rate;
} HandleSetupContext;

static void
handle_setup_context_free (HandleSetupContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->skeleton);
    g_slice_free (HandleSetupContext, ctx);
}

static void
handle_setup_auth_ready (MMBaseModem        *_self,
                         GAsyncResult       *res,
                         HandleSetupContext *ctx)
{
    MMIfaceModemSignal *self = MM_IFACE_MODEM_SIGNAL (_self);
    GError             *error = NULL;
    Private            *priv;

    if (!mm_base_modem_authorize_finish (_self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_setup_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_DISABLED)) {
        handle_setup_context_free (ctx);
        return;
    }

    priv = get_private (self);
    priv->rate = ctx->rate;
    polling_restart (self);
    check_interface_reset (self);
    mm_gdbus_modem_signal_set_rate (ctx->skeleton, ctx->rate);
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

    ctx = g_slice_new0 (HandleSetupContext);
    ctx->invocation = g_object_ref (invocation);
    ctx->skeleton = g_object_ref (skeleton);
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
    GVariant              *settings;
    guint                  previous_rssi_threshold;
    gboolean               previous_error_rate_threshold;
} HandleSetupThresholdsContext;

static void
handle_setup_thresholds_context_free (HandleSetupThresholdsContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->skeleton);
    if (ctx->settings)
        g_variant_unref (ctx->settings);
    g_slice_free (HandleSetupThresholdsContext, ctx);
}

static void
setup_thresholds_restart_ready (MMIfaceModemSignal           *self,
                                GAsyncResult                 *res,
                                HandleSetupThresholdsContext *ctx)
{
    GError  *error = NULL;
    Private *priv;

    priv = get_private (self);

    if (!thresholds_restart_finish (self, res, &error)) {
        priv->rssi_threshold = ctx->previous_rssi_threshold;
        priv->error_rate_threshold = ctx->previous_error_rate_threshold;
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        check_interface_reset (self);
        mm_gdbus_modem_signal_set_rssi_threshold (ctx->skeleton, priv->rssi_threshold);
        mm_gdbus_modem_signal_set_error_rate_threshold (ctx->skeleton, priv->error_rate_threshold);
        mm_gdbus_modem_signal_complete_setup_thresholds (ctx->skeleton, ctx->invocation);
    }

    handle_setup_thresholds_context_free (ctx);
}

static void
handle_setup_thresholds_auth_ready (MMBaseModem                  *_self,
                                    GAsyncResult                 *res,
                                    HandleSetupThresholdsContext *ctx)
{
    g_autoptr(MMSignalThresholdProperties)  properties = NULL;
    MMIfaceModemSignal                     *self = MM_IFACE_MODEM_SIGNAL (_self);
    GError                                 *error = NULL;
    Private                                *priv;
    guint                                   new_rssi_threshold;
    gboolean                                new_error_rate_threshold;

    priv = get_private (self);

    if (!mm_base_modem_authorize_finish (_self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->setup_thresholds ||
        !MM_IFACE_MODEM_SIGNAL_GET_INTERFACE (self)->setup_thresholds_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot setup thresholds: operation not supported");
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_DISABLED)) {
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    properties = mm_signal_threshold_properties_new_from_dictionary (ctx->settings, &error);
    if (!properties) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_setup_thresholds_context_free (ctx);
        return;
    }
    new_rssi_threshold       = mm_signal_threshold_properties_get_rssi       (properties);
    new_error_rate_threshold = mm_signal_threshold_properties_get_error_rate (properties);

    if ((new_rssi_threshold == priv->rssi_threshold) &&
        (new_error_rate_threshold == priv->error_rate_threshold)) {
        mm_gdbus_modem_signal_complete_setup_thresholds (ctx->skeleton, ctx->invocation);
        handle_setup_thresholds_context_free (ctx);
        return;
    }

    ctx->previous_rssi_threshold = priv->rssi_threshold;
    ctx->previous_error_rate_threshold = priv->error_rate_threshold;
    priv->rssi_threshold = new_rssi_threshold;
    priv->error_rate_threshold = new_error_rate_threshold;

    thresholds_restart (self,
                        (GAsyncReadyCallback)setup_thresholds_restart_ready,
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
    ctx->settings = g_variant_ref (settings);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_setup_thresholds_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Common enable/disable */

static gboolean
common_enable_disable_finish (MMIfaceModemSignal  *self,
                              GAsyncResult        *res,
                              GError             **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enable_disable_thresholds_restart_ready (MMIfaceModemSignal *self,
                                         GAsyncResult       *res,
                                         GTask              *task)
{
    GError *error = NULL;

    if (!thresholds_restart_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_enable_disable (MMIfaceModemSignal  *self,
                       gboolean             enabled,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
    GTask   *task;
    Private *priv;

    task = g_task_new (self, cancellable, callback, user_data);

    priv = get_private (MM_IFACE_MODEM_SIGNAL (self));
    priv->enabled = enabled;

    check_interface_reset (self);

    polling_restart (self);

    thresholds_restart (self,
                        (GAsyncReadyCallback)enable_disable_thresholds_restart_ready,
                        task);
}

/*****************************************************************************/

gboolean
mm_iface_modem_signal_disable_finish (MMIfaceModemSignal  *self,
                                      GAsyncResult        *res,
                                      GError             **error)
{
    return common_enable_disable_finish (self, res, error);
}

void
mm_iface_modem_signal_disable (MMIfaceModemSignal  *self,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
    common_enable_disable (self, FALSE, NULL, callback, user_data);
}

/*****************************************************************************/

gboolean
mm_iface_modem_signal_enable_finish (MMIfaceModemSignal  *self,
                                     GAsyncResult        *res,
                                     GError             **error)
{
    return common_enable_disable_finish (self, res, error);
}

void
mm_iface_modem_signal_enable (MMIfaceModemSignal  *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
    common_enable_disable (self, TRUE, cancellable, callback, user_data);
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
