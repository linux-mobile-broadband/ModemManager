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

#include "mm-port-mbim.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPortMbim, mm_port_mbim, MM_TYPE_PORT)

struct _MMPortMbimPrivate {
    gboolean in_progress;
    MbimDevice *mbim_device;
};

/*****************************************************************************/

typedef struct {
    MMPortMbim *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
} PortContext;

static void
port_context_complete_and_free (PortContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (PortContext, ctx);
}

static PortContext *
port_context_new (MMPortMbim *self,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    PortContext *ctx;

    ctx = g_slice_new0 (PortContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             port_context_new);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
    return ctx;
}

/*****************************************************************************/

gboolean
mm_port_mbim_open_finish (MMPortMbim *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
mbim_device_open_ready (MbimDevice *mbim_device,
                        GAsyncResult *res,
                        PortContext *ctx)
{
    GError *error = NULL;

    /* Reset the progress flag */
    ctx->self->priv->in_progress = FALSE;
    if (!mbim_device_open_full_finish (mbim_device, res, &error)) {
        g_clear_object (&ctx->self->priv->mbim_device);
        g_simple_async_result_take_error (ctx->result, error);
    } else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    port_context_complete_and_free (ctx);
}

static void
mbim_device_new_ready (GObject *unused,
                       GAsyncResult *res,
                       PortContext *ctx)
{
    GError *error = NULL;

    ctx->self->priv->mbim_device = mbim_device_new_finish (res, &error);
    if (!ctx->self->priv->mbim_device) {
        g_simple_async_result_take_error (ctx->result, error);
        port_context_complete_and_free (ctx);
        return;
    }

    /* Now open the MBIM device */
    mbim_device_open_full (ctx->self->priv->mbim_device,
                           MBIM_DEVICE_OPEN_FLAGS_PROXY,
                           30,
                           ctx->cancellable,
                           (GAsyncReadyCallback)mbim_device_open_ready,
                           ctx);
}

void
mm_port_mbim_open (MMPortMbim *self,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GFile *file;
    gchar *fullpath;
    PortContext *ctx;

    g_return_if_fail (MM_IS_PORT_MBIM (self));

    ctx = port_context_new (self, cancellable, callback, user_data);

    if (self->priv->in_progress) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_IN_PROGRESS,
                                         "MBIM device open/close operation in progress");
        port_context_complete_and_free (ctx);
        return;
    }

    if (self->priv->mbim_device) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        port_context_complete_and_free (ctx);
        return;
    }

    fullpath = g_strdup_printf ("/dev/%s", mm_port_get_device (MM_PORT (self)));
    file = g_file_new_for_path (fullpath);

    self->priv->in_progress = TRUE;
    mbim_device_new (file,
                     ctx->cancellable,
                     (GAsyncReadyCallback)mbim_device_new_ready,
                     ctx);

    g_free (fullpath);
    g_object_unref (file);
}

/*****************************************************************************/

gboolean
mm_port_mbim_is_open (MMPortMbim *self)
{
    g_return_val_if_fail (MM_IS_PORT_MBIM (self), FALSE);

    return !!self->priv->mbim_device;
}

/*****************************************************************************/

gboolean
mm_port_mbim_close_finish (MMPortMbim *self,
                           GAsyncResult *res,
                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
mbim_device_close_ready (MbimDevice *device,
                         GAsyncResult *res,
                         PortContext *ctx)
{
    GError *error = NULL;

    if (!mbim_device_close_finish (device, res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    ctx->self->priv->in_progress = FALSE;
    g_clear_object (&ctx->self->priv->mbim_device);

    port_context_complete_and_free (ctx);
}

void
mm_port_mbim_close (MMPortMbim *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    PortContext *ctx;

    g_return_if_fail (MM_IS_PORT_MBIM (self));

    ctx = port_context_new (self, NULL, callback, user_data);

    if (self->priv->in_progress) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_IN_PROGRESS,
                                         "MBIM device open/close operation in progress");
        port_context_complete_and_free (ctx);
        return;
    }

    if (!self->priv->mbim_device) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        port_context_complete_and_free (ctx);
        return;
    }

    self->priv->in_progress = TRUE;
    mbim_device_close (self->priv->mbim_device,
                       5,
                       NULL,
                       (GAsyncReadyCallback)mbim_device_close_ready,
                       ctx);
    g_clear_object (&self->priv->mbim_device);
}

/*****************************************************************************/

MbimDevice *
mm_port_mbim_peek_device (MMPortMbim *self)
{
    g_return_val_if_fail (MM_IS_PORT_MBIM (self), NULL);

    return self->priv->mbim_device;
}

/*****************************************************************************/

MMPortMbim *
mm_port_mbim_new (const gchar *name)
{
    return MM_PORT_MBIM (g_object_new (MM_TYPE_PORT_MBIM,
                                       MM_PORT_DEVICE, name,
                                       MM_PORT_SUBSYS, MM_PORT_SUBSYS_USB,
                                       MM_PORT_TYPE, MM_PORT_TYPE_MBIM,
                                       NULL));
}

static void
mm_port_mbim_init (MMPortMbim *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT_MBIM, MMPortMbimPrivate);
}

static void
dispose (GObject *object)
{
    MMPortMbim *self = MM_PORT_MBIM (object);

    /* Clear device object */
    g_clear_object (&self->priv->mbim_device);

    G_OBJECT_CLASS (mm_port_mbim_parent_class)->dispose (object);
}

static void
mm_port_mbim_class_init (MMPortMbimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortMbimPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
}
