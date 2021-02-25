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
#include "mm-port-net.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-log-object.h"

#define DEFAULT_LINK_PREALLOCATED_AMOUNT 4

G_DEFINE_TYPE (MMPortQmi, mm_port_qmi, MM_TYPE_PORT)

typedef struct {
    QmiService     service;
    QmiClient     *client;
    MMPortQmiFlag  flag;
} ServiceInfo;

struct _MMPortQmiPrivate {
    gboolean   in_progress;
    QmiDevice *qmi_device;
    GList     *services;
    /* endpoint info */
    gulong              endpoint_info_signal_id;
    QmiDataEndpointType endpoint_type;
    gint                endpoint_interface_number;
    /* kernel data format */
    QmiDeviceExpectedDataFormat kernel_data_format;
    /* wda settings */
    gboolean                      wda_unsupported;
    QmiWdaLinkLayerProtocol       llp;
    QmiWdaDataAggregationProtocol dap;
    /* preallocated links */
    MMPort   *preallocated_links_master;
    GArray   *preallocated_links;
    GList    *preallocated_links_setup_pending;
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

static void
initialize_endpoint_info (MMPortQmi *self)
{
    MMKernelDevice *kernel_device;

    kernel_device = mm_port_peek_kernel_device (MM_PORT (self));

    if (!kernel_device)
        self->priv->endpoint_type = QMI_DATA_ENDPOINT_TYPE_UNDEFINED;
    else
        self->priv->endpoint_type = mm_port_subsys_to_qmi_endpoint_type (mm_port_get_subsys (MM_PORT (self)));

    switch (self->priv->endpoint_type) {
        case QMI_DATA_ENDPOINT_TYPE_HSUSB:
            g_assert (kernel_device);
            self->priv->endpoint_interface_number = mm_kernel_device_get_interface_number (kernel_device);
            break;
        case QMI_DATA_ENDPOINT_TYPE_EMBEDDED:
            self->priv->endpoint_interface_number = 1;
            break;
        case QMI_DATA_ENDPOINT_TYPE_PCIE:
        case QMI_DATA_ENDPOINT_TYPE_UNDEFINED:
        case QMI_DATA_ENDPOINT_TYPE_HSIC:
        case QMI_DATA_ENDPOINT_TYPE_BAM_DMUX:
        case QMI_DATA_ENDPOINT_TYPE_UNKNOWN:
        default:
            self->priv->endpoint_interface_number = 0;
            break;
    }

    mm_obj_dbg (self, "endpoint info updated: type '%s', interface number '%u'",
                qmi_data_endpoint_type_get_string (self->priv->endpoint_type),
                self->priv->endpoint_interface_number);
}

QmiDataEndpointType
mm_port_qmi_get_endpoint_type (MMPortQmi *self)
{
    return self->priv->endpoint_type;
}

guint
mm_port_qmi_get_endpoint_interface_number (MMPortQmi *self)
{
    return self->priv->endpoint_interface_number;
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

typedef struct {
    gchar    *link_name;
    guint     mux_id;
    gboolean  setup;
} PreallocatedLinkInfo;

static void
preallocated_link_info_clear (PreallocatedLinkInfo *info)
{
    g_free (info->link_name);
}

static void
delete_preallocated_links (QmiDevice *qmi_device,
                           GArray    *preallocated_links)
{
    guint i;

    /* This link deletion cleanup may fail if the master interface is up
     * (a limitation of qmi_wwan in some kernel versions). It's just a minor
     * inconvenience really, if MM restarts they'll be all removed during
     * initialization anyway */

    for (i = 0; i < preallocated_links->len; i++) {
        PreallocatedLinkInfo *info;

        info = &g_array_index (preallocated_links, PreallocatedLinkInfo, i);
        qmi_device_delete_link (qmi_device, info->link_name, info->mux_id,
                                NULL, NULL, NULL);
    }
}

static guint
count_preallocated_links_setup (MMPortQmi *self)
{
    guint i;
    guint count = 0;

    for (i = 0; self->priv->preallocated_links && (i < self->priv->preallocated_links->len); i++) {
        PreallocatedLinkInfo *info;

        info = &g_array_index (self->priv->preallocated_links, PreallocatedLinkInfo, i);
        if (info->setup)
            count++;
    }

    return count;
}

static gboolean
release_preallocated_link (MMPortQmi    *self,
                           const gchar  *link_name,
                           guint         mux_id,
                           GError     **error)
{
    guint i;

    for (i = 0; self->priv->preallocated_links && (i < self->priv->preallocated_links->len); i++) {
        PreallocatedLinkInfo *info;

        info = &g_array_index (self->priv->preallocated_links, PreallocatedLinkInfo, i);
        if (!info->setup || (g_strcmp0 (info->link_name, link_name) != 0) || (info->mux_id != mux_id))
            continue;

        info->setup = FALSE;
        return TRUE;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "No preallocated link found to release");
    return FALSE;
}

static gboolean
acquire_preallocated_link (MMPortQmi  *self,
                           MMPort     *master,
                           gchar     **link_name,
                           guint      *mux_id,
                           GError    **error)
{
    guint i;

    if (!self->priv->qmi_device) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                     "port is closed");
        return FALSE;
    }

    if (!self->priv->preallocated_links || !self->priv->preallocated_links_master) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "No preallocated links available");
        return FALSE;
    }

    if ((master != self->priv->preallocated_links_master) &&
        (g_strcmp0 (mm_port_get_device (master), mm_port_get_device (self->priv->preallocated_links_master)) != 0)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Preallocated links available in 'net/%s', not in 'net/%s'",
                     mm_port_get_device (self->priv->preallocated_links_master),
                     mm_port_get_device (master));
        return FALSE;
    }

    for (i = 0; i < self->priv->preallocated_links->len; i++) {
        PreallocatedLinkInfo *info;

        info = &g_array_index (self->priv->preallocated_links, PreallocatedLinkInfo, i);
        if (info->setup)
            continue;

        info->setup = TRUE;
        *link_name = g_strdup (info->link_name);
        *mux_id = info->mux_id;
        return TRUE;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "No more preallocated links available");
    return FALSE;
}

/*****************************************************************************/

typedef struct {
    QmiDevice *qmi_device;
    MMPort    *data;
    GArray    *preallocated_links;
} InitializePreallocatedLinksContext;

static void
initialize_preallocated_links_context_free (InitializePreallocatedLinksContext *ctx)
{
    if (ctx->preallocated_links) {
        delete_preallocated_links (ctx->qmi_device, ctx->preallocated_links);
        g_array_unref (ctx->preallocated_links);
    }
    g_object_unref (ctx->qmi_device);
    g_object_unref (ctx->data);
    g_slice_free (InitializePreallocatedLinksContext, ctx);
}

