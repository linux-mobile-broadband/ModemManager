/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "mm-util.h"

static gboolean
remove_eval_cb (const GMatchInfo *match_info,
                GString *result,
                gpointer user_data)
{
    int *result_len = (int *) user_data;
    int start;
    int end;

    if (g_match_info_fetch_pos  (match_info, 0, &start, &end))
        *result_len -= (end - start);

    return FALSE;
}

void
mm_util_strip_string (GString *string,
                      GRegex *regex,
                      MMUtilStripFn callback,
                      gpointer user_data)
{
    GMatchInfo *match_info;
    gboolean matches;
    char *str;

    g_return_if_fail (string != NULL);
    g_return_if_fail (regex != NULL);

    matches = g_regex_match_full (regex, string->str, string->len, 0, 0, &match_info, NULL);
    if (callback) {
        while (g_match_info_matches (match_info)) {
            str = g_match_info_fetch (match_info, 1);
            callback (str, user_data);
            g_free (str);

            g_match_info_next (match_info, NULL);
        }
    }

    g_match_info_free (match_info);

    if (matches) {
        /* Remove matches */
        int result_len = string->len;

        str = g_regex_replace_eval (regex, string->str, string->len, 0, 0,
                                    remove_eval_cb, &result_len, NULL);

        g_string_truncate (string, 0);
        g_string_append_len (string, str, result_len);
        g_free (str);
    }
}
