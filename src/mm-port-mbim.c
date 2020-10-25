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
 * Copyright (C) 2013-2018 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#if defined WITH_QMI
# include <libqmi-glib.h>
#endif

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-port-mbim.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMPortMbim, mm_port_mbim, MM_TYPE_PORT)

struct _MMPortMbimPrivate {
    gboolean    in_progress;
    MbimDevice *mbim_device;
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    gboolean    qmi_supported;
    QmiDevice  *qmi_device;
    GList      *qmi_clients;
#endif
};

/*****************************************************************************/

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

gboolean
mm_port_mbim_supports_qmi (MMPortMbim *self)
{
    return !!self->priv->qmi_device;
}

QmiClient *
mm_port_mbim_peek_qmi_client (MMPortMbim *self,
                              QmiService  service)
{
    GList *l;

    for (l = self->priv->qmi_clients; l; l = g_list_next (l)) {
        QmiClient *qmi_client = QMI_CLIENT (l->data);

        if (qmi_client_get_service (qmi_client) == service)
            return qmi_client;
    }

    return NULL;
}

QmiClient *
mm_port_mbim_get_qmi_client (MMPortMbim *self,
                             QmiService  service)
{
    QmiClient *client;

    client = mm_port_mbim_peek_qmi_client (self, service);
    return (client ? g_object_ref (client) : NULL);
}

