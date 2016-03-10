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
 * Copyright (C) 2015 Telit.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-telit.h"


/*****************************************************************************/
/* Set current bands helpers */

void
mm_telit_get_band_flag (GArray *bands_array,
                        gint *flag2g,
                        gint *flag3g,
                        gint *flag4g)
{
    guint mask2g = 0;
    guint mask3g = 0;
    guint mask4g = 0;
    guint found4g = FALSE;
    guint i;

    for (i = 0; i < bands_array->len; i++) {
        MMModemBand band = g_array_index(bands_array, MMModemBand, i);

        if (flag2g != NULL &&
            band > MM_MODEM_BAND_UNKNOWN && band <= MM_MODEM_BAND_G850) {
            mask2g += 1 << band;
        }

        if (flag3g != NULL &&
            band >= MM_MODEM_BAND_U2100 && band <= MM_MODEM_BAND_U2600) {
            mask3g += 1 << band;
        }

         if (flag4g != NULL &&
             band >= MM_MODEM_BAND_EUTRAN_I && band <= MM_MODEM_BAND_EUTRAN_XLIV) {
             mask4g += 1 << (band - MM_MODEM_BAND_EUTRAN_I);
             found4g = TRUE;
        }
    }

    /* Get 2G flag */
    if (flag2g != NULL) {
        if (mask2g == ((1 << MM_MODEM_BAND_EGSM) + (1 << MM_MODEM_BAND_DCS)))
            *flag2g = 0;
        else if (mask2g == ((1 << MM_MODEM_BAND_EGSM) + (1 << MM_MODEM_BAND_PCS)))
            *flag2g = 1;
        else if (mask2g == ((1 << MM_MODEM_BAND_G850) + (1 << MM_MODEM_BAND_DCS)))
            *flag2g = 2;
        else if (mask2g == ((1 << MM_MODEM_BAND_G850) + (1 << MM_MODEM_BAND_PCS)))
            *flag2g = 3;
        else
            *flag2g = -1;
    }

    /* Get 3G flag */
    if (flag3g != NULL) {
        if (mask3g == (1 << MM_MODEM_BAND_U2100))
            *flag3g = 0;
        else if (mask3g == (1 << MM_MODEM_BAND_U1900))
            *flag3g = 1;
        else if (mask3g == (1 << MM_MODEM_BAND_U850))
            *flag3g = 2;
        else if (mask3g == ((1 << MM_MODEM_BAND_U2100) +
                            (1 << MM_MODEM_BAND_U1900) +
                            (1 << MM_MODEM_BAND_U850)))
            *flag3g = 3;
        else if (mask3g == ((1 << MM_MODEM_BAND_U1900) +
                            (1 << MM_MODEM_BAND_U850)))
            *flag3g = 4;
        else if (mask3g == (1 << MM_MODEM_BAND_U900))
            *flag3g = 5;
        else if (mask3g == ((1 << MM_MODEM_BAND_U2100) +
                            (1 << MM_MODEM_BAND_U900)))
            *flag3g = 6;
        else if (mask3g == (1 << MM_MODEM_BAND_U17IV))
            *flag3g = 7;
        else
            *flag3g = -1;
    }

    /* 4G flag correspond to the mask */
    if (flag4g != NULL) {
        if (found4g)
            *flag4g = mask4g;
        else
            *flag4g = -1;
    }
}

/*****************************************************************************/
/* +CSIM response parser */

