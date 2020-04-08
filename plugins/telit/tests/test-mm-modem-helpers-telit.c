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
 * Copyright (C) 2015-2019 Telit
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 */
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-test.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-telit.h"

#include "test-helpers.h"

/******************************************************************************/

#define MAX_BANDS_LIST_LEN 17

typedef struct {
    const gchar *response;
    gboolean     modem_is_2g;
    gboolean     modem_is_3g;
    gboolean     modem_is_4g;
    gboolean     modem_alternate_3g_bands;
    guint        mm_bands_len;
    MMModemBand  mm_bands [MAX_BANDS_LIST_LEN];
} BndResponseTest;

static BndResponseTest supported_band_mapping_tests [] = {
    {
        "#BND: (0-3)", TRUE, FALSE, FALSE, FALSE, 4,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_PCS,
          MM_MODEM_BAND_G850 }
    },
    {
        "#BND: (0-3),(0,2,5,6)", TRUE, TRUE, FALSE, FALSE, 7,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_PCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_5,
          MM_MODEM_BAND_UTRAN_8 }
    },
    {
        "#BND: (0,3),(0,2,5,6)", TRUE, TRUE, FALSE, FALSE, 7,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_PCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_5,
          MM_MODEM_BAND_UTRAN_8 }
    },
    {
        "#BND: (0,2),(0,2,5,6)", TRUE, TRUE, FALSE, FALSE, 6,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_5,
          MM_MODEM_BAND_UTRAN_8 }
    },
    {
        "#BND: (0,2),(0-4,5,6)", TRUE, TRUE, FALSE, FALSE, 7,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_2,
          MM_MODEM_BAND_UTRAN_5,
          MM_MODEM_BAND_UTRAN_8 }
    },
    {
        "#BND: (0-3),(0,2,5,6),(1-1)", TRUE, TRUE, TRUE, FALSE, 8,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_PCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_5,
          MM_MODEM_BAND_UTRAN_8,
          MM_MODEM_BAND_EUTRAN_1 }
    },
    {
        "#BND: (0),(0),(1-3)", TRUE, TRUE, TRUE, FALSE, 5,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_EUTRAN_1,
          MM_MODEM_BAND_EUTRAN_2 }
    },
    {
        "#BND: (0),(0),(1-3)", FALSE, FALSE, TRUE, FALSE, 2,
        { MM_MODEM_BAND_EUTRAN_1,
          MM_MODEM_BAND_EUTRAN_2 }
    },
    /* 3G alternate band settings: default */
    {
        "#BND: (0),(0,2,5,6,12,25)", FALSE, TRUE, FALSE, FALSE, 5,
        { MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_5,
          MM_MODEM_BAND_UTRAN_8,
          MM_MODEM_BAND_UTRAN_6,
          MM_MODEM_BAND_UTRAN_19 }
    },
    /* 3G alternate band settings: alternate */
    {
        "#BND: (0),(0,2,5,6,12,13)", FALSE, TRUE, FALSE, TRUE, 4,
        { MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_3,
          MM_MODEM_BAND_UTRAN_5,
          MM_MODEM_BAND_UTRAN_8 }
    },
    /* ME910 (2G+4G device)
     * 168695967: 0xA0E189F: 0000 1010 0000 1110 0001 1000 1001 1111
     */
    {
        "#BND: (0-5),(0),(1-168695967)", TRUE, FALSE, TRUE, FALSE, 17,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_PCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_EUTRAN_1,
          MM_MODEM_BAND_EUTRAN_2,
          MM_MODEM_BAND_EUTRAN_3,
          MM_MODEM_BAND_EUTRAN_4,
          MM_MODEM_BAND_EUTRAN_5,
          MM_MODEM_BAND_EUTRAN_8,
          MM_MODEM_BAND_EUTRAN_12,
          MM_MODEM_BAND_EUTRAN_13,
          MM_MODEM_BAND_EUTRAN_18,
          MM_MODEM_BAND_EUTRAN_19,
          MM_MODEM_BAND_EUTRAN_20,
          MM_MODEM_BAND_EUTRAN_26,
          MM_MODEM_BAND_EUTRAN_28 }
    }
};

