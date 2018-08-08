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

#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-xmm.h"

/*****************************************************************************/
/* Test XACT=? responses */

static void
validate_xact_test_response (const gchar                  *response,
                             const MMModemModeCombination *expected_modes,
                             guint                         n_expected_modes,
                             const MMModemBand            *expected_bands,
                             guint                         n_expected_bands)
{
    GError   *error = NULL;
    GArray   *modes = NULL;
    GArray   *bands = NULL;
    gboolean  ret;
    guint     i;

    ret = mm_xmm_parse_xact_test_response (response, &modes, &bands, &error);
    g_assert_no_error (error);
    g_assert (ret);

    g_assert_cmpuint (modes->len, ==, n_expected_modes);
    for (i = 0; i < modes->len; i++) {
        MMModemModeCombination mode;
        guint                  j;
        gboolean               found = FALSE;

        mode = g_array_index (modes, MMModemModeCombination, i);
        for (j = 0; !found && j < n_expected_modes; j++)
            found = (mode.allowed == expected_modes[j].allowed && mode.preferred == expected_modes[j].preferred);
        g_assert (found);
    }
    g_array_unref (modes);

    g_assert_cmpuint (bands->len, ==, n_expected_bands);
    for (i = 0; i < bands->len; i++) {
        MMModemBand band;
        guint       j;
        gboolean    found = FALSE;

        band = g_array_index (bands, MMModemBand, i);
        for (j = 0; !found && j < n_expected_bands; j++)
            found = (band == expected_bands[j]);
        g_assert (found);
    }
    g_array_unref (bands);
}

static void
test_xact_test_4g_only (void)
{
    const gchar *response =
        "+XACT: "
        "(0-6),(0-2),0,"
        "101,102,103,104,105,107,108,111,112,113,117,118,119,120,121,126,128,129,130,138,139,140,141,166";

    static const MMModemModeCombination expected_modes[] = {
        { MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE },
    };

    static const MMModemBand expected_bands[] = {
        MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,
        MM_MODEM_BAND_EUTRAN_7,  MM_MODEM_BAND_EUTRAN_8,  MM_MODEM_BAND_EUTRAN_11, MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13,
        MM_MODEM_BAND_EUTRAN_17, MM_MODEM_BAND_EUTRAN_18, MM_MODEM_BAND_EUTRAN_19, MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_21,
        MM_MODEM_BAND_EUTRAN_26, MM_MODEM_BAND_EUTRAN_28, MM_MODEM_BAND_EUTRAN_29, MM_MODEM_BAND_EUTRAN_30, MM_MODEM_BAND_EUTRAN_38,
        MM_MODEM_BAND_EUTRAN_39, MM_MODEM_BAND_EUTRAN_40, MM_MODEM_BAND_EUTRAN_41, MM_MODEM_BAND_EUTRAN_66
    };

    /* NOTE: 2G and 3G modes are reported in XACT but no 2G or 3G frequencies supported */
    validate_xact_test_response (response,
                                 expected_modes, G_N_ELEMENTS (expected_modes),
                                 expected_bands, G_N_ELEMENTS (expected_bands));
}

static void
test_xact_test_3g_4g (void)
{
    const gchar *response =
        "+XACT: "
        "(0-6),(0-2),0,"
        "1,2,4,5,8,"
        "101,102,103,104,105,107,108,111,112,113,117,118,119,120,121,126,128,129,130,138,139,140,141,166";

    static const MMModemModeCombination expected_modes[] = {
        { MM_MODEM_MODE_3G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_3G },
        { MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_4G },
    };

    static const MMModemBand expected_bands[] = {
        MM_MODEM_BAND_UTRAN_1,   MM_MODEM_BAND_UTRAN_2,   MM_MODEM_BAND_UTRAN_4,   MM_MODEM_BAND_UTRAN_5,   MM_MODEM_BAND_UTRAN_8,
        MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,
        MM_MODEM_BAND_EUTRAN_7,  MM_MODEM_BAND_EUTRAN_8,  MM_MODEM_BAND_EUTRAN_11, MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13,
        MM_MODEM_BAND_EUTRAN_17, MM_MODEM_BAND_EUTRAN_18, MM_MODEM_BAND_EUTRAN_19, MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_21,
        MM_MODEM_BAND_EUTRAN_26, MM_MODEM_BAND_EUTRAN_28, MM_MODEM_BAND_EUTRAN_29, MM_MODEM_BAND_EUTRAN_30, MM_MODEM_BAND_EUTRAN_38,
        MM_MODEM_BAND_EUTRAN_39, MM_MODEM_BAND_EUTRAN_40, MM_MODEM_BAND_EUTRAN_41, MM_MODEM_BAND_EUTRAN_66
    };

    /* NOTE: 2G modes are reported in XACT but no 2G frequencies supported */
    validate_xact_test_response (response,
                                 expected_modes, G_N_ELEMENTS (expected_modes),
                                 expected_bands, G_N_ELEMENTS (expected_bands));
}

