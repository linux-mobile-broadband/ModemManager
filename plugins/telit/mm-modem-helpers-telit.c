/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2015-2019 Telit.
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-telit.h"

/*****************************************************************************/
/* AT#BND 2G values */

#define MM_MODEM_BAND_TELIT_2G_FIRST MM_MODEM_BAND_EGSM
#define MM_MODEM_BAND_TELIT_2G_LAST  MM_MODEM_BAND_G850

#define B2G_FLAG(band) (1 << (band - MM_MODEM_BAND_TELIT_2G_FIRST))

/* Index of the array is the telit 2G band value [0-5]
 * The bitmask value here is built from the 2G MMModemBand values right away. */
static const guint32 telit_2g_to_mm_band_mask[] = {
    [0] = B2G_FLAG (MM_MODEM_BAND_EGSM) + B2G_FLAG (MM_MODEM_BAND_DCS),
    [1] = B2G_FLAG (MM_MODEM_BAND_EGSM) + B2G_FLAG (MM_MODEM_BAND_PCS),
    [2] = B2G_FLAG (MM_MODEM_BAND_DCS)  + B2G_FLAG (MM_MODEM_BAND_G850),
    [3] = B2G_FLAG (MM_MODEM_BAND_PCS)  + B2G_FLAG (MM_MODEM_BAND_G850),
    [4] = B2G_FLAG (MM_MODEM_BAND_EGSM) + B2G_FLAG (MM_MODEM_BAND_DCS) + B2G_FLAG (MM_MODEM_BAND_PCS),
    [5] = B2G_FLAG (MM_MODEM_BAND_EGSM) + B2G_FLAG (MM_MODEM_BAND_DCS) + B2G_FLAG (MM_MODEM_BAND_PCS) + B2G_FLAG (MM_MODEM_BAND_G850),
};

/*****************************************************************************/
/* AT#BND 3G values */

/* NOTE: UTRAN_1 to UTRAN_9 enum values are NOT IN ORDER!
 * E.g. numerically UTRAN_7 is after UTRAN_9...
 *
 * This array allows us to iterate them in a way which is a bit
 * more friendly.
 */
static const guint band_utran_index[] = {
    [MM_MODEM_BAND_UTRAN_1] = 1,
    [MM_MODEM_BAND_UTRAN_2] = 2,
    [MM_MODEM_BAND_UTRAN_3] = 3,
    [MM_MODEM_BAND_UTRAN_4] = 4,
    [MM_MODEM_BAND_UTRAN_5] = 5,
    [MM_MODEM_BAND_UTRAN_6] = 6,
    [MM_MODEM_BAND_UTRAN_7] = 7,
    [MM_MODEM_BAND_UTRAN_8] = 8,
    [MM_MODEM_BAND_UTRAN_9] = 9,
    [MM_MODEM_BAND_UTRAN_10] = 10,
    [MM_MODEM_BAND_UTRAN_11] = 11,
    [MM_MODEM_BAND_UTRAN_12] = 12,
    [MM_MODEM_BAND_UTRAN_13] = 13,
    [MM_MODEM_BAND_UTRAN_14] = 14,
    [MM_MODEM_BAND_UTRAN_19] = 19,
    [MM_MODEM_BAND_UTRAN_20] = 20,
    [MM_MODEM_BAND_UTRAN_21] = 21,
    [MM_MODEM_BAND_UTRAN_22] = 22,
    [MM_MODEM_BAND_UTRAN_25] = 25,
    [MM_MODEM_BAND_UTRAN_26] = 26,
    [MM_MODEM_BAND_UTRAN_32] = 32,
};

#define MM_MODEM_BAND_TELIT_3G_FIRST MM_MODEM_BAND_UTRAN_1
#define MM_MODEM_BAND_TELIT_3G_LAST  MM_MODEM_BAND_UTRAN_19

#define B3G_NUM(band) band_utran_index[band]
#define B3G_FLAG(band) ((B3G_NUM (band) > 0) ? (1LL << (B3G_NUM (band) - B3G_NUM (MM_MODEM_BAND_TELIT_3G_FIRST))) : 0)

/* Index of the arrays is the telit 3G band value.
 * The bitmask value here is built from the 3G MMModemBand values right away.
 *
 * We have 2 different sets of bands that are different for values >=12, because
 * the LM940/960 models support a different set of 3G bands.
 */

#define TELIT_3G_TO_MM_BAND_MASK_DEFAULT_N_ELEMENTS 27
static guint64 telit_3g_to_mm_band_mask_default[TELIT_3G_TO_MM_BAND_MASK_DEFAULT_N_ELEMENTS];

#define TELIT_3G_TO_MM_BAND_MASK_ALTERNATE_N_ELEMENTS 20
static guint64 telit_3g_to_mm_band_mask_alternate[TELIT_3G_TO_MM_BAND_MASK_ALTERNATE_N_ELEMENTS];