static void
test_parse_supported_bands_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (supported_band_mapping_tests); i++) {
        GError *error = NULL;
        GArray *bands = NULL;

        bands = mm_telit_parse_bnd_test_response (supported_band_mapping_tests[i].response,
                                                  supported_band_mapping_tests[i].modem_is_2g,
                                                  supported_band_mapping_tests[i].modem_is_3g,
                                                  supported_band_mapping_tests[i].modem_is_4g,
                                                  supported_band_mapping_tests[i].modem_alternate_3g_bands,
                                                  NULL,
                                                  &error);
        g_assert_no_error (error);
        g_assert (bands);

        mm_test_helpers_compare_bands (bands,
                                       supported_band_mapping_tests[i].mm_bands,
                                       supported_band_mapping_tests[i].mm_bands_len);
        g_array_unref (bands);
    }
}

static BndResponseTest current_band_mapping_tests [] = {
    {
        "#BND: 0", TRUE, FALSE, FALSE, FALSE, 2,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS }
    },
    {
        "#BND: 0,5", TRUE, TRUE, FALSE, FALSE, 3,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_UTRAN_8 }
    },
    {
        "#BND: 1,3", TRUE, TRUE, FALSE, FALSE, 5,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_PCS,
          MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_2,
          MM_MODEM_BAND_UTRAN_5 }
    },
    {
        "#BND: 2,7", TRUE, TRUE, FALSE, FALSE, 3,
        { MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_UTRAN_4 }
    },
    {
        "#BND: 3,0,1", TRUE, TRUE, TRUE, FALSE, 4,
        { MM_MODEM_BAND_PCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_EUTRAN_1 }
    },
    {
        "#BND: 0,0,3", TRUE, FALSE, TRUE, FALSE, 4,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_EUTRAN_1,
          MM_MODEM_BAND_EUTRAN_2 }
    },
    {
        "#BND: 0,0,3", FALSE, FALSE, TRUE, FALSE, 2,
        { MM_MODEM_BAND_EUTRAN_1,
          MM_MODEM_BAND_EUTRAN_2 }
    },
    /* 3G alternate band settings: default */
    {
        "#BND: 0,12", FALSE, TRUE, FALSE, FALSE, 1,
        { MM_MODEM_BAND_UTRAN_6 }
    },
    /* 3G alternate band settings: alternate */
    {
        "#BND: 0,12", FALSE, TRUE, FALSE, TRUE, 4,
        { MM_MODEM_BAND_UTRAN_1,
          MM_MODEM_BAND_UTRAN_3,
          MM_MODEM_BAND_UTRAN_5,
          MM_MODEM_BAND_UTRAN_8 }
    },
    /* ME910 (2G+4G device)
     * 168695967: 0xA0E189F: 0000 1010 0000 1110 0001 1000 1001 1111
     */
    {
        "#BND: 5,0,168695967", TRUE, FALSE, TRUE, FALSE, 17,
        { MM_MODEM_BAND_EGSM,
          MM_MODEM_BAND_DCS,
          MM_MODEM_BAND_PCS,
          MM_MODEM_BAND_G850,
          MM_MODEM_BAND_EUTRAN_1,
          MM_MODEM_BAND_EUTRAN_2,
          MM_MODEM_BAND_EUTRAN_3,
          MM_MODEM_BAND_EUTRAN_4,
          MM_MODEM_BAND_EUTRAN_5,
          MM_MODEM_BAND_EUTRAN_8,
          MM_MODEM_BAND_EUTRAN_12,
          MM_MODEM_BAND_EUTRAN_13,
          MM_MODEM_BAND_EUTRAN_18,
          MM_MODEM_BAND_EUTRAN_19,
          MM_MODEM_BAND_EUTRAN_20,
          MM_MODEM_BAND_EUTRAN_26,
          MM_MODEM_BAND_EUTRAN_28 }
    }
};

static void
test_parse_current_bands_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (current_band_mapping_tests); i++) {
        GError *error = NULL;
        GArray *bands = NULL;

        bands = mm_telit_parse_bnd_query_response (current_band_mapping_tests[i].response,
                                                   current_band_mapping_tests[i].modem_is_2g,
                                                   current_band_mapping_tests[i].modem_is_3g,
                                                   current_band_mapping_tests[i].modem_is_4g,
                                                   current_band_mapping_tests[i].modem_alternate_3g_bands,
                                                   NULL,
                                                   &error);
        g_assert_no_error (error);
        g_assert (bands);

        mm_test_helpers_compare_bands (bands,
                                       current_band_mapping_tests[i].mm_bands,
                                       current_band_mapping_tests[i].mm_bands_len);
        g_array_unref (bands);
    }
}

