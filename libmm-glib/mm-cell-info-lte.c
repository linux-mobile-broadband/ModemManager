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

#include "mm-helpers.h"
#include "mm-cell-info-lte.h"

/**
 * SECTION: mm-cell-info-lte
 * @title: MMCellInfoLte
 * @short_description: Helper object to report LTE cell info
 *
 * The #MMCellInfoLte is an object used to report LTE cell
 * information.
 *
 * The object inherits from the generic #MMCellInfo.
 */

G_DEFINE_TYPE (MMCellInfoLte, mm_cell_info_lte, MM_TYPE_CELL_INFO)

#define PROPERTY_OPERATOR_ID        "operator-id"
#define PROPERTY_TAC                "tac"
#define PROPERTY_CI                 "ci"
#define PROPERTY_PHYSICAL_CI        "physical-ci"
#define PROPERTY_EARFCN             "earfcn"
#define PROPERTY_RSRP               "rsrp"
#define PROPERTY_RSRQ               "rsrq"
#define PROPERTY_TIMING_ADVANCE     "timing-advance"
#define PROPERTY_SERVING_CELL_TYPE  "serving-cell-type"
#define PROPERTY_BANDWIDTH          "bandwidth"


struct _MMCellInfoLtePrivate {
    gchar   *operator_id;
    gchar   *tac;
    gchar   *ci;
    gchar   *physical_ci;
    guint    earfcn;
    gdouble  rsrp;
    gdouble  rsrq;
    guint    timing_advance;
    guint    serving_cell_type;
    guint    bandwidth;
};

/*****************************************************************************/

/**
 * mm_cell_info_lte_get_operator_id:
 * @self: a #MMCellInfoLte.
 *
 * Get the PLMN MCC/MNC.
 *
 * Returns: (transfer none): the MCCMNC, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_lte_get_operator_id (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->operator_id);
}

/**
 * mm_cell_info_lte_set_operator_id: (skip)
 */
void
mm_cell_info_lte_set_operator_id (MMCellInfoLte *self,
                                  const gchar   *operator_id)
{
    g_free (self->priv->operator_id);
    self->priv->operator_id = g_strdup (operator_id);
}

/**
 * mm_cell_info_lte_get_tac:
 * @self: a #MMCellInfoLte.
 *
 * Get the two- or three- byte Tracking Area Code of the base station.
 *
 * Encoded in upper-case hexadecimal format without leading zeros,
 * as specified in 3GPP TS 27.007.
 *
 * Returns: (transfer none): the MCCMNC, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_lte_get_tac (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->tac);
}

/**
 * mm_cell_info_lte_set_tac: (skip)
 */
void
mm_cell_info_lte_set_tac (MMCellInfoLte *self,
                          const gchar   *tac)
{
    g_free (self->priv->tac);
    self->priv->tac = g_strdup (tac);
}

/**
 * mm_cell_info_lte_get_ci:
 * @self: a #MMCellInfoLte.
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
mm_cell_info_lte_get_ci (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->ci);
}

/**
 * mm_cell_info_lte_set_ci: (skip)
 */
void
mm_cell_info_lte_set_ci (MMCellInfoLte *self,
                         const gchar   *ci)
{
    g_free (self->priv->ci);
    self->priv->ci = g_strdup (ci);
}

/**
 * mm_cell_info_lte_get_physical_ci:
 * @self: a #MMCellInfoLte.
 *
 * Get the physical cell identifier.
 *
 * Encoded in upper-case hexadecimal format without leading zeros,
 * as specified in 3GPP TS 27.007.
 *
 * Returns: (transfer none): the MCCMNC, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_lte_get_physical_ci (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->physical_ci);
}

/**
 * mm_cell_info_lte_set_physical_ci: (skip)
 */
void
mm_cell_info_lte_set_physical_ci (MMCellInfoLte *self,
                                  const gchar   *physical_ci)
{
    g_free (self->priv->physical_ci);
    self->priv->physical_ci = g_strdup (physical_ci);
}

