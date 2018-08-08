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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-xmm.h"

/*****************************************************************************/
/* XACT common config */

typedef struct {
    guint       num;
    MMModemBand band;
} XactBandConfig;

static const XactBandConfig xact_band_config[] = {
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
    { .num =  149, .band = MM_MODEM_BAND_EUTRAN_49 },
    { .num =  150, .band = MM_MODEM_BAND_EUTRAN_50 },
    { .num =  151, .band = MM_MODEM_BAND_EUTRAN_51 },
    { .num =  152, .band = MM_MODEM_BAND_EUTRAN_52 },
    { .num =  153, .band = MM_MODEM_BAND_EUTRAN_53 },
    { .num =  154, .band = MM_MODEM_BAND_EUTRAN_54 },
    { .num =  155, .band = MM_MODEM_BAND_EUTRAN_55 },
    { .num =  156, .band = MM_MODEM_BAND_EUTRAN_56 },
    { .num =  157, .band = MM_MODEM_BAND_EUTRAN_57 },
    { .num =  158, .band = MM_MODEM_BAND_EUTRAN_58 },
    { .num =  159, .band = MM_MODEM_BAND_EUTRAN_59 },
    { .num =  160, .band = MM_MODEM_BAND_EUTRAN_60 },
    { .num =  161, .band = MM_MODEM_BAND_EUTRAN_61 },
    { .num =  162, .band = MM_MODEM_BAND_EUTRAN_62 },
    { .num =  163, .band = MM_MODEM_BAND_EUTRAN_63 },
    { .num =  164, .band = MM_MODEM_BAND_EUTRAN_64 },
    { .num =  165, .band = MM_MODEM_BAND_EUTRAN_65 },
    { .num =  166, .band = MM_MODEM_BAND_EUTRAN_66 },
};

#define XACT_NUM_IS_BAND_2G(num) (num > 300)
#define XACT_NUM_IS_BAND_3G(num) (num < 100)
#define XACT_NUM_IS_BAND_4G(num) (num > 100 && num < 300)

static MMModemBand
xact_num_to_band (guint num)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (xact_band_config); i++) {
        if (num == xact_band_config[i].num)
            return xact_band_config[i].band;
    }
    return MM_MODEM_BAND_UNKNOWN;
}

static guint
xact_band_to_num (MMModemBand band)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (xact_band_config); i++) {
        if (band == xact_band_config[i].band)
            return xact_band_config[i].num;
    }
    return 0;
}

/*****************************************************************************/
/* XACT=? response parser */

/* Index of the array is the XMM-specific value */
static const MMModemMode xmm_modes[] = {
    ( MM_MODEM_MODE_2G ),
    (                    MM_MODEM_MODE_3G ),
    (                                       MM_MODEM_MODE_4G ),
    ( MM_MODEM_MODE_2G | MM_MODEM_MODE_3G ),
    (                    MM_MODEM_MODE_3G | MM_MODEM_MODE_4G ),
    ( MM_MODEM_MODE_2G |                    MM_MODEM_MODE_4G ),
    ( MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G ),
};

