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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-base-modem-at.h"
#include "mm-common-zte.h"
#include "mm-broadband-modem-zte-icera.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBroadbandModemZteIcera, mm_broadband_modem_zte_icera, MM_TYPE_BROADBAND_MODEM_ICERA);

struct _MMBroadbandModemZteIceraPrivate {
    /* Unsolicited messaging setup */
    MMCommonZteUnsolicitedSetup *unsolicited_setup;
};

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    MMAtSerialPort *ports[2];
    guint i;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_zte_icera_parent_class)->setup_ports (self);

    ports[0] = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    ports[1] = mm_base_modem_peek_port_secondary (MM_BASE_MODEM (self));

    /* Configure AT ports */
    for (i = 0; i < 2; i++) {
        if (!ports[i])
            continue;

        g_object_set (ports[i],
                      MM_PORT_CARRIER_DETECT, FALSE,
                      NULL);
    }

    /* Now reset the unsolicited messages we'll handle when enabled */
    mm_common_zte_set_unsolicited_events_handlers (MM_BROADBAND_MODEM (self),
                                                   MM_BROADBAND_MODEM_ZTE_ICERA (self)->priv->unsolicited_setup,
                                                   FALSE);
}

/*****************************************************************************/

MMBroadbandModemZteIcera *
mm_broadband_modem_zte_icera_new (const gchar *device,
                                  const gchar *driver,
                                  const gchar *plugin,
                                  guint16 vendor_id,
                                  guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_ZTE_ICERA,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_zte_icera_init (MMBroadbandModemZteIcera *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_ZTE_ICERA,
                                              MMBroadbandModemZteIceraPrivate);
    self->priv->unsolicited_setup = mm_common_zte_unsolicited_setup_new ();
}

static void
finalize (GObject *object)
{
    MMBroadbandModemZteIcera *self = MM_BROADBAND_MODEM_ZTE_ICERA (object);

    mm_common_zte_unsolicited_setup_free (self->priv->unsolicited_setup);

    G_OBJECT_CLASS (mm_broadband_modem_zte_icera_parent_class)->finalize (object);
}

static void
mm_broadband_modem_zte_icera_class_init (MMBroadbandModemZteIceraClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemZteIceraPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