/**
 * mm_cell_info_lte_get_earfcn:
 * @self: a #MMCellInfoLte.
 *
 * Get the E-UTRA absolute RF channel number.
 *
 * Returns: the EARFCN, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_lte_get_earfcn (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), G_MAXUINT);

    return self->priv->earfcn;
}

/**
 * mm_cell_info_lte_set_earfcn: (skip)
 */
void
mm_cell_info_lte_set_earfcn (MMCellInfoLte *self,
                             guint          earfcn)
{
    self->priv->earfcn = earfcn;
}

/**
 * mm_cell_info_lte_get_rsrp:
 * @self: a #MMCellInfoLte.
 *
 * Get the average reference signal received power in dBm.
 *
 * Returns: the RSRP, or -%G_MAXDOUBLE if not available.
 *
 * Since: 1.20
 */
gdouble
mm_cell_info_lte_get_rsrp (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), -G_MAXDOUBLE);

    return self->priv->rsrp;
}

/**
 * mm_cell_info_lte_set_rsrp: (skip)
 */
void
mm_cell_info_lte_set_rsrp (MMCellInfoLte *self,
                           gdouble        rsrp)
{
    self->priv->rsrp = rsrp;
}

/**
 * mm_cell_info_lte_get_rsrq:
 * @self: a #MMCellInfoLte.
 *
 * Get the average reference signal received quality in dB.
 *
 * Returns: the RSRQ, or -%G_MAXDOUBLE if not available.
 *
 * Since: 1.20
 */
gdouble
mm_cell_info_lte_get_rsrq (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), -G_MAXDOUBLE);

    return self->priv->rsrq;
}

/**
 * mm_cell_info_lte_set_rsrq: (skip)
 */
void
mm_cell_info_lte_set_rsrq (MMCellInfoLte *self,
                           gdouble        rsrq)
{
    self->priv->rsrq = rsrq;
}

/**
 * mm_cell_info_lte_get_timing_advance:
 * @self: a #MMCellInfoLte.
 *
 * Get the timing advance.
 *
 * Returns: the timing advance, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_lte_get_timing_advance (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), G_MAXUINT);

    return self->priv->timing_advance;
}

/**
 * mm_cell_info_lte_set_timing_advance: (skip)
 */
void
mm_cell_info_lte_set_timing_advance (MMCellInfoLte *self,
                                     guint          timing_advance)
{
    self->priv->timing_advance = timing_advance;
}

/**
 * mm_cell_info_lte_get_serving_cell_type:
 * @self: a #MMCellInfoLte.
 *
 * Get the serving cell type.
 *
 * Returns: the serving cell type, or %MM_SERVING_CELL_TYPE_INVALID if not available.
 *
 * Since: 1.22
 */
MMServingCellType
mm_cell_info_lte_get_serving_cell_type (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), MM_SERVING_CELL_TYPE_INVALID);

    return self->priv->serving_cell_type;
}

/**
 * mm_cell_info_lte_set_serving_cell_type: (skip)
 */
void
mm_cell_info_lte_set_serving_cell_type (MMCellInfoLte     *self,
                                        MMServingCellType  cell_type)
{
    self->priv->serving_cell_type = cell_type;
}

/**
 * mm_cell_info_lte_get_bandwidth:
 * @self: a #MMCellInfoLte.
 *
 * Get the bandwidth of the particular carrier in downlink.
 *
 * Returns: the bandwidth, or %G_MAXUINT if not available.
 *
 * Since: 1.22
 */
guint
mm_cell_info_lte_get_bandwidth (MMCellInfoLte *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_LTE (self), G_MAXUINT);

    return self->priv->bandwidth;
}

/**
 * mm_cell_info_lte_set_bandwidth: (skip)
 */
void
mm_cell_info_lte_set_bandwidth (MMCellInfoLte *self,
                                guint          bandwidth)
{
    self->priv->bandwidth = bandwidth;
}

/*****************************************************************************/

