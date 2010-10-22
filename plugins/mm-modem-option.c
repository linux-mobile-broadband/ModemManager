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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-option.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemOption, mm_modem_option, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

#define MM_MODEM_OPTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_OPTION, MMModemOptionPrivate))

typedef struct {
    guint enable_wait_id;
} MMModemOptionPrivate;

MMModem *
mm_modem_option_new (const char *device,
                     const char *driver,
                     const char *plugin,
                     guint32 vendor,
                     guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_OPTION,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

#include "mm-modem-option-utils.c"

/*****************************************************************************/

static gboolean
option_enabled (gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMGenericGsm *modem;
    MMModemOptionPrivate *priv;

    /* Make sure we don't use an invalid modem that may have been removed */
    if (info->modem) {
        modem = MM_GENERIC_GSM (info->modem);
        priv = MM_MODEM_OPTION_GET_PRIVATE (modem);
        priv->enable_wait_id = 0;

        option_change_unsolicited_messages (modem, TRUE, NULL, NULL);

        MM_GENERIC_GSM_CLASS (mm_modem_option_parent_class)->do_enable_power_up_done (modem, NULL, NULL, info);
    }
    return FALSE;
}

static void
real_do_enable_power_up_done (MMGenericGsm *gsm,
                              GString *response,
                              GError *error,
                              MMCallbackInfo *info)
{
    MMModemOptionPrivate *priv = MM_MODEM_OPTION_GET_PRIVATE (gsm);

    if (error) {
        /* Chain up to parent */
        MM_GENERIC_GSM_CLASS (mm_modem_option_parent_class)->do_enable_power_up_done (gsm, NULL, error, info);
        return;
    }

    /* Some Option devices return OK on +CFUN=1 right away but need some time
     * to finish initialization.
     */
    g_warn_if_fail (priv->enable_wait_id == 0);
    priv->enable_wait_id = g_timeout_add_seconds (10, option_enabled, info);
}

/*****************************************************************************/

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    option_get_allowed_mode (gsm, callback, user_data);
}

static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    option_set_allowed_mode (gsm, mode, callback, user_data);
}

static void
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    option_get_access_technology (gsm, callback, user_data);
}

/*****************************************************************************/

static void
parent_disable_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);
    mm_callback_info_schedule (info);
}

static void
unsolicited_disable_done (MMModem *modem,
                          GError *error,
                          gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModem *parent_modem_iface;
    GError *tmp_error = NULL;

    /* Handle modem removal, but ignore other errors */
    if (g_error_matches (error, MM_MODEM_ERROR, MM_MODEM_ERROR_REMOVED)) {
        parent_disable_done (modem, error, user_data);
        return;
    } else if (!modem) {
        tmp_error =  g_error_new_literal (MM_MODEM_ERROR,
                                          MM_MODEM_ERROR_REMOVED,
                                          "The modem was removed.");
        parent_disable_done (modem, tmp_error, user_data);
        g_error_free (tmp_error);
        return;
    }

    /* Chain up to parent */
    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));
    parent_modem_iface->disable (info->modem, parent_disable_done, info);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMCallbackInfo *info;

    mm_generic_gsm_pending_registration_stop (MM_GENERIC_GSM (modem));

    info = mm_callback_info_new (modem, callback, user_data);

    /* Turn off unsolicited messages so they don't pile up in the modem */
    option_change_unsolicited_messages (MM_GENERIC_GSM (modem), FALSE, unsolicited_disable_done, info);
}

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           MMPortType suggested_type,
           gpointer user_data,
           GError **error)
{
    MMGenericGsm *gsm = MM_GENERIC_GSM (modem);
    MMPort *port = NULL;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, suggested_type, error);
    if (port && MM_IS_AT_SERIAL_PORT (port)) {
        if (mm_port_get_port_type (port) == MM_PORT_TYPE_PRIMARY) {
            GRegex *regex;

            regex = g_regex_new ("\\r\\n\\+PACSP0\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
            mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
            g_regex_unref (regex);
        }
        option_register_unsolicted_handlers (gsm, MM_AT_SERIAL_PORT (port));
    }

    return !!port;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->disable = disable;
    modem_class->grab_port = grab_port;
}

static void
mm_modem_option_init (MMModemOption *self)
{
}

static void
dispose (GObject *object)
{
    MMModemOptionPrivate *priv = MM_MODEM_OPTION_GET_PRIVATE (object);

    if (priv->enable_wait_id)
        g_source_remove (priv->enable_wait_id);
}

static void
mm_modem_option_class_init (MMModemOptionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_option_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemOptionPrivate));

    object_class->dispose = dispose;
    gsm_class->do_enable_power_up_done = real_do_enable_power_up_done;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