static void
initialize_telit_3g_to_mm_band_masks (void)
{
    static gboolean initialized = FALSE;

    /* We need to initialize the arrays in runtime because gcc < 8 doesn't like initializing
     * with operations that are using the band_utran_index constant array elements */

    if (initialized)
        return;

    telit_3g_to_mm_band_mask_default[0]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1);
    telit_3g_to_mm_band_mask_default[1]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_2);
    telit_3g_to_mm_band_mask_default[2]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_default[3]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_default[4]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_default[5]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_default[6]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_default[7]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_4);
    telit_3g_to_mm_band_mask_default[8]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_default[9]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_default[10] = B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_default[11] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) +
                                           B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_default[12] = B3G_FLAG (MM_MODEM_BAND_UTRAN_6);
    telit_3g_to_mm_band_mask_default[13] = B3G_FLAG (MM_MODEM_BAND_UTRAN_3);
    telit_3g_to_mm_band_mask_default[14] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) +
                                           B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_6);
    telit_3g_to_mm_band_mask_default[15] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_3) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_default[16] = B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_default[17] = B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5) +
                                           B3G_FLAG (MM_MODEM_BAND_UTRAN_6);
    telit_3g_to_mm_band_mask_default[18] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_6) +
                                           B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_default[19] = B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_6);
    telit_3g_to_mm_band_mask_default[20] = B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_6);
    telit_3g_to_mm_band_mask_default[21] = B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_6);
    telit_3g_to_mm_band_mask_default[22] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_3) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5) +
                                           B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_default[23] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_3);
    telit_3g_to_mm_band_mask_default[24] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) +
                                           B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_default[25] = B3G_FLAG (MM_MODEM_BAND_UTRAN_19);
    telit_3g_to_mm_band_mask_default[26] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_6) +
                                           B3G_FLAG (MM_MODEM_BAND_UTRAN_8) + B3G_FLAG (MM_MODEM_BAND_UTRAN_19);

    /* Initialize alternate 3G band set */
    telit_3g_to_mm_band_mask_alternate[0]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1);
    telit_3g_to_mm_band_mask_alternate[1]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_2);
    telit_3g_to_mm_band_mask_alternate[2]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_alternate[3]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_alternate[4]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_alternate[5]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_alternate[6]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_alternate[7]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_4);
    telit_3g_to_mm_band_mask_alternate[8]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_alternate[9]  = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_alternate[10] = B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_alternate[11] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) +
                                             B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_alternate[12] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_3) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5) +
                                             B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_alternate[13] = B3G_FLAG (MM_MODEM_BAND_UTRAN_3);
    telit_3g_to_mm_band_mask_alternate[14] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_3) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_alternate[15] = B3G_FLAG (MM_MODEM_BAND_UTRAN_3) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5);
    telit_3g_to_mm_band_mask_alternate[16] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_3) +
                                             B3G_FLAG (MM_MODEM_BAND_UTRAN_4) + B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_alternate[17] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8);
    telit_3g_to_mm_band_mask_alternate[18] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) +
                                             B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8) + B3G_FLAG (MM_MODEM_BAND_UTRAN_9) +
                                             B3G_FLAG (MM_MODEM_BAND_UTRAN_19);
    telit_3g_to_mm_band_mask_alternate[19] = B3G_FLAG (MM_MODEM_BAND_UTRAN_1) + B3G_FLAG (MM_MODEM_BAND_UTRAN_2) + B3G_FLAG (MM_MODEM_BAND_UTRAN_4) +
                                             B3G_FLAG (MM_MODEM_BAND_UTRAN_5) + B3G_FLAG (MM_MODEM_BAND_UTRAN_6) + B3G_FLAG (MM_MODEM_BAND_UTRAN_8) +
                                             B3G_FLAG (MM_MODEM_BAND_UTRAN_9) + B3G_FLAG (MM_MODEM_BAND_UTRAN_19);
}

/*****************************************************************************/
/* AT#BND 4G values
 *
 * The Telit-specific value for 4G bands is a bitmask of the band values, given
 * in hexadecimal or decimal format.
 */

#define MM_MODEM_BAND_TELIT_4G_FIRST MM_MODEM_BAND_EUTRAN_1
#define MM_MODEM_BAND_TELIT_4G_LAST  MM_MODEM_BAND_EUTRAN_64

#define B4G_FLAG(band) (((guint64) 1) << (band - MM_MODEM_BAND_TELIT_4G_FIRST))

#define MM_MODEM_BAND_TELIT_EXT_4G_FIRST MM_MODEM_BAND_EUTRAN_65
#define MM_MODEM_BAND_TELIT_EXT_4G_LAST  MM_MODEM_BAND_EUTRAN_85

#define B4G_FLAG_EXT(band) (((guint64) 1) << (band - MM_MODEM_BAND_TELIT_EXT_4G_FIRST))

/*****************************************************************************/
/* Set current bands helpers */

