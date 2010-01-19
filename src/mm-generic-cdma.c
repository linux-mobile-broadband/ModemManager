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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include "mm-generic-cdma.h"
#include "mm-modem-cdma.h"
#include "mm-modem-simple.h"
#include "mm-serial-port.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-serial-parsers.h"

static void simple_reg_callback (MMModemCdma *modem,
                                 MMModemCdmaRegistrationState cdma_1x_reg_state,
                                 MMModemCdmaRegistrationState evdo_reg_state,
                                 GError *error,
                                 gpointer user_data);

static void simple_state_machine (MMModem *modem, GError *error, gpointer user_data);

static void update_enabled_state (MMGenericCdma *self,
                                  gboolean stay_connected,
                                  MMModemStateReason reason);

static void modem_init (MMModem *modem_class);
static void modem_cdma_init (MMModemCdma *cdma_class);
static void modem_simple_init (MMModemSimple *class);

G_DEFINE_TYPE_EXTENDED (MMGenericCdma, mm_generic_cdma, MM_TYPE_MODEM_BASE, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_CDMA, modem_cdma_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init))

#define MM_GENERIC_CDMA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_GENERIC_CDMA, MMGenericCdmaPrivate))

typedef struct {
    guint32 cdma1x_quality;
    guint32 evdo_quality;
    gboolean valid;
    gboolean evdo_rev0;
    gboolean evdo_revA;
    gboolean reg_try_css;

    MMModemCdmaRegistrationState cdma_1x_reg_state;
    MMModemCdmaRegistrationState evdo_reg_state;

    guint reg_tries;
    guint reg_retry_id;
    guint reg_state_changed_id;
    MMCallbackInfo *simple_connect_info;

    MMSerialPort *primary;
    MMSerialPort *secondary;
    MMPort *data;
} MMGenericCdmaPrivate;

enum {
    PROP_0,
    PROP_EVDO_REV0,
    PROP_EVDO_REVA,
    PROP_REG_TRY_CSS,
    LAST_PROP
};

MMModem *
mm_generic_cdma_new (const char *device,
                     const char *driver,
                     const char *plugin,
                     gboolean evdo_rev0,
                     gboolean evdo_revA)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_GENERIC_CDMA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_GENERIC_CDMA_EVDO_REV0, evdo_rev0,
                                   MM_GENERIC_CDMA_EVDO_REVA, evdo_revA,
                                   NULL));
}

/*****************************************************************************/

static void
check_valid (MMGenericCdma *self)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    gboolean new_valid = FALSE;

    if (priv->primary && priv->data)
        new_valid = TRUE;

    mm_modem_base_set_valid (MM_MODEM_BASE (self), new_valid);
}

static gboolean
owns_port (MMModem *modem, const char *subsys, const char *name)
{
    return !!mm_modem_base_get_port (MM_MODEM_BASE (modem), subsys, name);
}

MMPort *
mm_generic_cdma_grab_port (MMGenericCdma *self,
                           const char *subsys,
                           const char *name,
                           MMPortType suggested_type,
                           gpointer user_data,
                           GError **error)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    MMPortType ptype = MM_PORT_TYPE_IGNORED;
    MMPort *port;

    g_return_val_if_fail (!strcmp (subsys, "net") || !strcmp (subsys, "tty"), FALSE);
    if (priv->primary)
        g_return_val_if_fail (suggested_type != MM_PORT_TYPE_PRIMARY, FALSE);

    if (!strcmp (subsys, "tty")) {
        if (suggested_type != MM_PORT_TYPE_UNKNOWN)
            ptype = suggested_type;
        else {
            if (!priv->primary)
                ptype = MM_PORT_TYPE_PRIMARY;
            else if (!priv->secondary)
                ptype = MM_PORT_TYPE_SECONDARY;
        }
    }

    port = mm_modem_base_add_port (MM_MODEM_BASE (self), subsys, name, ptype);
    if (port && MM_IS_SERIAL_PORT (port)) {
        g_object_set (G_OBJECT (port), MM_PORT_CARRIER_DETECT, FALSE, NULL);
        mm_serial_port_set_response_parser (MM_SERIAL_PORT (port),
                                            mm_serial_parser_v1_parse,
                                            mm_serial_parser_v1_new (),
                                            mm_serial_parser_v1_destroy);

        if (ptype == MM_PORT_TYPE_PRIMARY) {
            priv->primary = MM_SERIAL_PORT (port);
            if (!priv->data) {
                priv->data = port;
                g_object_notify (G_OBJECT (self), MM_MODEM_DATA_DEVICE);
            }
            check_valid (self);
        } else if (ptype == MM_PORT_TYPE_SECONDARY)
            priv->secondary = MM_SERIAL_PORT (port);
    } else {
        /* Net device (if any) is the preferred data port */
        if (!priv->data || MM_IS_SERIAL_PORT (priv->data)) {
            priv->data = port;
            g_object_notify (G_OBJECT (self), MM_MODEM_DATA_DEVICE);
            check_valid (self);
        }
    }

    return port;
}

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           MMPortType suggested_type,
           gpointer user_data,
           GError **error)
{
    return !!mm_generic_cdma_grab_port (MM_GENERIC_CDMA (modem), subsys, name, suggested_type, user_data, error);
}

static void
release_port (MMModem *modem, const char *subsys, const char *name)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    MMPort *port;

    port = mm_modem_base_get_port (MM_MODEM_BASE (modem), subsys, name);
    if (!port)
        return;

    if (port == MM_PORT (priv->primary)) {
        mm_modem_base_remove_port (MM_MODEM_BASE (modem), port);
        priv->primary = NULL;
    }

    if (port == priv->data) {
        priv->data = NULL;
        g_object_notify (G_OBJECT (modem), MM_MODEM_DATA_DEVICE);
    }

    if (port == MM_PORT (priv->secondary)) {
        mm_modem_base_remove_port (MM_MODEM_BASE (modem), port);
        priv->secondary = NULL;
    }

    check_valid (MM_GENERIC_CDMA (modem));
}

