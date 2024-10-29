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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-quectel.h"

gboolean
mm_quectel_parse_ctzu_test_response (const gchar  *response,
                                     gpointer      log_object,
                                     gboolean     *supports_disable,
                                     gboolean     *supports_enable,
                                     gboolean     *supports_enable_update_rtc,
                                     GError      **error)
{
    g_auto(GStrv)      split = NULL;
    g_autoptr(GArray)  modes = NULL;
    GError            *inner_error = NULL;
    guint              i;

    /*
     * Response may be:
     *   - +CTZU: (0,1)
     *   - +CTZU: (0,1,3)
     */

#define N_EXPECTED_GROUPS 1

    split = mm_split_string_groups (mm_strip_tag (response, "+CTZU:"));
    if (!split) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't split the +CTZU test response in groups");
        return FALSE;
    }

    if (g_strv_length (split) != N_EXPECTED_GROUPS) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Cannot parse +CTZU test response: invalid number of groups (%u != %u)",
                     g_strv_length (split), N_EXPECTED_GROUPS);
        return FALSE;
    }

    modes = mm_parse_uint_list (split[0], &inner_error);
    if (inner_error) {
        g_propagate_prefixed_error (error, inner_error, "Failed to parse integer list in +CTZU test response: ");
        return FALSE;
    }
    if (!modes) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unexpected empty integer list in +CTZU test response: ");
        return FALSE;
    }

    *supports_disable = FALSE;
    *supports_enable = FALSE;
    *supports_enable_update_rtc = FALSE;

    for (i = 0; i < modes->len; i++) {
        guint mode;

        mode = g_array_index (modes, guint, i);
        switch (mode) {
            case 0:
                *supports_disable = TRUE;
                break;
            case 1:
                *supports_enable = TRUE;
                break;
            case 3:
                *supports_enable_update_rtc = TRUE;
                break;
            default:
                mm_obj_dbg (log_object, "unknown +CTZU mode: %u", mode);
                break;
        }
    }

    return TRUE;
}

/*****************************************************************************/
/* standard firmware info
 * Format of the string is:
 * "[main version]_[modem and app version]"
 * e.g. EM05GFAR07A07M1G_01.016.01.016
 */
#define QUECTEL_STD_FIRMWARE_VERSION_SEG  2

/* Format of the string is:
 * "modem_main.modem_minor.ap_main.ap_minor"
 * e.g. 01.016.01.016
 */
#define QUECTEL_STD_MODEM_AP_FIRMWARE_VER_SEG  4
#define QUECTEL_STD_MODEM_AP_FIRMWARE_VER_LEN  13

#define QUECTEL_MAIN_VERSION_INVALID_TAG   "00"
#define QUECTEL_MINOR_VERSION_INVALID_TAG  "000"

gboolean
mm_quectel_check_standard_firmware_version_valid (const gchar *std_str)
{
    gboolean      valid = TRUE;
    g_auto(GStrv) split_std_fw = NULL;
    g_auto(GStrv) split_modem_ap_fw = NULL;
    const gchar   *modem_ap_fw;

    if (std_str) {
        split_std_fw = g_strsplit (std_str, "_", QUECTEL_STD_FIRMWARE_VERSION_SEG);
        /* Quectel standard format of the [main version]_[modem and app version]
         * Sometimes we find that the [modem and app version] query is missing by [AT+QMGR]
         * for example: we expect EM05GFAR07A07M1G_01.016.01.016,but unexpected EM05GFAR07A07M1G_01.016.00.000 was returned
         * Quectel will check for this abnormal [modem and app version] and flag it
         */
        if (g_strv_length (split_std_fw) == QUECTEL_STD_FIRMWARE_VERSION_SEG) {
            modem_ap_fw = split_std_fw[1];
            if (strlen (modem_ap_fw) == QUECTEL_STD_MODEM_AP_FIRMWARE_VER_LEN) {
                split_modem_ap_fw = g_strsplit (modem_ap_fw, ".", QUECTEL_STD_MODEM_AP_FIRMWARE_VER_SEG);

                if (g_strv_length (split_modem_ap_fw) == QUECTEL_STD_MODEM_AP_FIRMWARE_VER_SEG &&
                    !g_strcmp0 (split_modem_ap_fw[2], QUECTEL_MAIN_VERSION_INVALID_TAG) &&
                    !g_strcmp0 (split_modem_ap_fw[3], QUECTEL_MINOR_VERSION_INVALID_TAG)){
                    valid = FALSE;
                }
            }
        }
    }
    return valid;
}

gboolean
mm_quectel_get_version_from_revision (const gchar  *revision,
                                      guint        *release,
                                      guint        *minor,
                                      GError      **error)
{
    g_autoptr(GRegex) version_regex = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;

    version_regex = g_regex_new ("R(\\d+)A(\\d+)",
                                 G_REGEX_RAW | G_REGEX_OPTIMIZE,
                                 0,
                                 NULL);

    if (!g_regex_match (version_regex, revision, 0, &match_info)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Cannot parse revision version %s", revision);
        return FALSE;
    }
    if (!mm_get_uint_from_match_info (match_info, 1, release)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't get release version from revision %s", revision);
        return FALSE;
    }
    if (!mm_get_uint_from_match_info (match_info, 2, minor)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't get minor version from revision %s", revision);
        return FALSE;
    }

    return TRUE;
}

gboolean
mm_quectel_is_profile_manager_supported (const gchar *revision,
                                         guint        release,
                                         guint        minor)
{
    guint i;
    static const struct {
        const gchar *revision_prefix;
        guint minimum_release;
        guint minimum_minor;
    } profile_support_map [] = {
        {"EC25", 6, 10},
    };

    for (i = 0; i < G_N_ELEMENTS (profile_support_map); ++i) {
        if (g_str_has_prefix (revision, profile_support_map[i].revision_prefix)) {
            guint minimum_release = profile_support_map[i].minimum_release;
            guint minimum_minor = profile_support_map[i].minimum_minor;

            return ((release > minimum_release) ||
                    (release == minimum_release && minor >= minimum_minor));
        }
    }

    return TRUE;
}
