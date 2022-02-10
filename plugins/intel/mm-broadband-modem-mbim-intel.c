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
 * Copyright (C) 2021-2022 Intel Corporation
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-modem-mbim-intel.h"

G_DEFINE_TYPE (MMBroadbandModemMbimIntel, mm_broadband_modem_mbim_intel, MM_TYPE_BROADBAND_MODEM_MBIM)

/*****************************************************************************/

MMBroadbandModemMbimIntel *
mm_broadband_modem_mbim_intel_new (const gchar  *device,
                                   const gchar **drivers,
                                   const gchar  *plugin,
                                   guint16       vendor_id,
                                   guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_INTEL,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         NULL);
}

static void
mm_broadband_modem_mbim_intel_init (MMBroadbandModemMbimIntel *self)
{
}

static void
mm_broadband_modem_mbim_intel_class_init (MMBroadbandModemMbimIntelClass *klass)
{
}
