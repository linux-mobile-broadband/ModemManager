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

    if (match_info)
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
            mm_warn ("Unexpected AcT value: %u", selected_value);
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
                mm_warn ("Unexpected AcT preferred value: %u", preferred_value);
                continue;
            }
            combination.preferred = ublox_combinations[preferred_value];
            if (mm_count_bits_set (combination.preferred) != 1) {
                mm_warn ("AcT preferred value should be a single AcT: %u", preferred_value);
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

/*****************************************************************************/

static MMModemMode
supported_modes_per_model (const gchar *model)
{
    MMModemMode all = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);

    if (model) {
        /* Some TOBY-L2/MPCI-L2 devices don't support 2G */
        if (g_str_equal (model, "TOBY-L201") || g_str_equal (model, "TOBY-L220") || g_str_equal (model, "MPCI-L201"))
            all &= ~MM_MODEM_MODE_2G;
        /* None of the LISA-U or SARA-U devices support 4G */
        else if (g_str_has_prefix (model, "LISA-U") || g_str_has_prefix (model, "SARA-U")) {
            all &= ~MM_MODEM_MODE_4G;
            /* Some SARA devices don't support 2G */
            if (g_str_equal (model, "SARA-U270-53S") || g_str_equal (model, "SARA-U280"))
                all &= ~MM_MODEM_MODE_2G;
        }
    }

    return all;
}

GArray *
mm_ublox_filter_supported_modes (const gchar  *model,
                                 GArray       *combinations,
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
    filtered = mm_filter_supported_modes (all, combinations);
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

typedef struct {
    guint       ubandsel_value;
    MMModemBand bands_2g[2];
    MMModemBand bands_3g[2];
    MMModemBand bands_4g[2];
} BandConfiguration;

static const BandConfiguration band_configuration[] = {
    {
        .ubandsel_value = 700,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_XIII, MM_MODEM_BAND_EUTRAN_XVII }
    },
    {
        .ubandsel_value = 800,
        .bands_3g = { MM_MODEM_BAND_U800 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_XX }
    },
    {
        .ubandsel_value = 850,
        .bands_2g = { MM_MODEM_BAND_G850 },
        .bands_3g = { MM_MODEM_BAND_U850 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_V }
    },
    {
        .ubandsel_value = 900,
        .bands_2g = { MM_MODEM_BAND_EGSM },
        .bands_3g = { MM_MODEM_BAND_U900 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_VIII }
    },
    {
        .ubandsel_value = 1500,
        .bands_4g = { MM_MODEM_BAND_EUTRAN_XI }
    },
    {
        .ubandsel_value = 1700,
        .bands_3g = { MM_MODEM_BAND_U17IV },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_IV }
    },
    {
        .ubandsel_value = 1800,
        .bands_2g = { MM_MODEM_BAND_DCS },
        .bands_3g = { MM_MODEM_BAND_U1800 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_III }
    },
    {
        .ubandsel_value = 1900,
        .bands_2g = { MM_MODEM_BAND_PCS },
        .bands_3g = { MM_MODEM_BAND_U1900 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_II }
    },
    {
        .ubandsel_value = 2100,
        .bands_3g = { MM_MODEM_BAND_U2100 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_I }
    },
    {
        .ubandsel_value = 2600,
        .bands_3g = { MM_MODEM_BAND_U2600 },
        .bands_4g = { MM_MODEM_BAND_EUTRAN_VII }
    },
};

