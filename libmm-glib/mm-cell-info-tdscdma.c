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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "mm-helpers.h"
#include "mm-cell-info-tdscdma.h"

/**
 * SECTION: mm-cell-info-tdscdma
 * @title: MMCellInfoTdscdma
 * @short_description: Helper object to report TDSCDMA cell info
 *
 * The #MMCellInfoTdscdma is an object used to report TDSCDMA cell
 * information.
 *
 * The object inherits from the generic #MMCellInfo.
 */

G_DEFINE_TYPE (MMCellInfoTdscdma, mm_cell_info_tdscdma, MM_TYPE_CELL_INFO)

#define PROPERTY_OPERATOR_ID       "operator-id"
#define PROPERTY_LAC               "lac"
#define PROPERTY_CI                "ci"
#define PROPERTY_UARFCN            "uarfcn"
#define PROPERTY_CELL_PARAMETER_ID "cell-parameter-id"
#define PROPERTY_TIMING_ADVANCE    "timing-advance"
#define PROPERTY_RSCP              "rscp"
#define PROPERTY_PATH_LOSS         "path-loss"

struct _MMCellInfoTdscdmaPrivate {
    gchar   *operator_id;
    gchar   *lac;
    gchar   *ci;
    guint    uarfcn;
    guint    cell_parameter_id;
    guint    timing_advance;
    gdouble  rscp;
    guint    path_loss;
};

/*****************************************************************************/

/**
 * mm_cell_info_tdscdma_get_operator_id:
 * @self: a #MMCellInfoTdscdma.
 *
 * Get the PLMN MCC/MNC.
 *
 * Returns: (transfer none): the MCCMNC, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_tdscdma_get_operator_id (MMCellInfoTdscdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_TDSCDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->operator_id);
}

/**
 * mm_cell_info_tdscdma_set_operator_id: (skip)
 */
void
mm_cell_info_tdscdma_set_operator_id (MMCellInfoTdscdma *self,
                                      const gchar       *operator_id)
{
    g_free (self->priv->operator_id);
    self->priv->operator_id = g_strdup (operator_id);
}

/**
 * mm_cell_info_tdscdma_get_lac:
 * @self: a #MMCellInfoTdscdma.
 *
 * Get the two-byte Location Area Code of the base station.
 *
 * Encoded in upper-case hexadecimal format without leading zeros,
 * as specified in 3GPP TS 27.007.
 *
 * Returns: (transfer none): the MCCMNC, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_tdscdma_get_lac (MMCellInfoTdscdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_TDSCDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->lac);
}

/**
 * mm_cell_info_tdscdma_set_lac: (skip)
 */
void
mm_cell_info_tdscdma_set_lac (MMCellInfoTdscdma *self,
                              const gchar       *lac)
{
    g_free (self->priv->lac);
    self->priv->lac = g_strdup (lac);
}

/**
 * mm_cell_info_tdscdma_get_ci:
 * @self: a #MMCellInfoTdscdma.
 *
 * Get the two- or four-byte Cell Identifier.
 *
 * Encoded in upper-case hexadecimal format without leading zeros,
 * as specified in 3GPP TS 27.007.
 *
 * Returns: (transfer none): the MCCMNC, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_tdscdma_get_ci (MMCellInfoTdscdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_TDSCDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->ci);
}

/**
 * mm_cell_info_tdscdma_set_ci: (skip)
 */
void
mm_cell_info_tdscdma_set_ci (MMCellInfoTdscdma *self,
                             const gchar       *ci)
{
    g_free (self->priv->ci);
    self->priv->ci = g_strdup (ci);
}

/**
 * mm_cell_info_tdscdma_get_uarfcn:
 * @self: a #MMCellInfoTdscdma.
 *
 * Get the UTRA absolute RF channel number.
 *
 * Returns: the UARFCN, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_tdscdma_get_uarfcn (MMCellInfoTdscdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_TDSCDMA (self), G_MAXUINT);

    return self->priv->uarfcn;
}

/**
 * mm_cell_info_tdscdma_set_uarfcn: (skip)
 */
void
mm_cell_info_tdscdma_set_uarfcn (MMCellInfoTdscdma *self,
                                 guint              uarfcn)
{
    self->priv->uarfcn = uarfcn;
}

/**
 * mm_cell_info_tdscdma_get_cell_parameter_id:
 * @self: a #MMCellInfoTdscdma.
 *
 * Get the cell parameter id.
 *
 * Returns: the cell parameter id, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_tdscdma_get_cell_parameter_id (MMCellInfoTdscdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_TDSCDMA (self), G_MAXUINT);

    return self->priv->cell_parameter_id;
}

/**
 * mm_cell_info_tdscdma_set_cell_parameter_id: (skip)
 */
void
mm_cell_info_tdscdma_set_cell_parameter_id (MMCellInfoTdscdma *self,
                                            guint              cell_parameter_id)
{
    self->priv->cell_parameter_id = cell_parameter_id;
}

/**
 * mm_cell_info_tdscdma_get_timing_advance:
 * @self: a #MMCellInfoTdscdma.
 *
 * Get the measured delay (in bit periods) of an access burst transmission
 * on the RACH or PRACH to the expected signal from a mobile station at zero
 * distance under static channel conditions.
 *
 * Returns: the timing advance, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_tdscdma_get_timing_advance (MMCellInfoTdscdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_TDSCDMA (self), G_MAXUINT);

    return self->priv->timing_advance;
}

/**
 * mm_cell_info_tdscdma_set_timing_advance: (skip)
 */