gchar *
mm_telit_build_bnd_request (GArray                 *bands_array,
                            MMTelitBNDParseConfig  *config,
                            GError                **error)
{
    guint32        mask2g = 0;
    guint64        mask3g = 0;
    guint64        mask4g = 0;
    guint64        mask4gext = 0;
    guint          i;
    gint           flag2g = -1;
    gint64         flag3g = -1;
    gint64         flag4g = -1;
    gchar         *cmd;
    gboolean       modem_is_2g = config->modem_is_2g;
    gboolean       modem_is_3g = config->modem_is_3g;
    gboolean       modem_is_4g = config->modem_is_4g;

    for (i = 0; i < bands_array->len; i++) {
        MMModemBand band;

        band = g_array_index (bands_array, MMModemBand, i);

        /* Convert 2G bands into a bitmask, to match against telit_2g_to_mm_band_mask. */
        if (modem_is_2g && mm_common_band_is_gsm (band) &&
            (band >= MM_MODEM_BAND_TELIT_2G_FIRST) && (band <= MM_MODEM_BAND_TELIT_2G_LAST))
            mask2g += B2G_FLAG (band);

        /* Convert 3G bands into a bitmask, to match against telit_3g_to_mm_band_mask. We use
         * a 64-bit explicit bitmask so that all values fit correctly. */
        if (modem_is_3g && mm_common_band_is_utran (band) &&
            (B3G_NUM (band) >= B3G_NUM (MM_MODEM_BAND_TELIT_3G_FIRST)) && (B3G_NUM (band) <= B3G_NUM (MM_MODEM_BAND_TELIT_3G_LAST)))
            mask3g += B3G_FLAG (band);

        /* Convert 4G bands into a bitmask. We use a 64bit explicit bitmask so that
         * all values fit correctly. */
        if (modem_is_4g && mm_common_band_is_eutran (band)) {
            if (band >= MM_MODEM_BAND_TELIT_4G_FIRST && band <= MM_MODEM_BAND_TELIT_4G_LAST)
                mask4g += B4G_FLAG (band);
            else if (band >= MM_MODEM_BAND_TELIT_EXT_4G_FIRST && band <= MM_MODEM_BAND_TELIT_EXT_4G_LAST)
                mask4gext += B4G_FLAG_EXT (band);
        }
    }

    /* Get 2G-specific telit value */
    if (mask2g) {
        for (i = 0; i < G_N_ELEMENTS (telit_2g_to_mm_band_mask); i++) {
            if (mask2g == telit_2g_to_mm_band_mask[i]) {
                flag2g = i;
                break;
            }
        }
        if (flag2g == -1) {
            gchar *bands_str;

            bands_str = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)(bands_array->data), bands_array->len);
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't find matching 2G bands Telit value for band combination: '%s'", bands_str);
            g_free (bands_str);
            return NULL;
        }
    }

    /* Get 3G-specific telit value */
    if (mask3g) {
        const guint64 *telit_3g_to_mm_band_mask;
        guint          telit_3g_to_mm_band_mask_n_elements;

        initialize_telit_3g_to_mm_band_masks ();

        /* Select correct 3G band mask */
        if (config->modem_alternate_3g_bands) {
            telit_3g_to_mm_band_mask = telit_3g_to_mm_band_mask_alternate;
            telit_3g_to_mm_band_mask_n_elements = G_N_ELEMENTS (telit_3g_to_mm_band_mask_alternate);
        } else {
            telit_3g_to_mm_band_mask = telit_3g_to_mm_band_mask_default;
            telit_3g_to_mm_band_mask_n_elements = G_N_ELEMENTS (telit_3g_to_mm_band_mask_default);
        }
        for (i = 0; i < telit_3g_to_mm_band_mask_n_elements; i++) {
            if (mask3g == telit_3g_to_mm_band_mask[i]) {
                flag3g = i;
                break;
            }
        }
        if (flag3g == -1) {
            gchar *bands_str;

            bands_str = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)(bands_array->data), bands_array->len);
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't find matching 3G bands Telit value for band combination: '%s'", bands_str);
            g_free (bands_str);
            return NULL;
        }
    }

    /* Get 4G-specific telit band bitmask */
    flag4g = (mask4g != 0) ? ((gint64)mask4g) : -1;

    /* If the modem supports a given access tech, we must always give band settings
     * for the specific tech */
    if (modem_is_2g && flag2g == -1) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                     "None or invalid 2G bands combination in the provided list");
        return NULL;
    }
    if (modem_is_3g && flag3g == -1) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                     "None or invalid 3G bands combination in the provided list");
        return NULL;
    }
    if (modem_is_4g && mask4g == 0 && mask4gext == 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                     "None or invalid 4G bands combination in the provided list");
        return NULL;
    }

    if (modem_is_2g && !modem_is_3g && !modem_is_4g)
        cmd = g_strdup_printf ("#BND=%d", flag2g);
    else if (!modem_is_2g && modem_is_3g && !modem_is_4g)
        cmd = g_strdup_printf ("#BND=0,%" G_GINT64_FORMAT, flag3g);
    else if (modem_is_2g && modem_is_3g && !modem_is_4g)
        cmd = g_strdup_printf ("#BND=%d,%" G_GINT64_FORMAT, flag2g, flag3g);
    else if (!modem_is_2g && !modem_is_3g && modem_is_4g) {
        if (!config->modem_ext_4g_bands)
            cmd = g_strdup_printf ("#BND=0,0,%" G_GINT64_FORMAT, flag4g);
        else
            cmd = g_strdup_printf ("#BND=0,0,%" G_GINT64_MODIFIER "x" ",%" G_GINT64_MODIFIER "x", mask4g, mask4gext);
    } else if (!modem_is_2g && modem_is_3g && modem_is_4g) {
        if (!config->modem_ext_4g_bands)
            cmd = g_strdup_printf ("#BND=0,%" G_GINT64_FORMAT ",%" G_GINT64_FORMAT, flag3g, flag4g);
        else
            cmd = g_strdup_printf ("#BND=0,%" G_GINT64_FORMAT ",%" G_GINT64_MODIFIER "x" ",%" G_GINT64_MODIFIER "x", flag3g, mask4g, mask4gext);
    } else if (modem_is_2g && !modem_is_3g && modem_is_4g) {
        if (!config->modem_ext_4g_bands)
            cmd = g_strdup_printf ("#BND=%d,0,%" G_GINT64_FORMAT, flag2g, flag4g);
        else
            cmd = g_strdup_printf ("#BND=%d,0,%" G_GINT64_MODIFIER "x" ",%" G_GINT64_MODIFIER "x", flag2g, mask4g, mask4gext);
    } else if (modem_is_2g && modem_is_3g && modem_is_4g) {
        if (!config->modem_ext_4g_bands)
            cmd = g_strdup_printf ("#BND=%d,%" G_GINT64_FORMAT ",%" G_GINT64_FORMAT, flag2g, flag3g, flag4g);
        else
            cmd = g_strdup_printf ("#BND=%d,%" G_GINT64_FORMAT ",%" G_GINT64_MODIFIER "x" ",%" G_GINT64_MODIFIER "x", flag2g, flag3g, mask4g, mask4gext);
    } else
        g_assert_not_reached ();

    return cmd;
}

