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
#include "mm-cell-info-gsm.h"

/**
 * SECTION: mm-cell-info-gsm
 * @title: MMCellInfoGsm
 * @short_description: Helper object to report GSM cell info
 *
 * The #MMCellInfoGsm is an object used to report GSM cell
 * information.
 *
 * The object inherits from the generic #MMCellInfo.
 */

G_DEFINE_TYPE (MMCellInfoGsm, mm_cell_info_gsm, MM_TYPE_CELL_INFO)

#define PROPERTY_OPERATOR_ID     "operator-id"
#define PROPERTY_LAC             "lac"
#define PROPERTY_CI              "ci"
#define PROPERTY_TIMING_ADVANCE  "timing-advance"
#define PROPERTY_ARFCN           "arfcn"
#define PROPERTY_BASE_STATION_ID "base-station-id"
#define PROPERTY_RX_LEVEL        "rx-level"

struct _MMCellInfoGsmPrivate {
    gchar *operator_id;
    gchar *lac;
    gchar *ci;
    guint  timing_advance;
    guint  arfcn;
    gchar *base_station_id;
    guint  rx_level;
};

/*****************************************************************************/

/**
 * mm_cell_info_gsm_get_operator_id:
 * @self: a #MMCellInfoGsm.
 *
 * Get the PLMN MCC/MNC.
 *
 * Returns: (transfer none): the MCCMNC, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_gsm_get_operator_id (MMCellInfoGsm *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_GSM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->operator_id);
}

/**
 * mm_cell_info_gsm_set_operator_id: (skip)
 */
void
mm_cell_info_gsm_set_operator_id (MMCellInfoGsm *self,
                                  const gchar   *operator_id)
{
    g_free (self->priv->operator_id);
    self->priv->operator_id = g_strdup (operator_id);
}

/**
 * mm_cell_info_gsm_get_lac:
 * @self: a #MMCellInfoGsm.
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
mm_cell_info_gsm_get_lac (MMCellInfoGsm *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_GSM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->lac);
}

/**
 * mm_cell_info_gsm_set_lac: (skip)
 */
void
mm_cell_info_gsm_set_lac (MMCellInfoGsm *self,
                          const gchar   *lac)
{
    g_free (self->priv->lac);
    self->priv->lac = g_strdup (lac);
}

/**
 * mm_cell_info_gsm_get_ci:
 * @self: a #MMCellInfoGsm.
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
mm_cell_info_gsm_get_ci (MMCellInfoGsm *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_GSM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->ci);
}

/**
 * mm_cell_info_gsm_set_ci: (skip)
 */
void
mm_cell_info_gsm_set_ci (MMCellInfoGsm *self,
                         const gchar   *ci)
{
    g_free (self->priv->ci);
    self->priv->ci = g_strdup (ci);
}

/**
 * mm_cell_info_gsm_get_timing_advance:
 * @self: a #MMCellInfoGsm.
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
mm_cell_info_gsm_get_timing_advance (MMCellInfoGsm *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_GSM (self), G_MAXUINT);

    return self->priv->timing_advance;
}

/**
 * mm_cell_info_gsm_set_timing_advance: (skip)
 */
void
mm_cell_info_gsm_set_timing_advance (MMCellInfoGsm *self,
                                     guint          timing_advance)
{
    self->priv->timing_advance = timing_advance;
}

/**
 * mm_cell_info_gsm_get_arfcn:
 * @self: a #MMCellInfoGsm.
 *
 * Get the absolute RF channel number.
 *
 * Returns: the ARFCN, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_gsm_get_arfcn (MMCellInfoGsm *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_GSM (self), G_MAXUINT);

    return self->priv->arfcn;
}

/**
 * mm_cell_info_gsm_set_arfcn: (skip)
 */
void
mm_cell_info_gsm_set_arfcn (MMCellInfoGsm *self,
                            guint          arfcn)
{
    self->priv->arfcn = arfcn;
}

/**
 * mm_cell_info_gsm_get_base_station_id:
 * @self: a #MMCellInfoGsm.
 *
 * Get the GSM base station id, in upper-case hexadecimal format without leading
 * zeros. E.g. "3F".
 *
 * Returns: (transfer none): the GSM base station id, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_gsm_get_base_station_id (MMCellInfoGsm *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_GSM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->base_station_id);
}

/**
 * mm_cell_info_gsm_set_base_station_id: (skip)
 */
