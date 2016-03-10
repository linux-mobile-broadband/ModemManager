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
 * Copyright (C) 2015 Telit
 *
 */
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers.h"
#include "mm-modem-helpers-telit.h"

typedef struct {
    gchar *response;
    gint result;
} CSIMResponseTest;

static CSIMResponseTest valid_csim_response_test_list [] = {
    /* The parser expects that 2nd arg contains
     * substring "63Cx" where x is an HEX string
     * representing the retry value */
    {"+CSIM:8,\"xxxx63C1\"", 1},
    {"+CSIM:8,\"xxxx63CA\"", 10},
    {"+CSIM:8,\"xxxx63CF\"", 15},
    /* The parser accepts spaces */
    {"+CSIM:8,\"xxxx63C1\"", 1},
    {"+CSIM:8, \"xxxx63C1\"", 1},
    {"+CSIM: 8, \"xxxx63C1\"", 1},
    {"+CSIM:  8, \"xxxx63C1\"", 1},
    /* the parser expects an int as first argument (2nd arg's length),
     * but does not check if it is correct */
    {"+CSIM: 10, \"63CF\"", 15},
    /* the parser expect a quotation mark at the end
     * of the response, but not at the begin */
    {"+CSIM: 10, 63CF\"", 15},
    { NULL, -1}
};

static CSIMResponseTest invalid_csim_response_test_list [] = {
    /* Test missing final quotation mark */
    {"+CSIM: 8, xxxx63CF", -1},
    /* Negative test for substring "63Cx" */
    {"+CSIM: 4, xxxx73CF\"", -1},
    /* Test missing first argument */
    {"+CSIM:xxxx63CF\"", -1},
    { NULL, -1}
};

static void
test_mm_telit_parse_csim_response (void)
{
    const gint step = 1;
    guint i;
    gint res;
    GError* error = NULL;

    /* Test valid responses */
    for (i = 0; valid_csim_response_test_list[i].response != NULL; i++) {
        res = mm_telit_parse_csim_response (step, valid_csim_response_test_list[i].response, &error);

        g_assert_no_error (error);
        g_assert_cmpint (res, ==, valid_csim_response_test_list[i].result);
    }

    /* Test invalid responses */
    for (i = 0; invalid_csim_response_test_list[i].response != NULL; i++) {
        res = mm_telit_parse_csim_response (step, invalid_csim_response_test_list[i].response, &error);

        g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
        g_assert_cmpint (res, ==, invalid_csim_response_test_list[i].result);

        if (NULL != error) {
            g_error_free (error);
            error = NULL;
        }
    }
}

static void
test_mm_bands_contains (void) {
    GArray* mm_bands;
    guint i = 1;

    mm_bands = g_array_sized_new (FALSE, TRUE, sizeof (MMModemBand), 3);

    for (i = 0; i < 3; i++)
        g_array_append_val (mm_bands, i);

    g_assert_true (mm_telit_bands_contains (mm_bands, 2));
    g_assert_true (mm_telit_bands_contains (mm_bands, 2));
    g_assert_false (mm_telit_bands_contains (mm_bands, 3));

    g_array_free (mm_bands, TRUE);
}

typedef struct {
    gchar* band_flag_str;
    guint band_flags_len;
    guint band_flags [MAX_BANDS_LIST_LEN];
} BNDFlagsTest;

static BNDFlagsTest band_flag_test[] = {
    {"0-3", 4, {0, 1, 2, 3} },
    {"0,3", 2, {0, 3} },
    {"0,2-3,5-7,9", 2, {0, 2, 3, 5, 6, 7, 9} },
    { NULL, 0, {}},
};

static void
test_parse_band_flag_str (void) {
    GError *error = NULL;
    gboolean res = FALSE;
    GArray *band_flags = NULL;
    guint i, j;

    for (i = 0; band_flag_test[i].band_flag_str != NULL; i++) {
        band_flags = g_array_new (FALSE, FALSE, sizeof (guint));
        res = mm_telit_get_band_flags_from_string (band_flag_test[i].band_flag_str,
                                                   &band_flags,
                                                   &error);
        g_assert_no_error (error);
        g_assert_true (res);

        for (j = 0; j < band_flag_test[i].band_flags_len; j++) {
            guint ref;
            guint cur;

            ref = band_flag_test[i].band_flags[j];
            cur = g_array_index (band_flags, guint, j);

            g_assert_true (ref == cur);
        }

        g_array_free (band_flags, TRUE);
    }
}

