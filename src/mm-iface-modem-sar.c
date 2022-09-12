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
 * Copyright (C) 2021 Fibocom Wireless Inc.
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-sar.h"
#include "mm-log-object.h"

#define SUPPORT_CHECKED_TAG "sar-support-checked-tag"
#define SUPPORTED_TAG       "sar-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/

void
mm_iface_modem_sar_bind_simple_status (MMIfaceModemSar *self,
                                       MMSimpleStatus  *status)
{
}

guint
mm_iface_modem_sar_get_power_level (MMIfaceModemSar *self)
{
    MmGdbusModemSar *skeleton = NULL;
    guint            level;

    g_object_get (self,
                  MM_IFACE_MODEM_SAR_DBUS_SKELETON, &skeleton,
                  NULL);

    if (!skeleton)
        return 0;

    level = mm_gdbus_modem_sar_get_power_level (skeleton);
    g_object_unref (skeleton);
    return level;
}

/*****************************************************************************/
/* Handle Enable() */

typedef struct {
    MmGdbusModemSar *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemSar *self;
    gboolean enable;
} HandleEnableContext;

static void
handle_enable_context_free (HandleEnableContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleEnableContext, ctx);
}

static void
enable_ready (MMIfaceModemSar     *self,
              GAsyncResult        *res,
              HandleEnableContext *ctx)
{
    GError *error = NULL;
    guint   power_level = 0;

    if (!MM_IFACE_MODEM_SAR_GET_INTERFACE (ctx->self)->enable_finish (self, res, &power_level, &error)) {
        mm_obj_warn (self, "failed %s SAR: %s", ctx->enable ? "enabling" : "disabling", error->message);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        mm_obj_info (self, "%s SAR", ctx->enable ? "enabled" : "disabled");
        mm_gdbus_modem_sar_set_state (ctx->skeleton, ctx->enable);
        if (ctx->enable)
            mm_gdbus_modem_sar_set_power_level (ctx->skeleton, power_level);
        mm_gdbus_modem_sar_complete_enable (ctx->skeleton, ctx->invocation);
    }

    handle_enable_context_free (ctx);
}

static void
handle_enable_auth_ready (MMBaseModem         *self,
                          GAsyncResult        *res,
                          HandleEnableContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_enable_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_SAR_GET_INTERFACE (ctx->self)->enable ||
        !MM_IFACE_MODEM_SAR_GET_INTERFACE (ctx->self)->enable_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot setup SAR: "
                                               "operation not supported");
        handle_enable_context_free (ctx);
        return;
    }

    if (mm_gdbus_modem_sar_get_state (ctx->skeleton) == ctx->enable) {
        mm_gdbus_modem_sar_complete_enable (ctx->skeleton, ctx->invocation);
        handle_enable_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to %s SAR...", ctx->enable ? "enable" : "disable");
    MM_IFACE_MODEM_SAR_GET_INTERFACE (ctx->self)->enable (ctx->self,
                                                          ctx->enable,
                                                          (GAsyncReadyCallback)enable_ready,
                                                          ctx);
}

static gboolean
handle_enable (MmGdbusModemSar       *skeleton,
               GDBusMethodInvocation *invocation,
               gboolean               enable,
               MMIfaceModemSar       *self)
{
    HandleEnableContext *ctx;

    ctx = g_slice_new0 (HandleEnableContext);
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
/* Handle Set Power Level() */

typedef struct {
    MmGdbusModemSar       *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemSar       *self;
    guint                  power_level;
} HandleSetPowerLevelContext;

static void
handle_set_power_level_context_free (HandleSetPowerLevelContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetPowerLevelContext, ctx);
}

static void
set_power_level_ready (MMIfaceModemSar            *self,
                       GAsyncResult               *res,
                       HandleSetPowerLevelContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_SAR_GET_INTERFACE (ctx->self)->set_power_level_finish (self, res, &error)) {
        mm_obj_warn (self, "failed setting SAR power level to %u: %s", ctx->power_level, error->message);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        mm_obj_info (self, "SAR power level set to %u", ctx->power_level);
        mm_gdbus_modem_sar_set_power_level (ctx->skeleton, ctx->power_level);
        mm_gdbus_modem_sar_complete_set_power_level (ctx->skeleton, ctx->invocation);
    }

    handle_set_power_level_context_free (ctx);
}