static GArray *
initialize_preallocated_links_finish (MMPortQmi     *self,
                                      GAsyncResult  *res,
                                      GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void initialize_preallocated_links_next (GTask *task);

static void
device_add_link_preallocated_ready (QmiDevice     *device,
                                    GAsyncResult  *res,
                                    GTask         *task)
{
    InitializePreallocatedLinksContext *ctx;
    GError                             *error = NULL;
    PreallocatedLinkInfo                info = { NULL, 0, FALSE };

    ctx = g_task_get_task_data (task);

    info.link_name = qmi_device_add_link_finish (device, res, &info.mux_id, &error);
    if (!info.link_name) {
        g_prefix_error (&error, "failed to add preallocated link (%u/%u) for device: ",
                        ctx->preallocated_links->len + 1, DEFAULT_LINK_PREALLOCATED_AMOUNT);
        g_task_return_error (task, error);
        return;
    }

    g_array_append_val (ctx->preallocated_links, info);
    initialize_preallocated_links_next (task);
}

static void
initialize_preallocated_links_next (GTask *task)
{
    MMPortQmi                          *self;
    InitializePreallocatedLinksContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* if we were closed while allocating, bad thing, abort */
    if (!self->priv->qmi_device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED, "port is closed");
        g_object_unref (task);
        return;
    }

    if (ctx->preallocated_links->len == DEFAULT_LINK_PREALLOCATED_AMOUNT) {
        g_task_return_pointer (task, g_steal_pointer (&ctx->preallocated_links), (GDestroyNotify)g_array_unref);
        g_object_unref (task);
        return;
    }

    qmi_device_add_link (self->priv->qmi_device,
                         ctx->preallocated_links->len + 1,
                         mm_kernel_device_get_name (mm_port_peek_kernel_device (ctx->data)),
                         "ignored", /* n/a in qmi_wwan add_mux */
                         NULL,
                         (GAsyncReadyCallback) device_add_link_preallocated_ready,
                         task);
}

static void
initialize_preallocated_links (MMPortQmi           *self,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
    InitializePreallocatedLinksContext *ctx;
    GTask                              *task;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (InitializePreallocatedLinksContext);
    ctx->qmi_device = g_object_ref (self->priv->qmi_device);
    ctx->data = g_object_ref (self->priv->preallocated_links_master);
    ctx->preallocated_links = g_array_sized_new (FALSE, FALSE, sizeof (PreallocatedLinkInfo), DEFAULT_LINK_PREALLOCATED_AMOUNT);
    g_array_set_clear_func (ctx->preallocated_links, (GDestroyNotify)preallocated_link_info_clear);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialize_preallocated_links_context_free);

    initialize_preallocated_links_next (task);
}

/*****************************************************************************/

typedef struct {
    MMPort *master;
    gchar  *link_name;
    guint   mux_id;
} SetupLinkContext;

static void
setup_link_context_free (SetupLinkContext *ctx)
{
    g_free (ctx->link_name);
    g_clear_object (&ctx->master);
    g_slice_free (SetupLinkContext, ctx);
}

gchar *
mm_port_qmi_setup_link_finish (MMPortQmi     *self,
                               GAsyncResult  *res,
                               guint         *mux_id,
                               GError       **error)
{
    SetupLinkContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return NULL;

    ctx = g_task_get_task_data (G_TASK (res));
    if (mux_id)
        *mux_id = ctx->mux_id;
    return g_steal_pointer (&ctx->link_name);
}

