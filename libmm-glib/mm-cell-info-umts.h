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

#ifndef MM_CELL_INFO_UMTS_H
#define MM_CELL_INFO_UMTS_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

#include "mm-cell-info.h"

G_BEGIN_DECLS

#define MM_TYPE_CELL_INFO_UMTS            (mm_cell_info_umts_get_type ())
#define MM_CELL_INFO_UMTS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CELL_INFO_UMTS, MMCellInfoUmts))
#define MM_CELL_INFO_UMTS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CELL_INFO_UMTS, MMCellInfoUmtsClass))
#define MM_IS_CELL_INFO_UMTS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CELL_INFO_UMTS))
#define MM_IS_CELL_INFO_UMTS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CELL_INFO_UMTS))
#define MM_CELL_INFO_UMTS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CELL_INFO_UMTS, MMCellInfoUmtsClass))

typedef struct _MMCellInfoUmts MMCellInfoUmts;
typedef struct _MMCellInfoUmtsClass MMCellInfoUmtsClass;
typedef struct _MMCellInfoUmtsPrivate MMCellInfoUmtsPrivate;

/**
 * MMCellInfoUmts:
 *
 * The #MMCellInfoUmts structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMCellInfoUmts {
    /*< private >*/
    MMCellInfo             parent;
    MMCellInfoUmtsPrivate *priv;
};

struct _MMCellInfoUmtsClass {
    /*< private >*/
    MMCellInfoClass parent;
};

GType mm_cell_info_umts_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCellInfoUmts, g_object_unref)

const gchar *mm_cell_info_umts_get_operator_id      (MMCellInfoUmts *self);
const gchar *mm_cell_info_umts_get_lac              (MMCellInfoUmts *self);
const gchar *mm_cell_info_umts_get_ci               (MMCellInfoUmts *self);
guint        mm_cell_info_umts_get_frequency_fdd_ul (MMCellInfoUmts *self);
guint        mm_cell_info_umts_get_frequency_fdd_dl (MMCellInfoUmts *self);
guint        mm_cell_info_umts_get_frequency_tdd    (MMCellInfoUmts *self);
guint        mm_cell_info_umts_get_uarfcn           (MMCellInfoUmts *self);
guint        mm_cell_info_umts_get_psc              (MMCellInfoUmts *self);
gdouble      mm_cell_info_umts_get_rscp             (MMCellInfoUmts *self);
gdouble      mm_cell_info_umts_get_ecio             (MMCellInfoUmts *self);
guint        mm_cell_info_umts_get_path_loss        (MMCellInfoUmts *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

void mm_cell_info_umts_set_operator_id      (MMCellInfoUmts *self,
                                             const gchar    *operator_id);
void mm_cell_info_umts_set_lac              (MMCellInfoUmts *self,
                                             const gchar    *lac);
void mm_cell_info_umts_set_ci               (MMCellInfoUmts *self,
                                             const gchar    *ci);
void mm_cell_info_umts_set_frequency_fdd_ul (MMCellInfoUmts *self,
                                             guint           frequency_fdd_ul);
void mm_cell_info_umts_set_frequency_fdd_dl (MMCellInfoUmts *self,
                                             guint           frequency_fdd_ul);
void mm_cell_info_umts_set_frequency_tdd    (MMCellInfoUmts *self,
                                             guint           frequency_tdd);
void mm_cell_info_umts_set_uarfcn           (MMCellInfoUmts *self,
                                             guint           uarfcn);
void mm_cell_info_umts_set_psc              (MMCellInfoUmts *self,
                                             guint           psc);
void mm_cell_info_umts_set_rscp             (MMCellInfoUmts *self,
                                             gdouble         rscp);
void mm_cell_info_umts_set_ecio             (MMCellInfoUmts *self,
                                             gdouble         ecio);
void mm_cell_info_umts_set_path_loss        (MMCellInfoUmts *self,
                                             guint           path_loss);

MMCellInfo *mm_cell_info_umts_new_from_dictionary (GVariantDict *dict);

#endif

G_END_DECLS

#endif /* MM_CELL_INFO_UMTS_H */
