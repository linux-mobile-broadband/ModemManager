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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-base-sim.h"
#include "mm-base-modem-at.h"
#include "mm-base-modem.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);
static void log_object_iface_init     (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMBaseSim, mm_base_sim, MM_GDBUS_TYPE_SIM_SKELETON, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_MODEM,
    PROP_SLOT_NUMBER,
    PROP_LAST
};

enum {
    SIGNAL_PIN_LOCK_ENABLED,
    SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseSimPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    guint            dbus_id;

    /* The modem which owns this SIM */
    MMBaseModem *modem;
    /* The path where the SIM object is exported */
    gchar *path;

    /* The SIM slot number, which will be 0 always if the system
     * doesn't support multiple SIMS. */
     guint slot_number;
};

static guint signals[SIGNAL_LAST] = { 0 };

/*****************************************************************************/

void
mm_base_sim_export (MMBaseSim *self)
{
    gchar *path;

    path = g_strdup_printf (MM_DBUS_SIM_PREFIX "/%d", self->priv->dbus_id);
    g_object_set (self,
                  MM_BASE_SIM_PATH, path,
                  NULL);
    g_free (path);
}

/*****************************************************************************/
/* Reprobe when a puk lock is discovered after pin1_retries are exhausted */

static void
reprobe_if_puk_discovered (MMBaseSim *self,
                           GError *error)
{
    if (g_error_matches (error,
                         MM_MOBILE_EQUIPMENT_ERROR,
                         MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK)) {
        mm_obj_dbg (self, "Discovered PUK lock, discarding old modem...");
        mm_base_modem_process_sim_event (self->priv->modem);
    }
}

/*****************************************************************************/
/* CHANGE PIN (Generic implementation) */

static gboolean
change_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
change_pin_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
change_pin (MMBaseSim *self,
            const gchar *old_pin,
            const gchar *new_pin,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GTask *task;
    gchar *command;

    task = g_task_new (self, NULL, callback, user_data);

    command = g_strdup_printf ("+CPWD=\"SC\",\"%s\",\"%s\"",
                               old_pin,
                               new_pin);
    mm_base_modem_at_command (self->priv->modem,
                              command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)change_pin_ready,
                              task);
    g_free (command);
}

/*****************************************************************************/
/* CHANGE PIN (DBus call handling) */

typedef struct {
    MMBaseSim *self;
    GDBusMethodInvocation *invocation;
    gchar *old_pin;
    gchar *new_pin;
    GError *save_error;
} HandleChangePinContext;

static void
handle_change_pin_context_free (HandleChangePinContext *ctx)
{
    g_assert (ctx->save_error == NULL);

    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->old_pin);
    g_free (ctx->new_pin);
    g_free (ctx);
}

static void
after_change_update_lock_info_ready (MMIfaceModem *modem,
                                     GAsyncResult *res,
                                     HandleChangePinContext *ctx)
{
    /* We just want to ensure that we tried to update the unlock
     * retries, no big issue if it failed */
    mm_iface_modem_update_lock_info_finish (modem, res, NULL);

    if (ctx->save_error) {
        g_dbus_method_invocation_return_gerror (ctx->invocation, ctx->save_error);
        reprobe_if_puk_discovered (ctx->self, ctx->save_error);
        g_clear_error (&ctx->save_error);
    } else {
        mm_gdbus_sim_complete_change_pin (MM_GDBUS_SIM (ctx->self), ctx->invocation);
    }

    handle_change_pin_context_free (ctx);
}

static void
handle_change_pin_ready (MMBaseSim *self,
                         GAsyncResult *res,
                         HandleChangePinContext *ctx)
{
    MMModemLock known_lock = MM_MODEM_LOCK_UNKNOWN;

    if (!MM_BASE_SIM_GET_CLASS (self)->change_pin_finish (self, res, &ctx->save_error)) {
        if (g_error_matches (ctx->save_error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK))
            known_lock = MM_MODEM_LOCK_SIM_PUK;
    }

    mm_iface_modem_update_lock_info (
        MM_IFACE_MODEM (self->priv->modem),
        known_lock,
        (GAsyncReadyCallback)after_change_update_lock_info_ready,
        ctx);
}

static void
handle_change_pin_auth_ready (MMBaseModem *modem,
                              GAsyncResult *res,
                              HandleChangePinContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_change_pin_context_free (ctx);
        return;
    }

    /* If changing PIN is not implemented, report an error */
    if (!MM_BASE_SIM_GET_CLASS (ctx->self)->change_pin ||
        !MM_BASE_SIM_GET_CLASS (ctx->self)->change_pin_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot change PIN: "
                                               "operation not supported");
        handle_change_pin_context_free (ctx);
        return;
    }

    if (!mm_gdbus_sim_get_active (MM_GDBUS_SIM (ctx->self))) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot change PIN: "
                                               "SIM not currently active");
        handle_change_pin_context_free (ctx);
        return;
    }

    MM_BASE_SIM_GET_CLASS (ctx->self)->change_pin (ctx->self,
                                                   ctx->old_pin,
                                                   ctx->new_pin,
                                                   (GAsyncReadyCallback)handle_change_pin_ready,
                                                   ctx);
}

