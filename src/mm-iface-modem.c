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

#include <mm-gdbus-modem.h>
#include <mm-gdbus-sim.h>
#include <mm-enums-types.h>
#include <mm-errors-types.h>

#include "mm-iface-modem.h"
#include "mm-base-modem.h"
#include "mm-sim.h"
#include "mm-log.h"

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    INTERFACE_STATUS_SHUTDOWN,
    INTERFACE_STATUS_INITIALIZING,
    INTERFACE_STATUS_INITIALIZED,
    INTERFACE_STATUS_ENABLED,
} InterfaceStatus;

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
} DbusCallContext;

static void
dbus_call_context_free (DbusCallContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static DbusCallContext *
dbus_call_context_new (MmGdbusModem *skeleton,
                       GDBusMethodInvocation *invocation,
                       MMIfaceModem *self)
{
    DbusCallContext *ctx;

    ctx = g_new (DbusCallContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    return ctx;
}

/*****************************************************************************/

static gboolean
handle_create_bearer (MmGdbusModem *object,
                      GDBusMethodInvocation *invocation,
                      GVariant *arg_properties,
                      MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_delete_bearer (MmGdbusModem *object,
                      GDBusMethodInvocation *invocation,
                      const gchar *arg_bearer,
                      MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_list_bearers (MmGdbusModem *object,
                     GDBusMethodInvocation *invocation,
                     MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

/*****************************************************************************/

static void
update_state (MMIfaceModem *self,
              MMModemState new_state,
              MMModemStateReason reason)
{
    MMModemState old_state = MM_MODEM_STATE_UNKNOWN;
    MmGdbusModem *skeleton = NULL;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &old_state,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    if (new_state != old_state) {
        GEnumClass *enum_class;
        GEnumValue *new_value;
        GEnumValue *old_value;
        const gchar *dbus_path;

        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_STATE));
        new_value = g_enum_get_value (enum_class, new_state);
        old_value = g_enum_get_value (enum_class, old_state);
        dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
        if (dbus_path)
            mm_info ("Modem %s: state changed (%s -> %s)",
                     dbus_path,
                     old_value->value_nick,
                     new_value->value_nick);
        else
            mm_info ("Modem: state changed (%s -> %s)",
                     old_value->value_nick,
                     new_value->value_nick);
        g_type_class_unref (enum_class);

        /* The property in the interface is bound to the property
         * in the skeleton, so just updating here is enough */
        g_object_set (self,
                      MM_IFACE_MODEM_STATE, new_state,
                      NULL);

        /* Signal status change */
        mm_gdbus_modem_emit_state_changed (skeleton,
                                           old_state,
                                           new_state,
                                           reason);
    }
}

/*****************************************************************************/

static gboolean
handle_enable (MmGdbusModem *object,
               GDBusMethodInvocation *invocation,
               gboolean arg_enable,
               MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

/*****************************************************************************/

static void
reset_ready (MMIfaceModem *self,
             GAsyncResult *res,
             DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->reset_finish (self,
                                                            res,
                                                            &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_complete_reset (ctx->skeleton,
                                       ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_reset (MmGdbusModem *skeleton,
              GDBusMethodInvocation *invocation,
              MMIfaceModem *self)
{
    MMModemState modem_state;

    /* If reseting is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->reset ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->reset_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot reset the modem: operation not supported");
        return TRUE;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot reset modem: not initialized/unlocked yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_GET_INTERFACE (self)->reset (self,
                                                    (GAsyncReadyCallback)reset_ready,
                                                    dbus_call_context_new (skeleton,
                                                                           invocation,
                                                                           self));
        break;
    }

    return TRUE;
}

/*****************************************************************************/

static void
factory_reset_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset_finish (self,
                                                                    res,
                                                                    &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_complete_factory_reset (ctx->skeleton,
                                               ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_factory_reset (MmGdbusModem *skeleton,
                      GDBusMethodInvocation *invocation,
                      const gchar *arg_code,
                      MMIfaceModem *self)
{
    MMModemState modem_state;

    /* If reseting is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot reset the modem to factory defaults: "
                                               "operation not supported");
        return TRUE;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot reset the modem to factory defaults: "
                                               "not initialized/unlocked yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset (self,
                                                            arg_code,
                                                            (GAsyncReadyCallback)factory_reset_ready,
                                                            dbus_call_context_new (skeleton,
                                                                                   invocation,
                                                                                   self));
        break;
    }

    return TRUE;
}

/*****************************************************************************/

static void
set_allowed_bands_ready (MMIfaceModem *self,
                         GAsyncResult *res,
                         DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands_finish (self,
                                                                        res,
                                                                        &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_complete_set_allowed_bands (ctx->skeleton,
                                                   ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_set_allowed_bands (MmGdbusModem *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint64 arg_bands,
                          MMIfaceModem *self)
{
    MMModemState modem_state;

    /* If setting allowed bands is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Setting allowed bands not supported");
        return TRUE;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot reset the modem to factory defaults: "
                                               "not initialized/unlocked yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands (self,
                                                                arg_bands,
                                                                (GAsyncReadyCallback)set_allowed_bands_ready,
                                                                dbus_call_context_new (skeleton,
                                                                                       invocation,
                                                                                       self));
        break;
    }

    return TRUE;
}

/*****************************************************************************/

static void
set_allowed_modes_ready (MMIfaceModem *self,
                         GAsyncResult *res,
                         DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes_finish (self,
                                                                        res,
                                                                        &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_complete_set_allowed_modes (ctx->skeleton,
                                                   ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_set_allowed_modes (MmGdbusModem *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint arg_modes,
                          guint arg_preferred,
                          MMIfaceModem *self)
{
    MMModemState modem_state;

    /* If setting allowed modes is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Setting allowed modes not supported");
        return TRUE;

    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot reset the modem to factory defaults: "
                                               "not initialized/unlocked yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes (self,
                                                                arg_modes,
                                                                arg_preferred,
                                                                (GAsyncReadyCallback)set_allowed_modes_ready,
                                                                dbus_call_context_new (skeleton,
                                                                                       invocation,
                                                                                       self));
        break;
    }

    return TRUE;
}

/*****************************************************************************/

typedef struct _UnlockCheckContext UnlockCheckContext;
struct _UnlockCheckContext {
    MMIfaceModem *self;
    MMAtSerialPort *port;
    guint pin_check_tries;
    guint pin_check_timeout_id;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static UnlockCheckContext *
unlock_check_context_new (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    UnlockCheckContext *ctx;

    ctx = g_new0 (UnlockCheckContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             unlock_check_context_new);
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
unlock_check_context_free (UnlockCheckContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
restart_initialize_idle (MMIfaceModem *self)
{
    mm_iface_modem_initialize (self, NULL, NULL);
    return FALSE;
}

static void
set_lock_status (MMIfaceModem *self,
                 MmGdbusModem *skeleton,
                 MMModemLock lock)
{
    MMModemLock old_lock;

    old_lock = mm_gdbus_modem_get_unlock_required (skeleton);
    mm_gdbus_modem_set_unlock_required (skeleton, lock);

    if (lock == MM_MODEM_LOCK_NONE) {
        if (old_lock != MM_MODEM_LOCK_NONE) {
            /* Notify transition from UNKNOWN/LOCKED to DISABLED */
            update_state (self,
                          MM_MODEM_STATE_DISABLED,
                          MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
            g_idle_add ((GSourceFunc)restart_initialize_idle, self);
        }
    } else {
        if (old_lock == MM_MODEM_LOCK_UNKNOWN) {
            /* Notify transition from UNKNOWN to LOCKED */
            update_state (self,
                          MM_MODEM_STATE_LOCKED,
                          MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
        }
    }
}

MMModemLock
mm_iface_modem_unlock_check_finish (MMIfaceModem *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error)) {
        return MM_MODEM_LOCK_UNKNOWN;
    }

    return (MMModemLock) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void unlock_check_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                UnlockCheckContext *ctx);

static gboolean
unlock_check_again  (UnlockCheckContext *ctx)
{
    ctx->pin_check_timeout_id = 0;

    MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required (
        ctx->self,
        (GAsyncReadyCallback)unlock_check_ready,
        ctx);
    return FALSE;
}

static void
unlock_check_ready (MMIfaceModem *self,
                    GAsyncResult *res,
                    UnlockCheckContext *ctx)
{
    GError *error = NULL;
    MMModemLock lock;

    lock = MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_required_finish (self,
                                                                             res,
                                                                             &error);
    if (error) {
        /* Retry up to 3 times */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE &&
            ++ctx->pin_check_tries < 3) {

            if (ctx->pin_check_timeout_id)
                g_source_remove (ctx->pin_check_timeout_id);
            ctx->pin_check_timeout_id = g_timeout_add_seconds (
                2,
                (GSourceFunc)unlock_check_again,
                ctx);
            return;
        }

        /* If reached max retries and still reporting error, set UNKNOWN */
        lock = MM_MODEM_LOCK_UNKNOWN;
    }

    /* Update lock status and modem status if needed */
    set_lock_status (self, ctx->skeleton, lock);

    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (lock),
                                               NULL);
    g_simple_async_result_complete (ctx->result);
    unlock_check_context_free (ctx);
}

void
mm_iface_modem_unlock_check (MMIfaceModem *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    UnlockCheckContext *ctx;

    ctx = unlock_check_context_new (self, callback, user_data);

    if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required &&
        MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required_finish) {
        MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required (
            self,
            (GAsyncReadyCallback)unlock_check_ready,
            ctx);
        return;
    }

    /* Just assume that no lock is required */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (MM_MODEM_LOCK_NONE),
                                               NULL);
    g_simple_async_result_complete_in_idle (ctx->result);
    unlock_check_context_free (ctx);
}

/*****************************************************************************/

typedef struct _SignalQualityCheckContext SignalQualityCheckContext;
struct _SignalQualityCheckContext {
    MMIfaceModem *self;
    MMAtSerialPort *port;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static SignalQualityCheckContext *
signal_quality_check_context_new (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    SignalQualityCheckContext *ctx;

    ctx = g_new0 (SignalQualityCheckContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             signal_quality_check_context_new);
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
signal_quality_check_context_free (SignalQualityCheckContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

guint
mm_iface_modem_signal_quality_check_finish (MMIfaceModem *self,
                                            GAsyncResult *res,
                                            gboolean *recent,
                                            GError **error)
{
    guint quality = 0;
    gboolean is_recent = FALSE;

    if (!g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error)) {
        GVariant *result;

        result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
        g_variant_get (result, "(ub)", &quality, &is_recent);
    }

    if (recent)
        *recent = is_recent;
    return quality;
}

static void
signal_quality_check_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            SignalQualityCheckContext *ctx)
{
    GError *error = NULL;
    guint quality;
    gboolean is_recent;

    quality = MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality_finish (self,
                                                                               res,
                                                                               &is_recent,
                                                                               &error);
    if (error)
        g_simple_async_result_take_error (ctx->result, error);
    else {
        GVariant *result;

        result = g_variant_new ("(ub)", quality, is_recent);
        /* Set operation result */
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   g_variant_ref (result),
                                                   (GDestroyNotify)g_variant_unref);
        /* Set the property value in the DBus skeleton */
        mm_gdbus_modem_set_signal_quality (ctx->skeleton, g_variant_ref (result));
        g_variant_unref (result);
    }
    g_simple_async_result_complete (ctx->result);
    signal_quality_check_context_free (ctx);
}

void
mm_iface_modem_signal_quality_check (MMIfaceModem *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    SignalQualityCheckContext *ctx;

    ctx = signal_quality_check_context_new (self,
                                            callback,
                                            user_data);

    if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_signal_quality &&
        MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_signal_quality_finish) {
        MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_signal_quality (
            ctx->self,
            (GAsyncReadyCallback)signal_quality_check_ready,
            ctx);
        return;
    }

    /* Cannot load signal quality... set operation result */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               g_variant_new ("(ub)", 0, FALSE),
                                               (GDestroyNotify)g_variant_unref);
    g_simple_async_result_complete_in_idle (ctx->result);
    signal_quality_check_context_free (ctx);
}

/*****************************************************************************/

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModem *self;
    EnablingStep step;
    gboolean enabled;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    EnablingContext *ctx;

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enabling_context_new);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    update_state (ctx->self,
                  MM_MODEM_STATE_ENABLING,
                  MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);

    return ctx;
}

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->enabled)
        update_state (ctx->self,
                      MM_MODEM_STATE_ENABLED,
                      MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);
    else {
        /* Fallback to DISABLED/LOCKED */
        update_state (ctx->self,
                      (mm_gdbus_modem_get_unlock_required (ctx->skeleton) == MM_MODEM_LOCK_NONE ?
                       MM_MODEM_STATE_DISABLED :
                       MM_MODEM_STATE_LOCKED),
                      MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
    }

    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_enable_finish (MMIfaceModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
interface_enabling_step (EnablingContext *ctx)
{
    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        ctx->enabled = TRUE;
        enabling_context_complete_and_free (ctx);
        /* mm_serial_port_close (MM_SERIAL_PORT (ctx->port)); */
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_enable (MMIfaceModem *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    interface_enabling_step (enabling_context_new (self,
                                                   callback,
                                                   user_data));
}

/*****************************************************************************/

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CURRENT_CAPABILITIES,
    INITIALIZATION_STEP_MODEM_CAPABILITIES,
    INITIALIZATION_STEP_MAX_BEARERS,
    INITIALIZATION_STEP_MAX_ACTIVE_BEARERS,
    INITIALIZATION_STEP_MANUFACTURER,
    INITIALIZATION_STEP_MODEL,
    INITIALIZATION_STEP_REVISION,
    INITIALIZATION_STEP_EQUIPMENT_ID,
    INITIALIZATION_STEP_DEVICE_ID,
    INITIALIZATION_STEP_UNLOCK_REQUIRED,
    INITIALIZATION_STEP_UNLOCK_RETRIES,
    INITIALIZATION_STEP_SIM,
    INITIALIZATION_STEP_SUPPORTED_MODES,
    INITIALIZATION_STEP_SUPPORTED_BANDS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem *self;
    MMAtSerialPort *port;
    InitializationStep step;
    guint pin_check_tries;
    guint pin_check_timeout_id;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static InitializationContext *
initialization_context_new (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    InitializationContext *ctx;

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_context_new);
    ctx->step = INITIALIZATION_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
interface_initialization_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
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
        mm_gdbus_modem_set_##NAME (ctx->skeleton, val); \
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
#define UINT_REPLY_READY_FN(NAME,DISPLAY)                               \
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
            mm_warn ("couldn't load %s: '%s'", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (ctx);                            \
    }

static void
load_current_capabilities_ready (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 InitializationContext *ctx)
{
    GError *error = NULL;

    /* We have the property in the interface bound to the property in the
     * skeleton. */
    g_object_set (self,
                  MM_IFACE_MODEM_CURRENT_CAPABILITIES,
                  MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_capabilities_finish (self, res, &error),
                  NULL);

    if (error) {
        mm_warn ("couldn't load Current Capabilities: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

UINT_REPLY_READY_FN (modem_capabilities, "Modem Capabilities")
UINT_REPLY_READY_FN (max_bearers, "Max Bearers")
UINT_REPLY_READY_FN (max_active_bearers, "Max Active Bearers")
STR_REPLY_READY_FN (manufacturer, "Manufacturer")
STR_REPLY_READY_FN (model, "Model")
STR_REPLY_READY_FN (revision, "Revision")
STR_REPLY_READY_FN (equipment_identifier, "Equipment Identifier")
STR_REPLY_READY_FN (device_identifier, "Device Identifier")
UINT_REPLY_READY_FN (supported_modes, "Supported Modes")
UINT_REPLY_READY_FN (supported_bands, "Supported Bands")

static void
load_unlock_required_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            InitializationContext *ctx)
{
    GError *error = NULL;

    /* NOTE: we already propagated the lock state, no need to do it again */
    mm_iface_modem_unlock_check_finish (self, res, &error);
    if (error) {
        mm_warn ("couldn't load unlock required status: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

UINT_REPLY_READY_FN (unlock_retries, "Unlock Retries")

static void
sim_new_ready (GAsyncInitable *initable,
               GAsyncResult *res,
               InitializationContext *ctx)
{
    MMSim *sim;
    GError *error = NULL;

    sim = mm_sim_new_finish (initable, res, &error);
    if (!sim) {
        mm_warn ("couldn't create SIM: '%s'",
                 error ? error->message : "Unknown error");
        g_clear_error (&error);
    } else {
        gchar *path = NULL;

        g_object_get (sim,
                      MM_SIM_PATH, &path,
                      NULL);
        mm_gdbus_modem_set_sim (MM_GDBUS_MODEM (ctx->skeleton),
                                path);
        g_object_bind_property (sim, MM_SIM_PATH,
                                ctx->skeleton, "sim",
                                G_BINDING_DEFAULT);
        g_free (path);

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
        if (!mm_gdbus_modem_get_driver (ctx->skeleton)) {
            gchar *driver;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_DRIVER, &driver,
                          NULL);
            mm_gdbus_modem_set_driver (ctx->skeleton, driver);
            g_free (driver);
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

    case INITIALIZATION_STEP_MAX_BEARERS:
        /* Max bearers value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_max_bearers (ctx->skeleton) == 0 &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_bearers &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_bearers_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_bearers (
                ctx->self,
                (GAsyncReadyCallback)load_max_bearers_ready,
                ctx);
            return;
        }
        /* Default to one bearer */
        mm_gdbus_modem_set_max_bearers (ctx->skeleton, 1);
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_MAX_ACTIVE_BEARERS:
        /* Max active bearers value is meant to be loaded only once during the
         * whole lifetime of the modem. Therefore, if we already have them
         * loaded, don't try to load them again. */
        if (mm_gdbus_modem_get_max_active_bearers (ctx->skeleton) == 0 &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_active_bearers &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_active_bearers_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_max_active_bearers (
                ctx->self,
                (GAsyncReadyCallback)load_max_active_bearers_ready,
                ctx);
            return;
        }
       /* If no specific way of getting max active bearers, assume they are
        * equal to the absolute max bearers */
        mm_gdbus_modem_set_max_active_bearers (
            ctx->skeleton,
            mm_gdbus_modem_get_max_bearers (ctx->skeleton));
        /* Fall down to next step */
        ctx->step++;

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

    case INITIALIZATION_STEP_UNLOCK_REQUIRED:
        /* Only check unlock required if we were previously not unlocked */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE) {
            mm_iface_modem_unlock_check (ctx->self,
                                         (GAsyncReadyCallback)load_unlock_required_ready,
                                         ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_UNLOCK_RETRIES:
        if ((MMModemLock)mm_gdbus_modem_get_unlock_required (ctx->skeleton) == MM_MODEM_LOCK_NONE) {
            /* Default to 0 when unlocked */
            mm_gdbus_modem_set_unlock_retries (ctx->skeleton, 0);
        } else {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries (
                    ctx->self,
                    (GAsyncReadyCallback)load_unlock_retries_ready,
                    ctx);
                return;
            }

            /* Default to 999 when we cannot check it */
            mm_gdbus_modem_set_unlock_retries (ctx->skeleton, 999);
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_SIM: {
        MMSim *sim = NULL;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_SIM, &sim,
                      NULL);
        if (!sim) {
            mm_sim_new (MM_BASE_MODEM (ctx->self),
                        NULL, /* TODO: cancellable */
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
        return;
    }

    case INITIALIZATION_STEP_SUPPORTED_MODES:
        /* Supported modes are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_supported_modes (ctx->skeleton) == MM_MODEM_MODE_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes (
                ctx->self,
                (GAsyncReadyCallback)load_supported_modes_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_SUPPORTED_BANDS:
        /* Supported bands are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_supported_bands (ctx->skeleton) == MM_MODEM_BAND_UNKNOWN &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands (
                ctx->self,
                (GAsyncReadyCallback)load_supported_bands_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        g_simple_async_result_complete_in_idle (ctx->result);
        mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
        initialization_context_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

static void
interface_initialization (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    InitializationContext *ctx;
    GError *error = NULL;

    ctx = initialization_context_new (self, callback, user_data);

    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        initialization_context_free (ctx);
        return;
    }

    /* Try to disable echo */
    mm_at_serial_port_queue_command (ctx->port, "E0", 3, NULL, NULL);
    /* Try to get extended errors */
    mm_at_serial_port_queue_command (ctx->port, "+CMEE=1", 2, NULL, NULL);

    interface_initialization_step (ctx);
}

/*****************************************************************************/


static InterfaceStatus
get_status (MMIfaceModem *self)
{
    GObject *skeleton = NULL;
    MMModemState modem_state;

    /* Are we already disabled? */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return INTERFACE_STATUS_SHUTDOWN;
    g_object_unref (skeleton);

    /* Are we being initialized? (interface not yet exported) */
    skeleton = G_OBJECT (mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)));
    if (!skeleton)
        return INTERFACE_STATUS_INITIALIZING;

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    g_object_unref (skeleton);

    return (modem_state > MM_MODEM_STATE_DISABLED ?
            INTERFACE_STATUS_ENABLED :
            INTERFACE_STATUS_INITIALIZED);
}

gboolean
mm_iface_modem_initialize_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
interface_initialization_ready (MMIfaceModem *self,
                                GAsyncResult *init_result,
                                GSimpleAsyncResult *op_result)
{
    GObject *skeleton = NULL;
    GError *inner_error = NULL;

    /* If initialization failed, remove the skeleton and return the error */
    if (!interface_initialization_finish (self,
                                          init_result,
                                          &inner_error)) {
        g_object_set (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, NULL,
                      NULL);
        g_simple_async_result_take_error (op_result, inner_error);
        g_simple_async_result_complete (op_result);
        g_object_unref (op_result);
        return;
    }

    /* Finish current initialization by setting up the DBus skeleton */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    g_assert (skeleton != NULL);

    /* Handle method invocations */
    g_signal_connect (skeleton,
                      "handle-create-bearer",
                      G_CALLBACK (handle_create_bearer),
                      self);
    g_signal_connect (skeleton,
                      "handle-delete-bearer",
                      G_CALLBACK (handle_delete_bearer),
                      self);
    g_signal_connect (skeleton,
                      "handle-list-bearers",
                      G_CALLBACK (handle_list_bearers),
                      self);
    g_signal_connect (skeleton,
                      "handle-enable",
                      G_CALLBACK (handle_enable),
                      self);
    g_signal_connect (skeleton,
                      "handle-reset",
                      G_CALLBACK (handle_reset),
                      self);
    g_signal_connect (skeleton,
                      "handle-factory-reset",
                      G_CALLBACK (handle_factory_reset),
                      self);
    g_signal_connect (skeleton,
                      "handle-set-allowed-bands",
                      G_CALLBACK (handle_set_allowed_bands),
                      self);
    g_signal_connect (skeleton,
                      "handle-set-allowed-modes",
                      G_CALLBACK (handle_set_allowed_modes),
                      self);

    /* Finally, export the new interface */
    mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self),
                                        MM_GDBUS_MODEM (skeleton));
    g_simple_async_result_set_op_res_gboolean (op_result, TRUE);
    g_simple_async_result_complete (op_result);
    g_object_unref (op_result);
}

void
mm_iface_modem_initialize (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_IS_IFACE_MODEM (self));

    /* Setup asynchronous result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_initialize);

    switch (get_status (self)) {
    case INTERFACE_STATUS_ENABLED:
    case INTERFACE_STATUS_INITIALIZED:
    case INTERFACE_STATUS_SHUTDOWN: {
        MmGdbusModem *skeleton = NULL;
        MMModemState modem_state = MM_MODEM_STATE_UNKNOWN;

        /* Did we already create it? */
        g_object_get (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                      MM_IFACE_MODEM_STATE, &modem_state,
                      NULL);
        if (!skeleton) {
            skeleton = mm_gdbus_modem_skeleton_new ();

            /* Set all initial property defaults */
            mm_gdbus_modem_set_sim (skeleton, NULL);
            mm_gdbus_modem_set_modem_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
            mm_gdbus_modem_set_max_bearers (skeleton, 0);
            mm_gdbus_modem_set_max_active_bearers (skeleton, 0);
            mm_gdbus_modem_set_manufacturer (skeleton, NULL);
            mm_gdbus_modem_set_model (skeleton, NULL);
            mm_gdbus_modem_set_revision (skeleton, NULL);
            mm_gdbus_modem_set_device_identifier (skeleton, NULL);
            mm_gdbus_modem_set_device (skeleton, NULL);
            mm_gdbus_modem_set_driver (skeleton, NULL);
            mm_gdbus_modem_set_plugin (skeleton, NULL);
            mm_gdbus_modem_set_equipment_identifier (skeleton, NULL);
            mm_gdbus_modem_set_unlock_required (skeleton, MM_MODEM_LOCK_UNKNOWN);
            mm_gdbus_modem_set_unlock_retries (skeleton, 0);
            mm_gdbus_modem_set_access_technology (skeleton, MM_MODEM_ACCESS_TECH_UNKNOWN);
            mm_gdbus_modem_set_signal_quality (skeleton, g_variant_new ("(ub)", 0, FALSE));
            mm_gdbus_modem_set_supported_modes (skeleton, MM_MODEM_MODE_NONE);
            mm_gdbus_modem_set_allowed_modes (skeleton, MM_MODEM_MODE_ANY);
            mm_gdbus_modem_set_preferred_mode (skeleton, MM_MODEM_MODE_NONE);
            mm_gdbus_modem_set_supported_bands (skeleton, MM_MODEM_BAND_UNKNOWN);
            mm_gdbus_modem_set_allowed_bands (skeleton, MM_MODEM_BAND_ANY);

            /* Bind our State property */
            g_object_bind_property (self, MM_IFACE_MODEM_STATE,
                                    skeleton, "state",
                                    G_BINDING_DEFAULT);
            /* Bind our Capabilities property */
            g_object_bind_property (self, MM_IFACE_MODEM_CURRENT_CAPABILITIES,
                                    skeleton, "current-capabilities",
                                    G_BINDING_DEFAULT);

            g_object_set (self,
                          MM_IFACE_MODEM_STATE, modem_state,
                          MM_IFACE_MODEM_CURRENT_CAPABILITIES, MM_MODEM_CAPABILITY_NONE,
                          MM_IFACE_MODEM_DBUS_SKELETON, skeleton,
                          NULL);
        }

        /* Perform async initialization here */
        interface_initialization (self,
                                  (GAsyncReadyCallback)interface_initialization_ready,
                                  result);
        g_object_unref (skeleton);
        return;
    }

    case INTERFACE_STATUS_INITIALIZING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_IN_PROGRESS,
                                         "Interface is already being enabled");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    g_return_if_reached ();
}

gboolean
mm_iface_modem_shutdown (MMIfaceModem *self,
                         GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM (self), FALSE);

    switch (get_status (self)) {
    case INTERFACE_STATUS_SHUTDOWN:
        return TRUE;
    case INTERFACE_STATUS_INITIALIZING:
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_IN_PROGRESS,
                     "Iinterface being currently initialized");
        return FALSE;
    case INTERFACE_STATUS_ENABLED:
    case INTERFACE_STATUS_INITIALIZED:
        /* Remove SIM object */
        g_object_set (self,
                      MM_IFACE_MODEM_SIM, NULL,
                      NULL);
        /* Unexport DBus interface and remove the skeleton */
        mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self), NULL);
        g_object_set (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, NULL,
                      NULL);
        return TRUE;
    }

    g_return_val_if_reached (FALSE);
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
         g_param_spec_flags (MM_IFACE_MODEM_CURRENT_CAPABILITIES,
                             "Current capabilities",
                             "Current capabilities of the modem",
                             MM_TYPE_MODEM_CAPABILITY,
                             MM_MODEM_CAPABILITY_NONE,
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
