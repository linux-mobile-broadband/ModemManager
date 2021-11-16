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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 */

#ifndef MM_CALL_PROPERTIES_H
#define MM_CALL_PROPERTIES_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_CALL_PROPERTIES            (mm_call_properties_get_type ())
#define MM_CALL_PROPERTIES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CALL_PROPERTIES, MMCallProperties))
#define MM_CALL_PROPERTIES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CALL_PROPERTIES, MMCallPropertiesClass))
#define MM_IS_CALL_PROPERTIES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CALL_PROPERTIES))
#define MM_IS_CALL_PROPERTIES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CALL_PROPERTIES))
#define MM_CALL_PROPERTIES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CALL_PROPERTIES, MMCallPropertiesClass))

typedef struct _MMCallProperties MMCallProperties;
typedef struct _MMCallPropertiesClass MMCallPropertiesClass;
typedef struct _MMCallPropertiesPrivate MMCallPropertiesPrivate;

/**
 * MMCallProperties:
 *
 * The #MMCallProperties structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMCallProperties {
    /*< private >*/
    GObject parent;
    MMCallPropertiesPrivate *priv;
};

struct _MMCallPropertiesClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_call_properties_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCallProperties, g_object_unref)

MMCallProperties *mm_call_properties_new        (void);
void              mm_call_properties_set_number (MMCallProperties *self,
                                                 const gchar *text);
const gchar      *mm_call_properties_get_number (MMCallProperties *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMCallProperties *mm_call_properties_new_from_string     (const gchar *str,
                                                          GError **error);
MMCallProperties *mm_call_properties_new_from_dictionary (GVariant *dictionary,
                                                          GError **error);
GVariant         *mm_call_properties_get_dictionary      (MMCallProperties *self);

#endif

G_END_DECLS

#endif /* MM_CALL_PROPERTIES_H */