/*****************************************************************************/
/* #BND response parser
 *
 * AT#BND=?
 *      #BND: <2G band flags range>,<3G band flags range>[, <4G band flags range>]
 *
 *  where "band flags" is a list of numbers defining the supported bands.
 *  Note that the one Telit band flag may represent more than one MM band.
 *
 *  e.g.
 *
 *  #BND: (0-2),(3,4)
 *
 *  (0,2) = 2G band flag 0 is EGSM + DCS
 *        = 2G band flag 1 is EGSM + PCS
 *        = 2G band flag 2 is DCS + G850
 *  (3,4) = 3G band flag 3 is U2100 + U1900 + U850
 *        = 3G band flag 4 is U1900 + U850
 *
 * Modems that supports 4G bands, return a range value(X-Y) where
 * X: represent the lower supported band, such as X = 2^(B-1), being B = B1, B2,..., B32
 * Y: is a 32 bit number resulting from a mask of all the supported bands:
 *      1 - B1
 *      2 - B2
 *      4 - B3
 *      8 - B4
 *      ...
 *      i - B(2exp(i-1))
 *      ...
 *      2147483648 - B32
 *
 *   e.g.
 *      (2-4106)
 *       2 = 2^1 --> lower supported band B2
 *       4106 = 2^1 + 2^3 + 2^12 --> the supported bands are B2, B4, B13
 *
 *
 * AT#BND?
 *      #BND: <2G band flags>,<3G band flags>[, <4G band flags>]
 *
 *  where "band flags" is a number defining the current bands.
 *  Note that the one Telit band flag may represent more than one MM band.
 *
 *  e.g.
 *
 *  #BND: 0,4
 *
 *  0 = 2G band flag 0 is EGSM + DCS
 *  4 = 3G band flag 4 is U1900 + U850
 *
 *  ----------------
 *
 *  For modems such as LN920 the #BND configuration/response for LTE bands is different
 *  from what is explained above:
 *
 *  AT#BND=<GSM_band>[,<UMTS_band>[,<LTE_band>[,<LTE_band_ext>]]]
 *
 *  <LTE_band>: hex: Indicates the LTE supported bands expressed as the sum of Band number (1+2+8 ...) calculated as shown in the table (mask of 64 bits):
 *
 * Band number(Hex)  Band i
 * 0                 disable
 * 1                 B1
 * 2                 B2
 * 4                 B3
 * 8                 B4
 * ...
 * ...
 * 80000000          B32
 * ...
 * ...
 * 800000000000      B48
 *
 * It can take value, 0 - 87A03B0F38DF: range of the sum of Band number (1+2+4 ...)
 *
 * <LTE_band_ext>: hex: Indicates the LTE supported bands from B65 expressed as the sum of Band number (1+2+8 ...) calculated as shown in the table (mask of 64 bits):
 *
 * Band number(Hex)  Band i
 * 0                 disable
 * 2                 B66
 * 40                B71
 *
 * It can take value, 0 - 42: range of the sum of Band number (2+40)
 *
 * Note: LTE_band and LTE_band_ext cannot be 0 at the same time
 *
 * Example output:
 *
 * AT#BND=?
 * #BND: (0),(0-11,17,18),(87A03B0F38DF),(42)
 *
 * AT#BND?
 * #BND: 0,18,87A03B0F38DF,42
 *
 */

