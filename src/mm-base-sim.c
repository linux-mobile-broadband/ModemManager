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
 * Copyright (C) 2011 - 2022 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2011 - 2022 Google, Inc.
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
/* SIM type helpers */

#define IS_PSIM(self)                                                   \
    (mm_gdbus_sim_get_sim_type (MM_GDBUS_SIM (self)) == MM_SIM_TYPE_PHYSICAL)

#define IS_ESIM(self)                                                   \
    (mm_gdbus_sim_get_sim_type (MM_GDBUS_SIM (self)) == MM_SIM_TYPE_ESIM)

#define IS_ESIM_WITHOUT_PROFILES(self)                                  \
    (IS_ESIM (self) && (mm_gdbus_sim_get_esim_status (MM_GDBUS_SIM (self)) == MM_SIM_ESIM_STATUS_NO_PROFILES))

gboolean
mm_base_sim_is_esim_without_profiles (MMBaseSim *self)
{
    return IS_ESIM_WITHOUT_PROFILES (self);
}

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
        mm_iface_modem_process_sim_event (MM_IFACE_MODEM (self->priv->modem));
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

    if (IS_ESIM_WITHOUT_PROFILES (ctx->self)) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot change PIN: "
                                               "eSIM without profiles");
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

    if (IS_ESIM_WITHOUT_PROFILES (ctx->self)) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot enable/disable PIN: "
                                               "eSIM without profiles");
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
    /* Consider it only an error if SIM-PIN/PUK is locked or lock is unknown */
    if (lock == MM_MODEM_LOCK_UNKNOWN ||
        lock == MM_MODEM_LOCK_SIM_PIN ||
        lock == MM_MODEM_LOCK_SIM_PUK) {
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

    if (IS_ESIM_WITHOUT_PROFILES (self)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "no SIM identifier in eSIM without profiles");
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

    if (IS_ESIM_WITHOUT_PROFILES (ctx->self)) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot send PIN: "
                                               "eSIM without profiles");
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
        mm_obj_msg (self, "received critical sim error: SIM might be permanently blocked, reprobing...");
        mm_iface_modem_process_sim_event (MM_IFACE_MODEM (self->priv->modem));
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

    if (IS_ESIM_WITHOUT_PROFILES (ctx->self)) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot send PUK: "
                                               "eSIM without profiles");
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
/* Check if preferred networks is supported.
 *
 * Modems like the Intel-based EM7345 fail very badly when CPOL? is run, even
 * completely blocking the AT port after that. We need to avoid running any
 * CPOL related command in these modules.
 */

static gboolean
check_preferred_networks_disabled (MMBaseSim *self)
{
    MMPort *primary;

    primary = MM_PORT (mm_base_modem_peek_port_primary (self->priv->modem));
    return (primary ?
            mm_kernel_device_get_global_property_as_boolean (mm_port_peek_kernel_device (primary),
                                                             "ID_MM_PREFERRED_NETWORKS_CPOL_DISABLED") :
            FALSE);
}

/*****************************************************************************/
/* SET PREFERRED NETWORKS (Generic implementation) */

/* Setting preferred network list with AT+CPOL is a complicated procedure with
 * the following steps:
 * 1. Using AT+CPOL=? to get SIM capacity; the capacity is checked to ensure
 *    that the list is not too large for the SIM card.
 * 2. Reading existing preferred networks from SIM with AT+CPOL?.
 * 3. Clearing existing networks with a series of AT+CPOL=<index> commands.
 * 4. Setting the new list by invoking AT+CPOL for each network.
 *
 * There are some complications with AT+CPOL handling which makes the work more
 * difficult for us. It seems that modems can only handle a certain exact number
 * of access technology identifiers - and this cannot be certainly known in
 * advance.
 *
 * If AT+CPOL? in step 2 returns anything, we can deduce the number of supported
 * identifiers there. But if there were no networks configured earlier, we must
 * start with a default based on modem capacity and work our way down from there
 * if the AT+CPOL command fails.
 */

static void set_preferred_networks_set_next   (MMBaseSim  *self,
                                               GTask      *task);
static void set_preferred_networks_clear_next (MMBaseSim  *self,
                                               GTask      *task);

typedef struct {
    GList    *set_list;
    /* AT+CPOL indices that must be cleared before setting the networks. */
    GArray   *clear_index;
    /* Number of access technology identifiers we will set. */
    guint     act_count;
    /* If TRUE, we know that act_count is something the modem can handle */
    gboolean  act_count_verified;
    /* Index of preferred network currently being set (0 = first) */
    guint     current_write_index;
    /* Operation error code */
    GError   *error;
} SetPreferredNetworksContext;

static void
set_preferred_network_context_free (SetPreferredNetworksContext *ctx)
{
    g_list_free_full (ctx->set_list, (GDestroyNotify) mm_sim_preferred_network_free);
    g_clear_error (&ctx->error);
    g_array_free (ctx->clear_index, TRUE);
    g_slice_free (SetPreferredNetworksContext, ctx);
}

