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
#include "mm-modem-zte.h"
#include "mm-serial-port.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-helpers.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemZte, mm_modem_zte, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

#define MM_MODEM_ZTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_ZTE, MMModemZtePrivate))

typedef struct {
    gboolean init_retried;
    guint32 cpms_tries;
    guint cpms_timeout;
} MMModemZtePrivate;

MMModem *
mm_modem_zte_new (const char *device,
                  const char *driver,
                  const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_ZTE,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

/*****************************************************************************/

static void
zte_access_tech_changed (MMAtSerialPort *port,
                         GMatchInfo *info,
                         gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;

    str = g_match_info_fetch (info, 1);
    if (str)
        act = mm_gsm_string_to_access_tech (str);
    g_free (str);

    mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);
}

/*****************************************************************************/

static void
get_allowed_mode_done (MMAtSerialPort *port,
                       GString *response,
                       GError *error,
                       gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    GRegex *r = NULL;
    GMatchInfo *match_info;

    info->error = mm_modem_check_removed (info->modem, error);
    if (info->error)
        goto done;

    r = g_regex_new ("+ZSNT:\\s*(\\d),(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    if (!r) {
        info->error = g_error_new_literal (MM_MODEM_ERROR,
                                           MM_MODEM_ERROR_GENERAL,
                                           "Failed to parse the allowed mode response");
        goto done;
    }

    if (g_regex_match_full (r, response->str, response->len, 0, 0, &match_info, &info->error)) {
        MMModemGsmAllowedMode mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
        char *str;
        int cm_mode = -1, pref_acq = -1;

        str = g_match_info_fetch (match_info, 1);
        cm_mode = atoi (str);
        g_free (str);

        str = g_match_info_fetch (match_info, 3);
        pref_acq = atoi (str);
        g_free (str);

        g_match_info_free (match_info);

        if (cm_mode < 0 || cm_mode > 2 || pref_acq < 0 || pref_acq > 2) {
            info->error = g_error_new (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_GENERAL,
                                       "Failed to parse the allowed mode response: '%s'",
                                       response->str);
            goto done;
        }

        if (cm_mode == 0) {  /* Both 2G and 3G allowed */
            if (pref_acq == 0)
                mode = MM_MODEM_GSM_ALLOWED_MODE_ANY;
            else if (pref_acq == 1)
                mode = MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED;
            else if (pref_acq == 2)
                mode = MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
        } else if (cm_mode == 1) /* GSM only */
            mode = MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
        else if (cm_mode == 2) /* WCDMA only */
            mode = MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;

        mm_callback_info_set_result (info, GUINT_TO_POINTER (mode), NULL);
    }

done:
    if (r)
        g_regex_unref (r);
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

    mm_at_serial_port_queue_command (port, "AT+ZSNT?", 3, get_allowed_mode_done, info);
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
    int cm_mode = 0, pref_acq = 0;

    info = mm_callback_info_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    switch (mode) {
    case MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY:
        cm_mode = 1;
        pref_acq = 0;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY:
        cm_mode = 2;
        pref_acq = 0;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED:
        cm_mode = 0;
        pref_acq = 1;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED:
        cm_mode = 0;
        pref_acq = 2;
        break;
    case MM_MODEM_GSM_ALLOWED_MODE_ANY:
    default:
        break;
    }

    command = g_strdup_printf ("AT+ZSNT=%d,0,%d", cm_mode, pref_acq);
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
        /* Sample response from an MF626:
         *   +ZPAS: "GPRS/EDGE","CS_ONLY"
         */
        p = mm_strip_tag (response->str, "+ZPAS:");
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

    mm_at_serial_port_queue_command (port, "+ZPAS?", 3, get_act_request_done, info);
}

/*****************************************************************************/
/*    Modem class override functions                                         */
/*****************************************************************************/

static void cpms_try_done (MMAtSerialPort *port,
                           GString *response,
                           GError *error,
                           gpointer user_data);

