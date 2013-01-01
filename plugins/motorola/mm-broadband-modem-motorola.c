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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-broadband-modem-motorola.h"

static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_init (MMIfaceModem3gpp *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemMotorola, mm_broadband_modem_motorola, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM_3GPP, iface_modem_3gpp_init));

/*****************************************************************************/

MMBroadbandModemMotorola *
mm_broadband_modem_motorola_new (const gchar *device,
                                 const gchar **drivers,
                                 const gchar *plugin,
                                 guint16 vendor_id,
                                 guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_MOTOROLA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_motorola_init (MMBroadbandModemMotorola *self)
{
}

static void
iface_modem_3gpp_init (MMIfaceModem3gpp *iface)
{
    /* Loading IMEI not supported */
    iface->load_imei = NULL;
    iface->load_imei_finish = NULL;
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Loading IMEI with +CGSN is not supported, just assume we cannot load
     * equipment ID */
    iface->load_equipment_identifier = NULL;
    iface->load_equipment_identifier_finish = NULL;

    /* These devices just don't implement AT+CFUN */
    iface->load_power_state = NULL;
    iface->load_power_state_finish = NULL;
    iface->modem_power_up = NULL;
    iface->modem_power_up_finish = NULL;
    iface->modem_power_down = NULL;
    iface->modem_power_down_finish = NULL;
}

static void
mm_broadband_modem_motorola_class_init (MMBroadbandModemMotorolaClass *klass)
{
}
