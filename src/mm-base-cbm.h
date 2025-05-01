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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 */

#ifndef MM_BASE_CBM_H
#define MM_BASE_CBM_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-cbm-part.h"

/*****************************************************************************/

#define MM_TYPE_BASE_CBM            (mm_base_cbm_get_type ())
#define MM_BASE_CBM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BASE_CBM, MMBaseCbm))
#define MM_BASE_CBM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BASE_CBM, MMBaseCbmClass))
#define MM_IS_BASE_CBM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BASE_CBM))
#define MM_IS_BASE_CBM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BASE_CBM))
#define MM_BASE_CBM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BASE_CBM, MMBaseCbmClass))

typedef struct _MMBaseCbm MMBaseCbm;
typedef struct _MMBaseCbmClass MMBaseCbmClass;
typedef struct _MMBaseCbmPrivate MMBaseCbmPrivate;

#define MM_BASE_CBM_PATH       "cbm-path"
#define MM_BASE_CBM_CONNECTION "cbm-connection"
#define MM_BASE_CBM_MAX_PARTS  "cbm-max-parts"
#define MM_BASE_CBM_SERIAL     "cbm-serial"

struct _MMBaseCbm {
    MmGdbusCbmSkeleton parent;
    MMBaseCbmPrivate *priv;
};

struct _MMBaseCbmClass {
    MmGdbusCbmSkeletonClass parent;
};

GType mm_base_cbm_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBaseCbm, g_object_unref)

MMBaseCbm *mm_base_cbm_new       (GObject   *bind_to);
gboolean   mm_base_cbm_take_part (MMBaseCbm *self,
                                  MMCbmPart *part,
                                  GError **error);
MMBaseCbm *mm_base_cbm_new_with_part (GObject *bind_to,
                                      MMCbmState state,
                                      guint max_parts,
                                      MMCbmPart *first_part,
                                      GError **error);

void         mm_base_cbm_export         (MMBaseCbm *self);
void         mm_base_cbm_unexport       (MMBaseCbm *self);
const gchar *mm_base_cbm_get_path       (MMBaseCbm *self);

gboolean     mm_base_cbm_has_part_num   (MMBaseCbm *self,
                                         guint      part_num);
GList       *mm_base_cbm_get_parts      (MMBaseCbm *self);
guint16      mm_base_cbm_get_serial     (MMBaseCbm *self);
guint16      mm_base_cbm_get_channel    (MMBaseCbm *self);

gboolean     mm_base_cbm_is_complete    (MMBaseCbm *self);

#endif /* MM_BASE_CBM_H */
