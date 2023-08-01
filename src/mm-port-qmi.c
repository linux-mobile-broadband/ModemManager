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
 * Copyright (C) 2012-2021 Google, Inc.
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>

#include <libqmi-glib.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-port-qmi.h"
#include "mm-port-net.h"
#include "mm-port-enums-types.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-log-object.h"

#define DEFAULT_LINK_PREALLOCATED_AMOUNT 4

/* as internally defined in the kernel */
#define RMNET_MAX_PACKET_SIZE 16384
#define MHI_NET_MTU_DEFAULT   16384

G_DEFINE_TYPE (MMPortQmi, mm_port_qmi, MM_TYPE_PORT)

#if defined WITH_QRTR

enum {
    PROP_0,
    PROP_NODE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

#endif

typedef struct {
    QmiService  service;
    QmiClient  *client;
    guint       flag;
} ServiceInfo;

struct _MMPortQmiPrivate {
    gboolean   in_progress;
    QmiDevice *qmi_device;
    GList     *services;
    gchar     *net_driver;
    gchar     *net_sysfs_path;
#if defined WITH_QRTR
    QrtrNode  *node;
#endif

    /* port monitoring */
    gulong timeout_monitoring_id;
    gulong removed_monitoring_id;
    /* endpoint info */
    QmiDataEndpointType endpoint_type;
    gint                endpoint_interface_number;
    /* kernel data mode */
    MMPortQmiKernelDataMode kernel_data_modes;
    /* wda settings */
    gboolean                      wda_unsupported;
    QmiWdaLinkLayerProtocol       llp;
    QmiWdaDataAggregationProtocol dap;
    guint                         max_multiplexed_links;
    /* preallocated links */
    MMPort   *preallocated_links_main;
    GArray   *preallocated_links;
    GList    *preallocated_links_setup_pending;
    /* first multiplex setup */
    gboolean first_multiplex_setup;
};

/*****************************************************************************/

static QmiClient *
lookup_client (MMPortQmi  *self,
               QmiService  service,
               guint       flag,
               gboolean    steal)
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
mm_port_qmi_peek_client (MMPortQmi  *self,
                         QmiService  service,
                         guint       flag)
{
    return lookup_client (self, service, flag, FALSE);
}

QmiClient *
mm_port_qmi_get_client (MMPortQmi  *self,
                        QmiService  service,
                        guint       flag)
{
    QmiClient *client;

    client = mm_port_qmi_peek_client (self, service, flag);
    return (client ? g_object_ref (client) : NULL);
}

/*****************************************************************************/

static void
initialize_endpoint_info (MMPortQmi *self)
{
    MMKernelDevice *kernel_device;

    kernel_device = mm_port_peek_kernel_device (MM_PORT (self));

    self->priv->endpoint_type = mm_port_net_driver_to_qmi_endpoint_type (self->priv->net_driver);

    switch (self->priv->endpoint_type) {
        case QMI_DATA_ENDPOINT_TYPE_HSUSB:
            g_assert (kernel_device);
            self->priv->endpoint_interface_number = mm_kernel_device_get_interface_number (kernel_device);
            break;
        case QMI_DATA_ENDPOINT_TYPE_EMBEDDED:
            self->priv->endpoint_interface_number = 1;
            break;
        case QMI_DATA_ENDPOINT_TYPE_PCIE:
            /* Qualcomm magic number */
            self->priv->endpoint_interface_number = 4;
            break;
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

void
mm_port_qmi_get_endpoint_info (MMPortQmi *self, MMQmiDataEndpoint *out_endpoint)
{
    out_endpoint->type = self->priv->endpoint_type;
    out_endpoint->interface_number = self->priv->endpoint_interface_number;
    out_endpoint->sio_port = QMI_SIO_PORT_NONE;
}

/*****************************************************************************/

static void
reset_monitoring (MMPortQmi *self,
                  QmiDevice *qmi_device)
{
    if (self->priv->timeout_monitoring_id && qmi_device) {
        g_signal_handler_disconnect (qmi_device, self->priv->timeout_monitoring_id);
        self->priv->timeout_monitoring_id = 0;
    }
    if (self->priv->removed_monitoring_id && qmi_device) {
        g_signal_handler_disconnect (qmi_device, self->priv->removed_monitoring_id);
        self->priv->removed_monitoring_id = 0;
    }
}

static void
consecutive_timeouts_updated_cb (MMPortQmi  *self,
                                 GParamSpec *pspec,
                                 QmiDevice  *qmi_device)
{
    g_signal_emit_by_name (self, MM_PORT_SIGNAL_TIMED_OUT, qmi_device_get_consecutive_timeouts (qmi_device));
}

static void
device_removed_cb (MMPortQmi  *self)
{
    g_signal_emit_by_name (self, MM_PORT_SIGNAL_REMOVED);
}

static void
setup_monitoring (MMPortQmi *self,
                  QmiDevice *qmi_device)
{
    g_assert (qmi_device);

    reset_monitoring (self, qmi_device);

    g_assert (!self->priv->timeout_monitoring_id);
    self->priv->timeout_monitoring_id = g_signal_connect_swapped (qmi_device,
                                                                  "notify::" QMI_DEVICE_CONSECUTIVE_TIMEOUTS,
                                                                  G_CALLBACK (consecutive_timeouts_updated_cb),
                                                                  self);

    g_assert (!self->priv->removed_monitoring_id);
    self->priv->removed_monitoring_id = g_signal_connect_swapped (qmi_device,
                                                                  QMI_DEVICE_SIGNAL_REMOVED,
                                                                  G_CALLBACK (device_removed_cb),
                                                                  self);
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
mm_port_qmi_allocate_client (MMPortQmi           *self,
                             QmiService           service,
                             guint                flag,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
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

    /* This link deletion cleanup may fail if the main interface is up
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
                           MMPort     *main,
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

    if (!self->priv->preallocated_links || !self->priv->preallocated_links_main) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "No preallocated links available");
        return FALSE;
    }

    if ((main != self->priv->preallocated_links_main) &&
        (g_strcmp0 (mm_port_get_device (main), mm_port_get_device (self->priv->preallocated_links_main)) != 0)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Preallocated links available in 'net/%s', not in 'net/%s'",
                     mm_port_get_device (self->priv->preallocated_links_main),
                     mm_port_get_device (main));
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
    ctx->data = g_object_ref (self->priv->preallocated_links_main);
    ctx->preallocated_links = g_array_sized_new (FALSE, FALSE, sizeof (PreallocatedLinkInfo), DEFAULT_LINK_PREALLOCATED_AMOUNT);
    g_array_set_clear_func (ctx->preallocated_links, (GDestroyNotify)preallocated_link_info_clear);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialize_preallocated_links_context_free);

    initialize_preallocated_links_next (task);
}

/*****************************************************************************/

typedef struct {
    MMPort *main;
    gchar  *link_name;
    guint   mux_id;
} SetupLinkContext;

static void
setup_link_context_free (SetupLinkContext *ctx)
{
    g_free (ctx->link_name);
    g_clear_object (&ctx->main);
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

    ctx->link_name = qmi_device_add_link_with_flags_finish (device, res, &ctx->mux_id, &error);
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

    if (!acquire_preallocated_link (self, ctx->main, &ctx->link_name, &ctx->mux_id, &error))
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
        /* and reset back the main, because we're not really initialized */
        g_clear_object (&self->priv->preallocated_links_main);
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

static QmiDeviceAddLinkFlags
get_rmnet_device_add_link_flags (MMPortQmi *self)
{
    QmiDeviceAddLinkFlags flags = QMI_DEVICE_ADD_LINK_FLAGS_NONE;
    g_autofree gchar *flags_str = NULL;

    if (g_strcmp0 (self->priv->net_driver, "ipa") == 0) {
        g_autofree gchar *tx_sysfs_path = NULL;
        g_autofree gchar *rx_sysfs_path = NULL;
        g_autofree gchar *tx_sysfs_str = NULL;
        g_autofree gchar *rx_sysfs_str = NULL;

        tx_sysfs_path = g_build_filename (self->priv->net_sysfs_path, "device", "feature", "tx_offload", NULL);
        rx_sysfs_path = g_build_filename (self->priv->net_sysfs_path, "device", "feature", "rx_offload", NULL);

        if (g_file_get_contents (rx_sysfs_path, &rx_sysfs_str, NULL, NULL) && rx_sysfs_str) {
            if (g_str_has_prefix (rx_sysfs_str, "MAPv4"))
                flags |= QMI_DEVICE_ADD_LINK_FLAGS_INGRESS_MAP_CKSUMV4;
            else if (g_str_has_prefix (rx_sysfs_str, "MAPv5"))
                flags |= QMI_DEVICE_ADD_LINK_FLAGS_INGRESS_MAP_CKSUMV5;
        }

        if (g_file_get_contents (tx_sysfs_path, &tx_sysfs_str, NULL, NULL) && tx_sysfs_str) {
            if (g_str_has_prefix (tx_sysfs_str, "MAPv4"))
                flags |= QMI_DEVICE_ADD_LINK_FLAGS_EGRESS_MAP_CKSUMV4;
            else if (g_str_has_prefix (tx_sysfs_str, "MAPv5"))
                flags |= QMI_DEVICE_ADD_LINK_FLAGS_EGRESS_MAP_CKSUMV5;
        }
    }

    if (g_strcmp0 (self->priv->net_driver, "qmi_wwan") == 0 ||
        g_strcmp0 (self->priv->net_driver, "mhi_net") == 0) {
        QmiWdaDataAggregationProtocol dap;

        dap = mm_port_qmi_get_data_aggregation_protocol (self);
        if (dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5)
            flags |= (QMI_DEVICE_ADD_LINK_FLAGS_INGRESS_MAP_CKSUMV5 |
                      QMI_DEVICE_ADD_LINK_FLAGS_EGRESS_MAP_CKSUMV5);
        else if (dap == QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV4)
            flags |= (QMI_DEVICE_ADD_LINK_FLAGS_INGRESS_MAP_CKSUMV4 |
                      QMI_DEVICE_ADD_LINK_FLAGS_EGRESS_MAP_CKSUMV4);
    }

    flags_str = qmi_device_add_link_flags_build_string_from_mask (flags);
    mm_obj_dbg (self, "Creating RMNET link with flags: %s", flags_str);
    return flags;
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

    if (!(self->priv->kernel_data_modes & (MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN))) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Multiplex support not available in kernel");
        g_object_unref (task);
        return;
    }

    if (!MM_PORT_QMI_DAP_IS_SUPPORTED_QMAP (self->priv->dap)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Aggregation not enabled");
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (SetupLinkContext);
    ctx->main = g_object_ref (data);
    ctx->mux_id = QMI_DEVICE_MUX_ID_UNBOUND;
    g_task_set_task_data (task, ctx, (GDestroyNotify) setup_link_context_free);

    /* When using rmnet, just try to add link in the QmiDevice */
    if (self->priv->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET) {
        qmi_device_add_link_with_flags (self->priv->qmi_device,
                                        QMI_DEVICE_MUX_ID_AUTOMATIC,
                                        mm_kernel_device_get_name (mm_port_peek_kernel_device (data)),
                                        link_prefix_hint,
                                        get_rmnet_device_add_link_flags (self),
                                        NULL,
                                        (GAsyncReadyCallback) device_add_link_ready,
                                        task);
        return;
    }

    /* For qmi_wwan, use preallocated links */
    if (self->priv->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN) {
        if (self->priv->preallocated_links) {
            setup_preallocated_link (task);
            return;
        }

        /* We must make sure we don't run this procedure in parallel (e.g. if multiple
         * connection attempts reach at the same time), so if we're told the preallocated
         * links are already being initialized (main is set) but the array didn't exist,
         * queue our task for completion once we're fully initialized */
        if (self->priv->preallocated_links_main) {
            self->priv->preallocated_links_setup_pending = g_list_append (self->priv->preallocated_links_setup_pending, task);
            return;
        }

        /* Store main to flag that we're initializing preallocated links */
        self->priv->preallocated_links_main = g_object_ref (data);
        initialize_preallocated_links (self,
                                       (GAsyncReadyCallback) initialize_preallocated_links_ready,
                                       task);
        return;
    }

    g_assert_not_reached ();
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

    if (!(self->priv->kernel_data_modes & (MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN))) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Multiplex support not available in kernel");
        g_object_unref (task);
        return;
    }

    if (!MM_PORT_QMI_DAP_IS_SUPPORTED_QMAP (self->priv->dap)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE, "Aggregation not enabled");
        g_object_unref (task);
        return;
    }

    /* When using rmnet, just try to add link in the QmiDevice */
    if (self->priv->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET) {
        qmi_device_delete_link (self->priv->qmi_device,
                                link_name,
                                mux_id,
                                NULL,
                                (GAsyncReadyCallback) device_delete_link_ready,
                                task);
        return;
    }

    /* For qmi_wwan, use preallocated links */
    if (self->priv->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN) {
        if (!release_preallocated_link (self, link_name, mux_id, &error))
            g_task_return_error (task, error);
        else
            g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    g_assert_not_reached ();
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
    if (g_strcmp0 (self->priv->net_driver, "qmi_wwan") == 0) {
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
    guint                 mtu;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (InternalResetContext);
    ctx->data = g_object_ref (data);
    ctx->device = g_object_ref (device);
    g_task_set_task_data (task, ctx, (GDestroyNotify) internal_reset_context_free);

    /* mhi_net has a custom default MTU set by the kernel driver */
    if (g_strcmp0 (self->priv->net_driver, "mhi_net") == 0)
        mtu = MHI_NET_MTU_DEFAULT;
    else
        mtu = MM_PORT_NET_MTU_DEFAULT;

    /* first, bring down main interface */
    mm_obj_dbg (self, "bringing down data interface '%s'",
                mm_port_get_device (ctx->data));
    mm_port_net_link_setup (MM_PORT_NET (ctx->data),
                            FALSE,
                            mtu,
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

MMPortQmiKernelDataMode
mm_port_qmi_get_kernel_data_modes (MMPortQmi *self)
{
    return self->priv->kernel_data_modes;
}

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

guint
mm_port_qmi_get_max_multiplexed_links (MMPortQmi *self)
{
    return self->priv->max_multiplexed_links;
}

/*****************************************************************************/

static MMPortQmiKernelDataMode
load_current_kernel_data_modes (MMPortQmi *self,
                                QmiDevice *device)
{
    /* For BAM-DMUX based setups, raw-ip only and no multiplexing */
    if (g_strcmp0 (self->priv->net_driver, "bam-dmux") == 0)
        return MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP;

    /* For IPA based setups, always rmnet multiplexing */
    if (g_strcmp0 (self->priv->net_driver, "ipa") == 0)
        return MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET;

    /* For USB based setups, query kernel */
    if (g_strcmp0 (self->priv->net_driver, "qmi_wwan") == 0) {
        switch (qmi_device_get_expected_data_format (device, NULL)) {
        case QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH:
            return MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET;
        case QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP:
            if (qmi_device_check_link_supported (device, NULL))
                return (MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN);
            return MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP;
        case QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN:
            /* If the expected data format is unknown, it means the kernel in use
             * doesn't have support for querying it; therefore it's 802.3 */
        case QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3:
            return MM_PORT_QMI_KERNEL_DATA_MODE_802_3;
        default:
            g_assert_not_reached ();
            return MM_PORT_QMI_KERNEL_DATA_MODE_NONE;
        }
    }

    if (g_strcmp0 (self->priv->net_driver, "mhi_net") == 0)
        return (MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET);

    /* For any driver, assume raw-ip only */
    return MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP;
}

static MMPortQmiKernelDataMode
load_supported_kernel_data_modes (MMPortQmi *self,
                                  QmiDevice *device)
{
    /* For BAM-DMUX based setups, raw-ip only and no multiplexing */
    if (g_strcmp0 (self->priv->net_driver, "bam-dmux") == 0)
        return MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP;

    /* For IPA based setups, always rmnet multiplexing */
    if (g_strcmp0 (self->priv->net_driver, "ipa") == 0)
        return MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET;

    /* For USB based setups, we may have all supported */
    if (g_strcmp0 (self->priv->net_driver, "qmi_wwan") == 0) {
        MMPortQmiKernelDataMode supported = MM_PORT_QMI_KERNEL_DATA_MODE_802_3;

        /* If raw-ip is not supported, muxing is also not supported */
        if (qmi_device_check_expected_data_format_supported (device, QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP, NULL)) {
            supported |= MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP;

            /* We switch to raw-ip to see if we can do link management with qmi_wwan.
             * This switch would not truly be required, but the logic afterwards is robust
             * enough to support this, nothing to worry about */
            if (qmi_device_set_expected_data_format (device, QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP, NULL) &&
                qmi_device_check_link_supported (device, NULL))
                supported |= MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN;

            if (qmi_device_check_expected_data_format_supported (device, QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH, NULL))
                supported |= MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET;
        }

        return supported;
    }

    /* PCIe based setups support both raw ip and QMAP through rmnet */
    if (g_strcmp0 (self->priv->net_driver, "mhi_net") == 0)
        return (MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET);

    /* For any driver, assume raw-ip only */
    return MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP;
}

/*****************************************************************************/

#define DEFAULT_DOWNLINK_DATA_AGGREGATION_MAX_SIZE                32768
#define DEFAULT_DOWNLINK_DATA_AGGREGATION_MAX_SIZE_QMI_WWAN_RMNET 16384
#define DEFAULT_DOWNLINK_DATA_AGGREGATION_MAX_DATAGRAMS           32

typedef struct {
    MMPortQmiKernelDataMode       kernel_data_mode;
    QmiWdaLinkLayerProtocol       wda_llp;
    QmiWdaDataAggregationProtocol wda_dap;
} DataFormatCombination;

static const DataFormatCombination data_format_combinations[] = {
    { MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET,   QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5   },
    { MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET,   QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV4   },
    { MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET,   QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP     },
    { MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN, QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAPV5   },
    { MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN, QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_QMAP     },
    { MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP,      QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP, QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED },
    { MM_PORT_QMI_KERNEL_DATA_MODE_802_3,       QMI_WDA_LINK_LAYER_PROTOCOL_802_3,  QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED },
};

typedef enum {
    INTERNAL_SETUP_DATA_FORMAT_STEP_FIRST,
    INTERNAL_SETUP_DATA_FORMAT_STEP_ALLOCATE_WDA_CLIENT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_SUPPORTED_KERNEL_DATA_MODES,
    INTERNAL_SETUP_DATA_FORMAT_STEP_RETRY,
    INTERNAL_SETUP_DATA_FORMAT_STEP_CURRENT_KERNEL_DATA_MODES,
    INTERNAL_SETUP_DATA_FORMAT_STEP_ALLOCATE_DPM_CLIENT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_DPM_OPEN,
    INTERNAL_SETUP_DATA_FORMAT_STEP_GET_WDA_DATA_FORMAT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_QUERY_DONE,
    INTERNAL_SETUP_DATA_FORMAT_STEP_CHECK_DATA_FORMAT_COMBINATION,
    INTERNAL_SETUP_DATA_FORMAT_STEP_SYNC_WDA_DATA_FORMAT,
    INTERNAL_SETUP_DATA_FORMAT_STEP_SETUP_MAIN_MTU,
    INTERNAL_SETUP_DATA_FORMAT_STEP_SYNC_KERNEL_DATA_MODE,
    INTERNAL_SETUP_DATA_FORMAT_STEP_LAST,
} InternalSetupDataFormatStep;

typedef struct {
    QmiDevice                      *device;
    MMPort                         *data;
    MMPortQmiSetupDataFormatAction  action;

    InternalSetupDataFormatStep step;
    gboolean                    use_endpoint;
    gint                        data_format_combination_i;

    /* kernel data modes */
    MMPortQmiKernelDataMode kernel_data_modes_current;
    MMPortQmiKernelDataMode kernel_data_modes_requested;
    MMPortQmiKernelDataMode kernel_data_modes_supported;

    /* configured device data format */
    QmiClient                     *wda;
    QmiClient                     *dpm;
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

    if (ctx->dpm && ctx->device)
        qmi_device_release_client (ctx->device,
                                   ctx->dpm,
                                   QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID,
                                   3, NULL, NULL, NULL);

    g_clear_object (&ctx->wda);
    g_clear_object (&ctx->dpm);
    g_clear_object (&ctx->data);
    g_clear_object (&ctx->device);
    g_slice_free (InternalSetupDataFormatContext, ctx);
}

static gboolean
internal_setup_data_format_finish (MMPortQmi                      *self,
                                   GAsyncResult                   *res,
                                   MMPortQmiKernelDataMode        *out_kernel_data_modes,
                                   QmiWdaLinkLayerProtocol        *out_llp,
                                   QmiWdaDataAggregationProtocol  *out_dap,
                                   guint                          *out_max_multiplexed_links,
                                   GError                        **error)
{
    InternalSetupDataFormatContext *ctx;

    if (!g_task_propagate_boolean (G_TASK (res), error))
        return FALSE;

    ctx = g_task_get_task_data (G_TASK (res));
    *out_kernel_data_modes = ctx->kernel_data_modes_current;
    *out_llp = ctx->wda_llp_current;
    g_assert (ctx->wda_dl_dap_current == ctx->wda_ul_dap_current);
    *out_dap = ctx->wda_dl_dap_current;

    if (out_max_multiplexed_links) {
        if (!ctx->wda_dap_supported) {
            *out_max_multiplexed_links = 0;
            mm_obj_dbg (self, "wda data aggregation protocol unsupported: no multiplexed bearers allowed");
        } else {
            /* if multiplex backend may be rmnet, MAX-MIN */
            if (ctx->kernel_data_modes_supported & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET) {
                *out_max_multiplexed_links = 1 + (QMI_DEVICE_MUX_ID_MAX - QMI_DEVICE_MUX_ID_MIN);
                mm_obj_dbg (self, "rmnet link management supported: %u multiplexed bearers allowed",
                            *out_max_multiplexed_links);
            }
            /* if multiplex backend may be qmi_wwan, the max preallocated amount :/  */
            else if (ctx->kernel_data_modes_supported & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN) {
                *out_max_multiplexed_links = DEFAULT_LINK_PREALLOCATED_AMOUNT;
                mm_obj_dbg (self, "qmi_wwan link management supported: %u multiplexed bearers allowed",
                            *out_max_multiplexed_links);
            } else {
                *out_max_multiplexed_links = 0;
                mm_obj_dbg (self, "link management unsupported: no multiplexed bearers allowed");
            }
        }
    }

    return TRUE;
}

static void internal_setup_data_format_context_step (GTask *task);

static void
sync_kernel_data_mode (GTask *task)
{
    MMPortQmi                      *self;
    InternalSetupDataFormatContext *ctx;
    GError                         *error = NULL;
    g_autofree gchar               *kernel_data_modes_current_str = NULL;
    g_autofree gchar               *kernel_data_modes_requested_str = NULL;
    QmiDeviceExpectedDataFormat     expected_data_format_requested = QMI_DEVICE_EXPECTED_DATA_FORMAT_UNKNOWN;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    kernel_data_modes_current_str = mm_port_qmi_kernel_data_mode_build_string_from_mask (ctx->kernel_data_modes_current);
    kernel_data_modes_requested_str = mm_port_qmi_kernel_data_mode_build_string_from_mask (ctx->kernel_data_modes_requested);

    mm_obj_dbg (self, "Updating kernel expected data format: %s -> %s",
                kernel_data_modes_current_str, kernel_data_modes_requested_str);

    if (ctx->kernel_data_modes_requested & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET)
        expected_data_format_requested = QMI_DEVICE_EXPECTED_DATA_FORMAT_QMAP_PASS_THROUGH;
    else if (ctx->kernel_data_modes_requested & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN)
        expected_data_format_requested = QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP;
    else if (ctx->kernel_data_modes_requested & MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP)
        expected_data_format_requested = QMI_DEVICE_EXPECTED_DATA_FORMAT_RAW_IP;
    else if (ctx->kernel_data_modes_requested & MM_PORT_QMI_KERNEL_DATA_MODE_802_3)
        expected_data_format_requested = QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3;
    else
        g_assert_not_reached ();

    if (!qmi_device_set_expected_data_format (ctx->device, expected_data_format_requested, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* request reload */
    ctx->kernel_data_modes_current = MM_PORT_QMI_KERNEL_DATA_MODE_NONE;

    /* Go on to next step */
    ctx->step++;
    internal_setup_data_format_context_step (task);
}

static void
main_mtu_ready (MMPortNet    *data,
                GAsyncResult *res,
                GTask        *task)
{
    MMPortQmi                      *self;
    InternalSetupDataFormatContext *ctx;
    GError                         *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!mm_port_net_link_setup_finish (data, res, &error)) {
        mm_obj_dbg (self, "failed to setup main MTU: %s", error->message);
        g_clear_error (&error);
    }

    /* Go on to next step */
    ctx->step++;
    internal_setup_data_format_context_step (task);
}

static void
setup_main_mtu (GTask *task)
{
    MMPortQmi                      *self;
    InternalSetupDataFormatContext *ctx;
    guint                           mtu = MM_PORT_NET_MTU_DEFAULT;
    GError                         *error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    /* qmi_wwan multiplexing logic requires main mtu set to the maximum data
     * aggregation size */
    if (ctx->kernel_data_modes_requested & (MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN)) {
        /* Load current max datagram size supported */
        if (MM_PORT_QMI_DAP_IS_SUPPORTED_QMAP (ctx->wda_dl_dap_requested)) {
            mtu = ctx->wda_dl_dap_max_size_current;
            if ((ctx->kernel_data_modes_requested & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET) && (mtu > RMNET_MAX_PACKET_SIZE)) {
                mm_obj_dbg (self, "mtu limited to maximum rmnet packet size");
                mtu = RMNET_MAX_PACKET_SIZE;
            }
        }

        /* If no max aggregation size was specified by the modem (e.g. if we requested QMAP
         * aggregation protocol but the modem doesn't support it), skip */
        if (!mtu) {
            mm_obj_dbg (self, "ignoring main mtu setup");
            ctx->step++;
            internal_setup_data_format_context_step (task);
            return;
        }
    }

    /* Main MTU change can only be changed while in 802-3 */
    if (!(ctx->kernel_data_modes_current & MM_PORT_QMI_KERNEL_DATA_MODE_802_3)) {
        mm_obj_dbg (self, "Updating kernel expected data format to 802-3 temporarily for main mtu setup");
        if (!qmi_device_set_expected_data_format (ctx->device, QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3, &error)) {
            g_prefix_error (&error, "Failed setting up 802.3 kernel data format before main mtu change: ");
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }
        /* the sync kernel data mode step will fix this appropriately */
        ctx->kernel_data_modes_current = MM_PORT_QMI_KERNEL_DATA_MODE_802_3;
    }

    mm_obj_dbg (self, "setting up main mtu: %u bytes", mtu);
    mm_port_net_link_setup (MM_PORT_NET (ctx->data),
                            FALSE,
                            mtu,
                            NULL,
                            (GAsyncReadyCallback) main_mtu_ready,
                            task);
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

    /* store max aggregation size so that the main MTU logic works */
    qmi_message_wda_set_data_format_output_get_downlink_data_aggregation_max_size (output, &ctx->wda_dl_dap_max_size_current, NULL);

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
        if ((g_strcmp0 (self->priv->net_driver, "qmi_wwan") == 0) &&
            (ctx->kernel_data_modes_supported & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET))
            qmi_message_wda_set_data_format_input_set_downlink_data_aggregation_max_size (input, DEFAULT_DOWNLINK_DATA_AGGREGATION_MAX_SIZE_QMI_WWAN_RMNET, NULL);
        else
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
    if ((MM_PORT_QMI_DAP_IS_SUPPORTED_QMAP (ctx->wda_dl_dap_requested)) &&
        (!(ctx->kernel_data_modes_current & (MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN)))) {
        mm_obj_dbg (self, "cannot enable data aggregation: link management unsupported");
        return FALSE;
    }

    /* check whether the current and requested ones are the same */
    if ((ctx->kernel_data_modes_current & ctx->kernel_data_modes_requested) &&
        (ctx->wda_llp_current    == ctx->wda_llp_requested) &&
        (ctx->wda_ul_dap_current == ctx->wda_ul_dap_requested) &&
        (ctx->wda_dl_dap_current == ctx->wda_dl_dap_requested)) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return TRUE;
    }

    return FALSE;
}

static void
check_data_format_combination (GTask *task)
{
    MMPortQmi                      *self;
    InternalSetupDataFormatContext *ctx;
    gboolean                        first_iteration;

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
        g_autofree gchar            *kernel_data_mode_str = NULL;

        combination = &data_format_combinations[ctx->data_format_combination_i];

        if (!(ctx->kernel_data_modes_supported & combination->kernel_data_mode))
            continue;

        if ((MM_PORT_QMI_DAP_IS_SUPPORTED_QMAP (combination->wda_dap)) &&
            ((!ctx->wda_dap_supported) ||
             (ctx->action != MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_SET_MULTIPLEX)))
            continue;

        kernel_data_mode_str = mm_port_qmi_kernel_data_mode_build_string_from_mask (combination->kernel_data_mode);
        mm_obj_dbg (self, "selected data format setup:");
        mm_obj_dbg (self, "    kernel data mode: %s", kernel_data_mode_str);
        mm_obj_dbg (self, "    link layer protocol: %s", qmi_wda_link_layer_protocol_get_string (combination->wda_llp));
        mm_obj_dbg (self, "    aggregation protocol: %s", qmi_wda_data_aggregation_protocol_get_string (combination->wda_dap));

        ctx->kernel_data_modes_requested = combination->kernel_data_mode;
        ctx->wda_llp_requested           = combination->wda_llp;
        ctx->wda_ul_dap_requested        = combination->wda_dap;
        ctx->wda_dl_dap_requested        = combination->wda_dap;

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
dpm_open_port_ready (QmiClientDpm *client,
                     GAsyncResult *res,
                     GTask        *task)
{
    g_autoptr(QmiMessageDpmOpenPortOutput)  output = NULL;
    InternalSetupDataFormatContext         *ctx;
    g_autoptr(GError)                       error = NULL;

    ctx  = g_task_get_task_data (task);

    output = qmi_client_dpm_open_port_finish (client, res, &error);
    if (!output ||
        !qmi_message_dpm_open_port_output_get_result (output, &error)) {
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    internal_setup_data_format_context_step (task);
}

static void
dpm_open_port (GTask *task)
{
    MMPortQmi                                          *self;
    InternalSetupDataFormatContext                     *ctx;
    QmiMessageDpmOpenPortInputHardwareDataPortsElement  hw_port;
    g_autoptr(GArray)                                   hw_data_ports = NULL;
    g_autoptr(QmiMessageDpmOpenPortInput)               input = NULL;
    g_autofree gchar *tx_sysfs_path = NULL;
    g_autofree gchar *rx_sysfs_path = NULL;
    g_autofree gchar *tx_sysfs_str = NULL;
    g_autofree gchar *rx_sysfs_str = NULL;
    guint tx_id = 0;
    guint rx_id = 0;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    tx_sysfs_path = g_build_filename (self->priv->net_sysfs_path, "device", "modem", "tx_endpoint_id", NULL);
    rx_sysfs_path = g_build_filename (self->priv->net_sysfs_path, "device", "modem", "rx_endpoint_id", NULL);

    if (g_file_get_contents (rx_sysfs_path, &rx_sysfs_str, NULL, NULL) &&
        g_file_get_contents (tx_sysfs_path, &tx_sysfs_str, NULL, NULL)) {
        if (rx_sysfs_str && tx_sysfs_str) {
            mm_get_uint_from_str (rx_sysfs_str, &rx_id);
            mm_get_uint_from_str (tx_sysfs_str, &tx_id);
        }
    }

    if (tx_id == 0 || rx_id == 0) {
        mm_obj_warn (self, "Unable to read TX and RX endpoint IDs from sysfs. skipping automatic DPM port opening.");

        /* Go on to next step */
        ctx->step++;
        internal_setup_data_format_context_step (task);
        return;
    }

    mm_obj_dbg (self, "Opening DPM port with TX ID: %u and RX ID: %u", tx_id, rx_id);

    /* The modem TX endpoint connects with the IPA's RX port and the modem RX endpoint connects with the IPA's TX port. */
    hw_port.rx_endpoint_number = tx_id;
    hw_port.tx_endpoint_number = rx_id;
    hw_port.endpoint_type = self->priv->endpoint_type;
    hw_port.interface_number = self->priv->endpoint_interface_number;
    hw_data_ports = g_array_new (FALSE, FALSE, sizeof (QmiMessageDpmOpenPortInputHardwareDataPortsElement));
    g_array_append_val (hw_data_ports, hw_port);

    input = qmi_message_dpm_open_port_input_new ();
    qmi_message_dpm_open_port_input_set_hardware_data_ports (input,
                                                             hw_data_ports,
                                                             NULL);
    qmi_client_dpm_open_port (QMI_CLIENT_DPM (ctx->dpm),
                              input,
                              10,
                              g_task_get_cancellable (task),
                              (GAsyncReadyCallback) dpm_open_port_ready,
                              task);
}

static void
allocate_client_dpm_ready (QmiDevice    *device,
                           GAsyncResult *res,
                           GTask        *task)
{
    InternalSetupDataFormatContext *ctx;
    GError                         *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->dpm = qmi_device_allocate_client_finish (device, res, &error);
    if (!ctx->dpm) {
        g_task_return_error (task, error);
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

        case INTERNAL_SETUP_DATA_FORMAT_STEP_ALLOCATE_WDA_CLIENT:
            /* Allocate new WDA client, only on first loop iteration */
            g_assert (!ctx->wda);
            qmi_device_allocate_client (ctx->device,
                                        QMI_SERVICE_WDA,
                                        QMI_CID_NONE,
                                        10,
                                        g_task_get_cancellable (task),
                                        (GAsyncReadyCallback) allocate_client_wda_ready,
                                        task);
            return;

        case INTERNAL_SETUP_DATA_FORMAT_STEP_SUPPORTED_KERNEL_DATA_MODES:
            /* Load kernel data format capabilities, only on first loop iteration */
            ctx->kernel_data_modes_supported = load_supported_kernel_data_modes (self, ctx->device);
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_RETRY:
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_CURRENT_KERNEL_DATA_MODES:
            /* Only reload kernel data modes if it was updated or on first loop */
            if (ctx->kernel_data_modes_current == MM_PORT_QMI_KERNEL_DATA_MODE_NONE)
                ctx->kernel_data_modes_current = load_current_kernel_data_modes (self, ctx->device);
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_ALLOCATE_DPM_CLIENT:
            /* Only allocate new DPM client on first loop */
            if ((g_strcmp0 (self->priv->net_driver, "ipa") == 0) && (ctx->data_format_combination_i < 0)) {
                g_assert (!ctx->dpm);
                qmi_device_allocate_client (ctx->device,
                                            QMI_SERVICE_DPM,
                                            QMI_CID_NONE,
                                            10,
                                            g_task_get_cancellable (task),
                                            (GAsyncReadyCallback) allocate_client_dpm_ready,
                                            task);
                return;
            }
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_DPM_OPEN:
            /* Only for IPA based setups, open dpm port */
            if (g_strcmp0 (self->priv->net_driver, "ipa") == 0) {
                dpm_open_port (task);
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

        case INTERNAL_SETUP_DATA_FORMAT_STEP_QUERY_DONE: {
            g_autofree gchar *kernel_data_modes_str = NULL;

            kernel_data_modes_str = mm_port_qmi_kernel_data_mode_build_string_from_mask (ctx->kernel_data_modes_current);
            mm_obj_dbg (self, "current data format setup:");
            mm_obj_dbg (self, "    kernel data modes: %s", kernel_data_modes_str);
            mm_obj_dbg (self, "    link layer protocol: %s", qmi_wda_link_layer_protocol_get_string (ctx->wda_llp_current));
            mm_obj_dbg (self, "    aggregation protocol ul: %s", qmi_wda_data_aggregation_protocol_get_string (ctx->wda_ul_dap_current));
            mm_obj_dbg (self, "    aggregation protocol dl: %s", qmi_wda_data_aggregation_protocol_get_string (ctx->wda_dl_dap_current));

            if (ctx->action == MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_QUERY) {
                g_task_return_boolean (task, TRUE);
                g_object_unref (task);
                return;
            }

            ctx->step++;
        } /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_CHECK_DATA_FORMAT_COMBINATION:
            /* This step is the one that may complete the async operation
             * successfully */
            check_data_format_combination (task);
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

        case INTERNAL_SETUP_DATA_FORMAT_STEP_SETUP_MAIN_MTU:
            /* qmi_wwan add_mux/del_mux based logic requires main MTU set to the maximum
             * data aggregation size reported by the modem.  */
            if (g_strcmp0 (self->priv->net_driver, "qmi_wwan") == 0) {
                setup_main_mtu (task);
                return;
            }
            ctx->step++;
            /* Fall through */

        case INTERNAL_SETUP_DATA_FORMAT_STEP_SYNC_KERNEL_DATA_MODE:
            if (!(ctx->kernel_data_modes_current & ctx->kernel_data_modes_requested)) {
                sync_kernel_data_mode (task);
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
    ctx->kernel_data_modes_current = MM_PORT_QMI_KERNEL_DATA_MODE_NONE;
    ctx->kernel_data_modes_requested = MM_PORT_QMI_KERNEL_DATA_MODE_NONE;
    ctx->kernel_data_modes_supported = MM_PORT_QMI_KERNEL_DATA_MODE_NONE;
    ctx->wda_llp_current = QMI_WDA_LINK_LAYER_PROTOCOL_UNKNOWN;
    ctx->wda_llp_requested = QMI_WDA_LINK_LAYER_PROTOCOL_UNKNOWN;
    ctx->wda_ul_dap_current = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    ctx->wda_ul_dap_requested = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    ctx->wda_dl_dap_current = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
    ctx->wda_dl_dap_requested = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;

    if (mm_port_get_subsys (MM_PORT (self)) == MM_PORT_SUBSYS_QRTR)
        ctx->use_endpoint = TRUE;

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
                                            &self->priv->kernel_data_modes,
                                            &self->priv->llp,
                                            &self->priv->dap,
                                            NULL, /* not expected to update */
                                            &error))
        g_task_return_error (task, error);
    else {
        self->priv->first_multiplex_setup = FALSE;
        g_task_return_boolean (task, TRUE);
    }
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
    if (self->priv->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET) {
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

        if (links)
            return links->len;

        /* No list of links returned, so there are none */
        return 0;
    }

    if (self->priv->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN)
        return count_preallocated_links_setup (self);

    return 0;
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
        (self->priv->kernel_data_modes & (MM_PORT_QMI_KERNEL_DATA_MODE_MUX_RMNET | MM_PORT_QMI_KERNEL_DATA_MODE_MUX_QMIWWAN)) &&
        MM_PORT_QMI_DAP_IS_SUPPORTED_QMAP (self->priv->dap)) {
        mm_obj_dbg (self, "multiplex support already available when setting up data format");
        /* If this is the first time that multiplex is used, perform anyway the internal reset operation, so that the links are properly managed */
        if (!self->priv->first_multiplex_setup) {
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }
    }

    if ((action == MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_SET_DEFAULT) &&
        (((self->priv->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP) && (self->priv->llp == QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP)) ||
         ((self->priv->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_802_3)  && (self->priv->llp == QMI_WDA_LINK_LAYER_PROTOCOL_802_3))) &&
        !MM_PORT_QMI_DAP_IS_SUPPORTED_QMAP (self->priv->dap)) {
        mm_obj_dbg (self, "multiplex support already disabled when setting up data format");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* support switching from multiplex to non-multiplex, but only if there are no active
     * links allocated */
    if ((action == MM_PORT_QMI_SETUP_DATA_FORMAT_ACTION_SET_DEFAULT) &&
        MM_PORT_QMI_DAP_IS_SUPPORTED_QMAP (self->priv->dap)) {
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
    QmiDevice               *device;
    GError                  *error;
    PortOpenStep             step;
    gboolean                 set_data_format;
    MMPortQmiKernelDataMode  kernel_data_modes;
    gboolean                 ctl_raw_ip_unsupported;
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
    MMPortQmi         *self;
    PortOpenContext   *ctx;
    g_autoptr(GError)  error = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    if (!qmi_device_open_finish (qmi_device, res, &error)) {
        /* Not all devices support raw-ip, which is the first thing we try
         * by default. Detect this case, and retry with 802.3 if so. */
        if ((g_strcmp0 (self->priv->net_driver, "qmi_wwan") == 0) &&
            g_error_matches (error, QMI_PROTOCOL_ERROR, QMI_PROTOCOL_ERROR_INVALID_DATA_FORMAT) &&
            (ctx->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP)) {
            /* switch to 802.3 right away, so that the logic can successfully go on after that */
            qmi_device_set_expected_data_format (qmi_device, QMI_DEVICE_EXPECTED_DATA_FORMAT_802_3, NULL);
            ctx->ctl_raw_ip_unsupported = TRUE;
            port_open_step (task);
            return;
        }

        /* Otherwise, fatal */
        ctx->error = g_steal_pointer (&error);
    } else {
        /* If the open with CTL data format is sucessful, update all settings
         * that we would have received with the internal setup data format
         * process */
        self->priv->kernel_data_modes = ctx->kernel_data_modes;
        if (ctx->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP)
            self->priv->llp = QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP;
        else if (ctx->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_802_3)
            self->priv->llp = QMI_WDA_LINK_LAYER_PROTOCOL_802_3;
        else
            g_assert_not_reached ();
        self->priv->dap = QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED;
        self->priv->max_multiplexed_links = 0;
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
                                            &self->priv->kernel_data_modes,
                                            &self->priv->llp,
                                            &self->priv->dap,
                                            &self->priv->max_multiplexed_links,
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
        self->priv->first_multiplex_setup = TRUE;
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

    case PORT_OPEN_STEP_DEVICE_NEW:
        /* We flag in this point that we're opening. From now on, if we stop
         * for whatever reason, we should clear this flag. We do this by ensuring
         * that all callbacks go through the LAST step for completing. */
        self->priv->in_progress = TRUE;

#if defined WITH_QRTR
        if (self->priv->node) {
            mm_obj_dbg (self, "Creating QMI device from QRTR node...");
            qmi_device_new_from_node (self->priv->node,
                                      g_task_get_cancellable (task),
                                      (GAsyncReadyCallback) qmi_device_new_ready,
                                      task);
            return;
        }
#endif
        {
            g_autoptr(GFile)  file = NULL;
            g_autofree gchar *fullpath = NULL;

            fullpath = g_strdup_printf ("/dev/%s", mm_port_get_device (MM_PORT (self)));
            file = g_file_new_for_path (fullpath);

            mm_obj_dbg (self, "Creating QMI device...");
            qmi_device_new (file,
                            g_task_get_cancellable (task),
                            (GAsyncReadyCallback) qmi_device_new_ready,
                            task);
            return;
        }

    case PORT_OPEN_STEP_OPEN_WITHOUT_DATA_FORMAT:
        if (!self->priv->wda_unsupported || !ctx->set_data_format) {
            QmiDeviceOpenFlags  open_flags;
            g_autofree gchar   *open_flags_str = NULL;

            /* Now open the QMI device without any data format CTL flag */
            open_flags = (QMI_DEVICE_OPEN_FLAGS_VERSION_INFO | QMI_DEVICE_OPEN_FLAGS_PROXY);
            open_flags_str = qmi_device_open_flags_build_string_from_mask (open_flags);
            mm_obj_dbg (self, "Opening device with flags: %s...", open_flags_str);
            qmi_device_open (ctx->device,
                             open_flags,
                             45,
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
        QmiDeviceOpenFlags  open_flags;
        g_autofree gchar   *open_flags_str = NULL;

        /* Common open flags */
        open_flags = (QMI_DEVICE_OPEN_FLAGS_VERSION_INFO |
                      QMI_DEVICE_OPEN_FLAGS_PROXY        |
                      QMI_DEVICE_OPEN_FLAGS_NET_NO_QOS_HEADER);

        ctx->kernel_data_modes = load_current_kernel_data_modes (self, ctx->device);

        /* Skip trying raw-ip if we already tried and it failed */
        if (ctx->ctl_raw_ip_unsupported)
            ctx->kernel_data_modes &= ~MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP;

        /* Need to reopen setting 802.3/raw-ip using CTL */
        if (ctx->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_RAW_IP)
            open_flags |= QMI_DEVICE_OPEN_FLAGS_NET_RAW_IP;
        else if (ctx->kernel_data_modes & MM_PORT_QMI_KERNEL_DATA_MODE_802_3)
            open_flags |= QMI_DEVICE_OPEN_FLAGS_NET_802_3;
        else {
            /* Set error and jump to last step, so that we cleanly close the device
             * in case we need to reopen it right away */
            ctx->error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                      "Unexpected kernel data mode: cannot setup using CTL");
            ctx->step = PORT_OPEN_STEP_LAST;
            port_open_step (task);
            return;
        }

        open_flags_str = qmi_device_open_flags_build_string_from_mask (open_flags);
        mm_obj_dbg (self, "Reopening device with flags: %s...", open_flags_str);

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
        setup_monitoring (self, ctx->device);
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
    ctx->kernel_data_modes = MM_PORT_QMI_KERNEL_DATA_MODE_NONE;

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

void
mm_port_qmi_set_net_driver (MMPortQmi   *self,
                            const gchar *net_driver)
{
    g_assert (MM_IS_PORT_QMI (self));
    g_assert (!self->priv->net_driver);
    self->priv->net_driver = g_strdup (net_driver);
    initialize_endpoint_info (self);
}

/*****************************************************************************/

void
mm_port_qmi_set_net_sysfs_path (MMPortQmi   *self,
                                const gchar *net_sysfs_path)
{
    g_assert (MM_IS_PORT_QMI (self));
    g_assert (!self->priv->net_sysfs_path);
    self->priv->net_sysfs_path = g_strdup (net_sysfs_path);
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

    /* Reset monitoring logic */
    reset_monitoring (self, ctx->qmi_device);

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
    g_clear_object (&self->priv->preallocated_links_main);

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

#if defined WITH_QRTR
MMPortQmi *
mm_port_qmi_new_from_node (const gchar *name,
                           QrtrNode    *node)
{
    return MM_PORT_QMI (g_object_new (MM_TYPE_PORT_QMI,
                                      "node", node,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_QRTR,
                                      MM_PORT_TYPE, MM_PORT_TYPE_QMI,
                                      NULL));
}
#endif

static void
mm_port_qmi_init (MMPortQmi *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT_QMI, MMPortQmiPrivate);
}

#if defined WITH_QRTR

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMPortQmi *self = MM_PORT_QMI (object);

    switch (prop_id) {
    case PROP_NODE:
        g_clear_object (&self->priv->node);
        self->priv->node = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MMPortQmi *self = MM_PORT_QMI (object);

    switch (prop_id) {
    case PROP_NODE:
        g_value_set_object (value, self->priv->node);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

#endif /* defined WITH_QRTR */

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

    /* Cleanup preallocated links, if any */
    if (self->priv->preallocated_links && self->priv->qmi_device)
        delete_preallocated_links (self->priv->qmi_device, self->priv->preallocated_links);
    g_clear_pointer (&self->priv->preallocated_links, g_array_unref);
    g_clear_object (&self->priv->preallocated_links_main);

    /* Clear node object */
#if defined WITH_QRTR
    g_clear_object (&self->priv->node);
#endif
    /* Clear device object */
    reset_monitoring (self, self->priv->qmi_device);
    g_clear_object (&self->priv->qmi_device);

    g_clear_pointer (&self->priv->net_driver, g_free);
    g_clear_pointer (&self->priv->net_sysfs_path, g_free);

    G_OBJECT_CLASS (mm_port_qmi_parent_class)->dispose (object);
}

static void
mm_port_qmi_class_init (MMPortQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortQmiPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;

#if defined WITH_QRTR
    object_class->get_property = get_property;
    object_class->set_property = set_property;

    properties[PROP_NODE] =
        g_param_spec_object ("node",
                             "Qrtr Node",
                             "Qrtr node to be probed",
                             QRTR_TYPE_NODE,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties (object_class, PROP_LAST, properties);
#endif
}
