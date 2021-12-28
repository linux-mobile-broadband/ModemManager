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

#ifndef MM_CELL_INFO_CDMA_H
#define MM_CELL_INFO_CDMA_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

#include "mm-cell-info.h"

G_BEGIN_DECLS

#define MM_TYPE_CELL_INFO_CDMA            (mm_cell_info_cdma_get_type ())
#define MM_CELL_INFO_CDMA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CELL_INFO_CDMA, MMCellInfoCdma))
#define MM_CELL_INFO_CDMA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CELL_INFO_CDMA, MMCellInfoCdmaClass))
#define MM_IS_CELL_INFO_CDMA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CELL_INFO_CDMA))
#define MM_IS_CELL_INFO_CDMA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CELL_INFO_CDMA))
#define MM_CELL_INFO_CDMA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CELL_INFO_CDMA, MMCellInfoCdmaClass))

typedef struct _MMCellInfoCdma MMCellInfoCdma;
typedef struct _MMCellInfoCdmaClass MMCellInfoCdmaClass;
typedef struct _MMCellInfoCdmaPrivate MMCellInfoCdmaPrivate;

/**
 * MMCellInfoCdma:
 *
 * The #MMCellInfoCdma structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMCellInfoCdma {
    /*< private >*/
    MMCellInfo             parent;
    MMCellInfoCdmaPrivate *priv;
};

struct _MMCellInfoCdmaClass {
    /*< private >*/
    MMCellInfoClass parent;
};

GType mm_cell_info_cdma_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCellInfoCdma, g_object_unref)

const gchar *mm_cell_info_cdma_get_nid             (MMCellInfoCdma *self);
const gchar *mm_cell_info_cdma_get_sid             (MMCellInfoCdma *self);
const gchar *mm_cell_info_cdma_get_base_station_id (MMCellInfoCdma *self);
const gchar *mm_cell_info_cdma_get_ref_pn          (MMCellInfoCdma *self);
guint        mm_cell_info_cdma_get_pilot_strength  (MMCellInfoCdma *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

void mm_cell_info_cdma_set_nid             (MMCellInfoCdma *self,
                                            const gchar    *nid);
void mm_cell_info_cdma_set_sid             (MMCellInfoCdma *self,
                                            const gchar    *sid);
void mm_cell_info_cdma_set_base_station_id (MMCellInfoCdma *self,
                                            const gchar    *base_station_id);
void mm_cell_info_cdma_set_ref_pn          (MMCellInfoCdma *self,
                                            const gchar    *ref_pn);
void mm_cell_info_cdma_set_pilot_strength  (MMCellInfoCdma *self,
                                            guint           pilot_strength);

MMCellInfo *mm_cell_info_cdma_new_from_dictionary (GVariantDict *dict);

#endif

G_END_DECLS

#endif /* MM_CELL_INFO_CDMA_H */
