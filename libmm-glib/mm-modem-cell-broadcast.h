/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 */

#ifndef _MM_MODEM_CELL_BROADCAST_H_
#define _MM_MODEM_CELL_BROADCAST_H_

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>

#include "mm-gdbus-modem.h"
#include "mm-cbm.h"

G_BEGIN_DECLS

#define MM_TYPE_MODEM_CELL_BROADCAST            (mm_modem_cell_broadcast_get_type ())
#define MM_MODEM_CELL_BROADCAST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_CELL_BROADCAST, MMModemCellBroadcast))
#define MM_MODEM_CELL_BROADCAST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MODEM_CELL_BROADCAST, MMModemCellBroadcastClass))
#define MM_IS_MODEM_CELL_BROADCAST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_CELL_BROADCAST))
#define MM_IS_MODEM_CELL_BROADCAST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MODEM_CELL_BROADCAST))
#define MM_MODEM_CELL_BROADCAST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MODEM_CELL_BROADCAST, MMModemCellBroadcastClass))

typedef struct _MMModemCellBroadcast MMModemCellBroadcast;
typedef struct _MMModemCellBroadcastClass MMModemCellBroadcastClass;
typedef struct _MMModemCellBroadcastPrivate MMModemCellBroadcastPrivate;

/**
 * MMModemCellBroadcast:
 *
 * The #MMModemCellBroadcast structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMModemCellBroadcast {
    /*< private >*/
    MmGdbusModemCellBroadcastProxy parent;
    MMModemCellBroadcastPrivate *priv;
};

struct _MMModemCellBroadcastClass {
    /*< private >*/
    MmGdbusModemCellBroadcastProxyClass parent;
};

GType mm_modem_cell_broadcast_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMModemCellBroadcast, g_object_unref)

const gchar *mm_modem_cell_broadcast_get_path (MMModemCellBroadcast *self);
gchar       *mm_modem_cell_broadcast_dup_path (MMModemCellBroadcast *self);

gboolean     mm_modem_cell_broadcast_get_channels  (MMModemCellBroadcast *self,
                                                    MMCellBroadcastChannels **channels,
                                                    guint *n_storages);
gboolean     mm_modem_cell_broadcast_peek_channels (MMModemCellBroadcast *self,
                                                    const MMCellBroadcastChannels **channels,
                                                    guint *n_storages);

void   mm_modem_cell_broadcast_list        (MMModemCellBroadcast *self,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
GList *mm_modem_cell_broadcast_list_finish (MMModemCellBroadcast *self,
                                            GAsyncResult *res,
                                            GError **error);
GList *mm_modem_cell_broadcast_list_sync   (MMModemCellBroadcast *self,
                                            GCancellable *cancellable,
                                            GError **error);

void     mm_modem_cell_broadcast_delete        (MMModemCellBroadcast *self,
                                                const gchar *cbm,
                                                GCancellable *cancellable,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
gboolean mm_modem_cell_broadcast_delete_finish (MMModemCellBroadcast *self,
                                                GAsyncResult *res,
                                                GError **error);
gboolean mm_modem_cell_broadcast_delete_sync   (MMModemCellBroadcast *self,
                                                const gchar *cbm,
                                                GCancellable *cancellable,
                                                GError **error);

gboolean mm_modem_cell_broadcast_set_channels_finish (MMModemCellBroadcast *self,
                                                      GAsyncResult *res,
                                                      GError **error);

void     mm_modem_cell_broadcast_set_channels (MMModemCellBroadcast *self,
                                               const MMCellBroadcastChannels *channels,
                                               guint n_channels,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);

gboolean mm_modem_cell_broadcast_set_channels_sync (MMModemCellBroadcast *self,
                                                    const MMCellBroadcastChannels *channels,
                                                    guint n_channels,
                                                    GCancellable *cancellable,
                                                    GError **error);

G_END_DECLS

#endif /* _MM_MODEM_CELL_BROADCAST_H_ */
