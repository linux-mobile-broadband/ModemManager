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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-iface-modem.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-ublox.h"
#include "mm-broadband-bearer-ublox.h"
#include "mm-modem-helpers-ublox.h"
#include "mm-ublox-enums-types.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemUblox, mm_broadband_modem_ublox, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init))

struct _MMBroadbandModemUbloxPrivate {
    /* USB profile in use */
    MMUbloxUsbProfile profile;
    gboolean          profile_checked;
    /* Networking mode in use */
    MMUbloxNetworkingMode mode;
    gboolean              mode_checked;

    /* Flag to specify whether a power operation is ongoing */
    gboolean power_operation_ongoing;

    /* Mode combination to apply if "any" requested */
    MMModemMode any_allowed;
};

/*****************************************************************************/

static gboolean
acquire_power_operation (MMBroadbandModemUblox  *self,
                         GError                **error)
{
    if (self->priv->power_operation_ongoing) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY,
                     "An operation which requires power updates is currently in progress");
        return FALSE;
    }
    self->priv->power_operation_ongoing = TRUE;
    return TRUE;
}

static void
release_power_operation (MMBroadbandModemUblox *self)
{
    g_assert (self->priv->power_operation_ongoing);
    self->priv->power_operation_ongoing = FALSE;
}

/*****************************************************************************/
/* Set allowed modes (Modem interface) */

typedef enum {
    SET_CURRENT_MODES_STEP_FIRST,
    SET_CURRENT_MODES_STEP_ACQUIRE,
    SET_CURRENT_MODES_STEP_CURRENT_POWER,
    SET_CURRENT_MODES_STEP_POWER_DOWN,
    SET_CURRENT_MODES_STEP_URAT,
    SET_CURRENT_MODES_STEP_RECOVER_CURRENT_POWER,
    SET_CURRENT_MODES_STEP_RELEASE,
    SET_CURRENT_MODES_STEP_LAST,
} SetCurrentModesStep;

typedef struct {
    MMBroadbandModemUblox *self;
    SetCurrentModesStep    step;
    gchar                 *command;
    MMModemPowerState      initial_state;
    GError                *saved_error;
} SetCurrentModesContext;

static void
set_current_modes_context_free (SetCurrentModesContext *ctx)
{
    g_assert (!ctx->saved_error);
    g_free (ctx->command);
    g_object_unref (ctx->self);
    g_slice_free (SetCurrentModesContext, ctx);
}

