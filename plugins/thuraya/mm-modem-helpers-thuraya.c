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
mm_thuraya_3gpp_parse_cpms_test_response (const gchar  *reply,
                                          GArray      **mem1,
                                          GArray      **mem2,
                                          GArray      **mem3,
                                          GError      **error)
{
#define N_EXPECTED_GROUPS 3

    gchar            **splitp;
    const gchar       *splita[N_EXPECTED_GROUPS];
    guint              i;
    g_auto(GStrv)      split = NULL;
    g_autoptr(GRegex)  r = NULL;
    g_autoptr(GArray)  tmp1 = NULL;
    g_autoptr(GArray)  tmp2 = NULL;
    g_autoptr(GArray)  tmp3 = NULL;

    g_assert (mem1 != NULL);
    g_assert (mem2 != NULL);
    g_assert (mem3 != NULL);

    split = g_strsplit (mm_strip_tag (reply, "+CPMS:"), " ", -1);
    if (!split) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't split +CPMS response");
        return FALSE;
    }

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
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't parse +CPMS response: invalid number of groups (%u != %u)",
                     i, N_EXPECTED_GROUPS);
        return FALSE;
    }

    r = g_regex_new ("\\s*\"([^,\\)]+)\"\\s*", 0, 0, NULL);
    g_assert (r);

    for (i = 0; i < N_EXPECTED_GROUPS; i++) {
        g_autoptr(GMatchInfo)  match_info = NULL;
        GArray                *array;

        /* We always return a valid array, even if it may be empty */
        array = g_array_new (FALSE, FALSE, sizeof (MMSmsStorage));

        /* Got a range group to match */
        if (g_regex_match (r, splita[i], 0, &match_info)) {
            while (g_match_info_matches (match_info)) {
                g_autofree gchar *str = NULL;

                str = g_match_info_fetch (match_info, 1);
                if (str) {
                    MMSmsStorage storage;

                    storage = storage_from_str (str);
                    g_array_append_val (array, storage);
                }

                g_match_info_next (match_info, NULL);
            }
        }

        if (!tmp1)
            tmp1 = array;
        else if (!tmp2)
            tmp2 = array;
        else if (!tmp3)
            tmp3 = array;
        else
            g_assert_not_reached ();
    }

    /* Only return TRUE if all sets have been parsed correctly
     * (even if the arrays may be empty) */
    if (tmp1 && tmp2 && tmp3) {
        *mem1 = g_steal_pointer (&tmp1);
        *mem2 = g_steal_pointer (&tmp2);
        *mem3 = g_steal_pointer (&tmp3);
        return TRUE;
    }

    /* Otherwise, cleanup and return FALSE */
    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "Couldn't parse +CPMS response: not all groups detected (mem1 %s, mem2 %s, mem3 %s)",
                 tmp1 ? "yes" : "no",
                 tmp2 ? "yes" : "no",
                 tmp3 ? "yes" : "no");
    return FALSE;
}
