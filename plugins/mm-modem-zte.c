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
#include "mm-modem-zte.h"
#include "mm-serial-port.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

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

static void
zte_access_tech_changed (MMAtSerialPort *port,
                         GMatchInfo *info,
                         gpointer user_data)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
    char *str;

    str = g_match_info_fetch (info, 1);
    if (str) {
        /* Better technologies are listed first since modem sometimes says
         * stuff like "GPRS/EDGE" and that should be handled as EDGE.
         */
        if (strstr (str, "HSPA"))
            act = MM_MODEM_GSM_ACCESS_TECH_HSPA;
        else if (strstr (str, "HSUPA"))
            act = MM_MODEM_GSM_ACCESS_TECH_HSUPA;
        else if (strstr (str, "HSDPA"))
            act = MM_MODEM_GSM_ACCESS_TECH_HSDPA;
        else if (strstr (str, "UMTS"))
            act = MM_MODEM_GSM_ACCESS_TECH_UMTS;
        else if (strstr (str, "EDGE"))
            act = MM_MODEM_GSM_ACCESS_TECH_EDGE;
        else if (strstr (str, "GPRS"))
            act = MM_MODEM_GSM_ACCESS_TECH_GPRS;
        else if (strstr (str, "GSM"))
            act = MM_MODEM_GSM_ACCESS_TECH_GSM;
    }
    g_free (str);

    mm_generic_gsm_update_access_technology (MM_GENERIC_GSM (user_data), act);
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
    MMModemZtePrivate *priv = MM_MODEM_ZTE_GET_PRIVATE (modem);
    MMAtSerialPort *primary;

    priv->cpms_timeout = 0;

    primary = mm_generic_gsm_get_at_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    mm_at_serial_port_queue_command (primary, "+CPMS?", 10, cpms_try_done, info);
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
            && g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_RESPONSE_TIMEOUT)) {
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
    mm_serial_port_flash (MM_SERIAL_PORT (primary), 100, enable_flash_done, info);
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
}

