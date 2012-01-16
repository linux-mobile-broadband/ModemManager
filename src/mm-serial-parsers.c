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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <string.h>
#include <stdlib.h>

#include "mm-serial-parsers.h"
#include "mm-errors.h"
#include "mm-log.h"

/* Clean up the response by removing control characters like <CR><LF> etc */
static void
response_clean (GString *response)
{
    char *s;

    /* Ends with one or more '<CR><LF>' */
    s = response->str + response->len - 1;
    while ((s > response->str) && (*s == '\n') && (*(s - 1) == '\r')) {
        g_string_truncate (response, response->len - 2);
        s -= 2;
    }

    /* Contains duplicate '<CR><CR>' */
    s = response->str;
    while ((response->len >= 2) && (*s == '\r') && (*(s + 1) == '\r')) {
        g_string_erase (response, 0, 1);
        s = response->str;
    }

    /* Starts with one or more '<CR><LF>' */
    s = response->str;
    while ((response->len >= 2) && (*s == '\r') && (*(s + 1) == '\n')) {
        g_string_erase (response, 0, 2);
        s = response->str;
    }
}


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

    return TRUE;
}

static void
remove_matches (GRegex *r, GString *string)
{
    char *str;
    int result_len = string->len;

    str = g_regex_replace_eval (r, string->str, string->len, 0, 0,
                                remove_eval_cb, &result_len, NULL);

    g_string_truncate (string, 0);
    g_string_append_len (string, str, result_len);
    g_free (str);
}

typedef struct {
    GRegex *generic_response;
    GRegex *detailed_error;
    GRegex *cms_error;
} MMSerialParserV0;

gpointer
mm_serial_parser_v0_new (void)
{
    MMSerialParserV0 *parser;
    GRegexCompileFlags flags = G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW | G_REGEX_OPTIMIZE;

    parser = g_slice_new (MMSerialParserV0);

    parser->generic_response = g_regex_new ("(\\d)\\0?\\r$", flags, 0, NULL);
    parser->detailed_error = g_regex_new ("\\+CME ERROR:\\s*(\\d+)\\r\\n$", flags, 0, NULL);
    parser->cms_error = g_regex_new ("\\+CMS ERROR:\\s*(\\d+)\\r\\n$", flags, 0, NULL);

    return parser;
}

gboolean
mm_serial_parser_v0_parse (gpointer data,
                           GString *response,
                           GError **error)
{
    MMSerialParserV0 *parser = (MMSerialParserV0 *) data;
    GMatchInfo *match_info;
    char *str;
    GError *local_error = NULL;
    int code;
    gboolean found;

    g_return_val_if_fail (parser != NULL, FALSE);
    g_return_val_if_fail (response != NULL, FALSE);

    if (G_UNLIKELY (!response->len || !strlen (response->str)))
        return FALSE;

    found = g_regex_match_full (parser->generic_response, response->str, response->len, 0, 0, &match_info, NULL);
    if (found) {
        str = g_match_info_fetch (match_info, 1);
        if (str) {
            code = atoi (str);
            g_free (str);
        } else
            code = MM_MOBILE_ERROR_UNKNOWN;

        switch (code) {
        case 0: /* OK */
            break;
        case 1: /* CONNECT */
            break;
        case 3: /* NO CARRIER */
            local_error = mm_modem_connect_error_for_code (MM_MODEM_CONNECT_ERROR_NO_CARRIER);
            break;
        case 4: /* ERROR */
            local_error = mm_mobile_error_for_code (MM_MOBILE_ERROR_UNKNOWN);
            break;
        case 6: /* NO DIALTONE */
            local_error = mm_modem_connect_error_for_code (MM_MODEM_CONNECT_ERROR_NO_DIALTONE);
            break;
        case 7: /* BUSY */
            local_error = mm_modem_connect_error_for_code (MM_MODEM_CONNECT_ERROR_BUSY);
            break;
        case 8: /* NO ANSWER */
            local_error = mm_modem_connect_error_for_code (MM_MODEM_CONNECT_ERROR_NO_ANSWER);
            break;
        default:
            local_error = mm_mobile_error_for_code (MM_MOBILE_ERROR_UNKNOWN);
            break;
        }

        remove_matches (parser->generic_response, response);
    }

    g_match_info_free (match_info);

    if (!found) {
        found = g_regex_match_full (parser->detailed_error, response->str, response->len, 0, 0, &match_info, NULL);
        if (found) {
            str = g_match_info_fetch (match_info, 1);
            if (str) {
                code = atoi (str);
                g_free (str);
            } else
                code = MM_MOBILE_ERROR_UNKNOWN;

            local_error = mm_mobile_error_for_code (code);
        }
        g_match_info_free (match_info);

        if (!found) {
            found = g_regex_match_full (parser->cms_error, response->str, response->len, 0, 0, &match_info, NULL);
            if (found) {
                str = g_match_info_fetch (match_info, 1);
                if (str) {
                    code = atoi (str);
                    g_free (str);
                } else
                    code = MM_MSG_ERROR_UNKNOWN;

                local_error = mm_msg_error_for_code (code);
            }
            g_match_info_free (match_info);
        }
    }

    if (found)
        response_clean (response);

    if (local_error) {
        mm_dbg ("Got failure code %d: %s", local_error->code, local_error->message);
        g_propagate_error (error, local_error);
    }

    return found;
}