static gboolean
cpms_timeout_cb (gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModem *modem = info->modem;
    MMAtSerialPort *primary;

    if (modem) {
        MM_MODEM_ZTE_GET_PRIVATE (modem)->cpms_timeout = 0;
        primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
        g_assert (primary);
        mm_at_serial_port_queue_command (primary, "+CPMS?", 10, cpms_try_done, info);
    }
    return FALSE;
}

static void
cpms_try_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = user_data;
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (info->modem);

    if (error && g_error_matches (error, MM_MOBILE_ERROR, MM_MOBILE_ERROR_SIM_BUSY)) {
        if (priv->cpms_tries++ < 4) {
            if (priv->cpms_timeout)
                g_source_remove (priv->cpms_timeout);

            /* Have to try a few times; sometimes the SIM is busy */
            priv->cpms_timeout = g_timeout_add_seconds (2, cpms_timeout_cb, info);
            return;
        } else {
            /* oh well, proceed... */
            error = NULL;
        }
    }

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
}

static void
init_modem_done (MMAtSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* Attempt to disable floods of "+ZUSIMR:2" unsolicited responses that
     * eventually fill up the device's buffers and make it crash.  Normally
     * done during probing, but if the device has a PIN enabled it won't
     * accept the +CPMS? during the probe and we have to do it here.
     */
    mm_at_serial_port_queue_command (port, "+CPMS?", 10, cpms_try_done, info);
}

static void enable_flash_done (MMSerialPort *port,
                               GError *error,
                               gpointer user_data);

static void
pre_init_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (info->modem);

    if (error) {
        /* Retry the init string one more time; the modem sometimes throws it away */
        if (   !priv->init_retried
            && g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            priv->init_retried = TRUE;
            enable_flash_done (MM_SERIAL_PORT (port), NULL, user_data);
        } else
            mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
    } else {
        /* Finish the initialization */
        mm_at_serial_port_queue_command (port, "Z E0 V1 X4 &C1 +CMEE=1;+CFUN=1;", 10, init_modem_done, info);
    }
}

static void
enable_flash_done (MMSerialPort *port, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
    else
        mm_at_serial_port_queue_command (MM_AT_SERIAL_PORT (port), "E0 V1", 3, pre_init_done, user_data);
}

static void
do_enable (MMGenericGsm *modem, MMModemFn callback, gpointer user_data)
{
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (modem);
    MMCallbackInfo *info;
    MMAtSerialPort *primary;

    priv->init_retried = FALSE;

    primary = mm_generic_gsm_get_at_port (modem, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_flash (MM_SERIAL_PORT (primary), 100, FALSE, enable_flash_done, info);
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (modem);
    MMModem *parent_modem_iface;

    priv->init_retried = FALSE;

    /* Do the normal disable stuff */
    parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));
    parent_modem_iface->disable (modem, callback, user_data);
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
        GRegex *regex;

        g_object_set (port, MM_PORT_CARRIER_DETECT, FALSE, NULL);

        regex = g_regex_new ("\\r\\n\\+ZUSIMR:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* Unsolicted operator display */
        regex = g_regex_new ("\\r\\n\\+ZDONR: (.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* Current network and service domain */
        regex = g_regex_new ("\\r\\n\\+ZPASR:\\s*(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, zte_access_tech_changed, modem, NULL);
        g_regex_unref (regex);

        /* SIM request to Build Main Menu */
        regex = g_regex_new ("\\r\\n\\+ZPSTM: (.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        /* SIM request to Rebuild Main Menu */
        regex = g_regex_new ("\\r\\n\\+ZEND\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_at_serial_port_add_unsolicited_msg_handler (MM_AT_SERIAL_PORT (port), regex, NULL, NULL, NULL);
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
mm_modem_zte_init (MMModemZte *self)
{
}

static void
dispose (GObject *object)
{
    MMModemZte *self = MM_MODEM_ZTE (object);
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (self);

    if (priv->cpms_timeout)
        g_source_remove (priv->cpms_timeout);
}

static void
mm_modem_zte_class_init (MMModemZteClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_zte_parent_class = g_type_class_peek_parent (klass);
    g_type_class_add_private (object_class, sizeof (MMModemZtePrivate));

    object_class->dispose = dispose;
    gsm_class->do_enable = do_enable;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