static gboolean
set_preferred_networks_finish (MMBaseSim *self,
                               GAsyncResult *res,
                               GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
parse_old_preferred_networks (const gchar *response,
                              SetPreferredNetworksContext *ctx)
{
    gchar **entries;
    gchar **iter;

    entries = g_strsplit_set (response, "\r\n", -1);
    for (iter = entries; iter && *iter; iter++) {
        guint index;
        guint act_count = 0;

        g_strstrip (*iter);
        if (strlen (*iter) == 0)
            continue;

        if (mm_sim_parse_cpol_query_response (*iter,
                                              &index,
                                              NULL, NULL, NULL, NULL, NULL, NULL,
                                              &act_count,
                                              NULL) && index > 0) {
            /* Remember how many access technologies the modem/SIM can take */
            if (!ctx->act_count_verified || act_count > ctx->act_count) {
                ctx->act_count = act_count;
                ctx->act_count_verified = TRUE;
            }

            /* Store the index to be cleared */
            g_array_append_val (ctx->clear_index, index);
        }
    }
    g_strfreev (entries);
}

/* This function is called only in error case, after reloading the network list from SIM. */
static void
set_preferred_networks_reload_ready (MMBaseSim    *self,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    g_autoptr(GError)            error = NULL;
    GList                       *preferred_nets_list;
    SetPreferredNetworksContext *ctx;

    ctx = g_task_get_task_data (task);

    preferred_nets_list = MM_BASE_SIM_GET_CLASS (self)->load_preferred_networks_finish (self, res, &error);
    if (error)
        mm_obj_dbg (self, "couldn't load list of preferred networks: %s", error->message);

    mm_gdbus_sim_set_preferred_networks (MM_GDBUS_SIM (self),
                                         mm_sim_preferred_network_list_get_variant (preferred_nets_list));
    g_list_free_full (preferred_nets_list, (GDestroyNotify) mm_sim_preferred_network_free);

    /* Return the original error stored in our context */
    g_task_return_error (task, g_steal_pointer (&ctx->error));
    g_object_unref (task);
}


static gboolean
set_preferred_networks_retry_command (MMBaseSim                   *self,
                                      SetPreferredNetworksContext *ctx)
{
    /* If we haven't yet determined the number of access technology parameters supported by
     * the modem, try reducing the count if possible and retry with the reduced count.
     */
    if (!ctx->act_count_verified && ctx->act_count > 0) {
        ctx->act_count--;
        mm_obj_dbg (self, "retrying operation with %u access technologies", ctx->act_count);
        return TRUE;
    }
    return FALSE;
}

static void
set_preferred_network_reload_and_return_error (MMBaseSim *self,
                                               GTask     *task,
                                               GError    *error)
{
    SetPreferredNetworksContext *ctx;

    ctx = g_task_get_task_data (task);

    /* Reload the complete list from SIM card to ensure that the PreferredNetworks
     * property matches with whatever is actually on the SIM.
     */
    if (MM_BASE_SIM_GET_CLASS (self)->load_preferred_networks &&
        MM_BASE_SIM_GET_CLASS (self)->load_preferred_networks_finish) {
        ctx->error = error;
        MM_BASE_SIM_GET_CLASS (self)->load_preferred_networks (
            self,
            (GAsyncReadyCallback)set_preferred_networks_reload_ready,
            task);
    } else {
        g_task_return_error (task, error);
        g_object_unref (task);
    }
}

static void
set_preferred_networks_set_ready (MMBaseModem *modem,
                                  GAsyncResult *res,
                                  GTask *task)
{
    MMBaseSim                   *self;
    SetPreferredNetworksContext *ctx;
    GError                      *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_base_modem_at_command_finish (modem, res, &error);
    /* The command may fail; check if we can retry */
    if (error) {
        if (!set_preferred_networks_retry_command (self, ctx)) {
            /* Retrying not possible, failing... */
            mm_obj_warn (self, "failed to set preferred networks: '%s'", error->message);
            set_preferred_network_reload_and_return_error (self, task, error);
            return;
        }
        /* Retrying possible */
        g_clear_error (&error);
    } else {
        /* Last set operation was successful, so we know for sure how many access technologies
         * the modem can take.
         */
        ctx->act_count_verified = TRUE;
        ctx->current_write_index++;
    }

    set_preferred_networks_set_next (self, task);
}

static gboolean
set_preferred_networks_check_support (MMBaseSim                    *self,
                                      SetPreferredNetworksContext  *ctx,
                                      MMSimPreferredNetwork        *network,
                                      GError                      **error)
{
    MMModemAccessTechnology requested_act;
    MMModemAccessTechnology supported_act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    requested_act = mm_sim_preferred_network_get_access_technology (network);
    if (ctx->act_count >= 1)
        supported_act |= MM_MODEM_ACCESS_TECHNOLOGY_GSM;
    if (ctx->act_count >= 2)
        supported_act |= MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT;
    if (ctx->act_count >= 3)
        supported_act |= MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
    if (ctx->act_count >= 4)
        supported_act |= MM_MODEM_ACCESS_TECHNOLOGY_LTE;
    if (ctx->act_count >= 5)
        supported_act |= MM_MODEM_ACCESS_TECHNOLOGY_5GNR;

    if (requested_act & ~supported_act) {
        g_autofree gchar *act_string = NULL;

        act_string = mm_modem_access_technology_build_string_from_mask (requested_act & ~supported_act);
        mm_obj_warn (self, "cannot set preferred net '%s'; access technology '%s' not supported by modem/SIM",
                     mm_sim_preferred_network_get_operator_code (network), act_string);

        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Access technology unsupported by modem or SIM");
        return FALSE;
    }
    return TRUE;
}

/* Set next preferred network in queue */
static void
set_preferred_networks_set_next (MMBaseSim  *self,
                                 GTask      *task)
{
    SetPreferredNetworksContext *ctx;
    g_autofree gchar            *command = NULL;
    MMSimPreferredNetwork       *current_network;
    const gchar                 *operator_code;
    MMModemAccessTechnology      act;
    GError                      *error = NULL;

    ctx = g_task_get_task_data (task);

    current_network = (MMSimPreferredNetwork *) g_list_nth_data (ctx->set_list, ctx->current_write_index);
    if (current_network == NULL) {
        /* No more networks to set; we are done. */
        mm_obj_dbg (self, "setting preferred networks completed.");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    if (!set_preferred_networks_check_support (self, ctx, current_network, &error)) {
        set_preferred_network_reload_and_return_error (self, task, error);
        return;
    }

    operator_code = mm_sim_preferred_network_get_operator_code (current_network);
    act = mm_sim_preferred_network_get_access_technology (current_network);

    /* Assemble the command to set the network */
    command = g_strdup_printf ("+CPOL=%u,2,\"%s\"%s%s%s%s%s", ctx->current_write_index + 1, operator_code,
                               ctx->act_count == 0 ? "" : ((act & MM_MODEM_ACCESS_TECHNOLOGY_GSM) ? ",1" : ",0"),
                               ctx->act_count <= 1 ? "" : ((act & MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT) ? ",1" : ",0"),
                               ctx->act_count <= 2 ? "" : ((act & MM_MODEM_ACCESS_TECHNOLOGY_UMTS) ? ",1" : ",0"),
                               ctx->act_count <= 3 ? "" : ((act & MM_MODEM_ACCESS_TECHNOLOGY_LTE) ? ",1" : ",0"),
                               ctx->act_count <= 4 ? "" : ((act & MM_MODEM_ACCESS_TECHNOLOGY_5GNR) ? ",1" : ",0"));
    mm_base_modem_at_command (
        self->priv->modem,
        command,
        20,
        FALSE,
        (GAsyncReadyCallback)set_preferred_networks_set_ready,
        task);
}

static void
set_preferred_networks_clear_ready (MMBaseModem *modem,
                                    GAsyncResult *res,
                                    GTask *task)
{
    MMBaseSim *self;
    GError *error = NULL;

    self = g_task_get_source_object (task);

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't clear preferred network entry: '%s'", error->message);
        set_preferred_network_reload_and_return_error (self, task, error);
        return;
    }

    set_preferred_networks_clear_next (self, task);
}

/* Clear one of the networks in the clear list */
static void
set_preferred_networks_clear_next (MMBaseSim  *self,
                                   GTask      *task)
{
    SetPreferredNetworksContext *ctx;
    g_autofree gchar            *command = NULL;
    guint                        current_clear_index;

    ctx = g_task_get_task_data (task);

    /* Clear from last index to first, since some modems (e.g. u-blox) may shift up items
     * following the cleared ones.
     */
    if (ctx->clear_index->len > 0) {
        current_clear_index = g_array_index (ctx->clear_index, guint, ctx->clear_index->len - 1);
        g_array_remove_index (ctx->clear_index, ctx->clear_index->len - 1);
        command = g_strdup_printf ("+CPOL=%u", current_clear_index);
        mm_base_modem_at_command (
            self->priv->modem,
            command,
            20,
            FALSE,
            (GAsyncReadyCallback)set_preferred_networks_clear_ready,
            task);
        return;
    }
    /* All clear operations done; start setting new networks */
    set_preferred_networks_set_next (self, task);
}

static void
set_preferred_networks_load_existing_ready (MMBaseModem *modem,
                                            GAsyncResult *res,
                                            GTask *task)
{
    MMBaseSim *self;
    GError *error = NULL;
    SetPreferredNetworksContext *ctx;
    const gchar *response;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load existing preferred network list: '%s'", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    parse_old_preferred_networks (response, ctx);
    set_preferred_networks_clear_next (self, task);
}

static void
set_preferred_networks_query_sim_capacity_ready (MMBaseModem *modem,
                                                 GAsyncResult *res,
                                                 GTask *task)
{
    MMBaseSim                   *self;
    GError                      *error = NULL;
    SetPreferredNetworksContext *ctx;
    const gchar                 *response;
    guint                        max_index;
    guint                        networks_count;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't query preferred network list capacity: '%s'", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!mm_sim_parse_cpol_test_response (response, NULL, &max_index, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Compare the number of networks to maximum index returned by AT+CPOL=? */
    networks_count = g_list_length (ctx->set_list);
    if (networks_count > max_index) {
        mm_obj_warn (self, "can't set %u preferred networks; SIM capacity: %u", networks_count, max_index);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_TOO_MANY,
                                 "Too many networks; SIM capacity %u", max_index);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "setting %u preferred networks, SIM capacity: %u", networks_count, max_index);
    mm_base_modem_at_command (
        self->priv->modem,
        "+CPOL?",
        20,
        FALSE,
        (GAsyncReadyCallback)set_preferred_networks_load_existing_ready,
        task);
}