static void
test_xact_test_2g_3g_4g (void)
{
    const gchar *response =
        "+XACT: "
        "(0-6),(0-2),0,"
        "900,1800,1900,850,"
        "1,2,4,5,8,"
        "101,102,103,104,105,107,108,111,112,113,117,118,119,120,121,126,128,129,130,138,139,140,141,166";

    static const MMModemModeCombination expected_modes[] = {
        { MM_MODEM_MODE_2G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_3G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, MM_MODEM_MODE_2G },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_3G, MM_MODEM_MODE_3G },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, MM_MODEM_MODE_2G },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_4G, MM_MODEM_MODE_4G },
        { MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_3G },
        { MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_4G },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_NONE },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_2G },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_3G },
        { MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G, MM_MODEM_MODE_4G },
    };

    static const MMModemBand expected_bands[] = {
        MM_MODEM_BAND_EGSM,      MM_MODEM_BAND_DCS,       MM_MODEM_BAND_PCS,       MM_MODEM_BAND_G850,
        MM_MODEM_BAND_UTRAN_1,   MM_MODEM_BAND_UTRAN_2,   MM_MODEM_BAND_UTRAN_4,   MM_MODEM_BAND_UTRAN_5,   MM_MODEM_BAND_UTRAN_8,
        MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,
        MM_MODEM_BAND_EUTRAN_7,  MM_MODEM_BAND_EUTRAN_8,  MM_MODEM_BAND_EUTRAN_11, MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13,
        MM_MODEM_BAND_EUTRAN_17, MM_MODEM_BAND_EUTRAN_18, MM_MODEM_BAND_EUTRAN_19, MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_21,
        MM_MODEM_BAND_EUTRAN_26, MM_MODEM_BAND_EUTRAN_28, MM_MODEM_BAND_EUTRAN_29, MM_MODEM_BAND_EUTRAN_30, MM_MODEM_BAND_EUTRAN_38,
        MM_MODEM_BAND_EUTRAN_39, MM_MODEM_BAND_EUTRAN_40, MM_MODEM_BAND_EUTRAN_41, MM_MODEM_BAND_EUTRAN_66
    };

    validate_xact_test_response (response,
                                 expected_modes, G_N_ELEMENTS (expected_modes),
                                 expected_bands, G_N_ELEMENTS (expected_bands));
}

/*****************************************************************************/
/* Test XACT? responses */

static void
validate_xact_query_response (const gchar                  *response,
                              const MMModemModeCombination *expected_mode,
                              const MMModemBand            *expected_bands,
                              guint                         n_expected_bands)
{
    GError   *error = NULL;
    GArray   *bands = NULL;
    gboolean  ret;
    guint     i;

    MMModemModeCombination mode = {
        .allowed = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE,
    };

    ret = mm_xmm_parse_xact_query_response (response, &mode, &bands, &error);
    g_assert_no_error (error);
    g_assert (ret);

    g_assert_cmpuint (mode.allowed,   ==, expected_mode->allowed);
    g_assert_cmpuint (mode.preferred, ==, expected_mode->preferred);

    g_assert_cmpuint (bands->len, ==, n_expected_bands);
    for (i = 0; i < bands->len; i++) {
        MMModemBand band;
        guint       j;
        gboolean    found = FALSE;

        band = g_array_index (bands, MMModemBand, i);
        for (j = 0; !found && j < n_expected_bands; j++)
            found = (band == expected_bands[j]);
        g_assert (found);
    }
    g_array_unref (bands);
}