static gboolean
handle_change_pin (MMBaseSim *self,
                   GDBusMethodInvocation *invocation,
                   const gchar *old_pin,
                   const gchar *new_pin,
                   gboolean changed)
{
    HandleChangePinContext *ctx;

    ctx = g_new0 (HandleChangePinContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    ctx->old_pin = g_strdup (old_pin);
    ctx->new_pin = g_strdup (new_pin);

    mm_base_modem_authorize (self->priv->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_change_pin_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* ENABLE PIN (Generic implementation) */

static gboolean
enable_pin_finish (MMBaseSim *self,
                   GAsyncResult *res,
                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enable_pin_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
enable_pin (MMBaseSim *self,
            const gchar *pin,
            gboolean enabled,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    GTask *task;
    gchar *command;

    task = g_task_new (self, NULL, callback, user_data);

    command = g_strdup_printf ("+CLCK=\"SC\",%d,\"%s\"",
                               enabled ? 1 : 0,
                               pin);
    mm_base_modem_at_command (self->priv->modem,
                              command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)enable_pin_ready,
                              task);
    g_free (command);
}

/*****************************************************************************/
/* ENABLE PIN (DBus call handling) */

typedef struct {
    MMBaseSim *self;
    GDBusMethodInvocation *invocation;
    gchar *pin;
    gboolean enabled;
    GError *save_error;
} HandleEnablePinContext;

static void
handle_enable_pin_context_free (HandleEnablePinContext *ctx)
{
    g_assert (ctx->save_error == NULL);

    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->pin);
    g_free (ctx);
}

static void
after_enable_update_lock_info_ready (MMIfaceModem *modem,
                                     GAsyncResult *res,
                                     HandleEnablePinContext *ctx)
{
    /* We just want to ensure that we tried to update the unlock
     * retries, no big issue if it failed */
    mm_iface_modem_update_lock_info_finish (modem, res, NULL);

    if (ctx->save_error) {
        g_dbus_method_invocation_return_gerror (ctx->invocation, ctx->save_error);
        reprobe_if_puk_discovered (ctx->self, ctx->save_error);
        g_clear_error (&ctx->save_error);
    } else {
        /* Signal about the new lock state */
        g_signal_emit (ctx->self, signals[SIGNAL_PIN_LOCK_ENABLED], 0, ctx->enabled);
        mm_gdbus_sim_complete_enable_pin (MM_GDBUS_SIM (ctx->self), ctx->invocation);
    }

    handle_enable_pin_context_free (ctx);
}

static void
handle_enable_pin_ready (MMBaseSim *self,
                         GAsyncResult *res,
                         HandleEnablePinContext *ctx)
{
    MMModemLock known_lock = MM_MODEM_LOCK_UNKNOWN;

    if (!MM_BASE_SIM_GET_CLASS (self)->enable_pin_finish (self, res, &ctx->save_error)) {
        if (g_error_matches (ctx->save_error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK))
            known_lock = MM_MODEM_LOCK_SIM_PUK;
    }

    mm_iface_modem_update_lock_info (
        MM_IFACE_MODEM (self->priv->modem),
        known_lock,
        (GAsyncReadyCallback)after_enable_update_lock_info_ready,
        ctx);
}

static void
handle_enable_pin_auth_ready (MMBaseModem *modem,
                              GAsyncResult *res,
                              HandleEnablePinContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_enable_pin_context_free (ctx);
        return;
    }

    /* If changing PIN is not implemented, report an error */
    if (!MM_BASE_SIM_GET_CLASS (ctx->self)->enable_pin ||
        !MM_BASE_SIM_GET_CLASS (ctx->self)->enable_pin_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot enable/disable PIN: "
                                               "operation not supported");
        handle_enable_pin_context_free (ctx);
        return;
    }

    if (!mm_gdbus_sim_get_active (MM_GDBUS_SIM (ctx->self))) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot enable/disable PIN: "
                                               "SIM not currently active");
        handle_enable_pin_context_free (ctx);
        return;
    }

    MM_BASE_SIM_GET_CLASS (ctx->self)->enable_pin (ctx->self,
                                                   ctx->pin,
                                                   ctx->enabled,
                                                   (GAsyncReadyCallback)handle_enable_pin_ready,
                                                   ctx);
}

