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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
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
#include "mm-sim-mbm.h"

G_DEFINE_TYPE (MMSimMbm, mm_sim_mbm, MM_TYPE_BASE_SIM)

/*****************************************************************************/
/* SEND PIN/PUK (Generic implementation) */

typedef struct {
    MMSimMbm *self;
    MMBaseModem *modem;
    GSimpleAsyncResult *result;
    MMModemLock expected;
    guint retries;
} SendPinPukContext;

static void
send_pin_puk_context_complete_and_free (SendPinPukContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_slice_free (SendPinPukContext, ctx);
}

static gboolean
common_send_pin_puk_finish (MMBaseSim *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void wait_for_unlocked_status (SendPinPukContext *ctx);

static void
cpin_query_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  SendPinPukContext *ctx)
{

    const gchar *result;

    result = mm_base_modem_at_command_finish (modem, res, NULL);
    if (result && strstr (result, "READY")) {
        /* All done! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        send_pin_puk_context_complete_and_free (ctx);
        return;
    }

    /* Need to recheck */
    wait_for_unlocked_status (ctx);
}

static gboolean
cpin_query_cb (SendPinPukContext *ctx)
{
    mm_base_modem_at_command (ctx->modem,
                              "+CPIN?",
                              20,
                              FALSE,
                              (GAsyncReadyCallback)cpin_query_ready,
                              ctx);
    return G_SOURCE_REMOVE;
}

static void
wait_for_unlocked_status (SendPinPukContext *ctx)
{
    /* Oops... :/ */
    if (ctx->retries == 0) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "PIN was sent but modem didn't report unlocked");
        send_pin_puk_context_complete_and_free (ctx);
        return;
    }

    /* Check status */
    ctx->retries--;
    mm_dbg ("Scheduling lock state check...");
    g_timeout_add_seconds (1, (GSourceFunc)cpin_query_cb, ctx);
}

static void
send_pin_puk_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    SendPinPukContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        send_pin_puk_context_complete_and_free (ctx);
        return;
    }

    /* No explicit error sending the PIN/PUK, now check status until we have the
     * expected lock status */
    ctx->retries = 3;
    wait_for_unlocked_status (ctx);
}

static void
common_send_pin_puk (MMBaseSim *self,
                     const gchar *pin,
                     const gchar *puk,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    SendPinPukContext *ctx;
    gchar *command;

    ctx = g_slice_new (SendPinPukContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             common_send_pin_puk);
    g_object_get (ctx->self,
                  MM_BASE_SIM_MODEM, &ctx->modem,
                  NULL);

    command = (puk ?
               g_strdup_printf ("+CPIN=\"%s\",\"%s\"", puk, pin) :
               g_strdup_printf ("+CPIN=\"%s\"", pin));
    mm_base_modem_at_command (ctx->modem,
                              command,
                              3,
                              FALSE,
                              (GAsyncReadyCallback)send_pin_puk_ready,
                              ctx);
    g_free (command);
}

static void
send_puk (MMBaseSim *self,
          const gchar *puk,
          const gchar *new_pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    common_send_pin_puk (self, new_pin, puk, callback, user_data);
}

static void
send_pin (MMBaseSim *self,
          const gchar *pin,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    common_send_pin_puk (self, pin, NULL, callback, user_data);
}

/*****************************************************************************/

MMBaseSim *
mm_sim_mbm_new_finish (GAsyncResult  *res,
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
mm_sim_mbm_new (MMBaseModem *modem,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    g_async_initable_new_async (MM_TYPE_SIM_MBM,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                MM_BASE_SIM_MODEM, modem,
                                NULL);
}

static void
mm_sim_mbm_init (MMSimMbm *self)
{
}

static void
mm_sim_mbm_class_init (MMSimMbmClass *klass)
{
    MMBaseSimClass *base_sim_class = MM_BASE_SIM_CLASS (klass);

    base_sim_class->send_pin = send_pin;
    base_sim_class->send_pin_finish = common_send_pin_puk_finish;
    base_sim_class->send_puk = send_puk;
    base_sim_class->send_puk_finish = common_send_pin_puk_finish;
}
