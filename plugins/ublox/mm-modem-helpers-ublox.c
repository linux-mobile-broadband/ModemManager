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

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-ublox.h"

/*****************************************************************************/
/* +UPINCNT response parser */

gboolean
mm_ublox_parse_upincnt_response (const gchar  *response,
                                 guint        *out_pin_attempts,
                                 guint        *out_pin2_attempts,
                                 guint        *out_puk_attempts,
                                 guint        *out_puk2_attempts,
                                 GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info;
    GError     *inner_error = NULL;
    guint       pin_attempts = 0;
    guint       pin2_attempts = 0;
    guint       puk_attempts = 0;
    guint       puk2_attempts = 0;
    gboolean    success = TRUE;

    g_assert (out_pin_attempts);
    g_assert (out_pin2_attempts);
    g_assert (out_puk_attempts);
    g_assert (out_puk2_attempts);

    /* Response may be e.g.:
     * +UPINCNT: 3,3,10,10
     */
    r = g_regex_new ("\\+UPINCNT: (\\d+),(\\d+),(\\d+),(\\d+)(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        if (!mm_get_uint_from_match_info (match_info, 1, &pin_attempts)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                       "Couldn't parse PIN attempts");
            goto out;
        }
        if (!mm_get_uint_from_match_info (match_info, 2, &pin2_attempts)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                       "Couldn't parse PIN2 attempts");
            goto out;
        }
        if (!mm_get_uint_from_match_info (match_info, 3, &puk_attempts)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                       "Couldn't parse PUK attempts");
            goto out;
        }
        if (!mm_get_uint_from_match_info (match_info, 4, &puk2_attempts)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                       "Couldn't parse PUK2 attempts");
            goto out;
        }
        success = TRUE;
    }

out:

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (!success) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't parse +UPINCNT response: '%s'", response);
        return FALSE;
    }

    *out_pin_attempts  = pin_attempts;
    *out_pin2_attempts = pin2_attempts;
    *out_puk_attempts  = puk_attempts;
    *out_puk2_attempts = puk2_attempts;
    return TRUE;
}

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

/*****************************************************************************/
/* CFUN? response parser */

gboolean
mm_ublox_parse_cfun_response (const gchar        *response,
                              MMModemPowerState  *out_state,
                              GError            **error)
{
    guint state;

    if (!mm_3gpp_parse_cfun_query_response (response, &state, error))
        return FALSE;

    switch (state) {
    case 1:
        *out_state = MM_MODEM_POWER_STATE_ON;
        return TRUE;
    case 0:
        /* minimum functionality */
    case 4:
        /* airplane mode */
    case 19:
        /* minimum functionality with SIM deactivated */
        *out_state = MM_MODEM_POWER_STATE_LOW;
        return TRUE;
    default:
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unknown +CFUN state: %u", state);
        return FALSE;
    }
}

/*****************************************************************************/
/* URAT=? response parser */

/* Index of the array is the ublox-specific value */
static const MMModemMode ublox_combinations[] = {
    ( MM_MODEM_MODE_2G ),
    ( MM_MODEM_MODE_2G | MM_MODEM_MODE_3G ),
    (                    MM_MODEM_MODE_3G ),
    (                                       MM_MODEM_MODE_4G ),
    ( MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G ),
    ( MM_MODEM_MODE_2G |                    MM_MODEM_MODE_4G ),
    (                    MM_MODEM_MODE_3G | MM_MODEM_MODE_4G ),
};

GArray *
mm_ublox_parse_urat_test_response (const gchar  *response,
                                   gpointer      log_object,
                                   GError      **error)
{
    GArray *combinations = NULL;
    GArray *selected = NULL;
    GArray *preferred = NULL;
    gchar **split;
    guint   split_len;
    GError *inner_error = NULL;
    guint   i;

    /*
     * E.g.:
     *  AT+URAT=?
     *  +URAT: (0-6),(0,2,3)
     */
    response = mm_strip_tag (response, "+URAT:");
    split = mm_split_string_groups (response);
    split_len = g_strv_length (split);
    if (split_len > 2 || split_len < 1) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Unexpected number of groups in +URAT=? response: %u", g_strv_length (split));
        goto out;
    }

    /* The selected list must have values */
    selected = mm_parse_uint_list (split[0], &inner_error);
    if (inner_error)
        goto out;

    if (!selected) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "No selected RAT values given in +URAT=? response");
        goto out;
    }

    /* For our purposes, the preferred list may be empty */
    preferred = mm_parse_uint_list (split[1], &inner_error);
    if (inner_error)
        goto out;

    /* Build array of combinations */
    combinations = g_array_new (FALSE, FALSE, sizeof (MMModemModeCombination));

    for (i = 0; i < selected->len; i++) {
        guint                  selected_value;
        MMModemModeCombination combination;
        guint                  j;

        selected_value = g_array_index (selected, guint, i);
        if (selected_value >= G_N_ELEMENTS (ublox_combinations)) {
            mm_obj_warn (log_object, "unexpected AcT value: %u", selected_value);
            continue;
        }

        /* Combination without any preferred */
        combination.allowed = ublox_combinations[selected_value];
        combination.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, combination);

        if (mm_count_bits_set (combination.allowed) == 1)
            continue;

        if (!preferred)
            continue;

        for (j = 0; j < preferred->len; j++) {
            guint preferred_value;

            preferred_value = g_array_index (preferred, guint, j);
            if (preferred_value >= G_N_ELEMENTS (ublox_combinations)) {
                mm_obj_warn (log_object, "unexpected AcT preferred value: %u", preferred_value);
                continue;
            }
            combination.preferred = ublox_combinations[preferred_value];
            if (mm_count_bits_set (combination.preferred) != 1) {
                mm_obj_warn (log_object, "AcT preferred value should be a single AcT: %u", preferred_value);
                continue;
            }
            if (!(combination.allowed & combination.preferred))
                continue;
            g_array_append_val (combinations, combination);
        }
    }

    if (combinations->len == 0) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "No combinations built from +URAT=? response");
        goto out;
    }

out:
    g_strfreev (split);
    if (selected)
        g_array_unref (selected);
    if (preferred)
        g_array_unref (preferred);

    if (inner_error) {
        if (combinations)
            g_array_unref (combinations);
        g_propagate_error (error, inner_error);
        return NULL;
    }

    return combinations;
}

