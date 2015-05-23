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

typedef struct {
    MMBaseCall *self;
    MMBaseModem *modem;
    GSimpleAsyncResult *result;
} CallStartContext;

static void
call_start_context_complete_and_free (CallStartContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
call_start_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  CallStartContext *ctx)
{
    GError *error = NULL;
    const gchar *response = NULL;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            /* something is wrong, serial timeout could never occurs */
        }

        if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_DIALTONE)) {
            /* Update state */
            mm_base_call_change_state(ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_ERROR);
        }

        if (g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_BUSY)      ||
            g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_ANSWER) ||
            g_error_matches (error, MM_CONNECTION_ERROR, MM_CONNECTION_ERROR_NO_CARRIER)) {
            /* Update state */
            mm_base_call_change_state(ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
        }

        mm_dbg ("Couldn't start call : '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        call_start_context_complete_and_free (ctx);
        return;
    }

    /* check response for error */
    if (response && strlen (response) > 0 ) {
        error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Couldn't start the call: "
                             "Modem response '%s'", response);

        /* Update state */
        mm_base_call_change_state (ctx->self, MM_CALL_STATE_TERMINATED, MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
    } else {
        /* Update state */
        mm_base_call_change_state (ctx->self, MM_CALL_STATE_DIALING, MM_CALL_STATE_REASON_OUTGOING_STARTED);
    }

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        call_start_context_complete_and_free (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    call_start_context_complete_and_free (ctx);
}

static void
call_start (MMBaseCall *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    CallStartContext *ctx;
    gchar *cmd;

    /* Setup the context */
    ctx = g_new0 (CallStartContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             call_start);
    ctx->self = g_object_ref (self);
    g_object_get (ctx->self,
                  MM_BASE_CALL_MODEM, &ctx->modem,
                  NULL);

    cmd = g_strdup_printf ("ATD%s;", mm_gdbus_call_get_number (MM_GDBUS_CALL (self)));
    mm_base_modem_at_command (ctx->modem,
                              cmd,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)call_start_ready,
                              ctx);
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
