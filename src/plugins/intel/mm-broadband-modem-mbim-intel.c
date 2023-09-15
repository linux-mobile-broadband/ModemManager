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
#include "mm-iface-modem-location.h"
#include "mm-shared-xmm.h"

static void iface_modem_location_init (MMIfaceModemLocation *iface);
static void shared_xmm_init           (MMSharedXmm          *iface);

static MMIfaceModemLocation *iface_modem_location_parent;

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMbimIntel, mm_broadband_modem_mbim_intel, MM_TYPE_BROADBAND_MODEM_MBIM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_SHARED_XMM,  shared_xmm_init))

/*****************************************************************************/

static void
setup_ports (MMBroadbandModem *self)
{
    MMPortSerialAt *ports[3];
    guint           i;

    /* Run the shared XMM port setup logic */
    mm_shared_xmm_setup_ports (self);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* GNSS control port may or may not be a primary/secondary port */
    ports[2] = mm_base_modem_peek_port_gps_control (MM_BASE_MODEM (self));
    if (ports[2] && ((ports[2] == ports[0]) || (ports[2] == ports[1])))
        ports[2] = NULL;

    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        g_object_set (ports[i],
                      MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                      NULL);
    }
}

/*****************************************************************************/

MMBroadbandModemMbimIntel *
mm_broadband_modem_mbim_intel_new (const gchar  *device,
                                   const gchar  *physdev,
                                   const gchar **drivers,
                                   const gchar  *plugin,
                                   guint16       vendor_id,
                                   guint16       product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MBIM_INTEL,
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
mm_broadband_modem_mbim_intel_init (MMBroadbandModemMbimIntel *self)
{
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
    return MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_mbim_intel_parent_class);
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
mm_broadband_modem_mbim_intel_class_init (MMBroadbandModemMbimIntelClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