typedef struct {
    const gchar          *model;
    SettingsUpdateMethod  method;
    FeatureSupport        uact;
    FeatureSupport        ubandsel;
    MMModemMode           mode;
    MMModemBand           bands_2g[4];
    MMModemBand           bands_3g[6];
    MMModemBand           bands_4g[12];
} BandConfiguration;

static const BandConfiguration band_configuration[] = {
    {
        .model    = "SARA-G300",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G,
        .bands_2g = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS }
    },
    {
        .model    = "SARA-G310",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS }
    },
     {
        .model    = "SARA-G340",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G,
        .bands_2g = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS }
    },
    {
        .model    = "SARA-G350",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS }
    },
    {
        .model    = "SARA-G450",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS }
    },
    {
        .model    = "LISA-U200",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_6, MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8,
                      MM_MODEM_BAND_UTRAN_4, MM_MODEM_BAND_UTRAN_2, MM_MODEM_BAND_UTRAN_1 }
    },
    {
        .model    = "LISA-U201",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_6, MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8,
                      MM_MODEM_BAND_UTRAN_2, MM_MODEM_BAND_UTRAN_1 }
    },
    {
        .model    = "LISA-U230",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_6, MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8,
                      MM_MODEM_BAND_UTRAN_4, MM_MODEM_BAND_UTRAN_2, MM_MODEM_BAND_UTRAN_1 }
    },
    {
        .model    = "LISA-U260",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_2 }
    },
    {
        .model    = "LISA-U270",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_1 }
    },
    {
        .model    = "SARA-U201",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_6, MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8,
                      MM_MODEM_BAND_UTRAN_2, MM_MODEM_BAND_UTRAN_1 }
    },
    {
        .model    = "SARA-U260",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_2 }
    },
    {
        .model    = "SARA-U270",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .bands_2g = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_1 }
    },
    {
        .model    = "SARA-U280",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_3G,
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_2 }
    },
    {
        .model    = "MPCI-L201",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_2 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_13, MM_MODEM_BAND_EUTRAN_17 }
    },
    {
        .model    = "MPCI-L200",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8,  MM_MODEM_BAND_UTRAN_4,
                      MM_MODEM_BAND_UTRAN_2, MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2, MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_17 }
    },
    {
        .model    = "MPCI-L210",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5,  MM_MODEM_BAND_UTRAN_8,  MM_MODEM_BAND_UTRAN_2,
                      MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1, MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_20 }
    },
    {
        .model    = "MPCI-L220",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_3g = { MM_MODEM_BAND_UTRAN_5,  MM_MODEM_BAND_UTRAN_8,  MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1, MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_19 }
    },
    {
        .model    = "MPCI-L280",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5,  MM_MODEM_BAND_UTRAN_8,  MM_MODEM_BAND_UTRAN_2,
                      MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1, MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_28 }
    },
    {
        .model    = "TOBY-L200",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_4,
                      MM_MODEM_BAND_UTRAN_2, MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2, MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_17 }
    },
    {
        .model    = "TOBY-L201",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_2 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_13, MM_MODEM_BAND_EUTRAN_17 }
    },
    {
        .model    = "TOBY-L210",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_2,
                      MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_20 }
    },
    {
        .model    = "TOBY-L220",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_2,
                      MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_19 }
    },
    {
        .model    = "TOBY-L280",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_2,
                      MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_28 }
    },
    {
        .model    = "TOBY-L4006",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_SUPPORTED,
        .ubandsel = FEATURE_UNSUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_4, MM_MODEM_BAND_UTRAN_2 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13,
                      MM_MODEM_BAND_EUTRAN_29 }
    },
    {
        .model    = "TOBY-L4106",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_SUPPORTED,
        .ubandsel = FEATURE_UNSUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1, MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_7,
                      MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_38 }
    },
    {
        .model    = "TOBY-L4206",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_SUPPORTED,
        .ubandsel = FEATURE_UNSUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1, MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_9,
                      MM_MODEM_BAND_EUTRAN_19, MM_MODEM_BAND_EUTRAN_28 }
    },
    {
        .model    = "TOBY-L4906",
        .method   = SETTINGS_UPDATE_METHOD_CFUN,
        .uact     = FEATURE_SUPPORTED,
        .ubandsel = FEATURE_UNSUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1, MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_39,
                      MM_MODEM_BAND_EUTRAN_40, MM_MODEM_BAND_EUTRAN_41 }
    },
    {
        .model    = "TOBY-R200",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_8, MM_MODEM_BAND_UTRAN_2,
                      MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2, MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_12 }
    },
    {
        .model    = "TOBY-R202",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_2 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2, MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_12 }
    },
    {
        .model    = "LARA-R202",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_3g = { MM_MODEM_BAND_UTRAN_5, MM_MODEM_BAND_UTRAN_2 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2, MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_12 }
    },
    {
        .model    = "LARA-R203",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2, MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_12 }
    },
    {
        .model    = "LARA-R204",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_13 }
    },
    {
        .model    = "LARA-R211",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_20 }
    },
    {
        .model    = "LARA-R280",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .bands_3g = { MM_MODEM_BAND_UTRAN_1 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_28 }
    },
    {
        .model    = "LARA-R3121",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_3, MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_20 }
    },
    {
        .model    = "SARA-N200",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_8 }
    },
    {
        .model    = "SARA-N201",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_5 }
    },
    {
        .model    = "SARA-N210",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_20 }
    },
    {
        .model    = "SARA-N211",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_8, MM_MODEM_BAND_EUTRAN_20 }
    },
    {
        .model    = "SARA-N280",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_28 }
    },
    {
        .model    = "SARA-R410M-52B",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_5,
                      MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13 }
    },
    {
        .model    = "SARA-R410M-02B",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_2, MM_MODEM_BAND_EUTRAN_3,
                      MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_5, MM_MODEM_BAND_EUTRAN_8,
                      MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13, MM_MODEM_BAND_EUTRAN_19,
                      MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_28, MM_MODEM_BAND_EUTRAN_39 }
    },
    {
        .model    = "SARA-R412M-02B",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G,
        .bands_2g = { MM_MODEM_BAND_G850, MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_3,
                      MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,  MM_MODEM_BAND_EUTRAN_8,
                      MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13, MM_MODEM_BAND_EUTRAN_19,
                      MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_28, MM_MODEM_BAND_EUTRAN_39 }
    },
    {
        .model    = "SARA-N410-02B",
        .method   = SETTINGS_UPDATE_METHOD_COPS,
        .uact     = FEATURE_UNSUPPORTED,
        .ubandsel = FEATURE_SUPPORTED,
        .mode     = MM_MODEM_MODE_4G,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_3,
                      MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,  MM_MODEM_BAND_EUTRAN_8,
                      MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13, MM_MODEM_BAND_EUTRAN_19,
                      MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_28 }
    },
};