static gboolean
set_current_modes_finish (MMIfaceModem  *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void set_current_modes_step (GTask *task);

static void
set_current_modes_recover_power_ready (MMBaseModem  *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    SetCurrentModesContext *ctx;

    ctx = (SetCurrentModesContext *) g_task_get_task_data (task);
    g_assert (ctx);

    /* propagate the error if none already set */
    mm_base_modem_at_command_finish (self, res, ctx->saved_error ? NULL : &ctx->saved_error);

    /* Go to next step (release power operation) regardless of the result */
    ctx->step++;
    set_current_modes_step (task);
}

static void
set_current_modes_urat_ready (MMBaseModem  *self,
                              GAsyncResult *res,
                              GTask        *task)
{
    SetCurrentModesContext *ctx;

    ctx = (SetCurrentModesContext *) g_task_get_task_data (task);
    g_assert (ctx);

    mm_base_modem_at_command_finish (self, res, &ctx->saved_error);

    /* Go to next step (recover current power) regardless of the result */
    ctx->step++;
    set_current_modes_step (task);
}

static void
set_current_modes_low_power_ready (MMBaseModem  *self,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    SetCurrentModesContext *ctx;

    ctx = (SetCurrentModesContext *) g_task_get_task_data (task);
    g_assert (ctx);

    if (!mm_base_modem_at_command_finish (self, res, &ctx->saved_error))
        ctx->step = SET_CURRENT_MODES_STEP_RELEASE;
    else
        ctx->step++;

    set_current_modes_step (task);
}

static void
set_current_modes_current_power_ready (MMBaseModem  *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    SetCurrentModesContext *ctx;
    const gchar            *response;

    ctx = (SetCurrentModesContext *) g_task_get_task_data (task);
    g_assert (ctx);

    response = mm_base_modem_at_command_finish (self, res, &ctx->saved_error);
    if (!response || !mm_ublox_parse_cfun_response (response, &ctx->initial_state, &ctx->saved_error))
        ctx->step = SET_CURRENT_MODES_STEP_RELEASE;
    else
        ctx->step++;

    set_current_modes_step (task);
}

static void
set_current_modes_step (GTask *task)
{
    SetCurrentModesContext *ctx;

    ctx = (SetCurrentModesContext *) g_task_get_task_data (task);
    g_assert (ctx);

    switch (ctx->step) {
    case SET_CURRENT_MODES_STEP_FIRST:
        ctx->step++;
        /* fall down */

    case SET_CURRENT_MODES_STEP_ACQUIRE:
        mm_dbg ("acquiring power operation...");
        if (!acquire_power_operation (ctx->self, &ctx->saved_error)) {
            ctx->step = SET_CURRENT_MODES_STEP_LAST;
            set_current_modes_step (task);
            return;
        }
        ctx->step++;
        /* fall down */

    case SET_CURRENT_MODES_STEP_CURRENT_POWER:
        mm_dbg ("checking current power operation...");
        mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                  "+CFUN?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback) set_current_modes_current_power_ready,
                                  task);
        return;

    case SET_CURRENT_MODES_STEP_POWER_DOWN:
        if (ctx->initial_state != MM_MODEM_POWER_STATE_LOW) {
            mm_dbg ("powering down before AcT change...");
            mm_base_modem_at_command (
                MM_BASE_MODEM (ctx->self),
                "+CFUN=4",
                3,
                FALSE,
                (GAsyncReadyCallback) set_current_modes_low_power_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall down */

    case SET_CURRENT_MODES_STEP_URAT:
        mm_dbg ("updating AcT...");
        mm_base_modem_at_command (
            MM_BASE_MODEM (ctx->self),
            ctx->command,
            3,
            FALSE,
            (GAsyncReadyCallback) set_current_modes_urat_ready,
            task);
        return;

    case SET_CURRENT_MODES_STEP_RECOVER_CURRENT_POWER:
        if (ctx->initial_state != MM_MODEM_POWER_STATE_LOW) {
            mm_dbg ("recovering power state after AcT change...");
            mm_base_modem_at_command (
                MM_BASE_MODEM (ctx->self),
                "+CFUN=1",
                3,
                FALSE,
                (GAsyncReadyCallback) set_current_modes_recover_power_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall down */

    case SET_CURRENT_MODES_STEP_RELEASE:
        mm_dbg ("releasing power operation...");
        release_power_operation (ctx->self);
        ctx->step++;
        /* fall down */

    case SET_CURRENT_MODES_STEP_LAST:
        if (ctx->saved_error) {
            g_task_return_error (task, ctx->saved_error);
            ctx->saved_error = NULL;
        } else
            g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
}

static void
set_current_modes (MMIfaceModem        *self,
                   MMModemMode          allowed,
                   MMModemMode          preferred,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask                  *task;
    SetCurrentModesContext *ctx;
    gchar                  *command;
    GError                 *error = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    /* Handle ANY */
    if (allowed == MM_MODEM_MODE_ANY)
        allowed = MM_BROADBAND_MODEM_UBLOX (self)->priv->any_allowed;

    /* Build command */
    command = mm_ublox_build_urat_set_command (allowed, preferred, &error);
    if (!command) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SetCurrentModesContext);
    ctx->self = MM_BROADBAND_MODEM_UBLOX (g_object_ref (self));
    ctx->command = command;
    ctx->initial_state = MM_MODEM_POWER_STATE_UNKNOWN;
    ctx->step = SET_CURRENT_MODES_STEP_FIRST;
    g_task_set_task_data (task, ctx, (GDestroyNotify) set_current_modes_context_free);

    set_current_modes_step (task);
}

/*****************************************************************************/
/* Load current modes (Modem interface) */

static gboolean
load_current_modes_finish (MMIfaceModem  *self,
                           GAsyncResult  *res,
                           MMModemMode   *allowed,
                           MMModemMode   *preferred,
                           GError       **error)
{
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    return mm_ublox_parse_urat_read_response (response, allowed, preferred, error);
}

static void
load_current_modes (MMIfaceModem        *self,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+URAT?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Load supported modes (Modem interface) */

static GArray *
load_supported_modes_finish (MMIfaceModem  *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    const gchar *response;
    GArray      *combinations;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    if (!(combinations = mm_ublox_parse_urat_test_response (response, error)))
        return FALSE;

    if (!(combinations = mm_ublox_filter_supported_modes (mm_iface_modem_get_model (self), combinations, error)))
        return FALSE;

    /* Decide and store which combination to apply when ANY requested */
    MM_BROADBAND_MODEM_UBLOX (self)->priv->any_allowed = mm_ublox_get_modem_mode_any (combinations);

    return combinations;
}

static void
load_supported_modes (MMIfaceModem        *self,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+URAT=?",
        3,
        TRUE,
        callback,
        user_data);
}

/*****************************************************************************/
/* Power state loading (Modem interface) */

static MMModemPowerState
load_power_state_finish (MMIfaceModem  *self,
                         GAsyncResult  *res,
                         GError       **error)
{
    MMModemPowerState  state = MM_MODEM_POWER_STATE_UNKNOWN;
    const gchar       *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (response)
        mm_ublox_parse_cfun_response (response, &state, error);
    return state;
}

static void
load_power_state (MMIfaceModem        *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CFUN?",
                              3,
                              FALSE,
                              callback,
                              user_data);
}

/*****************************************************************************/
/* Modem power up/down/off (Modem interface) */

static gboolean
common_modem_power_operation_finish (MMIfaceModem  *self,
                                     GAsyncResult  *res,
                                     GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
power_operation_ready (MMBaseModem  *self,
                       GAsyncResult *res,
                       GTask        *task)
{
    GError *error = NULL;

    release_power_operation (MM_BROADBAND_MODEM_UBLOX (self));

    if (!mm_base_modem_at_command_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_modem_power_operation (MMBroadbandModemUblox  *self,
                              const gchar            *command,
                              GAsyncReadyCallback     callback,
                              gpointer                user_data)
{
    GTask  *task;
    GError *error = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    /* Fail if there is already an ongoing power management operation */
    if (!acquire_power_operation (self, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Use AT+CFUN=4 for power down, puts device in airplane mode */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              command,
                              30,
                              FALSE,
                              (GAsyncReadyCallback) power_operation_ready,
                              task);
}

static void
modem_reset (MMIfaceModem        *self,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_UBLOX (self), "+CFUN=16", callback, user_data);
}

static void
modem_power_off (MMIfaceModem        *self,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_UBLOX (self), "+CPWROFF", callback, user_data);
}

static void
modem_power_down (MMIfaceModem        *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_UBLOX (self), "+CFUN=4", callback, user_data);
}

static void
modem_power_up (MMIfaceModem        *self,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    common_modem_power_operation (MM_BROADBAND_MODEM_UBLOX (self), "+CFUN=1", callback, user_data);
}

/*****************************************************************************/
/* Create Bearer (Modem interface) */

typedef enum {
    CREATE_BEARER_STEP_FIRST,
    CREATE_BEARER_STEP_CHECK_PROFILE,
    CREATE_BEARER_STEP_CHECK_MODE,
    CREATE_BEARER_STEP_CREATE_BEARER,
    CREATE_BEARER_STEP_LAST,
} CreateBearerStep;

typedef struct {
    MMBroadbandModemUblox *self;
    CreateBearerStep       step;
    MMBearerProperties    *properties;
    MMBaseBearer          *bearer;
} CreateBearerContext;

static void
create_bearer_context_free (CreateBearerContext *ctx)
{
    if (ctx->bearer)
        g_object_unref (ctx->bearer);
    g_object_unref (ctx->properties);
    g_object_unref (ctx->self);
    g_slice_free (CreateBearerContext, ctx);
}

static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem  *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    return MM_BASE_BEARER (g_task_propagate_pointer (G_TASK (res), error));
}

static void create_bearer_step (GTask *task);

static void
broadband_bearer_new_ready (GObject      *source,
                            GAsyncResult *res,
                            GTask        *task)
{
    CreateBearerContext *ctx;
    GError *error = NULL;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);

    g_assert (!ctx->bearer);
    ctx->bearer = mm_broadband_bearer_new_finish (res, &error);
    if (!ctx->bearer) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_dbg ("u-blox: new generic broadband bearer created at DBus path '%s'", mm_base_bearer_get_path (ctx->bearer));
    ctx->step++;
    create_bearer_step (task);
}

static void
broadband_bearer_ublox_new_ready (GObject      *source,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    CreateBearerContext *ctx;
    GError *error = NULL;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);

    g_assert (!ctx->bearer);
    ctx->bearer = mm_broadband_bearer_ublox_new_finish (res, &error);
    if (!ctx->bearer) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_dbg ("u-blox: new u-blox broadband bearer created at DBus path '%s'", mm_base_bearer_get_path (ctx->bearer));
    ctx->step++;
    create_bearer_step (task);
}

static void
mode_check_ready (MMBaseModem  *self,
                  GAsyncResult *res,
                  GTask        *task)
{
    const gchar *response;
    GError *error = NULL;
    CreateBearerContext *ctx;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        mm_dbg ("u-blox: couldn't load current networking mode: %s", error->message);
        g_error_free (error);
    } else if (!mm_ublox_parse_ubmconf_response (response, &ctx->self->priv->mode, &error)) {
        mm_dbg ("u-blox: couldn't parse current networking mode response '%s': %s", response, error->message);
        g_error_free (error);
    } else {
        g_assert (ctx->self->priv->mode != MM_UBLOX_NETWORKING_MODE_UNKNOWN);
        mm_dbg ("u-blox: networking mode loaded: %s", mm_ublox_networking_mode_get_string (ctx->self->priv->mode));
    }

    /* Assume the operation has been performed, even if it may have failed */
    ctx->self->priv->mode_checked = TRUE;

    ctx->step++;
    create_bearer_step (task);
}

