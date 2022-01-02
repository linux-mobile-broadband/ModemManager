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

#ifndef MM_CELL_INFO_H
#define MM_CELL_INFO_H

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include <ModemManager.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_CELL_INFO            (mm_cell_info_get_type ())
#define MM_CELL_INFO(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_CELL_INFO, MMCellInfo))
#define MM_CELL_INFO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_CELL_INFO, MMCellInfoClass))
#define MM_IS_CELL_INFO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_CELL_INFO))
#define MM_IS_CELL_INFO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_CELL_INFO))
#define MM_CELL_INFO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_CELL_INFO, MMCellInfoClass))

typedef struct _MMCellInfo MMCellInfo;
typedef struct _MMCellInfoClass MMCellInfoClass;
typedef struct _MMCellInfoPrivate MMCellInfoPrivate;

/**
 * MMCellInfo:
 *
 * The #MMCellInfo structure contains private data and should only be
 * accessed using the provided API.
 */
struct _MMCellInfo {
    /*< private >*/
    GObject            parent;
    MMCellInfoPrivate *priv;
};

struct _MMCellInfoClass {
    /*< private >*/
    GObjectClass parent;

    GVariantDict * (* get_dictionary) (MMCellInfo    *self);
    GString      * (* build_string)   (MMCellInfo    *self);

    /* class padding */
    gpointer padding [5];
};

GType mm_cell_info_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMCellInfo, g_object_unref)

MMCellType mm_cell_info_get_cell_type (MMCellInfo *self);
gboolean   mm_cell_info_get_serving   (MMCellInfo *self);

/*****************************************************************************/
/* ModemManager/libmm-glib/mmcli specific methods */

#if defined (_LIBMM_INSIDE_MM) ||    \
    defined (_LIBMM_INSIDE_MMCLI) || \
    defined (LIBMM_GLIB_COMPILATION)

void        mm_cell_info_set_serving         (MMCellInfo  *self,
                                              gboolean     serving);
GVariant   *mm_cell_info_get_dictionary      (MMCellInfo  *self);
MMCellInfo *mm_cell_info_new_from_dictionary (GVariant    *dictionary,
                                              GError     **error);
gchar      *mm_cell_info_build_string        (MMCellInfo  *self);

/* helpers to implement methods */

#define MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET(celltype,NAME,name) do {               \
        GVariant *aux;                                                                     \
                                                                                           \
        aux = g_variant_dict_lookup_value (dict, PROPERTY_##NAME, G_VARIANT_TYPE_STRING);  \
        if (aux) {                                                                         \
            mm_cell_info_##celltype##_set_##name (self, g_variant_get_string (aux, NULL)); \
            g_variant_unref (aux);                                                         \
        }                                                                                  \
    } while (0)

#define MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET(celltype,NAME,name,NUMTYPE,numtype) do {    \
        GVariant *aux;                                                                       \
                                                                                             \
        aux = g_variant_dict_lookup_value (dict, PROPERTY_##NAME, G_VARIANT_TYPE_##NUMTYPE); \
        if (aux) {                                                                           \
            mm_cell_info_##celltype##_set_##name (self, g_variant_get_##numtype (aux));      \
            g_variant_unref (aux);                                                           \
        }                                                                                    \
    } while (0)

#define MM_CELL_INFO_GET_DICTIONARY_INSERT(NAME,name,vartype,INVALID) do {                                   \
        if (self->priv->name != INVALID)                                                                     \
            g_variant_dict_insert_value (dict, PROPERTY_##NAME, g_variant_new_##vartype (self->priv->name)); \
    } while (0)

#define MM_CELL_INFO_BUILD_STRING_APPEND(STR,FORMAT,name,INVALID) do {            \
        if (self->priv->name != INVALID)                                          \
            g_string_append_printf (str, ", " STR ": " FORMAT, self->priv->name); \
    } while (0)

#endif

G_END_DECLS

#endif /* MM_CELL_INFO_H */