gboolean
mm_ublox_get_support_config (const gchar         *model,
                             UbloxSupportConfig  *config,
                             GError             **error)
{
    guint i;

    if (!model) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Support configuration unknown for unknown model");
        return FALSE;
    }

    for (i = 0; i < G_N_ELEMENTS (band_configuration); i++) {
        /* NOTE: matching by prefix! */
        if (g_str_has_prefix (model, band_configuration[i].model)) {
            config->loaded   = TRUE;
            config->method   = band_configuration[i].method;
            config->uact     = band_configuration[i].uact;
            config->ubandsel = band_configuration[i].ubandsel;
            return TRUE;
        }
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "No support configuration found for modem: %s", model);
    return FALSE;
}

/*****************************************************************************/
/* Supported modes loading */

static MMModemMode
supported_modes_per_model (const gchar *model)
{
    MMModemMode mode;
    guint       i;

    mode = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G;

    if (model) {
        for (i = 0; i < G_N_ELEMENTS (band_configuration); i++)
            if (g_str_has_prefix (model, band_configuration[i].model)) {
                mode = band_configuration[i].mode;
                return mode;;
            }
    }

    return mode;
}

GArray *
mm_ublox_filter_supported_modes (const gchar  *model,
                                 GArray       *combinations,
                                 gpointer      logger,
                                 GError      **error)
{
    MMModemModeCombination mode;
    GArray *all;
    GArray *filtered;

    /* Model not specified? */
    if (!model)
        return combinations;

    /* AT+URAT=? lies; we need an extra per-device filtering, thanks u-blox.
     * Don't know all PIDs for all devices, so model string based filtering... */

    mode.allowed   = supported_modes_per_model (model);
    mode.preferred = MM_MODEM_MODE_NONE;

    /* Nothing filtered? */
    if (mode.allowed == supported_modes_per_model (NULL))
        return combinations;

    all = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
    g_array_append_val (all, mode);
    filtered = mm_filter_supported_modes (all, combinations, logger);
    g_array_unref (all);
    g_array_unref (combinations);

    /* Error if nothing left */
    if (filtered->len == 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "No valid mode combinations built after filtering (model %s)", model);
        g_array_unref (filtered);
        return NULL;
    }

    return filtered;
}

/*****************************************************************************/
/* Supported bands loading */

GArray *
mm_ublox_get_supported_bands (const gchar  *model,
                              gpointer      log_object,
                              GError      **error)
{
    MMModemMode  mode;
    GArray      *bands;
    guint        i, j;

    mode  = supported_modes_per_model (model);
    bands = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

    for (i = 0; i < G_N_ELEMENTS (band_configuration); i++) {
        if (g_str_has_prefix (model, band_configuration[i].model)) {
            mm_obj_dbg (log_object, "known supported bands found for model: %s", band_configuration[i].model);
            break;
        }
    }

    if (i == G_N_ELEMENTS (band_configuration)) {
        mm_obj_warn (log_object, "unknown model name given when looking for supported bands: %s", model);
        return NULL;
    }

    mode = band_configuration[i].mode;

    if (mode & MM_MODEM_MODE_2G) {
        for (j = 0; j < G_N_ELEMENTS (band_configuration[i].bands_2g) && band_configuration[i].bands_2g[j]; j++) {
            bands = g_array_append_val (bands, band_configuration[i].bands_2g[j]);
        }
    }

    if (mode & MM_MODEM_MODE_3G) {
        for (j = 0; j < G_N_ELEMENTS (band_configuration[i].bands_3g) && band_configuration[i].bands_3g[j]; j++) {
            bands = g_array_append_val (bands, band_configuration[i].bands_3g[j]);
        }
    }

    if (mode & MM_MODEM_MODE_4G) {
        for (j = 0; j < G_N_ELEMENTS (band_configuration[i].bands_4g) && band_configuration[i].bands_4g[j]; j++) {
            bands = g_array_append_val (bands, band_configuration[i].bands_4g[j]);
        }
    }

    if (bands->len == 0) {
        g_array_unref (bands);
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "No valid supported bands loaded");
        return NULL;
    }

    return bands;
}

typedef struct {
    guint       num;
    MMModemBand band[4];
} NumToBand;

/* 2G GSM Band Frequencies */
static const NumToBand num_bands_2g [] = {
    { .num =  850, .band = { MM_MODEM_BAND_G850 } },
    { .num =  900, .band = { MM_MODEM_BAND_EGSM } },
    { .num = 1900, .band = { MM_MODEM_BAND_PCS  } },
    { .num = 1800, .band = { MM_MODEM_BAND_DCS  } },
};

/* 3G UMTS Band Frequencies */
static const NumToBand num_bands_3g [] = {
    { .num =  800, .band = { MM_MODEM_BAND_UTRAN_6 } },
    { .num =  850, .band = { MM_MODEM_BAND_UTRAN_5 } },
    { .num =  900, .band = { MM_MODEM_BAND_UTRAN_8 } },
    { .num = 1700, .band = { MM_MODEM_BAND_UTRAN_4 } },
    { .num = 1900, .band = { MM_MODEM_BAND_UTRAN_2 } },
    { .num = 2100, .band = { MM_MODEM_BAND_UTRAN_1 } },
};

