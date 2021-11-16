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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Intel Corporation
 */

#ifndef MM_SIGNAL_THRESHOLD_PROPERTIES_H
#define MM_SIGNAL_THRESHOLD_PROPERTIES_H

#include <ModemManager.h>
#include <glib-object.h>

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

G_BEGIN_DECLS

#define MM_TYPE_SIGNAL_THRESHOLD_PROPERTIES            (mm_signal_threshold_properties_get_type ())
#define MM_SIGNAL_THRESHOLD_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SIGNAL_THRESHOLD_PROPERTIES, MMSignalThresholdProperties))
#define MM_SIGNAL_THRESHOLD_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SIGNAL_THRESHOLD_PROPERTIES, MMSignalThresholdPropertiesClass))
#define MM_IS_SIGNAL_THRESHOLD_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SIGNAL_THRESHOLD_PROPERTIES))
#define MM_IS_SIGNAL_THRESHOLD_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SIGNAL_THRESHOLD_PROPERTIES))
#define MM_SIGNAL_THRESHOLD_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SIGNAL_THRESHOLD_PROPERTIES, MMSignalThresholdPropertiesClass))

typedef struct _MMSignalThresholdProperties MMSignalThresholdProperties;
typedef struct _MMSignalThresholdPropertiesClass MMSignalThresholdPropertiesClass;
typedef struct _MMSignalThresholdPropertiesPrivate MMSignalThresholdPropertiesPrivate;

/**
 * MMSignalThresholdProperties:
 *
 * The #MMSignalThresholdProperties structure contains private data and should
 * only be accessed using the provided API.
 */
struct _MMSignalThresholdProperties {
    /*< private >*/
    GObject parent;
    MMSignalThresholdPropertiesPrivate *priv;
};

struct _MMSignalThresholdPropertiesClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_signal_threshold_properties_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMSignalThresholdProperties, g_object_unref)

MMSignalThresholdProperties *mm_signal_threshold_properties_new (void);

void     mm_signal_threshold_properties_set_rssi       (MMSignalThresholdProperties *self,
                                                        guint                        rssi_threshold);
void     mm_signal_threshold_properties_set_error_rate (MMSignalThresholdProperties *self,
                                                        gboolean                     error_rate_threshold);
guint    mm_signal_threshold_properties_get_rssi       (MMSignalThresholdProperties *self);
gboolean mm_signal_threshold_properties_get_error_rate (MMSignalThresholdProperties *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMSignalThresholdProperties *mm_signal_threshold_properties_new_from_string     (const gchar                  *str,
                                                                                 GError                      **error);
MMSignalThresholdProperties *mm_signal_threshold_properties_new_from_dictionary (GVariant                     *dictionary,
                                                                                 GError                      **error);
GVariant                    *mm_signal_threshold_properties_get_dictionary      (MMSignalThresholdProperties  *self);

#endif

G_END_DECLS

#endif /* MM_SIGNAL_THRESHOLD_PROPERTIES_H */
