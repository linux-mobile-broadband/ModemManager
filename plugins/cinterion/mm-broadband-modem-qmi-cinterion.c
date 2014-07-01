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
 * Copyright (C) 2014 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-iface-modem-location.h"
#include "mm-broadband-modem-qmi-cinterion.h"
#include "mm-common-cinterion.h"

static void iface_modem_location_init (MMIfaceModemLocation *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmiCinterion, mm_broadband_modem_qmi_cinterion, MM_TYPE_BROADBAND_MODEM_QMI, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_LOCATION, iface_modem_location_init))

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_cinterion_parent_class)->setup_ports (self);

    mm_common_cinterion_setup_gps_port (self);
}

/*****************************************************************************/

MMBroadbandModemQmiCinterion *
mm_broadband_modem_qmi_cinterion_new (const gchar *device,
                                      const gchar **drivers,
                                      const gchar *plugin,
                                      guint16 vendor_id,
                                      guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI_CINTERION,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_qmi_cinterion_init (MMBroadbandModemQmiCinterion *self)
{
}

static void
iface_modem_location_init (MMIfaceModemLocation *iface)
{
    mm_common_cinterion_peek_parent_location_interface (iface);

    iface->load_capabilities = mm_common_cinterion_location_load_capabilities;
    iface->load_capabilities_finish = mm_common_cinterion_location_load_capabilities_finish;
    iface->enable_location_gathering = mm_common_cinterion_enable_location_gathering;
    iface->enable_location_gathering_finish = mm_common_cinterion_enable_location_gathering_finish;
    iface->disable_location_gathering = mm_common_cinterion_disable_location_gathering;
    iface->disable_location_gathering_finish = mm_common_cinterion_disable_location_gathering_finish;
}

static void
mm_broadband_modem_qmi_cinterion_class_init (MMBroadbandModemQmiCinterionClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    /* Virtual methods */
    broadband_modem_class->setup_ports = setup_ports;
}