gboolean
mm_xmm_parse_xact_test_response (const gchar  *response,
                                 GArray      **modes_out,
                                 GArray      **bands_out,
                                 GError      **error)
{
    GError  *inner_error = NULL;
    GArray  *modes = NULL;
    GArray  *all_modes = NULL;
    GArray  *filtered = NULL;
    GArray  *supported = NULL;
    GArray  *preferred = NULL;
    GArray  *bands = NULL;
    gchar  **split = NULL;
    guint    i;

    MMModemModeCombination all = {
        .allowed   = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE
    };

    g_assert (modes_out && bands_out);

    /*
     * AT+XACT=?
     * +XACT: (0-6),(0-2),0,1,2,4,5,8,101,102,103,104,105,107,108,111,...
     */
    response = mm_strip_tag (response, "+XACT:");
    split = mm_split_string_groups (response);

    if (g_strv_length (split) < 3) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing fields");
        goto out;
    }

    /* First group is list of supported modes */
    supported = mm_parse_uint_list (split[0], &inner_error);
    if (inner_error)
        goto out;
    if (!supported) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing modes");
        goto out;
    }

    /* Second group is list of possible preferred modes.
     * For our purposes, the preferred list may be empty */
    preferred = mm_parse_uint_list (split[1], &inner_error);
    if (inner_error)
        goto out;

    /* Build array of modes */
    modes = g_array_new (FALSE, FALSE, sizeof (MMModemModeCombination));

    for (i = 0; i < supported->len; i++) {
        guint                  supported_value;
        MMModemModeCombination combination;
        guint                  j;

        supported_value = g_array_index (supported, guint, i);

        if (supported_value >= G_N_ELEMENTS (xmm_modes)) {
            mm_warn ("Unexpected AcT supported value: %u", supported_value);
            continue;
        }

        /* Combination without any preferred */
        combination.allowed = xmm_modes[supported_value];
        combination.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (modes, combination);

        if (mm_count_bits_set (combination.allowed) == 1)
            continue;

        if (!preferred)
            continue;

        for (j = 0; j < preferred->len; j++) {
            guint preferred_value;

            preferred_value = g_array_index (preferred, guint, j);
            if (preferred_value >= G_N_ELEMENTS (xmm_modes)) {
                mm_warn ("Unexpected AcT preferred value: %u", preferred_value);
                continue;
            }
            combination.preferred = xmm_modes[preferred_value];
            if (mm_count_bits_set (combination.preferred) != 1) {
                mm_warn ("AcT preferred value should be a single AcT: %u", preferred_value);
                continue;
            }
            if (!(combination.allowed & combination.preferred))
                continue;
            g_array_append_val (modes, combination);
        }
    }

    if (modes->len == 0) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "No modes list built from +XACT=? response");
        goto out;
    }

    /* Build array of bands */
    bands = g_array_new (FALSE, FALSE, sizeof (MMModemBand));

    /*
     * The next element at index 2 may be '0'. We will just treat that field as
     * any other band field as '0' isn't a supported band, we'll just ignore it.
     */
    for (i = 2; split[i]; i++) {
        MMModemBand band;
        guint       num;

        if (!mm_get_uint_from_str (split[i], &num)) {
            mm_warn ("Unexpected band value: %s", split[i]);
            continue;
        }

        if (num == 0)
            continue;

        band = xact_num_to_band (num);
        if (band == MM_MODEM_BAND_UNKNOWN) {
            mm_warn ("Unsupported band value: %s", split[i]);
            continue;
        }

        g_array_append_val (bands, band);

        if (XACT_NUM_IS_BAND_2G (num))
            all.allowed |= MM_MODEM_MODE_2G;
        if (XACT_NUM_IS_BAND_3G (num))
            all.allowed |= MM_MODEM_MODE_3G;
        if (XACT_NUM_IS_BAND_4G (num))
            all.allowed |= MM_MODEM_MODE_4G;
    }

    if (bands->len == 0) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "No bands list built from +XACT=? response");
        goto out;
    }

    /* AT+XACT lies about the supported modes, e.g. it may report 2G supported
     * for 3G+4G only devices. So, filter out unsupported modes based on the
     * supported bands */
    all_modes = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination), 1);
    g_array_append_val (all_modes, all);

    filtered = mm_filter_supported_modes (all_modes, modes);
    if (!filtered || filtered->len == 0) {
        inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                   "Empty supported mode list after frequency band filtering");
        goto out;
    }

    /* success */

out:
    if (modes)
        g_array_unref (modes);
    if (all_modes)
        g_array_unref (all_modes);
    if (supported)
        g_array_unref (supported);
    if (preferred)
        g_array_unref (preferred);
    g_strfreev (split);

    if (inner_error) {
        if (filtered)
            g_array_unref (filtered);
        if (bands)
            g_array_unref (bands);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    g_assert (filtered);
    *modes_out = filtered;
    g_assert (bands);
    *bands_out = bands;
    return TRUE;
}

/*****************************************************************************/
/* AT+XACT? response parser */

gboolean
mm_xmm_parse_xact_query_response (const gchar             *response,
                                  MMModemModeCombination  *mode_out,
                                  GArray                 **bands_out,
                                  GError                 **error)
{
    GRegex     *r;
    GMatchInfo *match_info;
    GError     *inner_error = NULL;
    GArray     *bands = NULL;
    guint       i;

    MMModemModeCombination mode = {
        .allowed   = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE,
    };

    /* At least one */
    g_assert (mode_out || bands_out);

    /*
     * AT+XACT?
     * +XACT: 4,1,2,1,2,4,5,8,101,102,103,104,105,107,108,111,...
     *
     * Note: the first 3 fields corresponde to allowed and preferred modes. Only the
     * first one of those 3 first fields is mandatory, the other two may be empty.
     */
    r = g_regex_new ("\\+XACT: (\\d+),([^,]*),([^,]*),(.*)(?:\\r\\n)?",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW, 0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
    if (!inner_error && g_match_info_matches (match_info)) {
        if (mode_out) {
            guint xmm_mode;

            /* Number at index 1 */
            mm_get_uint_from_match_info (match_info, 1, &xmm_mode);
            if (xmm_mode >= G_N_ELEMENTS (xmm_modes)) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Unsupported XACT AcT value: %u", xmm_mode);
                goto out;
            }
            mode.allowed = xmm_modes[xmm_mode];

            /* Number at index 2 */
            if (mm_count_bits_set (mode.allowed) > 1 && mm_get_uint_from_match_info (match_info, 2, &xmm_mode)) {
                if (xmm_mode >= G_N_ELEMENTS (xmm_modes)) {
                    inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Unsupported XACT preferred AcT value: %u", xmm_mode);
                    goto out;
                }
                mode.preferred = xmm_modes[xmm_mode];
            }

            /* Number at index 3: ignored */
        }

        if (bands_out) {
            gchar  *bandstr;
            GArray *nums;

            /* Bands start at index 4 */
            bandstr = mm_get_string_unquoted_from_match_info (match_info, 4);
            nums = mm_parse_uint_list (bandstr, &inner_error);
            g_free (bandstr);

            if (inner_error)
                goto out;
            if (!nums) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Invalid XACT? response");
                goto out;
            }

            bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), nums->len);
            for (i = 0; i < nums->len; i++) {
                MMModemBand band;

                band = xact_num_to_band (g_array_index (nums, guint, i));
                if (band != MM_MODEM_BAND_UNKNOWN)
                    g_array_append_val (bands, band);
            }
            g_array_unref (nums);

            if (bands->len == 0) {
                inner_error = g_error_new (MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing current band list");
                goto out;
            }
        }
    }

    /* success */

