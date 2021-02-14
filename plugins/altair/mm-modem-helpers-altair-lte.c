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
        band = MM_MODEM_BAND_EUTRAN_1 - 1 + band_value;

        /* Due to a firmware issue, the modem may incorrectly includes 0 in the
         * bands response. We thus ignore any band value outside the range of
         * E-UTRAN operating bands. */
        if (band >= MM_MODEM_BAND_EUTRAN_1 && band <= MM_MODEM_BAND_EUTRAN_44)
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
    g_autoptr(GRegex) r = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;
    gchar *ceer_response = NULL;


    /* First accept an empty response as the no error case. Sometimes, the only
     * response to the AT+CEER query is an OK.
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
        return NULL;
    }

    if (g_match_info_matches (match_info)) {
        ceer_response = mm_get_string_unquoted_from_match_info (match_info, 1);
        if (!ceer_response)
            ceer_response = g_strdup ("");
    }

    return ceer_response;
}

/*****************************************************************************/
/* %CGINFO="cid",1 response parser */

gint
mm_altair_parse_cid (const gchar *response, GError **error)
{
    g_autoptr(GRegex) regex = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;
    guint cid = -1;

    regex = g_regex_new ("\\%CGINFO:\\s*(\\d+)", G_REGEX_RAW, 0, NULL);
    g_assert (regex);
    if (!g_regex_match_full (regex, response, strlen (response), 0, 0, &match_info, error))
        return -1;

    if (!mm_get_uint_from_match_info (match_info, 1, &cid))
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse %%CGINFO=\"cid\",1 response");

    return cid;
}

/*****************************************************************************/
/* %PCOINFO response parser */

MMPco *
mm_altair_parse_vendor_pco_info (const gchar *pco_info, GError **error)
{
    g_autoptr(GRegex) regex = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;
    MMPco *pco = NULL;
    gint num_matches;

    if (!pco_info || !pco_info[0]) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "No PCO info given");
        return NULL;
    }

    /* Expected %PCOINFO response:
     *
     *     Solicited response: %PCOINFO:<mode>,<cid>[,<pcoid>[,<payload>]]
     *     Unsolicited response: %PCOINFO:<cid>,<pcoid>[,<payload>]
     */
    regex = g_regex_new ("\\%PCOINFO:(?:\\s*\\d+\\s*,)?(\\d+)\\s*(,([^,\\)]*),([0-9A-Fa-f]*))?",
                         G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                         0, NULL);
    g_assert (regex);

    if (!g_regex_match_full (regex, pco_info, strlen (pco_info), 0, 0, &match_info, error))
        return NULL;

    num_matches = g_match_info_get_match_count (match_info);
    if (num_matches != 5) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse substrings, number of matches: %d",
                     num_matches);
        return NULL;
    }

    while (g_match_info_matches (match_info)) {
        guint              pco_cid;
        g_autofree gchar  *pco_id = NULL;
        g_autofree gchar  *pco_payload = NULL;
        g_autofree guint8 *pco_payload_bytes = NULL;
        gsize              pco_payload_bytes_len;
        guint8             pco_prefix[6];
        GByteArray        *pco_raw;
        gsize              pco_raw_len;

        if (!mm_get_uint_from_match_info (match_info, 1, &pco_cid)) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse CID from PCO info: '%s'", pco_info);
            break;
        }

        /* We are only interested in IMS and Internet PDN PCO. */
        if (pco_cid != MM_ALTAIR_IMS_PDN_CID && pco_cid != MM_ALTAIR_INTERNET_PDN_CID) {
            g_match_info_next (match_info, error);
            continue;
        }

        pco_id = mm_get_string_unquoted_from_match_info (match_info, 3);
        if (!pco_id) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse PCO ID from PCO info: '%s'", pco_info);
            break;
        }

        if (g_strcmp0 (pco_id, "FF00")) {
            g_match_info_next (match_info, error);
            continue;
        }

        pco_payload = mm_get_string_unquoted_from_match_info (match_info, 4);
        if (!pco_payload) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse PCO payload from PCO info: '%s'", pco_info);
            break;
        }

        pco_payload_bytes = mm_utils_hexstr2bin (pco_payload, -1, &pco_payload_bytes_len, error);
        if (!pco_payload_bytes) {
            g_prefix_error (error, "Invalid PCO payload from PCO info '%s': ", pco_info);
            break;
        }

        /* Protocol Configuration Options (PCO) is an information element with an
         * identifier (IEI) 0x27 and contains between 3 and 253 octets. See 3GPP TS
         * 24.008 for more details on PCO.
         *
         * NOTE: The standard uses one-based indexing, but to better correlate to the
         *       code, zero-based indexing is used in the description hereinafter.
         *
         *   Octet  | Value
         *  --------+--------------------------------------------
         *     0    | PCO IEI (= 0x27)
         *     1    | Length of PCO contents (= total length - 2)
         *     2    | bit 7      : ext
         *          | bit 6 to 3 : spare (= 0b0000)
         *          | bit 2 to 0 : Configuration protocol
         *   3 to 4 | Element 1 ID
         *     5    | Length of element 1 contents
         *   6 to m | Element 1 contents
         *    ...   |
         */
        pco_raw_len = sizeof (pco_prefix) + pco_payload_bytes_len;
        pco_prefix[0] = 0x27;
        pco_prefix[1] = pco_raw_len - 2;
        pco_prefix[2] = 0x80;
        /* Verizon uses element ID 0xFF00 for carrier-specific PCO content. */
        pco_prefix[3] = 0xFF;
        pco_prefix[4] = 0x00;
        pco_prefix[5] = pco_payload_bytes_len;

        pco_raw = g_byte_array_sized_new (pco_raw_len);
        g_byte_array_append (pco_raw, pco_prefix, sizeof (pco_prefix));
        g_byte_array_append (pco_raw, pco_payload_bytes, pco_payload_bytes_len);

        pco = mm_pco_new ();
        mm_pco_set_session_id (pco, pco_cid);
        mm_pco_set_complete (pco, TRUE);
        mm_pco_set_data (pco, pco_raw->data, pco_raw->len);
        break;
    }

    return pco;
}
