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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <stdio.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-sim-option.h"
#include "mm-shared-option.h"

/*****************************************************************************/
/* Create SIM (Modem inteface) */

MMBaseSim *
mm_shared_option_create_sim_finish (MMIfaceModem  *self,
                                    GAsyncResult  *res,
                                    GError       **error)
{
    return mm_sim_option_new_finish (res, error);
}

void
mm_shared_option_create_sim (MMIfaceModem        *self,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
    mm_sim_option_new (MM_BASE_MODEM (self),
                       NULL, /* cancellable */
                       callback,
                       user_data);
}

/*****************************************************************************/

static void
shared_option_init (gpointer g_iface)
{
}

GType
mm_shared_option_get_type (void)
{
    static GType shared_option_type = 0;

    if (!G_UNLIKELY (shared_option_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedOption),  /* class_size */
            shared_option_init,       /* base_init */
            NULL,                  /* base_finalize */
        };

        shared_option_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedOption", &info, 0);
        g_type_interface_add_prerequisite (shared_option_type, MM_TYPE_IFACE_MODEM);
    }

    return shared_option_type;
}