out:
    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);

    if (inner_error) {
        if (bands)
            g_array_unref (bands);
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    if (mode_out) {
        g_assert (mode.allowed != MM_MODEM_MODE_NONE);
        mode_out->allowed = mode.allowed;
        mode_out->preferred = mode.preferred;
    }

    if (bands_out) {
        g_assert (bands);
        *bands_out = bands;
    }

    return TRUE;
}

/*****************************************************************************/
/* AT+XACT=X command builder */

static gboolean
append_rat_value (GString      *str,
                  MMModemMode   mode,
                  GError      **error)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (xmm_modes); i++) {
        if (xmm_modes[i] == mode) {
            g_string_append_printf (str, "%u", i);
            return TRUE;
        }
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "No AcT value matches requested mode");
    return FALSE;
}

gchar *
mm_xmm_build_xact_set_command (const MMModemModeCombination  *mode,
                               const GArray                  *bands,
                               GError                       **error)
{
    GString *command;

    /* At least one required */
    g_assert (mode || bands);

    /* Build command */
    command = g_string_new ("+XACT=");

    /* Mode is optional. If not given, we set all fields as empty */
    if (mode) {
        /* Allowed mask */
        if (!append_rat_value (command, mode->allowed, error)) {
            g_string_free (command, TRUE);
            return NULL;
        }

        /* Preferred */
        if (mode->preferred != MM_MODEM_MODE_NONE) {
            g_string_append (command, ",");
            if (!append_rat_value (command, mode->preferred, error)) {
                g_string_free (command, TRUE);
                return NULL;
            }
            /* We never set <PreferredAct2> because that is anyway not part of
             * ModemManager's API. In modems with triple GSM/UMTS/LTE mode, the
             * <PreferredAct2> is always the highest of the remaining ones. E.g.
             * if "2G+3G+4G allowed with 2G preferred", the second preferred one
             * would be 4G, not 3G. */
            g_string_append (command, ",");
        } else
            g_string_append (command, ",,");
    } else
        g_string_append (command, ",,");

    if (bands) {
        g_string_append (command, ",");
        /* Automatic band selection */
        if (bands->len == 1 && g_array_index (bands, MMModemBand, 0) == MM_MODEM_BAND_ANY)
            g_string_append (command, "0");
        else {
            guint i;

            for (i = 0; i < bands->len; i++) {
                MMModemBand band;
                guint       num;

                band = g_array_index (bands, MMModemBand, i);
                num = xact_band_to_num (band);
                if (!num) {
                    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Band unsupported by this plugin: %s", mm_modem_band_get_string (band));
                    g_string_free (command, TRUE);
                    return NULL;
                }

                g_string_append_printf (command, "%s%u", i == 0 ? "" : ",", num);
            }
        }
    }

    return g_string_free (command, FALSE);
}

/*****************************************************************************/
/* Get mode to apply when ANY */

MMModemMode
mm_xmm_get_modem_mode_any (const GArray *combinations)
{
    guint       i;
    MMModemMode any = MM_MODEM_MODE_NONE;
    guint       any_bits_set = 0;

    for (i = 0; i < combinations->len; i++) {
        MMModemModeCombination *combination;
        guint                   bits_set;

        combination = &g_array_index (combinations, MMModemModeCombination, i);
        if (combination->preferred != MM_MODEM_MODE_NONE)
            continue;
        bits_set = mm_count_bits_set (combination->allowed);
        if (bits_set > any_bits_set) {
            any_bits_set = bits_set;
            any = combination->allowed;
        }
    }

    /* If combinations were processed via mm_xmm_parse_uact_test_response(),
     * we're sure that there will be at least one combination with preferred
     * 'none', so there must be some valid combination as result */
    g_assert (any != MM_MODEM_MODE_NONE);
    return any;
}
