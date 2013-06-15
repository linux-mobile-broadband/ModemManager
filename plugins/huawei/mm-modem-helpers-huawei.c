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
 * Copyright (C) 2013 Huawei Technologies Co., Ltd
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers-huawei.h"

/*****************************************************************************/
/* ^NDISSTATQRY response parser */

gboolean
mm_huawei_parse_ndisstatqry_response (const gchar *response,
                                      gboolean *ipv4_available,
                                      gboolean *ipv4_connected,
                                      gboolean *ipv6_available,
                                      gboolean *ipv6_connected,
                                      GError **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    GError *inner_error = NULL;

    if (!response || !g_str_has_prefix (response, "^NDISSTATQRY:")) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing ^NDISSTATQRY prefix");
        return FALSE;
    }

    *ipv4_available = FALSE;
    *ipv6_available = FALSE;

    /* The response maybe as:
     * <CR><LF>^NDISSTATQRY: 1,,,IPV4<CR><LF>^NDISSTATQRY: 0,33,,IPV6<CR><LF>
     * <CR><LF><CR><LF>OK<CR><LF>
     * So we have to split the status for IPv4 and IPv6. For now, we only care
     * about IPv4.
     */
    r = g_regex_new ("\\^NDISSTATQRY:\\s*(\\d),(.*),(.*),(.*)(\\r\\n)?",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    while (!inner_error && g_match_info_matches (match_info)) {
        gchar *ip_type_str;
        guint connected;

        /* Read values */
        ip_type_str = mm_get_string_unquoted_from_match_info (match_info, 4);
        if (!ip_type_str ||
            !mm_get_uint_from_match_info (match_info, 1, &connected) ||
            (connected != 0 && connected != 1)) {
          inner_error = g_error_new (MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't parse ^NDISSTATQRY fields");
        } else if (g_ascii_strcasecmp (ip_type_str, "IPV4") == 0) {
            *ipv4_available = TRUE;
            *ipv4_connected = (gboolean)connected;
        } else if (g_ascii_strcasecmp (ip_type_str, "IPV6") == 0) {
            *ipv6_available = TRUE;
            *ipv6_connected = (gboolean)connected;
        }

        g_free (ip_type_str);
        if (!inner_error)
            g_match_info_next (match_info, &inner_error);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (!ipv4_available && !ipv6_available) {
        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "Couldn't find IPv4 or IPv6 info in ^NDISSTATQRY response");
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}