static gboolean
telit_get_2g_mm_bands (GMatchInfo  *match_info,
                       gpointer     log_object,
                       GArray     **bands,
                       GError     **error)
{
    GError *inner_error = NULL;
    GArray *values = NULL;
    gchar  *match_str = NULL;
    guint   i;

    match_str = g_match_info_fetch_named (match_info, "Bands2G");
    if (!match_str || match_str[0] == '\0') {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find 2G band values from response");
        goto out;
    }

    values = mm_parse_uint_list (match_str, &inner_error);
    if (!values)
        goto out;

    for (i = 0; i < values->len; i++) {
        guint value;

        value = g_array_index (values, guint, i);
        if (value < G_N_ELEMENTS (telit_2g_to_mm_band_mask)) {
            guint j;

            for (j = MM_MODEM_BAND_TELIT_2G_FIRST; j <= MM_MODEM_BAND_TELIT_2G_LAST; j++) {
                if ((telit_2g_to_mm_band_mask[value] & B2G_FLAG (j)) && !mm_common_bands_garray_lookup (*bands, j))
                    *bands = g_array_append_val (*bands, j);
            }
        } else
            mm_obj_dbg (log_object, "unhandled telit 2G band value configuration: %u", value);
    }

out:
    g_free (match_str);
    g_clear_pointer (&values, g_array_unref);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean
telit_get_3g_mm_bands (GMatchInfo  *match_info,
                       gpointer     log_object,
                       gboolean     modem_alternate_3g_bands,
                       GArray     **bands,
                       GError     **error)
{
    GError        *inner_error = NULL;
    GArray        *values = NULL;
    gchar         *match_str = NULL;
    guint          i;
    const guint64 *telit_3g_to_mm_band_mask;
    guint          telit_3g_to_mm_band_mask_n_elements;

    initialize_telit_3g_to_mm_band_masks ();

    /* Select correct 3G band mask */
    if (modem_alternate_3g_bands) {
        telit_3g_to_mm_band_mask = telit_3g_to_mm_band_mask_alternate;
        telit_3g_to_mm_band_mask_n_elements = G_N_ELEMENTS (telit_3g_to_mm_band_mask_alternate);
    } else {
        telit_3g_to_mm_band_mask = telit_3g_to_mm_band_mask_default;
        telit_3g_to_mm_band_mask_n_elements = G_N_ELEMENTS (telit_3g_to_mm_band_mask_default);
    }

    match_str = g_match_info_fetch_named (match_info, "Bands3G");
    if (!match_str || match_str[0] == '\0') {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find 3G band values from response");
        goto out;
    }

    values = mm_parse_uint_list (match_str, &inner_error);
    if (!values)
        goto out;

    for (i = 0; i < values->len; i++) {
        guint value;

        value = g_array_index (values, guint, i);

        if (value < telit_3g_to_mm_band_mask_n_elements) {
            guint j;

            for (j = 0; j < G_N_ELEMENTS (band_utran_index); j++) {
                /* ignore non-3G bands */
                if (band_utran_index[j] == 0)
                    continue;

                if ((telit_3g_to_mm_band_mask[value] & B3G_FLAG (j)) && !mm_common_bands_garray_lookup (*bands, j))
                    *bands = g_array_append_val (*bands, j);
            }
        } else
            mm_obj_dbg (log_object, "unhandled telit 3G band value configuration: %u", value);
    }

out:
    g_free (match_str);
    g_clear_pointer (&values, g_array_unref);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean
telit_get_4g_mm_bands (GMatchInfo  *match_info,
                       GArray     **bands,
                       GError     **error)
{
    GError       *inner_error = NULL;
    MMModemBand   band;
    gchar        *match_str = NULL;
    guint64       value;
    gchar       **tokens = NULL;
    gboolean      hex_format = FALSE;
    gboolean      ok;

    match_str = g_match_info_fetch_named (match_info, "Bands4GDec");
    if (!match_str) {
        match_str = g_match_info_fetch_named (match_info, "Bands4GHex");
        hex_format = match_str != NULL;
    }

    if (!match_str || match_str[0] == '\0') {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find 4G band flags from response");
        goto out;
    }

    /* splitting will never return NULL as string is not empty */
    tokens = g_strsplit (match_str, "-", -1);

    /* If this is a range, get upper threshold, which contains the total supported mask */
    ok = hex_format?
        mm_get_u64_from_hex_str (tokens[1] ? tokens[1] : tokens[0], &value):
        mm_get_u64_from_str (tokens[1] ? tokens[1] : tokens[0], &value);
    if (!ok) {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse 4G band mask from string: '%s'", match_str);
        goto out;
    }

    for (band = MM_MODEM_BAND_TELIT_4G_FIRST; band <= MM_MODEM_BAND_TELIT_4G_LAST; band++) {
        if ((value & B4G_FLAG (band)) && !mm_common_bands_garray_lookup (*bands, band))
            g_array_append_val (*bands, band);
    }

out:
    g_strfreev (tokens);
    g_free (match_str);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }
    return TRUE;
}

static gboolean
telit_get_ext_4g_mm_bands (GMatchInfo  *match_info,
                           GArray     **bands,
                           GError     **error)
{
    GError       *inner_error = NULL;
    MMModemBand   band;
    gchar        *match_str = NULL;
    gchar        *match_str_ext = NULL;
    guint64       value;

    match_str = g_match_info_fetch_named (match_info, "Bands4GHex");
    if (!match_str || match_str[0] == '\0') {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find 4G band hex mask flag from response");
        goto out;
    }

    if (!mm_get_u64_from_hex_str (match_str, &value)) {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse 4G band hex mask from string: '%s'", match_str);
        goto out;
    }

    for (band = MM_MODEM_BAND_TELIT_4G_FIRST; band <= MM_MODEM_BAND_TELIT_4G_LAST; band++) {
        if ((value & B4G_FLAG (band)) && !mm_common_bands_garray_lookup (*bands, band))
            g_array_append_val (*bands, band);
    }

    /* extended bands */
    match_str_ext = g_match_info_fetch_named (match_info, "Bands4GExt");
    if (match_str_ext) {

        if (!mm_get_u64_from_hex_str (match_str_ext, &value)) {
            g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Could not parse 4G ext band mask from string: '%s'", match_str_ext);
            goto out;
        }

        for (band = MM_MODEM_BAND_TELIT_EXT_4G_FIRST; band <= MM_MODEM_BAND_TELIT_EXT_4G_LAST; band++) {
            if ((value & B4G_FLAG_EXT (band)) && !mm_common_bands_garray_lookup (*bands, band))
                g_array_append_val (*bands, band);
        }
    }

out:
    g_free (match_str);
    g_free (match_str_ext);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }
    return TRUE;
}