MMSerialPort *
mm_generic_cdma_get_port (MMGenericCdma *modem,
                          MMPortType ptype)
{
    g_return_val_if_fail (MM_IS_GENERIC_CDMA (modem), NULL);
    g_return_val_if_fail (ptype != MM_PORT_TYPE_UNKNOWN, NULL);

    if (ptype == MM_PORT_TYPE_PRIMARY)
        return MM_GENERIC_CDMA_GET_PRIVATE (modem)->primary;
    else if (ptype == MM_PORT_TYPE_SECONDARY)
        return MM_GENERIC_CDMA_GET_PRIVATE (modem)->secondary;

    return NULL;
}

/*****************************************************************************/

void
mm_generic_cdma_set_1x_registration_state (MMGenericCdma *self,
                                           MMModemCdmaRegistrationState new_state)
{
    MMGenericCdmaPrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_GENERIC_CDMA (self));

    priv = MM_GENERIC_CDMA_GET_PRIVATE (self);

    if (priv->cdma_1x_reg_state != new_state) {
        priv->cdma_1x_reg_state = new_state;

        update_enabled_state (self, TRUE, MM_MODEM_STATE_REASON_NONE);
        mm_modem_cdma_emit_registration_state_changed (MM_MODEM_CDMA (self),
                                                       priv->cdma_1x_reg_state,
                                                       priv->evdo_reg_state);
    }
}

void
mm_generic_cdma_set_evdo_registration_state (MMGenericCdma *self,
                                             MMModemCdmaRegistrationState new_state)
{
    MMGenericCdmaPrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_GENERIC_CDMA (self));

    priv = MM_GENERIC_CDMA_GET_PRIVATE (self);

    if (priv->evdo_reg_state == new_state)
        return;

    /* Don't update EVDO state if the card doesn't support it */
    if (   priv->evdo_rev0
        || priv->evdo_revA
        || (new_state == MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)) {
        priv->evdo_reg_state = new_state;

        update_enabled_state (self, TRUE, MM_MODEM_STATE_REASON_NONE);
        mm_modem_cdma_emit_registration_state_changed (MM_MODEM_CDMA (self),
                                                       priv->cdma_1x_reg_state,
                                                       priv->evdo_reg_state);
    }
}

MMModemCdmaRegistrationState
mm_generic_cdma_1x_get_registration_state_sync (MMGenericCdma *self)
{
    g_return_val_if_fail (self != NULL, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
    g_return_val_if_fail (MM_IS_GENERIC_CDMA (self), MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

    return MM_GENERIC_CDMA_GET_PRIVATE (self)->cdma_1x_reg_state;
}

MMModemCdmaRegistrationState
mm_generic_cdma_evdo_get_registration_state_sync (MMGenericCdma *self)
{
    g_return_val_if_fail (self != NULL, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
    g_return_val_if_fail (MM_IS_GENERIC_CDMA (self), MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

    return MM_GENERIC_CDMA_GET_PRIVATE (self)->evdo_reg_state;
}

/*****************************************************************************/

static void
update_enabled_state (MMGenericCdma *self,
                      gboolean stay_connected,
                      MMModemStateReason reason)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);

    /* While connected we don't want registration status changes to change
     * the modem's state away from CONNECTED.
     */
    if (stay_connected && (mm_modem_get_state (MM_MODEM (self)) >= MM_MODEM_STATE_DISCONNECTING))
        return;

    if (   priv->cdma_1x_reg_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN
        || priv->evdo_reg_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        mm_modem_set_state (MM_MODEM (self), MM_MODEM_STATE_REGISTERED, reason);
    else
        mm_modem_set_state (MM_MODEM (self), MM_MODEM_STATE_ENABLED, reason);
}

static void
registration_cleanup (MMGenericCdma *self, GQuark error_class, guint32 error_num)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    GError *error = NULL;

    priv->reg_tries = 0;

    if (priv->reg_state_changed_id) {
        g_signal_handler_disconnect (self, priv->reg_state_changed_id);
        priv->reg_state_changed_id = 0;
    }

    if (priv->reg_retry_id) {
        g_source_remove (priv->reg_retry_id);
        priv->reg_retry_id = 0;
    }

    /* Return an error to any explicit callers of simple_connect */
    if (priv->simple_connect_info && error_class) {
        error = g_error_new_literal (error_class, error_num,
                                     "Connection attempt terminated");
        simple_state_machine (MM_MODEM (self), error, priv->simple_connect_info);
        g_error_free (error);
    }
    priv->simple_connect_info = NULL;
}

static void
enable_all_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericCdma *self = MM_GENERIC_CDMA (info->modem);
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);

    if (error)
        info->error = g_error_copy (error);
    else {
        /* Open up the second port, if one exists */
        if (priv->secondary) {
            if (!mm_serial_port_open (priv->secondary, &info->error)) {
                g_assert (info->error);
                goto out;
            }
        }

        update_enabled_state (self, FALSE, MM_MODEM_STATE_REASON_NONE);
    }

out:
    if (info->error) {
        mm_modem_set_state (MM_MODEM (info->modem),
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);
    }

    mm_callback_info_schedule (info);
}

static void
enable_error_reporting_done (MMSerialPort *port,
                             GString *response,
                             GError *error,
                             gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericCdma *self = MM_GENERIC_CDMA (info->modem);

    /* Just ignore errors, see comment in init_done() */
    if (error)
        g_warning ("Your CDMA modem does not support +CMEE command");

    if (MM_GENERIC_CDMA_GET_CLASS (self)->post_enable)
        MM_GENERIC_CDMA_GET_CLASS (self)->post_enable (self, enable_all_done, info);
    else
        enable_all_done (MM_MODEM (self), NULL, info);
}

static void
init_done (MMSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        mm_modem_set_state (MM_MODEM (info->modem),
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);

        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        /* Try to enable better error reporting. My experience so far indicates
           there's some CDMA modems that does not support that.
        FIXME: It's mandatory by spec, so it really shouldn't be optional. Figure
        out which CDMA modems have problems with it and implement plugin for them.
        */
        mm_serial_port_queue_command (port, "+CMEE=1", 3, enable_error_reporting_done, user_data);
    }
}

static void
flash_done (MMSerialPort *port, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        mm_modem_set_state (MM_MODEM (info->modem),
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);

        /* Flash failed for some reason */
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        return;
    }

    mm_serial_port_queue_command (port, "Z E0 V1 X4 &C1", 3, init_done, user_data);
}