gint
mm_telit_parse_csim_response (const guint step,
                              const gchar *response,
                              GError **error)
{
    GRegex *r = NULL;
    GMatchInfo *match_info = NULL;
    gchar *retries_hex_str;
    guint retries;

    r = g_regex_new ("\\+CSIM:\\s*[0-9]+,\\s*.*63C(.*)\"", G_REGEX_RAW, 0, NULL);

    if (!g_regex_match (r, response, 0, &match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse reponse '%s'", response);
        g_match_info_free (match_info);
        g_regex_unref (r);
        return -1;
    }

    if (!g_match_info_matches (match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find matches in response '%s'", response);
        g_match_info_free (match_info);
        g_regex_unref (r);
        return -1;
    }

    retries_hex_str = mm_get_string_unquoted_from_match_info (match_info, 1);
    g_assert (NULL != retries_hex_str);

    /* convert hex value to uint */
    if (sscanf (retries_hex_str, "%x", &retries) != 1) {
         g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not get retry value from match '%s'",
                     retries_hex_str);
        g_match_info_free (match_info);
        g_regex_unref (r);
        return -1;
    }

    g_free (retries_hex_str);
    g_match_info_free (match_info);
    g_regex_unref (r);

    return retries;
}

#define SUPP_BAND_RESPONSE_REGEX          "#BND:\\s*\\((?P<Bands2G>.*)\\),\\s*\\((?P<Bands3G>.*)\\)"
#define SUPP_BAND_4G_MODEM_RESPONSE_REGEX "#BND:\\s*\\((?P<Bands2G>.*)\\),\\s*\\((?P<Bands3G>.*)\\),\\s*\\((?P<Bands4G>\\d+-\\d+)\\)"
#define CURR_BAND_RESPONSE_REGEX          "#BND:\\s*(?P<Bands2G>\\d+),\\s*(?P<Bands3G>\\d+)"
#define CURR_BAND_4G_MODEM_RESPONSE_REGEX "#BND:\\s*(?P<Bands2G>\\d+),\\s*(?P<Bands3G>\\d+),\\s*(?P<Bands4G>\\d+)"

/*****************************************************************************/
/* #BND response parser
 *
 * AT#BND=?
 *      #BND: <2G band flags range>,<3G band flags range>[, <4G band flags range>]
 *
 *  where "band flags" is a list of numbers definining the supported bands.
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
 *  where "band flags" is a number definining the current bands.
 *  Note that the one Telit band flag may represent more than one MM band.
 *
 *  e.g.
 *
 *  #BND: 0,4
 *
 *  0 = 2G band flag 0 is EGSM + DCS
 *  4 = 3G band flag 4 is U1900 + U850
 *
 */
gboolean
mm_telit_parse_bnd_response (const gchar *response,
                             gboolean modem_is_2g,
                             gboolean modem_is_3g,
                             gboolean modem_is_4g,
                             MMTelitLoadBandsType band_type,
                             GArray **supported_bands,
                             GError **error)
{
    GArray *bands = NULL;
    GMatchInfo *match_info = NULL;
    GRegex *r = NULL;
    gboolean ret = FALSE;

    switch (band_type) {
        case LOAD_SUPPORTED_BANDS:
            /* Parse #BND=? response */
            if (modem_is_4g)
                r = g_regex_new (SUPP_BAND_4G_MODEM_RESPONSE_REGEX, G_REGEX_RAW, 0, NULL);
            else
                r = g_regex_new (SUPP_BAND_RESPONSE_REGEX, G_REGEX_RAW, 0, NULL);
            break;
        case LOAD_CURRENT_BANDS:
            /* Parse #BND? response */
            if (modem_is_4g)
                r = g_regex_new (CURR_BAND_4G_MODEM_RESPONSE_REGEX, G_REGEX_RAW, 0, NULL);
            else
                r = g_regex_new (CURR_BAND_RESPONSE_REGEX, G_REGEX_RAW, 0, NULL);
            break;
        default:
            break;
    }


    if (!g_regex_match (r, response, 0, &match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not parse reponse '%s'", response);
        goto end;
    }

    if (!g_match_info_matches(match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find matches in response '%s'", response);
        goto end;
    }

    bands = g_array_new (TRUE, TRUE, sizeof (MMModemBand));

    if (modem_is_2g && !mm_telit_get_2g_mm_bands (match_info, &bands, error))
        goto end;

    if (modem_is_3g && !mm_telit_get_3g_mm_bands (match_info, &bands, error))
        goto end;

    if(modem_is_4g && !mm_telit_get_4g_mm_bands (match_info, &bands, error))
        goto end;

    *supported_bands = bands;
    ret = TRUE;

end:
    if (!ret && bands != NULL)
        g_array_free (bands, TRUE);

    if(match_info)
        g_match_info_free (match_info);

    g_regex_unref (r);

    return ret;
}

gboolean
mm_telit_get_2g_mm_bands (GMatchInfo *match_info,
                          GArray **bands,
                          GError **error)
{
    GArray *flags = NULL;
    gchar *match_str = NULL;
    guint i;
    gboolean ret = TRUE;

    TelitToMMBandMap map [5] = {
        { BND_FLAG_GSM900_DCS1800, {MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_UNKNOWN} }, /* 0 */
        { BND_FLAG_GSM900_PCS1900, {MM_MODEM_BAND_EGSM, MM_MODEM_BAND_PCS, MM_MODEM_BAND_UNKNOWN} }, /* 1 */
        { BND_FLAG_GSM850_DCS1800, {MM_MODEM_BAND_DCS, MM_MODEM_BAND_G850, MM_MODEM_BAND_UNKNOWN} }, /* 2 */
        { BND_FLAG_GSM850_PCS1900, {MM_MODEM_BAND_PCS, MM_MODEM_BAND_G850, MM_MODEM_BAND_UNKNOWN} }, /* 3 */
        { BND_FLAG_UNKNOWN, {}},
    };

    match_str = g_match_info_fetch_named (match_info, "Bands2G");

    if (match_str == NULL || match_str[0] == '\0') {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find 2G band flags from response");
        ret = FALSE;
        goto end;
    }

    flags = g_array_new (FALSE, FALSE, sizeof (guint));

    if (!mm_telit_get_band_flags_from_string (match_str, &flags, error)) {
        ret = FALSE;
        goto end;
    }

    for (i = 0; i < flags->len; i++) {
        guint flag;

        flag = g_array_index (flags, guint, i);
        if (!mm_telit_update_band_array (flag, map, bands, error)) {
            ret = FALSE;
            goto end;
        }
    }

end:
    if (match_str != NULL)
        g_free (match_str);

    if (flags != NULL)
        g_array_free (flags, TRUE);

    return ret;
}

gboolean
mm_telit_get_3g_mm_bands (GMatchInfo *match_info,
                          GArray **bands,
                          GError **error)
{
    GArray *flags = NULL;
    gchar *match_str = NULL;
    guint i;
    gboolean ret = TRUE;

    TelitToMMBandMap map [] = {
        { BND_FLAG_0, { MM_MODEM_BAND_U2100, MM_MODEM_BAND_UNKNOWN} },
        { BND_FLAG_1, { MM_MODEM_BAND_U1900, MM_MODEM_BAND_UNKNOWN} },
        { BND_FLAG_2, { MM_MODEM_BAND_U850, MM_MODEM_BAND_UNKNOWN} },
        { BND_FLAG_3, { MM_MODEM_BAND_U2100, MM_MODEM_BAND_U1900, MM_MODEM_BAND_U850, MM_MODEM_BAND_UNKNOWN} },
        { BND_FLAG_4, { MM_MODEM_BAND_U1900, MM_MODEM_BAND_U850, MM_MODEM_BAND_UNKNOWN} },
        { BND_FLAG_5, { MM_MODEM_BAND_U900, MM_MODEM_BAND_UNKNOWN} },
        { BND_FLAG_6, { MM_MODEM_BAND_U2100, MM_MODEM_BAND_U900, MM_MODEM_BAND_UNKNOWN} },
        { BND_FLAG_7, { MM_MODEM_BAND_U17IV, MM_MODEM_BAND_UNKNOWN} },
        { BND_FLAG_8, { MM_MODEM_BAND_U2100, MM_MODEM_BAND_U850, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_9, { MM_MODEM_BAND_U2100, MM_MODEM_BAND_U900, MM_MODEM_BAND_U850, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_10, { MM_MODEM_BAND_U1900, MM_MODEM_BAND_U17IV, MM_MODEM_BAND_U850, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_12, { MM_MODEM_BAND_U800, MM_MODEM_BAND_UNKNOWN}},
        { BND_FLAG_13, { MM_MODEM_BAND_U1800, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_14, { MM_MODEM_BAND_U2100, MM_MODEM_BAND_U900, MM_MODEM_BAND_U17IV, MM_MODEM_BAND_U850, MM_MODEM_BAND_U800, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_15, { MM_MODEM_BAND_U2100, MM_MODEM_BAND_U900, MM_MODEM_BAND_U1800, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_16, { MM_MODEM_BAND_U900, MM_MODEM_BAND_U850, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_17, { MM_MODEM_BAND_U1900, MM_MODEM_BAND_U17IV, MM_MODEM_BAND_U850, MM_MODEM_BAND_U800, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_18, { MM_MODEM_BAND_U2100, MM_MODEM_BAND_U1900, MM_MODEM_BAND_U850, MM_MODEM_BAND_U800, MM_MODEM_BAND_UNKNOWN}},
        { BND_FLAG_19, { MM_MODEM_BAND_U1900, MM_MODEM_BAND_U800, MM_MODEM_BAND_UNKNOWN }},
        { BND_FLAG_20, { MM_MODEM_BAND_U850, MM_MODEM_BAND_U800, MM_MODEM_BAND_UNKNOWN}},
        { BND_FLAG_21, { MM_MODEM_BAND_U1900, MM_MODEM_BAND_U850, MM_MODEM_BAND_U800, MM_MODEM_BAND_UNKNOWN}},
        { BND_FLAG_UNKNOWN, {}},
    };

    match_str = g_match_info_fetch_named (match_info, "Bands3G");

    if (match_str == NULL || match_str[0] == '\0') {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find 3G band flags from response");
        ret = FALSE;
        goto end;
    }

    flags = g_array_new (FALSE, FALSE, sizeof (guint));

    if (!mm_telit_get_band_flags_from_string (match_str, &flags, error)) {
        ret = FALSE;
        goto end;
    }

    for (i = 0; i < flags->len; i++) {
        guint flag;

        flag = g_array_index (flags, guint, i);
        if (!mm_telit_update_band_array (flag, map, bands, error)) {
            ret = FALSE;
            goto end;
        }
    }

end:
    if (match_str != NULL)
        g_free (match_str);

    if (flags != NULL)
        g_array_free (flags, TRUE);

    return ret;
}

gboolean
mm_telit_get_4g_mm_bands(GMatchInfo *match_info,
                         GArray **bands,
                         GError **error)
{
    GArray *flags = NULL;
    MMModemBand band;
    gboolean ret = TRUE;
    gchar *match_str = NULL;
    guint i;
    guint value;
    gchar **tokens;

    match_str = g_match_info_fetch_named (match_info, "Bands4G");

    if (match_str == NULL || match_str[0] == '\0') {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not find 4G band flags from response");
        ret = FALSE;
        goto end;
    }

    if (strstr(match_str, "-")) {
        tokens = g_strsplit (match_str, "-", -1);
        if (tokens == NULL) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Could not get 4G band ranges from string '%s'",
                         match_str);
            ret = FALSE;
            goto end;
        }
        sscanf (tokens[1], "%d", &value);
    } else {
        sscanf (match_str, "%d", &value);
    }


    for (i = 0; value > 0; i++) {
        if (value % 2 != 0) {
            band = MM_MODEM_BAND_EUTRAN_I + i;
            g_array_append_val (*bands, band);
        }
        value = value >> 1;
    }
end:
    if (match_str != NULL)
        g_free (match_str);

    if (flags != NULL)
        g_array_free (flags, TRUE);

    return ret;
}

gboolean
mm_telit_bands_contains (GArray *mm_bands, const MMModemBand mm_band)
{
    guint i;

    for (i = 0; i < mm_bands->len; i++) {
        if (mm_band == g_array_index (mm_bands, MMModemBand, i))
            return TRUE;
    }

    return FALSE;
}

gboolean
mm_telit_update_band_array (const gint bands_flag,
                            const TelitToMMBandMap *map,
                            GArray **bands,
                            GError **error)
{
    guint i;
    guint j;

    for (i = 0; map[i].flag != BND_FLAG_UNKNOWN; i++) {
        if (bands_flag == map[i].flag) {
            for (j = 0; map[i].mm_bands[j] != MM_MODEM_BAND_UNKNOWN; j++) {
                if (!mm_telit_bands_contains (*bands, map[i].mm_bands[j])) {
                    g_array_append_val (*bands, map[i].mm_bands[j]);
                }
            }

            return TRUE;
        }
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "No MM band found for Telit #BND flag '%d'",
                 bands_flag);

    return FALSE;
}


gboolean
mm_telit_get_band_flags_from_string (const gchar *flag_str,
                                     GArray **band_flags,
                                     GError **error)
{
    gchar **range;
    gchar **tokens;
    guint flag;
    guint i;

    if (flag_str == NULL || flag_str[0] == '\0') {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "String is empty, no band flags to parse");
        return FALSE;
    }

    tokens = g_strsplit (flag_str, ",", -1);
    if (!tokens) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Could not get the list of flags");
        return FALSE;
    }

    for (i = 0; tokens[i]; i++) {
        /* check whether tokens[i] defines a
         * single band value or a range of bands */
        if (!strstr(tokens[i], "-")) {
            sscanf(tokens[i], "%d", &flag);
            g_array_append_val (*band_flags, flag);
        } else {
            gint range_start;
            gint range_end;

            range = g_strsplit(tokens[i], "-", 2);

            sscanf(range[0], "%d", &range_start);
            sscanf(range[1], "%d", &range_end);

            for (flag=range_start; flag <= range_end; flag++) {
                g_array_append_val (*band_flags, flag);
            }

            g_strfreev (range);
        }
    }

    g_strfreev (tokens);

    return TRUE;
}