typedef struct {
    gchar* response;
    gboolean modem_is_2g;
    gboolean modem_is_3g;
    gboolean modem_is_4g;
    guint mm_bands_len;
    MMModemBand mm_bands [MAX_BANDS_LIST_LEN];
} BNDResponseTest;

static BNDResponseTest supported_band_mapping_tests [] = {
    { "#BND: (0-3),(0,2,5,6)", TRUE, TRUE, FALSE, 7, { MM_MODEM_BAND_EGSM,
                                                      MM_MODEM_BAND_DCS,
                                                      MM_MODEM_BAND_PCS,
                                                      MM_MODEM_BAND_G850,
                                                      MM_MODEM_BAND_U2100,
                                                      MM_MODEM_BAND_U850,
                                                      MM_MODEM_BAND_U900} },
    { "#BND: (0,3),(0,2,5,6)", TRUE, TRUE, FALSE, 7, { MM_MODEM_BAND_EGSM,
                                                       MM_MODEM_BAND_DCS,
                                                       MM_MODEM_BAND_PCS,
                                                       MM_MODEM_BAND_G850,
                                                       MM_MODEM_BAND_U2100,
                                                       MM_MODEM_BAND_U850,
                                                       MM_MODEM_BAND_U900} },
    { "#BND: (0,2),(0,2,5,6)", TRUE, TRUE, FALSE, 6, { MM_MODEM_BAND_EGSM,
                                                       MM_MODEM_BAND_DCS,
                                                       MM_MODEM_BAND_G850,
                                                       MM_MODEM_BAND_U2100,
                                                       MM_MODEM_BAND_U850,
                                                       MM_MODEM_BAND_U900} },
    { "#BND: (0,2),(0-4,5,6)", TRUE, TRUE, FALSE, 7, { MM_MODEM_BAND_EGSM,
                                                       MM_MODEM_BAND_DCS,
                                                       MM_MODEM_BAND_G850,
                                                       MM_MODEM_BAND_U2100,
                                                       MM_MODEM_BAND_U1900,
                                                       MM_MODEM_BAND_U850,
                                                       MM_MODEM_BAND_U900} },
    { "#BND: (0-3),(0,2,5,6),(1-1)", TRUE, TRUE, TRUE, 8, { MM_MODEM_BAND_EGSM,
                                                         MM_MODEM_BAND_DCS,
                                                         MM_MODEM_BAND_PCS,
                                                         MM_MODEM_BAND_G850,
                                                         MM_MODEM_BAND_U2100,
                                                         MM_MODEM_BAND_U850,
                                                         MM_MODEM_BAND_U900,
                                                         MM_MODEM_BAND_EUTRAN_I} },
    { "#BND: (0),(0),(1-3)", TRUE, TRUE, TRUE, 5, { MM_MODEM_BAND_EGSM,
                                                    MM_MODEM_BAND_DCS,
                                                    MM_MODEM_BAND_U2100,
                                                    MM_MODEM_BAND_EUTRAN_I,
                                                    MM_MODEM_BAND_EUTRAN_II} },
    { "#BND: (0),(0),(1-3)", FALSE, FALSE, TRUE, 2, { MM_MODEM_BAND_EUTRAN_I,
                                                      MM_MODEM_BAND_EUTRAN_II} },
    { NULL, FALSE, FALSE, FALSE, 0, {}},
};

static void
test_parse_supported_bands_response (void) {
    GError* error = NULL;
    gboolean res = FALSE;
    guint i, j;
    GArray* bands = NULL;

    for (i = 0; supported_band_mapping_tests[i].response != NULL; i++) {
        res = mm_telit_parse_bnd_response (supported_band_mapping_tests[i].response,
                                           supported_band_mapping_tests[i].modem_is_2g,
                                           supported_band_mapping_tests[i].modem_is_3g,
                                           supported_band_mapping_tests[i].modem_is_4g,
                                           LOAD_SUPPORTED_BANDS,
                                           &bands,
                                           &error);
        g_assert_no_error (error);
        g_assert_true (res);


        for (j = 0; j < supported_band_mapping_tests[i].mm_bands_len; j++) {
            MMModemBand ref;
            MMModemBand cur;

            ref = supported_band_mapping_tests[i].mm_bands[j];
            cur = g_array_index (bands, MMModemBand, j);
            g_assert_cmpint (cur, ==, ref);
        }

        g_assert_cmpint (bands->len, ==, supported_band_mapping_tests[i].mm_bands_len);

        g_array_free (bands, FALSE);
        bands = NULL;
    }
}