static void
test_xact_query_3g_only (void)
{
    const gchar *response =
        "+XACT: "
        "1,1,,"
        "1,2,4,5,8,"
        "101,102,103,104,105,107,108,111,112,113,117,118,119,120,121,126,128,129,130,138,139,140,141,166";

    static const MMModemModeCombination expected_mode = {
        .allowed = MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_NONE
    };

    static const MMModemBand expected_bands[] = {
        MM_MODEM_BAND_UTRAN_1,   MM_MODEM_BAND_UTRAN_2,   MM_MODEM_BAND_UTRAN_4,   MM_MODEM_BAND_UTRAN_5,   MM_MODEM_BAND_UTRAN_8,
        MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,
        MM_MODEM_BAND_EUTRAN_7,  MM_MODEM_BAND_EUTRAN_8,  MM_MODEM_BAND_EUTRAN_11, MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13,
        MM_MODEM_BAND_EUTRAN_17, MM_MODEM_BAND_EUTRAN_18, MM_MODEM_BAND_EUTRAN_19, MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_21,
        MM_MODEM_BAND_EUTRAN_26, MM_MODEM_BAND_EUTRAN_28, MM_MODEM_BAND_EUTRAN_29, MM_MODEM_BAND_EUTRAN_30, MM_MODEM_BAND_EUTRAN_38,
        MM_MODEM_BAND_EUTRAN_39, MM_MODEM_BAND_EUTRAN_40, MM_MODEM_BAND_EUTRAN_41, MM_MODEM_BAND_EUTRAN_66
    };

    validate_xact_query_response (response,
                                  &expected_mode,
                                  expected_bands, G_N_ELEMENTS (expected_bands));
}

static void
test_xact_query_3g_4g (void)
{
    const gchar *response =
        "+XACT: "
        "4,1,2,"
        "1,2,4,5,8,"
        "101,102,103,104,105,107,108,111,112,113,117,118,119,120,121,126,128,129,130,138,139,140,141,166";

    static const MMModemModeCombination expected_mode = {
        .allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_3G
    };

    static const MMModemBand expected_bands[] = {
        MM_MODEM_BAND_UTRAN_1,   MM_MODEM_BAND_UTRAN_2,   MM_MODEM_BAND_UTRAN_4,   MM_MODEM_BAND_UTRAN_5,   MM_MODEM_BAND_UTRAN_8,
        MM_MODEM_BAND_EUTRAN_1,  MM_MODEM_BAND_EUTRAN_2,  MM_MODEM_BAND_EUTRAN_3,  MM_MODEM_BAND_EUTRAN_4,  MM_MODEM_BAND_EUTRAN_5,
        MM_MODEM_BAND_EUTRAN_7,  MM_MODEM_BAND_EUTRAN_8,  MM_MODEM_BAND_EUTRAN_11, MM_MODEM_BAND_EUTRAN_12, MM_MODEM_BAND_EUTRAN_13,
        MM_MODEM_BAND_EUTRAN_17, MM_MODEM_BAND_EUTRAN_18, MM_MODEM_BAND_EUTRAN_19, MM_MODEM_BAND_EUTRAN_20, MM_MODEM_BAND_EUTRAN_21,
        MM_MODEM_BAND_EUTRAN_26, MM_MODEM_BAND_EUTRAN_28, MM_MODEM_BAND_EUTRAN_29, MM_MODEM_BAND_EUTRAN_30, MM_MODEM_BAND_EUTRAN_38,
        MM_MODEM_BAND_EUTRAN_39, MM_MODEM_BAND_EUTRAN_40, MM_MODEM_BAND_EUTRAN_41, MM_MODEM_BAND_EUTRAN_66
    };

    validate_xact_query_response (response,
                                  &expected_mode,
                                  expected_bands, G_N_ELEMENTS (expected_bands));
}

/*****************************************************************************/

#define XACT_SET_TEST_MAX_BANDS 6

typedef struct {
    MMModemMode  allowed;
    MMModemMode  preferred;
    MMModemBand  bands[XACT_SET_TEST_MAX_BANDS];
    const gchar *expected_command;
} XactSetTest;

