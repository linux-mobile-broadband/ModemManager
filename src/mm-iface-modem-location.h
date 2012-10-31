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

#ifndef MM_IFACE_MODEM_LOCATION_H
#define MM_IFACE_MODEM_LOCATION_H

#include <glib-object.h>
#include <gio/gio.h>

#include "mm-at-serial-port.h"

#define MM_TYPE_IFACE_MODEM_LOCATION               (mm_iface_modem_location_get_type ())
#define MM_IFACE_MODEM_LOCATION(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_LOCATION, MMIfaceModemLocation))
#define MM_IS_IFACE_MODEM_LOCATION(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_LOCATION))
#define MM_IFACE_MODEM_LOCATION_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_LOCATION, MMIfaceModemLocation))

#define MM_IFACE_MODEM_LOCATION_DBUS_SKELETON "iface-modem-location-dbus-skeleton"

typedef struct _MMIfaceModemLocation MMIfaceModemLocation;

struct _MMIfaceModemLocation {
    GTypeInterface g_iface;

    /* Loading of the Capabilities property */
    void (*load_capabilities) (MMIfaceModemLocation *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    MMModemLocationSource (*load_capabilities_finish) (MMIfaceModemLocation *self,
                                                       GAsyncResult *res,
                                                       GError **error);

    /* Enable location gathering (async) */
    void (* enable_location_gathering) (MMIfaceModemLocation *self,
                                        MMModemLocationSource source,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
    gboolean (*enable_location_gathering_finish) (MMIfaceModemLocation *self,
                                                  GAsyncResult *res,
                                                  GError **error);

    /* Disable location gathering (async) */
    void (* disable_location_gathering) (MMIfaceModemLocation *self,
                                         MMModemLocationSource source,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    gboolean (*disable_location_gathering_finish) (MMIfaceModemLocation *self,
                                                   GAsyncResult *res,
                                                   GError **error);
};

GType mm_iface_modem_location_get_type (void);

/* Initialize Location interface (async) */
void     mm_iface_modem_location_initialize        (MMIfaceModemLocation *self,
                                                    GCancellable *cancellable,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data);
gboolean mm_iface_modem_location_initialize_finish (MMIfaceModemLocation *self,
                                                    GAsyncResult *res,
                                                    GError **error);

/* Enable Location interface (async) */
void     mm_iface_modem_location_enable        (MMIfaceModemLocation *self,
                                                GCancellable *cancellable,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
gboolean mm_iface_modem_location_enable_finish (MMIfaceModemLocation *self,
                                                GAsyncResult *res,
                                                GError **error);

/* Disable Location interface (async) */
void     mm_iface_modem_location_disable        (MMIfaceModemLocation *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean mm_iface_modem_location_disable_finish (MMIfaceModemLocation *self,
                                                 GAsyncResult *res,
                                                 GError **error);

/* Shutdown Location interface */
void mm_iface_modem_location_shutdown (MMIfaceModemLocation *self);

/* Update 3GPP (LAC/CI) location */
void mm_iface_modem_location_3gpp_clear          (MMIfaceModemLocation *self);
void mm_iface_modem_location_3gpp_update_mcc_mnc (MMIfaceModemLocation *self,
                                                  guint mobile_country_code,
                                                  guint mobile_network_code);
void mm_iface_modem_location_3gpp_update_lac_ci  (MMIfaceModemLocation *self,
                                                  gulong location_area_code,
                                                  gulong cell_id);

/* Update GPS location */
void mm_iface_modem_location_gps_update (MMIfaceModemLocation *self,
                                         const gchar *nmea_trace);

/* Update CDMA BS location */
void mm_iface_modem_location_cdma_bs_update (MMIfaceModemLocation *self,
                                             gdouble longitude,
                                             gdouble latitude);
void mm_iface_modem_location_cdma_bs_clear (MMIfaceModemLocation *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_location_bind_simple_status (MMIfaceModemLocation *self,
                                                 MMSimpleStatus *status);

#endif /* MM_IFACE_MODEM_LOCATION_H */