static BNDResponseTest current_band_mapping_tests [] = {
    { "#BND: 0,5", TRUE, TRUE, FALSE, 3, { MM_MODEM_BAND_EGSM,
                                           MM_MODEM_BAND_DCS,
                                           MM_MODEM_BAND_U900
                                         }
    },
    { "#BND: 1,3", TRUE, TRUE, FALSE, 5, { MM_MODEM_BAND_EGSM,
                                           MM_MODEM_BAND_PCS,
                                           MM_MODEM_BAND_U2100,
                                           MM_MODEM_BAND_U1900,
                                           MM_MODEM_BAND_U850,
                                         }
    },
    { "#BND: 2,7", TRUE, TRUE, FALSE, 3, { MM_MODEM_BAND_DCS,
                                           MM_MODEM_BAND_G850,
                                           MM_MODEM_BAND_U17IV
                                         },
    },
    { "#BND: 3,0,1", TRUE, TRUE, TRUE, 4, { MM_MODEM_BAND_PCS,
                                            MM_MODEM_BAND_G850,
                                            MM_MODEM_BAND_U2100,
                                            MM_MODEM_BAND_EUTRAN_I
                                          }
    },
    { "#BND: 0,0,3", TRUE, FALSE, TRUE, 4, { MM_MODEM_BAND_EGSM,
                                            MM_MODEM_BAND_DCS,
                                            MM_MODEM_BAND_EUTRAN_I,
                                            MM_MODEM_BAND_EUTRAN_II
                                          }
    },
    { "#BND: 0,0,3", FALSE, FALSE, TRUE, 2, { MM_MODEM_BAND_EUTRAN_I,
                                              MM_MODEM_BAND_EUTRAN_II
                                            }
    },
    { NULL, FALSE, FALSE, FALSE, 0, {}},
};

static void
test_parse_current_bands_response (void) {
    GError* error = NULL;
    gboolean res = FALSE;
    guint i, j;
    GArray* bands = NULL;

    for (i = 0; current_band_mapping_tests[i].response != NULL; i++) {
        res = mm_telit_parse_bnd_response (current_band_mapping_tests[i].response,
                                           current_band_mapping_tests[i].modem_is_2g,
                                           current_band_mapping_tests[i].modem_is_3g,
                                           current_band_mapping_tests[i].modem_is_4g,
                                           LOAD_CURRENT_BANDS,
                                           &bands,
                                           &error);
        g_assert_no_error (error);
        g_assert_true (res);


        for (j = 0; j < current_band_mapping_tests[i].mm_bands_len; j++) {
            MMModemBand ref;
            MMModemBand cur;

            ref = current_band_mapping_tests[i].mm_bands[j];
            cur = g_array_index (bands, MMModemBand, j);
            g_assert_cmpint (cur, ==, ref);
        }

        g_assert_cmpint (bands->len, ==, current_band_mapping_tests[i].mm_bands_len);

        g_array_free (bands, FALSE);
        bands = NULL;
    }
}

static void
test_telit_get_2g_bnd_flag (void)
{
    GArray *bands_array;
    gint flag2g;
    MMModemBand egsm = MM_MODEM_BAND_EGSM;
    MMModemBand dcs = MM_MODEM_BAND_DCS;
    MMModemBand pcs = MM_MODEM_BAND_PCS;
    MMModemBand g850 = MM_MODEM_BAND_G850;

    /* Test Flag 0 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, egsm);
    g_array_append_val (bands_array, dcs);

    mm_telit_get_band_flag (bands_array, &flag2g, NULL, NULL);
    g_assert_cmpuint (flag2g, ==, 0);
    g_array_free (bands_array, TRUE);

    /* Test flag 1 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, egsm);
    g_array_append_val (bands_array, pcs);

    mm_telit_get_band_flag (bands_array, &flag2g, NULL, NULL);
    g_assert_cmpuint (flag2g, ==, 1);
    g_array_free (bands_array, TRUE);

    /* Test flag 2 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, g850);
    g_array_append_val (bands_array, dcs);

    mm_telit_get_band_flag (bands_array, &flag2g, NULL, NULL);
    g_assert_cmpuint (flag2g, ==, 2);
    g_array_free (bands_array, TRUE);

    /* Test flag 3 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, g850);
    g_array_append_val (bands_array, pcs);

    mm_telit_get_band_flag (bands_array, &flag2g, NULL, NULL);
    g_assert_cmpuint (flag2g, ==, 3);
    g_array_free (bands_array, TRUE);

    /* Test invalid band array */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, g850);
    g_array_append_val (bands_array, egsm);

    mm_telit_get_band_flag (bands_array, &flag2g, NULL, NULL);
    g_assert_cmpuint (flag2g, ==, -1);
    g_array_free (bands_array, TRUE);
}