static void
set_preferred_networks (MMBaseSim *self,
                        GList *preferred_network_list,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GTask *task;
    SetPreferredNetworksContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    if (check_preferred_networks_disabled (self)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "setting preferred networks is unsupported");
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "set preferred networks: loading existing networks...");

    ctx = g_slice_new0 (SetPreferredNetworksContext);
    ctx->set_list = mm_sim_preferred_network_list_copy (preferred_network_list);
    ctx->clear_index = g_array_new (FALSE, TRUE, sizeof (guint));
    if (mm_iface_modem_is_5g (MM_IFACE_MODEM (self->priv->modem)))
        ctx->act_count = 5;
    else if (mm_iface_modem_is_4g (MM_IFACE_MODEM (self->priv->modem)))
        ctx->act_count = 4;
    else if (mm_iface_modem_is_3g (MM_IFACE_MODEM (self->priv->modem)))
        ctx->act_count = 3;
    else if (mm_iface_modem_is_2g (MM_IFACE_MODEM (self->priv->modem)))
        ctx->act_count = 2;
    g_task_set_task_data (task, ctx, (GDestroyNotify) set_preferred_network_context_free);

    /* Query SIM capacity first to find out how many preferred networks it can take */
    mm_base_modem_at_command (
        self->priv->modem,
        "+CPOL=?",
        20,
        FALSE, /* Do not cache, the response depends on SIM card properties */
        (GAsyncReadyCallback)set_preferred_networks_query_sim_capacity_ready,
        task);
}

