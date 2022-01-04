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

#ifndef MM_CELL_INFO_NR5G_H
#define MM_CELL_INFO_NR5G_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

#include "mm-cell-info.h"

G_BEGIN_DECLS

#define MM_TYPE_CELL_INFO_NR5G            (mm_cell_info_nr5g_get_type ())
#define MM_CELL_INFO_NR5G(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CELL_INFO_NR5G, MMCellInfoNr5g))
#define MM_CELL_INFO_NR5G_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CELL_INFO_NR5G, MMCellInfoNr5gClass))
#define MM_IS_CELL_INFO_NR5G(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CELL_INFO_NR5G))
#define MM_IS_CELL_INFO_NR5G_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CELL_INFO_NR5G))
#define MM_CELL_INFO_NR5G_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CELL_INFO_NR5G, MMCellInfoNr5gClass))

typedef struct _MMCellInfoNr5g MMCellInfoNr5g;
typedef struct _MMCellInfoNr5gClass MMCellInfoNr5gClass;
typedef struct _MMCellInfoNr5gPrivate MMCellInfoNr5gPrivate;

/**
 * MMCellInfoNr5g:
 *
 * The #MMCellInfoNr5g structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMCellInfoNr5g {
    /*< private >*/
    MMCellInfo             parent;
    MMCellInfoNr5gPrivate *priv;
};

struct _MMCellInfoNr5gClass {
    /*< private >*/
    MMCellInfoClass parent;
};

GType mm_cell_info_nr5g_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCellInfoNr5g, g_object_unref)

const gchar *mm_cell_info_nr5g_get_operator_id    (MMCellInfoNr5g *self);
const gchar *mm_cell_info_nr5g_get_tac            (MMCellInfoNr5g *self);
const gchar *mm_cell_info_nr5g_get_ci             (MMCellInfoNr5g *self);
const gchar *mm_cell_info_nr5g_get_physical_ci    (MMCellInfoNr5g *self);
guint        mm_cell_info_nr5g_get_nrarfcn        (MMCellInfoNr5g *self);
gdouble      mm_cell_info_nr5g_get_rsrp           (MMCellInfoNr5g *self);
gdouble      mm_cell_info_nr5g_get_rsrq           (MMCellInfoNr5g *self);
gdouble      mm_cell_info_nr5g_get_sinr           (MMCellInfoNr5g *self);
guint        mm_cell_info_nr5g_get_timing_advance (MMCellInfoNr5g *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

void mm_cell_info_nr5g_set_operator_id    (MMCellInfoNr5g *self,
                                           const gchar    *operator_id);
void mm_cell_info_nr5g_set_tac            (MMCellInfoNr5g *self,
                                           const gchar    *tac);
void mm_cell_info_nr5g_set_ci             (MMCellInfoNr5g *self,
                                           const gchar    *ci);
void mm_cell_info_nr5g_set_physical_ci    (MMCellInfoNr5g *self,
                                           const gchar    *ci);
void mm_cell_info_nr5g_set_nrarfcn        (MMCellInfoNr5g *self,
                                           guint           earfcn);
void mm_cell_info_nr5g_set_rsrp           (MMCellInfoNr5g *self,
                                           gdouble         rsrp);
void mm_cell_info_nr5g_set_rsrq           (MMCellInfoNr5g *self,
                                           gdouble         rsrq);
void mm_cell_info_nr5g_set_sinr           (MMCellInfoNr5g *self,
                                           gdouble         sinr);
void mm_cell_info_nr5g_set_timing_advance (MMCellInfoNr5g *self,
                                           guint           earfcn);

MMCellInfo *mm_cell_info_nr5g_new_from_dictionary (GVariantDict *dict);

#endif

G_END_DECLS

#endif /* MM_CELL_INFO_NR5G_H */
