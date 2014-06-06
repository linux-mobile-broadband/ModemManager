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
 * Copyright (C) 2013 Google Inc.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers-altair-lte.h"

#define MM_ALTAIR_IMS_PDN_CID           1
#define MM_ALTAIR_INTERNET_PDN_CID      3

/*****************************************************************************/
/* Bands response parser */

GArray *
mm_altair_parse_bands_response (const gchar *response)
{
    gchar **split;
    GArray *bands;
    guint i;

    /*
     * Response is "<band>[,<band>...]"
     */
    split = g_strsplit_set (response, ",", -1);
    if (!split)
        return NULL;

    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), g_strv_length (split));

    for (i = 0; split[i]; i++) {
        guint32 band_value;
        MMModemBand band;

        band_value = (guint32)strtoul (split[i], NULL, 10);
        band = MM_MODEM_BAND_EUTRAN_I - 1 + band_value;

        /* Due to a firmware issue, the modem may incorrectly includes 0 in the
         * bands response. We thus ignore any band value outside the range of
         * E-UTRAN operating bands. */
        if (band >= MM_MODEM_BAND_EUTRAN_I && band <= MM_MODEM_BAND_EUTRAN_XLIV)
            g_array_append_val (bands, band);
    }

    g_strfreev (split);

    return bands;
}

/*****************************************************************************/
/* +CEER response parser */

gchar *
mm_altair_parse_ceer_response (const gchar *response,
                               GError **error)
{
    GRegex *r;
    GMatchInfo *match_info = NULL;
    gchar *ceer_response = NULL;


    /* First accept an empty response as the no error case. Sometimes, the only
     * respone to the AT+CEER query is an OK.
     */
    if (g_strcmp0 ("", response) == 0) {
        return g_strdup ("");
    }

    /* The response we are interested in looks so:
     * +CEER: EPS_AND_NON_EPS_SERVICES_NOT_ALLOWED
     */
    r = g_regex_new ("\\+CEER:\\s*(\\w*)?",
                     G_REGEX_RAW,
                     0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match (r, response, 0, &match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Could not parse +CEER response");
        g_match_info_free (match_info);
        g_regex_unref (r);
        return NULL;
    }

    if (g_match_info_matches (match_info)) {
        ceer_response = mm_get_string_unquoted_from_match_info (match_info, 1);
        if (!ceer_response)
            ceer_response = g_strdup ("");
    }

    g_match_info_free (match_info);
    g_regex_unref (r);
    return ceer_response;
}

/*****************************************************************************/
/* %CGINFO="cid",1 response parser */

guint
mm_altair_parse_cid (const gchar *response, GError **error)
{
    GRegex *regex;
    GMatchInfo *match_info;
    guint cid = -1;

    regex = g_regex_new ("\\%CGINFO:\\s*(\\d+)", G_REGEX_RAW, 0, NULL);
    g_assert (regex);
    if (!g_regex_match_full (regex, response, strlen (response), 0, 0, &match_info, error)) {
        g_match_info_free (match_info);
        g_regex_unref (regex);
        return -1;
    }

    if (!mm_get_uint_from_match_info (match_info, 1, &cid))
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse %%CGINFO=\"cid\",1 response");

    g_match_info_free (match_info);
    g_regex_unref (regex);
    return cid;
}

/*****************************************************************************/
/* %PCOINFO response parser */

typedef enum {
    MM_VZW_PCO_PROVISIONED = 0,
    MM_VZW_PCO_LIMIT_REACHED = 1,
    MM_VZW_PCO_OUT_OF_DATA = 3,
    MM_VZW_PCO_UNPROVISIONED = 5
} MMVzwPco;

static guint
altair_extract_vzw_pco_value (const gchar *pco_payload, GError **error)
{
    GRegex *regex;
    GMatchInfo *match_info;
    guint pco_value = -1;

    /* Extract PCO value from PCO payload.
     * The PCO value in the VZW network is after the VZW PLMN (MCC+MNC 311-480).
     */
    regex = g_regex_new ("130184(\\d+)", G_REGEX_RAW, 0, NULL);
    g_assert (regex);
    if (!g_regex_match_full (regex,
                             pco_payload,
                             strlen (pco_payload),
                             0,
                             0,
                             &match_info,
                             error)) {
        g_match_info_free (match_info);
        g_regex_unref (regex);
        return -1;
    }

    if (!g_match_info_matches (match_info) ||
        !mm_get_uint_from_match_info (match_info, 1, &pco_value))
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse PCO value from PCO payload: '%s'",
                     pco_payload);

    g_match_info_free (match_info);
    g_regex_unref (regex);

    return pco_value;
}

guint
mm_altair_parse_vendor_pco_info (const gchar *pco_info, GError **error)
{
    GRegex *regex;
    GMatchInfo *match_info;
    guint pco_value = -1;
    gint num_matches;

    if (!pco_info[0])
        /* No APNs configured, all done */
        return -1;

    /* Expected %PCOINFO response:
     *
     *     Solicited response: %PCOINFO:<mode>,<cid>[,<pcoid>[,<payload>]]
     *     Unsolicited response: %PCOINFO:<cid>,<pcoid>[,<payload>]
     */
    regex = g_regex_new ("\\%PCOINFO:(?:\\s*\\d+\\s*,)?(\\d+)\\s*(,([^,\\)]*),([0-9A-Fa-f]*))?",
                         G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                         0, NULL);
    g_assert (regex);
    if (!g_regex_match_full (regex, pco_info, strlen (pco_info), 0, 0, &match_info, error)) {
        g_match_info_free (match_info);
        g_regex_unref (regex);
        return -1;
    }

    num_matches = g_match_info_get_match_count (match_info);
    if (num_matches != 5) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse substrings, number of matches: %d",
                     num_matches);
        g_match_info_free (match_info);
        g_regex_unref (regex);
        return -1;
    }

    while (g_match_info_matches (match_info)) {
        guint pco_cid;
        gchar *pco_id;
        gchar *pco_payload;

        if (!mm_get_uint_from_match_info (match_info, 1, &pco_cid)) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Couldn't parse CID from PCO info: '%s'",
                         pco_info);
            break;
        }

        pco_id = mm_get_string_unquoted_from_match_info (match_info, 3);
        if (!pco_id) {
            g_set_error (error,
                         MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse PCO ID from PCO info: '%s'",
                         pco_info);
            break;
        }

        if (g_strcmp0 (pco_id, "FF00")) {
            g_free (pco_id);
            g_match_info_next (match_info, error);
            continue;
        }
        g_free (pco_id);

        pco_payload = mm_get_string_unquoted_from_match_info (match_info, 4);
        if (!pco_payload) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Couldn't parse PCO payload from PCO info: '%s'",
                         pco_info);
            break;
        }

        pco_value = altair_extract_vzw_pco_value (pco_payload, error);
        g_free (pco_payload);

        /* We are only interested in IMS and Internet PDN PCO. */
        if (pco_cid == MM_ALTAIR_IMS_PDN_CID || pco_cid == MM_ALTAIR_INTERNET_PDN_CID) {
            break;
        }

        pco_value = -1;
        g_match_info_next (match_info, error);
    }

    g_match_info_free (match_info);
    g_regex_unref (regex);

    return pco_value;
}
