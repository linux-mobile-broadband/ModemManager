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
 * Copyright (C) 2013-2021 Aleksander Morgado <aleksander@gnu.org>
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
#include "mm-port-net.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMPortMbim, mm_port_mbim, MM_TYPE_PORT)

enum {
    SIGNAL_NOTIFICATION,
    SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

struct _MMPortMbimPrivate {
    gboolean    in_progress;
    MbimDevice *mbim_device;

    /* monitoring */
    gulong notification_monitoring_id;
    gulong timeout_monitoring_id;
    gulong removed_monitoring_id;

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

typedef struct {
    gchar *link_name;
    guint  session_id;
} SetupLinkResult;

static void
setup_link_result_free (SetupLinkResult *ctx)
{
    g_free (ctx->link_name);
    g_slice_free (SetupLinkResult, ctx);
}

gchar *
mm_port_mbim_setup_link_finish (MMPortMbim    *self,
                                GAsyncResult  *res,
                                guint         *session_id,
                                GError       **error)
{
    SetupLinkResult *result;
    gchar           *link_name;

    result = g_task_propagate_pointer (G_TASK (res), error);
    if (!result)
        return NULL;

    if (session_id)
        *session_id = result->session_id;
    link_name = g_steal_pointer (&result->link_name);
    setup_link_result_free (result);

    return link_name;
}

static void
device_add_link_ready (MbimDevice   *device,
                       GAsyncResult *res,
                       GTask        *task)
{
    SetupLinkResult *result;
    GError          *error = NULL;

    result = g_slice_new0 (SetupLinkResult);

    result->link_name = mbim_device_add_link_finish (device, res, &result->session_id, &error);
    if (!result->link_name) {
        g_prefix_error (&error, "failed to add link for device: ");
        g_task_return_error (task, error);
        setup_link_result_free (result);
    } else
        g_task_return_pointer (task, result, (GDestroyNotify)setup_link_result_free);
    g_object_unref (task);
}

void
mm_port_mbim_setup_link (MMPortMbim          *self,
                         MMPort              *data,
                         const gchar         *link_prefix_hint,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->mbim_device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Port is not open");
        g_object_unref (task);
        return;
    }

