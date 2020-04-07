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
 * Copyright (C) 2011 Samsung Electronics, Inc.
 * Copyright (C) 2012 Google Inc.
 * Author: Nathan Williams <njw@google.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-samsung.h"
#include "mm-broadband-bearer-icera.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMBroadbandModemSamsung, mm_broadband_modem_samsung, MM_TYPE_BROADBAND_MODEM_ICERA)

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    MMPortSerialAt *ports[2];
    guint i;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_samsung_parent_class)->setup_ports (self);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Configure AT ports */
    for (i = 0; i < G_N_ELEMENTS (ports); i++) {
        if (!ports[i])
            continue;

        g_object_set (ports[i],
                      MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                      NULL);
    }
}

/*****************************************************************************/

MMBroadbandModemSamsung *
mm_broadband_modem_samsung_new (const gchar *device,
                                const gchar **drivers,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_SAMSUNG,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         /* We want to use DHCP always! */
                         MM_BROADBAND_MODEM_ICERA_DEFAULT_IP_METHOD, MM_BEARER_IP_METHOD_DHCP,
                         NULL);
}

static void
mm_broadband_modem_samsung_init (MMBroadbandModemSamsung *self)
{
}

static void
mm_broadband_modem_samsung_class_init (MMBroadbandModemSamsungClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
