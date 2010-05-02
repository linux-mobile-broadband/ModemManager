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

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-longcheer-gsm.h"
#include "mm-at-serial-port.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMModemLongcheerGsm, mm_modem_longcheer_gsm, MM_TYPE_GENERIC_GSM)

MMModem *
mm_modem_longcheer_gsm_new (const char *device,
                            const char *driver,
                            const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_LONGCHEER_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

/*****************************************************************************/

static void
get_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *p;
    MMModemGsmAllowedMode mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
    gint mododr = -1;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error)
        goto done;

    p = mm_strip_tag (response->str, "+MODODR:");
    if (!p) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse the allowed mode response");
        goto done;
    }

    mododr = atoi (p);
    switch (mododr) {
    case 1:
        mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;
        break;
    case 2:
    case 4:
        mode = MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
        break;
    case 3:
        mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
        break;
    default:
        break;
    }
    mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);

done:
    mm_callback_info_schedule (info);
}

static void
get_allowed_mode (MMGenericGsm *gsm,
                  MMModemUIntFn callback,
                  gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "AT+MODODR?", 3, get_allowed_mode_done, info);
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
    MMAtSerialPort *port;
    char *command;
    int mododr = 0;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        mododr = 3;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        mododr = 1;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
    default:
        mododr = 2;
        break;
    }

    command = g_strdup_printf ("+MODODR=%d", mododr);
    mm_at_serial_port_queue_command (port, command, 3, set_allowed_mode_done, info);
    g_free (command);
}

static void
get_act_request_done (MMAtSerialPort *port,
                      GString *response,
                      GError *error,
                      gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    const char *p;

    if (error)
        info->error = g_error_copy (error);
    else {
        p = mm_strip_tag (response->str, "+PSRAT:");
        act = mm_gsm_string_to_access_tech (p);
    }

    mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
    mm_callback_info_schedule (info);
}

static void
get_access_technology (MMGenericGsm *modem,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (modem, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+PSRAT", 3, get_act_request_done, info);
}

/*****************************************************************************/

static void
mm_modem_longcheer_gsm_init (MMModemLongcheerGsm *self)
{
}

static void
mm_modem_longcheer_gsm_class_init (MMModemLongcheerGsmClass *klass)
{
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_longcheer_gsm_parent_class = g_type_class_peek_parent (klass);

    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