typedef enum {
    LOAD_BANDS_TYPE_SUPPORTED,
    LOAD_BANDS_TYPE_CURRENT,
} LoadBandsType;

/* Regex tokens for #BND parsing */
#define MM_SUPPORTED_BANDS_2G       "\\s*\\((?P<Bands2G>[0-9\\-,]*)\\)"
#define MM_SUPPORTED_BANDS_3G       "(,\\s*\\((?P<Bands3G>[0-9\\-,]*)\\))?"
#define MM_SUPPORTED_BANDS_4G_HEX   "(,\\s*\\((?P<Bands4GHex>[0-9A-F\\-,]*)\\))?"
#define MM_SUPPORTED_BANDS_4G_DEC   "(,\\s*\\((?P<Bands4GDec>[0-9\\-,]*)\\))?"
#define MM_SUPPORTED_BANDS_4G_EXT   "(,\\s*\\((?P<Bands4GHex>[0-9A-F]+)\\))?(,\\s*\\((?P<Bands4GExt>[0-9A-F]+)\\))?"
#define MM_CURRENT_BANDS_2G         "\\s*(?P<Bands2G>\\d+)"
#define MM_CURRENT_BANDS_3G         "(,\\s*(?P<Bands3G>\\d+))?"
#define MM_CURRENT_BANDS_4G_HEX     "(,\\s*(?P<Bands4GHex>[0-9A-F]+))?"
#define MM_CURRENT_BANDS_4G_DEC     "(,\\s*(?P<Bands4GDec>\\d+))?"
#define MM_CURRENT_BANDS_4G_EXT     "(,\\s*(?P<Bands4GHex>[0-9A-F]+))?(,\\s*(?P<Bands4GExt>[0-9A-F]+))?"