GArray *
mm_ublox_get_supported_bands (const gchar  *model,
                              GError      **error)
{
    MMModemMode  mode;
    GArray      *bands;
    guint        i;

    mode = supported_modes_per_model (model);

    bands = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

    for (i = 0; i < G_N_ELEMENTS (band_configuration); i++) {
        if ((mode & MM_MODEM_MODE_2G) && band_configuration[i].bands_2g[0]) {
            bands = g_array_append_val (bands, band_configuration[i].bands_2g[0]);
            if (band_configuration[i].bands_2g[1])
                bands = g_array_append_val (bands, band_configuration[i].bands_2g[1]);
        }
        if ((mode & MM_MODEM_MODE_3G) && band_configuration[i].bands_3g[0]) {
            bands = g_array_append_val (bands, band_configuration[i].bands_3g[0]);
            if (band_configuration[i].bands_3g[1])
                bands = g_array_append_val (bands, band_configuration[i].bands_3g[1]);
        }
        if ((mode & MM_MODEM_MODE_4G) && band_configuration[i].bands_4g[0]) {
            bands = g_array_append_val (bands, band_configuration[i].bands_4g[0]);
            if (band_configuration[i].bands_4g[1])
                bands = g_array_append_val (bands, band_configuration[i].bands_4g[1]);
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

/*****************************************************************************/
/* +UBANDSEL? response parser */

static void
append_bands (GArray *bands,
              guint   ubandsel_value)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (band_configuration); i++)
        if (ubandsel_value == band_configuration[i].ubandsel_value)
            break;

    if (i == G_N_ELEMENTS (band_configuration)) {
        mm_warn ("Unknown band configuration value given: %u", ubandsel_value);
        return;
    }

    /* Note: we don't care if the device doesn't support one of these modes;
     * the generic logic will filter out all bands not supported before
     * exposing them in the DBus property */

    if (band_configuration[i].bands_2g[0]) {
        g_array_append_val (bands, band_configuration[i].bands_2g[0]);
        if (band_configuration[i].bands_2g[1])
            g_array_append_val (bands, band_configuration[i].bands_2g[1]);
    }

    if (band_configuration[i].bands_3g[0]) {
        g_array_append_val (bands, band_configuration[i].bands_3g[0]);
        if (band_configuration[i].bands_3g[1])
            g_array_append_val (bands, band_configuration[i].bands_3g[1]);
    }

    if (band_configuration[i].bands_4g[0]) {
        g_array_append_val (bands, band_configuration[i].bands_4g[0]);
        if (band_configuration[i].bands_4g[1])
            g_array_append_val (bands, band_configuration[i].bands_4g[1]);
    }
}

GArray *
mm_ublox_parse_ubandsel_response (const gchar  *response,
                                  GError      **error)
{
    GArray  *array_values = NULL;
    GArray  *array = NULL;
    gchar   *dupstr = NULL;
    GError  *inner_error = NULL;
    guint    i;

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
    array = g_array_new (FALSE, FALSE, sizeof (MMModemBand));
    for (i = 0; i < array_values->len; i++)
        append_bands (array, g_array_index (array_values, guint, i));

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
mm_ublox_build_ubandsel_set_command (GArray  *bands,
                                     GError **error)
{
    GString *command = NULL;
    GArray  *ubandsel_nums;
    guint    i;

    if (bands->len == 1 && g_array_index (bands, MMModemBand, 0) == MM_MODEM_BAND_ANY)
        return g_strdup ("+UBANDSEL=0");

    ubandsel_nums = g_array_sized_new (FALSE, FALSE, sizeof (guint), G_N_ELEMENTS (band_configuration));
    for (i = 0; i < G_N_ELEMENTS (band_configuration); i++) {
        guint j;

        for (j = 0; j < bands->len; j++) {
            MMModemBand band;

            band = g_array_index (bands, MMModemBand, j);

            if (band == band_configuration[i].bands_2g[0] || band == band_configuration[i].bands_2g[1] ||
                band == band_configuration[i].bands_3g[0] || band == band_configuration[i].bands_3g[1] ||
                band == band_configuration[i].bands_4g[0] || band == band_configuration[i].bands_4g[1]) {
                g_array_append_val (ubandsel_nums, band_configuration[i].ubandsel_value);
                break;
            }
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
        if (combination->preferred == MM_MODEM_MODE_NONE)
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
/* URAT? response parser */

gboolean
mm_ublox_parse_urat_read_response (const gchar  *response,
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
        mm_dbg ("current allowed modes retrieved: %s", allowed_str);

        /* Preferred item is optional */
        if (mm_get_uint_from_match_info (match_info, 2, &value)) {
            if (value >= G_N_ELEMENTS (ublox_combinations)) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                           "Unexpected AcT preferred value: %u", value);
                goto out;
            }
            preferred = ublox_combinations[value];
            preferred_str = mm_modem_mode_build_string_from_mask (preferred);
            mm_dbg ("current preferred modes retrieved: %s", preferred_str);
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

    if (match_info)
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
/* +UGCNTRD response parser */

gboolean
mm_ublox_parse_ugcntrd_response_for_cid (const gchar  *response,
                                         guint         in_cid,
                                         guint        *out_session_tx_bytes,
                                         guint        *out_session_rx_bytes,
                                         guint        *out_total_tx_bytes,
                                         guint        *out_total_rx_bytes,
                                         GError      **error)
{
    GRegex     *r;
    GMatchInfo *match_info = NULL;
    GError     *inner_error = NULL;
    guint       session_tx_bytes = 0;
    guint       session_rx_bytes = 0;
    guint       total_tx_bytes   = 0;
    guint       total_rx_bytes   = 0;
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

        if (out_session_tx_bytes && !mm_get_uint_from_match_info (match_info, 2, &session_tx_bytes)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing session TX bytes");
            goto out;
        }

        if (out_session_rx_bytes && !mm_get_uint_from_match_info (match_info, 3, &session_rx_bytes)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing session RX bytes");
            goto out;
        }

        if (out_total_tx_bytes && !mm_get_uint_from_match_info (match_info, 4, &total_tx_bytes)) {
            inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Error parsing total TX bytes");
            goto out;
        }

        if (out_total_rx_bytes && !mm_get_uint_from_match_info (match_info, 5, &total_rx_bytes)) {
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

    if (match_info)
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