/*****************************************************************************/
/* SET PREFERRED NETWORKS (DBus call handling) */

typedef struct {
    MMBaseSim             *self;
    GDBusMethodInvocation *invocation;
    GVariant              *networks;
} HandleSetPreferredNetworksContext;

static void
handle_set_preferred_networks_context_free (HandleSetPreferredNetworksContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_variant_unref (ctx->networks);
    g_free (ctx);
}

static void
handle_set_preferred_networks_ready (MMBaseSim *self,
                                     GAsyncResult *res,
                                     HandleSetPreferredNetworksContext *ctx)
{
    GError *error = NULL;

    MM_BASE_SIM_GET_CLASS (self)->set_preferred_networks_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't set preferred networks: %s", error->message);
        g_dbus_method_invocation_take_error (ctx->invocation, g_steal_pointer (&error));
    } else {
        mm_gdbus_sim_set_preferred_networks (MM_GDBUS_SIM (self), ctx->networks);
        mm_gdbus_sim_complete_set_preferred_networks (MM_GDBUS_SIM (self), ctx->invocation);
    }

    handle_set_preferred_networks_context_free (ctx);
}

static void
handle_set_preferred_networks_auth_ready (MMBaseModem *modem,
                                          GAsyncResult *res,
                                          HandleSetPreferredNetworksContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_preferred_networks_context_free (ctx);
        return;
    }

    if (!mm_gdbus_sim_get_active (MM_GDBUS_SIM (ctx->self))) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set preferred networks: "
                                               "SIM not currently active");
        handle_set_preferred_networks_context_free (ctx);
        return;
    }

    if (IS_ESIM_WITHOUT_PROFILES (ctx->self)) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set preferred networks: "
                                               "eSIM without profiles");
        handle_set_preferred_networks_context_free (ctx);
        return;
    }

    if (!MM_BASE_SIM_GET_CLASS (ctx->self)->set_preferred_networks ||
        !MM_BASE_SIM_GET_CLASS (ctx->self)->set_preferred_networks_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set preferred networks: "
                                               "not implemented");
        handle_set_preferred_networks_context_free (ctx);
        return;
    }

    MM_BASE_SIM_GET_CLASS (ctx->self)->set_preferred_networks (
            ctx->self,
            mm_sim_preferred_network_list_new_from_variant (ctx->networks),
            (GAsyncReadyCallback)handle_set_preferred_networks_ready,
            ctx);
}

