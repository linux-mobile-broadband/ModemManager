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
 * Copyright 2018 Google LLC.
 */

#ifndef MM_PCO_H
#define MM_PCO_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_PCO            (mm_pco_get_type ())
#define MM_PCO(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PCO, MMPco))
#define MM_PCO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PCO, MMPcoClass))
#define MM_IS_PCO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PCO))
#define MM_IS_PCO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PCO))
#define MM_PCO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PCO, MMPcoClass))

typedef struct _MMPco MMPco;
typedef struct _MMPcoClass MMPcoClass;
typedef struct _MMPcoPrivate MMPcoPrivate;

/**
 * MMPco:
 *
 * The #MMPco structure contains private data and should only be accessed
 * using the provided API.
 */
struct _MMPco {
    /*< private >*/
    GObject parent;
    MMPcoPrivate *priv;
};

struct _MMPcoClass {
    /*< private >*/
    GObjectClass parent;
};

GType mm_pco_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPco, g_object_unref)

guint32       mm_pco_get_session_id (MMPco *self);
gboolean      mm_pco_is_complete    (MMPco *self);
const guint8 *mm_pco_get_data       (MMPco *self,
                                     gsize *data_size);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

MMPco    *mm_pco_new            (void);
MMPco    *mm_pco_from_variant   (GVariant *variant,
                                 GError **error);
GVariant *mm_pco_to_variant     (MMPco *self);
void      mm_pco_set_session_id (MMPco *self,
                                 guint32 session_id);
void      mm_pco_set_complete   (MMPco *self,
                                 gboolean is_complete);
void      mm_pco_set_data       (MMPco *self,
                                 const guint8 *data,
                                 gsize data_size);

GList    *mm_pco_list_add       (GList *pco_list,
                                 MMPco *pco);
#endif

G_END_DECLS

#endif /* MM_PCO_H */
