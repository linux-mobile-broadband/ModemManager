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

#include "mm-modem-gobi-gsm.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-modem-gsm-card.h"
#include "mm-at-serial-port.h"

static void modem_init (MMModem *modem_class);
static void modem_gsm_card_init (MMModemGsmCard *gsm_card_class);

G_DEFINE_TYPE_EXTENDED (MMModemGobiGsm, mm_modem_gobi_gsm, MM_TYPE_GENERIC_GSM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM_GSM_CARD, modem_gsm_card_init))


MMModem *
mm_modem_gobi_gsm_new (const char *device,
                       const char *driver,
                       const char *plugin,
                       guint32 vendor,
                       guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_GOBI_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

/*****************************************************************************/

static void
get_string_done (MMAtSerialPort *port,
                 GString *response,
                 GError *error,
                 gpointer user_data)
{
    MMCallbackInfo *info = (MMCallbackInfo *) user_data;

    if (error && response && !strcmp (response->str, "ERROR")) {
        info->error = g_error_new_literal (MM_MOBILE_ERROR,
                                           MM_MOBILE_ERROR_SIM_NOT_INSERTED,
                                           "Unable to read IMSI");
    } else if (error)
        info->error = g_error_copy (error);
    else
        mm_callback_info_set_result (info, g_strdup (response->str), g_free);

    mm_callback_info_schedule (info);
}

static void
get_imsi (MMModemGsmCard *modem,
          MMModemStringFn callback,
          gpointer user_data)
{
    MMAtSerialPort *port;
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (modem), callback, user_data);

    port = mm_generic_gsm_get_best_at_port (MM_GENERIC_GSM (modem), &info->error);
    if (port)
        mm_at_serial_port_queue_command_cached (port, "+CIMI", 3, get_string_done, info);
    else
        mm_callback_info_schedule (info);
}

static void
modem_gsm_card_init (MMModemGsmCard *class)
{
    class->get_imsi = get_imsi;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
}

static void
mm_modem_gobi_gsm_init (MMModemGobiGsm *self)
{
}

static void
mm_modem_gobi_gsm_class_init (MMModemGobiGsmClass *klass)
{
}