/******************************************************************************/

static void
test_common_bnd_cmd (const gchar *expected_cmd,
                     gboolean     modem_is_2g,
                     gboolean     modem_is_3g,
                     gboolean     modem_is_4g,
                     gboolean     modem_alternate_3g_bands,
                     GArray      *bands_array)
{
    gchar  *cmd;
    GError *error = NULL;

    cmd = mm_telit_build_bnd_request (bands_array,
                                      modem_is_2g, modem_is_3g, modem_is_4g,
                                      modem_alternate_3g_bands,
                                      &error);
    g_assert_no_error (error);
    g_assert_cmpstr (cmd, ==, expected_cmd);
    g_free (cmd);
}

#define test_common_bnd_cmd_2g(EXPECTED_CMD, BANDS_ARRAY) test_common_bnd_cmd (EXPECTED_CMD, TRUE, FALSE, FALSE, FALSE, BANDS_ARRAY)
#define test_common_bnd_cmd_3g(EXPECTED_CMD, ALTERNATE, BANDS_ARRAY) test_common_bnd_cmd (EXPECTED_CMD, FALSE, TRUE, FALSE, ALTERNATE, BANDS_ARRAY)
#define test_common_bnd_cmd_4g(EXPECTED_CMD, BANDS_ARRAY) test_common_bnd_cmd (EXPECTED_CMD, FALSE, FALSE, TRUE, FALSE, BANDS_ARRAY)

static void
test_common_bnd_cmd_error (gboolean     modem_is_2g,
                           gboolean     modem_is_3g,
                           gboolean     modem_is_4g,
                           GArray      *bands_array,
                           MMCoreError  expected_error)
{
    gchar  *cmd;
    GError *error = NULL;

    cmd = mm_telit_build_bnd_request (bands_array,
                                      modem_is_2g, modem_is_3g, modem_is_4g,
                                      FALSE,
                                      &error);
    g_assert_error (error, MM_CORE_ERROR, (gint)expected_error);
    g_assert (!cmd);
}

#define test_common_bnd_cmd_2g_invalid(BANDS_ARRAY) test_common_bnd_cmd_error (TRUE, FALSE, FALSE, BANDS_ARRAY, MM_CORE_ERROR_FAILED)
#define test_common_bnd_cmd_3g_invalid(BANDS_ARRAY) test_common_bnd_cmd_error (FALSE, TRUE, FALSE, BANDS_ARRAY, MM_CORE_ERROR_FAILED)
#define test_common_bnd_cmd_4g_invalid(BANDS_ARRAY) test_common_bnd_cmd_error (FALSE, FALSE, TRUE, BANDS_ARRAY, MM_CORE_ERROR_FAILED)
#define test_common_bnd_cmd_2g_not_found(BANDS_ARRAY) test_common_bnd_cmd_error (TRUE, FALSE, FALSE, BANDS_ARRAY, MM_CORE_ERROR_NOT_FOUND)
#define test_common_bnd_cmd_3g_not_found(BANDS_ARRAY) test_common_bnd_cmd_error (FALSE, TRUE, FALSE, BANDS_ARRAY, MM_CORE_ERROR_NOT_FOUND)
#define test_common_bnd_cmd_4g_not_found(BANDS_ARRAY) test_common_bnd_cmd_error (FALSE, FALSE, TRUE, BANDS_ARRAY, MM_CORE_ERROR_NOT_FOUND)