static gboolean
handle_set_preferred_networks (MMBaseSim *self,
                               GDBusMethodInvocation *invocation,
                               GVariant *networks_variant)
{
    HandleSetPreferredNetworksContext *ctx;

    ctx = g_new0 (HandleSetPreferredNetworksContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    ctx->networks = g_variant_ref (networks_variant);

    mm_base_modem_authorize (self->priv->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_preferred_networks_auth_ready,
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
    g_signal_connect (self,
                      "handle-set-preferred-networks",
                      G_CALLBACK (handle_set_preferred_networks),
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
/* Preferred networks */

static GList *
parse_preferred_networks (const gchar  *response,
                          GError      **error)
{
    gchar **entries;
    gchar **iter;
    GList  *result = NULL;

    entries = g_strsplit_set (response, "\r\n", -1);
    for (iter = entries; iter && *iter; iter++) {
        gchar                   *operator_code = NULL;
        gboolean                 gsm_act;
        gboolean                 gsm_compact_act;
        gboolean                 utran_act;
        gboolean                 eutran_act;
        gboolean                 ngran_act;
        MMSimPreferredNetwork   *preferred_network = NULL;
        MMModemAccessTechnology  act = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

        g_strstrip (*iter);
        if (strlen (*iter) == 0)
            continue;

        if (mm_sim_parse_cpol_query_response (*iter,
                                              NULL,
                                              &operator_code,
                                              &gsm_act,
                                              &gsm_compact_act,
                                              &utran_act,
                                              &eutran_act,
                                              &ngran_act,
                                              NULL,
                                              error)) {
            preferred_network = mm_sim_preferred_network_new ();
            mm_sim_preferred_network_set_operator_code (preferred_network, operator_code);
            if (gsm_act)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_GSM;
            if (gsm_compact_act)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT;
            if (utran_act)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
            if (eutran_act)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_LTE;
            if (ngran_act)
                act |= MM_MODEM_ACCESS_TECHNOLOGY_5GNR;
            mm_sim_preferred_network_set_access_technology (preferred_network, act);
            result = g_list_append (result, preferred_network);
        } else
            break;
        g_free (operator_code);
    }
    g_strfreev (entries);

    return result;
}

static GList *
load_preferred_networks_finish (MMBaseSim     *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    gchar *result;
    GList *preferred_network_list;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return NULL;

    preferred_network_list = parse_preferred_networks (result, error);
    mm_obj_dbg (self, "loaded %u preferred networks", g_list_length (preferred_network_list));

    g_free (result);

    return preferred_network_list;
}

STR_REPLY_READY_FN (load_preferred_networks)

static void
load_preferred_networks_set_format_ready (MMBaseModem *modem,
                                          GAsyncResult *res,
                                          GTask *task)
{
    MMBaseSim *self;
    GError *error = NULL;

    self = g_task_get_source_object (task);

    /* Ignore error */
    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_obj_dbg (self, "setting preferred network list format failed: '%s'", error->message);
        g_error_free (error);
    }

    mm_obj_dbg (self, "loading preferred networks...");

    mm_base_modem_at_command (
        modem,
        "+CPOL?",
        20,
        FALSE,
        (GAsyncReadyCallback)load_preferred_networks_command_ready,
        task);
}

static void
load_preferred_networks_cpls_command_ready (MMBaseModem *modem,
                                            GAsyncResult *res,
                                            GTask *task)
{
    MMBaseSim *self;
    GError *error = NULL;

    self = g_task_get_source_object (task);

    /* AT+CPLS may not be supported so we ignore any error and proceed even if it fails */
    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_obj_dbg (self, "selecting user-defined preferred network list failed: '%s'", error->message);
        g_error_free (error);
    }

    mm_obj_dbg (self, "setting preferred networks format...");

    /* Request numeric MCCMNC format */
    mm_base_modem_at_command (
        modem,
        "+CPOL=,2",
        20,
        FALSE,
        (GAsyncReadyCallback)load_preferred_networks_set_format_ready,
        task);
}

static void
load_preferred_networks (MMBaseSim           *self,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (check_preferred_networks_disabled (self)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "setting preferred networks is unsupported");
        g_object_unref (task);
        return;
    }

    /* Invoke AT+CPLS=0 first to make sure the correct (user-defined) preferred network list is selected */
    mm_obj_dbg (self, "selecting user-defined preferred network list...");

    mm_base_modem_at_command (
        self->priv->modem,
        "+CPLS=0",
        20,
        FALSE,
        (GAsyncReadyCallback)load_preferred_networks_cpls_command_ready,
        task);
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
/* GID1 and GID2 */

static GByteArray *
parse_gid (const gchar  *response,
           GError      **error)
{
    guint             sw1 = 0;
    guint             sw2 = 0;
    g_autofree gchar *hex = NULL;

    if (!mm_3gpp_parse_crsm_response (response, &sw1, &sw2, &hex, error))
        return NULL;

    if ((sw1 == 0x90 && sw2 == 0x00) ||
        (sw1 == 0x91) ||
        (sw1 == 0x92) ||
        (sw1 == 0x9f)) {
        guint8 *bin = NULL;
        gsize   binlen = 0;

        /* Convert hex string to binary */
        bin = mm_utils_hexstr2bin (hex, -1, &binlen, error);
        if (!bin) {
            g_prefix_error (error, "SIM returned malformed response '%s': ", hex);
            return NULL;
        }

        /* return as bytearray */
        return g_byte_array_new_take (bin, binlen);
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "SIM failed to handle CRSM request (sw1 %d sw2 %d)", sw1, sw2);
    return NULL;
}

static GByteArray *
common_load_gid_finish (MMBaseSim     *self,
                        GAsyncResult  *res,
                        GError       **error)
{
    g_autofree gchar *result = NULL;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return NULL;

    return parse_gid (result, error);
}

STR_REPLY_READY_FN (load_gid1)
STR_REPLY_READY_FN (load_gid2)

static void
load_gid1 (MMBaseSim           *self,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    /* READ BINARY of EFgid1 */
    mm_base_modem_at_command (
        self->priv->modem,
        "+CRSM=176,28478,0,0,0",
        10,
        FALSE,
        (GAsyncReadyCallback)load_gid1_command_ready,
        g_task_new (self, NULL, callback, user_data));
}

static void
load_gid2 (MMBaseSim           *self,
           GAsyncReadyCallback  callback,
           gpointer             user_data)
{
    /* READ BINARY of EFgid2 */
    mm_base_modem_at_command (
        self->priv->modem,
        "+CRSM=176,28479,0,0,0",
        10,
        FALSE,
        (GAsyncReadyCallback)load_gid2_command_ready,
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
    INITIALIZATION_STEP_SIM_TYPE,
    INITIALIZATION_STEP_ESIM_STATUS,
    INITIALIZATION_STEP_SIM_IDENTIFIER,
    INITIALIZATION_STEP_IMSI,
    INITIALIZATION_STEP_OPERATOR_ID,
    INITIALIZATION_STEP_OPERATOR_NAME,
    INITIALIZATION_STEP_EMERGENCY_NUMBERS,
    INITIALIZATION_STEP_PREFERRED_NETWORKS,
    INITIALIZATION_STEP_GID1,
    INITIALIZATION_STEP_GID2,
    INITIALIZATION_STEP_EID,
    INITIALIZATION_STEP_REMOVABILITY,
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

#undef COMMON_STR_REPLY_READY_FN
#define COMMON_STR_REPLY_READY_FN(NAME,DISPLAY,VALUE_FORMAT)                              \
    static void                                                                           \
    init_load_##NAME##_ready (MMBaseSim    *self,                                         \
                              GAsyncResult *res,                                          \
                              GTask        *task)                                         \
    {                                                                                     \
        InitAsyncContext  *ctx;                                                           \
        g_autoptr(GError)  error = NULL;                                                  \
        g_autofree gchar  *val = NULL;                                                    \
                                                                                          \
        val = MM_BASE_SIM_GET_CLASS (self)->load_##NAME##_finish (self, res, &error);     \
        mm_gdbus_sim_set_##NAME (MM_GDBUS_SIM (self), val);                               \
                                                                                          \
        if (error)                                                                        \
            mm_obj_dbg (self, "couldn't load %s: %s", DISPLAY, error->message);           \
        else                                                                              \
            mm_obj_info (self, "loaded %s: %s", DISPLAY, VALUE_FORMAT (val));             \
                                                                                          \
        /* Go on to next step */                                                          \
        ctx = g_task_get_task_data (task);                                                \
        ctx->step++;                                                                      \
        interface_initialization_step (task);                                             \
    }

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY) COMMON_STR_REPLY_READY_FN (NAME, DISPLAY, (const gchar *))

#undef PERSONAL_STR_REPLY_READY_FN
#define PERSONAL_STR_REPLY_READY_FN(NAME,DISPLAY) COMMON_STR_REPLY_READY_FN (NAME, DISPLAY, mm_log_str_personal_info)

#undef ENUM_REPLY_READY_FN
#define ENUM_REPLY_READY_FN(NAME,DISPLAY,ENUM_TYPE,ENUM_GET_STRING)                   \
    static void                                                                       \
    init_load_##NAME##_ready (MMBaseSim    *self,                                     \
                              GAsyncResult *res,                                      \
                              GTask        *task)                                     \
    {                                                                                 \
        InitAsyncContext  *ctx;                                                       \
        g_autoptr(GError)  error = NULL;                                              \
        ENUM_TYPE          val;                                                       \
                                                                                      \
        val = MM_BASE_SIM_GET_CLASS (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_sim_set_##NAME (MM_GDBUS_SIM (self), (guint) val);                   \
                                                                                      \
        if (error)                                                                    \
            mm_obj_dbg (self, "couldn't load %s: %s", DISPLAY, error->message);       \
        else                                                                          \
            mm_obj_info (self, "loaded %s: %s", DISPLAY, ENUM_GET_STRING (val));      \
                                                                                      \
        /* Go on to next step */                                                      \
        ctx = g_task_get_task_data (task);                                            \
        ctx->step++;                                                                  \
        interface_initialization_step (task);                                         \
    }

#undef BYTEARRAY_REPLY_READY_FN
#define BYTEARRAY_REPLY_READY_FN(NAME,DISPLAY)                                    \
    static void                                                                   \
    init_load_##NAME##_ready (MMBaseSim    *self,                                 \
                              GAsyncResult *res,                                  \
                              GTask        *task)                                 \
    {                                                                             \
        InitAsyncContext      *ctx;                                               \
        g_autoptr(GError)      error = NULL;                                      \
        g_autoptr(GByteArray)  bytearray = NULL;                                  \
                                                                                  \
        bytearray = MM_BASE_SIM_GET_CLASS (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_sim_set_##NAME (MM_GDBUS_SIM (self),                             \
                                 (bytearray ?                                     \
                                  g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, \
                                                             bytearray->data,     \
                                                             bytearray->len,      \
                                                             sizeof (guint8)) :   \
                                  NULL));                                         \
                                                                                  \
        if (error)                                                                \
            mm_obj_dbg (self, "couldn't load %s: %s", DISPLAY, error->message);   \
        else {                                                                    \
            g_autofree gchar *bytearray_str = NULL;                               \
                                                                                  \
            bytearray_str = mm_utils_bin2hexstr (bytearray->data, bytearray->len); \
            mm_obj_info (self, "loaded %s: %s", DISPLAY, bytearray_str);          \
        }                                                                         \
                                                                                  \
        /* Go on to next step */                                                  \
        ctx = g_task_get_task_data (task);                                        \
        ctx->step++;                                                              \
        interface_initialization_step (task);                                     \
    }