void
mm_cell_info_gsm_set_base_station_id (MMCellInfoGsm *self,
                                      const gchar   *base_station_id)
{
    g_free (self->priv->base_station_id);
    self->priv->base_station_id = g_strdup (base_station_id);
}

/**
 * mm_cell_info_gsm_get_rx_level:
 * @self: a #MMCellInfoGsm.
 *
 * Get the serving cell RX measurement.
 *
 * Returns: the rx level, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_gsm_get_rx_level (MMCellInfoGsm *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_GSM (self), G_MAXUINT);

    return self->priv->rx_level;
}

/**
 * mm_cell_info_gsm_set_rx_level: (skip)
 */
void
mm_cell_info_gsm_set_rx_level (MMCellInfoGsm *self,
                               guint          rx_level)
{
    self->priv->rx_level = rx_level;
}

/*****************************************************************************/

static GString *
build_string (MMCellInfo *_self)
{
    MMCellInfoGsm *self = MM_CELL_INFO_GSM (_self);
    GString        *str;

    str = g_string_new (NULL);

    MM_CELL_INFO_BUILD_STRING_APPEND ("operator id",     "%s", operator_id,     NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("lac",             "%s", lac,             NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("ci",              "%s", ci,              NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("timing advance",  "%u", timing_advance,  G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("arfcn",           "%u", arfcn,           G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("base station id", "%s", base_station_id, NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("rx level",        "%u", rx_level,        G_MAXUINT);

    return str;
}

/*****************************************************************************/

/**
 * mm_cell_info_gsm_get_dictionary: (skip)
 */
static GVariantDict *
get_dictionary (MMCellInfo *_self)
{
    MMCellInfoGsm *self = MM_CELL_INFO_GSM (_self);
    GVariantDict   *dict;

    dict = g_variant_dict_new (NULL);

    MM_CELL_INFO_GET_DICTIONARY_INSERT (OPERATOR_ID,     operator_id,     string, NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (LAC,             lac,             string, NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (CI,              ci,              string, NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (TIMING_ADVANCE,  timing_advance,  uint32, G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (ARFCN,           arfcn,           uint32, G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (BASE_STATION_ID, base_station_id, string, NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (RX_LEVEL,        rx_level,        uint32, G_MAXUINT);

    return dict;
}

/*****************************************************************************/

/**
 * mm_cell_info_gsm_new_from_dictionary: (skip)
 */
MMCellInfo *
mm_cell_info_gsm_new_from_dictionary (GVariantDict *dict)
{
    MMCellInfoGsm *self;

    self = MM_CELL_INFO_GSM (g_object_new (MM_TYPE_CELL_INFO_GSM, NULL));

    if (dict) {
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (gsm, OPERATOR_ID,     operator_id);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (gsm, LAC,             lac);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (gsm, CI,              ci);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET    (gsm, TIMING_ADVANCE,  timing_advance, UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET    (gsm, ARFCN,           arfcn,          UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (gsm, BASE_STATION_ID, base_station_id);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET    (gsm, RX_LEVEL,        rx_level,       UINT32, uint32);
    }

    return MM_CELL_INFO (self);
}

/*****************************************************************************/

static void
mm_cell_info_gsm_init (MMCellInfoGsm *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CELL_INFO_GSM, MMCellInfoGsmPrivate);
    self->priv->timing_advance = G_MAXUINT;
    self->priv->arfcn = G_MAXUINT;
    self->priv->rx_level = G_MAXUINT;
}

static void
finalize (GObject *object)
{
    MMCellInfoGsm *self = MM_CELL_INFO_GSM (object);

    g_free (self->priv->operator_id);
    g_free (self->priv->lac);
    g_free (self->priv->ci);
    g_free (self->priv->base_station_id);

    G_OBJECT_CLASS (mm_cell_info_gsm_parent_class)->finalize (object);
}

static void
mm_cell_info_gsm_class_init (MMCellInfoGsmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMCellInfoClass *cell_info_class = MM_CELL_INFO_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCellInfoGsmPrivate));

    object_class->finalize = finalize;
    cell_info_class->get_dictionary = get_dictionary;
    cell_info_class->build_string = build_string;

}
