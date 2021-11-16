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
 * Copyright (C) 2015 Azimut Electronics
 *
 * Author: Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_BEARER_STATS_H
#define MM_BEARER_STATS_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_BEARER_STATS            (mm_bearer_stats_get_type ())
#define MM_BEARER_STATS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BEARER_STATS, MMBearerStats))
#define MM_BEARER_STATS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BEARER_STATS, MMBearerStatsClass))
#define MM_IS_BEARER_STATS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BEARER_STATS))
#define MM_IS_BEARER_STATS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BEARER_STATS))
#define MM_BEARER_STATS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BEARER_STATS, MMBearerStatsClass))

typedef struct _MMBearerStats MMBearerStats;
typedef struct _MMBearerStatsClass MMBearerStatsClass;
typedef struct _MMBearerStatsPrivate MMBearerStatsPrivate;

/**
 * MMBearerStats:
 *
 * The #MMBearerStats structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMBearerStats {
    /*< private >*/
    GObject parent;
    MMBearerStatsPrivate *priv;
};

struct _MMBearerStatsClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_bearer_stats_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBearerStats, g_object_unref)

guint   mm_bearer_stats_get_duration        (MMBearerStats *self);
guint64 mm_bearer_stats_get_rx_bytes        (MMBearerStats *self);
guint64 mm_bearer_stats_get_tx_bytes        (MMBearerStats *self);
guint   mm_bearer_stats_get_attempts        (MMBearerStats *self);
guint   mm_bearer_stats_get_failed_attempts (MMBearerStats *self);
guint   mm_bearer_stats_get_total_duration  (MMBearerStats *self);
guint64 mm_bearer_stats_get_total_rx_bytes  (MMBearerStats *self);
guint64 mm_bearer_stats_get_total_tx_bytes  (MMBearerStats *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMBearerStats *mm_bearer_stats_new (void);
MMBearerStats *mm_bearer_stats_new_from_dictionary (GVariant *dictionary,
                                                    GError **error);

void mm_bearer_stats_set_duration             (MMBearerStats *self, guint   duration);
void mm_bearer_stats_set_rx_bytes             (MMBearerStats *self, guint64 rx_bytes);
void mm_bearer_stats_set_tx_bytes             (MMBearerStats *self, guint64 tx_bytes);
void mm_bearer_stats_set_attempts             (MMBearerStats *self, guint   attempts);
void mm_bearer_stats_set_failed_attempts      (MMBearerStats *self, guint   failed_attempts);
void mm_bearer_stats_set_total_duration       (MMBearerStats *self, guint   duration);
void mm_bearer_stats_set_total_rx_bytes       (MMBearerStats *self, guint64 rx_bytes);
void mm_bearer_stats_set_total_tx_bytes       (MMBearerStats *self, guint64 tx_bytes);

GVariant *mm_bearer_stats_get_dictionary (MMBearerStats *self);

#endif

G_END_DECLS

#endif /* MM_BEARER_STATS_H */
