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

#include "mm-error-helpers.h"
#include "mm-serial-parsers.h"
#include "mm-log-object.h"

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
    /* Regular expressions for successful replies */
    GRegex *regex_ok;
    GRegex *regex_connect;
    GRegex *regex_sms;
    GRegex *regex_custom_successful;
    /* Regular expressions for error replies */
    GRegex *regex_cme_error;
    GRegex *regex_cms_error;
    GRegex *regex_cme_error_str;
    GRegex *regex_cms_error_str;
    GRegex *regex_ezx_error;
    GRegex *regex_unknown_error;
    GRegex *regex_connect_failed;
    GRegex *regex_na;
    GRegex *regex_custom_error;
    /* User-provided parser filter */
    mm_serial_parser_v1_filter_fn filter_callback;
    gpointer                      filter_user_data;
} MMSerialParserV1;

gpointer
mm_serial_parser_v1_new (void)
{
    MMSerialParserV1 *parser;
    GRegexCompileFlags flags = G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW | G_REGEX_OPTIMIZE;

    parser = g_slice_new (MMSerialParserV1);

    parser->regex_ok = g_regex_new ("\\r\\nOK(\\r\\n)+", flags, 0, NULL);
    parser->regex_connect = g_regex_new ("\\r\\nCONNECT.*\\r\\n", flags, 0, NULL);
    parser->regex_sms = g_regex_new ("\\r\\n>\\s*$", flags, 0, NULL);
    parser->regex_cme_error = g_regex_new ("\\r\\n\\+CME ERROR:\\s*(\\d+)\\r\\n", flags, 0, NULL);
    parser->regex_cms_error = g_regex_new ("\\r\\n\\+CMS ERROR:\\s*(\\d+)\\r\\n", flags, 0, NULL);
    parser->regex_cme_error_str = g_regex_new ("\\r\\n\\+CME ERROR:\\s*([^\\n\\r]+)\\r\\n", flags, 0, NULL);
    parser->regex_cms_error_str = g_regex_new ("\\r\\n\\+CMS ERROR:\\s*([^\\n\\r]+)\\r\\n", flags, 0, NULL);
    parser->regex_ezx_error = g_regex_new ("\\r\\n\\MODEM ERROR:\\s*(\\d+)\\r\\n", flags, 0, NULL);
    parser->regex_unknown_error = g_regex_new ("\\r\\n(ERROR)|(COMMAND NOT SUPPORT)\\r\\n", flags, 0, NULL);
    parser->regex_connect_failed = g_regex_new ("\\r\\n(NO CARRIER)|(BUSY)|(NO ANSWER)|(NO DIALTONE)\\r\\n", flags, 0, NULL);
    /* Samsung Z810 may reply "NA" to report a not-available error */
    parser->regex_na = g_regex_new ("\\r\\nNA\\r\\n", flags, 0, NULL);

    parser->regex_custom_successful = NULL;
    parser->regex_custom_error = NULL;
    parser->filter_callback = NULL;
    parser->filter_user_data = NULL;

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

void
mm_serial_parser_v1_add_filter (gpointer data,
                                mm_serial_parser_v1_filter_fn callback,
                                gpointer user_data)
{
    MMSerialParserV1 *parser = (MMSerialParserV1 *) data;

    g_return_if_fail (parser != NULL);

    parser->filter_callback = callback;
    parser->filter_user_data = user_data;
}

gboolean
mm_serial_parser_v1_parse (gpointer   data,
                           GString   *response,
                           gpointer   log_object,
                           GError   **error)
{
    MMSerialParserV1 *parser = (MMSerialParserV1 *) data;
    GMatchInfo *match_info;
    GError *local_error = NULL;
    gboolean found = FALSE;
    char *str = NULL;

    g_return_val_if_fail (parser != NULL, FALSE);
    g_return_val_if_fail (response != NULL, FALSE);

    /* Skip NUL bytes if they are found leading the response */
    while (response->len > 0 && response->str[0] == '\0')
        g_string_erase (response, 0, 1);

    if (G_UNLIKELY (!response->len))
        return FALSE;

    /* First, apply custom filter if any */
    if (parser->filter_callback &&
        !parser->filter_callback (parser,
                                  parser->filter_user_data,
                                  response,
                                  &local_error)) {
        g_assert (local_error != NULL);
        mm_obj_dbg (log_object, "response filtered in serial port: %s", local_error->message);
        g_propagate_error (error, local_error);
        response_clean (response);
        return TRUE;
    }

    /* Then, check for successful responses */

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
    }

    if (!found) {
        found = g_regex_match_full (parser->regex_connect,
                                    response->str, response->len,
                                    0, 0, NULL, NULL);
    }

    if (!found) {
        found = g_regex_match_full (parser->regex_sms,
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
            local_error = mm_mobile_equipment_error_for_code (atoi (str), log_object);
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
        local_error = mm_mobile_equipment_error_for_code (atoi (str), log_object);
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
        local_error = mm_message_error_for_code (atoi (str), log_object);
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
        local_error = mm_mobile_equipment_error_for_string (str, log_object);
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
        local_error = mm_message_error_for_string (str, log_object);
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
        local_error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN, log_object);
        goto done;
    }
    g_match_info_free (match_info);

    /* Last resort; unknown error */
    found = g_regex_match_full (parser->regex_unknown_error,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        local_error = mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN, log_object);
        goto done;
    }
    g_match_info_free (match_info);

    /* Connection failures */
    found = g_regex_match_full (parser->regex_connect_failed,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        MMConnectionError code;

        str = g_match_info_fetch (match_info, 1);
        g_assert (str);

        if (!strcmp (str, "NO CARRIER"))
            code = MM_CONNECTION_ERROR_NO_CARRIER;
        else if (!strcmp (str, "BUSY"))
            code = MM_CONNECTION_ERROR_BUSY;
        else if (!strcmp (str, "NO ANSWER"))
            code = MM_CONNECTION_ERROR_NO_ANSWER;
        else if (!strcmp (str, "NO DIALTONE"))
            code = MM_CONNECTION_ERROR_NO_DIALTONE;
        else {
            /* uhm... make something up (yes, ok, lie!). */
            code = MM_CONNECTION_ERROR_NO_CARRIER;
        }

        local_error = mm_connection_error_for_code (code, log_object);
        goto done;
    }
    g_match_info_free (match_info);

    /* NA error */
    found = g_regex_match_full (parser->regex_na,
                                response->str, response->len,
                                0, 0, &match_info, NULL);
    if (found) {
        /* Assume NA means 'Not Allowed' :) */
        local_error = g_error_new (MM_MOBILE_EQUIPMENT_ERROR,
                                   MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED,
                                   "Not Allowed");
        goto done;
    }

