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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_SIGNAL_H
#define MM_SIGNAL_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * MM_SIGNAL_UNKNOWN:
 *
 * Identifier for an unknown signal value.
 *
 * Since: 1.2
 */
#define MM_SIGNAL_UNKNOWN -G_MAXDOUBLE

#define MM_TYPE_SIGNAL            (mm_signal_get_type ())
#define MM_SIGNAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIGNAL, MMSignal))
#define MM_SIGNAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIGNAL, MMSignalClass))
#define MM_IS_SIGNAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIGNAL))
#define MM_IS_SIGNAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIGNAL))
#define MM_SIGNAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIGNAL, MMSignalClass))

typedef struct _MMSignal MMSignal;
typedef struct _MMSignalClass MMSignalClass;
typedef struct _MMSignalPrivate MMSignalPrivate;

/**
 * MMSignal:
 *
 * The #MMSignal structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMSignal {
    /*< private >*/
    GObject parent;
    MMSignalPrivate *priv;
};

struct _MMSignalClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_signal_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSignal, g_object_unref)

gdouble  mm_signal_get_rssi (MMSignal *self);
gdouble  mm_signal_get_rscp (MMSignal *self);
gdouble  mm_signal_get_ecio (MMSignal *self);
gdouble  mm_signal_get_sinr (MMSignal *self);
gdouble  mm_signal_get_io   (MMSignal *self);
gdouble  mm_signal_get_rsrq (MMSignal *self);
gdouble  mm_signal_get_rsrp (MMSignal *self);
gdouble  mm_signal_get_snr  (MMSignal *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

GVariant *mm_signal_get_dictionary (MMSignal *self);

MMSignal *mm_signal_new (void);
MMSignal *mm_signal_new_from_dictionary (GVariant *dictionary,
                                         GError **error);

void mm_signal_set_rssi (MMSignal *self, gdouble value);
void mm_signal_set_rscp (MMSignal *self, gdouble value);
void mm_signal_set_ecio (MMSignal *self, gdouble value);
void mm_signal_set_sinr (MMSignal *self, gdouble value);
void mm_signal_set_io   (MMSignal *self, gdouble value);
void mm_signal_set_rsrq (MMSignal *self, gdouble value);
void mm_signal_set_rsrp (MMSignal *self, gdouble value);
void mm_signal_set_snr  (MMSignal *self, gdouble value);

#endif

G_END_DECLS

#endif /* MM_SIGNAL_H */