void
mm_serial_parser_v0_destroy (gpointer data)
{
    MMSerialParserV0 *parser = (MMSerialParserV0 *) data;

    g_return_if_fail (parser != NULL);

    g_regex_unref (parser->generic_response);
    g_regex_unref (parser->detailed_error);
    g_regex_unref (parser->cms_error);

    g_slice_free (MMSerialParserV0, data);
}

typedef struct {
    /* Regular expressions for successful replies */
    GRegex *regex_ok;
    GRegex *regex_connect;
    GRegex *regex_custom_successful;
    /* Regular expressions for error replies */
    GRegex *regex_cme_error;
    GRegex *regex_cms_error;
    GRegex *regex_cme_error_str;
    GRegex *regex_cms_error_str;
    GRegex *regex_ezx_error;
    GRegex *regex_unknown_error;
    GRegex *regex_connect_failed;
    GRegex *regex_custom_error;
} MMSerialParserV1;

gpointer
mm_serial_parser_v1_new (void)
{
    MMSerialParserV1 *parser;
    GRegexCompileFlags flags = G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW | G_REGEX_OPTIMIZE;

    parser = g_slice_new (MMSerialParserV1);

    parser->regex_ok = g_regex_new ("\\r\\nOK(\\r\\n)+$", flags, 0, NULL);
    parser->regex_connect = g_regex_new ("\\r\\nCONNECT.*\\r\\n", flags, 0, NULL);
    parser->regex_cme_error = g_regex_new ("\\r\\n\\+CME ERROR:\\s*(\\d+)\\r\\n$", flags, 0, NULL);
    parser->regex_cms_error = g_regex_new ("\\r\\n\\+CMS ERROR:\\s*(\\d+)\\r\\n$", flags, 0, NULL);
    parser->regex_cme_error_str = g_regex_new ("\\r\\n\\+CME ERROR:\\s*([^\\n\\r]+)\\r\\n$", flags, 0, NULL);
    parser->regex_cms_error_str = g_regex_new ("\\r\\n\\+CMS ERROR:\\s*([^\\n\\r]+)\\r\\n$", flags, 0, NULL);
    parser->regex_ezx_error = g_regex_new ("\\r\\n\\MODEM ERROR:\\s*(\\d+)\\r\\n$", flags, 0, NULL);
    parser->regex_unknown_error = g_regex_new ("\\r\\n(ERROR)|(COMMAND NOT SUPPORT)\\r\\n$", flags, 0, NULL);
    parser->regex_connect_failed = g_regex_new ("\\r\\n(NO CARRIER)|(BUSY)|(NO ANSWER)|(NO DIALTONE)\\r\\n$", flags, 0, NULL);

    parser->regex_custom_successful = NULL;
    parser->regex_custom_error = NULL;

    return parser;
}