    mbim_device_add_link (self->priv->mbim_device,
                          MBIM_DEVICE_SESSION_ID_AUTOMATIC,
                          mm_kernel_device_get_name (mm_port_peek_kernel_device (data)),
                          link_prefix_hint,
                          NULL,
                          (GAsyncReadyCallback) device_add_link_ready,
                          task);
}

/*****************************************************************************/

gboolean
mm_port_mbim_cleanup_link_finish (MMPortMbim    *self,
                                  GAsyncResult  *res,
                                  GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
device_delete_link_ready (MbimDevice   *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    GError *error = NULL;

    if (!mbim_device_delete_link_finish (device, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_port_mbim_cleanup_link (MMPortMbim          *self,
                           const gchar         *link_name,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->mbim_device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Port is not open");
        g_object_unref (task);
        return;
    }

    mbim_device_delete_link (self->priv->mbim_device,
                             link_name,
                             NULL,
                             (GAsyncReadyCallback) device_delete_link_ready,
                             task);
}

/*****************************************************************************/

typedef struct {
    MbimDevice *device;
    MMPort    *data;
} ResetContext;

static void
reset_context_free (ResetContext *ctx)
{
    g_clear_object (&ctx->device);
    g_clear_object (&ctx->data);
    g_slice_free (ResetContext, ctx);
}

gboolean
mm_port_mbim_reset_finish (MMPortMbim    *self,
                           GAsyncResult  *res,
                           GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
delete_all_links_ready (MbimDevice   *device,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMPortMbim *self;
    GError     *error = NULL;

    self = g_task_get_source_object (task);

    /* link deletion not fatal */
    if (!mbim_device_delete_all_links_finish (device, res, &error)) {
        mm_obj_dbg (self, "couldn't delete all links: %s", error->message);
        g_clear_error (&error);
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
reset_device_new_ready (GObject      *source,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMPortMbim   *self;
    ResetContext *ctx;
    GError       *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    ctx->device = mbim_device_new_finish (res, &error);
    if (!ctx->device) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* first, delete all links found, if any */
    mm_obj_dbg (self, "deleting all links in data interface '%s'",
                mm_port_get_device (ctx->data));
    mbim_device_delete_all_links (ctx->device,
                                  mm_port_get_device (ctx->data),
                                  NULL,
                                  (GAsyncReadyCallback)delete_all_links_ready,
                                  task);
}

void
mm_port_mbim_reset (MMPortMbim          *self,
                    MMPort              *data,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    GTask            *task;
    ResetContext     *ctx;
    g_autoptr(GFile)  file = NULL;
    g_autofree gchar *fullpath = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->mbim_device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Port is already open");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (ResetContext);
    ctx->data = g_object_ref (data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) reset_context_free);

    fullpath = g_strdup_printf ("/dev/%s", mm_port_get_device (MM_PORT (self)));
    file = g_file_new_for_path (fullpath);

    mbim_device_new (file, NULL,
                     (GAsyncReadyCallback) reset_device_new_ready,
                     task);
}

/*****************************************************************************/

static void
reset_monitoring (MMPortMbim *self,
                  MbimDevice *mbim_device)
{
    if (self->priv->notification_monitoring_id && mbim_device) {
        g_signal_handler_disconnect (mbim_device, self->priv->notification_monitoring_id);
        self->priv->notification_monitoring_id = 0;
    }
    if (self->priv->timeout_monitoring_id && mbim_device) {
        g_signal_handler_disconnect (mbim_device, self->priv->timeout_monitoring_id);
        self->priv->timeout_monitoring_id = 0;
    }
    if (self->priv->removed_monitoring_id && mbim_device) {
        g_signal_handler_disconnect (mbim_device, self->priv->removed_monitoring_id);
        self->priv->removed_monitoring_id = 0;
    }
}

static void
consecutive_timeouts_updated_cb (MMPortMbim *self,
                                 GParamSpec *pspec,
                                 MbimDevice *mbim_device)
{
    g_signal_emit_by_name (self, MM_PORT_SIGNAL_TIMED_OUT, mbim_device_get_consecutive_timeouts (mbim_device));
}

static void
device_removed_cb (MMPortMbim  *self)
{
    g_signal_emit_by_name (self, MM_PORT_SIGNAL_REMOVED);
}

static void
notification_cb (MMPortMbim  *self,
                 MbimMessage *notification)
{
    g_signal_emit (self, signals[SIGNAL_NOTIFICATION], 0, notification);
}

static void
setup_monitoring (MMPortMbim *self,
                  MbimDevice *mbim_device)
{
    g_assert (mbim_device);

    reset_monitoring (self, mbim_device);

    g_assert (!self->priv->notification_monitoring_id);
    self->priv->notification_monitoring_id = g_signal_connect_swapped (mbim_device,
                                                                       MBIM_DEVICE_SIGNAL_INDICATE_STATUS,
                                                                       G_CALLBACK (notification_cb),
                                                                       self);

    g_assert (!self->priv->timeout_monitoring_id);
    self->priv->timeout_monitoring_id = g_signal_connect_swapped (mbim_device,
                                                                  "notify::" MBIM_DEVICE_CONSECUTIVE_TIMEOUTS,
                                                                  G_CALLBACK (consecutive_timeouts_updated_cb),
                                                                  self);

    g_assert (!self->priv->removed_monitoring_id);
    self->priv->removed_monitoring_id = g_signal_connect_swapped (mbim_device,
                                                                  MBIM_DEVICE_SIGNAL_REMOVED,
                                                                  G_CALLBACK (device_removed_cb),
                                                                  self);
}

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
        mm_obj_msg (self, "MBIM device is not QMI capable");
        self->priv->qmi_supported = FALSE;
    } else {
        mm_obj_msg (self, "MBIM device is QMI capable");
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
        mm_obj_msg (self, "MBIM device is not QMI capable");
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
        mm_obj_msg (self, "MBIM device is not QMI capable");
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
                         20,
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
    setup_monitoring (self, mbim_device);

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
                           MBIM_DEVICE_OPEN_FLAGS_PROXY | MBIM_DEVICE_OPEN_FLAGS_MS_MBIMEX_V3,
                           45,
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
    reset_monitoring (self, mbim_device);

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
    reset_monitoring (self, self->priv->mbim_device);
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

    signals[SIGNAL_NOTIFICATION] =
        g_signal_new (MM_PORT_MBIM_SIGNAL_NOTIFICATION,
                      G_OBJECT_CLASS_TYPE (G_OBJECT_CLASS (klass)),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (MMPortMbimClass, notification),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, MBIM_TYPE_MESSAGE);
}
