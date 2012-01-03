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

#ifndef MM_IFACE_MODEM_CDMA_H
#define MM_IFACE_MODEM_CDMA_H

#include <glib-object.h>
#include <gio/gio.h>

#include <libmm-common.h>

#include "mm-at-serial-port.h"

#define MM_TYPE_IFACE_MODEM_CDMA               (mm_iface_modem_cdma_get_type ())
#define MM_IFACE_MODEM_CDMA(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_CDMA, MMIfaceModemCdma))
#define MM_IS_IFACE_MODEM_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_CDMA))
#define MM_IFACE_MODEM_CDMA_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_CDMA, MMIfaceModemCdma))

#define MM_IFACE_MODEM_CDMA_DBUS_SKELETON        "iface-modem-cdma-dbus-skeleton"

typedef struct _MMIfaceModemCdma MMIfaceModemCdma;

struct _MMIfaceModemCdma {
    GTypeInterface g_iface;

    /* Loading of the MEID property */
    void (*load_meid) (MMIfaceModemCdma *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data);
    gchar * (*load_meid_finish) (MMIfaceModemCdma *self,
                                 GAsyncResult *res,
                                 GError **error);

    /* OTA activation */
    void (* activate) (MMIfaceModemCdma *self,
                       const gchar *carrier,
                       GAsyncReadyCallback callback,
                       gpointer user_data);
    gboolean (* activate_finish) (MMIfaceModemCdma *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Manual activation */
    void (* activate_manual) (MMIfaceModemCdma *self,
                              GVariant *properties,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
    gboolean (* activate_manual_finish) (MMIfaceModemCdma *self,
                                         GAsyncResult *res,
                                         GError **error);
};

GType mm_iface_modem_cdma_get_type (void);

/* Initialize CDMA interface (async) */
void     mm_iface_modem_cdma_initialize        (MMIfaceModemCdma *self,
                                                MMAtSerialPort *port,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
gboolean mm_iface_modem_cdma_initialize_finish (MMIfaceModemCdma *self,
                                                GAsyncResult *res,
                                                GError **error);

/* Enable CDMA interface (async) */
void     mm_iface_modem_cdma_enable        (MMIfaceModemCdma *self,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);
gboolean mm_iface_modem_cdma_enable_finish (MMIfaceModemCdma *self,
                                            GAsyncResult *res,
                                            GError **error);

/* Disable CDMA interface (async) */
void     mm_iface_modem_cdma_disable        (MMIfaceModemCdma *self,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
gboolean mm_iface_modem_cdma_disable_finish (MMIfaceModemCdma *self,
                                             GAsyncResult *res,
                                             GError **error);

/* Shutdown CDMA interface */
void mm_iface_modem_cdma_shutdown (MMIfaceModemCdma *self);

/* Allow OTA activation */
gboolean mm_iface_modem_cdma_activate_finish (MMIfaceModemCdma *self,
                                              GAsyncResult *res,
                                              GError **error);
void     mm_iface_modem_cdma_activate        (MMIfaceModemCdma *self,
                                              const gchar *carrier,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);

/* Allow manual activation */
gboolean mm_iface_modem_cdma_activate_manual_finish (MMIfaceModemCdma *self,
                                                     GAsyncResult *res,
                                                     GError **error);
void     mm_iface_modem_cdma_activate_manual        (MMIfaceModemCdma *self,
                                                     GVariant *properties,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_cdma_bind_simple_status (MMIfaceModemCdma *self,
                                             MMCommonSimpleProperties *status);

#endif /* MM_IFACE_MODEM_CDMA_H */