static void
enable (MMModem *modem,
        MMModemFn callback,
        gpointer user_data)
{
    MMGenericCdma *self = MM_GENERIC_CDMA (modem);
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    MMCallbackInfo *info;

    info = mm_callback_info_new (modem, callback, user_data);

    if (!mm_serial_port_open (priv->primary, &info->error)) {
        g_assert (info->error);
        mm_callback_info_schedule (info);
        return;
    }

    mm_modem_set_state (MM_MODEM (info->modem),
                        MM_MODEM_STATE_ENABLING,
                        MM_MODEM_STATE_REASON_NONE);

    mm_serial_port_flash (priv->primary, 100, flash_done, info);
}

static void
disable_set_previous_state (MMModem *modem, MMCallbackInfo *info)
{
    MMModemState prev_state;

    /* Reset old state since the operation failed */
    prev_state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, MM_GENERIC_CDMA_PREV_STATE_TAG));
    mm_modem_set_state (modem, prev_state, MM_MODEM_STATE_REASON_NONE);
}

static void
disable_all_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (modem, error);
    if (info->error) {
        if (modem)
            disable_set_previous_state (modem, info);
    } else {
        MMGenericCdma *self = MM_GENERIC_CDMA (info->modem);
        MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);

        mm_serial_port_close (priv->primary);
        mm_modem_set_state (modem, MM_MODEM_STATE_DISABLED, MM_MODEM_STATE_REASON_NONE);

        priv->cdma_1x_reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        priv->evdo_reg_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    }

    mm_callback_info_schedule (info);
}

static void
disable_flash_done (MMSerialPort *port,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericCdma *self;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        if (info->modem)
            disable_set_previous_state (info->modem, info);
        mm_callback_info_schedule (info);
        return;
    }

    self = MM_GENERIC_CDMA (info->modem);

    if (MM_GENERIC_CDMA_GET_CLASS (self)->post_disable)
        MM_GENERIC_CDMA_GET_CLASS (self)->post_disable (self, disable_all_done, info);
    else
        disable_all_done (MM_MODEM (self), NULL, info);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMGenericCdma *self = MM_GENERIC_CDMA (modem);
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    MMCallbackInfo *info;
    MMModemState state;

    /* Tear down any ongoing registration */
    registration_cleanup (self, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL);

    info = mm_callback_info_new (modem, callback, user_data);

    /* Cache the previous state so we can reset it if the operation fails */
    state = mm_modem_get_state (modem);
    mm_callback_info_set_data (info,
                               MM_GENERIC_CDMA_PREV_STATE_TAG,
                               GUINT_TO_POINTER (state),
                               NULL);

    if (priv->secondary)
        mm_serial_port_close (priv->secondary);

    mm_modem_set_state (MM_MODEM (info->modem),
                        MM_MODEM_STATE_DISABLING,
                        MM_MODEM_STATE_REASON_NONE);

    if (mm_port_get_connected (MM_PORT (priv->primary)))
        mm_serial_port_flash (priv->primary, 1000, disable_flash_done, info);
    else
        disable_flash_done (priv->primary, NULL, info);
}

static void
dial_done (MMSerialPort *port,
           GString *response,
           GError *error,
           gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        if (info->modem)
            update_enabled_state (MM_GENERIC_CDMA (info->modem), FALSE, MM_MODEM_STATE_REASON_NONE);
    } else {
        MMGenericCdma *self = MM_GENERIC_CDMA (info->modem);
        MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);

        /* Clear reg tries; we're obviously registered by this point */
        registration_cleanup (self, 0, 0);

        mm_port_set_connected (priv->data, TRUE);
        mm_modem_set_state (info->modem, MM_MODEM_STATE_CONNECTED, MM_MODEM_STATE_REASON_NONE);
    }

    mm_callback_info_schedule (info);
}

static void
connect (MMModem *modem,
         const char *number,
         MMModemFn callback,
         gpointer user_data)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    char *command;

    mm_modem_set_state (modem, MM_MODEM_STATE_CONNECTING, MM_MODEM_STATE_REASON_NONE);

    info = mm_callback_info_new (modem, callback, user_data);
    command = g_strconcat ("DT", number, NULL);
    mm_serial_port_queue_command (priv->primary, command, 90, dial_done, info);
    g_free (command);
}

static void
disconnect_flash_done (MMSerialPort *port,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemState prev_state;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        if (info->modem) {
            /* Reset old state since the operation failed */
            prev_state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, MM_GENERIC_CDMA_PREV_STATE_TAG));
            mm_modem_set_state (MM_MODEM (info->modem),
                                prev_state,
                                MM_MODEM_STATE_REASON_NONE);
        }
    } else {
        mm_port_set_connected (MM_GENERIC_CDMA_GET_PRIVATE (info->modem)->data, FALSE);
        update_enabled_state (MM_GENERIC_CDMA (info->modem), FALSE, MM_MODEM_STATE_REASON_NONE);
    }

    mm_callback_info_schedule (info);
}

static void
disconnect (MMModem *modem,
            MMModemFn callback,
            gpointer user_data)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMModemState state;

    g_return_if_fail (priv->primary != NULL);

    info = mm_callback_info_new (modem, callback, user_data);

    /* Cache the previous state so we can reset it if the operation fails */
    state = mm_modem_get_state (modem);
    mm_callback_info_set_data (info,
                               MM_GENERIC_CDMA_PREV_STATE_TAG,
                               GUINT_TO_POINTER (state),
                               NULL);

    mm_modem_set_state (modem, MM_MODEM_STATE_DISCONNECTING, MM_MODEM_STATE_REASON_NONE);
    mm_serial_port_flash (priv->primary, 1000, disconnect_flash_done, info);
}

static void
card_info_invoke (MMCallbackInfo *info)
{
    MMModemInfoFn callback = (MMModemInfoFn) info->callback;

    callback (info->modem,
              (char *) mm_callback_info_get_data (info, "card-info-manufacturer"),
              (char *) mm_callback_info_get_data (info, "card-info-model"),
              (char *) mm_callback_info_get_data (info, "card-info-version"),
              info->error, info->user_data);
}

static const char *
strip_response (const char *resp, const char *cmd)
{
    const char *p = resp;

    if (p) {
        if (!strncmp (p, cmd, strlen (cmd)))
            p += strlen (cmd);
        while (*p == ' ')
            p++;
    }
    return p;
}

