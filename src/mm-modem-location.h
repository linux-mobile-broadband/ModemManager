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
 * Copyright (C) 2010 Red Hat, Inc.
 */

#ifndef MM_MODEM_LOCATION_H
#define MM_MODEM_LOCATION_H

#include <mm-modem.h>

#define MM_TYPE_MODEM_LOCATION               (mm_modem_location_get_type ())
#define MM_MODEM_LOCATION(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_LOCATION, MMModemLocation))
#define MM_IS_MODEM_LOCATION(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_LOCATION))
#define MM_MODEM_LOCATION_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM_LOCATION, MMModemLocation))

#define MM_MODEM_LOCATION_DBUS_INTERFACE "org.freedesktop.ModemManager.Modem.Location"

#define MM_MODEM_LOCATION_PROP_TYPE (dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, G_TYPE_VALUE))

#define MM_MODEM_LOCATION_CAPABILITIES     "location-capabilities"
#define MM_MODEM_LOCATION_ENABLED          "location-enabled"
#define MM_MODEM_LOCATION_SIGNALS_LOCATION "signals-location"
#define MM_MODEM_LOCATION_LOCATION         "location"

typedef enum {
    MM_MODEM_LOCATION_CAPABILITY_UNKNOWN    = 0x00000000,
    MM_MODEM_LOCATION_CAPABILITY_GPS_NMEA   = 0x00000001,
    MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI = 0x00000002,
    MM_MODEM_LOCATION_CAPABILITY_GPS_RAW    = 0x00000004,

    MM_MODEM_LOCATION_CAPABILITY_LAST = MM_MODEM_LOCATION_CAPABILITY_GPS_RAW
} MMModemLocationCapabilities;

typedef struct _MMModemLocation MMModemLocation;

typedef void (*MMModemLocationGetFn) (MMModemLocation *modem,
                                      GHashTable *locations,
                                      GError *error,
                                      gpointer user_data);

struct _MMModemLocation {
    GTypeInterface g_iface;

    /* Methods */
    void (*enable) (MMModemLocation *modem,
                    gboolean enable,
                    gboolean signal_location,
                    MMModemFn callback,
                    gpointer user_data);

    void (*get_location) (MMModemLocation *modem,
                          MMModemLocationGetFn callback,
                          gpointer user_data);
};

GType mm_modem_location_get_type (void);

void mm_modem_location_enable (MMModemLocation *self,
                               gboolean enable,
                               gboolean signal_location,
                               MMModemFn callback,
                               gpointer user_data);

void mm_modem_location_get_location (MMModemLocation *self,
                                     MMModemLocationGetFn callback,
                                     gpointer user_data);

#endif /* MM_MODEM_LOCATION_H */
