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

#ifndef MM_QRTR_BUS_WATCHER_H
#define MM_QRTR_BUS_WATCHER_H

#include <glib-object.h>
#include <glib.h>

#include <libqrtr-glib.h>

G_BEGIN_DECLS

#define MM_TYPE_QRTR_BUS_WATCHER            (mm_qrtr_bus_watcher_get_type ())
#define MM_QRTR_BUS_WATCHER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_QRTR_BUS_WATCHER, MMQrtrBusWatcher))
#define MM_QRTR_BUS_WATCHER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_QRTR_BUS_WATCHER, MMQrtrBusWatcherClass))
#define MM_IS_QRTR_BUS_WATCHER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_QRTR_BUS_WATCHER))
#define MM_IS_QRTR_BUS_WATCHER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_QRTR_BUS_WATCHER))
#define MM_QRTR_BUS_WATCHER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_QRTR_BUS_WATCHER, MMQrtrBusWatcherClass))

#define MM_QRTR_BUS_WATCHER_DEVICE_ADDED   "qrtr-device-added"
#define MM_QRTR_BUS_WATCHER_DEVICE_REMOVED "qrtr-device-removed"

typedef struct _MMQrtrBusWatcher        MMQrtrBusWatcher;
typedef struct _MMQrtrBusWatcherClass   MMQrtrBusWatcherClass;
typedef struct _MMQrtrBusWatcherPrivate MMQrtrBusWatcherPrivate;

struct _MMQrtrBusWatcher {
    GObject                  parent;
    MMQrtrBusWatcherPrivate *priv;
};

struct _MMQrtrBusWatcherClass {
    GObjectClass parent;

    void (* qrtr_device_added)   (MMQrtrBusWatcher *bus_watcher);
    void (* qrtr_device_removed) (MMQrtrBusWatcher *bus_watcher);
};

GType mm_qrtr_bus_watcher_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMQrtrBusWatcher, g_object_unref)

MMQrtrBusWatcher *mm_qrtr_bus_watcher_new (void);

void     mm_qrtr_bus_watcher_start        (MMQrtrBusWatcher    *self,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data);
gboolean mm_qrtr_bus_watcher_start_finish (MMQrtrBusWatcher    *self,
                                           GAsyncResult        *res,
                                           GError             **error);

QrtrNode *mm_qrtr_bus_watcher_peek_node (MMQrtrBusWatcher *self,
                                         guint32           node_id);

G_END_DECLS

#endif /* MM_QRTR_BUS_WATCHER_H */
