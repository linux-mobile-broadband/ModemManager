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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm-modem-nokia.h"
#include "mm-serial-parsers.h"

G_DEFINE_TYPE (MMModemNokia, mm_modem_nokia, MM_TYPE_GENERIC_GSM)


MMModem *
mm_modem_nokia_new (const char *device,
                    const char *driver,
                    const char *plugin,
                    guint32 vendor,
                    guint32 product)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_NOKIA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   MM_MODEM_HW_VID, vendor,
                                   MM_MODEM_HW_PID, product,
                                   NULL));
}

static void
port_grabbed (MMGenericGsm *gsm,
              MMPort *port,
              MMAtPortFlags pflags,
              gpointer user_data)
{
    if (MM_IS_AT_SERIAL_PORT (port)) {
        mm_at_serial_port_set_response_parser (MM_AT_SERIAL_PORT (port),
                                               mm_serial_parser_v1_e1_parse,
                                               mm_serial_parser_v1_e1_new (),
                                               mm_serial_parser_v1_e1_destroy);
    }
}

/*****************************************************************************/

static void
mm_modem_nokia_init (MMModemNokia *self)
{
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    /* gobject does not like to just have get_property and seems to
     * to not honour our overriden properties ... keep this as an empty
     * func around */
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    /* Nokia headsets (at least N85) do not support "power on"; they do
     * support "power off" but you proabably do not want to turn off the
     * power on your telephone if something went wrong with connecting
     * process. So, disabling both these operations.  The Nokia GSM/UMTS command
     * reference v1.2 also states that only CFUN=0 (turn off but still charge)
     * and CFUN=1 (full functionality) are supported, and since the phone has
     * to be in CFUN=1 before we'll be able to talk to it in the first place,
     * we shouldn't bother with CFUN at all.
     */
    switch (prop_id) {
    case MM_GENERIC_GSM_PROP_POWER_UP_CMD:
        g_value_set_string (value, "");
        break;
    case MM_GENERIC_GSM_PROP_POWER_DOWN_CMD:
        g_value_set_string (value, "");
        break;
    case MM_GENERIC_GSM_PROP_INIT_CMD:
        /* When initializing a Nokia phone, first enable the echo,
         * and then disable it, so that we get it properly disabled */
        g_value_set_string (value, "Z E1 E0 V1");
        break;
    default:
        break;
    }
}

static void
mm_modem_nokia_class_init (MMModemNokiaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMGenericGsmClass *gsm_class = MM_GENERIC_GSM_CLASS (klass);

    mm_modem_nokia_parent_class = g_type_class_peek_parent (klass);

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    gsm_class->port_grabbed = port_grabbed;

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_INIT_CMD,
                                      MM_GENERIC_GSM_INIT_CMD);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_POWER_UP_CMD,
                                      MM_GENERIC_GSM_POWER_UP_CMD);

    g_object_class_override_property (object_class,
                                      MM_GENERIC_GSM_PROP_POWER_DOWN_CMD,
                                      MM_GENERIC_GSM_POWER_DOWN_CMD);
}

