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
 * Copyright 2020 Google LLC
 */

#include <ModemManager.h>
#include <libqrtr-glib.h>
#include <libqmi-glib.h>

#include "mm-utils.h"

#include "mm-qrtr-bus-watcher.h"

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMQrtrBusWatcher, mm_qrtr_bus_watcher, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

struct _MMQrtrBusWatcherPrivate {
    QrtrBus *qrtr_bus;
    guint    node_added_id;
    guint    node_removed_id;

    /* Map of NodeNumber -> QRTR nodes available */
    GHashTable *nodes;
};

enum {
    QRTR_DEVICE_ADDED,
    QRTR_DEVICE_REMOVED,
    LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("qrtr-bus-watcher");
}

/*****************************************************************************/

typedef struct {
    MMQrtrBusWatcher *self;
    QrtrNode         *node;
} DeviceContext;

static void
device_context_free (DeviceContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->node);
    g_slice_free (DeviceContext, ctx);
}

static void
qrtr_node_services_ready (QrtrNode      *node,
                          GAsyncResult  *res,
                          DeviceContext *ctx)
{
    guint32 node_id;

    node_id = qrtr_node_get_id (node);
    if (!qrtr_node_wait_for_services_finish (node, res, NULL)) {
        mm_obj_dbg (ctx->self,
                    "qrtr node %u doesn't have required services to be considered a control node",
                    node_id);
        g_hash_table_remove (ctx->self->priv->nodes, GUINT_TO_POINTER (node_id));
        device_context_free (ctx);
        return;
    }

    mm_obj_dbg (ctx->self, "qrtr services ready for node %u", node_id);
    g_signal_emit (ctx->self, signals[QRTR_DEVICE_ADDED], 0, node_id);
    device_context_free (ctx);
}

static void
handle_qrtr_node_added (QrtrBus          *qrtr_bus,
                        guint32           node_id,
                        MMQrtrBusWatcher *self)
{
    g_autoptr(QrtrNode)      node = NULL;
    g_autoptr(GArray)        services = NULL;
    DeviceContext           *ctx;
    static const QmiService  required_services[] = {
        QMI_SERVICE_WDS,
        QMI_SERVICE_NAS,
        QMI_SERVICE_DMS
    };

    mm_obj_dbg (self, "qrtr node %u added", node_id);

    node = qrtr_bus_get_node (qrtr_bus, node_id);
    if (!node) {
        mm_obj_warn (self, "cannot find node %u", node_id);
        return;
    }

    if (g_hash_table_contains (self->priv->nodes, GUINT_TO_POINTER (node_id))) {
        mm_obj_warn (self, "qrtr node %u was previously added", node_id);
        return;
    }

    /* a full node reference now owned by the hash table */
    g_hash_table_insert (self->priv->nodes, GUINT_TO_POINTER (node_id), g_object_ref (node));

    mm_obj_dbg (self, "waiting for modem services on node %u", node_id);

    /* Check if the node provides services to be sure the node represents a
     * modem. */
    services = g_array_sized_new (FALSE, FALSE, sizeof (QmiService), G_N_ELEMENTS (required_services));
    g_array_append_vals (services, required_services, G_N_ELEMENTS (required_services));

    /* Setup command context */
    ctx       = g_slice_new0 (DeviceContext);
    ctx->self = g_object_ref (self);
    ctx->node = g_object_ref (node);

    qrtr_node_wait_for_services (node,
                                 services,
                                 1000, /* ms */
                                 NULL,
                                 (GAsyncReadyCallback) qrtr_node_services_ready,
                                 ctx);
}

static void
handle_qrtr_node_removed (QrtrBus          *qrtr_bus,
                          guint32           node_id,
                          MMQrtrBusWatcher *self)
{
    QrtrNode *node;

    node = qrtr_bus_get_node (qrtr_bus, node_id);
    if (!node) {
        mm_obj_warn (self, "cannot find node %u", node_id);
        return;
    }

    g_hash_table_remove (self->priv->nodes, GUINT_TO_POINTER (node_id));
    mm_obj_dbg (self, "qrtr node %u removed", node_id);

    g_signal_emit (self, signals[QRTR_DEVICE_REMOVED], 0, node_id);
}

/*****************************************************************************/

QrtrNode *
mm_qrtr_bus_watcher_peek_node (MMQrtrBusWatcher *self,
                               guint32           node_id)
{
    g_assert (MM_IS_QRTR_BUS_WATCHER (self));

    return g_hash_table_lookup (self->priv->nodes, GUINT_TO_POINTER (node_id));
}

/*****************************************************************************/

