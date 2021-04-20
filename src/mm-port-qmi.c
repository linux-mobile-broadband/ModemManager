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
#include "mm-log-object.h"

G_DEFINE_TYPE (MMPortQmi, mm_port_qmi, MM_TYPE_PORT)

typedef struct {
    QmiService service;
    QmiClient *client;
    MMPortQmiFlag flag;
} ServiceInfo;

struct _MMPortQmiPrivate {
    gboolean in_progress;
    QmiDevice *qmi_device;
    GList *services;
    gboolean llp_is_raw_ip;
};

/*****************************************************************************/

static QmiClient *
lookup_client (MMPortQmi     *self,
               QmiService     service,
               MMPortQmiFlag  flag,
               gboolean       steal)
{
    GList *l;

    for (l = self->priv->services; l; l = g_list_next (l)) {
        ServiceInfo *info = l->data;

        if (info->service == service && info->flag == flag) {
            QmiClient *found;

            found = info->client;
            if (steal) {
                self->priv->services = g_list_delete_link (self->priv->services, l);
                g_free (info);
            }
            return found;
        }
    }

    return NULL;
}

QmiClient *
mm_port_qmi_peek_client (MMPortQmi *self,
                         QmiService service,
                         MMPortQmiFlag flag)
{
    return lookup_client (self, service, flag, FALSE);
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

QmiDevice *
mm_port_qmi_peek_device (MMPortQmi *self)
{
    g_return_val_if_fail (MM_IS_PORT_QMI (self), NULL);

    return self->priv->qmi_device;
}

/*****************************************************************************/

void
mm_port_qmi_release_client (MMPortQmi     *self,
                            QmiService     service,
                            MMPortQmiFlag  flag)
{
    QmiClient *client;

    if (!self->priv->qmi_device)
        return;

    client = lookup_client (self, service, flag, TRUE);
    if (!client)
        return;

    mm_obj_dbg (self, "explicitly releasing client for service '%s'...", qmi_service_get_string (service));
    qmi_device_release_client (self->priv->qmi_device,
                               client,
                               QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                               3, NULL, NULL, NULL);
    g_object_unref (client);
}

/*****************************************************************************/

typedef struct {
    ServiceInfo *info;
} AllocateClientContext;

static void
allocate_client_context_free (AllocateClientContext *ctx)
{
    if (ctx->info) {
        g_assert (ctx->info->client == NULL);
        g_free (ctx->info);
    }
    g_free (ctx);
}

gboolean
mm_port_qmi_allocate_client_finish (MMPortQmi *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
allocate_client_ready (QmiDevice *qmi_device,
                       GAsyncResult *res,
                       GTask *task)
{
    MMPortQmi *self;
    AllocateClientContext *ctx;
    GError *error = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);
    ctx->info->client = qmi_device_allocate_client_finish (qmi_device, res, &error);
    if (!ctx->info->client) {
        g_prefix_error (&error,
                        "Couldn't create client for service '%s': ",
                        qmi_service_get_string (ctx->info->service));
        g_task_return_error (task, error);
    } else {
        /* Move the service info to our internal list */
        self->priv->services = g_list_prepend (self->priv->services, ctx->info);
        ctx->info = NULL;
        g_task_return_boolean (task, TRUE);
    }

    g_object_unref (task);
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
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

    if (!mm_port_qmi_is_open (self)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "Port is closed");
        g_object_unref (task);
        return;
    }

    if (!!mm_port_qmi_peek_client (self, service, flag)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_EXISTS,
                                 "Client for service '%s' already allocated",
                                 qmi_service_get_string (service));
        g_object_unref (task);
        return;
    }

    ctx = g_new0 (AllocateClientContext, 1);
    ctx->info = g_new0 (ServiceInfo, 1);
    ctx->info->service = service;
    ctx->info->flag = flag;
    g_task_set_task_data (task, ctx, (GDestroyNotify)allocate_client_context_free);

    qmi_device_allocate_client (self->priv->qmi_device,
                                service,
                                QMI_CID_NONE,
                                10,
                                cancellable,
                                (GAsyncReadyCallback)allocate_client_ready,
                                task);
}

