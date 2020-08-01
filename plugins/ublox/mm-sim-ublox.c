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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
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
#include "mm-modem-helpers.h"
#include "mm-base-modem-at.h"

#include "mm-sim-ublox.h"

G_DEFINE_TYPE (MMSimUblox, mm_sim_ublox, MM_TYPE_BASE_SIM)

/*****************************************************************************/
/* SIM identifier loading */

static gchar *
load_sim_identifier_finish (MMBaseSim     *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
parent_load_sim_identifier_ready (MMSimUblox   *self,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    GError *error = NULL;
    gchar  *simid;

    simid = MM_BASE_SIM_CLASS (mm_sim_ublox_parent_class)->load_sim_identifier_finish (MM_BASE_SIM (self), res, &error);
    if (simid)
        g_task_return_pointer (task, simid, g_free);
    else
        g_task_return_error (task, error);
    g_object_unref (task);
}

static void
ccid_ready (MMBaseModem  *modem,
            GAsyncResult *res,
            GTask        *task)
{
    MMBaseSim   *self;
    const gchar *response;
    gchar       *parsed;

    response = mm_base_modem_at_command_finish (modem, res, NULL);
    if (!response)
        goto error;

    response = mm_strip_tag (response, "+CCID:");
    if (!response)
        goto error;

    parsed = mm_3gpp_parse_iccid (response, NULL);
    if (parsed) {
        g_task_return_pointer (task, parsed, g_free);
        g_object_unref (task);
        return;
    }

error:
    /* Chain up to parent method to for devices that don't support +CCID properly */
    self = g_task_get_source_object (task);
    MM_BASE_SIM_CLASS (mm_sim_ublox_parent_class)->load_sim_identifier (self,
                                                                        (GAsyncReadyCallback) parent_load_sim_identifier_ready,
                                                                        task);
}

static void
load_sim_identifier (MMBaseSim           *self,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    MMBaseModem *modem = NULL;

    g_object_get (self,
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);
    mm_base_modem_at_command (
        modem,
        "+CCID",
        5,
        FALSE,
        (GAsyncReadyCallback)ccid_ready,
        g_task_new (self, NULL, callback, user_data));
    g_object_unref (modem);
}

/*****************************************************************************/

MMBaseSim *
mm_sim_ublox_new_finish (GAsyncResult  *res,
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
mm_sim_ublox_new (MMBaseModem          *modem,
                  GCancellable         *cancellable,
                  GAsyncReadyCallback   callback,
                  gpointer              user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_UBLOX,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                "active", TRUE, /* by default always active */
                                NULL);
}

static void
mm_sim_ublox_init (MMSimUblox *self)
{
}

static void
mm_sim_ublox_class_init (MMSimUbloxClass *klass)
{
    MMBaseSimClass *base_sim_class = MM_BASE_SIM_CLASS (klass);

    base_sim_class->load_sim_identifier = load_sim_identifier;
    base_sim_class->load_sim_identifier_finish = load_sim_identifier_finish;
}