gboolean
mm_port_mbim_allocate_qmi_client_finish (MMPortMbim    *self,
                                         GAsyncResult  *res,
                                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
allocate_client_ready (QmiDevice    *qmi_device,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMPortMbim *self;
    QmiClient  *qmi_client;
    GError     *error = NULL;

    self = g_task_get_source_object (task);
    qmi_client = qmi_device_allocate_client_finish (qmi_device, res, &error);
    if (!qmi_client) {
        g_prefix_error (&error,
                        "Couldn't create QMI client for service '%s': ",
                        qmi_service_get_string ((QmiService) GPOINTER_TO_INT (g_task_get_task_data (task))));
        g_task_return_error (task, error);
    } else {
        /* Store the client in our internal list */
        self->priv->qmi_clients = g_list_prepend (self->priv->qmi_clients, qmi_client);
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

void
mm_port_mbim_allocate_qmi_client (MMPortMbim           *self,
                                  QmiService            service,
                                  GCancellable         *cancellable,
                                  GAsyncReadyCallback   callback,
                                  gpointer              user_data)
{
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

    if (!mm_port_mbim_is_open (self)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "Port is closed");
        g_object_unref (task);
        return;
    }

    if (!mm_port_mbim_supports_qmi (self)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Port doesn't support QMI over MBIM");
        g_object_unref (task);
        return;
    }

    if (!!mm_port_mbim_peek_qmi_client (self, service)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_EXISTS,
                                 "Client for service '%s' already allocated",
                                 qmi_service_get_string (service));
        g_object_unref (task);
        return;
    }

    g_task_set_task_data (task, GINT_TO_POINTER (service), NULL);
    qmi_device_allocate_client (self->priv->qmi_device,
                                service,
                                QMI_CID_NONE,
                                10,
                                cancellable,
                                (GAsyncReadyCallback)allocate_client_ready,
                                task);
}

#endif

/*****************************************************************************/

gboolean
mm_port_mbim_open_finish (MMPortMbim    *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
qmi_device_open_ready (QmiDevice    *dev,
                       GAsyncResult *res,
                       GTask        *task)
{
    GError     *error = NULL;
    MMPortMbim *self;

    self = g_task_get_source_object (task);

    if (!qmi_device_open_finish (dev, res, &error)) {
        mm_obj_dbg (self, "error: couldn't open QmiDevice: %s", error->message);
        g_error_free (error);
        g_clear_object (&self->priv->qmi_device);
        /* Ignore error and complete */
        mm_obj_info (self, "MBIM device is not QMI capable");
        self->priv->qmi_supported = FALSE;
    } else {
        mm_obj_info (self, "MBIM device is QMI capable");
    }

    self->priv->in_progress = FALSE;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
qmi_device_new_ready (GObject      *unused,
                      GAsyncResult *res,
                      GTask        *task)
{
    GError     *error = NULL;
    MMPortMbim *self;

    self = g_task_get_source_object (task);

    self->priv->qmi_device = qmi_device_new_finish (res, &error);
    if (!self->priv->qmi_device) {
        mm_obj_dbg (self, "error: couldn't create QmiDevice: %s", error->message);
        g_error_free (error);
        /* Ignore error and complete */
        mm_obj_info (self, "MBIM device is not QMI capable");
        self->priv->qmi_supported = FALSE;
        self->priv->in_progress = FALSE;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Try to open using QMI over MBIM */
    mm_obj_dbg (self, "trying to open QMI over MBIM device...");
    qmi_device_open (self->priv->qmi_device,
                     (QMI_DEVICE_OPEN_FLAGS_PROXY        |
                      QMI_DEVICE_OPEN_FLAGS_MBIM         |
                      QMI_DEVICE_OPEN_FLAGS_VERSION_INFO |
                      QMI_DEVICE_OPEN_FLAGS_EXPECT_INDICATIONS),
                     15,
                     g_task_get_cancellable (task),
                     (GAsyncReadyCallback)qmi_device_open_ready,
                     task);
}

static void
mbim_query_device_services_ready (MbimDevice   *device,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    MMPortMbim                *self;
    MbimMessage               *response;
    GError                    *error = NULL;
    MbimDeviceServiceElement **device_services;
    guint32                    device_services_count;
    GFile                     *file;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_device_services_response_parse (
            response,
            &device_services_count,
            NULL, /* max_dss_sessions */
            &device_services,
            &error)) {
        guint32 i;

        /* Look for the QMI service */
        for (i = 0; i < device_services_count; i++) {
            if (mbim_uuid_to_service (&device_services[i]->device_service_id) == MBIM_SERVICE_QMI)
                break;
        }
        /* If we were able to successfully list device services and none of them
         * is the QMI service, we'll skip trying to check QMI support. */
        if (i == device_services_count)
            self->priv->qmi_supported = FALSE;
        mbim_device_service_element_array_free (device_services);
    } else {
        /* Ignore error */
        mm_obj_dbg (self, "Couldn't query device services, will attempt QMI open anyway: %s", error->message);
        g_error_free (error);
    }

    if (response)
        mbim_message_unref (response);

    /* File path of the device */
    file = G_FILE (g_task_get_task_data (task));

    if (!file || !self->priv->qmi_supported) {
        mm_obj_info (self, "MBIM device is not QMI capable");
        self->priv->in_progress = FALSE;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Attempt to create and open the QMI device */
    mm_obj_dbg (self, "checking if QMI over MBIM is supported...");
    qmi_device_new (file,
                    g_task_get_cancellable (task),
                    (GAsyncReadyCallback) qmi_device_new_ready,
                    task);
}

static void
mbim_query_device_services (GTask *task)
{
    MbimMessage *message;
    MMPortMbim  *self;

    self = g_task_get_source_object (task);

    message = mbim_message_device_services_query_new (NULL);
    mbim_device_command (self->priv->mbim_device,
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)mbim_query_device_services_ready,
                         task);
    mbim_message_unref (message);
}

#endif

static void
mbim_device_open_ready (MbimDevice   *mbim_device,
                        GAsyncResult *res,
                        GTask        *task)
{
    GError     *error = NULL;
    MMPortMbim *self;

    self = g_task_get_source_object (task);

    if (!mbim_device_open_full_finish (mbim_device, res, &error)) {
        g_clear_object (&self->priv->mbim_device);
        self->priv->in_progress = FALSE;
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "MBIM device is now open");

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (self->priv->qmi_supported) {
        mbim_query_device_services (task);
        return;
    }
#endif

    self->priv->in_progress = FALSE;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
mbim_device_new_ready (GObject      *unused,
                       GAsyncResult *res,
                       GTask        *task)
{
    GError     *error = NULL;
    MMPortMbim *self;

    self = g_task_get_source_object (task);
    self->priv->mbim_device = mbim_device_new_finish (res, &error);
    if (!self->priv->mbim_device) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now open the MBIM device */
    mbim_device_open_full (self->priv->mbim_device,
                           MBIM_DEVICE_OPEN_FLAGS_PROXY,
                           30,
                           g_task_get_cancellable (task),
                           (GAsyncReadyCallback)mbim_device_open_ready,
                           task);
}

void
mm_port_mbim_open (MMPortMbim          *self,
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
                   gboolean             try_qmi_over_mbim,
#endif
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GFile *file;
    gchar *fullpath;
    GTask *task;

    g_return_if_fail (MM_IS_PORT_MBIM (self));

    task = g_task_new (self, cancellable, callback, user_data);

    if (self->priv->in_progress) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_IN_PROGRESS,
                                 "MBIM device open/close operation in progress");
        g_object_unref (task);
        return;
    }

    if (self->priv->mbim_device) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    fullpath = g_strdup_printf ("/dev/%s", mm_port_get_device (MM_PORT (self)));
    file = g_file_new_for_path (fullpath);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    /* If we want to try QMI over MBIM, store the GFile as task data */
    if (try_qmi_over_mbim)
        g_task_set_task_data (task, g_object_ref (file), g_object_unref);
#endif

    self->priv->in_progress = TRUE;
    mbim_device_new (file,
                     cancellable,
                     (GAsyncReadyCallback)mbim_device_new_ready,
                     task);

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

typedef struct {
    MbimDevice *mbim_device;
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    QmiDevice *qmi_device;
#endif
} PortMbimCloseContext;

static void
port_mbim_close_context_free (PortMbimCloseContext *ctx)
{
    g_clear_object (&ctx->mbim_device);
#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    g_clear_object (&ctx->qmi_device);
#endif
    g_slice_free (PortMbimCloseContext, ctx);
}

gboolean
mm_port_mbim_close_finish (MMPortMbim    *self,
                           GAsyncResult  *res,
                           GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
mbim_device_close_ready (MbimDevice   *mbim_device,
                         GAsyncResult *res,
                         GTask        *task)
{
    GError     *error = NULL;
    MMPortMbim *self;

    self = g_task_get_source_object (task);

    g_assert (!self->priv->mbim_device);
    self->priv->in_progress = FALSE;

    if (!mbim_device_close_finish (mbim_device, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
port_mbim_device_close (GTask *task)
{
    PortMbimCloseContext *ctx;

    ctx = g_task_get_task_data (task);
    g_assert (ctx->mbim_device);
    mbim_device_close (ctx->mbim_device,
                       5,
                       NULL,
                       (GAsyncReadyCallback)mbim_device_close_ready,
                       task);
}

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED

static void
qmi_device_close_ready (QmiDevice    *qmi_device,
                        GAsyncResult *res,
                        GTask        *task)
{
    GError     *error = NULL;
    MMPortMbim *self;

    self = g_task_get_source_object (task);

    if (!qmi_device_close_finish (qmi_device, res, &error)) {
        mm_obj_warn (self, "Couldn't properly close QMI device: %s", error->message);
        g_error_free (error);
    }

    port_mbim_device_close (task);
}

#endif

void
mm_port_mbim_close (MMPortMbim          *self,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    PortMbimCloseContext *ctx;
    GTask                *task;

    g_return_if_fail (MM_IS_PORT_MBIM (self));

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->in_progress) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_IN_PROGRESS,
                                 "MBIM device open/close operation in progress");
        g_object_unref (task);
        return;
    }

    if (!self->priv->mbim_device) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    self->priv->in_progress = TRUE;

    /* Store device(s) to close in the context */
    ctx = g_slice_new0 (PortMbimCloseContext);
    ctx->mbim_device = g_steal_pointer (&self->priv->mbim_device);
    g_task_set_task_data (task, ctx, (GDestroyNotify)port_mbim_close_context_free);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    if (self->priv->qmi_device) {
        GList *l;

        /* Release all allocated clients */
        for (l = self->priv->qmi_clients; l; l = g_list_next (l)) {
            QmiClient *qmi_client = QMI_CLIENT (l->data);

            mm_obj_dbg (self, "Releasing client for service '%s'...",
                        qmi_service_get_string (qmi_client_get_service (qmi_client)));
            qmi_device_release_client (self->priv->qmi_device,
                                       qmi_client,
                                       QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                                       3, NULL, NULL, NULL);
        }
        g_list_free_full (self->priv->qmi_clients, g_object_unref);
        self->priv->qmi_clients = NULL;

        ctx->qmi_device = g_steal_pointer (&self->priv->qmi_device);
        qmi_device_close_async (ctx->qmi_device,
                                5,
                                NULL,
                                (GAsyncReadyCallback)qmi_device_close_ready,
                                task);
        return;
    }
#endif

    port_mbim_device_close (task);
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
mm_port_mbim_new (const gchar  *name,
                  MMPortSubsys  subsys)
{
    return MM_PORT_MBIM (g_object_new (MM_TYPE_PORT_MBIM,
                                       MM_PORT_DEVICE, name,
                                       MM_PORT_SUBSYS, subsys,
                                       MM_PORT_TYPE, MM_PORT_TYPE_MBIM,
                                       NULL));
}

static void
mm_port_mbim_init (MMPortMbim *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT_MBIM, MMPortMbimPrivate);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    /* By default, always assume that QMI is supported, we'll later check if
     * that's true or not. */
    self->priv->qmi_supported = TRUE;
#endif
}

static void
dispose (GObject *object)
{
    MMPortMbim *self = MM_PORT_MBIM (object);

#if defined WITH_QMI && QMI_MBIM_QMUX_SUPPORTED
    g_list_free_full (self->priv->qmi_clients, g_object_unref);
    self->priv->qmi_clients = NULL;
    g_clear_object (&self->priv->qmi_device);
#endif

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