/*****************************************************************************/

gboolean
mm_port_qmi_llp_is_raw_ip (MMPortQmi *self)
{
    return self->priv->llp_is_raw_ip;
}

/*****************************************************************************/

typedef enum {
    PORT_OPEN_STEP_FIRST,
    PORT_OPEN_STEP_CHECK_OPENING,
    PORT_OPEN_STEP_CHECK_ALREADY_OPEN,
    PORT_OPEN_STEP_DEVICE_NEW,
    PORT_OPEN_STEP_OPEN_WITHOUT_DATA_FORMAT,
    PORT_OPEN_STEP_GET_KERNEL_DATA_FORMAT,
    PORT_OPEN_STEP_ALLOCATE_WDA_CLIENT,
    PORT_OPEN_STEP_GET_WDA_DATA_FORMAT,
    PORT_OPEN_STEP_CHECK_DATA_FORMAT,
    PORT_OPEN_STEP_SYNC_DATA_FORMAT,
    PORT_OPEN_STEP_CLOSE_BEFORE_OPEN_WITH_DATA_FORMAT,
    PORT_OPEN_STEP_OPEN_WITH_DATA_FORMAT,
    PORT_OPEN_STEP_LAST
} PortOpenStep;

typedef struct {
    QmiDevice                   *device;
    QmiClient                   *wda;
    GError                      *error;
    PortOpenStep                 step;
    gboolean                     set_data_format;
    QmiDeviceExpectedDataFormat  kernel_data_format;
    QmiWdaLinkLayerProtocol      llp;
} PortOpenContext;

static void
port_open_context_free (PortOpenContext *ctx)
{
    g_assert (!ctx->error);
    if (ctx->wda && ctx->device)
        qmi_device_release_client (ctx->device,
                                   ctx->wda,
                                   QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                                   3, NULL, NULL, NULL);
    g_clear_object (&ctx->wda);
    g_clear_object (&ctx->device);
    g_slice_free (PortOpenContext, ctx);
}

gboolean
mm_port_qmi_open_finish (MMPortQmi     *self,
                         GAsyncResult  *res,
                         GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void port_open_step (GTask *task);

static void
port_open_complete_with_error (GTask *task)
{
    MMPortQmi       *self;
    PortOpenContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    g_assert (ctx->error);
    self->priv->in_progress = FALSE;
    g_task_return_error (task, g_steal_pointer (&ctx->error));
    g_object_unref (task);
}

static void
qmi_device_close_on_error_ready (QmiDevice    *qmi_device,
                                 GAsyncResult *res,
                                 GTask        *task)
{
    MMPortQmi         *self;
    g_autoptr(GError)  error = NULL;

    self = g_task_get_source_object (task);

    if (!qmi_device_close_finish (qmi_device, res, &error))
        mm_obj_warn (self, "Couldn't close QMI device after failed open sequence: %s", error->message);

    port_open_complete_with_error (task);
}

static void
qmi_device_open_second_ready (QmiDevice    *qmi_device,
                              GAsyncResult *res,
                              GTask        *task)
{
    MMPortQmi       *self;
    PortOpenContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (qmi_device_open_finish (qmi_device, res, &ctx->error)) {
        /* If the open with CTL data format is sucessful, update */
        if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP)
            self->priv->llp_is_raw_ip = TRUE;
        else
            self->priv->llp_is_raw_ip = FALSE;
    }

    /* In both error and success, we go to last step */
    ctx->step = PORT_OPEN_STEP_LAST;
    port_open_step (task);
}

static void
qmi_device_close_to_reopen_ready (QmiDevice    *qmi_device,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    MMPortQmi       *self;
    PortOpenContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!qmi_device_close_finish (qmi_device, res, &ctx->error)) {
        mm_obj_warn (self, "Couldn't close QMI device to reopen it");
        ctx->step = PORT_OPEN_STEP_LAST;
    } else
        ctx->step++;
    port_open_step (task);
}