static GArray *
common_parse_bnd_response (const gchar            *response,
                           MMTelitBNDParseConfig  *config,
                           LoadBandsType           load_type,
                           gpointer                log_object,
                           GError                **error)
{
    g_autoptr(GMatchInfo)  match_info = NULL;
    g_autoptr(GRegex)      r = NULL;
    GError                *inner_error = NULL;
    GArray                *bands = NULL;
    const gchar           *load_bands_regex = NULL;

    static const gchar *load_bands_regex_4g_hex[] = {
        [LOAD_BANDS_TYPE_SUPPORTED] = "#BND:"MM_SUPPORTED_BANDS_2G MM_SUPPORTED_BANDS_3G MM_SUPPORTED_BANDS_4G_HEX,
        [LOAD_BANDS_TYPE_CURRENT]   = "#BND:"MM_CURRENT_BANDS_2G MM_CURRENT_BANDS_3G MM_CURRENT_BANDS_4G_HEX,

    };
    static const gchar *load_bands_regex_4g_dec[] = {
        [LOAD_BANDS_TYPE_SUPPORTED] = "#BND:"MM_SUPPORTED_BANDS_2G MM_SUPPORTED_BANDS_3G MM_SUPPORTED_BANDS_4G_DEC,
        [LOAD_BANDS_TYPE_CURRENT]   = "#BND:"MM_CURRENT_BANDS_2G MM_CURRENT_BANDS_3G MM_CURRENT_BANDS_4G_DEC,
    };
    static const gchar *load_bands_regex_4g_ext[] = {
        [LOAD_BANDS_TYPE_SUPPORTED] = "#BND:"MM_SUPPORTED_BANDS_2G MM_SUPPORTED_BANDS_3G MM_SUPPORTED_BANDS_4G_EXT,
        [LOAD_BANDS_TYPE_CURRENT]   = "#BND:"MM_CURRENT_BANDS_2G MM_CURRENT_BANDS_3G MM_CURRENT_BANDS_4G_EXT,
    };

    if (config->modem_ext_4g_bands)
        load_bands_regex = load_bands_regex_4g_ext[load_type];
    else if (config->modem_has_hex_format_4g_bands)
        load_bands_regex = load_bands_regex_4g_hex[load_type];
    else
        load_bands_regex = load_bands_regex_4g_dec[load_type];

    r = g_regex_new (load_bands_regex, G_REGEX_RAW, 0, NULL);
    g_assert (r);

    if (!g_regex_match (r, response, 0, &match_info)) {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse response '%s'", response);
        goto out;
    }

    if (!g_match_info_matches (match_info)) {
        g_set_error (&inner_error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find matches in response '%s'", response);
        goto out;
    }

    bands = g_array_new (TRUE, TRUE, sizeof (MMModemBand));

    if (config->modem_is_2g && !telit_get_2g_mm_bands (match_info, log_object, &bands, &inner_error))
        goto out;

    if (config->modem_is_3g && !telit_get_3g_mm_bands (match_info, log_object, config->modem_alternate_3g_bands, &bands, &inner_error))
        goto out;

    if (config->modem_is_4g) {
        gboolean ok;

        ok = config->modem_ext_4g_bands?
            telit_get_ext_4g_mm_bands (match_info, &bands, &inner_error) :
            telit_get_4g_mm_bands (match_info, &bands, &inner_error);
        if (!ok)
            goto out;
    }
out:
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_clear_pointer (&bands, g_array_unref);
        return NULL;
    }

    return bands;
}

GArray *
mm_telit_parse_bnd_query_response (const gchar            *response,
                                   MMTelitBNDParseConfig  *config,
                                   gpointer                log_object,
                                   GError                **error)
{
    return common_parse_bnd_response (response,
                                      config,
                                      LOAD_BANDS_TYPE_CURRENT,
                                      log_object,
                                      error);
}

GArray *
mm_telit_parse_bnd_test_response (const gchar            *response,
                                  MMTelitBNDParseConfig  *config,
                                  gpointer                log_object,
                                  GError                **error)
{
    return common_parse_bnd_response (response,
                                      config,
                                      LOAD_BANDS_TYPE_SUPPORTED,
                                      log_object,
                                      error);
}

/*****************************************************************************/
/* #QSS? response parser */

MMTelitQssStatus
mm_telit_parse_qss_query (const gchar *response,
                          GError **error)
{
    gint qss_status;
    gint qss_mode;

    qss_status = QSS_STATUS_UNKNOWN;
    if (sscanf (response, "#QSS: %d,%d", &qss_mode, &qss_status) != 2) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse \"#QSS?\" response: %s", response);
        return QSS_STATUS_UNKNOWN;
    }

    switch (qss_status) {
    case QSS_STATUS_SIM_REMOVED:
    case QSS_STATUS_SIM_INSERTED:
    case QSS_STATUS_SIM_INSERTED_AND_UNLOCKED:
    case QSS_STATUS_SIM_INSERTED_AND_READY:
        return (MMTelitQssStatus) qss_status;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unknown QSS status value given: %d", qss_status);
        return QSS_STATUS_UNKNOWN;
    }
}

/*****************************************************************************/
/* Supported modes list helper */

