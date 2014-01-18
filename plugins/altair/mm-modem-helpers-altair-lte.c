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

#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers-altair-lte.h"

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
                             error))
        return -1;

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
mm_altair_parse_vendor_pco_info (const gchar *pco_info,
                                 guint cid,
                                 GError **error)
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
    if (!g_regex_match_full (regex, pco_info, strlen (pco_info), 0, 0, &match_info, error))
        return -1;

    num_matches = g_match_info_get_match_count (match_info);
    if (num_matches != 5) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Failed to parse substrings, number of matches: %d",
                     num_matches);
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

        if (pco_cid != cid) {
            g_match_info_next (match_info, error);
            continue;
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
            g_match_info_next (match_info, error);
            continue;
        }

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
        break;
    }

    g_match_info_free (match_info);
    g_regex_unref (regex);

    return pco_value;
}
