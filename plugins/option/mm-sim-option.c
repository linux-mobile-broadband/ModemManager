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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-sim-option.h"

G_DEFINE_TYPE (MMSimOption, mm_sim_option, MM_TYPE_BASE_SIM)

/*****************************************************************************/

MMBaseSim *
mm_sim_option_new_finish (GAsyncResult  *res,
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
    mm_base_sim_export (MM_BASE_SIM (sim));

    return MM_BASE_SIM (sim);
}

void
mm_sim_option_new (MMBaseModem         *modem,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_OPTION,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                "active", TRUE, /* by default always active */
                                NULL);
}

static void
mm_sim_option_init (MMSimOption *self)
{
}

static void
mm_sim_option_class_init (MMSimOptionClass *klass)
{
    MMBaseSimClass *base_sim_class = MM_BASE_SIM_CLASS (klass);

    /* Skip managing preferred networks, not supported by Option modems */
    base_sim_class->load_preferred_networks = NULL;
    base_sim_class->load_preferred_networks_finish = NULL;
    base_sim_class->set_preferred_networks = NULL;
    base_sim_class->set_preferred_networks_finish = NULL;
}
