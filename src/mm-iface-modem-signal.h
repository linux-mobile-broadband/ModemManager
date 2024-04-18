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
 * Copyright (C) 2013-2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Intel Corporation
 */

#ifndef MM_IFACE_MODEM_SIGNAL_H
#define MM_IFACE_MODEM_SIGNAL_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"

#define MM_TYPE_IFACE_MODEM_SIGNAL mm_iface_modem_signal_get_type ()
G_DECLARE_INTERFACE (MMIfaceModemSignal, mm_iface_modem_signal, MM, IFACE_MODEM_SIGNAL, MMIfaceModem)

#define MM_IFACE_MODEM_SIGNAL_DBUS_SKELETON "iface-modem-signal-dbus-skeleton"

struct _MMIfaceModemSignalInterface {
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

    /* Setup thresholds */
    void     (* setup_thresholds)        (MMIfaceModemSignal   *self,
                                          guint32               rssi_threshold,
                                          gboolean              error_rate_threshold,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
    gboolean (* setup_thresholds_finish) (MMIfaceModemSignal   *self,
                                          GAsyncResult         *res,
                                          GError              **error);

};

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

/* Allow signal quality updates via indications */
void mm_iface_modem_signal_update (MMIfaceModemSignal *self,
                                   MMSignal           *cdma,
                                   MMSignal           *evdo,
                                   MMSignal           *gsm,
                                   MMSignal           *umts,
                                   MMSignal           *lte,
                                   MMSignal           *nr5g);

#endif /* MM_IFACE_MODEM_SIGNAL_H */