static const XactSetTest set_tests[] = {
    {
        /* 2G-only, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=0,,"
    },
    {
        /* 3G-only, no explicit bands */
        .allowed   = MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=1,,"
    },
    {
        /* 4G-only, no explicit bands */
        .allowed   = MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=2,,"
    },
    {
        /* 2G+3G, none preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=3,,"
    },
    {
        /* 2G+3G, 2G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_2G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=3,0,"
    },
    {
        /* 2G+3G, 3G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_3G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=3,1,"
    },
    {
        /* 3G+4G, none preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=4,,"
    },
    {
        /* 3G+4G, 3G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_3G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=4,1,"
    },
    {
        /* 3G+4G, 4G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_4G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=4,2,"
    },
    {
        /* 2G+4G, none preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=5,,"
    },
    {
        /* 2G+4G, 2G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_2G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=5,0,"
    },
    {
        /* 2G+4G, 4G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_4G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=5,2,"
    },
    {
        /* 2G+3G+4G, none preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=6,,"
    },
    {
        /* 2G+3G+4G, 2G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_2G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=6,0,"
    },
    {
        /* 2G+3G+4G, 3G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_3G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=6,1,"
    },
    {
        /* 2G+3G+4G, 4G preferred, no explicit bands */
        .allowed   = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_4G,
        .bands = { [0] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=6,2,"
    },
    {
        /* 2G bands, no explicit modes */
        .allowed = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_EGSM,
                   [1] = MM_MODEM_BAND_DCS,
                   [2] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=,,,900,1800"
    },
    {
        /* 3G bands, no explicit modes */
        .allowed = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_UTRAN_1,
                   [1] = MM_MODEM_BAND_UTRAN_2,
                   [2] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=,,,1,2"
    },
    {
        /* 4G bands, no explicit modes */
        .allowed = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_EUTRAN_1,
                   [1] = MM_MODEM_BAND_EUTRAN_2,
                   [2] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=,,,101,102"
    },
    {
        /* 2G, 3G and 4G bands, no explicit modes */
        .allowed = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_EGSM,
                   [1] = MM_MODEM_BAND_DCS,
                   [2] = MM_MODEM_BAND_UTRAN_1,
                   [3] = MM_MODEM_BAND_UTRAN_2,
                   [4] = MM_MODEM_BAND_EUTRAN_1,
                   [5] = MM_MODEM_BAND_EUTRAN_2 },
        .expected_command = "+XACT=,,,900,1800,1,2,101,102"
    },
    {
        /* Auto bands, no explicit modes */
        .allowed = MM_MODEM_MODE_NONE,
        .preferred = MM_MODEM_MODE_NONE,
        .bands = { [0] = MM_MODEM_BAND_ANY,
                   [1] = MM_MODEM_BAND_UNKNOWN },
        .expected_command = "+XACT=,,,0"
    },

    {
        /* 2G+3G+4G with 4G preferred, and 2G+3G+4G bands */
        .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G | MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_4G,
        .bands = { [0] = MM_MODEM_BAND_EGSM,
                   [1] = MM_MODEM_BAND_DCS,
                   [2] = MM_MODEM_BAND_UTRAN_1,
                   [3] = MM_MODEM_BAND_UTRAN_2,
                   [4] = MM_MODEM_BAND_EUTRAN_1,
                   [5] = MM_MODEM_BAND_EUTRAN_2 },
        .expected_command = "+XACT=6,2,,900,1800,1,2,101,102"
    },
};

static void
validate_xact_set_command (const MMModemMode  allowed,
                           const MMModemMode  preferred,
                           const MMModemBand *bands,
                           guint              n_bands,
                           const gchar       *expected_command)
{
    gchar                  *command;
    MMModemModeCombination  mode;
    GArray                 *bandsarray = NULL;
    GError                 *error = NULL;

    if (n_bands)
        bandsarray = g_array_append_vals (g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), n_bands), bands, n_bands);

    mode.allowed   = allowed;
    mode.preferred = preferred;

    command = mm_xmm_build_xact_set_command ((mode.allowed != MM_MODEM_MODE_NONE) ? &mode : NULL, bandsarray, &error);
    g_assert_no_error (error);
    g_assert (command);

    g_assert_cmpstr (command, == , expected_command);

    g_free (command);
    if (bandsarray)
        g_array_unref (bandsarray);
}

static void
test_xact_set (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (set_tests); i++) {
        guint n_bands = 0;
        guint j;

        for (j = 0; j < XACT_SET_TEST_MAX_BANDS; j++) {
            if (set_tests[i].bands[j] != MM_MODEM_BAND_UNKNOWN)
                n_bands++;
        }

        validate_xact_set_command (set_tests[i].allowed,
                                   set_tests[i].preferred,
                                   set_tests[i].bands,
                                   n_bands,
                                   set_tests[i].expected_command);
    }
}

/*****************************************************************************/

void
_mm_log (const char *loc,
         const char *func,
         guint32     level,
         const char *fmt,
         ...)
{
#if defined ENABLE_TEST_MESSAGE_TRACES
    /* Dummy log function */
    va_list args;
    gchar *msg;

    va_start (args, fmt);
    msg = g_strdup_vprintf (fmt, args);
    va_end (args);
    g_print ("%s\n", msg);
    g_free (msg);
#endif
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/xmm/xact/test/4g-only",  test_xact_test_4g_only);
    g_test_add_func ("/MM/xmm/xact/test/3g-4g",    test_xact_test_3g_4g);
    g_test_add_func ("/MM/xmm/xact/test/2g-3g-4g", test_xact_test_2g_3g_4g);

    g_test_add_func ("/MM/xmm/xact/query/3g-only", test_xact_query_3g_only);
    g_test_add_func ("/MM/xmm/xact/query/3g-4g",   test_xact_query_3g_4g);

    g_test_add_func ("/MM/xmm/xact/set", test_xact_set);

    return g_test_run ();
}