/* 4G LTE Band Frequencies */
static const NumToBand num_bands_4g [] = {
    { .num =  700, .band = { MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13, MM_MODEM_BAND_EUTRAN_17 } },
    { .num =  800, .band = { MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_27 } },
    { .num =  850, .band = { MM_MODEM_BAND_EUTRAN_5, MM_MODEM_BAND_EUTRAN_18, MM_MODEM_BAND_EUTRAN_19, MM_MODEM_BAND_EUTRAN_26 } },
    { .num =  900, .band = { MM_MODEM_BAND_EUTRAN_8 } },
    { .num = 1700, .band = { MM_MODEM_BAND_EUTRAN_4, MM_MODEM_BAND_EUTRAN_10 } },
    { .num = 1800, .band = { MM_MODEM_BAND_EUTRAN_3 } },
    { .num = 1900, .band = { MM_MODEM_BAND_EUTRAN_2, MM_MODEM_BAND_EUTRAN_39 } },
    { .num = 2100, .band = { MM_MODEM_BAND_EUTRAN_1 } },
    { .num = 2300, .band = { MM_MODEM_BAND_EUTRAN_40 } },
    { .num = 2500, .band = { MM_MODEM_BAND_EUTRAN_41 } },
    { .num = 2600, .band = { MM_MODEM_BAND_EUTRAN_7, MM_MODEM_BAND_EUTRAN_38 } },
};
/*****************************************************************************/
/* +UBANDSEL? response parser */

static MMModemBand
num_to_band_2g (guint num)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (num_bands_2g); i++) {
        if (num == num_bands_2g[i].num)
            return num_bands_2g[i].band[0];
    }
    return MM_MODEM_BAND_UNKNOWN;
}

static MMModemBand
num_to_band_3g (guint num)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (num_bands_3g); i++) {
        if (num == num_bands_3g[i].num)
            return num_bands_3g[i].band[0];
    }
    return MM_MODEM_BAND_UNKNOWN;
}

static guint
band_to_num (MMModemBand band)
{
    guint i, j;

    /* Search 2G list */
    for (i = 0; i < G_N_ELEMENTS (num_bands_2g); i++) {
        for (j = 0; j < G_N_ELEMENTS (num_bands_2g[i].band) && num_bands_2g[i].band[j]; j++) {
            if (band == num_bands_2g[i].band[j])
                return num_bands_2g[i].num;
        }
    }

    /* Search 3G list */
    for (i = 0; i < G_N_ELEMENTS (num_bands_3g); i++) {
        for (j = 0; j < G_N_ELEMENTS (num_bands_3g[i].band) && num_bands_3g[i].band[j]; j++) {
            if (band == num_bands_3g[i].band[j])
                return num_bands_3g[i].num;
        }
    }

    /* Search 4G list */
    for (i = 0; i < G_N_ELEMENTS (num_bands_4g); i++) {
        for (j = 0; j < G_N_ELEMENTS (num_bands_4g[i].band) && num_bands_4g[i].band[j]; j++) {
            if (band == num_bands_4g[i].band[j])
                return num_bands_4g[i].num;
        }
    }

    /* Should never happen */
    return 0;
}

static void
append_bands (GArray      *bands,
              guint        ubandsel_value,
              MMModemMode  mode,
              const gchar *model,
              gpointer     log_object)
{
    guint i, j, k, x;
    MMModemBand band;

    /* Find Modem Model Index in band_configuration */
    for (i = 0; i < G_N_ELEMENTS (band_configuration); i++) {
        if (g_str_has_prefix (model, band_configuration[i].model)) {
            mm_obj_dbg (log_object, "known bands found for model: %s", band_configuration[i].model);
            break;
        }
    }

    if (i == G_N_ELEMENTS (band_configuration)) {
        mm_obj_warn (log_object, "unknown model name given when looking for bands: %s", model);
        return;
    }

    if (mode & MM_MODEM_MODE_2G) {
        band = num_to_band_2g (ubandsel_value);
        if (band != MM_MODEM_BAND_UNKNOWN) {
            for (x = 0; x < G_N_ELEMENTS (band_configuration[i].bands_2g); x++) {
                if (band_configuration[i].bands_2g[x] == band) {
                    g_array_append_val (bands, band);
                    break;
                }
            }
        }
    }

    if (mode & MM_MODEM_MODE_3G) {
        band = num_to_band_3g (ubandsel_value);
        if (band != MM_MODEM_BAND_UNKNOWN) {
            for (x = 0; x < G_N_ELEMENTS (band_configuration[i].bands_3g); x++) {
                if (band_configuration[i].bands_3g[x] == band) {
                    g_array_append_val (bands, band);
                    break;
                }
            }
        }
    }

    /* Note: The weird code segment below is to separate out specific LTE bands since
     * UBANDSEL? reports back the frequency of the band and not the band itself.
     */

    if (mode & MM_MODEM_MODE_4G) {
        for (j = 0; j < G_N_ELEMENTS (num_bands_4g); j++) {
            if (ubandsel_value == num_bands_4g[j].num) {
                for (k = 0; k < G_N_ELEMENTS (num_bands_4g[j].band); k++) {
                    band = num_bands_4g[j].band[k];
                    if (band != MM_MODEM_BAND_UNKNOWN) {
                        for (x = 0; x < G_N_ELEMENTS (band_configuration[i].bands_4g); x++) {
                            if (band_configuration[i].bands_4g[x] == band) {
                                g_array_append_val (bands, band);
                                break;
                            }
                        }
                    }
                }
                break;
            }
        }
    }
}

GArray *
mm_ublox_parse_ubandsel_response (const gchar  *response,
                                  const gchar  *model,
                                  gpointer      log_object,
                                  GError      **error)
{
    GArray      *array_values = NULL;
    GArray      *array = NULL;
    gchar       *dupstr = NULL;
    GError      *inner_error = NULL;
    guint        i;
    MMModemMode  mode;

    if (!g_str_has_prefix (response, "+UBANDSEL")) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Couldn't parse +UBANDSEL response: '%s'", response);
        goto out;
    }

    /* Response may be e.g.:
     *  +UBANDSEL: 850,900,1800,1900
     */
    dupstr = g_strchomp (g_strdup (mm_strip_tag (response, "+UBANDSEL:")));

    array_values = mm_parse_uint_list (dupstr, &inner_error);
    if (!array_values)
        goto out;

    /* Convert list of ubandsel numbers to MMModemBand values */
    mode = supported_modes_per_model (model);
    array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
    for (i = 0; i < array_values->len; i++)
        append_bands (array, g_array_index (array_values, guint, i), mode, model, log_object);

    if (!array->len) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "No known band selection values matched in +UBANDSEL response: '%s'", response);
        goto out;
    }

