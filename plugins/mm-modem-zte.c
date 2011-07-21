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
#include "mm-modem-simple.h"
#include "mm-modem-icera.h"
#include "mm-modem-gsm-ussd.h"

static void modem_init (MMModem *modem_class);
static void modem_icera_init (MMModemIcera *icera_class);
static void modem_simple_init (MMModemSimple *simple_class);
static void modem_gsm_ussd_init (MMModemGsmUssd *ussd_class);

G_DEFINE_TYPE_EXTENDED (MMModemZte, mm_modem_zte, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_ICERA, modem_icera_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_SIMPLE, modem_simple_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_USSD, modem_gsm_ussd_init)
)

#define MM_MODEM_ZTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_ZTE, MMModemZtePrivate))

typedef struct {
    gboolean disposed;
    gboolean init_retried;
    guint32 cpms_tries;
    guint cpms_timeout;
    gboolean is_icera;
    MMModemIceraPrivate *icera;
} MMModemZtePrivate;

MMModem *
mm_modem_zte_new (const char *device,
                  const char *driver,
                  const char *plugin,
                  guint32 vendor,
                  guint32 product)
{
    MMModem *modem;

    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    modem = MM_MODEM (g_object_new (MM_TYPE_MODEM_ZTE,
                                    MM_MODEM_MASTER_DEVICE, device,
                                    MM_MODEM_DRIVER, driver,
                                    MM_MODEM_PLUGIN, plugin,
                                    MM_MODEM_HW_VID, vendor,
                                    MM_MODEM_HW_PID, product,
                                    NULL));
    if (modem)
        MM_MODEM_ZTE_GET_PRIVATE (modem)->icera = mm_modem_icera_init_private ();

    return modem;
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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    if (error) {
        info->error = g_error_copy (error);
        goto done;
    }

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
    MMModemZte *self = MM_MODEM_ZTE (gsm);
    MMCallbackInfo *info;
    MMAtSerialPort *port;

    if (MM_MODEM_ZTE_GET_PRIVATE (self)->is_icera) {
        mm_modem_icera_get_allowed_mode (MM_MODEM_ICERA (self), callback, user_data);
        return;
    }

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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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
    MMModemZte *self = MM_MODEM_ZTE (gsm);
    MMCallbackInfo *info;
    MMAtSerialPort *port;
    char *command;
    int cm_mode = 0, pref_acq = 0;

    if (MM_MODEM_ZTE_GET_PRIVATE (self)->is_icera) {
        mm_modem_icera_set_allowed_mode (MM_MODEM_ICERA (self), mode, callback, user_data);
        return;
    }

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

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

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
get_access_technology (MMGenericGsm *gsm,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMModemZte *self = MM_MODEM_ZTE (gsm);
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    if (MM_MODEM_ZTE_GET_PRIVATE (self)->is_icera) {
        mm_modem_icera_get_access_technology (MM_MODEM_ICERA (self), callback, user_data);
        return;
    }

    info = mm_callback_info_uint_new (MM_MODEM (gsm), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (gsm, &info->error);
    if (!port) {
        mm_callback_info_schedule (info);
        return;
    }

    mm_at_serial_port_queue_command (port, "+ZPAS?", 3, get_act_request_done, info);
}

static void
do_disconnect (MMGenericGsm *gsm,
               gint cid,
               MMModemFn callback,
               gpointer user_data)
{
    MMModemZte *self = MM_MODEM_ZTE (gsm);
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (self);

    if (priv->is_icera)
        mm_modem_icera_do_disconnect (gsm, cid, callback, user_data);
    else
        MM_GENERIC_GSM_CLASS (mm_modem_zte_parent_class)->do_disconnect (gsm, cid, callback, user_data);
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
    MMModemZtePrivate *priv;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    priv = MM_MODEM_ZTE_GET_PRIVATE (info->modem);

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

    /* Turn on unsolicited network state messages */
    if (priv->is_icera)
        mm_modem_icera_change_unsolicited_messages (MM_MODEM_ICERA (info->modem), TRUE);

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
}

static void
init_modem_done (MMAtSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    mm_at_serial_port_queue_command (port, "E0", 5, NULL, NULL);

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
icera_check_cb (MMModem *modem,
                guint32 result,
                GError *error,
                gpointer user_data)
{
    if (!error) {
        MMModemZte *self = MM_MODEM_ZTE (user_data);
        MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (self);

        if (result) {
            priv->is_icera = TRUE;
            g_object_set (G_OBJECT (modem),
                          MM_MODEM_IP_METHOD, MM_MODEM_IP_METHOD_STATIC,
                          NULL);
        }
    }
}

static void
pre_init_done (MMAtSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemZte *self;
    MMModemZtePrivate *priv;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    self = MM_MODEM_ZTE (info->modem);
    priv = MM_MODEM_ZTE_GET_PRIVATE (self);

    if (error) {
        /* Retry the init string one more time; the modem sometimes throws it away */
        if (   !priv->init_retried
            && g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            priv->init_retried = TRUE;
            enable_flash_done (MM_SERIAL_PORT (port), NULL, user_data);
        } else
            mm_generic_gsm_enable_complete (MM_GENERIC_GSM (self), error, info);
    } else {
        /* Finish the initialization */
        mm_modem_icera_is_icera (MM_MODEM_ICERA (self), icera_check_cb, self);
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

/*****************************************************************************/

static void
disable_unsolicited_done (MMAtSerialPort *port,
                          GString *response,
                          GError *error,
                          gpointer user_data)

{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    /* If the modem has already been removed, return without
     * scheduling callback */
    if (mm_callback_info_check_modem_removed (info))
        return;

    /* Ignore all errors */
    mm_callback_info_schedule (info);
}

static void
invoke_call_parent_disable_fn (MMCallbackInfo *info)
{
    /* Note: we won't call the parent disable if info->modem is no longer
     * valid. The invoke is called always once the info gets scheduled, which
     * may happen during removed modem detection. */
    if (info->modem) {
        MMModem *parent_modem_iface;

        parent_modem_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (info->modem));
        parent_modem_iface->disable (info->modem, (MMModemFn)info->callback, info->user_data);
    }
}

static void
disable (MMModem *modem,
         MMModemFn callback,
         gpointer user_data)
{
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (modem);
    MMAtSerialPort *primary;
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (modem,
                                      invoke_call_parent_disable_fn,
                                      (GCallback)callback,
                                      user_data);

    priv->init_retried = FALSE;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    /* Turn off unsolicited responses */
    if (priv->is_icera) {
        mm_modem_icera_cleanup (MM_MODEM_ICERA (modem));
        mm_modem_icera_change_unsolicited_messages (MM_MODEM_ICERA (modem), FALSE);
    }

    /* Random command to ensure unsolicited message disable completes */
    mm_at_serial_port_queue_command (primary, "E0", 5, disable_unsolicited_done, info);
}

/*****************************************************************************/

static void
do_connect (MMModem *modem,
            const char *number,
            MMModemFn callback,
            gpointer user_data)
{
    MMModem *parent_iface;

    if (MM_MODEM_ZTE_GET_PRIVATE (modem)->is_icera)
        mm_modem_icera_do_connect (MM_MODEM_ICERA (modem), number, callback, user_data);
    else {
        parent_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));
        parent_iface->connect (MM_MODEM (modem), number, callback, user_data);
    }
}

static void
get_ip4_config (MMModem *modem,
                MMModemIp4Fn callback,
                gpointer user_data)
{
    MMModem *parent_iface;

    if (MM_MODEM_ZTE_GET_PRIVATE (modem)->is_icera) {
        mm_modem_icera_get_ip4_config (MM_MODEM_ICERA (modem), callback, user_data);
    } else {
        parent_iface = g_type_interface_peek_parent (MM_MODEM_GET_INTERFACE (modem));
        parent_iface->get_ip4_config (MM_MODEM (modem), callback, user_data);
    }
}

/*****************************************************************************/

static void
simple_connect (MMModemSimple *simple,
                GHashTable *properties,
                MMModemFn callback,
                gpointer user_data)
{
    MMModemZte *self = MM_MODEM_ZTE (simple);
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (self);
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMModemSimple *parent_iface;

    if (priv->is_icera)
        mm_modem_icera_simple_connect (MM_MODEM_ICERA (self), properties);

    parent_iface = g_type_interface_peek_parent (MM_MODEM_SIMPLE_GET_INTERFACE (simple));
    parent_iface->connect (MM_MODEM_SIMPLE (simple), properties, callback, info);
}

/*****************************************************************************/

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

        /* Add Icera-specific handlers */
        mm_modem_icera_register_unsolicted_handlers (MM_MODEM_ICERA (gsm), MM_AT_SERIAL_PORT (port));
    }

    return !!port;
}

