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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_H
#define MM_IFACE_MODEM_H

#include <glib-object.h>
#include <gio/gio.h>

#define MM_TYPE_IFACE_MODEM            (mm_iface_modem_get_type ())
#define MM_IFACE_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM, MMIfaceModem))
#define MM_IS_IFACE_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM))
#define MM_IFACE_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM, MMIfaceModem))

#define MM_IFACE_MODEM_DBUS_SKELETON   "iface-modem-dbus-skeleton"

typedef struct _MMIfaceModem MMIfaceModem;

struct _MMIfaceModem {
    GTypeInterface g_iface;

    /* Loading of the ModemCapabilities property */
    void (*load_modem_capabilities) (MMIfaceModem *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    MMModemCapability (*load_modem_capabilities_finish) (MMIfaceModem *self,
                                                         GAsyncResult *res,
                                                         GError **error);

    /* Loading of the CurrentCapabilities property */
    void (*load_current_capabilities) (MMIfaceModem *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    MMModemCapability (*load_current_capabilities_finish) (MMIfaceModem *self,
                                                           GAsyncResult *res,
                                                           GError **error);
};

GType mm_iface_modem_get_type (void);

/* Initialize Modem interface (async) */
void     mm_iface_modem_initialize        (MMIfaceModem *self,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_iface_modem_initialize_finish (MMIfaceModem *self,
                                           GAsyncResult *res,
                                           GError **error);

/* Shutdown Modem interface */
gboolean mm_iface_modem_shutdown (MMIfaceModem *self,
                                  GError **error);

#endif /* MM_IFACE_MODEM_H */
