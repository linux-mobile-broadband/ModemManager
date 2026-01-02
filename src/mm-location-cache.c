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
 * Copyright (C) 2026 Andrey Skvortsov <andrej.skvortzov@gmail.com>
 */


#include <config.h>
#include <glib/gstdio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-location-cache.h"
#include "mm-log-object.h"

#if !defined PKGSTATEDIR
# error PKGSTATEDIR is not defined
#endif

#define LOCATION_STATE_FILE "location.ini"
#define LOCATION_LAST_POS_GROUP "last_position"
#define LOCATION_LAST_POS_LATITUDE_KEY "latitude"
#define LOCATION_LAST_POS_LONGITUDE_KEY "longitude"

/*****************************************************************************/

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMLocationCache, mm_location_cache, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

struct _MMLocationCachePrivate {
    MMLocationGpsRaw *gps_raw;
    gdouble last_latitude;
    gdouble last_longitude;
    gchar*  filename;
};

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("location-cache");
}

gboolean
mm_location_cache_load_from_file (MMLocationCache  *self,
                                  const gchar      *file,
                                  GError          **error)
{
    g_autoptr(GKeyFile)  key_file = g_key_file_new ();
    gdouble lat;
    gdouble lon;

    self->priv->last_latitude = MM_LOCATION_LATITUDE_UNKNOWN;
    self->priv->last_longitude = MM_LOCATION_LONGITUDE_UNKNOWN;

    if (!g_key_file_load_from_file (key_file, file, G_KEY_FILE_NONE, error)) {
        g_prefix_error (error, "Error loading cached location from %s: ", file);
        return FALSE;
    }

    lat = g_key_file_get_double (key_file,
                                 LOCATION_LAST_POS_GROUP,
                                 LOCATION_LAST_POS_LATITUDE_KEY,
                                 error);
    if (*error) {
        g_prefix_error (error, "Error finding latitude in file: ");
        return FALSE;
    }

    lon = g_key_file_get_double (key_file,
                                 LOCATION_LAST_POS_GROUP,
                                 LOCATION_LAST_POS_LONGITUDE_KEY,
                                 error);
    if (*error) {
        g_prefix_error (error, "Error finding longitude in file: ");
        return FALSE;
    }

    self->priv->last_latitude = lat;
    self->priv->last_longitude = lon;
    return TRUE;
}

gboolean
mm_location_cache_load (MMLocationCache  *self,
                        GError          **error)
{
    return mm_location_cache_load_from_file (self, self->priv->filename, error);
}

gboolean
mm_location_cache_save_to_file (MMLocationCache  *self,
                                const gchar      *file,
                                GError          **error)
{
    g_autoptr(GKeyFile)  key_file = g_key_file_new ();

    if ((self->priv->last_latitude == MM_LOCATION_LATITUDE_UNKNOWN) ||
        (self->priv->last_longitude == MM_LOCATION_LONGITUDE_UNKNOWN)) {
            return TRUE;
    }

    key_file = g_key_file_new ();
    g_key_file_set_double (key_file,
                           LOCATION_LAST_POS_GROUP,
                           LOCATION_LAST_POS_LATITUDE_KEY,
                           self->priv->last_latitude);
    g_key_file_set_double (key_file,
                           LOCATION_LAST_POS_GROUP,
                           LOCATION_LAST_POS_LONGITUDE_KEY,
                           self->priv->last_longitude);

    if (!g_key_file_save_to_file (key_file, file, error)) {
        g_prefix_error (error, "Error saving cached location to %s", file);
        return FALSE;
    }

    return TRUE;
}

gboolean
mm_location_cache_save (MMLocationCache  *self,
                        GError          **error)
{
    return mm_location_cache_save_to_file (self, self->priv->filename, error);
}

void
mm_location_cache_set_filename (MMLocationCache *self,
                                const gchar     *file)
{
    g_free (self->priv->filename);
    self->priv->filename = g_strdup (file);
}

void
mm_location_cache_update_from_lat_lon (MMLocationCache *self,
                                       const double     lat,
                                       const double     lon)
{
    if (lat != MM_LOCATION_LATITUDE_UNKNOWN &&
        lon != MM_LOCATION_LONGITUDE_UNKNOWN) {
        self->priv->last_latitude = lat;
        self->priv->last_longitude = lon;
    }
}

void
mm_location_cache_update_from_nmea (MMLocationCache *self,
                                    const gchar     *trace)
{
    gdouble  lat;
    gdouble  lon;

    g_assert (self->priv->gps_raw != NULL);
    if (mm_location_gps_raw_add_trace (self->priv->gps_raw, trace)) {
        lat =  mm_location_gps_raw_get_latitude (self->priv->gps_raw);
        lon =  mm_location_gps_raw_get_longitude (self->priv->gps_raw);
        mm_location_cache_update_from_lat_lon (self, lat, lon);
    }
}

void
mm_location_cache_get_lat_lon (MMLocationCache *self,
                               double          *lat,
                               double          *lon)
{
    *lat = self->priv->last_latitude;
    *lon = self->priv->last_longitude;
}

MMLocationCache *
mm_location_cache_new (void)
{
    return MM_LOCATION_CACHE (g_object_new (MM_TYPE_LOCATION_CACHE, NULL));
}

static void
mm_location_cache_init (MMLocationCache *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_LOCATION_CACHE, MMLocationCachePrivate);

    self->priv->last_longitude = MM_LOCATION_LONGITUDE_UNKNOWN;
    self->priv->last_latitude = MM_LOCATION_LATITUDE_UNKNOWN;
    self->priv->gps_raw = mm_location_gps_raw_new ();

    self->priv->filename = g_build_path (G_DIR_SEPARATOR_S, PKGSTATEDIR, LOCATION_STATE_FILE, NULL);
}

static void
finalize (GObject *object)
{
    MMLocationCache *self = MM_LOCATION_CACHE (object);

    g_object_unref (self->priv->gps_raw);
    g_free (self->priv->filename);

    G_OBJECT_CLASS (mm_location_cache_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    g_autoptr(GError)  error = NULL;
    MMLocationCache   *self = MM_LOCATION_CACHE (object);

    if (!mm_location_cache_save (self, &error))
        mm_obj_warn (self, "%s", error->message);

    G_OBJECT_CLASS (mm_location_cache_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_location_cache_class_init (MMLocationCacheClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMLocationCachePrivate));

    object_class->finalize = finalize;
    object_class->dispose = dispose;
}
