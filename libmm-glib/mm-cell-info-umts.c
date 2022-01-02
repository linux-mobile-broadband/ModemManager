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
#include "mm-cell-info-umts.h"

/**
 * SECTION: mm-cell-info-umts
 * @title: MMCellInfoUmts
 * @short_description: Helper object to report UMTS cell info
 *
 * The #MMCellInfoUmts is an object used to report UMTS cell
 * information.
 *
 * The object inherits from the generic #MMCellInfo.
 */

G_DEFINE_TYPE (MMCellInfoUmts, mm_cell_info_umts, MM_TYPE_CELL_INFO)

#define PROPERTY_OPERATOR_ID      "operator-id"
#define PROPERTY_LAC              "lac"
#define PROPERTY_CI               "ci"
#define PROPERTY_FREQUENCY_FDD_UL "frequency-fdd-ul"
#define PROPERTY_FREQUENCY_FDD_DL "frequency-fdd-dl"
#define PROPERTY_FREQUENCY_TDD    "frequency-tdd"
#define PROPERTY_UARFCN           "uarfcn"
#define PROPERTY_PSC              "psc"
#define PROPERTY_RSCP             "rscp"
#define PROPERTY_ECIO             "ecio"
#define PROPERTY_PATH_LOSS        "path-loss"

struct _MMCellInfoUmtsPrivate {
    gchar   *operator_id;
    gchar   *lac;
    gchar   *ci;
    guint    frequency_fdd_ul;
    guint    frequency_fdd_dl;
    guint    frequency_tdd;
    guint    uarfcn;
    guint    psc;
    gdouble  rscp;
    gdouble  ecio;
    guint    path_loss;
};

/*****************************************************************************/

/**
 * mm_cell_info_umts_get_operator_id:
 * @self: a #MMCellInfoUmts.
 *
 * Get the PLMN MCC/MNC.
 *
 * Returns: (transfer none): the MCCMNC, or %NULL if not available.
 *
 * Since: 1.20
 */
const gchar *
mm_cell_info_umts_get_operator_id (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->operator_id);
}

/**
 * mm_cell_info_umts_set_operator_id: (skip)
 */
void
mm_cell_info_umts_set_operator_id (MMCellInfoUmts *self,
                                   const gchar    *operator_id)
{
    g_free (self->priv->operator_id);
    self->priv->operator_id = g_strdup (operator_id);
}

/**
 * mm_cell_info_umts_get_lac:
 * @self: a #MMCellInfoUmts.
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
mm_cell_info_umts_get_lac (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->lac);
}

/**
 * mm_cell_info_umts_set_lac: (skip)
 */
void
mm_cell_info_umts_set_lac (MMCellInfoUmts *self,
                           const gchar    *lac)
{
    g_free (self->priv->lac);
    self->priv->lac = g_strdup (lac);
}

/**
 * mm_cell_info_umts_get_ci:
 * @self: a #MMCellInfoUmts.
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
mm_cell_info_umts_get_ci (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (self->priv->ci);
}

/**
 * mm_cell_info_umts_set_ci: (skip)
 */
void
mm_cell_info_umts_set_ci (MMCellInfoUmts *self,
                          const gchar    *ci)
{
    g_free (self->priv->ci);
    self->priv->ci = g_strdup (ci);
}

/**
 * mm_cell_info_umts_get_frequency_fdd_ul:
 * @self: a #MMCellInfoUmts.
 *
 * Get the frequency of the uplink in kHz while in FDD.
 *
 * Returns: the frequency, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_umts_get_frequency_fdd_ul (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), G_MAXUINT);

    return self->priv->frequency_fdd_ul;
}

/**
 * mm_cell_info_umts_set_frequency_fdd_ul: (skip)
 */
void
mm_cell_info_umts_set_frequency_fdd_ul (MMCellInfoUmts *self,
                                        guint           frequency_fdd_ul)
{
    self->priv->frequency_fdd_ul = frequency_fdd_ul;
}

/**
 * mm_cell_info_umts_get_frequency_fdd_dl:
 * @self: a #MMCellInfoUmts.
 *
 * Get the frequency of the downlink in kHz while in FDD.
 *
 * Returns: the frequency, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_umts_get_frequency_fdd_dl (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), G_MAXUINT);

    return self->priv->frequency_fdd_dl;
}

/**
 * mm_cell_info_umts_set_frequency_fdd_dl: (skip)
 */
