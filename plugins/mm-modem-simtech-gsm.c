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
#include <ctype.h>
#include "mm-modem-simtech-gsm.h"
#include "mm-at-serial-port.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-helpers.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemSimtechGsm, mm_modem_simtech_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

MMModem *
mm_modem_simtech_gsm_new (const char *device,
                          const char *driver,
                          const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_SIMTECH_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

/*****************************************************************************/

#define ACQ_ORDER_TAG "acq-order"

static void
get_mode_pref_done (MMAtSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *p;
    gint modepref = -1;
    guint32 acqord;
    MMModemGsmAllowedMode allowed = MM_MODEM_GSM_ALLOWED_MODE_ANY;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error)
        goto done;

    p = mm_strip_tag (response->str, "+CNMP:");
    if (!p) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse the mode preference response");
        goto done;
    }

    acqord = GPOINTER_TO_UINT (mm_callback_info_get_data (info, ACQ_ORDER_TAG));
    modepref = atoi (p);

    if (modepref == 2) {
        /* Automatic */
        if (acqord == 0)
            allowed = MM_MODEM_GSM_ALLOWED_MODE_ANY;
        else if (acqord == 1)
            allowed = MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED;
        else if (acqord == 2)
            allowed = MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
        else {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "Unknown acqisition order preference %d",
                                       acqord);
        }
    } else if (modepref == 13) {
        /* GSM only */
        allowed = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
    } else if (modepref == 14) {
        /* WCDMA only */
        allowed = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;
    } else {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Unknown mode preference %d",
                                   modepref);
    }

done:
    if (!info->error)
        mm_callback_info_set_result (info, GUINT_TO_POINTER (allowed), NULL);
    mm_callback_info_schedule (info);
}

static void
get_acq_order_done (MMAtSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    const char *p;
    gint acqord = -1;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error)
        goto done;

    p = mm_strip_tag (response->str, "+CNAOP:");
    if (!p) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse the acqisition order response");
        goto done;
    }

    acqord = atoi (p);
    if (acqord < 0 || acqord > 2) {
        info->error = g_error_new (MM_MODEM_ERROR,
                                   MM_MODEM_ERROR_GENERAL,
                                   "Unknown acquisition order response %d",
                                   acqord);
    } else {
        /* Cache the acquisition preference */
        mm_callback_info_set_data (info, ACQ_ORDER_TAG, GUINT_TO_POINTER (acqord), NULL);
    }

done:
    if (info->error)
        mm_callback_info_schedule (info);
    else
        mm_at_serial_port_queue_command (port, "+CNMP?", 3, get_mode_pref_done, info);
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

    mm_at_serial_port_queue_command (port, "+CNAOP?", 3, get_acq_order_done, info);
}

static void
set_acq_order_done (MMAtSerialPort *port,
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
set_mode_pref_done (MMAtSerialPort *port,
                    GString *response,
                    GError *error,
                    gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    guint32 naop;
    char *command;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        mm_callback_info_schedule (info);
        return;
    }

    naop = GPOINTER_TO_UINT (mm_callback_info_get_data (info, ACQ_ORDER_TAG));
    command = g_strdup_printf ("+CNAOP=%u", naop);
    mm_at_serial_port_queue_command (port, command, 3, set_acq_order_done, info);
    g_free (command);
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
    guint32 nmp = 2;   /* automatic mode preference */
    guint32 naop = 0;  /* automatic acquisition order */

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        nmp = 13;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        nmp = 14;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
        naop = 2;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
        naop = 3;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    default:
        break;
    }

    mm_callback_info_set_data (info, ACQ_ORDER_TAG, GUINT_TO_POINTER (naop), NULL);

    command = g_strdup_printf ("+CNMP=%u", nmp);
    mm_at_serial_port_queue_command (port, command, 3, set_mode_pref_done, info);
    g_free (command);
}

static MMModemGsmAccessTech
simtech_act_to_mm_act (int nsmod)
{
    if (nsmod == 1)
        return MM_MODEM_GSM_ACCESS_TECH_GSM;
    else if (nsmod == 2)
        return MM_MODEM_GSM_ACCESS_TECH_GPRS;
    else if (nsmod == 3)
        return MM_MODEM_GSM_ACCESS_TECH_EDGE;
    else if (nsmod == 4)
        return MM_MODEM_GSM_ACCESS_TECH_UMTS;
    else if (nsmod == 5)
        return MM_MODEM_GSM_ACCESS_TECH_HSDPA;
    else if (nsmod == 6)
        return MM_MODEM_GSM_ACCESS_TECH_HSUPA;
    else if (nsmod == 7)
        return MM_MODEM_GSM_ACCESS_TECH_HSPA;

    return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
}