static void
set_data_format_ready (QmiClientWda *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMPortQmi                                   *self;
    PortOpenContext                             *ctx;
    g_autoptr(QmiMessageWdaSetDataFormatOutput)  output = NULL;
    g_autoptr(GError)                            error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_wda_set_data_format_finish (client, res, &error);
    if (!output || !qmi_message_wda_set_data_format_output_get_result (output, &error)) {
        mm_obj_warn (self, "Couldn't set data format: %s", error->message);
        /* If setting WDA data format fails, fallback to LLP requested via CTL */
        ctx->step = PORT_OPEN_STEP_CLOSE_BEFORE_OPEN_WITH_DATA_FORMAT;
    } else {
        self->priv->llp_is_raw_ip = TRUE;
        ctx->step = PORT_OPEN_STEP_LAST;
    }

    port_open_step (task);
}

static void
get_data_format_ready (QmiClientWda *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMPortQmi                                   *self;
    PortOpenContext                             *ctx;
    g_autoptr(QmiMessageWdaGetDataFormatOutput)  output = NULL;
    g_autoptr(GError)                            error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_wda_get_data_format_finish (client, res, NULL);
    if (!output ||
        !qmi_message_wda_get_data_format_output_get_result (output, &error) ||
        !qmi_message_wda_get_data_format_output_get_link_layer_protocol (output, &ctx->llp, NULL)) {
        /* A 'missing argument' error when querying data format is seen in new
         * devices like the Quectel RM500Q, requiring the 'endpoint info' TLV.
         * When this happens, assume the device supports only raw-ip and be done
         * with it. */
        if (error && g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_MISSING_ARGUMENT)) {
            mm_obj_dbg (self, "Querying data format failed: '%s', assuming raw-ip is only supported", error->message);
            ctx->llp = QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP;
            ctx->step++;
        } else {
            /* If loading WDA data format fails, fallback to 802.3 requested via CTL */
            ctx->step = PORT_OPEN_STEP_CLOSE_BEFORE_OPEN_WITH_DATA_FORMAT;
        }
    } else
        /* Go on to next step */
        ctx->step++;

    port_open_step (task);
}

