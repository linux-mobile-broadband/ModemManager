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
#include "mm-log.h"
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
 *     ^SCFG: "Radio/Band\",("1"-"147")
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

    r = g_regex_new ("\\^SCFG:\\s*\"Radio/Band\",\\((?:\")?([0-9]*)(?:\")?-(?:\")?([0-9]*)(?:\")?.*\\)",
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
/* +CNMI test parser
 *
 * Example (PHS8):
 *   AT+CNMI=?
 *   +CNMI: (0,1,2),(0,1),(0,2),(0),(1)
 */

static GArray *
read_number_list (const gchar *str)
{
    GError *inner_error = NULL;
    GArray *out = NULL;
    GRegex *r;
    GMatchInfo *match_info;

    if (!str)
        return NULL;

    r = g_regex_new ("(\\d),?", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, str, strlen (str), 0, 0, &match_info, &inner_error);
    while (!inner_error && g_match_info_matches (match_info)) {
        guint aux;

        if (mm_get_uint_from_match_info (match_info, 1, &aux)) {
            if (!out)
                out = g_array_sized_new (FALSE, FALSE, sizeof (guint), 3);
            g_array_append_val (out, aux);
        }
        g_match_info_next (match_info, &inner_error);
    }

    if (inner_error) {
        mm_warn ("Unexpected error matching +CNMI response: '%s'", inner_error->message);
        g_error_free (inner_error);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    return out;
}

gboolean
mm_cinterion_parse_cnmi_test (const gchar *response,
                              GArray **supported_mode,
                              GArray **supported_mt,
                              GArray **supported_bm,
                              GArray **supported_ds,
                              GArray **supported_bfr,
                              GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    GError *inner_error = NULL;

    if (!response) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing response");
        return FALSE;
    }

    r = g_regex_new ("\\+CNMI:\\s*\\((.*)\\),\\((.*)\\),\\((.*)\\),\\((.*)\\),\\((.*)\\)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        if (supported_mode) {
            gchar *str;

            str = mm_get_string_unquoted_from_match_info (match_info, 1);
            *supported_mode = read_number_list (str);
            g_free (str);
        }
        if (supported_mt) {
            gchar *str;

            str = mm_get_string_unquoted_from_match_info (match_info, 2);
            *supported_mt = read_number_list (str);
            g_free (str);
        }
        if (supported_bm) {
            gchar *str;

            str = mm_get_string_unquoted_from_match_info (match_info, 3);
            *supported_bm = read_number_list (str);
            g_free (str);
        }
        if (supported_ds) {
            gchar *str;

            str = mm_get_string_unquoted_from_match_info (match_info, 4);
            *supported_ds = read_number_list (str);
            g_free (str);
        }
        if (supported_bfr) {
            gchar *str;

            str = mm_get_string_unquoted_from_match_info (match_info, 5);
            *supported_bfr = read_number_list (str);
            g_free (str);
        }
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if ((supported_mode && *supported_mode == NULL) ||
        (supported_mt   && *supported_mt == NULL) ||
        (supported_bm   && *supported_bm == NULL) ||
        (supported_ds   && *supported_ds == NULL) ||
        (supported_bfr  && *supported_bfr == NULL))
        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "Error parsing +CNMI=? response");

    if (inner_error) {
        if (supported_mode && *supported_mode) {
            g_array_unref (*supported_mode);
            *supported_mode = NULL;
        }
        if (supported_mt && *supported_mt) {
            g_array_unref (*supported_mt);
            *supported_mt = NULL;
        }
        if (supported_bm && *supported_bm) {
            g_array_unref (*supported_bm);
            *supported_bm = NULL;
        }
        if (supported_ds && *supported_ds) {
            g_array_unref (*supported_ds);
            *supported_ds = NULL;
        }
        if (supported_bfr && *supported_bfr) {
            g_array_unref (*supported_bfr);
            *supported_bfr = NULL;
        }
        g_propagate_error (error, inner_error);
        return FALSE;
    }

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

/*****************************************************************************/
/* Single ^SIND response parser */

gboolean
mm_cinterion_parse_sind_response (const gchar *response,
                                  gchar **description,
                                  guint *mode,
                                  guint *value,
                                  GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    guint errors = 0;

    if (!response) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing response");
        return FALSE;
    }

    r = g_regex_new ("\\^SIND:\\s*(.*),(\\d+),(\\d+)(\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    if (g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, NULL)) {
        if (description) {
            *description = mm_get_string_unquoted_from_match_info (match_info, 1);
            if (*description == NULL)
                errors++;
        }
        if (mode && !mm_get_uint_from_match_info (match_info, 2, mode))
            errors++;
        if (value && !mm_get_uint_from_match_info (match_info, 3, value))
            errors++;
    } else
        errors++;

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (errors > 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Failed parsing ^SIND response");
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/
/* ^SMONG response parser */

static MMModemAccessTechnology
get_access_technology_from_smong_gprs_status (guint    gprs_status,
                                              GError **error)
{
    switch (gprs_status) {
    case 0:
        return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    case 1:
    case 2:
        return MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
    case 3:
    case 4:
        return MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
    default:
        break;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_INVALID_ARGS,
                 "Couldn't get network capabilities, "
                 "unsupported GPRS status value: '%u'",
                 gprs_status);
    return MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
}

gboolean
mm_cinterion_parse_smong_response (const gchar              *response,
                                   MMModemAccessTechnology  *access_tech,
                                   GError                  **error)
{
    GError     *inner_error = NULL;
    GMatchInfo *match_info = NULL;
    GRegex     *regex;

    /* The AT^SMONG command returns a cell info table, where the second
     * column identifies the "GPRS status", which is exactly what we want.
     * So we'll try to read that second number in the values row.
     *
     * AT^SMONG
     * GPRS Monitor
     * BCCH  G  PBCCH  PAT MCC  MNC  NOM  TA      RAC    # Cell #
     * 0776  1  -      -   214   03  2    00      01
     * OK
     */
    regex = g_regex_new (".*GPRS Monitor(?:\r\n)*"
                         "BCCH\\s*G.*\\r\\n"
                         "\\s*(\\d+)\\s*(\\d+)\\s*",
                         G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                         0, NULL);
    g_assert (regex);

    if (g_regex_match_full (regex, response, strlen (response), 0, 0, &match_info, &inner_error)) {
        guint value = 0;

        if (!mm_get_uint_from_match_info (match_info, 2, &value))
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "Couldn't read 'GPRS status' field from AT^SMONG response");
        else if (access_tech)
            *access_tech = get_access_technology_from_smong_gprs_status (value, &inner_error);
    }

    g_match_info_free (match_info);
    g_regex_unref (regex);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    g_assert (access_tech != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
    return TRUE;
}