static void
handle_set_power_level_auth_ready (MMBaseModem                *self,
                                   GAsyncResult               *res,
                                   HandleSetPowerLevelContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_power_level_context_free (ctx);
        return;
    }

    if (!MM_IFACE_MODEM_SAR_GET_INTERFACE (ctx->self)->set_power_level ||
        !MM_IFACE_MODEM_SAR_GET_INTERFACE (ctx->self)->set_power_level_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set SAR power level: "
                                               "operation not supported");
        handle_set_power_level_context_free (ctx);
        return;
    }

    if (!mm_gdbus_modem_sar_get_state (ctx->skeleton)) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set SAR power level: SAR is disabled");
        handle_set_power_level_context_free (ctx);
        return;
    }

    if (mm_gdbus_modem_sar_get_power_level (ctx->skeleton) == ctx->power_level) {
        mm_gdbus_modem_sar_complete_set_power_level (ctx->skeleton, ctx->invocation);
        handle_set_power_level_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to set SAR power level to %u...", ctx->power_level);
    MM_IFACE_MODEM_SAR_GET_INTERFACE (ctx->self)->set_power_level (
        ctx->self,
        ctx->power_level,
        (GAsyncReadyCallback)set_power_level_ready,
        ctx);
}

static gboolean
handle_set_power_level (MmGdbusModemSar       *skeleton,
                        GDBusMethodInvocation *invocation,
                        guint                  level,
                        MMIfaceModemSar       *self)
{
    HandleSetPowerLevelContext *ctx;

    ctx = g_slice_new0 (HandleSetPowerLevelContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->power_level = level;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_power_level_auth_ready,
                             ctx);

    return TRUE;
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
    INITIALIZATION_STEP_LOAD_STATE,
    INITIALIZATION_STEP_LOAD_POWER_LEVEL,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModemSar   *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_sar_initialize_finish (MMIfaceModemSar  *self,
                                      GAsyncResult     *res,
                                      GError          **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
load_power_level_ready (MMIfaceModemSar *self,
                        GAsyncResult    *res,
                        GTask           *task)
{
    InitializationContext *ctx;
    GError                *error = NULL;
    guint                  level;

    if (!MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->load_power_level_finish (self, res, &level, &error)) {
        mm_obj_warn (self, "loading SAR power level failed: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    mm_gdbus_modem_sar_set_power_level (ctx->skeleton, level);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_state_ready (MMIfaceModemSar *self,
                  GAsyncResult    *res,
                  GTask           *task)
{
    InitializationContext *ctx;
    gboolean               state;
    GError                *error = NULL;

    if (!MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->load_state_finish (self, res, &state, &error)) {
        mm_obj_warn (self, "loading SAR state failed: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    mm_gdbus_modem_sar_set_state (ctx->skeleton, state);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
check_support_ready (MMIfaceModemSar *self,
                     GAsyncResult    *res,
                     GTask           *task)
{
    InitializationContext *ctx;
    g_autoptr(GError) error = NULL;

    if (!MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->check_support_finish (self, res, &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_obj_dbg (self, "SAR support check failed: %s", error->message);
        }
    } else {
        /* SAR is supported! */
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
    MMIfaceModemSar *self;
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

            if (MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->check_support &&
                MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->check_support (
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
                                     "SAR not supported");
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* fall through */

     case INITIALIZATION_STEP_LOAD_STATE:
        if (MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->load_state &&
            MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->load_state_finish) {
            MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->load_state (
                self,
                (GAsyncReadyCallback)load_state_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LOAD_POWER_LEVEL:
        if (MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->load_power_level &&
            MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->load_power_level_finish) {
            MM_IFACE_MODEM_SAR_GET_INTERFACE (self)->load_power_level (
                self,
                (GAsyncReadyCallback)load_power_level_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-enable",
                          G_CALLBACK (handle_enable),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-power-level",
                          G_CALLBACK (handle_set_power_level),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_sar (MM_GDBUS_OBJECT_SKELETON (self),
                                                MM_GDBUS_MODEM_SAR (ctx->skeleton));
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_sar_initialize (MMIfaceModemSar      *self,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
    InitializationContext *ctx;
    MmGdbusModemSar       *skeleton = NULL;
    GTask                 *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_SAR_DBUS_SKELETON, &skeleton,
                  NULL);

    if (!skeleton) {
        skeleton = mm_gdbus_modem_sar_skeleton_new ();
        g_object_set (self,
                      MM_IFACE_MODEM_SAR_DBUS_SKELETON, skeleton,
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

/*****************************************************************************/

void
mm_iface_modem_sar_shutdown (MMIfaceModemSar *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_sar (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_SAR_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_sar_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_SAR_DBUS_SKELETON,
                              "SAR DBus skeleton",
                              "DBus skeleton for the SAR interface",
                              MM_GDBUS_TYPE_MODEM_SAR_SKELETON,
                              G_PARAM_READWRITE));
    initialized = TRUE;
}

GType
mm_iface_modem_sar_get_type (void)
{
    static GType iface_modem_sar_type = 0;

    if (!G_UNLIKELY (iface_modem_sar_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemSar), /* class_size */
            iface_modem_sar_init,     /* base_init */
            NULL,                     /* base_finalize */
        };

        iface_modem_sar_type = g_type_register_static (G_TYPE_INTERFACE,
                                                       "MMIfaceModemSar",
                                                       &info,
                                                       0);

        g_type_interface_add_prerequisite (iface_modem_sar_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_sar_type;
}
