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

G_DEFINE_TYPE (MMModemOption, mm_modem_option, MM_TYPE_GENERIC_GSM)

#define MM_MODEM_OPTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_OPTION, MMModemOptionPrivate))

typedef struct {
    guint enable_wait_id;
} MMModemOptionPrivate;

MMModem *
mm_modem_option_new (const char *device,
                     const char *driver,
                     const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_OPTION,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

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

static void
get_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    gboolean parsed = FALSE;

    if (error)
        info->error = g_error_copy (error);
    else if (!g_str_has_prefix (response->str, "_OPSYS: ")) {
        int a, b;

        if (sscanf (response->str + 8, "%d,%d", &a, &b)) {
            MMModemGsmAllowedMode mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;

            switch (a) {
            case 0:
                mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
                break;
            case 1:
                mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;
                break;
            case 2:
                mode = MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED;
                break;
            case 3:
                mode = MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
                break;
            default:
                break;
            }

            mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);
            parsed = TRUE;
        }
    }

    if (!error && !parsed)
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                           "Could not parse allowed mode results");

    mm_callback_info_schedule (info);
}

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *primary;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);
    primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_at_serial_port_queue_command (primary, "AT_OPSYS?", 3, get_allowed_mode_done, info);
}

static void
set_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        info->error = g_error_copy (error);

   mm_callback_info_schedule (info);
}

static void
set_allowed_mode (MMGenericGsm *gsm,
                  MMModemGsmAllowedMode mode,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *primary;
    char *command;
    int i;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        i = 0;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        i = 1;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
        i = 2;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
        i = 3;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    default:
        i = 5;
        break;
    }

    command = g_strdup_printf ("AT_OPSYS=%d,2", i);
    primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_at_serial_port_queue_command (primary, command, 3, set_allowed_mode_done, info);
    g_free (command);
}

/*****************************************************************************/

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
}

