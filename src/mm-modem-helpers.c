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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include <glib.h>
#include <string.h>
#include <ctype.h>

#include "mm-errors.h"
#include "mm-modem-helpers.h"

static void
save_scan_value (GHashTable *hash, const char *key, GMatchInfo *info, guint32 num)
{
    char *quoted;
    size_t len;

    g_return_if_fail (info != NULL);

    quoted = g_match_info_fetch (info, num);
    if (!quoted)
        return;

    len = strlen (quoted);

    /* Unquote the item if needed */
    if ((len >= 2) && (quoted[0] == '"') && (quoted[len - 1] == '"')) {
        quoted[0] = ' ';
        quoted[len - 1] = ' ';
        quoted = g_strstrip (quoted);
    }

    if (!strlen (quoted)) {
        g_free (quoted);
        return;
    }

    g_hash_table_insert (hash, g_strdup (key), quoted);
}

/* If the response was successfully parsed (even if no valid entries were
 * found) the pointer array will be returned.
 */
GPtrArray *
mm_gsm_parse_scan_response (const char *reply, GError **error)
{
    /* Got valid reply */
    GPtrArray *results = NULL;
    GRegex *r;
    GMatchInfo *match_info;
    GError *err = NULL;
    gboolean umts_format = TRUE;

    g_return_val_if_fail (reply != NULL, NULL);
    if (error)
        g_return_val_if_fail (*error == NULL, NULL);

    if (!strstr (reply, "+COPS: ")) {
        g_set_error_literal (error,
                             MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Could not parse scan results.");
        return NULL;
    }

    reply = strstr (reply, "+COPS: ") + 7;

    /* Cell access technology (GSM, UTRAN, etc) got added later and not all
     * modems implement it.  Some modesm have quirks that make it hard to
     * use one regular experession for matching both pre-UMTS and UMTS
     * responses.  So try UMTS-format first and fall back to pre-UMTS if
     * we get no UMTS-formst matches.
     */

    /* Quirk: Sony-Ericsson TM-506 sometimes includes a stray ')' like so,
     *        which is what makes it hard to match both pre-UMTS and UMTS in
     *        the same regex:
     *
     *       +COPS: (2,"","T-Mobile","31026",0),(1,"AT&T","AT&T","310410"),0)
     */

    r = g_regex_new ("\\((\\d),([^,\\)]*),([^,\\)]*),([^,\\)]*)[\\)]?,(\\d)\\)", G_REGEX_UNGREEDY, 0, NULL);
    if (err) {
        g_error ("Invalid regular expression: %s", err->message);
        g_error_free (err);
        g_set_error_literal (error,
                             MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                             "Could not parse scan results.");
        return NULL;
    }

    /* If we didn't get any hits, try the pre-UMTS format match */
    if (!g_regex_match (r, reply, 0, &match_info)) {
        g_regex_unref (r);
        if (match_info) {
            g_match_info_free (match_info);
            match_info = NULL;
        }

        /* Pre-UMTS format doesn't include the cell access technology after
         * the numeric operator element.
         *
         * Ex: Motorola C-series (BUSlink SCWi275u) like so:
         *
         *       +COPS: (2,"T-Mobile","","310260"),(0,"Cingular Wireless","","310410")
         */

        /* Quirk: Some Nokia phones (N80) don't send the quotes for empty values:
         *
         *       +COPS: (2,"T - Mobile",,"31026"),(1,"Einstein PCS",,"31064"),(1,"Cingular",,"31041"),,(0,1,3),(0,2)
         */

        r = g_regex_new ("\\((\\d),([^,\\)]*),([^,\\)]*),([^\\)]*)\\)", G_REGEX_UNGREEDY, 0, NULL);
        if (err) {
            g_error ("Invalid regular expression: %s", err->message);
            g_error_free (err);
            g_set_error_literal (error,
                                 MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                 "Could not parse scan results.");
            return NULL;
        }

        g_regex_match (r, reply, 0, &match_info);
        umts_format = FALSE;
    }

    /* Parse the results */
    results = g_ptr_array_new ();
    while (g_match_info_matches (match_info)) {
        GHashTable *hash;
        char *access_tech = NULL;
        const char *tmp;
        gboolean valid = FALSE;

        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        save_scan_value (hash, MM_SCAN_TAG_STATUS, match_info, 1);
        save_scan_value (hash, MM_SCAN_TAG_OPER_LONG, match_info, 2);
        save_scan_value (hash, MM_SCAN_TAG_OPER_SHORT, match_info, 3);
        save_scan_value (hash, MM_SCAN_TAG_OPER_NUM, match_info, 4);

        /* Only try for access technology with UMTS-format matches */
        if (umts_format)
            access_tech = g_match_info_fetch (match_info, 5);
        if (access_tech && (strlen (access_tech) == 1)) {
            /* Recognized access technologies are between '0' and '6' inclusive... */
            if ((access_tech[0] >= '0') && (access_tech[0] <= '6'))
                g_hash_table_insert (hash, g_strdup (MM_SCAN_TAG_ACCESS_TECH), access_tech);
        } else
            g_free (access_tech);

        /* If the operator number isn't valid (ie, at least 5 digits),
         * ignore the scan result; it's probably the parameter stuff at the
         * end of the +COPS response.  The regex will sometimes catch this
         * but there's no good way to ignore it.
         */
        tmp = g_hash_table_lookup (hash, MM_SCAN_TAG_OPER_NUM);
        if (tmp && (strlen (tmp) >= 5)) {
            valid = TRUE;
            while (*tmp) {
                if (!isdigit (*tmp) && (*tmp != '-')) {
                    valid = FALSE;
                    break;
                }
                tmp++;
            }

            if (valid)
                g_ptr_array_add (results, hash);
        }

        if (!valid)
            g_hash_table_destroy (hash);

        g_match_info_next (match_info, NULL);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    return results;
}

void
mm_gsm_destroy_scan_data (gpointer data)
{
    GPtrArray *results = (GPtrArray *) data;

    g_ptr_array_foreach (results, (GFunc) g_hash_table_destroy, NULL);
    g_ptr_array_free (results, TRUE);
}