static void
get_version_done (MMSerialPort *port,
                  GString *response,
                  GError *error,
                  gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *p;

    if (!error) {
        p = strip_response (response->str, "+GMR:");
        mm_callback_info_set_data (info, "card-info-version", g_strdup (p), g_free);
    } else if (!info->error)
        info->error = g_error_copy (error);

    mm_callback_info_schedule (info);
}

static void
get_model_done (MMSerialPort *port,
                GString *response,
                GError *error,
                gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *p;

    if (!error) {
        p = strip_response (response->str, "+GMM:");
        mm_callback_info_set_data (info, "card-info-model", g_strdup (p), g_free);
    } else if (!info->error)
        info->error = g_error_copy (error);
}

static void
get_manufacturer_done (MMSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *p;

    if (!error) {
        p = strip_response (response->str, "+GMI:");
        mm_callback_info_set_data (info, "card-info-manufacturer", g_strdup (p), g_free);
    } else
        info->error = g_error_copy (error);
}

static void
get_card_info (MMModem *modem,
               MMModemInfoFn callback,
               gpointer user_data)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMSerialPort *port = priv->primary;

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      card_info_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    if (mm_port_get_connected (MM_PORT (priv->primary))) {
        if (!priv->secondary) {
            info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                                               "Cannot modem info while connected");
            mm_callback_info_schedule (info);
            return;
        }

        /* Use secondary port if primary is connected */
        port = priv->secondary;
    }

    mm_serial_port_queue_command_cached (port, "+GMI", 3, get_manufacturer_done, info);
    mm_serial_port_queue_command_cached (port, "+GMM", 3, get_model_done, info);
    mm_serial_port_queue_command_cached (port, "+GMR", 3, get_version_done, info);
}

/*****************************************************************************/

void
mm_generic_cdma_update_cdma1x_quality (MMGenericCdma *self, guint32 quality)
{
    MMGenericCdmaPrivate *priv;

    g_return_if_fail (MM_IS_GENERIC_CDMA (self));
    g_return_if_fail (quality >= 0 && quality <= 100);

    priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    if (priv->cdma1x_quality != quality) {
        priv->cdma1x_quality = quality;
        mm_modem_cdma_emit_signal_quality_changed (MM_MODEM_CDMA (self), quality);
    }
}

void
mm_generic_cdma_update_evdo_quality (MMGenericCdma *self, guint32 quality)
{
    MMGenericCdmaPrivate *priv;

    g_return_if_fail (MM_IS_GENERIC_CDMA (self));
    g_return_if_fail (quality >= 0 && quality <= 100);

    priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    if (priv->evdo_quality != quality) {
        priv->evdo_quality = quality;
        // FIXME: emit a signal
    }
}

#define CSQ2_TRIED "csq?-tried"

static void
get_signal_quality_done (MMSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMGenericCdmaPrivate *priv;
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *reply = response->str;

    if (error) {
        if (mm_callback_info_get_data (info, CSQ2_TRIED))
            info->error = g_error_copy (error);
        else {
            /* Some modems want +CSQ, others want +CSQ?, and some of both types
             * will return ERROR if they don't get the command they want.  So
             * try the other command if the first one fails.
             */
            mm_callback_info_set_data (info, CSQ2_TRIED, GUINT_TO_POINTER (1), NULL);
            mm_serial_port_queue_command (port, "+CSQ?", 3, get_signal_quality_done, info);
            return;
        }
    } else {
        int quality, ber;

        /* Got valid reply */
        if (!strncmp (reply, "+CSQ: ", 6))
            reply += 6;

        if (sscanf (reply, "%d, %d", &quality, &ber)) {
            /* 99 means unknown/no service */
            if (quality == 99) {
                info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                                   MM_MOBILE_ERROR_NO_NETWORK,
                                                   "No service");
            } else {
                /* Normalize the quality */
                quality = CLAMP (quality, 0, 31) * 100 / 31;

                priv = MM_GENERIC_CDMA_GET_PRIVATE (info->modem);
                mm_callback_info_set_result (info, GUINT_TO_POINTER (quality), NULL);
                if (priv->cdma1x_quality != quality) {
                    priv->cdma1x_quality = quality;
                    mm_modem_cdma_emit_signal_quality_changed (MM_MODEM_CDMA (info->modem), quality);
                }
            }
        } else
            info->error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                       "%s", "Could not parse signal quality results");
    }

    mm_callback_info_schedule (info);
}

static void
get_signal_quality (MMModemCdma *modem,
                    MMModemUIntFn callback,
                    gpointer user_data)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMSerialPort *port = priv->primary;

    if (mm_port_get_connected (MM_PORT (priv->primary))) {
        if (!priv->secondary) {
            g_message ("Returning saved signal quality %d", priv->cdma1x_quality);
            callback (MM_MODEM (modem), priv->cdma1x_quality, NULL, user_data);
            return;
        }

        /* Use secondary port if primary is connected */
        port = priv->secondary;
    }

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_queue_command (port, "+CSQ", 3, get_signal_quality_done, info);
}

static void
get_string_done (MMSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *p;

    if (error)
        info->error = g_error_copy (error);
    else {
        p = strip_response (response->str, "+GSN:");
        mm_callback_info_set_result (info, g_strdup (p), g_free);
    }

    mm_callback_info_schedule (info);
}

static void
get_esn (MMModemCdma *modem,
         MMModemStringFn callback,
         gpointer user_data)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    GError *error;
    MMSerialPort *port = priv->primary;

    if (mm_port_get_connected (MM_PORT (priv->primary))) {
        if (!priv->secondary) {
            error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                                         "Cannot get ESN while connected");
            callback (MM_MODEM (modem), NULL, error, user_data);
            g_error_free (error);
            return;
        }

        /* Use secondary port if primary is connected */
        port = priv->secondary;
    }

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_queue_command_cached (port, "+GSN", 3, get_string_done, info);
}

static void
serving_system_invoke (MMCallbackInfo *info)
{
    MMModemCdmaServingSystemFn callback = (MMModemCdmaServingSystemFn) info->callback;

    callback (MM_MODEM_CDMA (info->modem),
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, "class")),
              (unsigned char) GPOINTER_TO_UINT (mm_callback_info_get_data (info, "band")),
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, "sid")),
              info->error,
              info->user_data);
}