static gboolean
handle_enable_pin (MMBaseSim *self,
                   GDBusMethodInvocation *invocation,
                   const gchar *pin,
                   gboolean enabled)
{
    HandleEnablePinContext *ctx;

    ctx = g_new0 (HandleEnablePinContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    ctx->pin = g_strdup (pin);
    ctx->enabled = enabled;

    mm_base_modem_authorize (self->priv->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_enable_pin_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* SEND PIN/PUK (Generic implementation) */

static gboolean
common_send_pin_puk_finish (MMBaseSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
send_pin_puk_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    GTask *task)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
common_send_pin_puk (MMBaseSim *self,
                     const gchar *pin,
                     const gchar *puk,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GTask *task;
    gchar *command;

    task = g_task_new (self, NULL, callback, user_data);

    command = (puk ?
               g_strdup_printf ("+CPIN=\"%s\",\"%s\"", puk, pin) :
               g_strdup_printf ("+CPIN=\"%s\"", pin));

    mm_base_modem_at_command (self->priv->modem,
                              command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)send_pin_puk_ready,
                              task);
    g_free (command);
}

static void
send_puk (MMBaseSim *self,
          const gchar *puk,
          const gchar *new_pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    common_send_pin_puk (self, new_pin, puk, callback, user_data);
}

static void
send_pin (MMBaseSim *self,
          const gchar *pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    common_send_pin_puk (self, pin, NULL, callback, user_data);
}

/*****************************************************************************/
/* SEND PIN/PUK (common logic) */

static GError *
error_for_unlock_check (MMModemLock lock)
{
    static const MMMobileEquipmentError errors_for_locks [] = {
        MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,            /* MM_MODEM_LOCK_UNKNOWN */
        MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN,            /* MM_MODEM_LOCK_NONE */
        MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN,            /* MM_MODEM_LOCK_SIM_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_SIM_PIN2,           /* MM_MODEM_LOCK_SIM_PIN2 */
        MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK,            /* MM_MODEM_LOCK_SIM_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK2,           /* MM_MODEM_LOCK_SIM_PUK2 */
        MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PIN,        /* MM_MODEM_LOCK_PH_SP_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_SERVICE_PUK,        /* MM_MODEM_LOCK_PH_SP_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PIN,        /* MM_MODEM_LOCK_PH_NET_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_NETWORK_PUK,        /* MM_MODEM_LOCK_PH_NET_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_PH_SIM_PIN,         /* MM_MODEM_LOCK_PH_SIM_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_CORP_PIN,           /* MM_MODEM_LOCK_PH_CORP_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_CORP_PUK,           /* MM_MODEM_LOCK_PH_CORP_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PIN,        /* MM_MODEM_LOCK_PH_FSIM_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_PH_FSIM_PUK,        /* MM_MODEM_LOCK_PH_FSIM_PUK */
        MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PIN, /* MM_MODEM_LOCK_PH_NETSUB_PIN */
        MM_MOBILE_EQUIPMENT_ERROR_NETWORK_SUBSET_PUK, /* MM_MODEM_LOCK_PH_NETSUB_PUK */
    };

    g_assert (lock >= MM_MODEM_LOCK_UNKNOWN);
    g_assert (lock <= MM_MODEM_LOCK_PH_NETSUB_PUK);

    return g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                        errors_for_locks[lock],
                        "Device is locked: '%s'",
                        mm_modem_lock_get_string (lock));
}

gboolean
mm_base_sim_send_pin_finish (MMBaseSim     *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

gboolean
mm_base_sim_send_puk_finish (MMBaseSim     *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
update_lock_info_ready (MMIfaceModem *modem,
                        GAsyncResult *res,
                        GTask        *task)
{
    GError      *error = NULL;
    MMModemLock  lock;

    lock = mm_iface_modem_update_lock_info_finish (modem, res, &error);
    /* Even if we may be SIM-PIN2/PUK2 locked, we don't consider this an error
     * in the PIN/PUK sending */
    if (lock != MM_MODEM_LOCK_NONE &&
        lock != MM_MODEM_LOCK_SIM_PIN2 &&
        lock != MM_MODEM_LOCK_SIM_PUK2) {
        const GError *saved_error;

        /* Device is locked. Now:
         *   - If we got an error during update_lock_info, report it. The sim might have been blocked.
         *   - If we got an error in the original send-pin action, report it.
         *   - Otherwise, build our own error from the lock code.
         */
        if (!error) {
            saved_error = g_task_get_task_data (task);
            if (saved_error)
                error = g_error_copy (saved_error);
            else
                error = error_for_unlock_check (lock);
        }
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

static void
send_pin_ready (MMBaseSim    *self,
                GAsyncResult *res,
                GTask        *task)
{
    GError      *error = NULL;
    MMModemLock  known_lock = MM_MODEM_LOCK_UNKNOWN;

    if (!MM_BASE_SIM_GET_CLASS (self)->send_pin_finish (self, res, &error)) {
        g_task_set_task_data (task, error, (GDestroyNotify)g_error_free);
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK))
            known_lock = MM_MODEM_LOCK_SIM_PUK;
    }

    /* Once pin/puk has been sent, recheck lock */
    mm_iface_modem_update_lock_info (
        MM_IFACE_MODEM (self->priv->modem),
        known_lock,
        (GAsyncReadyCallback)update_lock_info_ready,
        task);
}

static void
send_puk_ready (MMBaseSim    *self,
                GAsyncResult *res,
                GTask        *task)
{
    GError *error = NULL;

    if (!MM_BASE_SIM_GET_CLASS (self)->send_puk_finish (self, res, &error))
        g_task_set_task_data (task, error, (GDestroyNotify)g_error_free);

    /* Once pin/puk has been sent, recheck lock */
    mm_iface_modem_update_lock_info (MM_IFACE_MODEM (self->priv->modem),
                                     MM_MODEM_LOCK_UNKNOWN, /* ask */
                                     (GAsyncReadyCallback)update_lock_info_ready,
                                     task);
}

void
mm_base_sim_send_pin (MMBaseSim           *self,
                      const gchar         *pin,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If sending PIN is not implemented, report an error */
    if (!MM_BASE_SIM_GET_CLASS (self)->send_pin ||
        !MM_BASE_SIM_GET_CLASS (self)->send_pin_finish) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Cannot send PIN: operation not supported");
        g_object_unref (task);
        return;
    }

    /* Only allow sending SIM-PIN if really SIM-PIN locked */
    if (mm_iface_modem_get_unlock_required (MM_IFACE_MODEM (self->priv->modem)) != MM_MODEM_LOCK_SIM_PIN) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "Cannot send PIN: device is not SIM-PIN locked");
        g_object_unref (task);
        return;
    }

    MM_BASE_SIM_GET_CLASS (self)->send_pin (self,
                                            pin,
                                            (GAsyncReadyCallback)send_pin_ready,
                                            task);
}

void
mm_base_sim_send_puk (MMBaseSim           *self,
                      const gchar         *puk,
                      const gchar         *new_pin,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* If sending PIN is not implemented, report an error */
    if (!MM_BASE_SIM_GET_CLASS (self)->send_puk ||
        !MM_BASE_SIM_GET_CLASS (self)->send_puk_finish) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Cannot send PUK: operation not supported");
        g_object_unref (task);
        return;
    }

    /* Only allow sending SIM-PUK if really SIM-PUK locked */
    if (mm_iface_modem_get_unlock_required (MM_IFACE_MODEM (self->priv->modem)) != MM_MODEM_LOCK_SIM_PUK) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "Cannot send PUK: device is not SIM-PUK locked");
        g_object_unref (task);
        return;
    }

    MM_BASE_SIM_GET_CLASS (self)->send_puk (self,
                                            puk,
                                            new_pin,
                                            (GAsyncReadyCallback)send_puk_ready,
                                            task);
}

/*****************************************************************************/
/* LOAD SIM IDENTIFIER */

gchar *
mm_base_sim_load_sim_identifier_finish (MMBaseSim *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
load_sim_identifier_ready (MMBaseSim *self,
                           GAsyncResult *res,
                           GTask *task)
{
    gchar *simid;
    GError *error = NULL;

    simid = MM_BASE_SIM_GET_CLASS (self)->load_sim_identifier_finish (self, res, &error);
    if (!simid)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, simid, g_free);
    g_object_unref (task);
}

