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
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 */

#ifndef MM_LOCATION_GPS_NMEA_H
#define MM_LOCATION_GPS_NMEA_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_LOCATION_GPS_NMEA            (mm_location_gps_nmea_get_type ())
#define MM_LOCATION_GPS_NMEA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_LOCATION_GPS_NMEA, MMLocationGpsNmea))
#define MM_LOCATION_GPS_NMEA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_LOCATION_GPS_NMEA, MMLocationGpsNmeaClass))
#define MM_IS_LOCATION_GPS_NMEA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_LOCATION_GPS_NMEA))
#define MM_IS_LOCATION_GPS_NMEA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_LOCATION_GPS_NMEA))
#define MM_LOCATION_GPS_NMEA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_LOCATION_GPS_NMEA, MMLocationGpsNmeaClass))

typedef struct _MMLocationGpsNmea MMLocationGpsNmea;
typedef struct _MMLocationGpsNmeaClass MMLocationGpsNmeaClass;
typedef struct _MMLocationGpsNmeaPrivate MMLocationGpsNmeaPrivate;

/**
 * MMLocationGpsNmea:
 *
 * The #MMLocationGpsNmea structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMLocationGpsNmea {
    /*< private >*/
    GObject parent;
    MMLocationGpsNmeaPrivate *priv;
};

struct _MMLocationGpsNmeaClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_location_gps_nmea_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMLocationGpsNmea, g_object_unref)

const gchar  *mm_location_gps_nmea_get_trace  (MMLocationGpsNmea *self,
                                               const gchar       *trace_type);
gchar       **mm_location_gps_nmea_get_traces (MMLocationGpsNmea *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMLocationGpsNmea *mm_location_gps_nmea_new (void);
MMLocationGpsNmea *mm_location_gps_nmea_new_from_string_variant (GVariant *string,
                                                                 GError **error);

gboolean mm_location_gps_nmea_add_trace (MMLocationGpsNmea *self,
                                         const gchar *trace);

GVariant *mm_location_gps_nmea_get_string_variant (MMLocationGpsNmea *self);

#endif

G_END_DECLS

#endif /* MM_LOCATION_GPS_NMEA_H */