void
mm_serial_parser_v1_set_custom_regex (gpointer data,
                                      GRegex *successful,
                                      GRegex *error)
{
    MMSerialParserV1 *parser = (MMSerialParserV1 *) data;

    g_return_if_fail (parser != NULL);

    if (parser->regex_custom_successful)
        g_regex_unref (parser->regex_custom_successful);
    if (parser->regex_custom_error)
        g_regex_unref (parser->regex_custom_error);

    parser->regex_custom_successful = successful ? g_regex_ref (successful) : NULL;
    parser->regex_custom_error = error ? g_regex_ref (error) : NULL;
}

gboolean
mm_serial_parser_v1_parse (gpointer data,
                           GString *response,
                           GError **error)
{
    MMSerialParserV1 *parser = (MMSerialParserV1 *) data;
    GMatchInfo *match_info;
    GError *local_error = NULL;
    gboolean found = FALSE;
    char *str = NULL;
    int code;

    g_return_val_if_fail (parser != NULL, FALSE);
    g_return_val_if_fail (response != NULL, FALSE);

    /* Skip NUL bytes if they are found leading the response */
    while (response->len > 0 && response->str[0] == '\0')
        g_string_erase (response, 0, 1);

    if (G_UNLIKELY (!response->len))
        return FALSE;

    /* First, check for successful responses */

    /* Custom successful replies first, if any */
    if (parser->regex_custom_successful) {
        found = g_regex_match_full (parser->regex_custom_successful,
                                    response->str, response->len,
                                    0, 0, NULL, NULL);
    }

    if (!found) {
        found = g_regex_match_full (parser->regex_ok,
                                    response->str, response->len,
                                    0, 0, NULL, NULL);
        if (found)
            remove_matches (parser->regex_ok, response);
        else
            found = g_regex_match_full (parser->regex_connect,
                                        response->str, response->len,
                                        0, 0, NULL, NULL);
    }

    if (found) {
        response_clean (response);
        return TRUE;
    }

    /* Now failures */

    /* Custom error matches first, if any */
    if (parser->regex_custom_error) {
        found = g_regex_match_full (parser->regex_custom_error,
                                    response->str, response->len,
                                    0, 0, &match_info, NULL);
        if (found) {
            str = g_match_info_fetch (match_info, 1);
            g_assert (str);
            local_error = mm_mobile_error_for_code (atoi (str));
            goto done;
        }
        g_match_info_free (match_info);
    }

    /* Numeric CME errors */
    found = g_regex_match_full (parser->regex_cme_error,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        str = g_match_info_fetch (match_info, 1);
        g_assert (str);
        local_error = mm_mobile_error_for_code (atoi (str));
        goto done;
    }
    g_match_info_free (match_info);

    /* Numeric CMS errors */
    found = g_regex_match_full (parser->regex_cms_error,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        str = g_match_info_fetch (match_info, 1);
        g_assert (str);
        local_error = mm_msg_error_for_code (atoi (str));
        goto done;
    }
    g_match_info_free (match_info);

    /* String CME errors */
    found = g_regex_match_full (parser->regex_cme_error_str,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        str = g_match_info_fetch (match_info, 1);
        g_assert (str);
        local_error = mm_mobile_error_for_string (str);
        goto done;
    }
    g_match_info_free (match_info);

    /* String CMS errors */
    found = g_regex_match_full (parser->regex_cms_error_str,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        str = g_match_info_fetch (match_info, 1);
        g_assert (str);
        local_error = mm_msg_error_for_string (str);
        goto done;
    }
    g_match_info_free (match_info);

    /* Motorola EZX errors */
    found = g_regex_match_full (parser->regex_ezx_error,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        str = g_match_info_fetch (match_info, 1);
        g_assert (str);
        local_error = mm_mobile_error_for_code (MM_MOBILE_ERROR_UNKNOWN);
        goto done;
    }
    g_match_info_free (match_info);

    /* Last resort; unknown error */
    found = g_regex_match_full (parser->regex_unknown_error,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        local_error = mm_mobile_error_for_code (MM_MOBILE_ERROR_UNKNOWN);
        goto done;
    }
    g_match_info_free (match_info);

    /* Connection failures */
    found = g_regex_match_full (parser->regex_connect_failed,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        str = g_match_info_fetch (match_info, 1);
        g_assert (str);

        if (!strcmp (str, "NO CARRIER"))
            code = MM_MODEM_CONNECT_ERROR_NO_CARRIER;
        else if (!strcmp (str, "BUSY"))
            code = MM_MODEM_CONNECT_ERROR_BUSY;
        else if (!strcmp (str, "NO ANSWER"))
            code = MM_MODEM_CONNECT_ERROR_NO_ANSWER;
        else if (!strcmp (str, "NO DIALTONE"))
            code = MM_MODEM_CONNECT_ERROR_NO_DIALTONE;
        else {
            /* uhm... make something up (yes, ok, lie!). */
            code = MM_MODEM_CONNECT_ERROR_NO_CARRIER;
        }

        local_error = mm_modem_connect_error_for_code (code);
    }

done:
    g_free (str);
    g_match_info_free (match_info);
    if (found)
        response_clean (response);

    if (local_error) {
        mm_dbg ("Got failure code %d: %s", local_error->code, local_error->message);
        g_propagate_error (error, local_error);
    }

    return found;
}

