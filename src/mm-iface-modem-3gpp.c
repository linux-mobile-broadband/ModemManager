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
 * Copyright (C) 2011 Google, Inc.
 */

#include <ModemManager.h>

#include <mm-gdbus-modem.h>
#include <mm-enums-types.h>
#include <mm-errors-types.h>

#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem.h"
#include "mm-log.h"

/*****************************************************************************/

static void
iface_modem_3gpp_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_3GPP_DBUS_SKELETON,
                              "3GPP DBus skeleton",
                              "DBus skeleton for the 3GPP interface",
                              MM_GDBUS_TYPE_MODEM3GPP_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_3gpp_get_type (void)
{
    static GType iface_modem_3gpp_type = 0;

    if (!G_UNLIKELY (iface_modem_3gpp_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem3gpp), /* class_size */
            iface_modem_3gpp_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_3gpp_type = g_type_register_static (G_TYPE_INTERFACE,
                                                        "MMIfaceModem3gpp",
                                                        &info,
                                                        0);

        g_type_interface_add_prerequisite (iface_modem_3gpp_type, MM_TYPE_BASE_MODEM);
    }

    return iface_modem_3gpp_type;
}
