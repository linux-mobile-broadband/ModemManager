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
 * Copyright (C) 2023 Google, Inc.
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-broadband-modem-mbim-mtk.h"

G_DEFINE_TYPE (MMBroadbandModemMbimMtk, mm_broadband_modem_mbim_mtk, MM_TYPE_BROADBAND_MODEM_MBIM)

/******************************************************************************/

MMBroadbandModemMbimMtk *
mm_broadband_modem_mbim_mtk_new (const gchar  *device,
                                 const gchar  *physdev,
                                 const gchar **drivers,
                                 const gchar  *plugin,
                                 guint16       vendor_id,
                                 guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_MTK,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_PHYSDEV,    physdev,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED, TRUE,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                         MM_BROADBAND_MODEM_MBIM_QMI_UNSUPPORTED, TRUE,
#endif
                         NULL);
}

static void
mm_broadband_modem_mbim_mtk_init (MMBroadbandModemMbimMtk *self)
{
}

static void
mm_broadband_modem_mbim_mtk_class_init (MMBroadbandModemMbimMtkClass *klass)
{
}
