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
#include "mm-broadband-modem-novatel.h"
#include "mm-errors-types.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBroadbandModemNovatel, mm_broadband_modem_novatel, MM_TYPE_BROADBAND_MODEM);

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static const MMBaseModemAtCommand nwdmat_sequence[] = {
    { "$NWDMAT=1", 3, FALSE, mm_base_modem_response_processor_continue_on_error },
    { "$NWDMAT=1", 3, FALSE, mm_base_modem_response_processor_continue_on_error },
    { "$NWDMAT=1", 3, FALSE, NULL },
    { NULL }
};

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_novatel_parent_class)->setup_ports (self);

    /* Flip secondary ports to AT mode */
    mm_base_modem_at_sequence (MM_BASE_MODEM (self), nwdmat_sequence, NULL, NULL, NULL, NULL);
}

/*****************************************************************************/

MMBroadbandModemNovatel *
mm_broadband_modem_novatel_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_NOVATEL,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_novatel_init (MMBroadbandModemNovatel *self)
{
}

static void
mm_broadband_modem_novatel_class_init (MMBroadbandModemNovatelClass *klass)
{
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    broadband_modem_class->setup_ports = setup_ports;
}
