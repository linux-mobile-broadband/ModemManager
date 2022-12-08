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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "ModemManager.h"
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>
#include "mm-errors-types.h"
#include "mm-modem-helpers-simtech.h"
#include "mm-modem-helpers.h"


/*****************************************************************************/
/* +CLCC test parser
 *
 * Example (SIM7600E):
 *   AT+CLCC=?
 *   +CLCC: (0-1)
 */

gboolean
mm_simtech_parse_clcc_test (const gchar  *response,
                            gboolean     *clcc_urcs_supported,
                            GError      **error)
{
    g_assert (response);

    response = mm_strip_tag (response, "+CLCC:");

    /* 3GPP specifies that the output of AT+CLCC=? should be just OK, so support
     * that */
    if (!response[0]) {
        *clcc_urcs_supported = FALSE;
        return TRUE;
    }

    /* As per 3GPP TS 27.007, the AT+CLCC command doesn't expect any argument,
     * as it only is designed to report the current call list, nothing else.
     * In the case of the Simtech plugin, though, we are going to support +CLCC
     * URCs that can be enabled/disabled via AT+CLCC=1/0. We therefore need to
     * detect whether this URC management is possible or not, for now with a
     * simple check looking for the specific "(0-1)" string.
     */
    if (!strncmp (response, "(0-1)", 5)) {
        *clcc_urcs_supported = TRUE;
        return TRUE;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "unexpected +CLCC test response: '%s'", response);
    return FALSE;
}

/*****************************************************************************/

GRegex *
mm_simtech_get_clcc_urc_regex (void)
{
    return g_regex_new ("\\r\\n(\\+CLCC: .*\\r\\n)+",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

gboolean
mm_simtech_parse_clcc_list (const gchar *str,
                            gpointer     log_object,
                            GList      **out_list,
                            GError     **error)
{
    /* Parse the URC contents as a plain +CLCC response, but make sure to skip first
     * EOL in the string because the plain +CLCC response would never have that.
     */
    return mm_3gpp_parse_clcc_response (mm_strip_tag (str, "\r\n"), log_object, out_list, error);
}

void
mm_simtech_call_info_list_free (GList *call_info_list)
{
    mm_3gpp_call_info_list_free (call_info_list);
}

/*****************************************************************************/

/*
 * <CR><LF>VOICE CALL: BEGIN<CR><LF>
 * <CR><LF>VOICE CALL: END: 000041<CR><LF>
 */
GRegex *
mm_simtech_get_voice_call_urc_regex (void)
{
    return g_regex_new ("\\r\\nVOICE CALL:\\s*([A-Z]+)(?::\\s*(\\d+))?\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

gboolean
mm_simtech_parse_voice_call_urc (GMatchInfo  *match_info,
                                 gboolean    *start_or_stop,
                                 guint       *duration,
                                 GError     **error)
{
    GError *inner_error = NULL;
    gchar  *str;

    str = mm_get_string_unquoted_from_match_info (match_info, 1);
    if (!str) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Couldn't read voice call URC action");
        goto out;
    }

    if (g_strcmp0 (str, "BEGIN") == 0) {
        *start_or_stop = TRUE;
        *duration = 0;
        goto out;
    }

    if (g_strcmp0 (str, "END") == 0) {
        *start_or_stop = FALSE;
        if (!mm_get_uint_from_match_info (match_info, 2, duration))
            *duration = 0;
        goto out;
    }

    inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                               "Unknown voice call URC action: %s", str);

out:
    g_free (str);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

/*
 * <CR><LF>MISSED_CALL: 11:01AM 07712345678<CR><LF>
 */
GRegex *
mm_simtech_get_missed_call_urc_regex (void)
{
    return g_regex_new ("\\r\\nMISSED_CALL:\\s*(.+)\\r\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

gboolean
mm_simtech_parse_missed_call_urc (GMatchInfo  *match_info,
                                  gchar      **details,
                                  GError     **error)
{
    gchar *str;

    str = mm_get_string_unquoted_from_match_info (match_info, 1);
    if (!str) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't read missed call URC details");
        return FALSE;
    }

    *details = str;
    return TRUE;
}

/*****************************************************************************/

/*
 * Using TWO <CR> instead of one...
 * <CR><CR><LF>+CRING: VOICE<CR><CR><LF>
 */
GRegex *
mm_simtech_get_cring_urc_regex (void)
{
    return g_regex_new ("(?:\\r)+\\n\\+CRING:\\s*(\\S+)(?:\\r)+\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

/*****************************************************************************/

/*
 * <CR><CR><LF>+RXDTMF: 8<CR><CR><LF>
 * <CR><CR><LF>+RXDTMF: *<CR><CR><LF>
 * <CR><CR><LF>+RXDTMF: 7<CR><CR><LF>
 *
 * Note! using TWO <CR> instead of one...
 */
GRegex *
mm_simtech_get_rxdtmf_urc_regex (void)
{
    return g_regex_new ("(?:\\r)+\\n\\+RXDTMF:\\s*([0-9A-D\\*\\#])(?:\\r)+\\n",
                        G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}