static void
get_act_tech_done (MMAtSerialPort *port,
                   GString *response,
                   GError *error,
                   gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    const char *p;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error) {
        mm_callback_info_schedule (info);
        return;
    }

    p = mm_strip_tag (response->str, "+CNSMOD:");
    if (p)
        p = strchr (p, ',');

    if (!p || !isdigit (*(p + 1))) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse the access technology response");
    } else {
        act = simtech_act_to_mm_act (atoi (p + 1));
        mm_callback_info_set_result (info, GUINT_TO_POINTER (act), NULL);
    }

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

    mm_at_serial_port_queue_command (port, "AT+CNSMOD?", 3, get_act_tech_done, info);
}

static void
handle_act_change (MMAtSerialPort *port,
                   GMatchInfo *match_info,
                   gpointer user_data)
{
    MMModemSimtechGsm *self = MM_MODEM_SIMTECH_GSM (user_data);
    MMModemGsmAccessTech act;
    char *str;

    str = g_match_info_fetch (match_info, 1);
    if (str && strlen (str)) {
        act = simtech_act_to_mm_act (atoi (str));
        mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (self), act);
    }
    g_free (str);
}

/*****************************************************************************/

static void
real_do_enable_power_up_done (MMGenericGsm *gsm,
                              GString *response,
                              GError *error,
                              MMCallbackInfo *info)
{
    if (!error) {
        MMAtSerialPort *primary;

        /* Enable unsolicited result codes */
        primary = mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY);
        g_assert (primary);

        /* Autoreport access technology changes */
        mm_at_serial_port_queue_command (primary, "+CNSMOD=1", 5, NULL, NULL);

        /* Autoreport CSQ (first arg), and only report when it changes (second arg) */
        mm_at_serial_port_queue_command (primary, "+AUTOCSQ=1,1", 5, NULL, NULL);
    }

    /* Chain up to parent */
    MM_GENERIC_GSM_CLASS (mm_modem_simtech_gsm_parent_class)->do_enable_power_up_done (gsm, NULL, error, info);
}

/*****************************************************************************/

typedef struct {
    MMModem *modem;
    MMModemFn callback;
    gpointer user_data;
} DisableInfo;

static void
disable_unsolicited_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)

{
    MMModem *parent_modem_iface;
    DisableInfo *info = user_data;

    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
    parent_modem_iface->disable (info->modem, info->callback, info->user_data);
    g_free (info);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMAtSerialPort *primary;
    DisableInfo *info;

    info = g_malloc0 (sizeof (DisableInfo));
    info->callback = callback;
    info->user_data = user_data;
    info->modem = modem;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    /* Turn off unsolicited responses */
    mm_at_serial_port_queue_command (primary, "+CNSMOD=0;+AUTOCSQ=0", 5, disable_unsolicited_done, info);
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
    MMPort *port;

    if (suggested_type == MM_PORT_TYPE_UNKNOWN) {
        if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_PRIMARY))
                ptype = MM_PORT_TYPE_PRIMARY;
        else if (!mm_generic_gsm_get_at_port (gsm, MM_PORT_TYPE_SECONDARY))
            ptype = MM_PORT_TYPE_SECONDARY;
    } else
        ptype = suggested_type;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);

    if (port && MM_IS_AT_SERIAL_PORT (port)) {
        GRegex *regex;

        regex = g_regex_new ("\\r\\n\\+CNSMOD:\\s*(\\d)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, handle_act_change, modem, NULL);
        g_regex_unref (regex);
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
mm_modem_simtech_gsm_init (MMModemSimtechGsm *self)
{
}

static void
mm_modem_simtech_gsm_class_init (MMModemSimtechGsmClass *klass)
{
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_simtech_gsm_parent_class = g_type_class_peek_parent (klass);

    gsm_class->do_enable_power_up_done = real_do_enable_power_up_done;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

