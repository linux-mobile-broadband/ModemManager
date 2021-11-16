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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_NETWORK_TIMEZONE_H
#define MM_NETWORK_TIMEZONE_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_NETWORK_TIMEZONE            (mm_network_timezone_get_type ())
#define MM_NETWORK_TIMEZONE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_NETWORK_TIMEZONE, MMNetworkTimezone))
#define MM_NETWORK_TIMEZONE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_NETWORK_TIMEZONE, MMNetworkTimezoneClass))
#define MM_IS_NETWORK_TIMEZONE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_NETWORK_TIMEZONE))
#define MM_IS_NETWORK_TIMEZONE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_NETWORK_TIMEZONE))
#define MM_NETWORK_TIMEZONE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_NETWORK_TIMEZONE, MMNetworkTimezoneClass))

/**
 * MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN:
 *
 * Identifier for an unknown timezone offset.
 *
 * Since: 1.0
 */
#define MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN G_MAXINT32

/**
 * MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN:
 *
 * Identifier for an unknown leap seconds value.
 *
 * Since: 1.0
 */
#define MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN G_MAXINT32

typedef struct _MMNetworkTimezone MMNetworkTimezone;
typedef struct _MMNetworkTimezoneClass MMNetworkTimezoneClass;
typedef struct _MMNetworkTimezonePrivate MMNetworkTimezonePrivate;

/**
 * MMNetworkTimezone:
 *
 * The #MMNetworkTimezone structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMNetworkTimezone {
    /*< private >*/
    GObject parent;
    MMNetworkTimezonePrivate *priv;
};

struct _MMNetworkTimezoneClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_network_timezone_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMNetworkTimezone, g_object_unref)

gint32 mm_network_timezone_get_offset       (MMNetworkTimezone *self);
gint32 mm_network_timezone_get_dst_offset   (MMNetworkTimezone *self);
gint32 mm_network_timezone_get_leap_seconds (MMNetworkTimezone *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMNetworkTimezone *mm_network_timezone_new (void);
MMNetworkTimezone *mm_network_timezone_new_from_dictionary (GVariant *dictionary,
                                                            GError **error);

void mm_network_timezone_set_offset       (MMNetworkTimezone *self,
                                           gint offset);
void mm_network_timezone_set_dst_offset   (MMNetworkTimezone *self,
                                           gint dst_offset);
void mm_network_timezone_set_leap_seconds (MMNetworkTimezone *self,
                                           gint leap_seconds);

GVariant *mm_network_timezone_get_dictionary (MMNetworkTimezone *self);

#endif

G_END_DECLS

#endif /* MM_NETWORK_TIMEZONE_H */