/*****************************************************************************/

static MMModemIceraPrivate *
get_icera_private (MMModemIcera *icera)
{
    return MM_MODEM_ZTE_GET_PRIVATE (icera)->icera;
}

/*****************************************************************************/

static char*
ussd_encode (MMModemGsmUssd *self, const char* command, guint *scheme)
{
    char *cmd;

    *scheme = MM_MODEM_GSM_USSD_SCHEME_7BIT;
    cmd = g_strdup (command);

    return cmd;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->disable = disable;
    modem_class->connect = do_connect;
    modem_class->get_ip4_config = get_ip4_config;
    modem_class->grab_port = grab_port;
}

static void
modem_icera_init (MMModemIcera *icera)
{
    icera->get_private = get_icera_private;
}

static void
modem_simple_init (MMModemSimple *class)
{
    class->connect = simple_connect;
}

static void
mm_modem_zte_init (MMModemZte *self)
{
}

static void
modem_gsm_ussd_init (MMModemGsmUssd *ussd_class)
{
    ussd_class->encode = ussd_encode;
}

static void
dispose (GObject *object)
{
    MMModemZte *self = MM_MODEM_ZTE (object);
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (self);

    if (priv->disposed == FALSE) {
        priv->disposed = TRUE;

        if (priv->cpms_timeout)
            g_source_remove (priv->cpms_timeout);

        mm_modem_icera_dispose_private (MM_MODEM_ICERA (self));
    }

    G_OBJECT_CLASS (mm_modem_zte_parent_class)->dispose (object);
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
    gsm_class->do_disconnect = do_disconnect;
    gsm_class->set_allowed_mode = set_allowed_mode;
    gsm_class->get_allowed_mode = get_allowed_mode;
    gsm_class->get_access_technology = get_access_technology;
}

