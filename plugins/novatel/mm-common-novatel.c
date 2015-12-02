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
 * Copyright (C) 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "mm-common-novatel.h"
#include "mm-log.h"

/*****************************************************************************/
/* Custom init */

typedef struct {
    MMPortProbe *probe;
    MMPortSerialAt *port;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    guint nwdmat_retries;
    guint wait_time;
} CustomInitContext;

static void
custom_init_context_complete_and_free (CustomInitContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->port);
    g_object_unref (ctx->probe);
    g_object_unref (ctx->result);
    g_slice_free (CustomInitContext, ctx);
}

gboolean
mm_common_novatel_custom_init_finish (MMPortProbe *probe,
                                      GAsyncResult *result,
                                      GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void custom_init_step (CustomInitContext *ctx);

static void
nwdmat_ready (MMPortSerialAt *port,
              GAsyncResult *res,
              CustomInitContext *ctx)
{
    const gchar *response;
    GError *error = NULL;

    response = mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        if (g_error_matches (error,
                             MM_SERIAL_ERROR,
                             MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            custom_init_step (ctx);
            goto out;
        }

        mm_dbg ("(Novatel) Error flipping secondary ports to AT mode: %s", error->message);
    }

    /* Finish custom_init */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    custom_init_context_complete_and_free (ctx);

out:
    if (error)
        g_error_free (error);
}

static gboolean
custom_init_wait_cb (CustomInitContext *ctx)
{
    custom_init_step (ctx);
    return G_SOURCE_REMOVE;
}

static void
custom_init_step (CustomInitContext *ctx)
{
    /* If cancelled, end */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        mm_dbg ("(Novatel) no need to keep on running custom init in (%s)",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        custom_init_context_complete_and_free (ctx);
        return;
    }

    /* If device has a QMI port, don't run $NWDMAT */
    if (mm_port_probe_list_has_qmi_port (mm_device_peek_port_probe_list (mm_port_probe_peek_device (ctx->probe)))) {
        mm_dbg ("(Novatel) no need to run custom init in (%s): device has QMI port",
                mm_port_get_device (MM_PORT (ctx->port)));
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        custom_init_context_complete_and_free (ctx);
        return;
    }

    if (ctx->wait_time > 0) {
        ctx->wait_time--;
        g_timeout_add_seconds (1, (GSourceFunc)custom_init_wait_cb, ctx);
        return;
    }

    if (ctx->nwdmat_retries > 0) {
        ctx->nwdmat_retries--;
        mm_port_serial_at_command (ctx->port,
                                   "$NWDMAT=1",
                                   3,
                                   FALSE, /* raw */
                                   FALSE, /* allow_cached */
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)nwdmat_ready,
                                   ctx);
        return;
    }

    /* Finish custom_init */
    mm_dbg ("(Novatel) couldn't flip secondary port to AT in (%s): all retries consumed",
            mm_port_get_device (MM_PORT (ctx->port)));
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    custom_init_context_complete_and_free (ctx);
}

void
mm_common_novatel_custom_init (MMPortProbe *probe,
                               MMPortSerialAt *port,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    CustomInitContext *ctx;

    ctx = g_slice_new (CustomInitContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (probe),
                                             callback,
                                             user_data,
                                             mm_common_novatel_custom_init);
    ctx->probe = g_object_ref (probe);
    ctx->port = g_object_ref (port);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
    ctx->nwdmat_retries = 3;
    ctx->wait_time = 2;

    custom_init_step (ctx);
}
