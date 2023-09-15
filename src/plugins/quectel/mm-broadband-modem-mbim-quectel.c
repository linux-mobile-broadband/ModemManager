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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 */

#include <config.h>

#include "mm-base-modem-at.h"
#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-time.h"
#include "mm-shared-quectel.h"
#include "mm-modem-helpers-quectel.h"
#include "mm-broadband-modem-mbim-quectel.h"

static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);
static void iface_modem_time_init     (MMIfaceModemTime     *iface);
static void shared_quectel_init       (MMSharedQuectel      *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimQuectel, mm_broadband_modem_mbim_quectel, MM_TYPE_BROADBAND_MODEM_MBIM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QUECTEL, shared_quectel_init))

/*****************************************************************************/

MMBroadbandModemMbimQuectel *
mm_broadband_modem_mbim_quectel_new (const gchar  *device,
                                     const gchar  *physdev,
                                     const gchar **drivers,
                                     const gchar  *plugin,
                                     guint16       vendor_id,
                                     guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_QUECTEL,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_PHYSDEV,    physdev,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* include carrier information */
                         MM_IFACE_MODEM_FIRMWARE_IGNORE_CARRIER, FALSE,
                         /* MBIM bearer supports NET only */
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         NULL);
}

static void
mm_broadband_modem_mbim_quectel_init (MMBroadbandModemMbimQuectel *self)
{
}

static void
iface_modem_firmware_init (MMIfaceModemFirmware *iface)
{
    iface->load_update_settings        = mm_shared_quectel_firmware_load_update_settings;
    iface->load_update_settings_finish = mm_shared_quectel_firmware_load_update_settings_finish;
}

static void
iface_modem_time_init (MMIfaceModemTime *iface)
{
    iface->check_support        = mm_shared_quectel_time_check_support;
    iface->check_support_finish = mm_shared_quectel_time_check_support_finish;
}

static void
shared_quectel_init (MMSharedQuectel *iface)
{
}

static void
mm_broadband_modem_mbim_quectel_class_init (MMBroadbandModemMbimQuectelClass *klass)
{
}
