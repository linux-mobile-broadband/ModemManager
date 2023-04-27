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

#include "mm-cell-info.h"
#include "mm-cell-info-cdma.h"
#include "mm-cell-info-gsm.h"
#include "mm-cell-info-umts.h"
#include "mm-cell-info-tdscdma.h"
#include "mm-cell-info-lte.h"
#include "mm-cell-info-nr5g.h"

#include "mm-enums-types.h"
#include "mm-flags-types.h"
#include "mm-errors-types.h"

/**
 * SECTION: mm-cell-info
 * @title: MMCellInfo
 * @short_description: Helper base object to report cell info
 *
 * The #MMCellInfo is a base object used to report cell information.
 *
 * This object is retrieved from the #MMModem object with
 * mm_modem_get_cell_info() or mm_modem_get_cell_info_sync().
 */

G_DEFINE_TYPE (MMCellInfo, mm_cell_info, G_TYPE_OBJECT)

#define PROPERTY_CELL_TYPE "cell-type"
#define PROPERTY_SERVING   "serving"

struct _MMCellInfoPrivate {
    MMCellType cell_type;
    gboolean   serving;
};

/*****************************************************************************/

static void
ensure_cell_type (MMCellInfo *self)
{
    if (self->priv->cell_type != MM_CELL_TYPE_UNKNOWN)
        return;

    if (MM_IS_CELL_INFO_CDMA (self))
        self->priv->cell_type = MM_CELL_TYPE_CDMA;
    else if (MM_IS_CELL_INFO_GSM (self))
        self->priv->cell_type = MM_CELL_TYPE_GSM;
    else if (MM_IS_CELL_INFO_UMTS (self))
        self->priv->cell_type = MM_CELL_TYPE_UMTS;
    else if (MM_IS_CELL_INFO_TDSCDMA (self))
        self->priv->cell_type = MM_CELL_TYPE_TDSCDMA;
    else if (MM_IS_CELL_INFO_LTE (self))
        self->priv->cell_type = MM_CELL_TYPE_LTE;
    else if (MM_IS_CELL_INFO_NR5G (self))
        self->priv->cell_type = MM_CELL_TYPE_5GNR;
}

/**
 * mm_cell_info_get_cell_type:
 * @self: a #MMCellInfo.
 *
 * Get the type of cell.
 *
 * Returns: a #MMCellType.
 *
 * Since: 1.20
 */
MMCellType
mm_cell_info_get_cell_type (MMCellInfo *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO (self), MM_CELL_TYPE_UNKNOWN);

    ensure_cell_type (self);

    return self->priv->cell_type;
}

/**
 * mm_cell_info_get_serving:
 * @self: a #MMCellInfo.
 *
 * Get whether the cell is a serving cell or a neighboring cell.a
 *
 * Returns: %TRUE if the cell is a serving cell, %FALSE otherwise.
 *
 * Since: 1.20
 */
gboolean
mm_cell_info_get_serving (MMCellInfo *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO (self), FALSE);

    return self->priv->serving;
}

/**
 * mm_cell_info_set_serving: (skip)
 */
void
mm_cell_info_set_serving (MMCellInfo *self,
                                gboolean         serving)
{
    g_return_if_fail (MM_IS_CELL_INFO (self));

    self->priv->serving = serving;
}

/*****************************************************************************/

/**
 * mm_cell_info_get_dictionary: (skip)
 */
GVariant *
mm_cell_info_get_dictionary (MMCellInfo *self)
{
    g_autoptr(GVariantDict) dict = NULL;

    dict = MM_CELL_INFO_GET_CLASS (self)->get_dictionary (self);
    g_assert (dict);

    g_variant_dict_insert_value (dict, PROPERTY_SERVING,   g_variant_new_boolean (self->priv->serving));
    g_variant_dict_insert_value (dict, PROPERTY_CELL_TYPE, g_variant_new_uint32 (mm_cell_info_get_cell_type (self)));

    return g_variant_ref_sink (g_variant_dict_end (dict));
}

/*****************************************************************************/

/**
 * mm_cell_info_new_from_dictionary: (skip)
 */
MMCellInfo *
mm_cell_info_new_from_dictionary (GVariant  *dictionary,
                                  GError   **error)
{
    g_autoptr(MMCellInfo)    self = NULL;
    g_autoptr(GVariantDict)  dict = NULL;
    GVariant                *aux;

    dict = g_variant_dict_new (dictionary);

    aux = g_variant_dict_lookup_value (dict, PROPERTY_CELL_TYPE, G_VARIANT_TYPE_UINT32);
    if (!aux) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "missing '" PROPERTY_CELL_TYPE "' key in cell info");
        return NULL;
    }
    switch (g_variant_get_uint32 (aux)) {
        case MM_CELL_TYPE_CDMA:
            self = mm_cell_info_cdma_new_from_dictionary (dict);
            break;
        case MM_CELL_TYPE_GSM:
            self = mm_cell_info_gsm_new_from_dictionary (dict);
            break;
        case MM_CELL_TYPE_UMTS:
            self = mm_cell_info_umts_new_from_dictionary (dict);
            break;
        case MM_CELL_TYPE_TDSCDMA:
            self = mm_cell_info_tdscdma_new_from_dictionary (dict);
            break;
        case MM_CELL_TYPE_LTE:
            self = mm_cell_info_lte_new_from_dictionary (dict);
            break;
        case MM_CELL_TYPE_5GNR:
            self = mm_cell_info_nr5g_new_from_dictionary (dict);
            break;
        default:
            break;
    }
    g_variant_unref (aux);

    if (!self) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "unknown '" PROPERTY_CELL_TYPE "' key value in cell info");
        return NULL;
    }

    aux = g_variant_dict_lookup_value (dict, PROPERTY_SERVING, G_VARIANT_TYPE_BOOLEAN);
    if (aux) {
        mm_cell_info_set_serving (self, g_variant_get_boolean (aux));
        g_variant_unref (aux);
    }

    return g_steal_pointer (&self);
}

/*****************************************************************************/

/**
 * mm_cell_info_build_string: (skip)
 */
gchar *
mm_cell_info_build_string (MMCellInfo *self)
{
    GString *str;
    GString *substr;

    substr = MM_CELL_INFO_GET_CLASS (self)->build_string (self);
    g_assert (substr);

    ensure_cell_type (self);

    str = g_string_new (NULL);
    g_string_append_printf (str, "cell type: %s, serving: %s",
                            mm_cell_type_get_string (self->priv->cell_type),
                            self->priv->serving ? "yes" : "no");
    g_string_append_len (str, substr->str, (gssize)substr->len);

    g_string_free (substr, TRUE);

    return g_string_free (str, FALSE);
}

/*****************************************************************************/

static void
mm_cell_info_init (MMCellInfo *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CELL_INFO, MMCellInfoPrivate);
    self->priv->cell_type = MM_CELL_TYPE_UNKNOWN;
}

static void
mm_cell_info_class_init (MMCellInfoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCellInfoPrivate));
}