ENUM_REPLY_READY_FN         (removability, "removability", MMSimRemovability, mm_sim_removability_get_string)
PERSONAL_STR_REPLY_READY_FN (eid,          "EID")
BYTEARRAY_REPLY_READY_FN    (gid2,         "GID2")
BYTEARRAY_REPLY_READY_FN    (gid1,         "GID1")

static void
init_load_preferred_networks_ready (MMBaseSim    *self,
                                    GAsyncResult *res,
                                    GTask        *task)
{
    InitAsyncContext  *ctx;
    g_autoptr(GError)  error = NULL;
    GList             *preferred_nets_list;

    preferred_nets_list = MM_BASE_SIM_GET_CLASS (self)->load_preferred_networks_finish (self, res, &error);
    if (error)
        mm_obj_dbg (self, "couldn't load list of preferred networks: %s", error->message);
    else {
        g_autoptr(GString)  str = NULL;
        GList              *l;

        str = g_string_new ("");
        for (l = preferred_nets_list; l; l = g_list_next (l)) {
            MMSimPreferredNetwork *item;
            g_autofree gchar      *access_tech_str = NULL;

            item = (MMSimPreferredNetwork *)(l->data);
            access_tech_str = mm_modem_access_technology_build_string_from_mask (mm_sim_preferred_network_get_access_technology (item));
            g_string_append_printf (str, "%s%s (%s)", str->len ? ", " : "", mm_sim_preferred_network_get_operator_code (item), access_tech_str);
        }

        mm_obj_info (self, "loaded list of preferred networks: %s", str->str);
    }

    mm_gdbus_sim_set_preferred_networks (MM_GDBUS_SIM (self),
                                         mm_sim_preferred_network_list_get_variant (preferred_nets_list));

    g_list_free_full (preferred_nets_list, (GDestroyNotify) mm_sim_preferred_network_free);

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_initialization_step (task);
}

