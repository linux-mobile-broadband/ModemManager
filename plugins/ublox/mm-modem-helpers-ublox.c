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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <string.h>

#include "mm-modem-helpers.h"
#include "mm-modem-helpers-ublox.h"

/*****************************************************************************/
/* UUSBCONF? response parser */

gboolean
mm_ublox_parse_uusbconf_response (const gchar        *response,
                                  MMUbloxUsbProfile  *out_profile,
                                  GError            **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    GError *inner_error = NULL;
    MMUbloxUsbProfile profile = MM_UBLOX_USB_PROFILE_UNKNOWN;

    g_assert (out_profile != NULL);

    /* Response may be e.g.:
     * +UUSBCONF: 3,"RNDIS",,"0x1146"
     * +UUSBCONF: 2,"ECM",,"0x1143"
     * +UUSBCONF: 0,"",,"0x1141"
     *
     * Note: we don't rely on the PID; assuming future new modules will
     * have a different PID but they may keep the profile names.
     */
    r = g_regex_new ("\\+UUSBCONF: (\\d+),([^,]*),([^,]*),([^,]*)(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        gchar *profile_name;

        profile_name = mm_get_string_unquoted_from_match_info (match_info, 2);
        if (profile_name && profile_name[0]) {
            if (g_str_equal (profile_name, "RNDIS"))
                profile = MM_UBLOX_USB_PROFILE_RNDIS;
            else if (g_str_equal (profile_name, "ECM"))
                profile = MM_UBLOX_USB_PROFILE_ECM;
            else
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                           "Unknown USB profile: '%s'", profile_name);
        } else
            profile = MM_UBLOX_USB_PROFILE_BACK_COMPATIBLE;
        g_free (profile_name);
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (profile == MM_UBLOX_USB_PROFILE_UNKNOWN) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't parse profile response");
        return FALSE;
    }

    *out_profile = profile;
    return TRUE;
}

/*****************************************************************************/
/* UBMCONF? response parser */

gboolean
mm_ublox_parse_ubmconf_response (const gchar            *response,
                                 MMUbloxNetworkingMode  *out_mode,
                                 GError                **error)
{
    GRegex *r;
    GMatchInfo *match_info;
    GError *inner_error = NULL;
    MMUbloxNetworkingMode mode = MM_UBLOX_NETWORKING_MODE_UNKNOWN;

    g_assert (out_mode != NULL);

    /* Response may be e.g.:
     * +UBMCONF: 1
     * +UBMCONF: 2
     */
    r = g_regex_new ("\\+UBMCONF: (\\d+)(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        guint mode_id = 0;

        if (mm_get_uint_from_match_info (match_info, 1, &mode_id)) {
            switch (mode_id) {
            case 1:
                mode = MM_UBLOX_NETWORKING_MODE_ROUTER;
                break;
            case 2:
                mode = MM_UBLOX_NETWORKING_MODE_BRIDGE;
                break;
            default:
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                           "Unknown mode id: '%u'", mode_id);
                break;
            }
        }
    }

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (mode == MM_UBLOX_NETWORKING_MODE_UNKNOWN) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't parse networking mode response");
        return FALSE;
    }

    *out_mode = mode;
    return TRUE;
}

/*****************************************************************************/
/* UIPADDR=N response parser */

gboolean
mm_ublox_parse_uipaddr_response (const gchar  *response,
                                 guint        *out_cid,
                                 gchar       **out_if_name,
                                 gchar       **out_ipv4_address,
                                 gchar       **out_ipv4_subnet,
                                 gchar       **out_ipv6_global_address,
                                 gchar       **out_ipv6_link_local_address,
                                 GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info;
    GError     *inner_error = NULL;
    guint       cid = 0;
    gchar      *if_name = NULL;
    gchar      *ipv4_address = NULL;
    gchar      *ipv4_subnet = NULL;
    gchar      *ipv6_global_address = NULL;
    gchar      *ipv6_link_local_address = NULL;

    /* Response may be e.g.:
     * +UIPADDR: 1,"ccinet0","5.168.120.13","255.255.255.0","",""
     * +UIPADDR: 2,"ccinet1","","","2001::2:200:FF:FE00:0/64","FE80::200:FF:FE00:0/64"
     * +UIPADDR: 3,"ccinet2","5.10.100.2","255.255.255.0","2001::1:200:FF:FE00:0/64","FE80::200:FF:FE00:0/64"
     *
     * We assume only ONE line is returned; because we request +UIPADDR with a specific N CID.
     */
    r = g_regex_new ("\\+UIPADDR: (\\d+),([^,]*),([^,]*),([^,]*),([^,]*),([^,]*)(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (!g_match_info_matches (match_info)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Couldn't match +UIPADDR response");
        goto out;
    }

    if (out_cid && !mm_get_uint_from_match_info (match_info, 1, &cid)) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing cid");
        goto out;
    }

    if (out_if_name && !(if_name = mm_get_string_unquoted_from_match_info (match_info, 2))) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing interface name");
        goto out;
    }

    /* Remaining strings are optional */

    if (out_ipv4_address)
        ipv4_address = mm_get_string_unquoted_from_match_info (match_info, 3);

    if (out_ipv4_subnet)
        ipv4_subnet = mm_get_string_unquoted_from_match_info (match_info, 4);

    if (out_ipv6_global_address)
        ipv6_global_address = mm_get_string_unquoted_from_match_info (match_info, 5);

    if (out_ipv6_link_local_address)
        ipv6_link_local_address = mm_get_string_unquoted_from_match_info (match_info, 6);

out:

    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_free (if_name);
        g_free (ipv4_address);
        g_free (ipv4_subnet);
        g_free (ipv6_global_address);
        g_free (ipv6_link_local_address);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (out_cid)
        *out_cid = cid;
    if (out_if_name)
        *out_if_name = if_name;
    if (out_ipv4_address)
        *out_ipv4_address = ipv4_address;
    if (out_ipv4_subnet)
        *out_ipv4_subnet = ipv4_subnet;
    if (out_ipv6_global_address)
        *out_ipv6_global_address = ipv6_global_address;
    if (out_ipv6_link_local_address)
        *out_ipv6_link_local_address = ipv6_link_local_address;
    return TRUE;
}