static int
normalize_class (const char *orig_class)
{
    char class;

    g_return_val_if_fail (orig_class != NULL, '0');

    class = toupper (orig_class[0]);

    /* Cellular (850MHz) */
    if (class == '1' || class == 'C')
        return 1;
    /* PCS (1900MHz) */
    if (class == '2' || class == 'P')
        return 2;

    /* Unknown/not registered */
    return 0;
}

static char
normalize_band (const char *long_band, int *out_class)
{
    char band;

    g_return_val_if_fail (long_band != NULL, 'Z');

    /* There are two response formats for the band; one includes the band
     * class and the other doesn't.  For modems that include the band class
     * (ex Novatel S720) you'll see "Px" or "Cx" depending on whether the modem
     * is registered on a PCS/1900 (P) or Cellular/850 (C) system.
     */
    band = toupper (long_band[0]);

    /* Possible band class in first position; return it */
    if (band == 'C' || band == 'P') {
        char tmp[2] = { band, '\0' };

        *out_class = normalize_class (tmp);
        band = toupper (long_band[1]);
    }

    /* normalize to A - F, and Z */
    if (band >= 'A' && band <= 'F')
        return band;

    /* Unknown/not registered */
    return 'Z';
}

static int
convert_sid (const char *sid)
{
    long int tmp_sid;

    g_return_val_if_fail (sid != NULL, 99999);

    errno = 0;
    tmp_sid = strtol (sid, NULL, 10);
    if ((errno == EINVAL) || (errno == ERANGE))
        return 99999;
    else if (tmp_sid < G_MININT || tmp_sid > G_MAXINT)
        return 99999;

    return (int) tmp_sid;
}

static void
serving_system_done (MMSerialPort *port,
                     GString *response,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    char *reply = response->str;
    int class = 0, sid = 99999, num;
    unsigned char band = 'Z';
    gboolean success = FALSE;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    }

    if (strstr (reply, "+CSS: "))
        reply += 6;

    num = sscanf (reply, "? , %d", &sid);
    if (num == 1) {
        /* UTStarcom and Huawei modems that use IS-707-A format; note that
         * this format obviously doesn't have other indicators like band and
         * class and thus SID 0 will be reported as "no service" (see below).
         */
        class = 0;
        band = 'Z';
        success = TRUE;
    } else {
        GRegex *r;
        GMatchInfo *match_info;
        int override_class = 0;

        /* Format is "<band_class>,<band>,<sid>" */
        r = g_regex_new ("\\s*([^,]*?)\\s*,\\s*([^,]*?)\\s*,\\s*(\\d+)", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        if (!r) {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_GENERAL,
                                               "Could not parse Serving System results (regex creation failed).");
            goto out;
        }

        g_regex_match (r, reply, 0, &match_info);
        if (g_match_info_get_match_count (match_info) >= 3) {
            char *str;

            /* band class */
            str = g_match_info_fetch (match_info, 1);
            class = normalize_class (str);
            g_free (str);

            /* band */
            str = g_match_info_fetch (match_info, 2);
            band = normalize_band (str, &override_class);
            if (override_class)
                class = override_class;
            g_free (str);

            /* sid */
            str = g_match_info_fetch (match_info, 3);
            sid = convert_sid (str);
            g_free (str);

            success = TRUE;
        }

        g_match_info_free (match_info);
        g_regex_unref (r);
    }

    if (success) {
        gboolean class_ok = FALSE, band_ok = FALSE;

        /* Normalize the SID */
        if (sid < 0 || sid > 32767)
            sid = 99999;

        if (class == 1 || class == 2)
            class_ok = TRUE;
        if (band != 'Z')
            band_ok = TRUE;

        /* Return 'no service' if none of the elements of the +CSS response
         * indicate that the modem has service.  Note that this allows SID 0
         * when at least one of the other elements indicates service.
         * Normally we'd treat SID 0 as 'no service' but some modems
         * (Sierra 5725) sometimes return SID 0 even when registered.
         */
        if (sid == 0 && !class_ok && !band_ok)
            sid = 99999;

        /* 99999 means unknown/no service */
        if (sid == 99999) {
            /* NOTE: update reg_state_css_response() if this error changes */
            info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                               MM_MOBILE_ERROR_NO_NETWORK,
                                               "No service");
        } else {
            mm_callback_info_set_data (info, "class", GUINT_TO_POINTER (class), NULL);
            mm_callback_info_set_data (info, "band", GUINT_TO_POINTER ((guint32) band), NULL);
            mm_callback_info_set_data (info, "sid", GUINT_TO_POINTER (sid), NULL);
        }
    } else {
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse Serving System results.");
    }

 out:
    mm_callback_info_schedule (info);
}

static void
get_serving_system (MMModemCdma *modem,
                    MMModemCdmaServingSystemFn callback,
                    gpointer user_data)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    GError *error;
    MMSerialPort *port = priv->primary;

    if (mm_port_get_connected (MM_PORT (priv->primary))) {
        if (!priv->secondary) {
            error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_CONNECTED,
                                         "Cannot get serving system while connected");
            callback (modem, 0, 0, 0, error, user_data);
            g_error_free (error);
            return;
        }

        /* Use secondary port if primary is connected */
        port = priv->secondary;
    }

    info = mm_callback_info_new_full (MM_MODEM (modem),
                                      serving_system_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    mm_serial_port_queue_command (port, "+CSS?", 3, serving_system_done, info);
}

#define CDMA_1X_STATE_TAG     "cdma-1x-reg-state"
#define EVDO_STATE_TAG        "evdo-reg-state"

void
mm_generic_cdma_query_reg_state_set_callback_1x_state (MMCallbackInfo *info,
                                                       MMModemCdmaRegistrationState new_state)
{
    g_return_if_fail (info != NULL);
    g_return_if_fail (info->modem != NULL);
    g_return_if_fail (MM_IS_GENERIC_CDMA (info->modem));

    mm_callback_info_set_data (info, CDMA_1X_STATE_TAG, GUINT_TO_POINTER (new_state), NULL);
}