static void
test_telit_get_3g_bnd_flag (void)
{
    GArray *bands_array;
    MMModemBand u2100 = MM_MODEM_BAND_U2100;
    MMModemBand u1900 = MM_MODEM_BAND_U1900;
    MMModemBand u850 = MM_MODEM_BAND_U850;
    MMModemBand u900 = MM_MODEM_BAND_U900;
    MMModemBand u17iv = MM_MODEM_BAND_U17IV;
    MMModemBand u17ix = MM_MODEM_BAND_U17IX;
    gint flag;

    /* Test flag 0 */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u2100);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, 0);
    g_array_free (bands_array, TRUE);

    /* Test flag 1 */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u1900);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, 1);
    g_array_free (bands_array, TRUE);

    /* Test flag 2 */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u850);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, 2);
    g_array_free (bands_array, TRUE);

    /* Test flag 3 */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 3);
    g_array_append_val (bands_array, u2100);
    g_array_append_val (bands_array, u1900);
    g_array_append_val (bands_array, u850);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, 3);
    g_array_free (bands_array, TRUE);

    /* Test flag 4 */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, u1900);
    g_array_append_val (bands_array, u850);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, 4);
    g_array_free (bands_array, TRUE);

    /* Test flag 5 */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u900);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, 5);
    g_array_free (bands_array, TRUE);

    /* Test flag 6 */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, u2100);
    g_array_append_val (bands_array, u900);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, 6);
    g_array_free (bands_array, TRUE);

    /* Test flag 7 */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u17iv);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, 7);
    g_array_free (bands_array, TRUE);

    /* Test invalid band array */
    flag = -1;
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, u17ix);

    mm_telit_get_band_flag (bands_array, NULL, &flag, NULL);
    g_assert_cmpint (flag, ==, -1);
    g_array_free (bands_array, TRUE);
}

static void
test_telit_get_4g_bnd_flag (void)
{
    GArray *bands_array;
    MMModemBand eutran_i = MM_MODEM_BAND_EUTRAN_I;
    MMModemBand eutran_ii = MM_MODEM_BAND_EUTRAN_II;
    MMModemBand egsm = MM_MODEM_BAND_EGSM;
    gint flag = -1;

    /* Test flag 1 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, eutran_i);

    mm_telit_get_band_flag (bands_array, NULL, NULL, &flag);
    g_assert_cmpint (flag, ==, 1);
    g_array_free (bands_array, TRUE);

    /* Test flag 3 */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 2);
    g_array_append_val (bands_array, eutran_i);
    g_array_append_val (bands_array, eutran_ii);

    mm_telit_get_band_flag (bands_array, NULL, NULL, &flag);
    g_assert_cmpint (flag, ==, 3);
    g_array_free (bands_array, TRUE);

    /* Test invalid bands array */
    bands_array = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 1);
    g_array_append_val (bands_array, egsm);

    mm_telit_get_band_flag (bands_array, NULL, NULL, &flag);
    g_assert_cmpint (flag, ==, -1);
    g_array_free (bands_array, TRUE);
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/telit/csim", test_mm_telit_parse_csim_response);
    g_test_add_func ("/MM/telit/bands/supported/bands_contains", test_mm_bands_contains);
    g_test_add_func ("/MM/telit/bands/supported/parse_band_flag", test_parse_band_flag_str);
    g_test_add_func ("/MM/telit/bands/supported/parse_bands_response", test_parse_supported_bands_response);
    g_test_add_func ("/MM/telit/bands/current/parse_bands_response", test_parse_current_bands_response);
    g_test_add_func ("/MM/telit/bands/current/set_bands/2g", test_telit_get_2g_bnd_flag);
    g_test_add_func ("/MM/telit/bands/current/set_bands/3g", test_telit_get_3g_bnd_flag);
    g_test_add_func ("/MM/telit/bands/current/set_bands/4g", test_telit_get_4g_bnd_flag);
    return g_test_run ();
}
