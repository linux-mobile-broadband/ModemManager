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
#include "mm-log-object.h"

/*****************************************************************************/
/* Custom init */

typedef struct {
    MMPortSerialAt *port;
    guint nwdmat_retries;
    guint wait_time;
} CustomInitContext;

static void
custom_init_context_free (CustomInitContext *ctx)
{
    g_object_unref (ctx->port);
    g_slice_free (CustomInitContext, ctx);
}

gboolean
mm_common_novatel_custom_init_finish (MMPortProbe *probe,
                                      GAsyncResult *result,
                                      GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void custom_init_step (GTask *task);

static void
nwdmat_ready (MMPortSerialAt *port,
              GAsyncResult   *res,
              GTask          *task)
{
    g_autoptr(GError)  error = NULL;
    MMPortProbe       *probe;

    probe = g_task_get_source_object (task);

    mm_port_serial_at_command_finish (port, res, &error);
    if (error) {
        if (g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_RESPONSE_TIMEOUT)) {
            custom_init_step (task);
            return;
        }

        mm_obj_dbg (probe, "error flipping secondary ports to AT mode: %s", error->message);
    }

    /* Finish custom_init */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
custom_init_wait_cb (GTask *task)
{
    custom_init_step (task);
    return G_SOURCE_REMOVE;
}

static void
custom_init_step (GTask *task)
{
    CustomInitContext *ctx;
    MMPortProbe       *probe;

    probe = g_task_get_source_object (task);
    ctx   = g_task_get_task_data (task);

    /* If cancelled, end */
    if (g_task_return_error_if_cancelled (task)) {
        mm_obj_dbg (probe, "no need to keep on running custom init");
        g_object_unref (task);
        return;
    }

    /* If device has a QMI port, don't run $NWDMAT */
    if (mm_port_probe_list_has_qmi_port (mm_device_peek_port_probe_list (mm_port_probe_peek_device (probe)))) {
        mm_obj_dbg (probe, "no need to run custom init: device has QMI port");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    if (ctx->wait_time > 0) {
        ctx->wait_time--;
        g_timeout_add_seconds (1, (GSourceFunc)custom_init_wait_cb, task);
        return;
    }

    if (ctx->nwdmat_retries > 0) {
        ctx->nwdmat_retries--;
        mm_port_serial_at_command (ctx->port,
                                   "$NWDMAT=1",
                                   3,
                                   FALSE, /* raw */
                                   FALSE, /* allow_cached */
                                   g_task_get_cancellable (task),
                                   (GAsyncReadyCallback)nwdmat_ready,
                                   task);
        return;
    }

    /* Finish custom_init */
    mm_obj_dbg (probe, "couldn't flip secondary port to AT: all retries consumed");
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_common_novatel_custom_init (MMPortProbe *probe,
                               MMPortSerialAt *port,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    CustomInitContext *ctx;
    GTask *task;

    ctx = g_slice_new (CustomInitContext);
    ctx->port = g_object_ref (port);
    ctx->nwdmat_retries = 3;
    ctx->wait_time = 2;

    task = g_task_new (probe, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)custom_init_context_free);

    custom_init_step (task);
}