void
mm_generic_cdma_query_reg_state_set_callback_evdo_state (MMCallbackInfo *info,
                                                         MMModemCdmaRegistrationState new_state)
{
    g_return_if_fail (info != NULL);
    g_return_if_fail (info->modem != NULL);
    g_return_if_fail (MM_IS_GENERIC_CDMA (info->modem));

    mm_callback_info_set_data (info, EVDO_STATE_TAG, GUINT_TO_POINTER (new_state), NULL);
}

static void
registration_state_invoke (MMCallbackInfo *info)
{
    MMModemCdmaRegistrationStateFn callback = (MMModemCdmaRegistrationStateFn) info->callback;

    /* note: This is the MMModemCdma interface callback */
    callback (MM_MODEM_CDMA (info->modem),
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, CDMA_1X_STATE_TAG)),
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, EVDO_STATE_TAG)),
              info->error,
              info->user_data);
}

MMCallbackInfo *
mm_generic_cdma_query_reg_state_callback_info_new (MMGenericCdma *self,
                                                   MMModemCdmaRegistrationStateFn callback,
                                                   gpointer user_data)
{
    MMGenericCdmaPrivate *priv;
    MMCallbackInfo *info;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_GENERIC_CDMA (self), NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    info = mm_callback_info_new_full (MM_MODEM (self),
                                      registration_state_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    /* Fill with current state */
    priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    mm_callback_info_set_data (info,
                               CDMA_1X_STATE_TAG,
                               GUINT_TO_POINTER (priv->cdma_1x_reg_state),
                               NULL);
    mm_callback_info_set_data (info,
                               EVDO_STATE_TAG,
                               GUINT_TO_POINTER (priv->evdo_reg_state),
                               NULL);
    return info;
}

static void
set_callback_1x_state_helper (MMCallbackInfo *info,
                              MMModemCdmaRegistrationState new_state)
{
    MMGenericCdma *self = MM_GENERIC_CDMA (info->modem);
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (info->modem);

    mm_generic_cdma_set_1x_registration_state (self, new_state);
    mm_generic_cdma_query_reg_state_set_callback_1x_state (info, priv->cdma_1x_reg_state);
}

static void
set_callback_evdo_state_helper (MMCallbackInfo *info,
                                MMModemCdmaRegistrationState new_state)
{
    MMGenericCdma *self = MM_GENERIC_CDMA (info->modem);
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (info->modem);

    mm_generic_cdma_set_evdo_registration_state (self, new_state);
    mm_generic_cdma_query_reg_state_set_callback_evdo_state (info, priv->evdo_reg_state);
}

static void
reg_state_query_done (MMModemCdma *cdma,
                      MMModemCdmaRegistrationState cdma_1x_reg_state,
                      MMModemCdmaRegistrationState evdo_reg_state,
                      GError *error,
                      gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    else {
        set_callback_1x_state_helper (info, cdma_1x_reg_state);
        set_callback_evdo_state_helper (info, evdo_reg_state);
    }

    mm_callback_info_schedule (info);
}

static void
query_subclass_registration_state (MMGenericCdma *self, MMCallbackInfo *info)
{
    /* Let subclasses figure out roaming and detailed registration state */
    if (MM_GENERIC_CDMA_GET_CLASS (self)->query_registration_state) {
        MM_GENERIC_CDMA_GET_CLASS (self)->query_registration_state (self,
                                                                    reg_state_query_done,
                                                                    info);
    } else {
        /* Or if the subclass doesn't implement more specific checking,
         * assume we're registered.
         */
        reg_state_query_done (MM_MODEM_CDMA (self),
                              MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED,
                              MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED,
                              NULL,
                              info);
    }
}

static void
reg_state_css_response (MMModemCdma *cdma,
                        guint32 class,
                        unsigned char band,
                        guint32 sid,
                        GError *error,
                        gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* We'll get an error if the SID isn't valid, so detect that and
     * report unknown registration state.
     */
    if (error) {
        if (   (error->domain == MM_MOBILE_ERROR)
            && (error->code == MM_MOBILE_ERROR_NO_NETWORK)) {
            set_callback_1x_state_helper (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
            set_callback_evdo_state_helper (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        } else {
            /* Some other error parsing CSS results */
            info->error = g_error_copy (error);
        }
        mm_callback_info_schedule (info);
        return;
    }

    query_subclass_registration_state (MM_GENERIC_CDMA (info->modem), info);
}

static void
get_analog_digital_done (MMSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *reply;
    long int int_cad;

    if (error) {
        info->error = g_error_copy (error);
        goto error;
    }

    /* Strip any leading command tag and spaces */
    reply = strip_response (response->str, "+CAD:");

    errno = 0;
    int_cad = strtol (reply, NULL, 10);
    if ((errno == EINVAL) || (errno == ERANGE)) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse +CAD response");
        goto error;
    }

    if (int_cad == 1) {  /* 1 == CDMA service */
        MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (info->modem);

        /* Now that we have some sort of service, check if the the device is
         * registered on the network.
         */

        /* Some devices key the AT+CSS? response off the 1X state, but if the
         * device has EVDO service but no 1X service, then reading AT+CSS? will
         * error out too early.  Let subclasses that know that their AT+CSS?
         * response is wrong in this case handle more specific registration
         * themselves; if they do, they'll set priv->reg_try_css to FALSE.
         */
        if (priv->reg_try_css) {
            get_serving_system (MM_MODEM_CDMA (info->modem),
                                reg_state_css_response,
                                info);
        } else {
            /* Subclass knows that AT+CSS? will respond incorrectly to EVDO
             * state, so skip AT+CSS? query.
             */
            query_subclass_registration_state (MM_GENERIC_CDMA (info->modem), info);
        }
        return;
    } else {
        /* No service */
        set_callback_1x_state_helper (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        set_callback_evdo_state_helper (info, MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
    }

error:
    mm_callback_info_schedule (info);
}

static void
get_registration_state (MMModemCdma *modem,
                        MMModemCdmaRegistrationStateFn callback,
                        gpointer user_data)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMSerialPort *port = priv->primary;

    if (mm_port_get_connected (MM_PORT (priv->primary))) {
        if (!priv->secondary) {
            g_message ("Returning saved registration states: 1x: %d  EVDO: %d",
                       priv->cdma_1x_reg_state, priv->evdo_reg_state);
            callback (MM_MODEM_CDMA (modem), priv->cdma_1x_reg_state, priv->evdo_reg_state, NULL, user_data);
            return;
        }

        /* Use secondary port if primary is connected */
        port = priv->secondary;
    }

    info = mm_generic_cdma_query_reg_state_callback_info_new (MM_GENERIC_CDMA (modem), callback, user_data);
    mm_serial_port_queue_command (port, "+CAD?", 3, get_analog_digital_done, info);
}

/*****************************************************************************/
/* MMModemSimple interface */

typedef enum {
    SIMPLE_STATE_BEGIN = 0,
    SIMPLE_STATE_ENABLE,
    SIMPLE_STATE_REGISTER,
    SIMPLE_STATE_CONNECT,
    SIMPLE_STATE_DONE
} SimpleState;

static const char *
simple_get_string_property (MMCallbackInfo *info, const char *name, GError **error)
{
    GHashTable *properties = (GHashTable *) mm_callback_info_get_data (info, "simple-connect-properties");
    GValue *value;

    value = (GValue *) g_hash_table_lookup (properties, name);
    if (!value)
        return NULL;

    if (G_VALUE_HOLDS_STRING (value))
        return g_value_get_string (value);

    g_set_error (error, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                 "Invalid property type for '%s': %s (string expected)",
                 name, G_VALUE_TYPE_NAME (value));

    return NULL;
}

static gboolean
simple_reg_retry (gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    mm_modem_cdma_get_registration_state (MM_MODEM_CDMA (info->modem),
                                          simple_reg_callback,
                                          info);
    return TRUE;
}

static void
simple_reg_callback (MMModemCdma *modem,
                     MMModemCdmaRegistrationState cdma_1x_reg_state,
                     MMModemCdmaRegistrationState evdo_reg_state,
                     GError *error,
                     gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (modem);
    gboolean no_service_error = FALSE;

    if (   error
        && (error->domain == MM_MOBILE_ERROR)
        && (error->code == MM_MOBILE_ERROR_NO_NETWORK))
        no_service_error = TRUE;

    /* Fail immediately on anything but "no service" */
    if (error && !no_service_error) {
        simple_state_machine (MM_MODEM (modem), error, info);
        return;
    }

    if (   no_service_error
        || (   (cdma_1x_reg_state == MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
            && (evdo_reg_state == MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN))) {
        /* Not registered yet, queue up a retry */
        priv->reg_tries++;
        if (priv->reg_tries > 15) {
            error = g_error_new_literal (MM_MOBILE_ERROR,
                                         MM_MOBILE_ERROR_NO_NETWORK,
                                         "No service");
            simple_state_machine (MM_MODEM (modem), error, info);
            g_error_free (error);
            return;
        }

        /* otherwise, just try again in a bit */
        if (!priv->reg_retry_id)
            priv->reg_retry_id = g_timeout_add_seconds (4, simple_reg_retry, info);
    } else {
        /* Yay, at least one of 1x or EVDO is registered, we can proceed to dial */
        simple_state_machine (MM_MODEM (modem), NULL, info);
    }
}

static void
reg_state_changed (MMModemCdma *self,
                   MMModemCdmaRegistrationState cdma_1x_new_state,
                   MMModemCdmaRegistrationState evdo_new_state,
                   gpointer user_data)
{
/* Disabled for now...  changing the registration state from the 
 * subclass' query_registration_state handler also emits the registration
 * state changed signal, which will call this function, and execute
 * simple_state_machine() to advance to the next state.  Then however
 * query_registration_state will call its callback, which ends up in
 * simple_reg_callback(), which calls simple_state_machine() too in
 * the same mainloop iteration.  Not good.  So until that's sorted out
 * we'll just have to poll registration state (every 4 seconds so its
 * not that bad.
 */
#if 0
    MMCallbackInfo *info = user_data;

    /* If we're registered, we can proceed */
    if (   (cdma_1x_reg_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        || (evdo_reg_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN))
        simple_state_machine (MM_MODEM (modem), NULL, info);
#endif
}

static SimpleState
set_simple_state (MMCallbackInfo *info, SimpleState state)
{
    mm_callback_info_set_data (info, "simple-connect-state", GUINT_TO_POINTER (state), NULL);
    return state;
}

static void
simple_state_machine (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (info->modem);
    SimpleState state = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "simple-connect-state"));
    const char *str;
    guint id;

    if (error) {
        info->error = g_error_copy (error);
        goto out;
    }

    switch (state) {
    case SIMPLE_STATE_BEGIN:
        state = set_simple_state (info, SIMPLE_STATE_ENABLE);
        mm_modem_enable (modem, simple_state_machine, info);
        break;
    case SIMPLE_STATE_ENABLE:
        state = set_simple_state (info, SIMPLE_STATE_REGISTER);
        mm_modem_cdma_get_registration_state (MM_MODEM_CDMA (modem),
                                              simple_reg_callback,
                                              info);
        id = g_signal_connect (modem,
                               MM_MODEM_CDMA_REGISTRATION_STATE_CHANGED,
                               G_CALLBACK (reg_state_changed),
                               info);
        priv->reg_state_changed_id = id;
        break;
    case SIMPLE_STATE_REGISTER:
        registration_cleanup (MM_GENERIC_CDMA (modem), 0, 0);
        state = set_simple_state (info, SIMPLE_STATE_CONNECT);
        mm_modem_set_state (modem, MM_MODEM_STATE_REGISTERED, MM_MODEM_STATE_REASON_NONE);

        str = simple_get_string_property (info, "number", &info->error);
        mm_modem_connect (modem, str, simple_state_machine, info);
        break;
    case SIMPLE_STATE_CONNECT:
        state = set_simple_state (info, SIMPLE_STATE_DONE);
        break;
    case SIMPLE_STATE_DONE:
        break;
    }

 out:
    if (info->error || state == SIMPLE_STATE_DONE) {
        registration_cleanup (MM_GENERIC_CDMA (modem), 0, 0);
        mm_callback_info_schedule (info);
    }
}

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMGenericCdma *self = MM_GENERIC_CDMA (simple);
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (self);
    MMCallbackInfo *info;
    GError *error = NULL;

    if (priv->simple_connect_info) {
        error = g_error_new_literal (MM_MODEM_ERROR,
                                     MM_MODEM_ERROR_OPERATION_IN_PROGRESS,
                                     "Connection is already in progress");
        callback (MM_MODEM (simple), error, user_data);
        g_error_free (error);
        return;
    }

    info = mm_callback_info_new (MM_MODEM (simple), callback, user_data);
    priv->simple_connect_info = info;
    mm_callback_info_set_data (info, "simple-connect-properties", 
                               g_hash_table_ref (properties),
                               (GDestroyNotify) g_hash_table_unref);

    /* At least number must be present */
    if (!simple_get_string_property (info, "number", &error)) {
        if (!error)
            error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL, "Missing number property");
    }

    simple_state_machine (MM_MODEM (simple), error, info);
}

static void
simple_free_gvalue (gpointer data)
{
    g_value_unset ((GValue *) data);
    g_slice_free (GValue, data);
}

static GValue *
simple_uint_value (guint32 i)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_UINT);
    g_value_set_uint (val, i);

    return val;
}