static void
profile_check_ready (MMBaseModem  *self,
                     GAsyncResult *res,
                     GTask        *task)
{
    const gchar *response;
    GError *error = NULL;
    CreateBearerContext *ctx;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);

    response = mm_base_modem_at_command_finish (self, res, &error);
    if (!response) {
        mm_dbg ("u-blox: couldn't load current usb profile: %s", error->message);
        g_error_free (error);
    } else if (!mm_ublox_parse_uusbconf_response (response, &ctx->self->priv->profile, &error)) {
        mm_dbg ("u-blox: couldn't parse current usb profile response '%s': %s", response, error->message);
        g_error_free (error);
    } else {
        g_assert (ctx->self->priv->profile != MM_UBLOX_USB_PROFILE_UNKNOWN);
        mm_dbg ("u-blox: usb profile loaded: %s", mm_ublox_usb_profile_get_string (ctx->self->priv->profile));
    }

    /* Assume the operation has been performed, even if it may have failed */
    ctx->self->priv->profile_checked = TRUE;

    ctx->step++;
    create_bearer_step (task);
}

static void
create_bearer_step (GTask *task)
{
    CreateBearerContext *ctx;

    ctx = (CreateBearerContext *) g_task_get_task_data (task);
    switch (ctx->step) {
    case CREATE_BEARER_STEP_FIRST:
        ctx->step++;
        /* fall down */

    case CREATE_BEARER_STEP_CHECK_PROFILE:
        if (!ctx->self->priv->profile_checked) {
            mm_dbg ("u-blox: checking current USB profile...");
            mm_base_modem_at_command (
                MM_BASE_MODEM (ctx->self),
                "+UUSBCONF?",
                3,
                FALSE,
                (GAsyncReadyCallback) profile_check_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall down */

    case CREATE_BEARER_STEP_CHECK_MODE:
        if (!ctx->self->priv->mode_checked) {
            mm_dbg ("u-blox: checking current networking mode...");
            mm_base_modem_at_command (
                MM_BASE_MODEM (ctx->self),
                "+UBMCONF?",
                3,
                FALSE,
                (GAsyncReadyCallback) mode_check_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall down */

    case CREATE_BEARER_STEP_CREATE_BEARER:
        /* If we have a net interface, we'll create a u-blox bearer, unless for
         * any reason we have the back-compatible profile selected, or if we don't
         * know the mode to use. */
        if ((ctx->self->priv->profile == MM_UBLOX_USB_PROFILE_ECM || ctx->self->priv->profile == MM_UBLOX_USB_PROFILE_RNDIS) &&
            (ctx->self->priv->mode == MM_UBLOX_NETWORKING_MODE_BRIDGE || ctx->self->priv->mode == MM_UBLOX_NETWORKING_MODE_ROUTER) &&
            mm_base_modem_peek_best_data_port (MM_BASE_MODEM (ctx->self), MM_PORT_TYPE_NET)) {
            mm_dbg ("u-blox: creating u-blox broadband bearer (%s profile, %s mode)...",
                    mm_ublox_usb_profile_get_string (ctx->self->priv->profile),
                    mm_ublox_networking_mode_get_string (ctx->self->priv->mode));
            mm_broadband_bearer_ublox_new (
                MM_BROADBAND_MODEM (ctx->self),
                ctx->self->priv->profile,
                ctx->self->priv->mode,
                ctx->properties,
                NULL, /* cancellable */
                (GAsyncReadyCallback) broadband_bearer_ublox_new_ready,
                task);
            return;
        }

        /* If usb profile is back-compatible already, or if there is no NET port
         * available, create default generic bearer */
        mm_dbg ("u-blox: creating generic broadband bearer...");
        mm_broadband_bearer_new (MM_BROADBAND_MODEM (ctx->self),
                                 ctx->properties,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback) broadband_bearer_new_ready,
                                 task);
        return;

    case CREATE_BEARER_STEP_LAST:
        g_assert (ctx->bearer);
        g_task_return_pointer (task, g_object_ref (ctx->bearer), (GDestroyNotify) g_object_unref);
        g_object_unref (task);
        return;
    }

    g_assert_not_reached ();
}

static void
modem_create_bearer (MMIfaceModem        *self,
                     MMBearerProperties  *properties,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    CreateBearerContext *ctx;
    GTask               *task;

    ctx = g_slice_new0 (CreateBearerContext);
    ctx->step = CREATE_BEARER_STEP_FIRST;
    ctx->self = g_object_ref (self);
    ctx->properties = g_object_ref (properties);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) create_bearer_context_free);
    create_bearer_step (task);
}

/*****************************************************************************/

MMBroadbandModemUblox *
mm_broadband_modem_ublox_new (const gchar  *device,
                              const gchar **drivers,
                              const gchar  *plugin,
                              guint16       vendor_id,
                              guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_UBLOX,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_ublox_init (MMBroadbandModemUblox *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_MODEM_UBLOX,
                                              MMBroadbandModemUbloxPrivate);
    self->priv->profile = MM_UBLOX_USB_PROFILE_UNKNOWN;
    self->priv->mode = MM_UBLOX_NETWORKING_MODE_UNKNOWN;
    self->priv->any_allowed = MM_MODEM_MODE_NONE;
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->create_bearer        = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->load_power_state        = load_power_state;
    iface->load_power_state_finish = load_power_state_finish;
    iface->modem_power_up        = modem_power_up;
    iface->modem_power_up_finish = common_modem_power_operation_finish;
    iface->modem_power_down        = modem_power_down;
    iface->modem_power_down_finish = common_modem_power_operation_finish;
    iface->modem_power_off        = modem_power_off;
    iface->modem_power_off_finish = common_modem_power_operation_finish;
    iface->reset        = modem_reset;
    iface->reset_finish = common_modem_power_operation_finish;
    iface->load_supported_modes        = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes        = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->set_current_modes        = set_current_modes;
    iface->set_current_modes_finish = set_current_modes_finish;
}

static void
mm_broadband_modem_ublox_class_init (MMBroadbandModemUbloxClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemUbloxPrivate));
}
