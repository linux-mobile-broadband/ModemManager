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

static gpointer mm_modem_nokia_parent_class = NULL;

MMModem *
mm_modem_nokia_new (const char *device,
                    const char *driver,
                    const char *plugin)
{
    g_return_val_if_fail (device != NULL, NULL);
    g_return_val_if_fail (driver != NULL, NULL);
    g_return_val_if_fail (plugin != NULL, NULL);

    return MM_MODEM (g_object_new (MM_TYPE_MODEM_NOKIA,
                                   MM_MODEM_MASTER_DEVICE, device,
                                   MM_MODEM_DRIVER, driver,
                                   MM_MODEM_PLUGIN, plugin,
                                   NULL));
}

static gboolean
grab_port (MMModem *modem,
           const char *subsys,
           const char *name,
           gpointer user_data,
           GError **error)
{
    MMGenericGsm *gsm = MM_GENERIC_GSM (modem);
    MMPortType ptype = MM_PORT_TYPE_IGNORED;
    MMPort *port = NULL;

    if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_PRIMARY))
        ptype = MM_PORT_TYPE_PRIMARY;
    else if (!mm_generic_gsm_get_port (gsm, MM_PORT_TYPE_SECONDARY))
        ptype = MM_PORT_TYPE_SECONDARY;

    port = mm_generic_gsm_grab_port (gsm, subsys, name, ptype, error);
    if (port && MM_IS_SERIAL_PORT (port)) {
        mm_serial_port_set_response_parser (MM_SERIAL_PORT (port),
                                            mm_serial_parser_v1_e1_parse,
                                            mm_serial_parser_v1_e1_new (),
                                            mm_serial_parser_v1_e1_destroy);
    }

    return !!port;
}

/*****************************************************************************/

static void
modem_init (MMModem *modem_class)
{
    modem_class->grab_port = grab_port;
}

static void
mm_modem_nokia_init (MMModemNokia *self)
{
}

static void
mm_modem_nokia_class_init (MMModemNokiaClass *klass)
{
    mm_modem_nokia_parent_class = g_type_class_peek_parent (klass);
}

GType
mm_modem_nokia_get_type (void)
{
    static GType modem_nokia_type = 0;

    if (G_UNLIKELY (modem_nokia_type == 0)) {
        static const GTypeInfo modem_nokia_type_info = {
            sizeof (MMModemNokiaClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) mm_modem_nokia_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,   /* class_data */
            sizeof (MMModemNokia),
            0,      /* n_preallocs */
            (GInstanceInitFunc) mm_modem_nokia_init,
        };

        static const GInterfaceInfo modem_iface_info = { 
            (GInterfaceInitFunc) modem_init
        };

        modem_nokia_type = g_type_register_static (MM_TYPE_GENERIC_GSM, "MMModemNokia", &modem_nokia_type_info, 0);

        g_type_add_interface_static (modem_nokia_type, MM_TYPE_MODEM, &modem_iface_info);
    }

    return modem_nokia_type;
}