out:
    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_clear_pointer (&array, g_array_unref);
    }
    g_clear_pointer (&array_values, g_array_unref);
    g_free (dupstr);
    return array;
}

/*****************************************************************************/
/* UBANDSEL=X command builder */

static gint
ubandsel_num_cmp (const guint *a, const guint *b)
{
    return (*a - *b);
}

gchar *
mm_ublox_build_ubandsel_set_command (GArray       *bands,
                                     const gchar  *model,
                                     GError      **error)
{
    GString *command = NULL;
    GArray  *ubandsel_nums;
    guint    num;
    guint    i, j, k;

    if (bands->len == 1 && g_array_index (bands, MMModemBand, 0) == MM_MODEM_BAND_ANY)
        return g_strdup ("+UBANDSEL=0");

    for (i = 0; i < G_N_ELEMENTS (band_configuration); i++) {
        if (g_str_has_prefix (model, band_configuration[i].model))
            break;
    }

    if (i == G_N_ELEMENTS (band_configuration)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unknown modem model %s", model);
        return NULL;
    }

    ubandsel_nums = g_array_sized_new (FALSE, FALSE, sizeof (guint), bands->len);

    for (j = 0; j < bands->len; j++) {
        MMModemBand band;
        gboolean   found = FALSE;

        band = g_array_index (bands, MMModemBand, j);

        /* Check to see if band is supported by the model */
        for (k = 0; !found && k < G_N_ELEMENTS (band_configuration[i].bands_2g) && band_configuration[i].bands_2g[k]; k++) {
            if (band == band_configuration[i].bands_2g[k])
                found = TRUE;
        }

        for (k = 0; !found && k < G_N_ELEMENTS (band_configuration[i].bands_3g) && band_configuration[i].bands_3g[k]; k++) {
            if (band == band_configuration[i].bands_3g[k])
                found = TRUE;
        }

        for (k = 0; !found && k < G_N_ELEMENTS (band_configuration[i].bands_4g) && band_configuration[i].bands_4g[k]; k++) {
            if (band == band_configuration[i].bands_4g[k])
                found = TRUE;
        }

        if (found) {
            num = band_to_num (band);
            g_assert (num != 0);
            g_array_append_val (ubandsel_nums, num);
        }
    }

    if (ubandsel_nums->len == 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Given band combination is unsupported");
        g_array_unref (ubandsel_nums);
        return NULL;
    }

    if (ubandsel_nums->len > 1)
        g_array_sort (ubandsel_nums, (GCompareFunc) ubandsel_num_cmp);

    /* Build command */
    command = g_string_new ("+UBANDSEL=");
    for (i = 0; i < ubandsel_nums->len; i++)
        g_string_append_printf (command, "%s%u", i == 0 ? "" : ",", g_array_index (ubandsel_nums, guint, i));

    return g_string_free (command, FALSE);
}

/*****************************************************************************/
/* Get mode to apply when ANY */

MMModemMode
mm_ublox_get_modem_mode_any (const GArray *combinations)
{
    guint       i;
    MMModemMode any = MM_MODEM_MODE_NONE;
    guint       any_bits_set = 0;

    for (i = 0; i < combinations->len; i++) {
        MMModemModeCombination *combination;
        guint bits_set;

        combination = &g_array_index (combinations, MMModemModeCombination, i);
        if (combination->preferred != MM_MODEM_MODE_NONE)
            continue;
        bits_set = mm_count_bits_set (combination->allowed);
        if (bits_set > any_bits_set) {
            any_bits_set = bits_set;
            any = combination->allowed;
        }
    }

    /* If combinations were processed via mm_ublox_parse_urat_test_response(),
     * we're sure that there will be at least one combination with preferred
     * 'none', so there must be some valid combination as result */
    g_assert (any != MM_MODEM_MODE_NONE);
    return any;
}

/*****************************************************************************/
/* UACT common config */

typedef struct {
    guint       num;
    MMModemBand band;
} UactBandConfig;

