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
#include "mm-cell-info-cdma.h"

/**
 * SECTION: mm-cell-info-cdma
 * @title: MMCellInfoCdma
 * @short_description: Helper object to report CDMA cell info
 *
 * The #MMCellInfoCdma is an object used to report CDMA cell
 * information.
 *
 * The object inherits from the generic #MMCellInfo.
 */

G_DEFINE_TYPE (MMCellInfoCdma, mm_cell_info_cdma, MM_TYPE_CELL_INFO)

#define PROPERTY_NID             "nid"
#define PROPERTY_SID             "sid"
#define PROPERTY_BASE_STATION_ID "base-station-id"
#define PROPERTY_REF_PN          "ref-pn"
#define PROPERTY_PILOT_STRENGTH  "pilot-strength"

struct _MMCellInfoCdmaPrivate {
    gchar *nid;
    gchar *sid;
    gchar *base_station_id;
    gchar *ref_pn;
    guint  pilot_strength;
};

/*****************************************************************************/

/**
 * mm_cell_info_cdma_get_nid:
 * @self: a #MMCellInfoCdma.
 *
 * Get the CDMA network id.
 *
 * Encoded in upper-case hexadecimal format without leading zeros.
 *
 * Returns: (transfer none): the CDMA network id, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_cdma_get_nid (MMCellInfoCdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_CDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->nid);
}

/**
 * mm_cell_info_cdma_set_nid: (skip)
 */
void
mm_cell_info_cdma_set_nid (MMCellInfoCdma *self,
                           const gchar    *nid)
{
    g_free (self->priv->nid);
    self->priv->nid = g_strdup (nid);
}

/**
 * mm_cell_info_cdma_get_sid:
 * @self: a #MMCellInfoCdma.
 *
 * Get the CDMA system id.
 *
 * Encoded in upper-case hexadecimal format without leading zeros.
 *
 * Returns: (transfer none): the CDMA system id, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_cdma_get_sid (MMCellInfoCdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_CDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->sid);
}

/**
 * mm_cell_info_cdma_set_sid: (skip)
 */
void
mm_cell_info_cdma_set_sid (MMCellInfoCdma *self,
                           const gchar    *sid)
{
    g_free (self->priv->sid);
    self->priv->sid = g_strdup (sid);
}

/**
 * mm_cell_info_cdma_get_base_station_id:
 * @self: a #MMCellInfoCdma.
 *
 * Get the CDMA base station id.
 *
 * Encoded in upper-case hexadecimal format without leading zeros.
 *
 * Returns: (transfer none): the CDMA base station id, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_cdma_get_base_station_id (MMCellInfoCdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_CDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->base_station_id);
}

/**
 * mm_cell_info_cdma_set_base_station_id: (skip)
 */
void
mm_cell_info_cdma_set_base_station_id (MMCellInfoCdma *self,
                                       const gchar    *base_station_id)
{
    g_free (self->priv->base_station_id);
    self->priv->base_station_id = g_strdup (base_station_id);
}

/**
 * mm_cell_info_cdma_get_ref_pn:
 * @self: a #MMCellInfoCdma.
 *
 * Get the CDMA base station PN number.
 *
 * Encoded in upper-case hexadecimal format without leading zeros.
 *
 * Returns: (transfer none): the CDMA base station PN number, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_cdma_get_ref_pn (MMCellInfoCdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_CDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->ref_pn);
}

/**
 * mm_cell_info_cdma_set_ref_pn: (skip)
 */
void
mm_cell_info_cdma_set_ref_pn (MMCellInfoCdma *self,
                              const gchar    *ref_pn)
{
    g_free (self->priv->ref_pn);
    self->priv->ref_pn = g_strdup (ref_pn);
}

/**
 * mm_cell_info_cdma_get_pilot_strength:
 * @self: a #MMCellInfoCdma.
 *
 * Get the signal strength of the pilot.
 *
 * Given in the same format and scale as the GSM SINR level.
 *
 * Returns: the pilot strength, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_cdma_get_pilot_strength (MMCellInfoCdma *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_CDMA (self), G_MAXUINT);

    return self->priv->pilot_strength;
}

/**
 * mm_cell_info_cdma_set_pilot_strength: (skip)
 */
void
mm_cell_info_cdma_set_pilot_strength (MMCellInfoCdma *self,
                                      guint           pilot_strength)
{
    self->priv->pilot_strength = pilot_strength;
}

/*****************************************************************************/

static GString *
build_string (MMCellInfo *_self)
{
    MMCellInfoCdma *self = MM_CELL_INFO_CDMA (_self);
    GString        *str;

    str = g_string_new (NULL);

    MM_CELL_INFO_BUILD_STRING_APPEND ("nid",             "%s", nid,             NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("sid",             "%s", sid,             NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("base station id", "%s", base_station_id, NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("ref pn",          "%s", ref_pn,          NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("pilot strength",  "%u", pilot_strength,  G_MAXUINT);

    return str;
}

/*****************************************************************************/

/**
 * mm_cell_info_cdma_get_dictionary: (skip)
 */
static GVariantDict *
get_dictionary (MMCellInfo *_self)
{
    MMCellInfoCdma *self = MM_CELL_INFO_CDMA (_self);
    GVariantDict   *dict;

    dict = g_variant_dict_new (NULL);

    MM_CELL_INFO_GET_DICTIONARY_INSERT (NID,             nid,             string, NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (SID,             sid,             string, NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (BASE_STATION_ID, base_station_id, string, NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (REF_PN,          ref_pn,          string, NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (PILOT_STRENGTH,  pilot_strength,  uint32, G_MAXUINT);

    return dict;
}

/*****************************************************************************/

/**
 * mm_cell_info_cdma_new_from_dictionary: (skip)
 */
MMCellInfo *
mm_cell_info_cdma_new_from_dictionary (GVariantDict *dict)
{
    MMCellInfoCdma *self;

    self = MM_CELL_INFO_CDMA (g_object_new (MM_TYPE_CELL_INFO_CDMA, NULL));

    if (dict) {

        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (cdma, NID,             nid);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (cdma, SID,             sid);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (cdma, BASE_STATION_ID, base_station_id);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (cdma, REF_PN,          ref_pn);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET    (cdma, PILOT_STRENGTH,  pilot_strength, UINT32, uint32);
    }

    return MM_CELL_INFO (self);
}

/*****************************************************************************/

static void
mm_cell_info_cdma_init (MMCellInfoCdma *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CELL_INFO_CDMA, MMCellInfoCdmaPrivate);
    self->priv->pilot_strength = G_MAXUINT;
}

static void
finalize (GObject *object)
{
    MMCellInfoCdma *self = MM_CELL_INFO_CDMA (object);

    g_free (self->priv->sid);
    g_free (self->priv->nid);
    g_free (self->priv->base_station_id);
    g_free (self->priv->ref_pn);

    G_OBJECT_CLASS (mm_cell_info_cdma_parent_class)->finalize (object);
}

static void
mm_cell_info_cdma_class_init (MMCellInfoCdmaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMCellInfoClass *cell_info_class = MM_CELL_INFO_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCellInfoCdmaPrivate));

    object_class->finalize = finalize;
    cell_info_class->get_dictionary = get_dictionary;
    cell_info_class->build_string = build_string;

}
