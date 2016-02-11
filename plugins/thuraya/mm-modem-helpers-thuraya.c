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
 * Copyright (C) 2016 Thomas Sailer <t.sailer@alumni.ethz.ch>
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
#include "mm-modem-helpers-thuraya.h"

/*************************************************************************/

static MMSmsStorage
storage_from_str (const gchar *str)
{
    if (g_str_equal (str, "SM"))
        return MM_SMS_STORAGE_SM;
    if (g_str_equal (str, "ME"))
        return MM_SMS_STORAGE_ME;
    if (g_str_equal (str, "MT"))
        return MM_SMS_STORAGE_MT;
    if (g_str_equal (str, "SR"))
        return MM_SMS_STORAGE_SR;
    if (g_str_equal (str, "BM"))
        return MM_SMS_STORAGE_BM;
    if (g_str_equal (str, "TA"))
        return MM_SMS_STORAGE_TA;
    return MM_SMS_STORAGE_UNKNOWN;
}

gboolean
mm_thuraya_3gpp_parse_cpms_test_response (const gchar *reply,
                                          GArray **mem1,
                                          GArray **mem2,
                                          GArray **mem3)
{
#define N_EXPECTED_GROUPS 3

    GRegex *r;
    gchar **split;
    gchar **splitp;
    const gchar *splita[N_EXPECTED_GROUPS];
    guint i;
    GArray *tmp1 = NULL;
    GArray *tmp2 = NULL;
    GArray *tmp3 = NULL;

    g_assert (mem1 != NULL);
    g_assert (mem2 != NULL);
    g_assert (mem3 != NULL);

    split = g_strsplit (mm_strip_tag (reply, "+CPMS:"), " ", -1);
    if (!split)
        return FALSE;

    /* remove empty strings, and count non-empty strings */
    i = 0;
    for (splitp = split; *splitp; ++splitp) {
        if (!**splitp)
            continue;
        if (i < N_EXPECTED_GROUPS)
            splita[i] = *splitp;
        ++i;
    }

    if (i != N_EXPECTED_GROUPS) {
        mm_warn ("Cannot parse +CPMS test response: invalid number of groups (%u != %u)",
                 i, N_EXPECTED_GROUPS);
        g_strfreev (split);
        return FALSE;
    }

    r = g_regex_new ("\\s*\"([^,\\)]+)\"\\s*", 0, 0, NULL);
    g_assert (r);

    for (i = 0; i < N_EXPECTED_GROUPS; i++) {
        GMatchInfo *match_info = NULL;
        GArray *array;

        /* We always return a valid array, even if it may be empty */
        array = g_array_new (FALSE, FALSE, sizeof (MMSmsStorage));

        /* Got a range group to match */
        if (g_regex_match_full (r, splita[i], strlen (splita[i]), 0, 0, &match_info, NULL)) {
            while (g_match_info_matches (match_info)) {
                gchar *str;

                str = g_match_info_fetch (match_info, 1);
                if (str) {
                    MMSmsStorage storage;

                    storage = storage_from_str (str);
                    g_array_append_val (array, storage);
                    g_free (str);
                }

                g_match_info_next (match_info, NULL);
            }
        }
        g_match_info_free (match_info);

        if (!tmp1)
            tmp1 = array;
        else if (!tmp2)
            tmp2 = array;
        else if (!tmp3)
            tmp3 = array;
        else
            g_assert_not_reached ();
    }

    g_strfreev (split);
    g_regex_unref (r);

    g_warn_if_fail (tmp1 != NULL);
    g_warn_if_fail (tmp2 != NULL);
    g_warn_if_fail (tmp3 != NULL);

    /* Only return TRUE if all sets have been parsed correctly
     * (even if the arrays may be empty) */
    if (tmp1 && tmp2 && tmp3) {
        *mem1 = tmp1;
        *mem2 = tmp2;
        *mem3 = tmp3;
        return TRUE;
    }

    /* Otherwise, cleanup and return FALSE */
    if (tmp1)
        g_array_unref (tmp1);
    if (tmp2)
        g_array_unref (tmp2);
    if (tmp3)
        g_array_unref (tmp3);
    return FALSE;
}

/*****************************************************************************/