static void
test_telit_get_2g_bnd_flag (void)
{
    GArray      *bands_array;
    MMModemBand  egsm = MM_MODEM_BAND_EGSM;
    MMModemBand  dcs  = MM_MODEM_BAND_DCS;
    MMModemBand  pcs  = MM_MODEM_BAND_PCS;
    MMModemBand  g850 = MM_MODEM_BAND_G850;
    MMModemBand  u2100 = MM_MODEM_BAND_UTRAN_1;
    MMModemBand  eutran_i = MM_MODEM_BAND_EUTRAN_1;

    /* Test Flag 0 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, egsm);
    g_array_append_val (bands_array, dcs);
    test_common_bnd_cmd_2g ("#BND=0", bands_array);
    g_array_unref (bands_array);

    /* Test flag 1 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, egsm);
    g_array_append_val (bands_array, pcs);
    test_common_bnd_cmd_2g ("#BND=1", bands_array);
    g_array_unref (bands_array);

    /* Test flag 2 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, g850);
    g_array_append_val (bands_array, dcs);
    test_common_bnd_cmd_2g ("#BND=2", bands_array);
    g_array_unref (bands_array);

    /* Test flag 3 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, g850);
    g_array_append_val (bands_array, pcs);
    test_common_bnd_cmd_2g ("#BND=3", bands_array);
    g_array_unref (bands_array);

    /* Test invalid band array */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, g850);
    g_array_append_val (bands_array, egsm);
    test_common_bnd_cmd_2g_invalid (bands_array);
    g_array_unref (bands_array);

    /* Test unmatched band array */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, u2100);
    g_array_append_val (bands_array, eutran_i);
    test_common_bnd_cmd_2g_not_found (bands_array);
    g_array_unref (bands_array);
}

static void
test_telit_get_3g_bnd_flag (void)
{
    GArray      *bands_array;
    MMModemBand  u2100 = MM_MODEM_BAND_UTRAN_1;
    MMModemBand  u1900 = MM_MODEM_BAND_UTRAN_2;
    MMModemBand  u1800 = MM_MODEM_BAND_UTRAN_3;
    MMModemBand  u850  = MM_MODEM_BAND_UTRAN_5;
    MMModemBand  u800  = MM_MODEM_BAND_UTRAN_6;
    MMModemBand  u900  = MM_MODEM_BAND_UTRAN_8;
    MMModemBand  u17iv = MM_MODEM_BAND_UTRAN_4;
    MMModemBand  u17ix = MM_MODEM_BAND_UTRAN_9;
    MMModemBand  egsm = MM_MODEM_BAND_EGSM;
    MMModemBand  eutran_i = MM_MODEM_BAND_EUTRAN_1;

    /* Test flag 0 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u2100);
    test_common_bnd_cmd_3g ("#BND=0,0", FALSE, bands_array);
    test_common_bnd_cmd_3g ("#BND=0,0", TRUE,  bands_array);
    g_array_unref (bands_array);

    /* Test flag 1 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u1900);
    test_common_bnd_cmd_3g ("#BND=0,1", FALSE, bands_array);
    test_common_bnd_cmd_3g ("#BND=0,1", TRUE,  bands_array);
    g_array_unref (bands_array);

    /* Test flag 2 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u850);
    test_common_bnd_cmd_3g ("#BND=0,2", FALSE, bands_array);
    test_common_bnd_cmd_3g ("#BND=0,2", TRUE,  bands_array);
    g_array_unref (bands_array);

    /* Test flag 3 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 3);
    g_array_append_val (bands_array, u2100);
    g_array_append_val (bands_array, u1900);
    g_array_append_val (bands_array, u850);
    test_common_bnd_cmd_3g ("#BND=0,3", FALSE, bands_array);
    test_common_bnd_cmd_3g ("#BND=0,3", TRUE,  bands_array);
    g_array_unref (bands_array);

    /* Test flag 4 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, u1900);
    g_array_append_val (bands_array, u850);
    test_common_bnd_cmd_3g ("#BND=0,4", FALSE, bands_array);
    test_common_bnd_cmd_3g ("#BND=0,4", TRUE,  bands_array);
    g_array_unref (bands_array);

    /* Test flag 5 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u900);
    test_common_bnd_cmd_3g ("#BND=0,5", FALSE, bands_array);
    test_common_bnd_cmd_3g ("#BND=0,5", TRUE,  bands_array);
    g_array_unref (bands_array);

    /* Test flag 6 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, u2100);
    g_array_append_val (bands_array, u900);
    test_common_bnd_cmd_3g ("#BND=0,6", FALSE, bands_array);
    test_common_bnd_cmd_3g ("#BND=0,6", TRUE,  bands_array);
    g_array_unref (bands_array);

    /* Test flag 7 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u17iv);
    test_common_bnd_cmd_3g ("#BND=0,7", FALSE, bands_array);
    test_common_bnd_cmd_3g ("#BND=0,7", TRUE,  bands_array);
    g_array_unref (bands_array);

    /* Test flag 12 in default */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u800);
    test_common_bnd_cmd_3g ("#BND=0,12", FALSE, bands_array);
    g_array_unref (bands_array);

    /* Test flag 12 in alternate */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
    g_array_append_val (bands_array, u2100);
    g_array_append_val (bands_array, u1800);
    g_array_append_val (bands_array, u850);
    g_array_append_val (bands_array, u900);
    test_common_bnd_cmd_3g ("#BND=0,12", TRUE, bands_array);
    g_array_unref (bands_array);

    /* Test invalid band array */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u17ix);
    test_common_bnd_cmd_3g_invalid (bands_array);
    g_array_unref (bands_array);

    /* Test unmatched band array */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, egsm);
    g_array_append_val (bands_array, eutran_i);
    test_common_bnd_cmd_3g_not_found (bands_array);
    g_array_unref (bands_array);
}

