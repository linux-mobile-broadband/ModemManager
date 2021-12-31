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

#ifndef MM_CELL_INFO_GSM_H
#define MM_CELL_INFO_GSM_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

#include "mm-cell-info.h"

G_BEGIN_DECLS

#define MM_TYPE_CELL_INFO_GSM            (mm_cell_info_gsm_get_type ())
#define MM_CELL_INFO_GSM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CELL_INFO_GSM, MMCellInfoGsm))
#define MM_CELL_INFO_GSM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CELL_INFO_GSM, MMCellInfoGsmClass))
#define MM_IS_CELL_INFO_GSM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CELL_INFO_GSM))
#define MM_IS_CELL_INFO_GSM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CELL_INFO_GSM))
#define MM_CELL_INFO_GSM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CELL_INFO_GSM, MMCellInfoGsmClass))

typedef struct _MMCellInfoGsm MMCellInfoGsm;
typedef struct _MMCellInfoGsmClass MMCellInfoGsmClass;
typedef struct _MMCellInfoGsmPrivate MMCellInfoGsmPrivate;

/**
 * MMCellInfoGsm:
 *
 * The #MMCellInfoGsm structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMCellInfoGsm {
    /*< private >*/
    MMCellInfo            parent;
    MMCellInfoGsmPrivate *priv;
};

struct _MMCellInfoGsmClass {
    /*< private >*/
    MMCellInfoClass parent;
};

GType mm_cell_info_gsm_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCellInfoGsm, g_object_unref)

const gchar *mm_cell_info_gsm_get_operator_id     (MMCellInfoGsm *self);
const gchar *mm_cell_info_gsm_get_lac             (MMCellInfoGsm *self);
const gchar *mm_cell_info_gsm_get_ci              (MMCellInfoGsm *self);
guint        mm_cell_info_gsm_get_timing_advance  (MMCellInfoGsm *self);
guint        mm_cell_info_gsm_get_arfcn           (MMCellInfoGsm *self);
const gchar *mm_cell_info_gsm_get_base_station_id (MMCellInfoGsm *self);
guint        mm_cell_info_gsm_get_rx_level        (MMCellInfoGsm *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

void mm_cell_info_gsm_set_operator_id     (MMCellInfoGsm *self,
                                           const gchar   *operator_id);
void mm_cell_info_gsm_set_lac             (MMCellInfoGsm *self,
                                           const gchar   *lac);
void mm_cell_info_gsm_set_ci              (MMCellInfoGsm *self,
                                           const gchar   *ci);
void mm_cell_info_gsm_set_timing_advance  (MMCellInfoGsm *self,
                                           guint          timing_advance);
void mm_cell_info_gsm_set_arfcn           (MMCellInfoGsm *self,
                                           guint          arfcn);
void mm_cell_info_gsm_set_base_station_id (MMCellInfoGsm *self,
                                           const gchar   *base_station_id);
void mm_cell_info_gsm_set_rx_level        (MMCellInfoGsm *self,
                                           guint          rx_level);

MMCellInfo *mm_cell_info_gsm_new_from_dictionary (GVariantDict *dict);

#endif

G_END_DECLS

#endif /* MM_CELL_INFO_GSM_H */
