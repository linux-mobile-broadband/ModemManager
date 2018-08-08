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

    return g_test_run ();
}