static void
test_telit_get_4g_bnd_flag (void)
{
    GArray      *bands_array;
    MMModemBand  eutran_i = MM_MODEM_BAND_EUTRAN_1;
    MMModemBand  eutran_ii = MM_MODEM_BAND_EUTRAN_2;
    MMModemBand  u2100 = MM_MODEM_BAND_UTRAN_1;
    MMModemBand  egsm = MM_MODEM_BAND_EGSM;

    /* Test flag 1 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, eutran_i);
    test_common_bnd_cmd_4g ("#BND=0,0,1", bands_array);
    g_array_unref (bands_array);

    /* Test flag 3 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, eutran_i);
    g_array_append_val (bands_array, eutran_ii);
    test_common_bnd_cmd_4g ("#BND=0,0,3", bands_array);
    g_array_unref (bands_array);

    /* Test unmatched band array */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, egsm);
    g_array_append_val (bands_array, u2100);
    test_common_bnd_cmd_4g_not_found (bands_array);
    g_array_unref (bands_array);
}

/******************************************************************************/

typedef struct {
    const char* response;
    MMTelitQssStatus expected_qss;
    const char *error_message;
} QssParseTest;

static QssParseTest qss_parse_tests [] = {
    {"#QSS: 0,0", QSS_STATUS_SIM_REMOVED, NULL},
    {"#QSS: 1,0", QSS_STATUS_SIM_REMOVED, NULL},
    {"#QSS: 0,1", QSS_STATUS_SIM_INSERTED, NULL},
    {"#QSS: 0,2", QSS_STATUS_SIM_INSERTED_AND_UNLOCKED, NULL},
    {"#QSS: 0,3", QSS_STATUS_SIM_INSERTED_AND_READY, NULL},
    {"#QSS:0,3", QSS_STATUS_SIM_INSERTED_AND_READY, NULL},
    {"#QSS: 0, 3", QSS_STATUS_SIM_INSERTED_AND_READY, NULL},
    {"#QSS: 0", QSS_STATUS_UNKNOWN, "Could not parse \"#QSS?\" response: #QSS: 0"},
    {"QSS:0,1", QSS_STATUS_UNKNOWN, "Could not parse \"#QSS?\" response: QSS:0,1"},
    {"#QSS: 0,5", QSS_STATUS_UNKNOWN, "Unknown QSS status value given: 5"},
};

static void
test_telit_parse_qss_query (void)
{
    MMTelitQssStatus actual_qss_status;
    GError *error = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (qss_parse_tests); i++) {
        actual_qss_status = mm_telit_parse_qss_query (qss_parse_tests[i].response, &error);

        g_assert_cmpint (actual_qss_status, ==, qss_parse_tests[i].expected_qss);
        if (qss_parse_tests[i].error_message) {
            g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
            g_assert_cmpstr (error->message, ==, qss_parse_tests[i].error_message);
            g_clear_error (&error);
        }
    }
}

/******************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/telit/bands/supported/parse_bands_response", test_parse_supported_bands_response);
    g_test_add_func ("/MM/telit/bands/current/parse_bands_response", test_parse_current_bands_response);
    g_test_add_func ("/MM/telit/bands/current/set_bands/2g", test_telit_get_2g_bnd_flag);
    g_test_add_func ("/MM/telit/bands/current/set_bands/3g", test_telit_get_3g_bnd_flag);
    g_test_add_func ("/MM/telit/bands/current/set_bands/4g", test_telit_get_4g_bnd_flag);
    g_test_add_func ("/MM/telit/qss/query", test_telit_parse_qss_query);
    return g_test_run ();
}
