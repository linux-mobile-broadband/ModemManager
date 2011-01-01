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
 * Copyright (C) 2010 Red Hat, Inc.
 */

/******************************************
 * Generic utilities for Icera-based modems
 ******************************************/

#include <config.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mm-modem-icera.h"

#include "mm-modem.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-at-serial-port.h"
#include "mm-generic-gsm.h"
#include "mm-modem-helpers.h"

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
    else if (!g_str_has_prefix (response->str, "%IPSYS: ")) {
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

void
mm_modem_icera_get_allowed_mode (MMModemIcera *self,
                                 MMModemUIntFn callback,
                                 gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }
    mm_at_serial_port_queue_command (port, "%IPSYS?", 3, get_allowed_mode_done, info);
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

void
mm_modem_icera_set_allowed_mode (MMModemIcera *self,
                                 MMModemGsmAllowedMode mode,
                                 MMModemFn callback,
                                 gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    char *command;
    int i;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

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

    command = g_strdup_printf ("%%IPSYS=%d", i);
    mm_at_serial_port_queue_command (port, command, 3, set_allowed_mode_done, info);
    g_free (command);
}

static MMModemGsmAccessTech
nwstate_to_act (const char *str)
{
    /* small 'g' means CS, big 'G' means PS */
    if (!strcmp (str, "2G-GPRS"))
        return MM_MODEM_GSM_ACCESS_TECH_GPRS;
    else if (!strcmp (str, "2G-EDGE"))
        return MM_MODEM_GSM_ACCESS_TECH_EDGE;
    else if (!strcmp (str, "3G"))
        return MM_MODEM_GSM_ACCESS_TECH_UMTS;
    else if (!strcmp (str, "3G-HSDPA"))
        return MM_MODEM_GSM_ACCESS_TECH_HSDPA;
    else if (!strcmp (str, "3G-HSUPA"))
        return MM_MODEM_GSM_ACCESS_TECH_HSUPA;
    else if (!strcmp (str, "3G-HSDPA-HSUPA"))
        return MM_MODEM_GSM_ACCESS_TECH_HSPA;

    return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
}

static void
nwstate_changed (MMAtSerialPort *port,
                 GMatchInfo *info,
                 gpointer user_data)
{
    MMModemIcera *self = MM_MODEM_ICERA (user_data);
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;
    int rssi = -1;

    str = g_match_info_fetch (info, 1);
    if (str) {
        rssi = atoi (str);
        rssi = CLAMP (rssi, -1, 5);
        g_free (str);
    }

    str = g_match_info_fetch (info, 3);
    if (str) {
        act = nwstate_to_act (str);
        g_free (str);
    }

    self->last_act = act;
    mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (self), act);
}

void
mm_modem_icera_register_unsolicted_handlers (MMModemIcera *self,
                                             MMAtSerialPort *port)
{
    GRegex *regex;

    /* %NWSTATE: <rssi>,<mccmnc>,<tech>,<connected>,<regulation> */
    regex = g_regex_new ("\\r\\n%NWSTATE:\\s*(\\d+),(\\d+),([^,]*),([^,]*),(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, nwstate_changed, self, NULL);
    g_regex_unref (regex);
}

void
mm_modem_icera_change_unsolicited_messages (MMModemIcera *self, gboolean enabled)
{
    MMAtSerialPort *primary;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (self), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_at_serial_port_queue_command (primary, enabled ? "%NWSTATE=1" : "%NWSTATE=0", 3, NULL, NULL);
}

static void
get_nwstate_done (MMAtSerialPort *port,
                  GString *response,
                  GError *error,
                  gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error) {
        MMModemIcera *self = MM_MODEM_ICERA (info->modem);

        /* The unsolicited message handler will already have run and
         * removed the NWSTATE response, so we have to work around that.
         */
        mm_callback_info_set_result (info, GUINT_TO_POINTER (self->last_act), NULL);
        self->last_act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    }

    mm_callback_info_schedule (info);
}

void
mm_modem_icera_get_access_technology (MMModemIcera *self,
                                      MMModemUIntFn callback,
                                      gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (self), &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "%NWSTATE=1", 3, get_nwstate_done, info);
}

static void
icera_check_done (MMAtSerialPort *port,
                  GString *response,
                  GError *error,
                  gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    info->error = mm_modem_check_removed (info->modem, error);
    if (!info->error)
        mm_callback_info_set_result (info, GUINT_TO_POINTER (TRUE), NULL);
    mm_callback_info_schedule (info);
}

void
mm_modem_icera_is_icera (MMGenericGsm *modem,
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

    mm_at_serial_port_queue_command (port, "%IPSYS?", 5, icera_check_done, info);
}

/****************************************************************/

void
mm_modem_icera_dispose (MMModemIcera *self)
{
}

static void
mm_modem_icera_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (!initialized) {
        initialized = TRUE;
    }
}

GType
mm_modem_icera_get_type (void)
{
    static GType icera_type = 0;

    if (!G_UNLIKELY (icera_type)) {
        const GTypeInfo icera_info = {
            sizeof (MMModemIcera), /* class_size */
            mm_modem_icera_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        icera_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "MMModemIcera",
                                             &icera_info, 0);

        g_type_interface_add_prerequisite (icera_type, G_TYPE_OBJECT);
        g_type_interface_add_prerequisite (icera_type, MM_TYPE_MODEM);
        g_type_interface_add_prerequisite (icera_type, MM_TYPE_GENERIC_GSM);
    }

    return icera_type;
}

