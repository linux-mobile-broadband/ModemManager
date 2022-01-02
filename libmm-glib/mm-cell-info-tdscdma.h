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
 * Copyright (C) 2022 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_CELL_INFO_TDSCDMA_H
#define MM_CELL_INFO_TDSCDMA_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

#include "mm-cell-info.h"

G_BEGIN_DECLS

#define MM_TYPE_CELL_INFO_TDSCDMA            (mm_cell_info_tdscdma_get_type ())
#define MM_CELL_INFO_TDSCDMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CELL_INFO_TDSCDMA, MMCellInfoTdscdma))
#define MM_CELL_INFO_TDSCDMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CELL_INFO_TDSCDMA, MMCellInfoTdscdmaClass))
#define MM_IS_CELL_INFO_TDSCDMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CELL_INFO_TDSCDMA))
#define MM_IS_CELL_INFO_TDSCDMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CELL_INFO_TDSCDMA))
#define MM_CELL_INFO_TDSCDMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CELL_INFO_TDSCDMA, MMCellInfoTdscdmaClass))

typedef struct _MMCellInfoTdscdma MMCellInfoTdscdma;
typedef struct _MMCellInfoTdscdmaClass MMCellInfoTdscdmaClass;
typedef struct _MMCellInfoTdscdmaPrivate MMCellInfoTdscdmaPrivate;

/**
 * MMCellInfoTdscdma:
 *
 * The #MMCellInfoTdscdma structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMCellInfoTdscdma {
    /*< private >*/
    MMCellInfo                parent;
    MMCellInfoTdscdmaPrivate *priv;
};

struct _MMCellInfoTdscdmaClass {
    /*< private >*/
    MMCellInfoClass parent;
};

GType mm_cell_info_tdscdma_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCellInfoTdscdma, g_object_unref)

const gchar *mm_cell_info_tdscdma_get_operator_id       (MMCellInfoTdscdma *self);
const gchar *mm_cell_info_tdscdma_get_lac               (MMCellInfoTdscdma *self);
const gchar *mm_cell_info_tdscdma_get_ci                (MMCellInfoTdscdma *self);
guint        mm_cell_info_tdscdma_get_uarfcn            (MMCellInfoTdscdma *self);
guint        mm_cell_info_tdscdma_get_cell_parameter_id (MMCellInfoTdscdma *self);
guint        mm_cell_info_tdscdma_get_timing_advance    (MMCellInfoTdscdma *self);
gdouble      mm_cell_info_tdscdma_get_rscp              (MMCellInfoTdscdma *self);
guint        mm_cell_info_tdscdma_get_path_loss         (MMCellInfoTdscdma *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

void mm_cell_info_tdscdma_set_operator_id       (MMCellInfoTdscdma *self,
                                                 const gchar       *operator_id);
void mm_cell_info_tdscdma_set_lac               (MMCellInfoTdscdma *self,
                                                 const gchar       *lac);
void mm_cell_info_tdscdma_set_ci                (MMCellInfoTdscdma *self,
                                                 const gchar       *ci);
void mm_cell_info_tdscdma_set_uarfcn            (MMCellInfoTdscdma *self,
                                                 guint              uarfcn);
void mm_cell_info_tdscdma_set_cell_parameter_id (MMCellInfoTdscdma *self,
                                                 guint              cell_parameter_id);
void mm_cell_info_tdscdma_set_timing_advance    (MMCellInfoTdscdma *self,
                                                 guint              timing_advance);
void mm_cell_info_tdscdma_set_rscp              (MMCellInfoTdscdma *self,
                                                 gdouble            rscp);
void mm_cell_info_tdscdma_set_path_loss         (MMCellInfoTdscdma *self,
                                                 guint              path_loss);

MMCellInfo *mm_cell_info_tdscdma_new_from_dictionary (GVariantDict *dict);

#endif

G_END_DECLS

#endif /* MM_CELL_INFO_TDSCDMA_H */
