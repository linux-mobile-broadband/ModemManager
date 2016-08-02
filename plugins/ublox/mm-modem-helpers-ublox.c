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