GArray *
mm_telit_build_modes_list (void)
{
    GArray *combinations;
    MMModemModeCombination mode;

    /* Build list of combinations for 3GPP devices */
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 11);

    /* 2G only */
    mode.allowed = MM_MODEM_MODE_2G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G only */
    mode.allowed = MM_MODEM_MODE_3G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 3G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 4G only */
    mode.allowed = MM_MODEM_MODE_4G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G and 4G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_4G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G and 4G */
    mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 2G, 3G and 4G */
    mode.allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 5G only */
    mode.allowed = MM_MODEM_MODE_5G;
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G and 5G */
    mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_5G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 4G and 5G */
    mode.allowed = (MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);
    /* 3G, 4G and 5G */
    mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
    mode.preferred = MM_MODEM_MODE_NONE;
    g_array_append_val (combinations, mode);

    return combinations;
}

/*****************************************************************************/
/* Software Package version response parser */

gchar *
mm_telit_parse_swpkgv_response (const gchar *response)
{
    gchar *version = NULL;
    g_autofree gchar *base_version = NULL;
    g_autoptr(GRegex) r = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;
    guint matches;

    /* We are interested only in the first line of the response */
    r = g_regex_new ("(?P<Base>\\d{2}.\\d{2}.*)",
                     G_REGEX_RAW | G_REGEX_MULTILINE | G_REGEX_NEWLINE_CRLF,
                     G_REGEX_MATCH_NEWLINE_CR,
                     NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, NULL)) {
        return NULL;
    }

    matches = g_match_info_get_match_count (match_info);
    if (matches < 2 || matches > 4) {
        return NULL;
    }

    base_version = g_match_info_fetch_named (match_info, "Base");
    if (base_version)
        version = g_strdup (base_version);

    return version;
}

/*****************************************************************************/
/* MM Telit Model from revision string */

MMTelitModel
mm_telit_model_from_revision (const gchar *revision)
{
    guint i;
    static const struct {
        const gchar *revision_prefix;
        MMTelitModel model;
    } revision_to_model_map [] = {
        {"24.01", MM_TELIT_MODEL_LM940},
        {"25.", MM_TELIT_MODEL_LE910C1},
        {"32.", MM_TELIT_MODEL_LM960},
        {"38.", MM_TELIT_MODEL_FN980},
        {"40.", MM_TELIT_MODEL_LN920},
        {"45.00", MM_TELIT_MODEL_FN990},
    };

    if (!revision)
        return MM_TELIT_MODEL_DEFAULT;

    for (i = 0; i < G_N_ELEMENTS (revision_to_model_map); ++i) {
        if (g_str_has_prefix (revision, revision_to_model_map[i].revision_prefix))
            return revision_to_model_map[i].model;
    }

    return MM_TELIT_MODEL_DEFAULT;
}

static MMTelitSwRevCmp lm9x0_software_revision_cmp (const gchar *revision_a,
                                                    const gchar *revision_b)
{
    /* LM940 and LM960 share the same software revision format
     * WW.XY.ABC[-ZZZZ], where WW is the chipset code and C the major version.
     * If WW is the same, the other values X, Y, A and B are also the same, so
     * we can limit the comparison to C only. ZZZZ is the minor version (it
     * includes if version is beta, test, or alpha), but at this stage we are
     * not interested in compare it. */
    guint chipset_a, chipset_b;
    guint major_a, major_b;
    guint x, y, a, b;

    g_return_val_if_fail (
        sscanf (revision_a, "%2u.%1u%1u.%1u%1u%1u", &chipset_a, &x, &y, &a, &b, &major_a) == 6,
        MM_TELIT_SW_REV_CMP_INVALID);
    g_return_val_if_fail (
        sscanf (revision_b, "%2u.%1u%1u.%1u%1u%1u", &chipset_b, &x, &y, &a, &b, &major_b) == 6,
        MM_TELIT_SW_REV_CMP_INVALID);

    if (chipset_a != chipset_b)
        return MM_TELIT_SW_REV_CMP_INVALID;
    if (major_a > major_b)
        return MM_TELIT_SW_REV_CMP_NEWER;
    if (major_a < major_b)
        return MM_TELIT_SW_REV_CMP_OLDER;
    return MM_TELIT_SW_REV_CMP_EQUAL;
}

MMTelitSwRevCmp mm_telit_software_revision_cmp (const gchar *revision_a,
                                                const gchar *revision_b)
{
    MMTelitModel model_a;
    MMTelitModel model_b;

    model_a = mm_telit_model_from_revision (revision_a);
    model_b = mm_telit_model_from_revision (revision_b);

    if ((model_a == MM_TELIT_MODEL_LM940 || model_a == MM_TELIT_MODEL_LM960) &&
        (model_b == MM_TELIT_MODEL_LM940 || model_b == MM_TELIT_MODEL_LM960)) {
        return lm9x0_software_revision_cmp (revision_a, revision_b);
    }

    return MM_TELIT_SW_REV_CMP_UNSUPPORTED;
}