void
mm_base_sim_load_sim_identifier (MMBaseSim *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (!MM_BASE_SIM_GET_CLASS (self)->load_sim_identifier ||
        !MM_BASE_SIM_GET_CLASS (self)->load_sim_identifier_finish) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "not implemented");
        g_object_unref (task);
        return;
    }

    MM_BASE_SIM_GET_CLASS (self)->load_sim_identifier (
            self,
            (GAsyncReadyCallback)load_sim_identifier_ready,
            task);
}

/*****************************************************************************/
/* SEND PIN (DBus call handling) */

typedef struct {
    MMBaseSim *self;
    GDBusMethodInvocation *invocation;
    gchar *pin;
} HandleSendPinContext;

static void
handle_send_pin_context_free (HandleSendPinContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->pin);
    g_free (ctx);
}

static void
handle_send_pin_ready (MMBaseSim *self,
                       GAsyncResult *res,
                       HandleSendPinContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_sim_send_pin_finish (self, res, &error)) {
        g_dbus_method_invocation_return_gerror (ctx->invocation, error);
        reprobe_if_puk_discovered (self, error);
        g_clear_error (&error);
    } else
        mm_gdbus_sim_complete_send_pin (MM_GDBUS_SIM (self), ctx->invocation);

    handle_send_pin_context_free (ctx);
}

static void
handle_send_pin_auth_ready (MMBaseModem *modem,
                            GAsyncResult *res,
                            HandleSendPinContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_send_pin_context_free (ctx);
        return;
    }

    if (!mm_gdbus_sim_get_active (MM_GDBUS_SIM (ctx->self))) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot send PIN: "
                                               "SIM not currently active");
        handle_send_pin_context_free (ctx);
        return;
    }

    mm_base_sim_send_pin (ctx->self,
                          ctx->pin,
                          (GAsyncReadyCallback)handle_send_pin_ready,
                          ctx);
}

static gboolean
handle_send_pin (MMBaseSim *self,
                 GDBusMethodInvocation *invocation,
                 const gchar *pin)
{
    HandleSendPinContext *ctx;

    ctx = g_new0 (HandleSendPinContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    ctx->pin = g_strdup (pin);

    mm_base_modem_authorize (self->priv->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_send_pin_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* SEND PUK (DBus call handling) */

typedef struct {
    MMBaseSim *self;
    GDBusMethodInvocation *invocation;
    gchar *puk;
    gchar *new_pin;
} HandleSendPukContext;

static void
handle_send_puk_context_free (HandleSendPukContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->puk);
    g_free (ctx->new_pin);
    g_free (ctx);
}

static void
handle_send_puk_ready (MMBaseSim *self,
                       GAsyncResult *res,
                       HandleSendPukContext *ctx)
{
    GError *error = NULL;
    gboolean sim_error = FALSE;

    if (!mm_base_sim_send_puk_finish (self, res, &error)) {
        sim_error = g_error_matches (error,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) ||
                    g_error_matches (error,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE) ||
                    g_error_matches (error,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG);
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    } else
        mm_gdbus_sim_complete_send_puk (MM_GDBUS_SIM (self), ctx->invocation);

    if (sim_error) {
        mm_obj_info (self, "Received critical sim error. SIM might be permanently blocked. Reprobing...");
        mm_base_modem_process_sim_event (self->priv->modem);
    }

    handle_send_puk_context_free (ctx);
}

static void
handle_send_puk_auth_ready (MMBaseModem *modem,
                            GAsyncResult *res,
                            HandleSendPukContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_send_puk_context_free (ctx);
        return;
    }

    if (!mm_gdbus_sim_get_active (MM_GDBUS_SIM (ctx->self))) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot send PUK: "
                                               "SIM not currently active");
        handle_send_puk_context_free (ctx);
        return;
    }

    mm_base_sim_send_puk (ctx->self,
                          ctx->puk,
                          ctx->new_pin,
                          (GAsyncReadyCallback)handle_send_puk_ready,
                          ctx);
}

