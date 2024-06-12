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
 * Copyright (C) 2024 Fibocom Wireless Inc.
 */

#include <config.h>
#include <arpa/inet.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-iface-modem.h"
#include "mm-shared-mtk.h"
#include "mm-base-modem-at.h"

G_DEFINE_INTERFACE (MMSharedMtk, mm_shared_mtk, MM_TYPE_IFACE_MODEM)

/*****************************************************************************/
/* Unlock retries (Modem interface) */

MMUnlockRetries *
mm_shared_mtk_load_unlock_retries_finish (MMIfaceModem  *self,
                                          GAsyncResult  *res,
                                          GError        **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static MMUnlockRetries *
epinc_query_response_parse (const gchar  *response,
                            GError       **error)
{
    g_autoptr(GRegex)      r = NULL;
    g_autoptr(GMatchInfo)  match_info = NULL;
    gint                   pin1;
    gint                   puk1;
    gint                   pin2;
    gint                   puk2;
    MMUnlockRetries       *retries;

    r = g_regex_new ("\\+EPINC:\\s*([0-9]+),\\s*([0-9]+),\\s*([0-9]+),\\s*([0-9]+)", 0, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen(response), 0, 0, &match_info, error))
        return NULL;

    if (!mm_get_int_from_match_info (match_info, 1, &pin1) ||
        !mm_get_int_from_match_info (match_info, 2, &pin2) ||
        !mm_get_int_from_match_info (match_info, 3, &puk1) ||
        !mm_get_int_from_match_info (match_info, 4, &puk2)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Failed to parse the EPINC response: '%s'", response);
        return NULL;
    }

    retries = mm_unlock_retries_new();
    mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN, pin1);
    mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PIN2, pin2);
    mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK, puk1);
    mm_unlock_retries_set (retries, MM_MODEM_LOCK_SIM_PUK2, puk2);

    return retries;
}

static void
epinc_query_ready (MMBaseModem  *self,
                   GAsyncResult *res,
                   GTask        *task)
{
    const gchar      *response;
    GError           *error = NULL;
    MMUnlockRetries  *retries;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    retries = epinc_query_response_parse (response, &error);
    if (!retries) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_pointer (task, retries, g_object_unref);
    g_object_unref (task);
}

void
mm_shared_mtk_load_unlock_retries (MMIfaceModem         *self,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    mm_base_modem_at_command (
        MM_BASE_MODEM (self),
        "+EPINC?",
        3,
        FALSE,
        (GAsyncReadyCallback)epinc_query_ready,
        task);
}

static void
mm_shared_mtk_default_init (MMSharedMtkInterface *iface)
{
}
