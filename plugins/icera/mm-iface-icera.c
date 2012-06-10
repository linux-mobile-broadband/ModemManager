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
 * Copyright (C) 2010 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-iface-icera.h"
#include "mm-iface-modem.h"

/*****************************************************************************/

static void
iface_icera_init (gpointer g_iface)
{
}

GType
mm_iface_icera_get_type (void)
{
    static GType iface_icera_type = 0;

    if (!G_UNLIKELY (iface_icera_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceIcera), /* class_size */
            iface_icera_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_icera_type = g_type_register_static (G_TYPE_INTERFACE,
                                                   "MMIfaceIcera",
                                                   &info,
                                                   0);

        g_type_interface_add_prerequisite (iface_icera_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_icera_type;
}
