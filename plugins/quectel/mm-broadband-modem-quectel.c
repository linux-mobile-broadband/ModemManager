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
 * Copyright (C) 2018-2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include "mm-broadband-modem-quectel.h"
#include "mm-iface-modem-firmware.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-time.h"
#include "mm-shared-quectel.h"

static void iface_modem_init          (MMIfaceModem         *iface);
static void iface_modem_firmware_init (MMIfaceModemFirmware *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void iface_modem_time_init     (MMIfaceModemTime     *iface);
static void shared_quectel_init       (MMSharedQuectel      *iface);

static MMIfaceModem         *iface_modem_parent;
static MMIfaceModemLocation *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQuectel, mm_broadband_modem_quectel, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_FIRMWARE, iface_modem_firmware_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_TIME, iface_modem_time_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_QUECTEL, shared_quectel_init))

/*****************************************************************************/

MMBroadbandModemQuectel *
mm_broadband_modem_quectel_new (const gchar  *device,
                                const gchar **drivers,
                                const gchar  *plugin,
                                guint16       vendor_id,
                                guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QUECTEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_quectel_init (MMBroadbandModemQuectel *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->setup_sim_hot_swap = mm_shared_quectel_setup_sim_hot_swap;
    iface->setup_sim_hot_swap_finish = mm_shared_quectel_setup_sim_hot_swap_finish;
}

static MMIfaceModem *
peek_parent_modem_interface (MMSharedQuectel *self)
{
    return iface_modem_parent;
}

static MMBroadbandModemClass *
peek_parent_broadband_modem_class (MMSharedQuectel *self)
{
    return MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_quectel_parent_class);
}

static void
iface_modem_firmware_init (MMIfaceModemFirmware *iface)
{
    iface->load_update_settings = mm_shared_quectel_firmware_load_update_settings;
    iface->load_update_settings_finish = mm_shared_quectel_firmware_load_update_settings_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities                 = mm_shared_quectel_location_load_capabilities;
    iface->load_capabilities_finish          = mm_shared_quectel_location_load_capabilities_finish;
    iface->enable_location_gathering         = mm_shared_quectel_enable_location_gathering;
    iface->enable_location_gathering_finish  = mm_shared_quectel_enable_location_gathering_finish;
    iface->disable_location_gathering        = mm_shared_quectel_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_quectel_disable_location_gathering_finish;
}

static MMIfaceModemLocation *
peek_parent_modem_location_interface (MMSharedQuectel *self)
{
    return iface_modem_location_parent;
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
    iface->peek_parent_modem_interface          = peek_parent_modem_interface;
    iface->peek_parent_modem_location_interface = peek_parent_modem_location_interface;
    iface->peek_parent_broadband_modem_class    = peek_parent_broadband_modem_class;
}

static void
mm_broadband_modem_quectel_class_init (MMBroadbandModemQuectelClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = mm_shared_quectel_setup_ports;
}