void
mm_cell_info_umts_set_frequency_fdd_dl (MMCellInfoUmts *self,
                                        guint           frequency_fdd_dl)
{
    self->priv->frequency_fdd_dl = frequency_fdd_dl;
}


/**
 * mm_cell_info_umts_get_frequency_tdd:
 * @self: a #MMCellInfoUmts.
 *
 * Get the frequency in kHz while in TDD.
 *
 * Returns: the frequency, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_umts_get_frequency_tdd (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), G_MAXUINT);

    return self->priv->frequency_tdd;
}

/**
 * mm_cell_info_umts_set_frequency_tdd: (skip)
 */
void
mm_cell_info_umts_set_frequency_tdd (MMCellInfoUmts *self,
                                     guint           frequency_tdd)
{
    self->priv->frequency_tdd = frequency_tdd;
}

/**
 * mm_cell_info_umts_get_uarfcn:
 * @self: a #MMCellInfoUmts.
 *
 * Get the UTRA absolute RF channel number.
 *
 * Returns: the UARFCN, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_umts_get_uarfcn (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), G_MAXUINT);

    return self->priv->uarfcn;
}

/**
 * mm_cell_info_umts_set_uarfcn: (skip)
 */
void
mm_cell_info_umts_set_uarfcn (MMCellInfoUmts *self,
                              guint           uarfcn)
{
    self->priv->uarfcn = uarfcn;
}

/**
 * mm_cell_info_umts_get_psc:
 * @self: a #MMCellInfoUmts.
 *
 * Get the primary scrambling code.
 *
 * Returns: the PSC, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_umts_get_psc (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), G_MAXUINT);

    return self->priv->psc;
}

/**
 * mm_cell_info_umts_set_psc: (skip)
 */
void
mm_cell_info_umts_set_psc (MMCellInfoUmts *self,
                           guint           psc)
{
    self->priv->psc = psc;
}

/**
 * mm_cell_info_umts_get_rscp:
 * @self: a #MMCellInfoUmts.
 *
 * Get the received signal code power.
 *
 * Returns: the RSCP, or -%G_MAXDOUBLE if not available.
 *
 * Since: 1.20
 */
gdouble
mm_cell_info_umts_get_rscp (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), -G_MAXDOUBLE);

    return self->priv->rscp;
}

/**
 * mm_cell_info_umts_set_rscp: (skip)
 */
void
mm_cell_info_umts_set_rscp (MMCellInfoUmts *self,
                            gdouble         rscp)
{
    self->priv->rscp = rscp;
}

/**
 * mm_cell_info_umts_get_ecio:
 * @self: a #MMCellInfoUmts.
 *
 * Get the ECIO, the received energy per chip divided by the power density
 * in the band measured in dBm on the primary CPICH channel of the cell.
 *
 * Returns: the ECIO, or -%G_MAXDOUBLE if not available.
 *
 * Since: 1.20
 */
gdouble
mm_cell_info_umts_get_ecio (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), -G_MAXDOUBLE);

    return self->priv->ecio;
}

/**
 * mm_cell_info_umts_set_ecio: (skip)
 */
void
mm_cell_info_umts_set_ecio (MMCellInfoUmts *self,
                            gdouble         ecio)
{
    self->priv->ecio = ecio;
}

/**
 * mm_cell_info_umts_get_path_loss:
 * @self: a #MMCellInfoUmts.
 *
 * Get the path loss of the cell.
 *
 * Returns: the path loss, or %G_MAXUINT if not available.
 *
 * Since: 1.20
 */
guint
mm_cell_info_umts_get_path_loss (MMCellInfoUmts *self)
{
    g_return_val_if_fail (MM_IS_CELL_INFO_UMTS (self), G_MAXUINT);

    return self->priv->path_loss;
}

/**
 * mm_cell_info_umts_set_path_loss: (skip)
 */
void
mm_cell_info_umts_set_path_loss (MMCellInfoUmts *self,
                                 guint           path_loss)
{
    self->priv->path_loss = path_loss;
}

/*****************************************************************************/