static gboolean
handle_send_puk (MMBaseSim *self,
                 GDBusMethodInvocation *invocation,
                 const gchar *puk,
                 const gchar *new_pin)
{
    HandleSendPukContext *ctx;

    ctx = g_new0 (HandleSendPukContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    ctx->puk = g_strdup (puk);
    ctx->new_pin = g_strdup (new_pin);

    mm_base_modem_authorize (self->priv->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_send_puk_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static void
sim_dbus_export (MMBaseSim *self)
{
    GError *error = NULL;

    /* Handle method invocations */
    g_signal_connect (self,
                      "handle-change-pin",
                      G_CALLBACK (handle_change_pin),
                      NULL);
    g_signal_connect (self,
                      "handle-enable-pin",
                      G_CALLBACK (handle_enable_pin),
                      NULL);
    g_signal_connect (self,
                      "handle-send-pin",
                      G_CALLBACK (handle_send_pin),
                      NULL);
    g_signal_connect (self,
                      "handle-send-puk",
                      G_CALLBACK (handle_send_puk),
                      NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_obj_warn (self, "couldn't export SIM: %s", error->message);
        g_error_free (error);
    }
}

static void
sim_dbus_unexport (MMBaseSim *self)
{
    /* Only unexport if currently exported */
    if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
}

/*****************************************************************************/

const gchar *
mm_base_sim_get_path (MMBaseSim *self)
{
    return self->priv->path;
}

guint
mm_base_sim_get_slot_number (MMBaseSim *self)
{
    return self->priv->slot_number;
}

/*****************************************************************************/

gboolean
mm_base_sim_is_emergency_number (MMBaseSim   *self,
                                 const gchar *number)
{
    const gchar *const *emergency_numbers;
    guint               i;

    emergency_numbers = mm_gdbus_sim_get_emergency_numbers (MM_GDBUS_SIM (self));

    if (!emergency_numbers)
        return FALSE;

    for (i = 0; emergency_numbers[i]; i++) {
        if (g_strcmp0 (number, emergency_numbers[i]) == 0)
            return TRUE;
    }

    return FALSE;
}

/*****************************************************************************/

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME)                                        \
    static void                                                         \
    NAME##_command_ready (MMBaseModem *modem,                           \
                          GAsyncResult *res,                            \
                          GTask *task)                                  \
    {                                                                   \
        GError *error = NULL;                                           \
        const gchar *response;                                          \
                                                                        \
        response = mm_base_modem_at_command_finish (modem, res, &error); \
        if (error)                                                      \
            g_task_return_error (task, error);                          \
        else                                                            \
            g_task_return_pointer (task, g_strdup (response), g_free);  \
                                                                        \
        g_object_unref (task);                                          \
    }

/*****************************************************************************/
/* Emergency numbers */

static GStrv
parse_emergency_numbers (const gchar  *response,
                         GError      **error)
{
    guint  sw1 = 0;
    guint  sw2 = 0;
    gchar *hex = 0;
    GStrv  ret;

    if (!mm_3gpp_parse_crsm_response (response, &sw1, &sw2, &hex, error))
        return NULL;

    if ((sw1 == 0x90 && sw2 == 0x00) ||
        (sw1 == 0x91) ||
        (sw1 == 0x92) ||
        (sw1 == 0x9f)) {
        ret = mm_3gpp_parse_emergency_numbers (hex, error);
        g_free (hex);
        return ret;
    }

    g_free (hex);
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "SIM failed to handle CRSM request (sw1 %d sw2 %d)",
                 sw1, sw2);
    return NULL;
}

static GStrv
load_emergency_numbers_finish (MMBaseSim     *self,
                               GAsyncResult  *res,
                               GError       **error)
{
    gchar *result;
    GStrv  emergency_numbers;
    guint  i;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return NULL;

    emergency_numbers = parse_emergency_numbers (result, error);
    g_free (result);

    if (!emergency_numbers)
        return NULL;

    for (i = 0; emergency_numbers[i]; i++)
        mm_obj_dbg (self, "loaded emergency number: %s", emergency_numbers[i]);

    return emergency_numbers;
}

STR_REPLY_READY_FN (load_emergency_numbers)

static void
load_emergency_numbers (MMBaseSim           *self,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    mm_obj_dbg (self, "loading emergency numbers...");

    /* READ BINARY of EF_ECC (Emergency Call Codes) ETSI TS 51.011 section 10.3.27 */
    mm_base_modem_at_command (
        self->priv->modem,
        "+CRSM=176,28599,0,0,15",
        20,
        FALSE,
        (GAsyncReadyCallback)load_emergency_numbers_command_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* ICCID */

static gchar *
parse_iccid (const gchar *response,
             GError **error)
{
    guint sw1 = 0;
    guint sw2 = 0;
    gchar *hex = 0;
    gchar *ret;

    if (!mm_3gpp_parse_crsm_response (response,
                                      &sw1,
                                      &sw2,
                                      &hex,
                                      error))
        return NULL;

    if ((sw1 == 0x90 && sw2 == 0x00) ||
        (sw1 == 0x91) ||
        (sw1 == 0x92) ||
        (sw1 == 0x9f)) {
        ret = mm_3gpp_parse_iccid (hex, error);
        g_free (hex);
        return ret;
    } else {
        g_free (hex);
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "SIM failed to handle CRSM request (sw1 %d sw2 %d)",
                     sw1, sw2);
        return NULL;
    }
}

static gchar *
load_sim_identifier_finish (MMBaseSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    gchar *result;
    gchar *sim_identifier;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return NULL;

    sim_identifier = parse_iccid (result, error);
    g_free (result);
    if (!sim_identifier)
        return NULL;

    mm_obj_dbg (self, "loaded SIM identifier: %s", sim_identifier);
    return sim_identifier;
}

STR_REPLY_READY_FN (load_sim_identifier)

static void
load_sim_identifier (MMBaseSim *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_obj_dbg (self, "loading SIM identifier...");

    /* READ BINARY of EFiccid (ICC Identification) ETSI TS 102.221 section 13.2 */
    mm_base_modem_at_command (
        self->priv->modem,
        "+CRSM=176,12258,0,0,10",
        20,
        FALSE,
        (GAsyncReadyCallback)load_sim_identifier_command_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* IMSI */

static gchar *
parse_imsi (const gchar *response,
            GError **error)
{
    const gchar *s;
    gint len;

    g_assert (response != NULL);

    for (s = mm_strip_tag (response, "+CIMI"), len = 0;
         *s;
         ++s, ++len) {
        /* IMSI is a number with 15 or less decimal digits. */
        if (!isdigit (*s) || len > 15) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Invalid +CIMI response '%s'", response ? response : "<null>");
            return NULL;
        }
    }

    return g_strdup (response);
}

static gchar *
load_imsi_finish (MMBaseSim *self,
                  GAsyncResult *res,
                  GError **error)
{
    gchar *result;
    gchar *imsi;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return NULL;

    imsi = parse_imsi (result, error);
    g_free (result);
    if (!imsi)
        return NULL;

    mm_obj_dbg (self, "loaded IMSI: %s", imsi);
    return imsi;
}

STR_REPLY_READY_FN (load_imsi)

static void
load_imsi (MMBaseSim *self,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    mm_obj_dbg (self, "loading IMSI...");

    mm_base_modem_at_command (
        self->priv->modem,
        "+CIMI",
        3,
        FALSE,
        (GAsyncReadyCallback)load_imsi_command_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Operator ID */

static guint
parse_mnc_length (const gchar *response,
                  GError **error)
{
    guint             sw1 = 0;
    guint             sw2 = 0;
    g_autofree gchar *hex = NULL;

    if (!mm_3gpp_parse_crsm_response (response,
                                      &sw1,
                                      &sw2,
                                      &hex,
                                      error))
        return 0;

    if ((sw1 == 0x90 && sw2 == 0x00) ||
        (sw1 == 0x91) ||
        (sw1 == 0x92) ||
        (sw1 == 0x9f)) {
        gsize              buflen = 0;
        guint32            mnc_len;
        g_autofree guint8 *bin = NULL;

        /* Convert hex string to binary */
        bin = mm_utils_hexstr2bin (hex, -1, &buflen, error);
        if (!bin) {
            g_prefix_error (error, "SIM returned malformed response '%s': ", hex);
            return 0;
        }
        if (buflen < 4) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "SIM returned malformed response '%s': too short", hex);
            return 0;
        }

        /* MNC length is byte 4 of this SIM file */
        mnc_len = bin[3];
        if (mnc_len == 2 || mnc_len == 3)
            return mnc_len;

        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "SIM returned invalid MNC length %d (should be either 2 or 3)", mnc_len);
        return 0;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "SIM failed to handle CRSM request (sw1 %d sw2 %d)", sw1, sw2);
    return 0;
}

static gchar *
load_operator_identifier_finish (MMBaseSim *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    GError *inner_error = NULL;
    const gchar *imsi;
    gchar *result;
    guint mnc_length;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return NULL;

    mnc_length = parse_mnc_length (result, &inner_error);
    g_free (result);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return NULL;
    }

    imsi = mm_gdbus_sim_get_imsi (MM_GDBUS_SIM (self));
    if (!imsi) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Cannot load Operator ID without IMSI");
        return NULL;
    }

    /* Build Operator ID */
    return g_strndup (imsi, 3 + mnc_length);
}