static void
init_load_emergency_numbers_ready (MMBaseSim    *self,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    InitAsyncContext  *ctx;
    g_autoptr(GError)  error = NULL;
    g_auto(GStrv)      str_list = NULL;

    str_list = MM_BASE_SIM_GET_CLASS (self)->load_emergency_numbers_finish (self, res, &error);
    if (error)
        mm_obj_dbg (self, "couldn't load list of emergency numbers: %s", error->message);
    else {
        g_autoptr(GString) str = NULL;
        guint              i;

        str = g_string_new ("");
        for (i = 0; str_list && str_list[i]; i++)
            g_string_append_printf (str, "%s%s", str->len ? ", " : "", str_list[i]);
        mm_obj_info (self, "loaded list of emergency numbers: %s", str->str);
    }

    mm_gdbus_sim_set_emergency_numbers (MM_GDBUS_SIM (self), (const gchar *const *) str_list);

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_initialization_step (task);
}

STR_REPLY_READY_FN          (operator_name,       "operator name")
STR_REPLY_READY_FN          (operator_identifier, "operator identifier")
PERSONAL_STR_REPLY_READY_FN (imsi,                "IMSI")

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
    } else
        mm_obj_info (self, "loaded SIM identifier: %s", mm_log_str_personal_info (simid));

    mm_gdbus_sim_set_sim_identifier (MM_GDBUS_SIM (self), simid);
    g_free (simid);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

ENUM_REPLY_READY_FN (esim_status, "esim status", MMSimEsimStatus, mm_sim_esim_status_get_string)
ENUM_REPLY_READY_FN (sim_type,    "sim type",    MMSimType,       mm_sim_type_get_string)

