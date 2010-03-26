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
#include "mm-modem-helpers.h"

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

static gboolean
octi_to_mm (char octi, MMModemGsmAccessTech *out_act)
{
    if (octi == '1') {
        *out_act = MM_MODEM_GSM_ACCESS_TECH_GSM;
        return TRUE;
    } else if (octi == '2') {
        *out_act = MM_MODEM_GSM_ACCESS_TECH_GPRS;
        return TRUE;
    } else if (octi == '3') {
        *out_act = MM_MODEM_GSM_ACCESS_TECH_EDGE;
        return TRUE;
    }
    return FALSE;
}

static gboolean
owcti_to_mm (char owcti, MMModemGsmAccessTech *out_act)
{
    if (owcti == '1') {
        *out_act = MM_MODEM_GSM_ACCESS_TECH_UMTS;
        return TRUE;
    } else if (owcti == '2') {
        *out_act = MM_MODEM_GSM_ACCESS_TECH_HSDPA;
        return TRUE;
    } else if (owcti == '3') {
        *out_act = MM_MODEM_GSM_ACCESS_TECH_HSUPA;
        return TRUE;
    } else if (owcti == '4') {
        *out_act = MM_MODEM_GSM_ACCESS_TECH_HSPA;
        return TRUE;
    }
    return FALSE;
}

static gboolean
parse_octi_response (GString *response, MMModemGsmAccessTech *act)
{
    MMModemGsmAccessTech cur_act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    const char *p;
    GRegex *r;
    GMatchInfo *match_info;
    char *str;
    gboolean success = FALSE;

    g_return_val_if_fail (act != NULL, FALSE);
    g_return_val_if_fail (response != NULL, FALSE);

    p = mm_strip_tag (response->str, "_OCTI:");

    r = g_regex_new ("(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_return_val_if_fail (r != NULL, FALSE);

    g_regex_match (r, p, 0, &match_info);
    if (g_match_info_matches (match_info)) {
        str = g_match_info_fetch (match_info, 2);
        if (str && octi_to_mm (str[0], &cur_act)) {
            *act = cur_act;
            success = TRUE;
        }
        g_free (str);
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    return success;
}

static void
ossys_octi_request_done (MMAtSerialPort *port,
                         GString *response,
                         GError *error,
                         gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;

    if (!error) {
        if (parse_octi_response (response, &act))
            mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);
    }
}

static void
ossys_owcti_request_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)
{
    const char *p;
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;

    if (!error) {
        p = mm_strip_tag (response->str, "_OWCTI:");
        if (owcti_to_mm (*p, &act))
            mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);
    }
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
            break;
        }
    }
    g_free (str);

    mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);

    /* _OSSYSI only indicates general 2G/3G mode, so queue up some explicit
     * access technology requests.
     */
    if (act == MM_MODEM_GSM_ACCESS_TECH_GPRS)
        mm_at_serial_port_queue_command (port, "_OCTI?", 3, ossys_octi_request_done, user_data);
    else if (act == MM_MODEM_GSM_ACCESS_TECH_UMTS)
        mm_at_serial_port_queue_command (port, "_OWCTI?", 3, ossys_owcti_request_done, user_data);
}

static void
option_2g_tech_changed (MMAtSerialPort *port,
                        GMatchInfo *match_info,
                        gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;

    str = g_match_info_fetch (match_info, 1);
    if (octi_to_mm (str[0], &act))
        mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);
    g_free (str);
}

static void
option_3g_tech_changed (MMAtSerialPort *port,
                        GMatchInfo *match_info,
                        gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;

    str = g_match_info_fetch (match_info, 1);
    if (owcti_to_mm (str[0], &act))
        mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);
    g_free (str);
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

    regex = g_regex_new ("\\r\\n_OUWCTI:\\s*(\\d+)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, option_3g_tech_changed, modem, NULL);
    g_regex_unref (regex);

    regex = g_regex_new ("\\r\\n_OSIGQ:\\s*(\\d+),(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (port, regex, option_signal_changed, modem, NULL);
    g_regex_unref (regex);
}

static void
unsolicited_msg_done (MMAtSerialPort *port,
                      GString *response,
                      GError *error,
                      gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    if (info)
        mm_callback_info_chain_complete_one (info);
}

static void
option_change_unsolicited_messages (MMGenericGsm *modem,
                                    gboolean enabled,
                                    MMModemFn callback,
                                    gpointer user_data)
{
    MMCallbackInfo *info = NULL;
    MMAtSerialPort *primary;

    if (callback) {
        info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
        mm_callback_info_chain_start (info, 4);
    }

    primary = mm_generic_gsm_get_at_port (modem, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    mm_at_serial_port_queue_command (primary, enabled ? "_OSSYS=1" : "_OSSYS=0", 3, unsolicited_msg_done, info);
    mm_at_serial_port_queue_command (primary, enabled ? "_OCTI=1" : "_OCTI=0", 3, unsolicited_msg_done, info);
    mm_at_serial_port_queue_command (primary, enabled ? "_OUWCTI=1" : "_OUWCTI=0", 3, unsolicited_msg_done, info);
    mm_at_serial_port_queue_command (primary, enabled ? "_OSQI=1" : "_OSQI=0", 3, unsolicited_msg_done, info);
}

static void
get_act_octi_request_done (MMAtSerialPort *port,
                           GString *response,
                           GError *error,
                           gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemGsmAccessTech octi = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    MMModemGsmAccessTech owcti;

    if (!error) {
        if (parse_octi_response (response, &octi)) {
            /* If no 3G tech yet or current tech isn't 3G, then 2G tech is the best */
            owcti = GPOINTER_TO_UINT (mm_callback_info_get_data (info, "owcti"));
            if (octi && !owcti)
                mm_callback_info_set_result (info, GUINT_TO_POINTER (octi), NULL);
        }
    }

    mm_callback_info_chain_complete_one (info);
}

static void
get_act_owcti_request_done (MMAtSerialPort *port,
                            GString *response,
                            GError *error,
                            gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemGsmAccessTech owcti = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    const char *p;

    if (!error) {
        p = mm_strip_tag (response->str, "_OWCTI:");
        if (owcti_to_mm (*p, &owcti)) {
            /* 3G tech always takes precedence over 2G tech */
            if (owcti)
                mm_callback_info_set_result (info, GUINT_TO_POINTER (owcti), NULL);
        }
    }

    mm_callback_info_chain_complete_one (info);
}

static void
option_get_access_technology (MMGenericGsm *modem,
                              MMModemUIntFn callback,
                              gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (modem), callback, user_data);
    mm_callback_info_chain_start (info, 2);

    port = mm_generic_gsm_get_best_at_port (modem, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "_OCTI?", 3, get_act_octi_request_done, info);
    mm_at_serial_port_queue_command (port, "_OWCTI?", 3, get_act_owcti_request_done, info);
}