static void
device_add_link_ready (QmiDevice    *device,
                       GAsyncResult *res,
                       GTask        *task)
{
    SetupLinkContext *ctx;
    GError           *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->link_name = qmi_device_add_link_finish (device, res, &ctx->mux_id, &error);
    if (!ctx->link_name) {
        g_prefix_error (&error, "failed to add link for device: ");
        g_task_return_error (task, error);
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
setup_preallocated_link (GTask *task)
{
    MMPortQmi        *self;
    SetupLinkContext *ctx;
    GError           *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!acquire_preallocated_link (self, ctx->master, &ctx->link_name, &ctx->mux_id, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
initialize_preallocated_links_ready (MMPortQmi    *self,
                                     GAsyncResult *res,
                                     GTask        *task)
{
    g_autoptr(GError) error = NULL;

    g_assert (!self->priv->preallocated_links);
    self->priv->preallocated_links = initialize_preallocated_links_finish (self, res, &error);
    if (!self->priv->preallocated_links) {
        /* We need to fail this task and all the additional tasks also pending */
        g_task_return_error (task, g_error_copy (error));
        g_object_unref (task);
        while (self->priv->preallocated_links_setup_pending) {
            g_task_return_error (self->priv->preallocated_links_setup_pending->data, g_error_copy (error));
            g_object_unref (self->priv->preallocated_links_setup_pending->data);
            self->priv->preallocated_links_setup_pending = g_list_delete_link (self->priv->preallocated_links_setup_pending,
                                                                               self->priv->preallocated_links_setup_pending);
        }
        /* and reset back the master, because we're not really initialized */
        g_clear_object (&self->priv->preallocated_links_master);
        return;
    }

    /* Now we know preallocated links are available, complete our task and all the pending ones */
    setup_preallocated_link (task);
    while (self->priv->preallocated_links_setup_pending) {
        setup_preallocated_link (self->priv->preallocated_links_setup_pending->data);
        self->priv->preallocated_links_setup_pending = g_list_delete_link (self->priv->preallocated_links_setup_pending,
                                                                           self->priv->preallocated_links_setup_pending);
    }
}

void
mm_port_qmi_setup_link (MMPortQmi           *self,
                        MMPort              *data,
                        const gchar         *link_prefix_hint,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    SetupLinkContext *ctx;
    GTask            *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->qmi_device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Port is not open");
        g_object_unref (task);
        return;
    }

    if ((self->priv->dap != QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5) &&
        (self->priv->dap != QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Aggregation not enabled");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SetupLinkContext);
    ctx->master = g_object_ref (data);
    ctx->mux_id = QMI_DEVICE_MUX_ID_UNBOUND;
    g_task_set_task_data (task, ctx, (GDestroyNotify) setup_link_context_free);

    /* For all drivers except for qmi_wwan, or when qmi_wwan is setup with
     * qmap-pass-through, just try to add link in the QmiDevice */
    if ((mm_port_get_subsys (MM_PORT (self)) != MM_PORT_SUBSYS_USBMISC) ||
        self->priv->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH) {
        qmi_device_add_link (self->priv->qmi_device,
                             QMI_DEVICE_MUX_ID_AUTOMATIC,
                             mm_kernel_device_get_name (mm_port_peek_kernel_device (data)),
                             link_prefix_hint,
                             NULL,
                             (GAsyncReadyCallback) device_add_link_ready,
                             task);
        return;
    }

    /* For qmi_wwan, use preallocated links */
    if (self->priv->preallocated_links) {
        setup_preallocated_link (task);
        return;
    }

    /* We must make sure we don't run this procedure in parallel (e.g. if multiple
     * connection attempts reach at the same time), so if we're told the preallocated
     * links are already being initialized (master is set) but the array didn't exist,
     * queue our task for completion once we're fully initialized */
    if (self->priv->preallocated_links_master) {
        self->priv->preallocated_links_setup_pending = g_list_append (self->priv->preallocated_links_setup_pending, task);
        return;
    }

    /* Store master to flag that we're initializing preallocated links */
    self->priv->preallocated_links_master = g_object_ref (data);
    initialize_preallocated_links (self,
                                   (GAsyncReadyCallback) initialize_preallocated_links_ready,
                                   task);
}

/*****************************************************************************/

gboolean
mm_port_qmi_cleanup_link_finish (MMPortQmi     *self,
                                 GAsyncResult  *res,
                                 GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
device_delete_link_ready (QmiDevice    *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    GError *error = NULL;

    if (!qmi_device_delete_link_finish (device, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_port_qmi_cleanup_link (MMPortQmi           *self,
                          const gchar         *link_name,
                          guint                mux_id,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    GTask  *task;
    GError *error = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->qmi_device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Port is not open");
        g_object_unref (task);
        return;
    }

    if ((self->priv->dap != QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5) &&
        (self->priv->dap != QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Aggregation not enabled");
        g_object_unref (task);
        return;
    }

    /* For all drivers except for qmi_wwan, or when qmi_wwan is setup with
     * qmap-pass-through, just try to delete link in the QmiDevice */
    if ((mm_port_get_subsys (MM_PORT (self)) != MM_PORT_SUBSYS_USBMISC) ||
        self->priv->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH) {
        qmi_device_delete_link (self->priv->qmi_device,
                                link_name,
                                mux_id,
                                NULL,
                                (GAsyncReadyCallback) device_delete_link_ready,
                                task);
        return;
    }

    /* For qmi_wwan, use preallocated links */
    if (!release_preallocated_link (self, link_name, mux_id, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/*****************************************************************************/

typedef struct {
    QmiDevice *device;
    MMPort    *data;
} InternalResetContext;

static void
internal_reset_context_free (InternalResetContext *ctx)
{
    g_clear_object (&ctx->device);
    g_clear_object (&ctx->data);
    g_slice_free (InternalResetContext, ctx);
}

static gboolean
internal_reset_finish (MMPortQmi     *self,
                       GAsyncResult  *res,
                       GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
delete_all_links_ready (QmiDevice    *device,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMPortQmi            *self;
    InternalResetContext *ctx;
    GError               *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* link deletion not fatal, it may happen if in 802.3 already */
    if (!qmi_device_delete_all_links_finish (device, res, &error)) {
        mm_obj_dbg (self, "couldn't delete all links: %s", error->message);
        g_clear_error (&error);
    }

    /* expected data format only applicable to qmi_wwan */
    if (mm_port_get_subsys (MM_PORT (self)) == MM_PORT_SUBSYS_USBMISC) {
        mm_obj_dbg (self, "reseting expected kernel data format to 802.3 in data interface '%s'",
                    mm_port_get_device (MM_PORT (ctx->data)));
        if (!qmi_device_set_expected_data_format (ctx->device, QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3, &error)) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
    }

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
net_link_down_ready (MMPortNet    *data,
                     GAsyncResult *res,
                     GTask        *task)
{
    MMPortQmi            *self;
    InternalResetContext *ctx;
    GError               *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_port_net_link_setup_finish (data, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* first, delete all links found, if any */
    mm_obj_dbg (self, "deleting all links in data interface '%s'",
                mm_port_get_device (ctx->data));
    qmi_device_delete_all_links (ctx->device,
                                 mm_port_get_device (ctx->data),
                                 NULL,
                                 (GAsyncReadyCallback)delete_all_links_ready,
                                 task);
}

static void
internal_reset (MMPortQmi           *self,
                MMPort              *data,
                QmiDevice           *device,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    GTask                *task;
    InternalResetContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (InternalResetContext);
    ctx->data = g_object_ref (data);
    ctx->device = g_object_ref (device);
    g_task_set_task_data (task, ctx, (GDestroyNotify) internal_reset_context_free);

    /* first, bring down master interface */
    mm_obj_dbg (self, "bringing down data interface '%s'",
                mm_port_get_device (ctx->data));
    mm_port_net_link_setup (MM_PORT_NET (ctx->data),
                            FALSE,
                            MM_PORT_NET_MTU_DEFAULT,
                            NULL,
                            (GAsyncReadyCallback) net_link_down_ready,
                            task);
}

/*****************************************************************************/

typedef struct {
    QmiDevice *device;
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
mm_port_qmi_reset_finish (MMPortQmi     *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
internal_reset_ready (MMPortQmi    *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    GError *error = NULL;

    if (!internal_reset_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
reset_device_new_ready (GObject      *source,
                        GAsyncResult *res,
                        GTask        *task)
{
    MMPortQmi    *self;
    ResetContext *ctx;
    GError       *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    ctx->device = qmi_device_new_finish (res, &error);
    if (!ctx->device) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    internal_reset (self,
                    ctx->data,
                    ctx->device,
                    (GAsyncReadyCallback) internal_reset_ready,
                    task);
}

void
mm_port_qmi_reset (MMPortQmi           *self,
                   MMPort              *data,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask            *task;
    ResetContext     *ctx;
    g_autoptr(GFile)  file = NULL;
    g_autofree gchar *fullpath = NULL;

    task = g_task_new (self, NULL, callback, user_data);

    if (self->priv->qmi_device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Port is already open");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (ResetContext);
    ctx->data = g_object_ref (data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) reset_context_free);

    fullpath = g_strdup_printf ("/dev/%s", mm_port_get_device (MM_PORT (self)));
    file = g_file_new_for_path (fullpath);

    qmi_device_new (file, NULL,
                    (GAsyncReadyCallback) reset_device_new_ready,
                    task);
}

/*****************************************************************************/

QmiWdaLinkLayerProtocol
mm_port_qmi_get_link_layer_protocol (MMPortQmi *self)
{
    return self->priv->llp;
}

QmiWdaDataAggregationProtocol
mm_port_qmi_get_data_aggregation_protocol (MMPortQmi *self)
{
    return self->priv->dap;
}

/*****************************************************************************/

static QmiDeviceExpectedDataFormat
load_kernel_data_format_current (MMPortQmi *self,
                                 QmiDevice *device,
                                 gboolean  *supports_links)
{
    QmiDeviceExpectedDataFormat value;

    /* The flag specifying depending whether link management is supported DEPENDS
     * on the current expected data format; i.e. if 802-3 is currently selected it
     * will say link management is unsupported, even if it would be supported once
     * we change it to raw-ip */
    if (supports_links)
        *supports_links = qmi_device_check_link_supported (device, NULL);

    /* For any driver other than qmi_wwan, assume raw-ip */
    if (mm_port_get_subsys (MM_PORT (self)) != MM_PORT_SUBSYS_USBMISC)
        return QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP;

    /* If the expected data format is unknown, it means the kernel in use
     * doesn't have support for querying it; therefore it's 802.3 */
    value = qmi_device_get_expected_data_format (device, NULL);
    if (value == QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN)
        value = QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3;

    return value;
}

static void
load_kernel_data_format_capabilities (MMPortQmi *self,
                                      QmiDevice *device,
                                      gboolean  *supports_802_3,
                                      gboolean  *supports_raw_ip,
                                      gboolean  *supports_qmap_pass_through)
{
    /* For any driver other than qmi_wwan, assume raw-ip */
    if (mm_port_get_subsys (MM_PORT (self)) != MM_PORT_SUBSYS_USBMISC) {
        *supports_802_3 = FALSE;
        *supports_raw_ip = TRUE;
        *supports_qmap_pass_through = FALSE;
        return;
    }

    *supports_802_3 = TRUE;
    *supports_raw_ip = qmi_device_check_expected_data_format_supported (device,
                                                                        QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP,
                                                                        NULL);
    if (!*supports_raw_ip) {
        *supports_qmap_pass_through = FALSE;
        return;
    }

    *supports_qmap_pass_through = qmi_device_check_expected_data_format_supported (device,
                                                                                   QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH,
                                                                                   NULL);
}

/*****************************************************************************/

#define DEFAULT_DOWNLINK_DATA_AGGREGATION_MAX_SIZE      32768
#define DEFAULT_DOWNLINK_DATA_AGGREGATION_MAX_DATAGRAMS 32

typedef struct {
    QmiDeviceExpectedDataFormat   kernel_data_format;
    QmiWdaLinkLayerProtocol       wda_llp;
    QmiWdaDataAggregationProtocol wda_dap;
} DataFormatCombination;

static const DataFormatCombination data_format_combinations[] = {
    { QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH, QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5   },
    { QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH, QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP     },
    { QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP,            QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5   },
    { QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP,            QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP     },
    { QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP,            QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED },
    { QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3,             QMI_WDA_LINK_LAYER_PROTOCOL_802_3,  QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED },
};

typedef enum {
    INTERNAL_SETUP_DATA_FORMAT_STEP_FIRST,
    INTERNAL_SETUP_DATA_FORMAT_STEP_KERNEL_DATA_FORMAT_CAPABILITIES,
    INTERNAL_SETUP_DATA_FORMAT_STEP_RETRY,
    INTERNAL_SETUP_DATA_FORMAT_STEP_KERNEL_DATA_FORMAT_CURRENT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_ALLOCATE_WDA_CLIENT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_GET_WDA_DATA_FORMAT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_QUERY_DONE,
    INTERNAL_SETUP_DATA_FORMAT_STEP_CHECK_DATA_FORMAT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_SYNC_WDA_DATA_FORMAT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_SYNC_KERNEL_DATA_FORMAT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_LAST,
} InternalSetupDataFormatStep;

typedef struct {
    QmiDevice                      *device;
    MMPort                         *data;
    MMPortQmiSetupDataFormatAction  action;

    InternalSetupDataFormatStep step;
    gboolean                    use_endpoint;
    gint                        data_format_combination_i;

    /* configured kernel data format, mainly when using qmi_wwan */
    QmiDeviceExpectedDataFormat kernel_data_format_current;
    QmiDeviceExpectedDataFormat kernel_data_format_requested;
    gboolean                    kernel_data_format_802_3_supported;
    gboolean                    kernel_data_format_raw_ip_supported;
    gboolean                    kernel_data_format_qmap_pass_through_supported;
    gboolean                    link_management_supported;

    /* configured device data format */
    QmiClient                     *wda;
    QmiWdaLinkLayerProtocol        wda_llp_current;
    QmiWdaLinkLayerProtocol        wda_llp_requested;
    QmiWdaDataAggregationProtocol  wda_ul_dap_current;
    QmiWdaDataAggregationProtocol  wda_ul_dap_requested;
    QmiWdaDataAggregationProtocol  wda_dl_dap_current;
    QmiWdaDataAggregationProtocol  wda_dl_dap_requested;
    guint32                        wda_dl_dap_max_datagrams_current;
    guint32                        wda_dl_dap_max_size_current;
    gboolean                       wda_dap_supported;
} InternalSetupDataFormatContext;

static void
internal_setup_data_format_context_free (InternalSetupDataFormatContext *ctx)
{
    if (ctx->wda && ctx->device)
        qmi_device_release_client (ctx->device,
                                   ctx->wda,
                                   QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                                   3, NULL, NULL, NULL);
    g_clear_object (&ctx->wda);
    g_clear_object (&ctx->data);
    g_clear_object (&ctx->device);
    g_slice_free (InternalSetupDataFormatContext, ctx);
}

static gboolean
internal_setup_data_format_finish (MMPortQmi                      *self,
                                   GAsyncResult                   *res,
                                   QmiDeviceExpectedDataFormat    *out_kernel_data_format,
                                   QmiWdaLinkLayerProtocol        *out_llp,
                                   QmiWdaDataAggregationProtocol  *out_dap,
                                   GError                        **error)
{
    InternalSetupDataFormatContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    *out_kernel_data_format = ctx->kernel_data_format_current;
    *out_llp = ctx->wda_llp_current;
    g_assert (ctx->wda_dl_dap_current == ctx->wda_ul_dap_current);
    *out_dap = ctx->wda_dl_dap_current;
    return TRUE;
}

static void internal_setup_data_format_context_step (GTask *task);

static void
sync_kernel_data_format (GTask *task)
{
    MMPortQmi              *self;
    InternalSetupDataFormatContext *ctx;
    GError                 *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    mm_obj_dbg (self, "Updating kernel expected data format: %s -> %s",
                qmi_device_expected_data_format_get_string (ctx->kernel_data_format_current),
                qmi_device_expected_data_format_get_string (ctx->kernel_data_format_requested));

    if (!qmi_device_set_expected_data_format (ctx->device,
                                              ctx->kernel_data_format_requested,
                                              &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* request reload */
    ctx->kernel_data_format_current = QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN;

    /* Go on to next step */
    ctx->step++;
    internal_setup_data_format_context_step (task);
}

static void
set_data_format_ready (QmiClientWda *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    InternalSetupDataFormatContext              *ctx;
    g_autoptr(QmiMessageWdaSetDataFormatOutput)  output = NULL;
    g_autoptr(GError)                            error = NULL;

    ctx = g_task_get_task_data (task);

    output = qmi_client_wda_set_data_format_finish (client, res, &error);
    if (!output || !qmi_message_wda_set_data_format_output_get_result (output, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* request reload */
    ctx->wda_llp_current = QMI_WDA_LINK_LAYER_PROTOCOL_UNKNOWN;
    ctx->wda_ul_dap_current = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    ctx->wda_dl_dap_current = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;

    /* Go on to next step */
    ctx->step++;
    internal_setup_data_format_context_step (task);
}

static void
sync_wda_data_format (GTask *task)
{
    MMPortQmi                                  *self;
    InternalSetupDataFormatContext             *ctx;
    g_autoptr(QmiMessageWdaSetDataFormatInput)  input = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (ctx->wda_llp_current != ctx->wda_llp_requested)
        mm_obj_dbg (self, "Updating device link layer protocol: %s -> %s",
                    qmi_wda_link_layer_protocol_get_string (ctx->wda_llp_current),
                    qmi_wda_link_layer_protocol_get_string (ctx->wda_llp_requested));

    if (ctx->wda_ul_dap_current != ctx->wda_ul_dap_requested)
        mm_obj_dbg (self, "Updating device uplink data aggregation protocol: %s -> %s",
                    qmi_wda_data_aggregation_protocol_get_string (ctx->wda_ul_dap_current),
                    qmi_wda_data_aggregation_protocol_get_string (ctx->wda_ul_dap_requested));

    if (ctx->wda_dl_dap_current != ctx->wda_dl_dap_requested)
        mm_obj_dbg (self, "Updating device downlink data aggregation protocol: %s -> %s",
                    qmi_wda_data_aggregation_protocol_get_string (ctx->wda_dl_dap_current),
                    qmi_wda_data_aggregation_protocol_get_string (ctx->wda_dl_dap_requested));

    input = qmi_message_wda_set_data_format_input_new ();
    qmi_message_wda_set_data_format_input_set_link_layer_protocol (input, ctx->wda_llp_requested, NULL);
    qmi_message_wda_set_data_format_input_set_uplink_data_aggregation_protocol (input, ctx->wda_ul_dap_requested, NULL);
    qmi_message_wda_set_data_format_input_set_downlink_data_aggregation_protocol (input, ctx->wda_dl_dap_requested, NULL);
    if (ctx->wda_dl_dap_requested != QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED) {
        qmi_message_wda_set_data_format_input_set_downlink_data_aggregation_max_size (input, DEFAULT_DOWNLINK_DATA_AGGREGATION_MAX_SIZE, NULL);
        qmi_message_wda_set_data_format_input_set_downlink_data_aggregation_max_datagrams (input, DEFAULT_DOWNLINK_DATA_AGGREGATION_MAX_DATAGRAMS, NULL);
    }
    if (ctx->use_endpoint)
        qmi_message_wda_set_data_format_input_set_endpoint_info (input, self->priv->endpoint_type, self->priv->endpoint_interface_number, NULL);

    qmi_client_wda_set_data_format (QMI_CLIENT_WDA (ctx->wda),
                                    input,
                                    10,
                                    g_task_get_cancellable (task),
                                    (GAsyncReadyCallback) set_data_format_ready,
                                    task);
}

static gboolean
setup_data_format_completed (GTask *task)
{
    MMPortQmi                      *self;
    InternalSetupDataFormatContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* if aggregation enabled we require link management supported; this covers the
     * case of old qmi_wwan drivers where add_mux/del_mux wasn't available yet  */
    if (((ctx->wda_dl_dap_requested == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5) ||
         (ctx->wda_ul_dap_requested == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP)) &&
        !ctx->link_management_supported) {
        mm_obj_dbg (self, "cannot enable data aggregation: link management unsupported");
        return FALSE;
    }

    /* check whether the current and requested ones are the same */
    if ((ctx->kernel_data_format_current == ctx->kernel_data_format_requested) &&
        (ctx->wda_llp_current            == ctx->wda_llp_requested) &&
        (ctx->wda_ul_dap_current         == ctx->wda_ul_dap_requested) &&
        (ctx->wda_dl_dap_current         == ctx->wda_dl_dap_requested)) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return TRUE;
    }

    return FALSE;
}

static void
check_data_format (GTask *task)
{
    MMPortQmi              *self;
    InternalSetupDataFormatContext *ctx;
    gboolean                first_iteration;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    first_iteration = (ctx->data_format_combination_i < 0);
    if (!first_iteration && setup_data_format_completed (task))
        return;

    /* go on to the next supported combination */
    for (++ctx->data_format_combination_i;
         ctx->data_format_combination_i <= (gint)G_N_ELEMENTS (data_format_combinations);
         ctx->data_format_combination_i++) {
        const DataFormatCombination *combination;

        combination = &data_format_combinations[ctx->data_format_combination_i];

        if ((combination->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3) &&
            !ctx->kernel_data_format_802_3_supported)
            continue;
        if ((combination->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP) &&
            !ctx->kernel_data_format_raw_ip_supported)
            continue;
        if ((combination->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH) &&
            !ctx->kernel_data_format_qmap_pass_through_supported)
            continue;

        if (((combination->wda_dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5) ||
             (combination->wda_dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP)) &&
            ((!ctx->wda_dap_supported) ||
             (ctx->action != MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_SET_MULTIPLEX)))
            continue;

        mm_obj_dbg (self, "selected data format setup:");
        mm_obj_dbg (self, "    kernel format: %s", qmi_device_expected_data_format_get_string (combination->kernel_data_format));
        mm_obj_dbg (self, "    link layer protocol: %s", qmi_wda_link_layer_protocol_get_string (combination->wda_llp));
        mm_obj_dbg (self, "    aggregation protocol: %s", qmi_wda_data_aggregation_protocol_get_string (combination->wda_dap));

        ctx->kernel_data_format_requested = combination->kernel_data_format;
        ctx->wda_llp_requested            = combination->wda_llp;
        ctx->wda_ul_dap_requested         = combination->wda_dap;
        ctx->wda_dl_dap_requested         = combination->wda_dap;

        if (first_iteration && setup_data_format_completed (task))
            return;

        /* Go on to next step */
        ctx->step++;
        internal_setup_data_format_context_step (task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "No more data format combinations supported");
    g_object_unref (task);
}

static gboolean
process_data_format_output (MMPortQmi                         *self,
                            QmiMessageWdaGetDataFormatOutput  *output,
                            InternalSetupDataFormatContext    *ctx,
                            GError                           **error)
{
    /* Let's consider the lack o the LLP TLV a hard error; it really would be strange
     * a module supporting WDA Get Data Format but not containing the LLP info */
    if (!qmi_message_wda_get_data_format_output_get_link_layer_protocol (output, &ctx->wda_llp_current, error))
        return FALSE;

    /* QMAP assumed supported if both uplink and downlink TLVs are given */
    ctx->wda_dap_supported = TRUE;
    if (!qmi_message_wda_get_data_format_output_get_uplink_data_aggregation_protocol (output, &ctx->wda_ul_dap_current, NULL))
        ctx->wda_dap_supported = FALSE;
    if (!qmi_message_wda_get_data_format_output_get_downlink_data_aggregation_protocol (output, &ctx->wda_dl_dap_current, NULL))
        ctx->wda_dap_supported = FALSE;

    ctx->wda_dl_dap_max_size_current = 0;
    ctx->wda_dl_dap_max_datagrams_current = 0;
    if (ctx->wda_dl_dap_current != QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED) {
        qmi_message_wda_get_data_format_output_get_downlink_data_aggregation_max_size (output, &ctx->wda_dl_dap_max_size_current, NULL);
        qmi_message_wda_get_data_format_output_get_downlink_data_aggregation_max_datagrams (output, &ctx->wda_dl_dap_max_datagrams_current, NULL);
    }
    return TRUE;
}

static void
get_data_format_ready (QmiClientWda *client,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMPortQmi                                   *self;
    InternalSetupDataFormatContext              *ctx;
    g_autoptr(QmiMessageWdaGetDataFormatOutput)  output = NULL;
    g_autoptr(GError)                            error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    output = qmi_client_wda_get_data_format_finish (client, res, &error);
    if (!output ||
        !qmi_message_wda_get_data_format_output_get_result (output, &error) ||
        !process_data_format_output (self, output, ctx, &error)) {
        /* A 'missing argument' error when querying data format is seen in new
         * devices like the Quectel RM500Q, requiring the 'endpoint info' TLV.
         * When this happens, retry the step with the missing TLV.
         *
         * Note that this is not an additional step, we're still in the
         * GET_WDA_DATA_FORMAT step.
         */
        if (g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_MISSING_ARGUMENT) &&
            (self->priv->endpoint_type != QMI_DATA_ENDPOINT_TYPE_UNDEFINED)) {
            /* retry same step with endpoint info */
            ctx->use_endpoint = TRUE;
            internal_setup_data_format_context_step (task);
            return;
        }

        /* otherwise, fatal */
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    internal_setup_data_format_context_step (task);
}

static void
allocate_client_wda_ready (QmiDevice    *device,
                           GAsyncResult *res,
                           GTask        *task)
{
    InternalSetupDataFormatContext *ctx;
    GError                         *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->wda = qmi_device_allocate_client_finish (device, res, &error);
    if (!ctx->wda) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    internal_setup_data_format_context_step (task);
}

static void
internal_setup_data_format_context_step (GTask *task)
{
    MMPortQmi                      *self;
    InternalSetupDataFormatContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
        case INTERNAL_SETUP_DATA_FORMAT_STEP_FIRST:
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_KERNEL_DATA_FORMAT_CAPABILITIES:
            /* Load kernel data format capabilities, only on first loop iteration */
            load_kernel_data_format_capabilities (self,
                                                  ctx->device,
                                                  &ctx->kernel_data_format_802_3_supported,
                                                  &ctx->kernel_data_format_raw_ip_supported,
                                                  &ctx->kernel_data_format_qmap_pass_through_supported);
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_RETRY:
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_KERNEL_DATA_FORMAT_CURRENT:
            /* Only reload kernel data format if it was updated or on first loop */
            if (ctx->kernel_data_format_current == QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN) {
                ctx->kernel_data_format_current = load_kernel_data_format_current (self,
                                                                                   ctx->device,
                                                                                   &ctx->link_management_supported);
            }
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_ALLOCATE_WDA_CLIENT:
            /* Only allocate new WDA client on first loop */
            if (ctx->data_format_combination_i < 0) {
                g_assert (!ctx->wda);
                qmi_device_allocate_client (ctx->device,
                                            QMI_SERVICE_WDA,
                                            QMI_CID_NONE,
                                            10,
                                            g_task_get_cancellable (task),
                                            (GAsyncReadyCallback) allocate_client_wda_ready,
                                            task);
                return;
            }
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_GET_WDA_DATA_FORMAT:
            /* Only reload WDA data format if it was updated or on first loop */
            if (ctx->wda_llp_current == QMI_WDA_LINK_LAYER_PROTOCOL_UNKNOWN) {
                g_autoptr(QmiMessageWdaGetDataFormatInput) input = NULL;

                if (ctx->use_endpoint) {
                    input = qmi_message_wda_get_data_format_input_new ();
                    qmi_message_wda_get_data_format_input_set_endpoint_info (input,
                                                                             self->priv->endpoint_type,
                                                                             self->priv->endpoint_interface_number,
                                                                             NULL);
                }
                qmi_client_wda_get_data_format (QMI_CLIENT_WDA (ctx->wda),
                                                input,
                                                10,
                                                g_task_get_cancellable (task),
                                                (GAsyncReadyCallback) get_data_format_ready,
                                                task);
                return;
            }
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_QUERY_DONE:
            mm_obj_dbg (self, "current data format setup:");
            mm_obj_dbg (self, "    kernel format: %s", qmi_device_expected_data_format_get_string (ctx->kernel_data_format_current));
            mm_obj_dbg (self, "    link layer protocol: %s", qmi_wda_link_layer_protocol_get_string (ctx->wda_llp_current));
            mm_obj_dbg (self, "    link management: %s", ctx->link_management_supported ? "supported" : "unsupported");
            mm_obj_dbg (self, "    aggregation protocol ul: %s", qmi_wda_data_aggregation_protocol_get_string (ctx->wda_ul_dap_current));
            mm_obj_dbg (self, "    aggregation protocol dl: %s", qmi_wda_data_aggregation_protocol_get_string (ctx->wda_dl_dap_current));

            if (ctx->action == MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_QUERY) {
                g_task_return_boolean (task, TRUE);
                g_object_unref (task);
                return;
            }

            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_CHECK_DATA_FORMAT:
            /* This step is the one that may complete the async operation
             * successfully */
            check_data_format (task);
            return;

        case INTERNAL_SETUP_DATA_FORMAT_STEP_SYNC_WDA_DATA_FORMAT:
            if ((ctx->wda_llp_current != ctx->wda_llp_requested) ||
                (ctx->wda_ul_dap_current != ctx->wda_ul_dap_requested) ||
                (ctx->wda_dl_dap_current != ctx->wda_dl_dap_requested)) {
                sync_wda_data_format (task);
                return;
            }
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_SYNC_KERNEL_DATA_FORMAT:
            if (ctx->kernel_data_format_current != ctx->kernel_data_format_requested) {
                sync_kernel_data_format (task);
                return;
            }
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_LAST:
            /* jump back to first step to reload current state after
             * the updates have been done */
            ctx->step = INTERNAL_SETUP_DATA_FORMAT_STEP_RETRY;
            internal_setup_data_format_context_step (task);
            return;

        default:
            g_assert_not_reached ();
    }
}

static void
internal_setup_data_format (MMPortQmi                      *self,
                            QmiDevice                      *device,
                            MMPort                         *data, /* may be NULL in query */
                            MMPortQmiSetupDataFormatAction  action,
                            GAsyncReadyCallback             callback,
                            gpointer                        user_data)
{
    InternalSetupDataFormatContext *ctx;
    GTask                          *task;

    task = g_task_new (self, NULL, callback, user_data);

    if (!device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "Port must be open to setup data format");
        g_object_unref (task);
        return;
    }

    if (self->priv->wda_unsupported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Setting up data format is not supported");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (InternalSetupDataFormatContext);
    ctx->device = g_object_ref (device);
    ctx->data = data ? g_object_ref (data) : NULL;
    ctx->action = action;
    ctx->step = INTERNAL_SETUP_DATA_FORMAT_STEP_FIRST;
    ctx->data_format_combination_i = -1;
    ctx->kernel_data_format_current = QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN;
    ctx->kernel_data_format_requested = QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN;
    ctx->wda_llp_current = QMI_WDA_LINK_LAYER_PROTOCOL_UNKNOWN;
    ctx->wda_llp_requested = QMI_WDA_LINK_LAYER_PROTOCOL_UNKNOWN;
    ctx->wda_ul_dap_current = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    ctx->wda_ul_dap_requested = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    ctx->wda_dl_dap_current = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    ctx->wda_dl_dap_requested = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    g_task_set_task_data (task, ctx, (GDestroyNotify) internal_setup_data_format_context_free);

    internal_setup_data_format_context_step (task);
}

/*****************************************************************************/

typedef struct {
    MMPort                         *data;
    QmiDevice                      *device;
    MMPortQmiSetupDataFormatAction  action;
} SetupDataFormatContext;

static void
setup_data_format_context_free (SetupDataFormatContext *ctx)
{
    g_clear_object (&ctx->device);
    g_clear_object (&ctx->data);
    g_slice_free (SetupDataFormatContext, ctx);
}

gboolean
mm_port_qmi_setup_data_format_finish (MMPortQmi                *self,
                                      GAsyncResult             *res,
                                      GError                  **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
internal_setup_data_format_ready (MMPortQmi    *self,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    GError *error = NULL;

    if (!internal_setup_data_format_finish (self,
                                            res,
                                            &self->priv->kernel_data_format,
                                            &self->priv->llp,
                                            &self->priv->dap,
                                            &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
setup_data_format_internal_reset_ready (MMPortQmi    *self,
                                        GAsyncResult *res,
                                        GTask        *task)
{
    SetupDataFormatContext *ctx;
    GError                 *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!internal_reset_finish (self, res, &error)) {
        g_prefix_error (&error, "Couldn't reset interface before setting up data format: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* call internal method with the already open QmiDevice */
    internal_setup_data_format (self,
                                ctx->device,
                                ctx->data,
                                ctx->action,
                                (GAsyncReadyCallback)internal_setup_data_format_ready,
                                task);
}

static guint
count_links_setup (MMPortQmi *self,
                   MMPort    *data)
{
    if ((mm_port_get_subsys (MM_PORT (self)) != MM_PORT_SUBSYS_USBMISC) ||
        self->priv->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH) {
        g_autoptr(GPtrArray) links = NULL;
        g_autoptr(GError)    error = NULL;

        if (!qmi_device_list_links (self->priv->qmi_device,
                                    mm_port_get_device (data),
                                    &links,
                                    &error)) {
            mm_obj_warn (self, "couldn't list links in %s: %s",
                         mm_port_get_device (data),
                         error->message);
            return 0;
        }

        return links->len;
    }

    return count_preallocated_links_setup (self);
}

void
mm_port_qmi_setup_data_format (MMPortQmi                      *self,
                               MMPort                         *data,
                               MMPortQmiSetupDataFormatAction  action,
                               GAsyncReadyCallback             callback,
                               gpointer                        user_data)
{
    SetupDataFormatContext *ctx;
    GTask                  *task;

    /* External calls are never query */
    g_assert (action != MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_QUERY);
    g_assert (MM_IS_PORT (data));

    task = g_task_new (self, NULL, callback, user_data);

    if (!self->priv->qmi_device) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Port not open");
        g_object_unref (task);
        return;
    }

    if (self->priv->wda_unsupported) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "Setting up data format is unsupported");
        g_object_unref (task);
        return;
    }

    if ((action == MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_SET_MULTIPLEX) &&
        (self->priv->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP ||
         self->priv->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH) &&
        (self->priv->dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5 ||
         self->priv->dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP)) {
        mm_obj_dbg (self, "multiplex support already available when setting up data format");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    if ((action == MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_SET_DEFAULT) &&
        (self->priv->kernel_data_format != QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH) &&
        (self->priv->dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED)) {
        mm_obj_dbg (self, "multiplex support already disabled when setting up data format");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* support switching from multiplex to non-multiplex, but only if there are no active
     * links allocated */
    if ((action == MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_SET_DEFAULT) &&
        (self->priv->dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5 ||
         self->priv->dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP)) {
        guint n_links_setup;

        n_links_setup = count_links_setup (self, data);
        if (n_links_setup > 0) {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                     "Cannot switch to non-multiplex setup: %u links already setup exist",
                                     n_links_setup);
            g_object_unref (task);
            return;
        }
    }

    ctx = g_slice_new0 (SetupDataFormatContext);
    ctx->data = g_object_ref (data);
    ctx->device = g_object_ref (self->priv->qmi_device);
    ctx->action = action;
    g_task_set_task_data (task, ctx, (GDestroyNotify)setup_data_format_context_free);

    internal_reset (self,
                    data,
                    ctx->device,
                    (GAsyncReadyCallback)setup_data_format_internal_reset_ready,
                    task);
}

/*****************************************************************************/

typedef enum {
    PORT_OPEN_STEP_FIRST,
    PORT_OPEN_STEP_CHECK_OPENING,
    PORT_OPEN_STEP_CHECK_ALREADY_OPEN,
    PORT_OPEN_STEP_DEVICE_NEW,
    PORT_OPEN_STEP_OPEN_WITHOUT_DATA_FORMAT,
    PORT_OPEN_STEP_SETUP_DATA_FORMAT,
    PORT_OPEN_STEP_CLOSE_BEFORE_OPEN_WITH_DATA_FORMAT,
    PORT_OPEN_STEP_OPEN_WITH_DATA_FORMAT,
    PORT_OPEN_STEP_LAST
} PortOpenStep;

typedef struct {
    QmiDevice                   *device;
    GError                      *error;
    PortOpenStep                 step;
    gboolean                     set_data_format;
    QmiDeviceExpectedDataFormat  kernel_data_format;
} PortOpenContext;

static void
port_open_context_free (PortOpenContext *ctx)
{
    g_assert (!ctx->error);
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
        /* If the open with CTL data format is sucessful, update all settings
         * that we would have received with the internal setup data format
         * process */
        self->priv->kernel_data_format = ctx->kernel_data_format;
        if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP)
            self->priv->llp = QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP;
        else if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3)
            self->priv->llp = QMI_WDA_LINK_LAYER_PROTOCOL_802_3;
        else
            g_assert_not_reached ();
        self->priv->dap = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    }

    /* In both error and success, we go to last step */
    ctx->step++;
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
open_internal_setup_data_format_ready (MMPortQmi    *self,
                                       GAsyncResult *res,
                                       GTask        *task)
{
    PortOpenContext   *ctx;
    g_autoptr(GError)  error = NULL;

    ctx = g_task_get_task_data (task);

    if (!internal_setup_data_format_finish (self,
                                            res,
                                            &self->priv->kernel_data_format,
                                            &self->priv->llp,
                                            &self->priv->dap,
                                            &error)) {
        /* Continue with fallback to LLP requested via CTL */
        mm_obj_warn (self, "Couldn't setup data format: %s", error->message);
        self->priv->wda_unsupported = TRUE;
        ctx->step++;
    } else {
        /* on success, we're done */
        ctx->step = PORT_OPEN_STEP_LAST;
    }
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
        if (!self->priv->wda_unsupported) {
            /* Now open the QMI device without any data format CTL flag */
            mm_obj_dbg (self, "Opening device without data format update...");
            qmi_device_open (ctx->device,
                             (QMI_DEVICE_OPEN_FLAGS_VERSION_INFO |
                              QMI_DEVICE_OPEN_FLAGS_PROXY),
                             25,
                             g_task_get_cancellable (task),
                             (GAsyncReadyCallback) qmi_device_open_first_ready,
                             task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PORT_OPEN_STEP_SETUP_DATA_FORMAT:
        if (qmi_device_is_open (ctx->device)) {
            internal_setup_data_format (self,
                                        ctx->device,
                                        NULL,
                                        MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_QUERY,
                                        (GAsyncReadyCallback) open_internal_setup_data_format_ready,
                                        task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PORT_OPEN_STEP_CLOSE_BEFORE_OPEN_WITH_DATA_FORMAT:
        /* This fallback only applies when WDA unsupported */
        if (qmi_device_is_open (ctx->device)) {
            mm_obj_dbg (self, "Closing device to reopen it right away...");
            qmi_device_close_async (ctx->device,
                                    5,
                                    g_task_get_cancellable (task),
                                    (GAsyncReadyCallback) qmi_device_close_to_reopen_ready,
                                    task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case PORT_OPEN_STEP_OPEN_WITH_DATA_FORMAT: {
        QmiDeviceOpenFlags open_flags;

        /* Common open flags */
        open_flags = (QMI_DEVICE_OPEN_FLAGS_VERSION_INFO |
                      QMI_DEVICE_OPEN_FLAGS_PROXY        |
                      QMI_DEVICE_OPEN_FLAGS_NET_NO_QOS_HEADER);

        ctx->kernel_data_format = load_kernel_data_format_current (self, ctx->device, NULL);

        /* Need to reopen setting 802.3/raw-ip using CTL */
        if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP)
            open_flags |= QMI_DEVICE_OPEN_FLAGS_NET_RAW_IP;
        else if (ctx->kernel_data_format == QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3)
            open_flags |= QMI_DEVICE_OPEN_FLAGS_NET_802_3;
        else {
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Unexpected kernel data format: cannot setup using CTL");
            g_object_unref (task);
            return;
        }

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

    /* Cleanup preallocated links, if any */
    if (self->priv->preallocated_links) {
        delete_preallocated_links (ctx->qmi_device, self->priv->preallocated_links);
        g_clear_pointer (&self->priv->preallocated_links, g_array_unref);
    }
    g_clear_object (&self->priv->preallocated_links_master);

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
    MMPortQmi *self;

    self = MM_PORT_QMI (g_object_new (MM_TYPE_PORT_QMI,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, subsys,
                                      MM_PORT_TYPE, MM_PORT_TYPE_QMI,
                                      NULL));

    /* load endpoint info as soon as kernel device is set */
    self->priv->endpoint_info_signal_id = g_signal_connect (self,
                                                            "notify::" MM_PORT_KERNEL_DEVICE,
                                                            G_CALLBACK (initialize_endpoint_info),
                                                            NULL);

    return self;
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

    if (self->priv->endpoint_info_signal_id) {
        g_signal_handler_disconnect (self, self->priv->endpoint_info_signal_id);
        self->priv->endpoint_info_signal_id = 0;
    }

    /* Deallocate all clients */
    for (l = self->priv->services; l; l = g_list_next (l)) {
        ServiceInfo *info = l->data;

        if (info->client)
            g_object_unref (info->client);
    }
    g_list_free_full (self->priv->services, g_free);
    self->priv->services = NULL;

    /* Cleanup preallocated links, if any */
    if (self->priv->preallocated_links && self->priv->qmi_device)
        delete_preallocated_links (self->priv->qmi_device, self->priv->preallocated_links);
    g_clear_pointer (&self->priv->preallocated_links, g_array_unref);
    g_clear_object (&self->priv->preallocated_links_master);

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
