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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 */

#ifndef MM_IFACE_MODEM_CELLBROADCAST_H
#define MM_IFACE_MODEM_CELLBROADCAST_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-cbm-part.h"
#include "mm-base-cbm.h"

#define MM_TYPE_IFACE_MODEM_CELL_BROADCAST mm_iface_modem_cell_broadcast_get_type ()
G_DECLARE_INTERFACE (MMIfaceModemCellBroadcast, mm_iface_modem_cell_broadcast, MM, IFACE_MODEM_CELL_BROADCAST, MMIfaceModem)

#define MM_IFACE_MODEM_CELL_BROADCAST_DBUS_SKELETON "iface-modem-cell-broadcast-dbus-skeleton"
#define MM_IFACE_MODEM_CELL_BROADCAST_CBM_LIST      "iface-modem-cell-broadcast-cbm-list"

struct _MMIfaceModemCellBroadcastInterface {
    GTypeInterface g_iface;

    /* Check for CellBroadcast support (async) */
    void (* check_support) (MMIfaceModemCellBroadcast    *self,
                            GAsyncReadyCallback           callback,
                            gpointer                      user_data);
    gboolean (* check_support_finish) (MMIfaceModemCellBroadcast *self,
                                       GAsyncResult              *res,
                                       GError                   **error);

    /* Asynchronous setting up unsolicited CellBroadcast reception events */
    void (* setup_unsolicited_events) (MMIfaceModemCellBroadcast *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    gboolean (* setup_unsolicited_events_finish) (MMIfaceModemCellBroadcast *self,
                                                  GAsyncResult *res,
                                                  GError **error);

    /* Asynchronous cleaning up of unsolicited CellBroadcast reception events */
    void (* cleanup_unsolicited_events) (MMIfaceModemCellBroadcast *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    gboolean (* cleanup_unsolicited_events_finish) (MMIfaceModemCellBroadcast *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Asynchronous enabling unsolicited CellBroadcast reception events */
    void (* enable_unsolicited_events) (MMIfaceModemCellBroadcast *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (* enable_unsolicited_events_finish) (MMIfaceModemCellBroadcast *self,
                                                   GAsyncResult *res,
                                                   GError **error);

    /* Asynchronous disabling unsolicited CellBroadcast reception events */
    void (* disable_unsolicited_events) (MMIfaceModemCellBroadcast *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    gboolean (* disable_unsolicited_events_finish) (MMIfaceModemCellBroadcast *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Asynchronous loading of channel list */
    GArray * (*load_channels_finish) (MMIfaceModemCellBroadcast *self,
                                      GAsyncResult *res,
                                      GError **error);

    void (*load_channels) (MMIfaceModemCellBroadcast *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data);

    /* Set channel list */
    void (* set_channels) (MMIfaceModemCellBroadcast *self,
                           GArray *channels,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
    gboolean (* set_channels_finish) (MMIfaceModemCellBroadcast *self,
                                      GAsyncResult *res,
                                      GError **error);
};

/* Initialize CellBroadcast interface (async) */
void     mm_iface_modem_cell_broadcast_initialize   (MMIfaceModemCellBroadcast *self,
                                                     GCancellable *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
gboolean mm_iface_modem_cell_broadcast_initialize_finish (MMIfaceModemCellBroadcast *self,
                                                          GAsyncResult *res,
                                                          GError **error);
/* Enable CellBroadcast interface (async) */
void     mm_iface_modem_cell_broadcast_enable        (MMIfaceModemCellBroadcast *self,
                                                      GCancellable *cancellable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data);
gboolean mm_iface_modem_cell_broadcast_enable_finish (MMIfaceModemCellBroadcast *self,
                                                      GAsyncResult *res,
                                                      GError **error);

/* Disable CellBroadcast interface (async) */
void     mm_iface_modem_cell_broadcast_disable        (MMIfaceModemCellBroadcast *self,
                                                       GAsyncReadyCallback callback,
                                                       gpointer user_data);
gboolean mm_iface_modem_cell_broadcast_disable_finish (MMIfaceModemCellBroadcast *self,
                                                       GAsyncResult *res,
                                                       GError **error);
/* Shutdown CellBroadcast interface */
void mm_iface_modem_cell_broadcast_shutdown (MMIfaceModemCellBroadcast *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_cell_broadcast_bind_simple_status (MMIfaceModemCellBroadcast *self,
                                                       MMSimpleStatus *status);

/* Report new CBM part */
gboolean mm_iface_modem_cell_broadcast_take_part (MMIfaceModemCellBroadcast *self,
                                                  GObject *bind_to,
                                                  MMCbmPart *cbm_part,
                                                  MMCbmState state,
                                                  GError **error);

#endif /* MM_IFACE_MODEM_CELLBROADCAST_H */