done:
    g_free (str);
    g_match_info_free (match_info);
    if (found)
        response_clean (response);

    if (local_error) {
        mm_obj_dbg (log_object, "operation failure: %d (%s)", local_error->code, local_error->message);
        g_propagate_error (error, local_error);
    }

    return found;
}

gboolean
mm_serial_parser_v1_is_known_error (const GError *error)
{
    /* Need to return TRUE for the kind of errors that this parser may set */
    return (error->domain == MM_MOBILE_EQUIPMENT_ERROR ||
            error->domain == MM_CONNECTION_ERROR ||
            error->domain == MM_MESSAGE_ERROR ||
            g_error_matches (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_PARSE_FAILED));
}

void
mm_serial_parser_v1_destroy (gpointer data)
{
    MMSerialParserV1 *parser = (MMSerialParserV1 *) data;

    g_return_if_fail (parser != NULL);

    g_regex_unref (parser->regex_ok);
    g_regex_unref (parser->regex_connect);
    g_regex_unref (parser->regex_sms);
    g_regex_unref (parser->regex_cme_error);
    g_regex_unref (parser->regex_cms_error);
    g_regex_unref (parser->regex_cme_error_str);
    g_regex_unref (parser->regex_cms_error_str);
    g_regex_unref (parser->regex_ezx_error);
    g_regex_unref (parser->regex_unknown_error);
    g_regex_unref (parser->regex_connect_failed);
    g_regex_unref (parser->regex_na);

    if (parser->regex_custom_successful)
        g_regex_unref (parser->regex_custom_successful);
    if (parser->regex_custom_error)
        g_regex_unref (parser->regex_custom_error);

    g_slice_free (MMSerialParserV1, data);
}
