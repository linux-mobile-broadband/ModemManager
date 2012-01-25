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

#ifndef MM_IFACE_MODEM_MESSAGING_H
#define MM_IFACE_MODEM_MESSAGING_H

#include <glib-object.h>
#include <gio/gio.h>

#include "mm-at-serial-port.h"

#define MM_TYPE_IFACE_MODEM_MESSAGING               (mm_iface_modem_messaging_get_type ())
#define MM_IFACE_MODEM_MESSAGING(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_MESSAGING, MMIfaceModemMessaging))
#define MM_IS_IFACE_MODEM_MESSAGING(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_MESSAGING))
#define MM_IFACE_MODEM_MESSAGING_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_MESSAGING, MMIfaceModemMessaging))

#define MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON "iface-modem-messaging-dbus-skeleton"

typedef struct _MMIfaceModemMessaging MMIfaceModemMessaging;

struct _MMIfaceModemMessaging {
    GTypeInterface g_iface;

    /* Check for Messaging support (async) */
    void (* check_support) (MMIfaceModemMessaging *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
    gboolean (*check_support_finish) (MMIfaceModemMessaging *self,
                                      GAsyncResult *res,
                                      GError **error);
};

GType mm_iface_modem_messaging_get_type (void);

/* Initialize Messaging interface (async) */
void     mm_iface_modem_messaging_initialize        (MMIfaceModemMessaging *self,
                                                     MMAtSerialPort *port,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);
gboolean mm_iface_modem_messaging_initialize_finish (MMIfaceModemMessaging *self,
                                                     GAsyncResult *res,
                                                     GError **error);

/* Enable Messaging interface (async) */
void     mm_iface_modem_messaging_enable        (MMIfaceModemMessaging *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean mm_iface_modem_messaging_enable_finish (MMIfaceModemMessaging *self,
                                                 GAsyncResult *res,
                                                 GError **error);

/* Disable Messaging interface (async) */
void     mm_iface_modem_messaging_disable        (MMIfaceModemMessaging *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
gboolean mm_iface_modem_messaging_disable_finish (MMIfaceModemMessaging *self,
                                                  GAsyncResult *res,
                                                  GError **error);

/* Shutdown Messaging interface */
void mm_iface_modem_messaging_shutdown (MMIfaceModemMessaging *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_messaging_bind_simple_status (MMIfaceModemMessaging *self,
                                                  MMCommonSimpleProperties *status);

#endif /* MM_IFACE_MODEM_MESSAGING_H */