void
mm_cell_info_tdscdma_set_timing_advance (MMCellInfoTdscdma *self,
                                         guint              timing_advance)
{
    self->priv->timing_advance = timing_advance;
}

/**
 * mm_cell_info_tdscdma_get_rscp:
 * @self: a #MMCellInfoTdscdma.
 *
 * Get the received signal code power.
 *
 * Returns: the RSCP, or -%G_MAXDOUBLE if not available.
 *
 * Since: 1.20
 */
gdouble
mm_cell_info_tdscdma_get_rscp (MMCellInfoTdscdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_TDSCDMA (self), -G_MAXDOUBLE);

    return self->priv->rscp;
}

/**
 * mm_cell_info_tdscdma_set_rscp: (skip)
 */
void
mm_cell_info_tdscdma_set_rscp (MMCellInfoTdscdma *self,
                               gdouble            rscp)
{
    self->priv->rscp = rscp;
}

/**
 * mm_cell_info_tdscdma_get_path_loss:
 * @self: a #MMCellInfoTdscdma.
 *
 * Get the path loss of the cell.
 *
 * Returns: the path loss, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_tdscdma_get_path_loss (MMCellInfoTdscdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_TDSCDMA (self), G_MAXUINT);

    return self->priv->path_loss;
}

/**
 * mm_cell_info_tdscdma_set_path_loss: (skip)
 */
void
mm_cell_info_tdscdma_set_path_loss (MMCellInfoTdscdma *self,
                                    guint              path_loss)
{
    self->priv->path_loss = path_loss;
}

/*****************************************************************************/

static GString *
build_string (MMCellInfo *_self)
{
    MMCellInfoTdscdma *self = MM_CELL_INFO_TDSCDMA (_self);
    GString           *str;

    str = g_string_new (NULL);

    MM_CELL_INFO_BUILD_STRING_APPEND ("operator id",       "%s",  operator_id,       NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("lac",               "%s",  lac,               NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("ci",                "%s",  ci,                NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("uarfcn",            "%u",  uarfcn,            G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("cell parameter id", "%u",  cell_parameter_id, G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("timing advance",    "%u",  timing_advance,    G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("rscp",              "%lf", rscp,             -G_MAXDOUBLE);
    MM_CELL_INFO_BUILD_STRING_APPEND ("path loss",         "%u",  path_loss,         G_MAXUINT);

    return str;
}

/*****************************************************************************/

/**
 * mm_cell_info_tdscdma_get_dictionary: (skip)
 */
static GVariantDict *
get_dictionary (MMCellInfo *_self)
{
    MMCellInfoTdscdma *self = MM_CELL_INFO_TDSCDMA (_self);
    GVariantDict   *dict;

    dict = g_variant_dict_new (NULL);

    MM_CELL_INFO_GET_DICTIONARY_INSERT (OPERATOR_ID,       operator_id,       string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (LAC,               lac,               string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (CI,                ci,                string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (UARFCN,            uarfcn,            uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (CELL_PARAMETER_ID, cell_parameter_id, uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (TIMING_ADVANCE,    timing_advance,    uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (RSCP,              rscp,              double, -G_MAXDOUBLE);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (PATH_LOSS,         path_loss,         uint32,  G_MAXUINT);

    return dict;
}

/*****************************************************************************/

/**
 * mm_cell_info_tdscdma_new_from_dictionary: (skip)
 */
MMCellInfo *
mm_cell_info_tdscdma_new_from_dictionary (GVariantDict *dict)
{
    MMCellInfoTdscdma *self;

    self = MM_CELL_INFO_TDSCDMA (g_object_new (MM_TYPE_CELL_INFO_TDSCDMA, NULL));

    if (dict) {
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (tdscdma, OPERATOR_ID, operator_id);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (tdscdma, LAC,         lac);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (tdscdma, CI,          ci);

        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (tdscdma, UARFCN,            uarfcn,            UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (tdscdma, CELL_PARAMETER_ID, cell_parameter_id, UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (tdscdma, TIMING_ADVANCE,    timing_advance,    UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (tdscdma, RSCP,              rscp,              DOUBLE, double);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (tdscdma, PATH_LOSS,         path_loss,         UINT32, uint32);
    }

    return MM_CELL_INFO (self);
}

/*****************************************************************************/

static void
mm_cell_info_tdscdma_init (MMCellInfoTdscdma *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CELL_INFO_TDSCDMA, MMCellInfoTdscdmaPrivate);
    self->priv->uarfcn = G_MAXUINT;
    self->priv->cell_parameter_id = G_MAXUINT;
    self->priv->timing_advance = G_MAXUINT;
    self->priv->rscp = -G_MAXDOUBLE;
    self->priv->path_loss = G_MAXUINT;
}

static void
finalize (GObject *object)
{
    MMCellInfoTdscdma *self = MM_CELL_INFO_TDSCDMA (object);

    g_free (self->priv->operator_id);
    g_free (self->priv->lac);
    g_free (self->priv->ci);

    G_OBJECT_CLASS (mm_cell_info_tdscdma_parent_class)->finalize (object);
}

static void
mm_cell_info_tdscdma_class_init (MMCellInfoTdscdmaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMCellInfoClass *cell_info_class = MM_CELL_INFO_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCellInfoTdscdmaPrivate));

    object_class->finalize = finalize;
    cell_info_class->get_dictionary = get_dictionary;
    cell_info_class->build_string = build_string;

}
