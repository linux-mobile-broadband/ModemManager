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
 * Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <string.h>
#include <stdlib.h>

#include "ModemManager.h"
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>
#include "mm-charsets.h"
#include "mm-errors-types.h"
#include "mm-modem-helpers-cinterion.h"

/* Setup relationship between the 3G band bitmask in the modem and the bitmask
 * in ModemManager. */
typedef struct {
    guint32 cinterion_band_flag;
    MMModemBand mm_band;
} CinterionBand;

/* Table checked in HC25 and PHS8 references. This table includes both 2G and 3G
 * frequencies. Depending on which one is configured, one access technology or
 * the other will be used. This may conflict with the allowed mode configuration
 * set, so you shouldn't for example set 3G frequency bands, and then use a
 * 2G-only allowed mode. */
static const CinterionBand cinterion_bands[] = {
    { (1 << 0), MM_MODEM_BAND_EGSM  },
    { (1 << 1), MM_MODEM_BAND_DCS   },
    { (1 << 2), MM_MODEM_BAND_PCS   },
    { (1 << 3), MM_MODEM_BAND_G850  },
    { (1 << 4), MM_MODEM_BAND_U2100 },
    { (1 << 5), MM_MODEM_BAND_U1900 },
    { (1 << 6), MM_MODEM_BAND_U850  },
    { (1 << 7), MM_MODEM_BAND_U900  },
    { (1 << 8), MM_MODEM_BAND_U800  }
};

/* Check valid combinations in 2G-only devices */
#define VALIDATE_2G_BAND(cinterion_mask) \
    (cinterion_mask == 1  ||             \
     cinterion_mask == 2  ||             \
     cinterion_mask == 4  ||             \
     cinterion_mask == 8  ||             \
     cinterion_mask == 3  ||             \
     cinterion_mask == 5  ||             \
     cinterion_mask == 10 ||             \
     cinterion_mask == 12 ||             \
     cinterion_mask == 15)

/*****************************************************************************/
/* ^SCFG (3G) test parser
 *
 * Example:
 *   AT^SCFG=?
 *     ...
 *     ^SCFG: "MEShutdown/OnIgnition",("on","off")
 *     ^SCFG: "Radio/Band",("1-511","0-1")
 *     ^SCFG: "Radio/NWSM",("0","1","2")
 *     ...
 *
 */

gboolean
mm_cinterion_parse_scfg_test (const gchar *response,
                              MMModemCharset charset,
                              GArray **supported_bands,
                              GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    GError *inner_error = NULL;
    GArray *bands = NULL;

    if (!response) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing response");
        return FALSE;
    }

    r = g_regex_new ("\\^SCFG:\\s*\"Radio/Band\",\\(\"([0-9a-fA-F]*)-([0-9a-fA-F]*)\",.*\\)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        gchar *maxbandstr;
        guint maxband = 0;

        maxbandstr = mm_get_string_unquoted_from_match_info (match_info, 2);
        if (maxbandstr) {
            /* Handle charset conversion if the number is given in UCS2 */
            if (charset != MM_MODEM_CHARSET_UNKNOWN)
                maxbandstr = mm_charset_take_and_convert_to_utf8 (maxbandstr, charset);

            mm_get_uint_from_str (maxbandstr, &maxband);
        }

        if (maxband == 0) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't parse ^SCFG=? response");
        } else {
            guint i;

            for (i = 0; i < G_N_ELEMENTS (cinterion_bands); i++) {
                if (maxband & cinterion_bands[i].cinterion_band_flag) {
                    if (G_UNLIKELY (!bands))
                        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 9);
                    g_array_append_val (bands, cinterion_bands[i].mm_band);
                }
            }
        }

        g_free (maxbandstr);
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (!bands)
        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "No valid bands found in ^SCFG=? response");

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    g_assert (bands != NULL && bands->len > 0);
    *supported_bands = bands;

    return TRUE;
}

/*****************************************************************************/
/* ^SCFG response parser
 *
 * Example (3G):
 *   AT^SCFG="Radio/Band"
 *     ^SCFG: "Radio/Band",127
 *
 * Example (2G, UCS-2):
 *   AT+SCFG="Radio/Band"
 *     ^SCFG: "Radio/Band","0031","0031"
 *
 * Example (2G):
 *   AT+SCFG="Radio/Band"
 *     ^SCFG: "Radio/Band","3","3"
 */

gboolean
mm_cinterion_parse_scfg_response (const gchar *response,
                                  MMModemCharset charset,
                                  GArray **current_bands,
                                  GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    GError *inner_error = NULL;
    GArray *bands = NULL;

    if (!response) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing response");
        return FALSE;
    }

    r = g_regex_new ("\\^SCFG:\\s*\"Radio/Band\",\\s*\"?([0-9a-fA-F]*)\"?", 0, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, NULL)) {
        gchar *currentstr;
        guint current = 0;

        currentstr = mm_get_string_unquoted_from_match_info (match_info, 1);
        if (currentstr) {
            /* Handle charset conversion if the number is given in UCS2 */
            if (charset != MM_MODEM_CHARSET_UNKNOWN)
                currentstr = mm_charset_take_and_convert_to_utf8 (currentstr, charset);

            mm_get_uint_from_str (currentstr, &current);
        }

        if (current == 0) {
            inner_error = g_error_new (MM_CORE_ERROR,
                                       MM_CORE_ERROR_FAILED,
                                       "Couldn't parse ^SCFG response");
        } else {
            guint i;

            for (i = 0; i < G_N_ELEMENTS (cinterion_bands); i++) {
                if (current & cinterion_bands[i].cinterion_band_flag) {
                    if (G_UNLIKELY (!bands))
                        bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 9);
                    g_array_append_val (bands, cinterion_bands[i].mm_band);
                }
            }
        }

        g_free (currentstr);
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (!bands)
        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "No valid bands found in ^SCFG response");

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    g_assert (bands != NULL && bands->len > 0);
    *current_bands = bands;

    return TRUE;
}

/*****************************************************************************/
/* Build Cinterion-specific band value */

gboolean
mm_cinterion_build_band (GArray *bands,
                         guint supported,
                         gboolean only_2g,
                         guint *out_band,
                         GError **error)
{
    guint band = 0;

    /* The special case of ANY should be treated separately. */
    if (bands->len == 1 && g_array_index (bands, MMModemBand, 0) == MM_MODEM_BAND_ANY) {
        band = supported;
    } else {
        guint i;

        for (i = 0; i < G_N_ELEMENTS (cinterion_bands); i++) {
            guint j;

            for (j = 0; j < bands->len; j++) {
                if (g_array_index (bands, MMModemBand, j) == cinterion_bands[i].mm_band) {
                    band |= cinterion_bands[i].cinterion_band_flag;
                    break;
                }
            }
        }

        /* 2G-only modems only support a subset of the possible band
         * combinations. Detect it early and error out.
         */
        if (only_2g && !VALIDATE_2G_BAND (band))
            band = 0;
    }

    if (band == 0) {
        gchar *bands_string;

        bands_string = mm_common_build_bands_string ((MMModemBand *)bands->data, bands->len);
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "The given band combination is not supported: '%s'",
                     bands_string);
        g_free (bands_string);
        return FALSE;
    }

    *out_band = band;
    return TRUE;
}
