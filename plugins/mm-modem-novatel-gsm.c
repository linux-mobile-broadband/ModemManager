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
#include "mm-modem-novatel-gsm.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemNovatelGsm, mm_modem_novatel_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))


MMModem *
mm_modem_novatel_gsm_new (const char *device,
                          const char *driver,
                          const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_NOVATEL_GSM,
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

    mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
}

static void
pin_check_done (MMModem *modem, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;
    MMGenericGsm *self = MM_GENERIC_GSM (modem);
    MMSerialPort *primary;

    if (error) {
        mm_generic_gsm_enable_complete (self, error, info);
        return;
    }

    /* Finish the initialization */
    primary = mm_generic_gsm_get_port (self, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);
    mm_serial_port_queue_command (primary, "Z E0 V1 X4 &C1 +CMEE=1;+CFUN=1", 10, init_modem_done, info);
}

static void
pre_init_done (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error) {
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
        return;
    }

    /* Now check the PIN explicitly, novatel doesn't seem to report
     * that it needs it otherwise.
     */
    mm_generic_gsm_check_pin (MM_GENERIC_GSM (info->modem), pin_check_done, info);
}

static void
enable_flash_done (MMSerialPort *port, GError *error, gpointer user_data)
{
    MMCallbackInfo *info = user_data;

    if (error)
        mm_generic_gsm_enable_complete (MM_GENERIC_GSM (info->modem), error, info);
    else
        mm_serial_port_queue_command (port, "E0 V1", 3, pre_init_done, user_data);
}

static void
do_enable (MMGenericGsm *modem, MMModemFn callback, gpointer user_data)
{
    MMCallbackInfo *info;
    MMSerialPort *primary;

    primary = mm_generic_gsm_get_port (modem, MM_PORT_TYPE_PRIMARY);
    g_assert (primary);

    info = mm_callback_info_new (MM_MODEM (modem), callback, user_data);
    mm_serial_port_flash (primary, 100, enable_flash_done, info);
}

static void
dmat_callback (MMSerialPort *port,
               GString *response,
               GError *error,
               gpointer user_data)
{
    mm_serial_port_close (port);
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
    if (port && MM_IS_SERIAL_PORT (port) && (ptype == MM_PORT_TYPE_PRIMARY)) {
        /* Flip secondary ports to AT mode */
        if (mm_serial_port_open (MM_SERIAL_PORT (port), NULL))
            mm_serial_port_queue_command (MM_SERIAL_PORT (port), "$NWDMAT=1", 2, dmat_callback, NULL);
    }

    return !!port;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
}

static void
mm_modem_novatel_gsm_init (MMModemNovatelGsm *self)
{
}

static void
mm_modem_novatel_gsm_class_init (MMModemNovatelGsmClass *klass)
{
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_novatel_gsm_parent_class = g_type_class_peek_parent (klass);

    gsm_class->do_enable = do_enable;
}

