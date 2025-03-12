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

G_DEFINE_INTERFACE (MMSharedOption, mm_shared_option, MM_TYPE_IFACE_MODEM)

/*****************************************************************************/
/* Create SIM (Modem interface) */

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
mm_shared_option_default_init (MMSharedOptionInterface *iface)
{
}