static void
simple_status_got_signal_quality (MMModem *modem,
                                  guint32 result,
                                  GError *error,
                                  gpointer user_data)
{
    if (error)
        g_warning ("Error getting signal quality: %s", error->message);
    else
        g_hash_table_insert ((GHashTable *) user_data, "signal_quality", simple_uint_value (result));
}

static void
simple_get_status_invoke (MMCallbackInfo *info)
{
    MMModemSimpleGetStatusFn callback = (MMModemSimpleGetStatusFn) info->callback;

    callback (MM_MODEM_SIMPLE (info->modem),
              (GHashTable *) mm_callback_info_get_data (info, "simple-get-status"),
              info->error, info->user_data);
}

static void
simple_get_status (MMModemSimple *simple,
                   MMModemSimpleGetStatusFn callback,
                   gpointer user_data)
{
    GHashTable *properties;
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (simple),
                                      simple_get_status_invoke,
                                      G_CALLBACK (callback),
                                      user_data);

    properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, simple_free_gvalue);
    mm_callback_info_set_data (info, "simple-get-status", properties, (GDestroyNotify) g_hash_table_unref);
    mm_modem_cdma_get_signal_quality (MM_MODEM_CDMA (simple), simple_status_got_signal_quality, properties);
}

/*****************************************************************************/

