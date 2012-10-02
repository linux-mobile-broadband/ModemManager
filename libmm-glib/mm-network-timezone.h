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

#ifndef MM_NETWORK_TIMEZONE_H
#define MM_NETWORK_TIMEZONE_H

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_NETWORK_TIMEZONE            (mm_network_timezone_get_type ())
#define MM_NETWORK_TIMEZONE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_NETWORK_TIMEZONE, MMNetworkTimezone))
#define MM_NETWORK_TIMEZONE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_NETWORK_TIMEZONE, MMNetworkTimezoneClass))
#define MM_IS_NETWORK_TIMEZONE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_NETWORK_TIMEZONE))
#define MM_IS_NETWORK_TIMEZONE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_NETWORK_TIMEZONE))
#define MM_NETWORK_TIMEZONE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_NETWORK_TIMEZONE, MMNetworkTimezoneClass))

#define MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN       G_MAXINT32
#define MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN G_MAXINT32

typedef struct _MMNetworkTimezone MMNetworkTimezone;
typedef struct _MMNetworkTimezoneClass MMNetworkTimezoneClass;
typedef struct _MMNetworkTimezonePrivate MMNetworkTimezonePrivate;

struct _MMNetworkTimezone {
    GObject parent;
    MMNetworkTimezonePrivate *priv;
};

struct _MMNetworkTimezoneClass {
    GObjectClass parent;
};

GType mm_network_timezone_get_type (void);

MMNetworkTimezone *mm_network_timezone_new (void);
MMNetworkTimezone *mm_network_timezone_new_from_dictionary (GVariant *dictionary,
                                                            GError **error);

gint32 mm_network_timezone_get_offset       (MMNetworkTimezone *self);
gint32 mm_network_timezone_get_dst_offset   (MMNetworkTimezone *self);
gint32 mm_network_timezone_get_leap_seconds (MMNetworkTimezone *self);

void mm_network_timezone_set_offset       (MMNetworkTimezone *self,
                                           gint offset);
void mm_network_timezone_set_dst_offset   (MMNetworkTimezone *self,
                                           gint dst_offset);
void mm_network_timezone_set_leap_seconds (MMNetworkTimezone *self,
                                           gint leap_seconds);

GVariant *mm_network_timezone_get_dictionary (MMNetworkTimezone *self);

G_END_DECLS

#endif /* MM_NETWORK_TIMEZONE_H */