static GString *
build_string (MMCellInfo *_self)
{
    MMCellInfoLte *self = MM_CELL_INFO_LTE (_self);
    GString       *str;

    str = g_string_new (NULL);

    MM_CELL_INFO_BUILD_STRING_APPEND ("operator id",       "%s",  operator_id,       NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("tac",               "%s",  tac,               NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("ci",                "%s",  ci,                NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("physical ci",       "%s",  physical_ci,       NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("earfcn",            "%u",  earfcn,            G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("rsrp",              "%lf", rsrp,             -G_MAXDOUBLE);
    MM_CELL_INFO_BUILD_STRING_APPEND ("rsrq",              "%lf", rsrq,             -G_MAXDOUBLE);
    MM_CELL_INFO_BUILD_STRING_APPEND ("timing advance",    "%u",  timing_advance,    G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("serving cell type", "%u",  serving_cell_type, MM_SERVING_CELL_TYPE_INVALID);
    MM_CELL_INFO_BUILD_STRING_APPEND ("bandwidth",         "%u",  bandwidth,         G_MAXUINT);

    return str;
}

/*****************************************************************************/

/**
 * mm_cell_info_lte_get_dictionary: (skip)
 */
static GVariantDict *
get_dictionary (MMCellInfo *_self)
{
    MMCellInfoLte *self = MM_CELL_INFO_LTE (_self);
    GVariantDict  *dict;

    dict = g_variant_dict_new (NULL);

    MM_CELL_INFO_GET_DICTIONARY_INSERT (OPERATOR_ID,       operator_id,       string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (TAC,               tac,               string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (CI,                ci,                string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (PHYSICAL_CI,       physical_ci,       string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (EARFCN,            earfcn,            uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (RSRP,              rsrp,              double, -G_MAXDOUBLE);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (RSRQ,              rsrq,              double, -G_MAXDOUBLE);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (TIMING_ADVANCE,    timing_advance,    uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (SERVING_CELL_TYPE, serving_cell_type, uint32,  MM_SERVING_CELL_TYPE_INVALID);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (BANDWIDTH,         bandwidth,         uint32,  G_MAXUINT);

    return dict;
}

/*****************************************************************************/

/**
 * mm_cell_info_lte_new_from_dictionary: (skip)
 */
MMCellInfo *
mm_cell_info_lte_new_from_dictionary (GVariantDict *dict)
{
    MMCellInfoLte *self;

    self = MM_CELL_INFO_LTE (g_object_new (MM_TYPE_CELL_INFO_LTE, NULL));

    if (dict) {
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (lte, OPERATOR_ID, operator_id);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (lte, TAC,         tac);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (lte, CI,          ci);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (lte, PHYSICAL_CI, physical_ci);

        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (lte, EARFCN,            earfcn,            UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (lte, RSRP,              rsrp,              DOUBLE, double);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (lte, RSRQ,              rsrq,              DOUBLE, double);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (lte, TIMING_ADVANCE,    timing_advance,    UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (lte, SERVING_CELL_TYPE, serving_cell_type, UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (lte, BANDWIDTH,         bandwidth,         UINT32, uint32);
    }

    return MM_CELL_INFO (self);
}

/*****************************************************************************/

static void
mm_cell_info_lte_init (MMCellInfoLte *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CELL_INFO_LTE, MMCellInfoLtePrivate);
    self->priv->earfcn            = G_MAXUINT;
    self->priv->rsrp              = -G_MAXDOUBLE;
    self->priv->rsrq              = -G_MAXDOUBLE;
    self->priv->timing_advance    = G_MAXUINT;
    self->priv->serving_cell_type = MM_SERVING_CELL_TYPE_INVALID;
    self->priv->bandwidth         = G_MAXUINT;
}

static void
finalize (GObject *object)
{
    MMCellInfoLte *self = MM_CELL_INFO_LTE (object);

    g_free (self->priv->operator_id);
    g_free (self->priv->tac);
    g_free (self->priv->ci);
    g_free (self->priv->physical_ci);

    G_OBJECT_CLASS (mm_cell_info_lte_parent_class)->finalize (object);
}

static void
mm_cell_info_lte_class_init (MMCellInfoLteClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMCellInfoClass *cell_info_class = MM_CELL_INFO_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCellInfoLtePrivate));

    object_class->finalize = finalize;
    cell_info_class->get_dictionary = get_dictionary;
    cell_info_class->build_string = build_string;

}