static void
allocate_client_wda_ready (QmiDevice    *device,
                           GAsyncResult *res,
                           GTask        *task)
{
    PortOpenContext *ctx;

    ctx = g_task_get_task_data (task);

    ctx->wda = qmi_device_allocate_client_finish (device, res, NULL);
    if (!ctx->wda) {
        /* If no WDA supported, then we just fallback to reopening explicitly
         * requesting 802.3 in the CTL service. */
        ctx->step = PORT_OPEN_STEP_CLOSE_BEFORE_OPEN_WITH_DATA_FORMAT;
        port_open_step (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    port_open_step (task);
}

static void
qmi_device_open_first_ready (QmiDevice    *qmi_device,
                             GAsyncResult *res,
                             GTask        *task)
{
    PortOpenContext *ctx;

    ctx = g_task_get_task_data (task);

    if (!qmi_device_open_finish (qmi_device, res, &ctx->error))
        /* Error opening the device */
        ctx->step = PORT_OPEN_STEP_LAST;
    else if (!ctx->set_data_format)
        /* If not setting data format, we're done */
        ctx->step = PORT_OPEN_STEP_LAST;
    else
        /* Go on to next step */
        ctx->step++;
    port_open_step (task);
}

static void
qmi_device_new_ready (GObject *unused,
                      GAsyncResult *res,
                      GTask *task)
{
    PortOpenContext *ctx;

    ctx = g_task_get_task_data (task);
    /* Store the device in the context until the operation is fully done,
     * so that we return IN_PROGRESS errors until we finish this async
     * operation. */
    ctx->device = qmi_device_new_finish (res, &ctx->error);
    if (!ctx->device)
        /* Error creating the device */
        ctx->step = PORT_OPEN_STEP_LAST;
    else
        /* Go on to next step */
        ctx->step++;
    port_open_step (task);
}

static void
port_open_step (GTask *task)
{
    MMPortQmi       *self;
    PortOpenContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);
    switch (ctx->step) {
    case PORT_OPEN_STEP_FIRST:
        mm_obj_dbg (self, "Opening QMI device...");
        ctx->step++;
        /* Fall through */

    case PORT_OPEN_STEP_CHECK_OPENING:
        mm_obj_dbg (self, "Checking if QMI device already opening...");
        if (self->priv->in_progress) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_IN_PROGRESS,
                                     "QMI device open/close operation in progress");
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PORT_OPEN_STEP_CHECK_ALREADY_OPEN:
        mm_obj_dbg (self, "Checking if QMI device already open...");
        if (self->priv->qmi_device) {
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PORT_OPEN_STEP_DEVICE_NEW: {
        GFile *file;
        gchar *fullpath;

        fullpath = g_strdup_printf ("/dev/%s", mm_port_get_device (MM_PORT (self)));
        file = g_file_new_for_path (fullpath);

        /* We flag in this point that we're opening. From now on, if we stop
         * for whatever reason, we should clear this flag. We do this by ensuring
         * that all callbacks go through the LAST step for completing. */
        self->priv->in_progress = TRUE;

        mm_obj_dbg (self, "Creating QMI device...");
        qmi_device_new (file,
                        g_task_get_cancellable (task),
                        (GAsyncReadyCallback) qmi_device_new_ready,
                        task);

        g_free (fullpath);
        g_object_unref (file);
        return;
    }

    case PORT_OPEN_STEP_OPEN_WITHOUT_DATA_FORMAT:
        /* Now open the QMI device without any data format CTL flag */
        mm_obj_dbg (self, "Opening device without data format update...");
        qmi_device_open (ctx->device,
                         (QMI_DEVICE_OPEN_FLAGS_VERSION_INFO |
                          QMI_DEVICE_OPEN_FLAGS_PROXY),
                         45,
                         g_task_get_cancellable (task),
                         (GAsyncReadyCallback) qmi_device_open_first_ready,
                         task);
        return;

    case PORT_OPEN_STEP_GET_KERNEL_DATA_FORMAT:
        /* Querying kernel data format is only expected when using qmi_wwan */
        if (mm_port_get_subsys (MM_PORT (self)) == MM_PORT_SUBSYS_USBMISC) {
            mm_obj_dbg (self, "Querying kernel data format...");
            /* Try to gather expected data format from the sysfs file */
            ctx->kernel_data_format = qmi_device_get_expected_data_format (ctx->device, NULL);
            /* If data format cannot be retrieved, we fallback to 802.3 via CTL */
            if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN) {
                ctx->step = PORT_OPEN_STEP_CLOSE_BEFORE_OPEN_WITH_DATA_FORMAT;
                port_open_step (task);
                return;
            }
        }
        /* For any driver other than qmi_wwan, assume raw-ip */
        else {
            ctx->kernel_data_format = QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP;
            mm_obj_dbg (self, "Assuming default kernel data format: %s",
                        qmi_device_expected_data_format_get_string (ctx->kernel_data_format));
        }

        ctx->step++;
        /* Fall through */

    case PORT_OPEN_STEP_ALLOCATE_WDA_CLIENT:
        /* Allocate WDA client */
        mm_obj_dbg (self, "Allocating WDA client...");
        qmi_device_allocate_client (ctx->device,
                                    QMI_SERVICE_WDA,
                                    QMI_CID_NONE,
                                    10,
                                    g_task_get_cancellable (task),
                                    (GAsyncReadyCallback) allocate_client_wda_ready,
                                    task);
        return;

    case PORT_OPEN_STEP_GET_WDA_DATA_FORMAT:
        /* If we have WDA client, query current data format */
        mm_obj_dbg (self, "Querying device data format...");
        qmi_client_wda_get_data_format (QMI_CLIENT_WDA (ctx->wda),
                                        NULL,
                                        10,
                                        g_task_get_cancellable (task),
                                        (GAsyncReadyCallback) get_data_format_ready,
                                        task);
        return;

    case PORT_OPEN_STEP_CHECK_DATA_FORMAT:
        /* We now have the WDA data format and the kernel data format, if they're
         * equal, we're done */
        mm_obj_dbg (self, "Checking data format: kernel %s, device %s",
                    qmi_device_expected_data_format_get_string (ctx->kernel_data_format),
                    qmi_wda_link_layer_protocol_get_string (ctx->llp));

        if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3 &&
            ctx->llp == QMI_WDA_LINK_LAYER_PROTOCOL_802_3) {
            self->priv->llp_is_raw_ip = FALSE;
            ctx->step = PORT_OPEN_STEP_LAST;
            port_open_step (task);
            return;
        }

        if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP &&
            ctx->llp == QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP) {
            self->priv->llp_is_raw_ip = TRUE;
            ctx->step = PORT_OPEN_STEP_LAST;
            port_open_step (task);
            return;
        }

        ctx->step++;
        /* Fall through */

    case PORT_OPEN_STEP_SYNC_DATA_FORMAT:
        /* For drivers other than qmi_wwan, the kernel data format was raw-ip
         * by default, we need to ask the module to switch to it */
        if (mm_port_get_subsys (MM_PORT (self)) != MM_PORT_SUBSYS_USBMISC) {
            g_autoptr(QmiMessageWdaSetDataFormatInput) input = NULL;

            g_assert (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP);
            input = qmi_message_wda_set_data_format_input_new ();
            qmi_message_wda_set_data_format_input_set_link_layer_protocol (input, QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, NULL);
            qmi_client_wda_set_data_format (QMI_CLIENT_WDA (ctx->wda),
                                            input,
                                            10,
                                            g_task_get_cancellable (task),
                                            (GAsyncReadyCallback) set_data_format_ready,
                                            task);
            return;
        }

        /* If using the qmi_wwan driver, we ask the kernel to sync with the
         * data format requested by the module */
        mm_obj_dbg (self, "Updating kernel data format: %s", qmi_wda_link_layer_protocol_get_string (ctx->llp));
        if (ctx->llp == QMI_WDA_LINK_LAYER_PROTOCOL_802_3) {
            ctx->kernel_data_format = QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3;
            self->priv->llp_is_raw_ip = FALSE;
        } else if (ctx->llp == QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP) {
            ctx->kernel_data_format = QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP;
            self->priv->llp_is_raw_ip = TRUE;
        } else
            g_assert_not_reached ();

        /* Regardless of the output, we're done after this action */
        qmi_device_set_expected_data_format (ctx->device,
                                             ctx->kernel_data_format,
                                             &ctx->error);
        ctx->step = PORT_OPEN_STEP_LAST;
        port_open_step (task);
        return;

    case PORT_OPEN_STEP_CLOSE_BEFORE_OPEN_WITH_DATA_FORMAT:
        /* This fallback only applies when WDA unsupported */
        mm_obj_dbg (self, "Closing device to reopen it right away...");
        qmi_device_close_async (ctx->device,
                                5,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback) qmi_device_close_to_reopen_ready,
                                task);
        return;

    case PORT_OPEN_STEP_OPEN_WITH_DATA_FORMAT: {
        QmiDeviceOpenFlags open_flags;

        /* Common open flags */
        open_flags = (QMI_DEVICE_OPEN_FLAGS_VERSION_INFO |
                      QMI_DEVICE_OPEN_FLAGS_PROXY        |
                      QMI_DEVICE_OPEN_FLAGS_NET_NO_QOS_HEADER);

        /* Need to reopen setting 802.3/raw-ip using CTL */
        if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP)
            open_flags |= QMI_DEVICE_OPEN_FLAGS_NET_RAW_IP;
        else
            open_flags |= QMI_DEVICE_OPEN_FLAGS_NET_802_3;

        mm_obj_dbg (self, "Reopening device with data format: %s...",
                    qmi_device_expected_data_format_get_string (ctx->kernel_data_format));
        qmi_device_open (ctx->device,
                         open_flags,
                         10,
                         g_task_get_cancellable (task),
                         (GAsyncReadyCallback) qmi_device_open_second_ready,
                         task);
        return;
    }

    case PORT_OPEN_STEP_LAST:
        if (ctx->error) {
            mm_obj_dbg (self, "QMI port open operation failed: %s", ctx->error->message);

            if (ctx->device) {
                qmi_device_close_async (ctx->device,
                                        5,
                                        NULL,
                                        (GAsyncReadyCallback) qmi_device_close_on_error_ready,
                                        task);
                return;
            }

            port_open_complete_with_error (task);
            return;
        }

        mm_obj_dbg (self, "QMI port open operation finished successfully");

        /* Store device in private info */
        g_assert (ctx->device);
        g_assert (!self->priv->qmi_device);
        self->priv->qmi_device = g_object_ref (ctx->device);
        self->priv->in_progress = FALSE;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

