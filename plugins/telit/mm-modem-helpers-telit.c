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
/* +CSIM response parser */

gint
mm_telit_parse_csim_response (const guint step,
                              const gchar *response,
                              GError **error)
{
    GRegex *r = NULL;
    GMatchInfo *match_info = NULL;
    gchar* retries_hex_str;
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