gboolean
mm_qrtr_bus_watcher_start_finish (MMQrtrBusWatcher  *self,
                                  GAsyncResult      *res,
                                  GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

typedef struct {
    MMQrtrBusWatcher *self;
    QrtrNode         *node;
} ProcessExistingNodes;

static gboolean
process_existing_nodes_idle (ProcessExistingNodes *ctx)
{
    handle_qrtr_node_added (
        ctx->self->priv->qrtr_bus, qrtr_node_get_id (ctx->node), ctx->self);

    g_object_unref (ctx->self);
    g_object_unref (ctx->node);
    g_slice_free (ProcessExistingNodes, ctx);
    return G_SOURCE_REMOVE;
}

static void
process_existing_nodes (MMQrtrBusWatcher *self)
{
    GList                *nodes, *l;
    QrtrNode             *node;
    ProcessExistingNodes *ctx;

    nodes = qrtr_bus_peek_nodes (self->priv->qrtr_bus);
    for (l = nodes; l; l = g_list_next (l)) {
        node      = l->data;
        ctx       = g_slice_new (ProcessExistingNodes);
        ctx->self = g_object_ref (self);
        ctx->node = g_object_ref (node);
        g_idle_add ((GSourceFunc) process_existing_nodes_idle, ctx);
    }
}

static void
qrtr_bus_ready (GObject      *source,
                GAsyncResult *res,
                GTask        *task)
{
    MMQrtrBusWatcher *self;
    GError           *error = NULL;

    self = g_task_get_source_object (task);

    self->priv->qrtr_bus = qrtr_bus_new_finish (res, &error);
    if (!self->priv->qrtr_bus) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Listen for bus events */
    self->priv->node_added_id = g_signal_connect (self->priv->qrtr_bus,
                                                  QRTR_BUS_SIGNAL_NODE_ADDED,
                                                  G_CALLBACK (handle_qrtr_node_added),
                                                  self);
    self->priv->node_removed_id = g_signal_connect (self->priv->qrtr_bus,
                                                    QRTR_BUS_SIGNAL_NODE_REMOVED,
                                                    G_CALLBACK (handle_qrtr_node_removed),
                                                    self);

    process_existing_nodes (self);

    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_qrtr_bus_watcher_start (MMQrtrBusWatcher    *self,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    qrtr_bus_new (0, /* disable initial lookup wait */
                  NULL,
                  (GAsyncReadyCallback)qrtr_bus_ready,
                  task);
}

/*****************************************************************************/

MMQrtrBusWatcher *
mm_qrtr_bus_watcher_new (void)
{
    return MM_QRTR_BUS_WATCHER (g_object_new (MM_TYPE_QRTR_BUS_WATCHER, NULL));
}

static void
mm_qrtr_bus_watcher_init (MMQrtrBusWatcher *self)
{
    /* Initialize opaque pointer to private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_QRTR_BUS_WATCHER,
                                              MMQrtrBusWatcherPrivate);
    /* Setup internal lists of device and node objects */
    self->priv->nodes = g_hash_table_new_full (g_direct_hash,
                                               g_direct_equal,
                                               NULL,
                                               (GDestroyNotify) g_object_unref);
}

static void
finalize (GObject *object)
{
    MMQrtrBusWatcher *self = MM_QRTR_BUS_WATCHER (object);

    g_hash_table_destroy (self->priv->nodes);

    if (self->priv->node_added_id)
        g_signal_handler_disconnect (self->priv->qrtr_bus, self->priv->node_added_id);
    if (self->priv->node_removed_id)
        g_signal_handler_disconnect (self->priv->qrtr_bus, self->priv->node_removed_id);
    g_clear_object (&self->priv->qrtr_bus);

    G_OBJECT_CLASS (mm_qrtr_bus_watcher_parent_class)->finalize (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_qrtr_bus_watcher_class_init (MMQrtrBusWatcherClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMQrtrBusWatcherPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;

    /**
     * QrtrBusWatcher::qrtr-device-added:
     * @self: the #QrtrBusWatcher
     * @node: the node ID of the modem that is added
     *
     * The ::qrtr-device-added signal is emitted when a new qrtr modem is
     * available on the QRTR bus.
     */
    signals[QRTR_DEVICE_ADDED] = g_signal_new (MM_QRTR_BUS_WATCHER_DEVICE_ADDED,
                                               G_OBJECT_CLASS_TYPE (object_class),
                                               G_SIGNAL_RUN_FIRST,
                                               G_STRUCT_OFFSET (MMQrtrBusWatcherClass, qrtr_device_added),
                                               NULL, /* accumulator      */
                                               NULL, /* accumulator data */
                                               g_cclosure_marshal_generic,
                                               G_TYPE_NONE,
                                               1,
                                               G_TYPE_UINT);

    /**
     * QrtrBusWatcher::qrtr-device-removed:
     * @self: the #QrtrBusWatcher
     * @node: the node ID of the modem that is removed
     *
     * The ::qrtr-device-removed signal is emitted when a qrtr modem deregisters
     * all services from the QRTR bus.
     */
    signals[QRTR_DEVICE_REMOVED] = g_signal_new (MM_QRTR_BUS_WATCHER_DEVICE_REMOVED,
                                                 G_OBJECT_CLASS_TYPE (object_class),
                                                 G_SIGNAL_RUN_FIRST,
                                                 G_STRUCT_OFFSET (MMQrtrBusWatcherClass, qrtr_device_removed),
                                                 NULL, /* accumulator      */
                                                 NULL, /* accumulator data */
                                                 g_cclosure_marshal_generic,
                                                 G_TYPE_NONE,
                                                 1,
                                                 G_TYPE_UINT);
}
