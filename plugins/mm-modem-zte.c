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
/*    Modem class override functions                                         */
/*****************************************************************************/

static void
init_modem_done (MMSerialPort *port,
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
pin_check_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMSerialPort *primary;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        /* Finish the initialization */
        primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
        g_assert (primary);
        mm_serial_port_queue_command (primary, "Z E0 V1 X4 &C1 +CMEE=1;+CFUN=1;", 10, init_modem_done, info);
    }
}

static void
pre_init_done (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
    } else {
        /* Now check the PIN explicitly, zte doesn't seem to report
           that it needs it otherwise */
        mm_generic_gsm_check_pin (MM_GENERIC_GSM (info->modem), pin_check_done, info);
    }
}

static void
enable_flash_done (MMSerialPort *port, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        info->error = g_error_copy (error);
        mm_callback_info_schedule (info);
        return;
    }

    mm_serial_port_queue_command (port, "E0 V1", 3, pre_init_done, user_data);
}

static void
enable (MMModem *modem,
        MMModemFn callback,
        gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    /* First, reset the previously used CID */
    mm_generic_gsm_set_cid (MM_GENERIC_GSM (modem), 0);

    info = mm_callback_info_new (modem, callback, user_data);

    primary = mm_generic_gsm_get_port (MM_GENERIC_GSM (modem), MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    if (!mm_serial_port_open (primary, &info->error)) {
        g_assert (info->error);
        mm_callback_info_schedule (info);
        return;
    }

    mm_serial_port_flash (primary, 100, enable_flash_done, info);
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
        if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_PRIMARY))
                ptype = MM_PORT_TYPE_PRIMARY;
        else if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_SECONDARY))
            ptype = MM_PORT_TYPE_SECONDARY;
    } else
        ptype = suggested_type;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (port && MM_IS_SERIAL_PORT (port)) {
        GRegex *regex;

        mm_generic_gsm_set_unsolicited_registration (gsm, TRUE);
        g_object_set (port, MM_PORT_CARRIER_DETECT, FALSE, NULL);

        regex = g_regex_new ("\\r\\n\\+ZUSIMR:(.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+ZDONR: (.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+ZPASR: (.*)\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);

        regex = g_regex_new ("\\r\\n\\+ZEND\\r\\n", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        mm_serial_port_add_unsolicited_msg_handler (MM_SERIAL_PORT (port), regex, NULL, NULL, NULL);
        g_regex_unref (regex);
    }

    return !!port;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->enable = enable;
    modem_class->grab_port = grab_port;
}

static void
mm_modem_zte_init (MMModemZte *self)
{
}

static void
mm_modem_zte_class_init (MMModemZteClass *klass)
{
    mm_modem_zte_parent_class = g_type_class_peek_parent (klass);
}

