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
 * Copyright (C) 2012 Google, Inc.
 */

#include <stdio.h>
#include <stdlib.h>

#include <libqmi-glib.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-qmi-port.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMQmiPort, mm_qmi_port, MM_TYPE_PORT)

struct _MMQmiPortPrivate {
    gboolean opening;
    QmiDevice *qmi_device;
};

/*****************************************************************************/

typedef struct {
    MMQmiPort *self;
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
    g_free (ctx);
}

gboolean
mm_qmi_port_open_finish (MMQmiPort *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
qmi_device_open_ready (QmiDevice *qmi_device,
                       GAsyncResult *res,
                       PortOpenContext *ctx)
{
    GError *error = NULL;

    /* Reset the opening flag */
    ctx->self->priv->opening = FALSE;

    if (!qmi_device_open_finish (qmi_device, res, &error)) {
        g_clear_object (&ctx->self->priv->qmi_device);
        g_simple_async_result_take_error (ctx->result, error);
    } else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    port_open_context_complete_and_free (ctx);
}

static void
qmi_device_new_ready (GObject *unused,
                      GAsyncResult *res,
                      PortOpenContext *ctx)
{
    GError *error = NULL;

    ctx->self->priv->qmi_device = qmi_device_new_finish (res, &error);
    if (!ctx->self->priv->qmi_device) {
        g_simple_async_result_take_error (ctx->result, error);
        port_open_context_complete_and_free (ctx);
        return;
    }

    /* Now open the QMI device */
    qmi_device_open (ctx->self->priv->qmi_device,
                     QMI_DEVICE_OPEN_FLAGS_VERSION_INFO,
                     10,
                     ctx->cancellable,
                     (GAsyncReadyCallback)qmi_device_open_ready,
                     ctx);
}

void
mm_qmi_port_open (MMQmiPort *self,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GFile *file;
    gchar *fullpath;
    PortOpenContext *ctx;

    g_return_if_fail (MM_IS_QMI_PORT (self));

    ctx = g_new0 (PortOpenContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_qmi_port_open);
    ctx->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

    if (self->priv->opening) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_IN_PROGRESS,
                                         "QMI device already being opened");
        port_open_context_complete_and_free (ctx);
        return;
    }

    if (self->priv->qmi_device) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        port_open_context_complete_and_free (ctx);
        return;
    }

    fullpath = g_strdup_printf ("/dev/%s",
                                mm_port_get_device (MM_PORT (self)));
    file = g_file_new_for_path (fullpath);

    self->priv->opening = TRUE;
    qmi_device_new (file,
                    ctx->cancellable,
                    (GAsyncReadyCallback)qmi_device_new_ready,
                    ctx);

    g_free (fullpath);
    g_object_unref (file);
}

gboolean
mm_qmi_port_is_open (MMQmiPort *self)
{
    g_return_val_if_fail (MM_IS_QMI_PORT (self), FALSE);

    return !!self->priv->qmi_device;
}

void
mm_qmi_port_close (MMQmiPort *self)
{
    GError *error = NULL;

    g_return_if_fail (MM_IS_QMI_PORT (self));

    if (!self->priv->qmi_device)
        return;

    if (!qmi_device_close (self->priv->qmi_device, &error)) {
        mm_warn ("Couldn't properly close QMI device: %s",
                 error->message);
        g_error_free (error);
    }

    g_clear_object (&self->priv->qmi_device);
}

/*****************************************************************************/

MMQmiPort *
mm_qmi_port_new (const gchar *name)
{
    return MM_QMI_PORT (g_object_new (MM_TYPE_QMI_PORT,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_USB,
                                      MM_PORT_TYPE, MM_PORT_TYPE_QMI,
                                      NULL));
}

static void
mm_qmi_port_init (MMQmiPort *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_QMI_PORT, MMQmiPortPrivate);
}

static void
dispose (GObject *object)
{
    MMQmiPort *self = MM_QMI_PORT (object);

    g_clear_object (&self->priv->qmi_device);

    G_OBJECT_CLASS (mm_qmi_port_parent_class)->dispose (object);
}

static void
mm_qmi_port_class_init (MMQmiPortClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMQmiPortPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
}