static GString *
build_string (MMCellInfo *_self)
{
    MMCellInfoUmts *self = MM_CELL_INFO_UMTS (_self);
    GString        *str;

    str = g_string_new (NULL);

    MM_CELL_INFO_BUILD_STRING_APPEND ("operator id",      "%s",  operator_id,       NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("lac",              "%s",  lac,               NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("ci",               "%s",  ci,                NULL);
    MM_CELL_INFO_BUILD_STRING_APPEND ("frequency fdd ul", "%u",  frequency_fdd_ul,  G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("frequency fdd dl", "%u",  frequency_fdd_dl,  G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("frequency tdd",    "%u",  frequency_tdd,     G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("uarfcn",           "%u",  uarfcn,            G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("psc",              "%u",  psc,               G_MAXUINT);
    MM_CELL_INFO_BUILD_STRING_APPEND ("rscp",             "%lf", rscp,             -G_MAXDOUBLE);
    MM_CELL_INFO_BUILD_STRING_APPEND ("ecio",             "%lf", ecio,             -G_MAXDOUBLE);
    MM_CELL_INFO_BUILD_STRING_APPEND ("path loss",        "%u",  path_loss,         G_MAXUINT);

    return str;
}

/*****************************************************************************/

/**
 * mm_cell_info_umts_get_dictionary: (skip)
 */
static GVariantDict *
get_dictionary (MMCellInfo *_self)
{
    MMCellInfoUmts *self = MM_CELL_INFO_UMTS (_self);
    GVariantDict   *dict;

    dict = g_variant_dict_new (NULL);

    MM_CELL_INFO_GET_DICTIONARY_INSERT (OPERATOR_ID,      operator_id,      string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (LAC,              lac,              string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (CI,               ci,               string,  NULL);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (FREQUENCY_FDD_UL, frequency_fdd_ul, uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (FREQUENCY_FDD_DL, frequency_fdd_dl, uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (FREQUENCY_TDD,    frequency_tdd,    uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (UARFCN,           uarfcn,           uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (PSC,              psc,              uint32,  G_MAXUINT);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (RSCP,             rscp,             double, -G_MAXDOUBLE);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (ECIO,             ecio,             double, -G_MAXDOUBLE);
    MM_CELL_INFO_GET_DICTIONARY_INSERT (PATH_LOSS,        path_loss,        uint32,  G_MAXUINT);

    return dict;
}

/*****************************************************************************/

/**
 * mm_cell_info_umts_new_from_dictionary: (skip)
 */
MMCellInfo *
mm_cell_info_umts_new_from_dictionary (GVariantDict *dict)
{
    MMCellInfoUmts *self;

    self = MM_CELL_INFO_UMTS (g_object_new (MM_TYPE_CELL_INFO_UMTS, NULL));

    if (dict) {
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (umts, OPERATOR_ID, operator_id);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (umts, LAC,         lac);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_STRING_SET (umts, CI,          ci);

        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (umts, FREQUENCY_FDD_UL, frequency_fdd_ul, UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (umts, FREQUENCY_FDD_DL, frequency_fdd_dl, UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (umts, FREQUENCY_TDD,    frequency_tdd,    UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (umts, UARFCN,           uarfcn,           UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (umts, PSC,              psc,              UINT32, uint32);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (umts, RSCP,             rscp,             DOUBLE, double);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (umts, ECIO,             ecio,             DOUBLE, double);
        MM_CELL_INFO_NEW_FROM_DICTIONARY_NUM_SET (umts, PATH_LOSS,        path_loss,        UINT32, uint32);
    }

    return MM_CELL_INFO (self);
}

/*****************************************************************************/

static void
mm_cell_info_umts_init (MMCellInfoUmts *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_CELL_INFO_UMTS, MMCellInfoUmtsPrivate);
    self->priv->frequency_fdd_ul = G_MAXUINT;
    self->priv->frequency_fdd_dl = G_MAXUINT;
    self->priv->frequency_tdd = G_MAXUINT;
    self->priv->uarfcn = G_MAXUINT;
    self->priv->psc = G_MAXUINT;
    self->priv->rscp = -G_MAXDOUBLE;
    self->priv->ecio = -G_MAXDOUBLE;
    self->priv->path_loss = G_MAXUINT;
}

static void
finalize (GObject *object)
{
    MMCellInfoUmts *self = MM_CELL_INFO_UMTS (object);

    g_free (self->priv->operator_id);
    g_free (self->priv->lac);
    g_free (self->priv->ci);

    G_OBJECT_CLASS (mm_cell_info_umts_parent_class)->finalize (object);
}

static void
mm_cell_info_umts_class_init (MMCellInfoUmtsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMCellInfoClass *cell_info_class = MM_CELL_INFO_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMCellInfoUmtsPrivate));

    object_class->finalize = finalize;
    cell_info_class->get_dictionary = get_dictionary;
    cell_info_class->build_string = build_string;

}
