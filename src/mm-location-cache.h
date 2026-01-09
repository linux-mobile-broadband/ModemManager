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

#ifndef MM_LOCATION_CACHE_H
#define MM_LOCATION_CACHE_H

#include <glib.h>
#include <glib-object.h>

#define MM_TYPE_LOCATION_CACHE            (mm_location_cache_get_type ())
#define MM_LOCATION_CACHE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_LOCATION_CACHE, MMLocationCache))
#define MM_LOCATION_CACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_LOCATION_CACHE, MMLocationCacheClass))
#define MM_IS_LOCATION_CACHE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_LOCATION_CACHE))
#define MM_IS_LOCATION_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_LOCATION_CACHE))
#define MM_LOCATION_CACHE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_LOCATION_CACHE, MMLocationCacheClass))

typedef struct _MMLocationCache MMLocationCache;
typedef struct _MMLocationCacheClass MMLocationCacheClass;
typedef struct _MMLocationCachePrivate MMLocationCachePrivate;

struct _MMLocationCache {
    GObject parent;
    MMLocationCachePrivate *priv;
};

struct _MMLocationCacheClass {
    GObjectClass parent;
};

GType mm_location_cache_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMLocationCache, g_object_unref)

MMLocationCache *mm_location_cache_new (void);

gboolean mm_location_cache_load_from_file      (MMLocationCache *self, const gchar *file, GError **error);
gboolean mm_location_cache_load                (MMLocationCache *self, GError **error);
gboolean mm_location_cache_save_to_file        (MMLocationCache *self, const gchar *file, GError **error);
gboolean mm_location_cache_save                (MMLocationCache *self, GError **error);
void     mm_location_cache_set_filename        (MMLocationCache *self, const gchar *file);
void     mm_location_cache_update_from_lat_lon (MMLocationCache *self, const double lat, const double lon);
void     mm_location_cache_update_from_nmea    (MMLocationCache *self, const gchar *nmea);
void     mm_location_cache_get_lat_lon         (MMLocationCache *self, double *lat, double *lon);

#endif /* MM_LOCATION_CACHE_H */

