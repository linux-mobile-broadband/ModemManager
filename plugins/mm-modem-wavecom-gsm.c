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
 * Copyright (C) 2011 Ammonit Gesellschaft für Messtechnik mbH
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mm-modem-wavecom-gsm.h"
#include "mm-errors.h"
#include "mm-modem-simple.h"
#include "mm-callback-info.h"
#include "mm-modem-helpers.h"
#include "mm-at-serial-port.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemWavecomGsm,
                        mm_modem_wavecom_gsm,
                        MM_TYPE_GENERIC_GSM,
                        0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

MMModem *
mm_modem_wavecom_gsm_new (const char *device,
                          const char *driver,
                          const char *plugin,
                          guint32 vendor,
                          guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_WAVECOM_GSM,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
}

static void
mm_modem_wavecom_gsm_init (MMModemWavecomGsm *self)
{
}

static void
mm_modem_wavecom_gsm_class_init (MMModemWavecomGsmClass *klass)
{
}

