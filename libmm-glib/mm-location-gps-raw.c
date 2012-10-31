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
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "mm-common-helpers.h"
#include "mm-errors-types.h"
#include "mm-location-gps-raw.h"

/**
 * SECTION: mm-location-gps-raw
 * @title: MMLocationGpsRaw
 * @short_description: Helper object to handle generic GPS location information.
 *
 * The #MMLocationGpsRaw is an object handling the location information of the
 * modem when this is reported by GPS.
 *
 * This object is retrieved with either mm_modem_location_get_gps_raw(),
 * mm_modem_location_get_gps_raw_sync(), mm_modem_location_get_full() or
 * mm_modem_location_get_full_sync().
 */

G_DEFINE_TYPE (MMLocationGpsRaw, mm_location_gps_raw, G_TYPE_OBJECT);

#define PROPERTY_UTC_TIME  "utc-time"
#define PROPERTY_LATITUDE  "latitude"
#define PROPERTY_LONGITUDE "longitude"
#define PROPERTY_ALTITUDE  "altitude"

struct _MMLocationGpsRawPrivate {
    GRegex *gpgga_regex;

    gchar   *utc_time;
    gdouble  latitude;
    gdouble  longitude;
    gdouble  altitude;
};

/*****************************************************************************/

/**
 * mm_location_gps_raw_get_utc_time:
 * @self: a #MMLocationGpsRaw.
 *
 * Gets the UTC time of the location being reported.
 *
 * Returns: a string with the UTC time, or #NULL if unknown. Do not free the returned value, it is owned by @self.
 */
const gchar *
mm_location_gps_raw_get_utc_time (MMLocationGpsRaw *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_GPS_RAW (self), NULL);

    return self->priv->utc_time;
}

/*****************************************************************************/

/**
 * mm_location_gps_raw_get_longitude:
 * @self: a #MMLocationGpsRaw.
 *
 * Gets the longitude, in the [-180,180] range.
 *
 * Returns: the longitude, or %MM_LOCATION_LONGITUDE_UNKNOWN if unknown.
 */
gdouble
mm_location_gps_raw_get_longitude (MMLocationGpsRaw *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_GPS_RAW (self),
                          MM_LOCATION_LONGITUDE_UNKNOWN);

    return self->priv->longitude;
}

/*****************************************************************************/

/**
 * mm_location_gps_raw_get_latitude:
 * @self: a #MMLocationGpsRaw.
 *
 * Gets the latitude, in the [-90,90] range.
 *
 * Returns: the latitude, or %MM_LOCATION_LATITUDE_UNKNOWN if unknown.
 */
gdouble
mm_location_gps_raw_get_latitude (MMLocationGpsRaw *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_GPS_RAW (self),
                          MM_LOCATION_LATITUDE_UNKNOWN);

    return self->priv->latitude;
}

/*****************************************************************************/

/**
 * mm_location_gps_raw_get_altitude:
 * @self: a #MMLocationGpsRaw.
 *
 * Gets the altitude, in the [-90,90] range.
 *
 * Returns: the altitude, or %MM_LOCATION_ALTITUDE_UNKNOWN if unknown.
 */
gdouble
mm_location_gps_raw_get_altitude (MMLocationGpsRaw *self)
{
    g_return_val_if_fail (MM_IS_LOCATION_GPS_RAW (self),
                          MM_LOCATION_ALTITUDE_UNKNOWN);

    return self->priv->altitude;
}

/*****************************************************************************/

static gboolean
get_longitude_or_latitude_from_match_info (GMatchInfo *match_info,
                                           guint32 match_index,
                                           gdouble *out)
{
    gchar *aux;
    gchar *s;
    gboolean ret = FALSE;
    gdouble minutes;
    gdouble degrees;

    s = g_match_info_fetch (match_info, match_index);
    if (!s)
        goto out;

    /* 4533.35 is 45 degrees and 33.35 minutes */

    aux = strchr (s, '.');
    if (!aux || ((aux - s) < 3))
        goto out;

    aux -= 2;
    if (!mm_get_double_from_str (aux, &minutes))
        goto out;

    aux[0] = '\0';
    if (!mm_get_double_from_str (s, &degrees))
        goto out;

    /* Include the minutes as part of the degrees */
    *out = degrees + (minutes / 60.0);
    ret = TRUE;

out:
    g_free (s);

    return ret;
}