void
mm_port_qmi_open (MMPortQmi           *self,
                  gboolean             set_data_format,
                  GCancellable        *cancellable,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    PortOpenContext *ctx;
    GTask           *task;

    ctx = g_slice_new0 (PortOpenContext);
    ctx->step = PORT_OPEN_STEP_FIRST;
    ctx->set_data_format = set_data_format;
    ctx->llp = QMI_WDA_LINK_LAYER_PROTOCOL_UNKNOWN;
    ctx->kernel_data_format = QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)port_open_context_free);
    port_open_step (task);
}

/*****************************************************************************/

gboolean
mm_port_qmi_is_open (MMPortQmi *self)
{
    g_return_val_if_fail (MM_IS_PORT_QMI (self), FALSE);

    return !!self->priv->qmi_device;
}

/*****************************************************************************/

typedef struct {
    QmiDevice *qmi_device;
} PortQmiCloseContext;

static void
port_qmi_close_context_free (PortQmiCloseContext *ctx)
{
    g_clear_object (&ctx->qmi_device);
    g_slice_free (PortQmiCloseContext, ctx);
}

gboolean
mm_port_qmi_close_finish (MMPortQmi     *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
qmi_device_close_ready (QmiDevice    *qmi_device,
                        GAsyncResult *res,
                        GTask        *task)
{
    GError    *error = NULL;
    MMPortQmi *self;

    self = g_task_get_source_object (task);

    g_assert (!self->priv->qmi_device);
    self->priv->in_progress = FALSE;

    if (!qmi_device_close_finish (qmi_device, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_port_qmi_close (MMPortQmi           *self,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    PortQmiCloseContext *ctx;
    GTask               *task;
    GList               *l;

    g_return_if_fail (MM_IS_PORT_QMI (self));

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->in_progress) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_IN_PROGRESS,
                                 "QMI device open/close operation in progress");
        g_object_unref (task);
        return;
    }

    if (!self->priv->qmi_device) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    self->priv->in_progress = TRUE;

    /* Store device to close in the context */
    ctx = g_slice_new0 (PortQmiCloseContext);
    ctx->qmi_device = g_steal_pointer (&self->priv->qmi_device);
    g_task_set_task_data (task, ctx, (GDestroyNotify)port_qmi_close_context_free);

    /* Release all allocated clients */
    for (l = self->priv->services; l; l = g_list_next (l)) {
        ServiceInfo *info = l->data;

        mm_obj_dbg (self, "Releasing client for service '%s'...", qmi_service_get_string (info->service));
        qmi_device_release_client (ctx->qmi_device,
                                   info->client,
                                   QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                                   3, NULL, NULL, NULL);
        g_clear_object (&info->client);
    }
    g_list_free_full (self->priv->services, g_free);
    self->priv->services = NULL;

    qmi_device_close_async (ctx->qmi_device,
                            5,
                            NULL,
                            (GAsyncReadyCallback)qmi_device_close_ready,
                            task);
}

/*****************************************************************************/

MMPortQmi *
mm_port_qmi_new (const gchar  *name,
                 MMPortSubsys  subsys)
{
    return MM_PORT_QMI (g_object_new (MM_TYPE_PORT_QMI,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, subsys,
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
    g_list_free_full (self->priv->services, g_free);
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
