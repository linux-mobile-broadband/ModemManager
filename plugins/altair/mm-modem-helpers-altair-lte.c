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
