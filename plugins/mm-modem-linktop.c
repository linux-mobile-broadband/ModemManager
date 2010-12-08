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
#include "mm-modem-linktop.h"
#include "mm-serial-parsers.h"
#include "mm-errors.h"

#define LINKTOP_NETWORK_MODE_ANY  1
#define LINKTOP_NETWORK_MODE_2G   5
#define LINKTOP_NETWORK_MODE_3G   6

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemLinktop, mm_modem_linktop, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))


MMModem *
mm_modem_linktop_new (const char *device,
                    const char *driver,
                    const char *plugin,
                    guint32 vendor,
                    guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_LINKTOP,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
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
    MMPortType ptype = MM_PORT_TYPE_IGNORED;
    MMPort *port = NULL;

    if (suggested_type == MM_PORT_TYPE_UNKNOWN) {
        if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY))
                ptype = MM_PORT_TYPE_PRIMARY;
        else if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_SECONDARY))
            ptype = MM_PORT_TYPE_SECONDARY;
    } else
        ptype = suggested_type;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (port && MM_IS_AT_SERIAL_PORT (port)) {
        mm_at_serial_port_set_response_parser (MM_AT_SERIAL_PORT (port),
                                               mm_serial_parser_v1_e1_parse,
                                               mm_serial_parser_v1_e1_new (),
                                               mm_serial_parser_v1_e1_destroy);
    }

    return !!port;
}

static int
linktop_parse_allowed_mode (MMModemGsmAllowedMode network_mode)
{
    switch (network_mode) {
        case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
            return LINKTOP_NETWORK_MODE_2G;
        case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
            return LINKTOP_NETWORK_MODE_3G;
        default:
            return LINKTOP_NETWORK_MODE_ANY;
    }
}

static void
linktop_set_allowed_mode_done (MMAtSerialPort *port,
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
    char *command;
    MMAtSerialPort *port;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }
    
    command = g_strdup_printf ("+CFUN=%d", linktop_parse_allowed_mode (mode));
    mm_at_serial_port_queue_command (port, command, 3, linktop_set_allowed_mode_done, info);
    g_free (command);
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
    else if (!g_str_has_prefix (response->str, "CFUN: ")) {
        MMModemGsmAllowedMode mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
        int a;

        a = atoi (response->str + 6);
        if (a == LINKTOP_NETWORK_MODE_2G)
            mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
        else if (a == LINKTOP_NETWORK_MODE_3G)
            mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;

        mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);
        parsed = TRUE;
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
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+CFUN?", 3, get_allowed_mode_done, info);
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
}

static void
mm_modem_linktop_init (MMModemLinktop *self)
{
}

static void
mm_modem_linktop_class_init (MMModemLinktopClass *klass)
{
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->set_allowed_mode = set_allowed_mode;
}

