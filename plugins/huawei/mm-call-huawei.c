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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
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
#include "mm-base-modem-at.h"
#include "mm-call-huawei.h"

G_DEFINE_TYPE (MMCallHuawei, mm_call_huawei, MM_TYPE_BASE_CALL)

/*****************************************************************************/
/* Start the CALL */

static void
call_start_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  GTask *task)
{
    MMBaseCall *self;
    GError *error = NULL;
    const gchar *response = NULL;

    self = g_task_get_source_object (task);

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            /* something is wrong, serial timeout could never occurs */
        }

        if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_DIALTONE)) {
            /* Update state */
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
        }

        if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_BUSY)      ||
            g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_ANSWER) ||
            g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_CARRIER)) {
            /* Update state */
            mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
        }

        mm_dbg ("Couldn't start call : '%s'", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* check response for error */
    if (response && strlen (response) > 0 ) {
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't start the call: "
                             "Modem response '%s'", response);

        /* Update state */
        mm_base_call_change_state (self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
    } else {
        /* Update state */
        mm_base_call_change_state (self, MM_CALL_STATE_DIALING, MM_CALL_STATE_REASON_OUTGOING_STARTED);
    }

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
call_start (MMBaseCall *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMBaseModem *modem;
    GTask *task;
    gchar *cmd;

    g_object_get (self,
                  MM_BASE_CALL_MODEM, &modem,
                  NULL);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, modem, g_object_unref);

    cmd = g_strdup_printf ("ATD%s;", mm_gdbus_call_get_number (MM_GDBUS_CALL (self)));
    mm_base_modem_at_command (modem,
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)call_start_ready,
                              task);
    g_free (cmd);
}

/*****************************************************************************/

MMBaseCall *
mm_call_huawei_new (MMBaseModem *modem)
{
    return MM_BASE_CALL (g_object_new (MM_TYPE_CALL_HUAWEI,
                                       MM_BASE_CALL_MODEM, modem,
                                       NULL));
}

static void
mm_call_huawei_init (MMCallHuawei *self)
{
}

static void
mm_call_huawei_class_init (MMCallHuaweiClass *klass)
{
    MMBaseCallClass *base_call_class = MM_BASE_CALL_CLASS (klass);

    base_call_class->start = call_start;
}