static const UactBandConfig uact_band_config[] = {
    /* GSM bands */
    { .num =  900, .band = MM_MODEM_BAND_EGSM },
    { .num = 1800, .band = MM_MODEM_BAND_DCS  },
    { .num = 1900, .band = MM_MODEM_BAND_PCS  },
    { .num =  850, .band = MM_MODEM_BAND_G850 },
    { .num =  450, .band = MM_MODEM_BAND_G450 },
    { .num =  480, .band = MM_MODEM_BAND_G480 },
    { .num =  750, .band = MM_MODEM_BAND_G750 },
    { .num =  380, .band = MM_MODEM_BAND_G380 },
    { .num =  410, .band = MM_MODEM_BAND_G410 },
    { .num =  710, .band = MM_MODEM_BAND_G710 },
    { .num =  810, .band = MM_MODEM_BAND_G810 },
    /* UMTS bands */
    { .num =    1, .band = MM_MODEM_BAND_UTRAN_1  },
    { .num =    2, .band = MM_MODEM_BAND_UTRAN_2  },
    { .num =    3, .band = MM_MODEM_BAND_UTRAN_3  },
    { .num =    4, .band = MM_MODEM_BAND_UTRAN_4  },
    { .num =    5, .band = MM_MODEM_BAND_UTRAN_5  },
    { .num =    6, .band = MM_MODEM_BAND_UTRAN_6  },
    { .num =    7, .band = MM_MODEM_BAND_UTRAN_7  },
    { .num =    8, .band = MM_MODEM_BAND_UTRAN_8  },
    { .num =    9, .band = MM_MODEM_BAND_UTRAN_9  },
    { .num =   10, .band = MM_MODEM_BAND_UTRAN_10 },
    { .num =   11, .band = MM_MODEM_BAND_UTRAN_11 },
    { .num =   12, .band = MM_MODEM_BAND_UTRAN_12 },
    { .num =   13, .band = MM_MODEM_BAND_UTRAN_13 },
    { .num =   14, .band = MM_MODEM_BAND_UTRAN_14 },
    { .num =   19, .band = MM_MODEM_BAND_UTRAN_19 },
    { .num =   20, .band = MM_MODEM_BAND_UTRAN_20 },
    { .num =   21, .band = MM_MODEM_BAND_UTRAN_21 },
    { .num =   22, .band = MM_MODEM_BAND_UTRAN_22 },
    { .num =   25, .band = MM_MODEM_BAND_UTRAN_25 },
    /* LTE bands */
    { .num =  101, .band = MM_MODEM_BAND_EUTRAN_1  },
    { .num =  102, .band = MM_MODEM_BAND_EUTRAN_2  },
    { .num =  103, .band = MM_MODEM_BAND_EUTRAN_3  },
    { .num =  104, .band = MM_MODEM_BAND_EUTRAN_4  },
    { .num =  105, .band = MM_MODEM_BAND_EUTRAN_5  },
    { .num =  106, .band = MM_MODEM_BAND_EUTRAN_6  },
    { .num =  107, .band = MM_MODEM_BAND_EUTRAN_7  },
    { .num =  108, .band = MM_MODEM_BAND_EUTRAN_8  },
    { .num =  109, .band = MM_MODEM_BAND_EUTRAN_9  },
    { .num =  110, .band = MM_MODEM_BAND_EUTRAN_10 },
    { .num =  111, .band = MM_MODEM_BAND_EUTRAN_11 },
    { .num =  112, .band = MM_MODEM_BAND_EUTRAN_12 },
    { .num =  113, .band = MM_MODEM_BAND_EUTRAN_13 },
    { .num =  114, .band = MM_MODEM_BAND_EUTRAN_14 },
    { .num =  117, .band = MM_MODEM_BAND_EUTRAN_17 },
    { .num =  118, .band = MM_MODEM_BAND_EUTRAN_18 },
    { .num =  119, .band = MM_MODEM_BAND_EUTRAN_19 },
    { .num =  120, .band = MM_MODEM_BAND_EUTRAN_20 },
    { .num =  121, .band = MM_MODEM_BAND_EUTRAN_21 },
    { .num =  122, .band = MM_MODEM_BAND_EUTRAN_22 },
    { .num =  123, .band = MM_MODEM_BAND_EUTRAN_23 },
    { .num =  124, .band = MM_MODEM_BAND_EUTRAN_24 },
    { .num =  125, .band = MM_MODEM_BAND_EUTRAN_25 },
    { .num =  126, .band = MM_MODEM_BAND_EUTRAN_26 },
    { .num =  127, .band = MM_MODEM_BAND_EUTRAN_27 },
    { .num =  128, .band = MM_MODEM_BAND_EUTRAN_28 },
    { .num =  129, .band = MM_MODEM_BAND_EUTRAN_29 },
    { .num =  130, .band = MM_MODEM_BAND_EUTRAN_30 },
    { .num =  131, .band = MM_MODEM_BAND_EUTRAN_31 },
    { .num =  132, .band = MM_MODEM_BAND_EUTRAN_32 },
    { .num =  133, .band = MM_MODEM_BAND_EUTRAN_33 },
    { .num =  134, .band = MM_MODEM_BAND_EUTRAN_34 },
    { .num =  135, .band = MM_MODEM_BAND_EUTRAN_35 },
    { .num =  136, .band = MM_MODEM_BAND_EUTRAN_36 },
    { .num =  137, .band = MM_MODEM_BAND_EUTRAN_37 },
    { .num =  138, .band = MM_MODEM_BAND_EUTRAN_38 },
    { .num =  139, .band = MM_MODEM_BAND_EUTRAN_39 },
    { .num =  140, .band = MM_MODEM_BAND_EUTRAN_40 },
    { .num =  141, .band = MM_MODEM_BAND_EUTRAN_41 },
    { .num =  142, .band = MM_MODEM_BAND_EUTRAN_42 },
    { .num =  143, .band = MM_MODEM_BAND_EUTRAN_43 },
    { .num =  144, .band = MM_MODEM_BAND_EUTRAN_44 },
    { .num =  145, .band = MM_MODEM_BAND_EUTRAN_45 },
    { .num =  146, .band = MM_MODEM_BAND_EUTRAN_46 },
    { .num =  147, .band = MM_MODEM_BAND_EUTRAN_47 },
    { .num =  148, .band = MM_MODEM_BAND_EUTRAN_48 },
};

static MMModemBand
uact_num_to_band (guint num)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (uact_band_config); i++) {
        if (num == uact_band_config[i].num)
            return uact_band_config[i].band;
    }
    return MM_MODEM_BAND_UNKNOWN;
}

static guint
uact_band_to_num (MMModemBand band)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (uact_band_config); i++) {
        if (band == uact_band_config[i].band)
            return uact_band_config[i].num;
    }
    return 0;
}

/*****************************************************************************/
/* UACT? response parser */

static GArray *
uact_num_array_to_band_array (GArray *nums)
{
    GArray *bands = NULL;
    guint   i;

    if (!nums)
        return NULL;

    bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), nums->len);
    for (i = 0; i < nums->len; i++) {
        MMModemBand band;

        band = uact_num_to_band (g_array_index (nums, guint, i));
        g_array_append_val (bands, band);
    }

    return bands;
}

GArray *
mm_ublox_parse_uact_response (const gchar  *response,
                              GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info;
    GError     *inner_error = NULL;
    GArray     *nums = NULL;
    GArray     *bands = NULL;

    /*
     * AT+UACT?
     * +UACT: ,,,900,1800,1,8,101,103,107,108,120,138
     */
    r = g_regex_new ("\\+UACT: ([^,]*),([^,]*),([^,]*),(.*)(?:\\r\\n)?",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        gchar *bandstr;

        bandstr = mm_get_string_unquoted_from_match_info (match_info, 4);
        nums = mm_parse_uint_list (bandstr, &inner_error);
        g_free (bandstr);
    }

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return NULL;
    }

    /* Convert to MMModemBand values */
    if (nums) {
        bands = uact_num_array_to_band_array (nums);
        g_array_unref (nums);
    }

    if (!bands)
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "No known band selection values matched in +UACT response: '%s'", response);

    return bands;
}

/*****************************************************************************/
/* UACT=? response parser */

