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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-sim-mbim.h"

G_DEFINE_TYPE (MMSimMbim, mm_sim_mbim, MM_TYPE_SIM)

/*****************************************************************************/

MMSim *
mm_sim_mbim_new_finish (GAsyncResult  *res,
                        GError       **error)
{
    GObject *source;
    GObject *sim;

    source = g_async_result_get_source_object (res);
    sim = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!sim)
        return NULL;

    /* Only export valid SIMs */
    mm_sim_export (MM_SIM (sim));

    return MM_SIM (sim);
}

void
mm_sim_mbim_new (MMBaseModem *modem,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_MBIM,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_SIM_MODEM, modem,
                                NULL);
}

static void
mm_sim_mbim_init (MMSimMbim *self)
{
}

static void
mm_sim_mbim_class_init (MMSimMbimClass *klass)
{
}
