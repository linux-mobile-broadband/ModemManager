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
 * Generic utilities for Option NV modems
 * Used with both 'option' and 'hso'
 ******************************************/

#include "mm-callback-info.h"
#include "mm-at-serial-port.h"
#include "mm-generic-gsm.h"

static void
option_get_allowed_mode_done (MMAtSerialPort *port,
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
option_get_allowed_mode (MMGenericGsm *gsm,
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
    mm_at_serial_port_queue_command (port, "AT_OPSYS?", 3, option_get_allowed_mode_done, info);
}

static void
option_set_allowed_mode_done (MMAtSerialPort *port,
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
option_set_allowed_mode (MMGenericGsm *gsm,
                         MMModemGsmAllowedMode mode,
                         MMModemFn callback,
                         gpointer user_data)
{
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    char *command;
    int i;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
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

    command = g_strdup_printf ("AT_OPSYS=%d,2", i);
    mm_at_serial_port_queue_command (port, command, 3, option_set_allowed_mode_done, info);
    g_free (command);
}

static void
option_ossys_tech_changed (MMAtSerialPort *port,
                           GMatchInfo *info,
                           gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;

    str = g_match_info_fetch (info, 1);
    if (str) {
        switch (atoi (str)) {
        case 0:
            act = MM_MODEM_GSM_ACCESS_TECH_GPRS;
            break;
        case 2:
            act = MM_MODEM_GSM_ACCESS_TECH_UMTS;
            break;
        default:
            /* _OSSYSI only indicates general 2G/3G mode */
            break;
        }
    }
    g_free (str);

    mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);
}

static void
option_2g_tech_changed (MMAtSerialPort *port,
                        GMatchInfo *match_info,
                        gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;

    str = g_match_info_fetch (match_info, 1);
    switch (atoi (str)) {
    case 1:
        act = MM_MODEM_GSM_ACCESS_TECH_GSM;
        break;
    case 2:
        act = MM_MODEM_GSM_ACCESS_TECH_GPRS;
        break;
    case 3:
        act = MM_MODEM_GSM_ACCESS_TECH_EDGE;
        break;
    default:
        break;
    }
    g_free (str);

    /* At the moment we can't do much with this since it's not consistently
     * reported by the modem.  _OSSYSI appears to always be reported, but 
     * doesn't provide the granularity that _OCTI and _OWCTI do, and so it
     * would overwrite any _OCTI or _OWCTI response, and cause the access tech
     * to flip-flop often.
     */
}

static void
option_3g_tech_changed (MMAtSerialPort *port,
                        GMatchInfo *match_info,
                        gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;

    str = g_match_info_fetch (match_info, 1);
    switch (atoi (str)) {
    case 1:
        act = MM_MODEM_GSM_ACCESS_TECH_UMTS;
        break;
    case 2:
        act = MM_MODEM_GSM_ACCESS_TECH_HSDPA;
        break;
    case 3:
        act = MM_MODEM_GSM_ACCESS_TECH_HSUPA;
        break;
    case 4:
        act = MM_MODEM_GSM_ACCESS_TECH_HSPA;
        break;
    default:
        break;
    }
    g_free (str);

    /* At the moment we can't do much with this since it's not consistently
     * reported by the modem.  _OSSYSI appears to always be reported, but 
     * doesn't provide the granularity that _OCTI and _OWCTI do, and so it
     * would overwrite any _OCTI or _OWCTI response, and cause the access tech
     * to flip-flop often.
     */
}

static void
option_signal_changed (MMAtSerialPort *port,
                       GMatchInfo *match_info,
                       gpointer user_data)
{
    char *str;
    int quality = 0;

    str = g_match_info_fetch (match_info, 1);
    quality = atoi (str);
    g_free (str);

    if (quality == 99) {
        /* 99 means unknown */
        quality = 0;
    } else {
        /* Normalize the quality */
        quality = CLAMP (quality, 0, 31) * 100 / 31;
    }

    mm_generic_gsm_update_signal_quality (MM_GENERIC_GSM (user_data), (guint32) quality);
}

static void
option_register_unsolicted_handlers (MMGenericGsm *modem, MMAtSerialPort *port)
{
    GRegex *regex;

    regex = g_regex_new ("\\r\\n_OSSYSI:\\s*(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, option_ossys_tech_changed, modem, NULL);
    g_regex_unref (regex);

    regex = g_regex_new ("\\r\\n_OCTI:\\s*(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, option_2g_tech_changed, modem, NULL);
    g_regex_unref (regex);

    regex = g_regex_new ("\\r\\n_OWCTI:\\s*(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, option_3g_tech_changed, modem, NULL);
    g_regex_unref (regex);

    regex = g_regex_new ("\\r\\n_OSIGQ:\\s*(\\d+),(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, option_signal_changed, modem, NULL);
    g_regex_unref (regex);
}

static void
option_change_unsolicited_messages (MMGenericGsm *modem, gboolean enabled)
{
    MMAtSerialPort *primary;

    primary = mm_generic_gsm_get_at_port (modem, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_at_serial_port_queue_command (primary, enabled ? "_OSSYS=1" : "_OSSYS=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (primary, enabled ? "_OCTI=1" : "_OCTI=0", 3, NULL, NULL);
    mm_at_serial_port_queue_command (primary, enabled ? "_OSQI=1" : "_OSQI=0", 3, NULL, NULL);
}

