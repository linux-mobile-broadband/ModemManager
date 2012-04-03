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
 * Copyright (C) 2011 Ammonit Measurement GmbH
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <libmm-common.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-broadband-modem-wavecom.h"

G_DEFINE_TYPE (MMBroadbandModemWavecom, mm_broadband_modem_wavecom, MM_TYPE_BROADBAND_MODEM);

/*****************************************************************************/

MMBroadbandModemWavecom *
mm_broadband_modem_wavecom_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_WAVECOM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_wavecom_init (MMBroadbandModemWavecom *self)
{
}

static void
mm_broadband_modem_wavecom_class_init (MMBroadbandModemWavecomClass *klass)
{
}
