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
 * Copyright (C) 2013 Google Inc.
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers-altair-lte.h"

/*****************************************************************************/
/* Test bands response parsing */

static void
test_parse_bands (void)
{
    GArray *bands;

    bands = mm_altair_parse_bands_response ("");
    g_assert (bands != NULL);
    g_assert_cmpuint (bands->len, ==, 0);
    g_array_free (bands, TRUE);

    /* 0 and 45 are outside the range of E-UTRAN operating bands and should be ignored. */
    bands = mm_altair_parse_bands_response ("0, 0, 1, 4,13,44,45");
    g_assert (bands != NULL);
    g_assert_cmpuint (bands->len, ==, 4);
    g_assert_cmpuint (g_array_index (bands, MMModemBand, 0), ==, MM_MODEM_BAND_EUTRAN_1);
    g_assert_cmpuint (g_array_index (bands, MMModemBand, 1), ==, MM_MODEM_BAND_EUTRAN_4);
    g_assert_cmpuint (g_array_index (bands, MMModemBand, 2), ==, MM_MODEM_BAND_EUTRAN_13);
    g_assert_cmpuint (g_array_index (bands, MMModemBand, 3), ==, MM_MODEM_BAND_EUTRAN_44);
    g_array_free (bands, TRUE);
}

/*****************************************************************************/
/* Test +CEER responses */

typedef struct {
    const gchar *str;
    const gchar *result;
} CeerTest;

static const CeerTest ceer_tests[] = {
    { "", "" }, /* Special case, sometimes the response is empty, treat it as a good response. */
    { "+CEER:", "" },
    { "+CEER: EPS_AND_NON_EPS_SERVICES_NOT_ALLOWED", "EPS_AND_NON_EPS_SERVICES_NOT_ALLOWED" },
    { "+CEER: NO_SUITABLE_CELLS_IN_TRACKING_AREA", "NO_SUITABLE_CELLS_IN_TRACKING_AREA" },
    { "WRONG RESPONSE", NULL },
    { NULL, NULL }
};

static void
test_ceer (void)
{
    guint i;

    for (i = 0; ceer_tests[i].str; ++i) {
        GError *error = NULL;
        gchar *result;

        result = mm_altair_parse_ceer_response (ceer_tests[i].str, &error);
        if (ceer_tests[i].result) {
            g_assert_cmpstr (ceer_tests[i].result, ==, result);
            g_assert_no_error (error);
            g_free (result);
        }
        else {
            g_assert (result == NULL);
            g_assert (error != NULL);
            g_error_free (error);
        }
    }
}

static void
test_parse_cid (void)
{
    g_assert (mm_altair_parse_cid ("%CGINFO: 2", NULL) == 2);
    g_assert (mm_altair_parse_cid ("%CGINFO:blah", NULL) == -1);
}

/*****************************************************************************/
/* Test %PCOINFO responses */

typedef struct {
    const gchar *pco_info;
    guint32 session_id;
    gsize pco_data_size;
    guint8 pco_data[50];
} TestValidPcoInfo;

static const TestValidPcoInfo good_pco_infos[] = {
    /* Valid PCO values */
    { "%PCOINFO: 1,1,FF00,13018400", 1, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x00 } },
    { "%PCOINFO: 1,1,FF00,13018403", 1, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x03 } },
    { "%PCOINFO: 1,1,FF00,13018405", 1, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x05 } },
    { "%PCOINFO: 1,3,FF00,13018400", 3, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x00 } },
    { "%PCOINFO: 1,3,FF00,13018403", 3, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x03 } },
    { "%PCOINFO: 1,3,FF00,13018405", 3, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x05 } },
    { "%PCOINFO:1,FF00,13018400", 1, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x00 } },
    { "%PCOINFO:1,FF00,13018403", 1, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x03 } },
    { "%PCOINFO:1,FF00,13018405", 1, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x05 } },
    /* Different payload */
    { "%PCOINFO: 1,3,FF00,130185", 3, 9,
      { 0x27, 0x07, 0x80, 0xFF, 0x00, 0x03, 0x13, 0x01, 0x85 } },
    /* Multiline PCO info */
    { "%PCOINFO: 1,2,FF00,13018400\r\n%PCOINFO: 1,3,FF00,13018403", 3, 10,
      { 0x27, 0x08, 0x80, 0xFF, 0x00, 0x04, 0x13, 0x01, 0x84, 0x03 } },
};

static const gchar *bad_pco_infos[] = {
    /* Different container */
    "%PCOINFO: 1,3,F000,13018401",
    /* Ingood CID */
    "%PCOINFO: 1,2,FF00,13018401",
    /* Bad PCO info */
    "%PCOINFO: blah,blah,FF00,13018401",
    /* Bad PCO payload */
    "%PCOINFO: 1,1,FF00,130184011",
};

static void
test_parse_vendor_pco_info (void)
{
    MMPco *pco;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (good_pco_infos); ++i) {
        const guint8 *pco_data;
        gsize pco_data_size;

        pco = mm_altair_parse_vendor_pco_info (good_pco_infos[i].pco_info, NULL);
        g_assert (pco != NULL);
        g_assert_cmpuint (mm_pco_get_session_id (pco), ==, good_pco_infos[i].session_id);
        g_assert (mm_pco_is_complete (pco));
        pco_data = mm_pco_get_data (pco, &pco_data_size);
        g_assert (pco_data != NULL);
        g_assert_cmpuint (pco_data_size, ==, good_pco_infos[i].pco_data_size);
        g_assert_cmpint (memcmp (pco_data, good_pco_infos[i].pco_data, pco_data_size), ==, 0);
        g_object_unref (pco);
    }

    for (i = 0; i < G_N_ELEMENTS (bad_pco_infos); ++i) {
        pco = mm_altair_parse_vendor_pco_info (bad_pco_infos[i], NULL);
        g_assert (pco == NULL);
    }
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/altair/parse_bands", test_parse_bands);
    g_test_add_func ("/MM/altair/ceer", test_ceer);
    g_test_add_func ("/MM/altair/parse_cid", test_parse_cid);
    g_test_add_func ("/MM/altair/parse_vendor_pco_info", test_parse_vendor_pco_info);

    return g_test_run ();
}