void
mm_serial_parser_v1_destroy (gpointer data)
{
    MMSerialParserV1 *parser = (MMSerialParserV1 *) data;

    g_return_if_fail (parser != NULL);

    g_regex_unref (parser->regex_ok);
    g_regex_unref (parser->regex_connect);
    g_regex_unref (parser->regex_cme_error);
    g_regex_unref (parser->regex_cms_error);
    g_regex_unref (parser->regex_cme_error_str);
    g_regex_unref (parser->regex_cms_error_str);
    g_regex_unref (parser->regex_ezx_error);
    g_regex_unref (parser->regex_unknown_error);
    g_regex_unref (parser->regex_connect_failed);

    if (parser->regex_custom_successful)
        g_regex_unref (parser->regex_custom_successful);
    if (parser->regex_custom_error)
        g_regex_unref (parser->regex_custom_error);

    g_slice_free (MMSerialParserV1, data);
}

typedef struct {
    gpointer v1;
    GRegex *regex_echo;
} MMSerialParserV1E1;

gpointer
mm_serial_parser_v1_e1_new (void)
{
    MMSerialParserV1E1 *parser;
    GRegexCompileFlags flags = G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW | G_REGEX_OPTIMIZE;

    parser = g_slice_new (MMSerialParserV1E1);
    parser->v1 = mm_serial_parser_v1_new ();

    /* Does not start with '<CR><LF>' and ends with '<CR>'. */
    parser->regex_echo = g_regex_new ("^(?!\\r\\n).+\\r", flags, 0, NULL);

    return parser;
}

gboolean
mm_serial_parser_v1_e1_parse (gpointer data,
                              GString *response,
                              GError **error)
{
    MMSerialParserV1E1 *parser = (MMSerialParserV1E1 *) data;
    GMatchInfo *match_info = NULL;

    /* Remove the command echo */
    if (g_regex_match_full (parser->regex_echo, response->str, response->len, 0, 0, &match_info, NULL)) {
        gchar *match = g_match_info_fetch (match_info, 0);

        g_string_erase (response, 0, strlen (match));
        g_free (match);
        g_match_info_free (match_info);
    }

    return mm_serial_parser_v1_parse (parser->v1, response, error);
}

void
mm_serial_parser_v1_e1_destroy (gpointer data)
{
    MMSerialParserV1E1 *parser = (MMSerialParserV1E1 *) data;

    g_regex_unref (parser->regex_echo);
    mm_serial_parser_v1_destroy (parser->v1);

    g_slice_free (MMSerialParserV1E1, data);
}
