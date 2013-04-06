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

#include <stdio.h>
#include <stdlib.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-mbim-port.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMMbimPort, mm_mbim_port, MM_TYPE_PORT)

struct _MMMbimPortPrivate {
    gboolean opening;
    MbimDevice *mbim_device;
};

/*****************************************************************************/

typedef struct {
    MMMbimPort *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
} PortOpenContext;

static void
port_open_context_complete_and_free (PortOpenContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (PortOpenContext, ctx);
}

gboolean
mm_mbim_port_open_finish (MMMbimPort *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
mbim_device_open_ready (MbimDevice *mbim_device,
                        GAsyncResult *res,
                        PortOpenContext *ctx)
{
    GError *error = NULL;

    /* Reset the opening flag */
    ctx->self->priv->opening = FALSE;

    if (!mbim_device_open_finish (mbim_device, res, &error)) {
        g_clear_object (&ctx->self->priv->mbim_device);
        g_simple_async_result_take_error (ctx->result, error);
    } else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    port_open_context_complete_and_free (ctx);
}

static void
mbim_device_new_ready (GObject *unused,
                       GAsyncResult *res,
                       PortOpenContext *ctx)
{
    GError *error = NULL;

    ctx->self->priv->mbim_device = mbim_device_new_finish (res, &error);
    if (!ctx->self->priv->mbim_device) {
        g_simple_async_result_take_error (ctx->result, error);
        port_open_context_complete_and_free (ctx);
        return;
    }

    /* Now open the MBIM device */
    mbim_device_open (ctx->self->priv->mbim_device,
                      10,
                      ctx->cancellable,
                      (GAsyncReadyCallback)mbim_device_open_ready,
                      ctx);
}

void
mm_mbim_port_open (MMMbimPort *self,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GFile *file;
    gchar *fullpath;
    PortOpenContext *ctx;

    g_return_if_fail (MM_IS_MBIM_PORT (self));

    ctx = g_slice_new0 (PortOpenContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_mbim_port_open);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

    if (self->priv->opening) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_IN_PROGRESS,
                                         "MBIM device already being opened");
        port_open_context_complete_and_free (ctx);
        return;
    }

    if (self->priv->mbim_device) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        port_open_context_complete_and_free (ctx);
        return;
    }

    fullpath = g_strdup_printf ("/dev/%s", mm_port_get_device (MM_PORT (self)));
    file = g_file_new_for_path (fullpath);

    self->priv->opening = TRUE;
    mbim_device_new (file,
                     ctx->cancellable,
                     (GAsyncReadyCallback)mbim_device_new_ready,
                     ctx);

    g_free (fullpath);
    g_object_unref (file);
}

gboolean
mm_mbim_port_is_open (MMMbimPort *self)
{
    g_return_val_if_fail (MM_IS_MBIM_PORT (self), FALSE);

    return !!self->priv->mbim_device;
}

static void
mbim_device_close_ready (MbimDevice *device,
                         GAsyncResult *res)
{
    GError *error = NULL;

    if (!mbim_device_close_finish (device, res, &error)) {
        mm_warn ("Couldn't properly close MBIM device: %s",
                 error->message);
        g_error_free (error);
    }
}

void
mm_mbim_port_close (MMMbimPort *self)
{
    g_return_if_fail (MM_IS_MBIM_PORT (self));

    if (!self->priv->mbim_device)
        return;

    /* Close and release the device. This method is async,
     * but we don't really care about the result. */
    mbim_device_close (self->priv->mbim_device,
                       5,
                       NULL,
                       (GAsyncReadyCallback)mbim_device_close_ready,
                       NULL);

    g_clear_object (&self->priv->mbim_device);
}

/*****************************************************************************/

MMMbimPort *
mm_mbim_port_new (const gchar *name)
{
    return MM_MBIM_PORT (g_object_new (MM_TYPE_MBIM_PORT,
                                       MM_PORT_DEVICE, name,
                                       MM_PORT_SUBSYS, MM_PORT_SUBSYS_USB,
                                       MM_PORT_TYPE, MM_PORT_TYPE_MBIM,
                                       NULL));
}

static void
mm_mbim_port_init (MMMbimPort *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_MBIM_PORT, MMMbimPortPrivate);
}

static void
dispose (GObject *object)
{
    MMMbimPort *self = MM_MBIM_PORT (object);

    /* Clear device object */
    g_clear_object (&self->priv->mbim_device);

    G_OBJECT_CLASS (mm_mbim_port_parent_class)->dispose (object);
}

static void
mm_mbim_port_class_init (MMMbimPortClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMMbimPortPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
}
