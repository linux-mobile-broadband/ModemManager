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
    g_assert_cmpuint (g_array_index (bands, MMModemBand, 0), ==, MM_MODEM_BAND_EUTRAN_I);
    g_assert_cmpuint (g_array_index (bands, MMModemBand, 1), ==, MM_MODEM_BAND_EUTRAN_IV);
    g_assert_cmpuint (g_array_index (bands, MMModemBand, 2), ==, MM_MODEM_BAND_EUTRAN_XIII);
    g_assert_cmpuint (g_array_index (bands, MMModemBand, 3), ==, MM_MODEM_BAND_EUTRAN_XLIV);
    g_array_free (bands, TRUE);
}

/*****************************************************************************/
/* Test +CEER responses */

typedef struct {
    const gchar *str;
    const gchar *result;
} CeerTest;

static const CeerTest ceer_tests[] = {
    { "", "" }, /* Special case, sometimes the response is empty, treat it as a valid response. */
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

static void
test_parse_vendor_pco_info (void)
{
    guint pco_value;

    /* Valid PCO values */
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,1,FF00,13018400", NULL);
    g_assert (pco_value == 0);
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,1,FF00,13018403", NULL);
    g_assert (pco_value == 3);
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,1,FF00,13018405", NULL);
    g_assert (pco_value == 5);
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,3,FF00,13018400", NULL);
    g_assert (pco_value == 0);
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,3,FF00,13018403", NULL);
    g_assert (pco_value == 3);
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,3,FF00,13018405", NULL);
    g_assert (pco_value == 5);
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO:1,FF00,13018400", NULL);
    g_assert (pco_value == 0);
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO:1,FF00,13018403", NULL);
    g_assert (pco_value == 3);
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO:1,FF00,13018405", NULL);
    g_assert (pco_value == 5);
    /* Different container */
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,3,F000,13018401", NULL);
    g_assert (pco_value == -1);
    /* Invalid CID */
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,2,FF00,13018401", NULL);
    g_assert (pco_value == -1);
    /* Different payload */
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,3,FF00,13018501", NULL);
    g_assert (pco_value == -1);
    /* Bad PCO info */
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: blah,blah,FF00,13018401", NULL);
    g_assert (pco_value == -1);
    /* Multiline PCO info */
    pco_value = mm_altair_parse_vendor_pco_info ("%PCOINFO: 1,2,FF00,13018400\r\n%PCOINFO: 1,3,FF00,13018403", NULL);
    g_assert (pco_value == 3);
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/altair/parse_bands", test_parse_bands);
    g_test_add_func ("/MM/altair/ceer", test_ceer);
    g_test_add_func ("/MM/altair/parse_cid", test_parse_cid);
    g_test_add_func ("/MM/altair/parse_vendor_pco_info", test_parse_vendor_pco_info);

    return g_test_run ();
}
