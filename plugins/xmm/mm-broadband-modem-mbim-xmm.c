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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-broadband-modem-mbim-xmm.h"
#include "mm-shared-xmm.h"

static void iface_modem_init          (MMIfaceModem         *iface);
static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void shared_xmm_init           (MMSharedXmm          *iface);

static MMIfaceModemLocation *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimXmm, mm_broadband_modem_mbim_xmm, MM_TYPE_BROADBAND_MODEM_MBIM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_XMM,  shared_xmm_init))

/*****************************************************************************/

MMBroadbandModemMbimXmm *
mm_broadband_modem_mbim_xmm_new (const gchar  *device,
                                 const gchar **drivers,
                                 const gchar  *plugin,
                                 guint16       vendor_id,
                                 guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_XMM,
                         MM_BASE_MODEM_DEVICE,     device,
                         MM_BASE_MODEM_DRIVERS,    drivers,
                         MM_BASE_MODEM_PLUGIN,     plugin,
                         MM_BASE_MODEM_VENDOR_ID,  vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED, TRUE,
                         MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED, FALSE,
                         MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED, TRUE,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                         MM_BROADBAND_MODEM_MBIM_QMI_UNSUPPORTED, TRUE,
#endif
                         NULL);
}

static void
mm_broadband_modem_mbim_xmm_init (MMBroadbandModemMbimXmm *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->load_supported_modes        = mm_shared_xmm_load_supported_modes;
    iface->load_supported_modes_finish = mm_shared_xmm_load_supported_modes_finish;
    iface->load_current_modes          = mm_shared_xmm_load_current_modes;
    iface->load_current_modes_finish   = mm_shared_xmm_load_current_modes_finish;
    iface->set_current_modes           = mm_shared_xmm_set_current_modes;
    iface->set_current_modes_finish    = mm_shared_xmm_set_current_modes_finish;

    iface->load_supported_bands        = mm_shared_xmm_load_supported_bands;
    iface->load_supported_bands_finish = mm_shared_xmm_load_supported_bands_finish;
    iface->load_current_bands          = mm_shared_xmm_load_current_bands;
    iface->load_current_bands_finish   = mm_shared_xmm_load_current_bands_finish;
    iface->set_current_bands           = mm_shared_xmm_set_current_bands;
    iface->set_current_bands_finish    = mm_shared_xmm_set_current_bands_finish;

    /* power up/down already managed via MBIM */
    iface->modem_power_off         = mm_shared_xmm_power_off;
    iface->modem_power_off_finish  = mm_shared_xmm_power_off_finish;
    iface->reset                   = mm_shared_xmm_reset;
    iface->reset_finish            = mm_shared_xmm_reset_finish;
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);

    iface->load_capabilities                 = mm_shared_xmm_location_load_capabilities;
    iface->load_capabilities_finish          = mm_shared_xmm_location_load_capabilities_finish;
    iface->enable_location_gathering         = mm_shared_xmm_enable_location_gathering;
    iface->enable_location_gathering_finish  = mm_shared_xmm_enable_location_gathering_finish;
    iface->disable_location_gathering        = mm_shared_xmm_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_shared_xmm_disable_location_gathering_finish;
    iface->load_supl_server                  = mm_shared_xmm_location_load_supl_server;
    iface->load_supl_server_finish           = mm_shared_xmm_location_load_supl_server_finish;
    iface->set_supl_server                   = mm_shared_xmm_location_set_supl_server;
    iface->set_supl_server_finish            = mm_shared_xmm_location_set_supl_server_finish;
}

static MMBroadbandModemClass *
peek_parent_broadband_modem_class (MMSharedXmm *self)
{
    return MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_xmm_parent_class);
}

static MMIfaceModemLocation *
peek_parent_location_interface (MMSharedXmm *self)
{
    return iface_modem_location_parent;
}

static void
shared_xmm_init (MMSharedXmm *iface)
{
    iface->peek_parent_broadband_modem_class = peek_parent_broadband_modem_class;
    iface->peek_parent_location_interface    = peek_parent_location_interface;
}

static void
mm_broadband_modem_mbim_xmm_class_init (MMBroadbandModemMbimXmmClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = mm_shared_xmm_setup_ports;
}
