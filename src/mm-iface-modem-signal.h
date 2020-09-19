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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_IFACE_MODEM_SIGNAL_H
#define MM_IFACE_MODEM_SIGNAL_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#define MM_TYPE_IFACE_MODEM_SIGNAL               (mm_iface_modem_signal_get_type ())
#define MM_IFACE_MODEM_SIGNAL(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_SIGNAL, MMIfaceModemSignal))
#define MM_IS_IFACE_MODEM_SIGNAL(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_SIGNAL))
#define MM_IFACE_MODEM_SIGNAL_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_SIGNAL, MMIfaceModemSignal))

#define MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON "iface-modem-signal-dbus-skeleton"

typedef struct _MMIfaceModemSignal MMIfaceModemSignal;

struct _MMIfaceModemSignal {
    GTypeInterface g_iface;

    /* Check for Messaging support (async) */
    void (* check_support) (MMIfaceModemSignal *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
    gboolean (*check_support_finish) (MMIfaceModemSignal *self,
                                      GAsyncResult *res,
                                      GError **error);

    /* Load all values */
    void     (* load_values)        (MMIfaceModemSignal *self,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    gboolean (* load_values_finish) (MMIfaceModemSignal *self,
                                     GAsyncResult *res,
                                     MMSignal **cdma,
                                     MMSignal **evdo,
                                     MMSignal **gsm,
                                     MMSignal **umts,
                                     MMSignal **lte,
                                     MMSignal **nr5g,
                                     GError **error);
};

GType mm_iface_modem_signal_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMIfaceModemSignal, g_object_unref)

/* Initialize Signal interface (async) */
void     mm_iface_modem_signal_initialize        (MMIfaceModemSignal *self,
                                                  GCancellable *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
gboolean mm_iface_modem_signal_initialize_finish (MMIfaceModemSignal *self,
                                                  GAsyncResult *res,
                                                  GError **error);

/* Enable Signal interface (async) */
void     mm_iface_modem_signal_enable        (MMIfaceModemSignal *self,
                                              GCancellable *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);
gboolean mm_iface_modem_signal_enable_finish (MMIfaceModemSignal *self,
                                              GAsyncResult *res,
                                              GError **error);

/* Disable Signal interface (async) */
void     mm_iface_modem_signal_disable        (MMIfaceModemSignal *self,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
gboolean mm_iface_modem_signal_disable_finish (MMIfaceModemSignal *self,
                                               GAsyncResult *res,
                                               GError **error);

/* Shutdown Signal interface */
void mm_iface_modem_signal_shutdown (MMIfaceModemSignal *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_signal_bind_simple_status (MMIfaceModemSignal *self,
                                               MMSimpleStatus *status);

#endif /* MM_IFACE_MODEM_SIGNAL_H */