gboolean
mm_location_gps_raw_add_trace (MMLocationGpsRaw *self,
                               const gchar *trace)
{
    GMatchInfo *match_info = NULL;

    /* Current implementation works only with $GPGGA traces */
    if (!g_str_has_prefix (trace, "$GPGGA"))
        return FALSE;

    /*
     * $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
     * 1    = UTC of Position
     * 2    = Latitude
     * 3    = N or S
     * 4    = Longitude
     * 5    = E or W
     * 6    = GPS quality indicator (0=invalid; 1=GPS fix; 2=Diff. GPS fix)
     * 7    = Number of satellites in use [not those in view]
     * 8    = Horizontal dilution of position
     * 9    = Antenna altitude above/below mean sea level (geoid)
     * 10   = Meters  (Antenna height unit)
     * 11   = Geoidal separation (Diff. between WGS-84 earth ellipsoid and
     *        mean sea level.  -=geoid is below WGS-84 ellipsoid)
     * 12   = Meters  (Units of geoidal separation)
     * 13   = Age in seconds since last update from diff. reference station
     * 14   = Diff. reference station ID#
     * 15   = Checksum
     */
    if (G_UNLIKELY (!self->priv->gpgga_regex))
        self->priv->gpgga_regex = g_regex_new ("\\$GPGGA,(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*),(.*)\\*(.*).*",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE,
                                               0,
                                               NULL);

    if (g_regex_match (self->priv->gpgga_regex, trace, 0, &match_info)) {
        /* UTC time */
        if (self->priv->utc_time)
            g_free (self->priv->utc_time);
        self->priv->utc_time = g_match_info_fetch (match_info, 1);

        /* Latitude */
        self->priv->latitude = MM_LOCATION_LATITUDE_UNKNOWN;
        if (get_longitude_or_latitude_from_match_info (match_info, 2, &self->priv->latitude)) {
            gchar *str;

            /* N/S */
            str = g_match_info_fetch (match_info, 3);
            if (str && str[0] == 'S')
                self->priv->latitude *= -1;
            g_free (str);
        }

        /* Longitude */
        self->priv->longitude = MM_LOCATION_LONGITUDE_UNKNOWN;
        if (get_longitude_or_latitude_from_match_info (match_info, 4, &self->priv->longitude)) {
            gchar *str;

            /* N/S */
            str = g_match_info_fetch (match_info, 5);
            if (str && str[0] == 'W')
                self->priv->longitude *= -1;
            g_free (str);
        }

        /* Altitude */
        self->priv->altitude = MM_LOCATION_ALTITUDE_UNKNOWN;
        mm_get_double_from_match_info (match_info, 9, &self->priv->altitude);
    }

    g_match_info_free (match_info);

    return TRUE;
}

/*****************************************************************************/

GVariant *
mm_location_gps_raw_get_dictionary (MMLocationGpsRaw *self)
{
    GVariantBuilder builder;

    /* We do allow NULL */
    if (!self)
        return NULL;

    g_return_val_if_fail (MM_IS_LOCATION_GPS_RAW (self), NULL);

    /* If mandatory parameters are not found, return NULL */
    if (!self->priv->utc_time ||
        self->priv->longitude == MM_LOCATION_LONGITUDE_UNKNOWN ||
        self->priv->latitude == MM_LOCATION_LATITUDE_UNKNOWN)
        return NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_UTC_TIME,
                           g_variant_new_string (self->priv->utc_time));
    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_LONGITUDE,
                           g_variant_new_double (self->priv->longitude));
    g_variant_builder_add (&builder,
                           "{sv}",
                           PROPERTY_LATITUDE,
                           g_variant_new_double (self->priv->latitude));

    /* Altitude is optional */
    if (self->priv->altitude != MM_LOCATION_ALTITUDE_UNKNOWN)
        g_variant_builder_add (&builder,
                               "{sv}",
                               PROPERTY_ALTITUDE,
                               g_variant_new_double (self->priv->altitude));

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*****************************************************************************/

MMLocationGpsRaw *
mm_location_gps_raw_new_from_dictionary (GVariant *dictionary,
                                         GError **error)
{
    GError *inner_error = NULL;
    MMLocationGpsRaw *self;
    GVariantIter iter;
    gchar *key;
    GVariant *value;

    self = mm_location_gps_raw_new ();
    if (!dictionary)
        return self;

    if (!g_variant_is_of_type (dictionary, G_VARIANT_TYPE ("a{sv}"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create GPS RAW location from dictionary: "
                     "invalid variant type received");
        g_object_unref (self);
        return NULL;
    }

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, PROPERTY_UTC_TIME))
            self->priv->utc_time = g_variant_dup_string (value, NULL);
        else if (g_str_equal (key, PROPERTY_LONGITUDE))
            self->priv->longitude = g_variant_get_double (value);
        else if (g_str_equal (key, PROPERTY_LATITUDE))
            self->priv->latitude = g_variant_get_double (value);
        else if (g_str_equal (key, PROPERTY_ALTITUDE))
            self->priv->altitude = g_variant_get_double (value);
        g_free (key);
        g_variant_unref (value);
    }

    /* If any of the mandatory parameters is missing, cleanup */
    if (!self->priv->utc_time ||
        self->priv->longitude == MM_LOCATION_LONGITUDE_UNKNOWN ||
        self->priv->latitude == MM_LOCATION_LATITUDE_UNKNOWN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Cannot create GPS RAW location from dictionary: "
                     "mandatory parameters missing "
                     "(utc-time: %s, longitude: %s, latitude: %s)",
                     self->priv->utc_time ? "yes" : "missing",
                     (self->priv->longitude != MM_LOCATION_LONGITUDE_UNKNOWN) ? "yes" : "missing",
                     (self->priv->latitude != MM_LOCATION_LATITUDE_UNKNOWN) ? "yes" : "missing");
        g_clear_object (&self);
    }

    return self;
}

/*****************************************************************************/

MMLocationGpsRaw *
mm_location_gps_raw_new (void)
{
    return (MM_LOCATION_GPS_RAW (
                g_object_new (MM_TYPE_LOCATION_GPS_RAW, NULL)));
}

static void
mm_location_gps_raw_init (MMLocationGpsRaw *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_LOCATION_GPS_RAW,
                                              MMLocationGpsRawPrivate);

    self->priv->utc_time = NULL;
    self->priv->latitude = MM_LOCATION_LATITUDE_UNKNOWN;
    self->priv->longitude = MM_LOCATION_LONGITUDE_UNKNOWN;
    self->priv->altitude = MM_LOCATION_ALTITUDE_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMLocationGpsRaw *self = MM_LOCATION_GPS_RAW (object);

    if (self->priv->gpgga_regex)
        g_regex_unref (self->priv->gpgga_regex);

    G_OBJECT_CLASS (mm_location_gps_raw_parent_class)->finalize (object);
}

static void
mm_location_gps_raw_class_init (MMLocationGpsRawClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMLocationGpsRawPrivate));

    object_class->finalize = finalize;
}