static void
init_wait_sim_ready (MMBaseSim    *self,
                     GAsyncResult *res,
                     GTask        *task)
{
    InitAsyncContext  *ctx;
    g_autoptr(GError)  error = NULL;

    if (!MM_BASE_SIM_GET_CLASS (self)->wait_sim_ready_finish (self, res, &error))
        mm_obj_dbg (self, "couldn't wait for SIM to be ready: %s", error->message);

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

    case INITIALIZATION_STEP_SIM_TYPE:
        if (mm_gdbus_sim_get_sim_type (MM_GDBUS_SIM (self)) == MM_SIM_TYPE_UNKNOWN &&
            MM_BASE_SIM_GET_CLASS (self)->load_sim_type &&
            MM_BASE_SIM_GET_CLASS (self)->load_sim_type_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_sim_type (
                self,
                (GAsyncReadyCallback)init_load_sim_type_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_ESIM_STATUS:
        /* Don't load eSIM status if the SIM is known to be a physical SIM */
        if (IS_PSIM (self))
            mm_obj_dbg (self, "not loading eSIM status in physical SIM");
        else if (mm_gdbus_sim_get_esim_status (MM_GDBUS_SIM (self)) == MM_SIM_ESIM_STATUS_UNKNOWN &&
                 MM_BASE_SIM_GET_CLASS (self)->load_esim_status &&
                 MM_BASE_SIM_GET_CLASS (self)->load_esim_status_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_esim_status (
                self,
                (GAsyncReadyCallback)init_load_esim_status_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_SIM_IDENTIFIER:
        /* Don't load SIM ICCID if the SIM is known to be an eSIM without
         * profiles; otherwise (if physical SIM, or if eSIM with profile, or if
         * SIM type unknown) try to load it. */
        if (IS_ESIM_WITHOUT_PROFILES (self))
            mm_obj_dbg (self, "not loading SIM identifier in eSIM without profiles");
        else if (mm_gdbus_sim_get_sim_identifier (MM_GDBUS_SIM (self)) == NULL &&
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
        /* Don't load SIM IMSI if the SIM is known to be an eSIM without
         * profiles; otherwise (if physical SIM, or if eSIM with profile, or if
         * SIM type unknown) try to load it. */
        if (IS_ESIM_WITHOUT_PROFILES (self))
            mm_obj_dbg (self, "not loading IMSI in eSIM without profiles");
        else if (mm_gdbus_sim_get_imsi (MM_GDBUS_SIM (self)) == NULL &&
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

    case INITIALIZATION_STEP_OPERATOR_ID:
        /* Don't load operator ID if the SIM is known to be an eSIM without
         * profiles; otherwise (if physical SIM, or if eSIM with profile, or if
         * SIM type unknown) try to load it. */
        if (IS_ESIM_WITHOUT_PROFILES (self))
            mm_obj_dbg (self, "not loading operator ID in eSIM without profiles");
        else if (mm_gdbus_sim_get_operator_identifier (MM_GDBUS_SIM (self)) == NULL &&
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
        /* Don't load operator name if the SIM is known to be an eSIM without
         * profiles; otherwise (if physical SIM, or if eSIM with profile, or if
         * SIM type unknown) try to load it. */
        if (IS_ESIM_WITHOUT_PROFILES (self))
            mm_obj_dbg (self, "not loading operator name in eSIM without profiles");
        else if (mm_gdbus_sim_get_operator_name (MM_GDBUS_SIM (self)) == NULL &&
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
        /* Don't load emergency numbers if the SIM is known to be an eSIM without
         * profiles; otherwise (if physical SIM, or if eSIM with profile, or if
         * SIM type unknown) try to load it. */
        if (IS_ESIM_WITHOUT_PROFILES (self))
            mm_obj_dbg (self, "not loading emergency numbers in eSIM without profiles");
        else if (mm_gdbus_sim_get_emergency_numbers (MM_GDBUS_SIM (self)) == NULL &&
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

    case INITIALIZATION_STEP_PREFERRED_NETWORKS:
        /* Don't load preferred networks if the SIM is known to be an eSIM without
         * profiles; otherwise (if physical SIM, or if eSIM with profile, or if
         * SIM type unknown) try to load it. */
        if (IS_ESIM_WITHOUT_PROFILES (self))
            mm_obj_dbg (self, "not loading preferred networks in eSIM without profiles");
        else if (mm_gdbus_sim_get_preferred_networks (MM_GDBUS_SIM (self)) == NULL &&
                 MM_BASE_SIM_GET_CLASS (self)->load_preferred_networks &&
                 MM_BASE_SIM_GET_CLASS (self)->load_preferred_networks_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_preferred_networks (
                self,
                (GAsyncReadyCallback)init_load_preferred_networks_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_GID1:
        /* Don't load GID1 if the SIM is known to be an eSIM without profiles;
         * otherwise (if physical SIM, or if eSIM with profile, or if
         * SIM type unknown) try to load it. */
        if (IS_ESIM_WITHOUT_PROFILES (self))
            mm_obj_dbg (self, "not loading GID1 in eSIM without profiles");
        else if (mm_gdbus_sim_get_gid1 (MM_GDBUS_SIM (self)) == NULL &&
                 MM_BASE_SIM_GET_CLASS (self)->load_gid1 &&
                 MM_BASE_SIM_GET_CLASS (self)->load_gid1_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_gid1 (
                self,
                (GAsyncReadyCallback)init_load_gid1_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_GID2:
        /* Don't load GID2 if the SIM is known to be an eSIM without profiles;
         * otherwise (if physical SIM, or if eSIM with profile, or if
         * SIM type unknown) try to load it. */
        if (IS_ESIM_WITHOUT_PROFILES (self))
            mm_obj_dbg (self, "not loading GID2 in eSIM without profiles");
        else if (mm_gdbus_sim_get_gid2 (MM_GDBUS_SIM (self)) == NULL &&
                 MM_BASE_SIM_GET_CLASS (self)->load_gid2 &&
                 MM_BASE_SIM_GET_CLASS (self)->load_gid2_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_gid2 (
                self,
                (GAsyncReadyCallback)init_load_gid2_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case INITIALIZATION_STEP_EID:
        /* Don't load EID if the SIM is known to be a physical SIM; otherwise
         * (if eSIM with or without profiles) try to load it. */
        if (IS_PSIM (self))
            mm_obj_dbg (self, "not loading EID in physical SIM");
        else if (mm_gdbus_sim_get_eid (MM_GDBUS_SIM (self)) == NULL &&
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

    case INITIALIZATION_STEP_REMOVABILITY:
        /* Although not very common, there are removable eSIMs, so always try to
         * load it, regardless of SIM type. */
        if (mm_gdbus_sim_get_removability (MM_GDBUS_SIM (self)) == MM_SIM_REMOVABILITY_UNKNOWN &&
            MM_BASE_SIM_GET_CLASS (self)->load_removability &&
            MM_BASE_SIM_GET_CLASS (self)->load_removability_finish) {
            MM_BASE_SIM_GET_CLASS (self)->load_removability (
                self,
                (GAsyncReadyCallback)init_load_removability_ready,
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
    mm_gdbus_sim_set_gid1 (MM_GDBUS_SIM (initable), NULL);
    mm_gdbus_sim_set_gid2 (MM_GDBUS_SIM (initable), NULL);

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
    klass->load_preferred_networks = load_preferred_networks;
    klass->load_preferred_networks_finish = load_preferred_networks_finish;
    klass->load_gid1 = load_gid1;
    klass->load_gid1_finish = common_load_gid_finish;
    klass->load_gid2 = load_gid2;
    klass->load_gid2_finish = common_load_gid_finish;
    klass->set_preferred_networks = set_preferred_networks;
    klass->set_preferred_networks_finish = set_preferred_networks_finish;
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