static GArray *
parse_bands_from_string (const gchar *str,
                         const gchar *group,
                         gpointer     log_object)
{
    GArray *bands = NULL;
    GError *inner_error = NULL;
    GArray *nums;

    nums = mm_parse_uint_list (str, &inner_error);
    if (nums) {
        gchar *tmpstr;

        bands = uact_num_array_to_band_array (nums);
        tmpstr = mm_common_build_bands_string ((MMModemBand *)(gpointer)(bands->data), bands->len);
        mm_obj_dbg (log_object, "modem reports support for %s bands: %s", group, tmpstr);
        g_free (tmpstr);

        g_array_unref (nums);
    } else if (inner_error) {
        mm_obj_warn (log_object, "couldn't parse list of supported %s bands: %s", group, inner_error->message);
        g_clear_error (&inner_error);
    }

    return bands;
}

gboolean
mm_ublox_parse_uact_test (const gchar  *response,
                          gpointer      log_object,
                          GArray      **bands2g_out,
                          GArray      **bands3g_out,
                          GArray      **bands4g_out,
                          GError      **error)
{
    GRegex       *r;
    GMatchInfo   *match_info;
    GError       *inner_error = NULL;
    const gchar  *bands2g_str = NULL;
    const gchar  *bands3g_str = NULL;
    const gchar  *bands4g_str = NULL;
    GArray       *bands2g = NULL;
    GArray       *bands3g = NULL;
    GArray       *bands4g = NULL;
    gchar       **split = NULL;

    g_assert (bands2g_out && bands3g_out && bands4g_out);

    /*
     * AT+UACT=?
     * +UACT: ,,,(900,1800),(1,8),(101,103,107,108,120),(138)
     */
    r = g_regex_new ("\\+UACT: ([^,]*),([^,]*),([^,]*),(.*)(?:\\r\\n)?",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (inner_error)
        goto out;

    if (g_match_info_matches (match_info)) {
        gchar *aux;
        guint n_groups;

        aux  = mm_get_string_unquoted_from_match_info (match_info, 4);
        split = mm_split_string_groups (aux);
        n_groups = g_strv_length (split);
        if (n_groups >= 1)
            bands2g_str = split[0];
        if (n_groups >= 2)
            bands3g_str = split[1];
        if (n_groups >= 3)
            bands4g_str = split[2];
        g_free (aux);
    }

    if (!bands2g_str && !bands3g_str && !bands4g_str) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "frequency groups not found: %s", response);
        goto out;
    }

    bands2g = parse_bands_from_string (bands2g_str, "2G", log_object);
    bands3g = parse_bands_from_string (bands3g_str, "3G", log_object);
    bands4g = parse_bands_from_string (bands4g_str, "4G", log_object);

    if (!bands2g->len && !bands3g->len && !bands4g->len) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "no supported frequencies reported: %s", response);
        goto out;
    }

    /* success */

out:
    g_strfreev (split);
    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        if (bands2g)
            g_array_unref (bands2g);
        if (bands3g)
            g_array_unref (bands3g);
        if (bands4g)
            g_array_unref (bands4g);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    *bands2g_out = bands2g;
    *bands3g_out = bands3g;
    *bands4g_out = bands4g;
    return TRUE;
}

/*****************************************************************************/
/* UACT=X command builder */

gchar *
mm_ublox_build_uact_set_command (GArray  *bands,
                                 GError **error)
{
    GString *command;

    /* Build command */
    command = g_string_new ("+UACT=,,,");

    if (bands->len == 1 && g_array_index (bands, MMModemBand, 0) == MM_MODEM_BAND_ANY)
        g_string_append (command, "0");
    else {
        guint i;

        for (i = 0; i < bands->len; i++) {
            MMModemBand band;
            guint       num;

            band = g_array_index (bands, MMModemBand, i);
            num = uact_band_to_num (band);
            if (!num) {
                g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                             "Band unsupported by this plugin: %s", mm_modem_band_get_string (band));
                g_string_free (command, TRUE);
                return NULL;
            }

            g_string_append_printf (command, "%s%u", i == 0 ? "" : ",", num);
        }
    }

    return g_string_free (command, FALSE);
}

/*****************************************************************************/
/* URAT? response parser */

gboolean
mm_ublox_parse_urat_read_response (const gchar  *response,
                                   gpointer      log_object,
                                   MMModemMode  *out_allowed,
                                   MMModemMode  *out_preferred,
                                   GError      **error)
{
    GRegex      *r;
    GMatchInfo  *match_info;
    GError      *inner_error = NULL;
    MMModemMode  allowed = MM_MODEM_MODE_NONE;
    MMModemMode  preferred = MM_MODEM_MODE_NONE;
    gchar       *allowed_str = NULL;
    gchar       *preferred_str = NULL;

    g_assert (out_allowed != NULL && out_preferred != NULL);

    /* Response may be e.g.:
     * +URAT: 1,2
     * +URAT: 1
     */
    r = g_regex_new ("\\+URAT: (\\d+)(?:,(\\d+))?(?:\\r\\n)?", 0, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        guint  value = 0;

        /* Selected item is mandatory */
        if (!mm_get_uint_from_match_info (match_info, 1, &value)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "Couldn't read AcT selected value");
            goto out;
        }
        if (value >= G_N_ELEMENTS (ublox_combinations)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                       "Unexpected AcT selected value: %u", value);
            goto out;
        }
        allowed = ublox_combinations[value];
        allowed_str = mm_modem_mode_build_string_from_mask (allowed);
        mm_obj_dbg (log_object, "current allowed modes retrieved: %s", allowed_str);

        /* Preferred item is optional */
        if (mm_get_uint_from_match_info (match_info, 2, &value)) {
            if (value >= G_N_ELEMENTS (ublox_combinations)) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                           "Unexpected AcT preferred value: %u", value);
                goto out;
            }
            preferred = ublox_combinations[value];
            preferred_str = mm_modem_mode_build_string_from_mask (preferred);
            mm_obj_dbg (log_object, "current preferred modes retrieved: %s", preferred_str);
            if (mm_count_bits_set (preferred) != 1) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                           "AcT preferred value should be a single AcT: %s", preferred_str);
                goto out;
            }
            if (!(allowed & preferred)) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                           "AcT preferred value (%s) not a subset of the allowed value (%s)",
                                           preferred_str, allowed_str);
                goto out;
            }
        }
    }