static void
modem_valid_changed (MMGenericCdma *self, GParamSpec *pspec, gpointer user_data)
{
    /* Be paranoid about tearing down any pending registration */
    if (!mm_modem_get_valid (MM_MODEM (self)))
        registration_cleanup (self, MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->owns_port = owns_port;
    modem_class->grab_port = grab_port;
    modem_class->release_port = release_port;
    modem_class->enable = enable;
    modem_class->disable = disable;
    modem_class->connect = connect;
    modem_class->disconnect = disconnect;
    modem_class->get_info = get_card_info;
}

static void
modem_cdma_init (MMModemCdma *cdma_class)
{
    cdma_class->get_signal_quality = get_signal_quality;
    cdma_class->get_esn = get_esn;
    cdma_class->get_serving_system = get_serving_system;
    cdma_class->get_registration_state = get_registration_state;
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
    class->get_status = simple_get_status;
}

static GObject*
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
    GObject *object;

    object = G_OBJECT_CLASS (mm_generic_cdma_parent_class)->constructor (type,
                                                                         n_construct_params,
                                                                         construct_params);
    if (object) {
        g_signal_connect (object, "notify::" MM_MODEM_VALID,
                          G_CALLBACK (modem_valid_changed), NULL);
    }

    return object;
}

static void
mm_generic_cdma_init (MMGenericCdma *self)
{
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (object);

    switch (prop_id) {
    case MM_MODEM_PROP_TYPE:
        break;
    case PROP_EVDO_REV0:
        priv->evdo_rev0 = g_value_get_boolean (value);
        break;
    case PROP_EVDO_REVA:
        priv->evdo_revA = g_value_get_boolean (value);
        break;
    case PROP_REG_TRY_CSS:
        priv->reg_try_css = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMGenericCdmaPrivate *priv = MM_GENERIC_CDMA_GET_PRIVATE (object);

    switch (prop_id) {
    case MM_MODEM_PROP_DATA_DEVICE:
        if (priv->data)
            g_value_set_string (value, mm_port_get_device (priv->data));
        else
            g_value_set_string (value, NULL);
        break;
    case MM_MODEM_PROP_TYPE:
        g_value_set_uint (value, MM_MODEM_TYPE_CDMA);
        break;
    case PROP_EVDO_REV0:
        g_value_set_boolean (value, priv->evdo_rev0);
        break;
    case PROP_EVDO_REVA:
        g_value_set_boolean (value, priv->evdo_revA);
        break;
    case PROP_REG_TRY_CSS:
        g_value_set_boolean (value, priv->reg_try_css);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
    registration_cleanup (MM_GENERIC_CDMA (object), MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL);

    G_OBJECT_CLASS (mm_generic_cdma_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    G_OBJECT_CLASS (mm_generic_cdma_parent_class)->finalize (object);
}

static void
mm_generic_cdma_class_init (MMGenericCdmaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    mm_generic_cdma_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMGenericCdmaPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->dispose = dispose;
    object_class->finalize = finalize;
    object_class->constructor = constructor;

    /* Properties */
    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DATA_DEVICE,
                                      MM_MODEM_DATA_DEVICE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_TYPE,
                                      MM_MODEM_TYPE);

    g_object_class_install_property (object_class, PROP_EVDO_REV0,
            g_param_spec_boolean (MM_GENERIC_CDMA_EVDO_REV0,
                                  "EVDO rev0",
                                  "Supports EVDO rev0",
                                  FALSE,
                                  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class, PROP_EVDO_REVA,
            g_param_spec_boolean (MM_GENERIC_CDMA_EVDO_REVA,
                                  "EVDO revA",
                                  "Supports EVDO revA",
                                  FALSE,
                                  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (object_class, PROP_REG_TRY_CSS,
            g_param_spec_boolean (MM_GENERIC_CDMA_REGISTRATION_TRY_CSS,
                                  "RegistrationTryCss",
                                  "Use Serving System response when checking modem"
                                  " registration state.",
                                  TRUE,
                                  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

