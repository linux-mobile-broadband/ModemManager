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
/* ^NDISSTAT /  ^NDISSTATQRY response parser */

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

    if (!response ||
        !(g_str_has_prefix (response, "^NDISSTAT:") ||
          g_str_has_prefix (response, "^NDISSTATQRY:"))) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing ^NDISSTAT / ^NDISSTATQRY prefix");
        return FALSE;
    }

    *ipv4_available = FALSE;
    *ipv6_available = FALSE;

    /* The response maybe as:
     *     ^NDISSTAT: 1,,,IPV4
     *     ^NDISSTAT: 0,33,,IPV6
     *     ^NDISSTATQRY: 1,,,IPV4
     *     ^NDISSTATQRY: 0,33,,IPV6
     *     OK
     *
     * Or, in newer firmwares:
     *     ^NDISSTATQRY:0,,,"IPV4",0,,,"IPV6"
     *     OK
     */
    r = g_regex_new ("\\^NDISSTAT(?:QRY)?:\\s*(\\d),([^,]*),([^,]*),([^,\\r\\n]*)(?:\\r\\n)?"
                     "(?:\\^NDISSTAT:|\\^NDISSTATQRY:)?\\s*,?(\\d)?,?([^,]*)?,?([^,]*)?,?([^,\\r\\n]*)?(?:\\r\\n)?",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        guint ip_type_field = 4;

        /* IPv4 and IPv6 are fields 4 and (if available) 8 */

        while (!inner_error && ip_type_field <= 8) {
            gchar *ip_type_str;
            guint connected;

            ip_type_str = mm_get_string_unquoted_from_match_info (match_info, ip_type_field);
            if (!ip_type_str)
                break;

            if (!mm_get_uint_from_match_info (match_info, (ip_type_field - 3), &connected) ||
                (connected != 0 && connected != 1)) {
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_FAILED,
                                           "Couldn't parse ^NDISSTAT / ^NDISSTATQRY fields");
            } else if (g_ascii_strcasecmp (ip_type_str, "IPV4") == 0) {
                *ipv4_available = TRUE;
                *ipv4_connected = (gboolean)connected;
            } else if (g_ascii_strcasecmp (ip_type_str, "IPV6") == 0) {
                *ipv6_available = TRUE;
                *ipv6_connected = (gboolean)connected;
            }

            g_free (ip_type_str);
            ip_type_field += 4;
        }
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (!ipv4_available && !ipv6_available) {
        inner_error = g_error_new (MM_CORE_ERROR,
                                   MM_CORE_ERROR_FAILED,
                                   "Couldn't find IPv4 or IPv6 info in ^NDISSTAT / ^NDISSTATQRY response");
    }

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/
/* ^SYSINFO response parser */

gboolean
mm_huawei_parse_sysinfo_response (const char *reply,
                                  guint *out_srv_status,
                                  guint *out_srv_domain,
                                  guint *out_roam_status,
                                  guint *out_sys_mode,
                                  guint *out_sim_state,
                                  gboolean *out_sys_submode_valid,
                                  guint *out_sys_submode,
                                  GError **error)
{
    gboolean matched;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;

    g_assert (out_srv_status != NULL);
    g_assert (out_srv_domain != NULL);
    g_assert (out_roam_status != NULL);
    g_assert (out_sys_mode != NULL);
    g_assert (out_sim_state != NULL);
    g_assert (out_sys_submode_valid != NULL);
    g_assert (out_sys_submode != NULL);

    /* Format:
     *
     * ^SYSINFO: <srv_status>,<srv_domain>,<roam_status>,<sys_mode>,<sim_state>[,<reserved>,<sys_submode>]
     */

    /* Can't just use \d here since sometimes you get "^SYSINFO:2,1,0,3,1,,3" */
    r = g_regex_new ("\\^SYSINFO:\\s*(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),?(\\d+)?,?(\\d+)?$", 0, 0, NULL);
    g_assert (r != NULL);

    matched = g_regex_match_full (r, reply, -1, 0, 0, &match_info, &match_error);
    if (!matched) {
        if (match_error) {
            g_propagate_error (error, match_error);
            g_prefix_error (error, "Could not parse ^SYSINFO results: ");
        } else {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't match ^SYSINFO reply");
        }
    } else {
        mm_get_uint_from_match_info (match_info, 1, out_srv_status);
        mm_get_uint_from_match_info (match_info, 2, out_srv_domain);
        mm_get_uint_from_match_info (match_info, 3, out_roam_status);
        mm_get_uint_from_match_info (match_info, 4, out_sys_mode);
        mm_get_uint_from_match_info (match_info, 5, out_sim_state);

        /* Remember that g_match_info_get_match_count() includes match #0 */
        if (g_match_info_get_match_count (match_info) >= 8) {
            *out_sys_submode_valid = TRUE;
            mm_get_uint_from_match_info (match_info, 7, out_sys_submode);
        }
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);
    return matched;
}

/*****************************************************************************/
/* ^SYSINFOEX response parser */

gboolean
mm_huawei_parse_sysinfoex_response (const char *reply,
                                    guint *out_srv_status,
                                    guint *out_srv_domain,
                                    guint *out_roam_status,
                                    guint *out_sim_state,
                                    guint *out_sys_mode,
                                    guint *out_sys_submode,
                                    GError **error)
{
    gboolean matched;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;

    g_assert (out_srv_status != NULL);
    g_assert (out_srv_domain != NULL);
    g_assert (out_roam_status != NULL);
    g_assert (out_sim_state != NULL);
    g_assert (out_sys_mode != NULL);
    g_assert (out_sys_submode != NULL);

    /* Format:
     *
     * ^SYSINFOEX: <srv_status>,<srv_domain>,<roam_status>,<sim_state>,<reserved>,<sysmode>,<sysmode_name>,<submode>,<submode_name>
     *
     * <sysmode_name> and <submode_name> may not be quoted on some Huawei modems (e.g. E303).
     */

    /* ^SYSINFOEX:2,3,0,1,,3,"WCDMA",41,"HSPA+" */

    r = g_regex_new ("\\^SYSINFOEX:\\s*(\\d+),(\\d+),(\\d+),(\\d+),?(\\d*),(\\d+),\"?([^\"]*)\"?,(\\d+),\"?([^\"]*)\"?$", 0, 0, NULL);
    g_assert (r != NULL);

    matched = g_regex_match_full (r, reply, -1, 0, 0, &match_info, &match_error);
    if (!matched) {
        if (match_error) {
            g_propagate_error (error, match_error);
            g_prefix_error (error, "Could not parse ^SYSINFOEX results: ");
        } else {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't match ^SYSINFOEX reply");
        }
    } else {
        mm_get_uint_from_match_info (match_info, 1, out_srv_status);
        mm_get_uint_from_match_info (match_info, 2, out_srv_domain);
        mm_get_uint_from_match_info (match_info, 3, out_roam_status);
        mm_get_uint_from_match_info (match_info, 4, out_sim_state);

        /* We just ignore the sysmode and submode name strings */
        mm_get_uint_from_match_info (match_info, 6, out_sys_mode);
        mm_get_uint_from_match_info (match_info, 8, out_sys_submode);
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);
    return matched;
}