STR_REPLY_READY_FN (load_operator_identifier)

static void
load_operator_identifier (MMBaseSim *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    mm_obj_dbg (self, "loading operator ID...");

    /* READ BINARY of EFad (Administrative Data) ETSI 51.011 section 10.3.18 */
    mm_base_modem_at_command (
        self->priv->modem,
        "+CRSM=176,28589,0,0,4",
        10,
        FALSE,
        (GAsyncReadyCallback)load_operator_identifier_command_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/
/* Operator Name (Service Provider Name) */

static gchar *
parse_spn (const gchar *response,
           GError **error)
{
    guint             sw1 = 0;
    guint             sw2 = 0;
    g_autofree gchar *hex = NULL;

    if (!mm_3gpp_parse_crsm_response (response,
                                      &sw1,
                                      &sw2,
                                      &hex,
                                      error))
        return NULL;

    if ((sw1 == 0x90 && sw2 == 0x00) ||
        (sw1 == 0x91) ||
        (sw1 == 0x92) ||
        (sw1 == 0x9f)) {
        g_autoptr(GByteArray)  bin_array = NULL;
        g_autofree guint8     *bin = NULL;
        gsize                  binlen = 0;

        /* Convert hex string to binary */
        bin = mm_utils_hexstr2bin (hex, -1, &binlen, error);
        if (!bin) {
            g_prefix_error (error, "SIM returned malformed response '%s': ", hex);
            return NULL;
        }

        /* Remove the FF filler at the end */
        while (binlen > 1 && bin[binlen - 1] == 0xff)
            binlen--;
        if (binlen <= 1) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "SIM returned empty response '%s'", hex);
            return NULL;
        }
        /* Setup as bytearray.
         * First byte is metadata; remainder is GSM-7 unpacked into octets; convert to UTF8 */
        bin_array = g_byte_array_sized_new (binlen - 1);
        g_byte_array_append (bin_array, bin + 1, binlen - 1);

        return mm_modem_charset_bytearray_to_utf8 (bin_array, MM_MODEM_CHARSET_GSM, FALSE, error);
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "SIM failed to handle CRSM request (sw1 %d sw2 %d)", sw1, sw2);
    return NULL;
}

static gchar *
load_operator_name_finish (MMBaseSim *self,
                           GAsyncResult *res,
                           GError **error)
{
    gchar *result;
    gchar *spn;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return NULL;

    spn = parse_spn (result, error);
    g_free (result);
    return spn;
}

STR_REPLY_READY_FN (load_operator_name)

static void
load_operator_name (MMBaseSim *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    mm_obj_dbg (self, "loading operator name...");

    /* READ BINARY of EFspn (Service Provider Name) ETSI 51.011 section 10.3.11 */
    mm_base_modem_at_command (
        self->priv->modem,
        "+CRSM=176,28486,0,0,17",
        10,
        FALSE,
        (GAsyncReadyCallback)load_operator_name_command_ready,
        g_task_new (self, NULL, callback, user_data));
}

/*****************************************************************************/

MMBaseSim *
mm_base_sim_new_initialized (MMBaseModem *modem,
                             guint        slot_number,
                             gboolean     active,
                             const gchar *sim_identifier,
                             const gchar *imsi,
                             const gchar *eid,
                             const gchar *operator_identifier,
                             const gchar *operator_name,
                             const GStrv  emergency_numbers)
{
    MMBaseSim *sim;

    sim = MM_BASE_SIM (g_object_new (MM_TYPE_BASE_SIM,
                                     MM_BASE_SIM_MODEM,       modem,
                                     MM_BASE_SIM_SLOT_NUMBER, slot_number,
                                     "active",                active,
                                     "sim-identifier",        sim_identifier,
                                     "imsi",                  imsi,
                                     "eid",                   eid,
                                     "operator-identifier",   operator_identifier,
                                     "operator-name",         operator_name,
                                     "emergency-numbers",     emergency_numbers,
                                     NULL));

    mm_base_sim_export (sim);
    return sim;
}

/*****************************************************************************/

