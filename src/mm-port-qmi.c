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

#include "mm-port-qmi.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPortQmi, mm_port_qmi, MM_TYPE_PORT)

typedef struct {
    QmiService service;
    QmiClient *client;
    MMPortQmiFlag flag;
} ServiceInfo;

struct _MMPortQmiPrivate {
    gboolean opening;
    QmiDevice *qmi_device;
    GList *services;
};

/*****************************************************************************/

QmiClient *
mm_port_qmi_peek_client (MMPortQmi *self,
                         QmiService service,
                         MMPortQmiFlag flag)
{
    GList *l;

    for (l = self->priv->services; l; l = g_list_next (l)) {
        ServiceInfo *info = l->data;

        if (info->service == service &&
            info->flag == flag)
            return info->client;
    }

    return NULL;
}

QmiClient *
mm_port_qmi_get_client (MMPortQmi *self,
                        QmiService service,
                        MMPortQmiFlag flag)
{
    QmiClient *client;

    client = mm_port_qmi_peek_client (self, service, flag);
    return (client ? g_object_ref (client) : NULL);
}

/*****************************************************************************/

typedef struct {
    MMPortQmi *self;
    GSimpleAsyncResult *result;
    ServiceInfo *info;
} AllocateClientContext;

static void
allocate_client_context_complete_and_free (AllocateClientContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    if (ctx->info) {
        g_assert (ctx->info->client == NULL);
        g_free (ctx->info);
    }
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

gboolean
mm_port_qmi_allocate_client_finish (MMPortQmi *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
allocate_client_ready (QmiDevice *qmi_device,
                       GAsyncResult *res,
                       AllocateClientContext *ctx)
{
    GError *error = NULL;

    ctx->info->client = qmi_device_allocate_client_finish (qmi_device, res, &error);
    if (!ctx->info->client) {
        g_prefix_error (&error,
                        "Couldn't create client for service '%s': ",
                        qmi_service_get_string (ctx->info->service));
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        /* Move the service info to our internal list */
        ctx->self->priv->services = g_list_prepend (ctx->self->priv->services, ctx->info);
        ctx->info = NULL;
    }

    allocate_client_context_complete_and_free (ctx);
}

void
mm_port_qmi_allocate_client (MMPortQmi *self,
                             QmiService service,
                             MMPortQmiFlag flag,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    AllocateClientContext *ctx;

    if (!!mm_port_qmi_peek_client (self, service, flag)) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_EXISTS,
                                             "Client for service '%s' already allocated",
                                             qmi_service_get_string (service));
        return;
    }

    ctx = g_new0 (AllocateClientContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_port_qmi_allocate_client);
    ctx->info = g_new0 (ServiceInfo, 1);
    ctx->info->service = service;
    ctx->info->flag = flag;

    qmi_device_allocate_client (self->priv->qmi_device,
                                service,
                                QMI_CID_NONE,
                                10,
                                cancellable,
                                (GAsyncReadyCallback)allocate_client_ready,
                                ctx);
}

/*****************************************************************************/

typedef struct {
    MMPortQmi *self;
    gboolean set_data_format;
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
mm_port_qmi_open_finish (MMPortQmi *self,
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
    QmiDeviceOpenFlags flags = QMI_DEVICE_OPEN_FLAGS_VERSION_INFO;

    /* If possible, try to open the QMI port through the QMI proxy daemon, which
     * allows other applications to also talk to the QMI port properly. */
#if QMI_CHECK_VERSION (1,7,0)
    flags |= QMI_DEVICE_OPEN_FLAGS_PROXY;
#endif

    ctx->self->priv->qmi_device = qmi_device_new_finish (res, &error);
    if (!ctx->self->priv->qmi_device) {
        g_simple_async_result_take_error (ctx->result, error);
        port_open_context_complete_and_free (ctx);
        return;
    }

    if (ctx->set_data_format)
        flags |= (QMI_DEVICE_OPEN_FLAGS_NET_802_3 | QMI_DEVICE_OPEN_FLAGS_NET_NO_QOS_HEADER);

    /* Now open the QMI device */
    qmi_device_open (ctx->self->priv->qmi_device,
                     flags,
                     10,
                     ctx->cancellable,
                     (GAsyncReadyCallback)qmi_device_open_ready,
                     ctx);
}

void
mm_port_qmi_open (MMPortQmi *self,
                  gboolean set_data_format,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GFile *file;
    gchar *fullpath;
    PortOpenContext *ctx;

    g_return_if_fail (MM_IS_PORT_QMI (self));

    ctx = g_new0 (PortOpenContext, 1);
    ctx->self = g_object_ref (self);
    ctx->set_data_format = set_data_format;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_port_qmi_open);
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
mm_port_qmi_is_open (MMPortQmi *self)
{
    g_return_val_if_fail (MM_IS_PORT_QMI (self), FALSE);

    return !!self->priv->qmi_device;
}

void
mm_port_qmi_close (MMPortQmi *self)
{
    GList *l;
    GError *error = NULL;

    g_return_if_fail (MM_IS_PORT_QMI (self));

    if (!self->priv->qmi_device)
        return;

    /* Release all allocated clients */
    for (l = self->priv->services; l; l = g_list_next (l)) {
        ServiceInfo *info = l->data;

        mm_dbg ("Releasing client for service '%s'...", qmi_service_get_string (info->service));
        qmi_device_release_client (self->priv->qmi_device,
                                   info->client,
                                   QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                                   3, NULL, NULL, NULL);
        g_clear_object (&info->client);
    }
    g_list_free_full (self->priv->services, (GDestroyNotify)g_free);
    self->priv->services = NULL;

    /* Close and release the device */
    if (!qmi_device_close (self->priv->qmi_device, &error)) {
        mm_warn ("Couldn't properly close QMI device: %s",
                 error->message);
        g_error_free (error);
    }

    g_clear_object (&self->priv->qmi_device);
}

/*****************************************************************************/

MMPortQmi *
mm_port_qmi_new (const gchar *name)
{
    return MM_PORT_QMI (g_object_new (MM_TYPE_PORT_QMI,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_USB,
                                      MM_PORT_TYPE, MM_PORT_TYPE_QMI,
                                      NULL));
}

static void
mm_port_qmi_init (MMPortQmi *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT_QMI, MMPortQmiPrivate);
}

static void
dispose (GObject *object)
{
    MMPortQmi *self = MM_PORT_QMI (object);
    GList *l;

    /* Deallocate all clients */
    for (l = self->priv->services; l; l = g_list_next (l)) {
        ServiceInfo *info = l->data;

        if (info->client)
            g_object_unref (info->client);
    }
    g_list_free_full (self->priv->services, (GDestroyNotify)g_free);
    self->priv->services = NULL;

    /* Clear device object */
    g_clear_object (&self->priv->qmi_device);

    G_OBJECT_CLASS (mm_port_qmi_parent_class)->dispose (object);
}

static void
mm_port_qmi_class_init (MMPortQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortQmiPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
}
