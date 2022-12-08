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
 * Copyright (C) 2012 Lanedo GmbH
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
#include "mm-modem-helpers.h"
#include "mm-base-modem-at.h"

#include "mm-sim-sierra.h"

G_DEFINE_TYPE (MMSimSierra, mm_sim_sierra, MM_TYPE_BASE_SIM)

/*****************************************************************************/
/* SIM identifier loading */

static gchar *
load_sim_identifier_finish (MMBaseSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
iccid_read_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;
    const gchar *response;
    const gchar *p;
    char *parsed;
    GError *local = NULL;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    p = mm_strip_tag (response, "!ICCID:");
    if (!p) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Failed to parse !ICCID response: '%s'",
                                 response);
        g_object_unref (task);
        return;
    }

    parsed = mm_3gpp_parse_iccid (p, &local);
    if (parsed)
        g_task_return_pointer (task, parsed, g_free);
    else
        g_task_return_error (task, local);

    g_object_unref (task);
}

static void
load_sim_identifier (MMBaseSim *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    MMBaseModem *modem = NULL;
    GTask *task;

    g_object_get (self,
                  MM_BASE_SIM_MODEM, &modem,
                  NULL);

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (
        modem,
        "!ICCID?",
        3,
        FALSE,
        (GAsyncReadyCallback)iccid_read_ready,
        task);
    g_object_unref (modem);
}

/*****************************************************************************/

MMBaseSim *
mm_sim_sierra_new_finish (GAsyncResult  *res,
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
mm_sim_sierra_new (MMBaseModem *modem,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_SIERRA,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                "active", TRUE, /* by default always active */
                                NULL);
}

static void
mm_sim_sierra_init (MMSimSierra *self)
{
}

static void
mm_sim_sierra_class_init (MMSimSierraClass *klass)
{
    MMBaseSimClass *base_sim_class = MM_BASE_SIM_CLASS (klass);

    base_sim_class->load_sim_identifier = load_sim_identifier;
    base_sim_class->load_sim_identifier_finish = load_sim_identifier_finish;
}