typedef struct _InitAsyncContext InitAsyncContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_WAIT_READY,
    INITIALIZATION_STEP_SIM_IDENTIFIER,
    INITIALIZATION_STEP_IMSI,
    INITIALIZATION_STEP_EID,
    INITIALIZATION_STEP_OPERATOR_ID,
    INITIALIZATION_STEP_OPERATOR_NAME,
    INITIALIZATION_STEP_EMERGENCY_NUMBERS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitAsyncContext {
    InitializationStep step;
    guint sim_identifier_tries;
};

MMBaseSim *
mm_base_sim_new_finish (GAsyncResult  *res,
                        GError       **error)
{
    GObject *source;
    GObject *sim;

    source = g_async_result_get_source_object (res);
    sim = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!sim)
        return NULL;

    /* Only export valid SIMs */
    mm_base_sim_export (MM_BASE_SIM (sim));

    return MM_BASE_SIM (sim);
}

static gboolean
initable_init_finish (GAsyncInitable  *initable,
                      GAsyncResult    *result,
                      GError         **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void
init_load_sim_identifier_ready (MMBaseSim *self,
                                GAsyncResult *res,
                                GTask *task)
{
    InitAsyncContext *ctx;
    GError *error = NULL;
    gchar *simid;

    ctx = g_task_get_task_data (task);
    simid  = MM_BASE_SIM_GET_CLASS (self)->load_sim_identifier_finish (self, res, &error);
    if (!simid) {
        /* TODO: make the retries gobi-specific? */

        /* Try one more time... Gobi 1K cards may reply to the first
         * request with '+CRSM: 106,134,""' which is bogus because
         * subsequent requests work fine.
         */
        if (++ctx->sim_identifier_tries < 2) {
            g_clear_error (&error);
            interface_initialization_step (task);
            return;
        }

        mm_obj_warn (self, "couldn't load SIM identifier: %s", error ? error->message : "unknown error");
        g_clear_error (&error);
    }

    mm_gdbus_sim_set_sim_identifier (MM_GDBUS_SIM (self), simid);
    g_free (simid);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
init_load_emergency_numbers_ready (MMBaseSim    *self,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    InitAsyncContext *ctx;
    GError           *error = NULL;
    GStrv             str_list;

    str_list = MM_BASE_SIM_GET_CLASS (self)->load_emergency_numbers_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load list of emergency numbers: %s", error->message);
        g_error_free (error);
    }

    if (str_list) {
        mm_gdbus_sim_set_emergency_numbers (MM_GDBUS_SIM (self), (const gchar *const *) str_list);
        g_strfreev (str_list);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_initialization_step (task);
}

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY)                                \
    static void                                                         \
    init_load_##NAME##_ready (MMBaseSim *self,                          \
                              GAsyncResult *res,                        \
                              GTask *task)                              \
    {                                                                   \
        InitAsyncContext *ctx;                                          \
        GError *error = NULL;                                           \
        gchar *val;                                                     \
                                                                        \
        val = MM_BASE_SIM_GET_CLASS (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_sim_set_##NAME (MM_GDBUS_SIM (self), val);             \
        g_free (val);                                                   \
                                                                        \
        if (error) {                                                    \
            mm_obj_warn (self, "couldn't load %s: %s", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx = g_task_get_task_data (task);                              \
        ctx->step++;                                                    \
        interface_initialization_step (task);                           \
    }

STR_REPLY_READY_FN (imsi, "IMSI")
STR_REPLY_READY_FN (eid, "EID")
STR_REPLY_READY_FN (operator_identifier, "operator identifier")
STR_REPLY_READY_FN (operator_name, "operator name")

static void
init_wait_sim_ready (MMBaseSim    *self,
                     GAsyncResult *res,
                     GTask        *task)
{
    InitAsyncContext  *ctx;
    g_autoptr(GError)  error = NULL;

    if (!MM_BASE_SIM_GET_CLASS (self)->wait_sim_ready_finish (self, res, &error))
        mm_obj_warn (self, "couldn't wait for SIM to be ready: %s", error->message);

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMBaseSim *self;
    InitAsyncContext *ctx;

    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_WAIT_READY:
        if (MM_BASE_SIM_GET_CLASS (self)->wait_sim_ready &&
            MM_BASE_SIM_GET_CLASS (self)->wait_sim_ready_finish) {
            MM_BASE_SIM_GET_CLASS (self)->wait_sim_ready (
                self,
                (GAsyncReadyCallback)init_wait_sim_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_SIM_IDENTIFIER:
        /* SIM ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_sim_identifier (MM_GDBUS_SIM (self)) == NULL &&
            MM_BASE_SIM_GET_CLASS (self)->load_sim_identifier &&
            MM_BASE_SIM_GET_CLASS (self)->load_sim_identifier_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_sim_identifier (
                self,
                (GAsyncReadyCallback)init_load_sim_identifier_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_IMSI:
        /* IMSI is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_imsi (MM_GDBUS_SIM (self)) == NULL &&
            MM_BASE_SIM_GET_CLASS (self)->load_imsi &&
            MM_BASE_SIM_GET_CLASS (self)->load_imsi_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_imsi (
                self,
                (GAsyncReadyCallback)init_load_imsi_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_EID:
        /* EID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_eid (MM_GDBUS_SIM (self)) == NULL &&
            MM_BASE_SIM_GET_CLASS (self)->load_eid &&
            MM_BASE_SIM_GET_CLASS (self)->load_eid_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_eid (
                self,
                (GAsyncReadyCallback)init_load_eid_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_OPERATOR_ID:
        /* Operator ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_operator_identifier (MM_GDBUS_SIM (self)) == NULL &&
            MM_BASE_SIM_GET_CLASS (self)->load_operator_identifier &&
            MM_BASE_SIM_GET_CLASS (self)->load_operator_identifier_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_operator_identifier (
                self,
                (GAsyncReadyCallback)init_load_operator_identifier_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_OPERATOR_NAME:
        /* Operator Name is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_operator_name (MM_GDBUS_SIM (self)) == NULL &&
            MM_BASE_SIM_GET_CLASS (self)->load_operator_name &&
            MM_BASE_SIM_GET_CLASS (self)->load_operator_name_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_operator_name (
                self,
                (GAsyncReadyCallback)init_load_operator_name_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_EMERGENCY_NUMBERS:
        /* Emergency Numbers are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_sim_get_emergency_numbers (MM_GDBUS_SIM (self)) == NULL &&
            MM_BASE_SIM_GET_CLASS (self)->load_emergency_numbers &&
            MM_BASE_SIM_GET_CLASS (self)->load_emergency_numbers_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_emergency_numbers (
                self,
                (GAsyncReadyCallback)init_load_emergency_numbers_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

static void
common_init_async (GAsyncInitable *initable,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)

{
    MMBaseSim *self;
    InitAsyncContext *ctx;
    GTask *task;

    self = MM_BASE_SIM (initable);

    ctx = g_new (InitAsyncContext, 1);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->sim_identifier_tries = 0;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, g_free);

    interface_initialization_step (task);
}

static void
initable_init_async (GAsyncInitable *initable,
                     int io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    mm_gdbus_sim_set_sim_identifier (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_imsi (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_eid (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_operator_identifier (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_operator_name (MM_GDBUS_SIM (initable), NULL);

    common_init_async (initable, cancellable, callback, user_data);
}

void
mm_base_sim_new (MMBaseModem *modem,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_async_initable_new_async (MM_TYPE_BASE_SIM,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                "active", TRUE, /* by default always active */
                                NULL);
}

gboolean
mm_base_sim_initialize_finish (MMBaseSim *self,
                               GAsyncResult *result,
                               GError **error)
{
    return initable_init_finish (G_ASYNC_INITABLE (self), result, error);
}

void
mm_base_sim_initialize (MMBaseSim *self,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    common_init_async (G_ASYNC_INITABLE (self),
                       cancellable,
                       callback,
                       user_data);
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMBaseSim *self;

    self = MM_BASE_SIM (_self);
    return g_strdup_printf ("sim%u", self->priv->dbus_id);
}

/*****************************************************************************/

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBaseSim *self = MM_BASE_SIM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);

        /* Export when we get a DBus connection AND we have a path */
        if (self->priv->path &&
            self->priv->connection)
            sim_dbus_export (self);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->connection)
            sim_dbus_unexport (self);
        else if (self->priv->path)
            sim_dbus_export (self);
        break;
    case PROP_MODEM:
        g_clear_object (&self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
        if (self->priv->modem) {
            /* Set owner ID */
            mm_log_object_set_owner_id (MM_LOG_OBJECT (self), mm_log_object_get_id (MM_LOG_OBJECT (self->priv->modem)));
            /* Bind the modem's connection (which is set when it is exported,
             * and unset when unexported) to the SIM's connection */
            g_object_bind_property (self->priv->modem, MM_BASE_MODEM_CONNECTION,
                                    self, MM_BASE_SIM_CONNECTION,
                                    G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        }
        break;
    case PROP_SLOT_NUMBER:
        self->priv->slot_number = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBaseSim *self = MM_BASE_SIM (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    case PROP_SLOT_NUMBER:
        g_value_set_uint (value, self->priv->slot_number);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_base_sim_init (MMBaseSim *self)
{
    static guint id = 0;

    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BASE_SIM, MMBaseSimPrivate);

    /* Each SIM is given a unique id to build its own DBus path */
    self->priv->dbus_id = id++;
}

static void
finalize (GObject *object)
{
    MMBaseSim *self = MM_BASE_SIM (object);

    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_base_sim_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseSim *self = MM_BASE_SIM (object);

    if (self->priv->connection) {
        /* If we arrived here with a valid connection, make sure we unexport
         * the object */
        sim_dbus_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_base_sim_parent_class)->dispose (object);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
    iface->init_async = initable_init_async;
    iface->init_finish = initable_init_finish;
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_base_sim_class_init (MMBaseSimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBaseSimPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    klass->load_sim_identifier = load_sim_identifier;
    klass->load_sim_identifier_finish = load_sim_identifier_finish;
    klass->load_imsi = load_imsi;
    klass->load_imsi_finish = load_imsi_finish;
    klass->load_operator_identifier = load_operator_identifier;
    klass->load_operator_identifier_finish = load_operator_identifier_finish;
    klass->load_operator_name = load_operator_name;
    klass->load_operator_name_finish = load_operator_name_finish;
    klass->load_emergency_numbers = load_emergency_numbers;
    klass->load_emergency_numbers_finish = load_emergency_numbers_finish;
    klass->send_pin = send_pin;
    klass->send_pin_finish = common_send_pin_puk_finish;
    klass->send_puk = send_puk;
    klass->send_puk_finish = common_send_pin_puk_finish;
    klass->enable_pin = enable_pin;
    klass->enable_pin_finish = enable_pin_finish;
    klass->change_pin = change_pin;
    klass->change_pin_finish = change_pin_finish;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BASE_SIM_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_BASE_SIM_PATH,
                             "Path",
                             "DBus path of the SIM",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_BASE_SIM_MODEM,
                             "Modem",
                             "The Modem which owns this SIM",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    properties[PROP_SLOT_NUMBER] =
        g_param_spec_uint (MM_BASE_SIM_SLOT_NUMBER,
                           "Slot number",
                           "The slot number where the SIM is inserted",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_SLOT_NUMBER, properties[PROP_SLOT_NUMBER]);

    /* Signals */
    signals[SIGNAL_PIN_LOCK_ENABLED] =
        g_signal_new (MM_BASE_SIM_PIN_LOCK_ENABLED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMBaseSimClass, pin_lock_enabled),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}