out:

    g_free (allowed_str);
    g_free (preferred_str);

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (allowed == MM_MODEM_MODE_NONE) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Couldn't parse +URAT response: %s", response);
        return FALSE;
    }

    *out_allowed = allowed;
    *out_preferred = preferred;
    return TRUE;
}

/*****************************************************************************/
/* URAT=X command builder */

static gboolean
append_rat_value (GString      *str,
                  MMModemMode   mode,
                  GError      **error)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (ublox_combinations); i++) {
        if (ublox_combinations[i] == mode) {
            g_string_append_printf (str, "%u", i);
            return TRUE;
        }
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "No AcT value matches requested mode");
    return FALSE;
}

gchar *
mm_ublox_build_urat_set_command (MMModemMode   allowed,
                                 MMModemMode   preferred,
                                 GError      **error)
{
    GString *command;

    command = g_string_new ("+URAT=");
    if (!append_rat_value (command, allowed, error)) {
        g_string_free (command, TRUE);
        return NULL;
    }

    if (preferred != MM_MODEM_MODE_NONE) {
        g_string_append (command, ",");
        if (!append_rat_value (command, preferred, error)) {
            g_string_free (command, TRUE);
            return NULL;
        }
    }

    return g_string_free (command, FALSE);
}

/*****************************************************************************/
/* +UAUTHREQ=? test parser */

MMUbloxBearerAllowedAuth
mm_ublox_parse_uauthreq_test (const char  *response,
                              gpointer     log_object,
                              GError     **error)
{
    MMUbloxBearerAllowedAuth   mask = MM_UBLOX_BEARER_ALLOWED_AUTH_UNKNOWN;
    GError                    *inner_error = NULL;
    GArray                    *allowed_auths = NULL;
    gchar                    **split;
    guint                      split_len;

    /*
     * Response may be like:
     *   AT+UAUTHREQ=?
     *   +UAUTHREQ: (1-4),(0-2),,
     */
    response = mm_strip_tag (response, "+UAUTHREQ:");
    split = mm_split_string_groups (response);
    split_len = g_strv_length (split);
    if (split_len < 2) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Unexpected number of groups in +UAUTHREQ=? response: %u", g_strv_length (split));
        goto out;
    }

    allowed_auths = mm_parse_uint_list (split[1], &inner_error);
    if (inner_error)
        goto out;

    if (allowed_auths) {
        guint i;

        for (i = 0; i < allowed_auths->len; i++) {
            guint val;

            val = g_array_index (allowed_auths, guint, i);
            switch (val) {
                case 0:
                    mask |= MM_UBLOX_BEARER_ALLOWED_AUTH_NONE;
                    break;
                case 1:
                    mask |= MM_UBLOX_BEARER_ALLOWED_AUTH_PAP;
                    break;
                case 2:
                    mask |= MM_UBLOX_BEARER_ALLOWED_AUTH_CHAP;
                    break;
                case 3:
                    mask |= MM_UBLOX_BEARER_ALLOWED_AUTH_AUTO;
                    break;
                default:
                    mm_obj_warn (log_object, "unexpected +UAUTHREQ value: %u", val);
                    break;
            }
        }
    }

    if (!mask) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "No supported authentication methods in +UAUTHREQ=? response");
        goto out;
    }

out:
    g_strfreev (split);

    if (allowed_auths)
        g_array_unref (allowed_auths);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_UBLOX_BEARER_ALLOWED_AUTH_UNKNOWN;
    }

    return mask;
}

/*****************************************************************************/
/* +UGCNTRD response parser */

gboolean
mm_ublox_parse_ugcntrd_response_for_cid (const gchar  *response,
                                         guint         in_cid,
                                         guint64      *out_session_tx_bytes,
                                         guint64      *out_session_rx_bytes,
                                         guint64      *out_total_tx_bytes,
                                         guint64      *out_total_rx_bytes,
                                         GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info = NULL;
    GError     *inner_error = NULL;
    guint64     session_tx_bytes = 0;
    guint64     session_rx_bytes = 0;
    guint64     total_tx_bytes   = 0;
    guint64     total_rx_bytes   = 0;
    gboolean    matched = FALSE;

    /* Response may be e.g.:
     *  +UGCNTRD: 31,2704,1819,2724,1839
     * We assume only ONE line is returned.
     */
    r = g_regex_new ("\\+UGCNTRD:\\s*(\\d+),\\s*(\\d+),\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW, 0, NULL);
    g_assert (r != NULL);

    /* Report invalid CID given */
    if (!in_cid) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Invalid CID given");
        goto out;
    }

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    while (!inner_error && g_match_info_matches (match_info)) {
        guint cid = 0;

        /* Matched CID? */
        if (!mm_get_uint_from_match_info (match_info, 1, &cid) || cid != in_cid) {
            g_match_info_next (match_info, &inner_error);
            continue;
        }

        if (out_session_tx_bytes && !mm_get_u64_from_match_info (match_info, 2, &session_tx_bytes)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing session TX bytes");
            goto out;
        }

        if (out_session_rx_bytes && !mm_get_u64_from_match_info (match_info, 3, &session_rx_bytes)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing session RX bytes");
            goto out;
        }

        if (out_total_tx_bytes && !mm_get_u64_from_match_info (match_info, 4, &total_tx_bytes)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing total TX bytes");
            goto out;
        }

        if (out_total_rx_bytes && !mm_get_u64_from_match_info (match_info, 5, &total_rx_bytes)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing total RX bytes");
            goto out;
        }

        matched = TRUE;
        break;
    }

    if (!matched) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "No statistics found for CID %u", in_cid);
        goto out;
    }

out:

    g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (out_session_tx_bytes)
        *out_session_tx_bytes = session_tx_bytes;
    if (out_session_rx_bytes)
        *out_session_rx_bytes = session_rx_bytes;
    if (out_total_tx_bytes)
        *out_total_tx_bytes = total_tx_bytes;
    if (out_total_rx_bytes)
        *out_total_rx_bytes = total_rx_bytes;
    return TRUE;
}
