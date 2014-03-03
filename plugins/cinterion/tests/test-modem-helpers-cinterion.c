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
 * Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-cinterion.h"

static gint
sort_band (MMModemBand a, MMModemBand b)
{
    return a - b;
}

/*****************************************************************************/
/* Test ^SCFG test responses */

static void
common_test_scfg (const gchar *response,
                  GArray *expected_bands)
{
    GArray *bands = NULL;
    gchar *expected_bands_str;
    gchar *bands_str;
    GError *error = NULL;
    gboolean res;

    res = mm_cinterion_parse_scfg_test (response,
                                        MM_MODEM_CHARSET_UNKNOWN,
                                        &bands,
                                        &error);
    g_assert_no_error (error);
    g_assert (res == TRUE);
    g_assert (bands != NULL);

    g_array_sort (bands, (GCompareFunc)sort_band);
    g_array_sort (expected_bands, (GCompareFunc)sort_band);

    expected_bands_str = mm_common_build_bands_string ((const MMModemBand *)expected_bands->data,
                                                       expected_bands->len);
    bands_str = mm_common_build_bands_string ((const MMModemBand *)bands->data,
                                              bands->len);

    /* Instead of comparing the array one by one, compare the strings built from the mask
     * (we get a nicer error if it fails) */
    g_assert_cmpstr (bands_str, ==, expected_bands_str);

    g_free (bands_str);
    g_free (expected_bands_str);
}

static void
test_scfg (void)
{
    GArray *expected_bands;
    MMModemBand single;
    const gchar *response =
        "^SCFG: \"Audio/Loop\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Call/ECC\",(\"0\"-\"255\")\r\n"
        "^SCFG: \"Call/Speech/Codec\",(\"0\",\"1\")\r\n"
        "^SCFG: \"GPRS/Auth\",(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"GPRS/AutoAttach\",(\"disabled\",\"enabled\")\r\n"
        "^SCFG: \"GPRS/MaxDataRate/HSDPA\",(\"0\",\"1\")\r\n"
        "^SCFG: \"GPRS/MaxDataRate/HSUPA\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Ident/Manufacturer\",(25)\r\n"
        "^SCFG: \"Ident/Product\",(25)\r\n"
        "^SCFG: \"MEopMode/Airplane\",(\"off\",\"on\")\r\n"
        "^SCFG: \"MEopMode/CregRoam\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/CFUN\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/PowerMgmt/LCI\",(\"disabled\",\"enabled\")\r\n"
        "^SCFG: \"MEopMode/PowerMgmt/VExt\",(\"high\",\"low\")\r\n"
        "^SCFG: \"MEopMode/PwrSave\",(\"disabled\",\"enabled\"),(\"0-600\"),(\"1-36000\")\r\n"
        "^SCFG: \"MEopMode/RingOnData\",(\"on\",\"off\")\r\n"
        "^SCFG: \"MEopMode/RingUrcOnCall\",(\"on\",\"off\")\r\n"
        "^SCFG: \"MEShutdown/OnIgnition\",(\"on\",\"off\")\r\n"
        "^SCFG: \"Radio/Band\",(\"1-511\",\"0-1\")\r\n"
        "^SCFG: \"Radio/NWSM\",(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"Radio/OutputPowerReduction\",(\"4\"-\"8\")\r\n"
        "^SCFG: \"Serial/USB/DDD\",(\"0\",\"1\"),(\"0\"),(4),(4),(4),(63),(63),(4)\r\n"
        "^SCFG: \"URC/DstIfc\",(\"mdm\",\"app\")\r\n"
        "^SCFG: \"URC/Datamode/Ringline\",(\"off\",\"on\")\r\n"
        "^SCFG: \"URC/Ringline\",(\"off\",\"local\",\"asc0\",\"wakeup\")\r\n"
        "^SCFG: \"URC/Ringline/ActiveTime\",(\"0\",\"1\",\"2\",\"keep\")\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 9);
    single = MM_MODEM_BAND_EGSM,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_PCS,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_G850,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U2100, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U1900, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U850,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U900,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U800,  g_array_append_val (expected_bands, single);

    common_test_scfg (response, expected_bands);

    g_array_unref (expected_bands);
}

/*****************************************************************************/
/* Test ^SCFG responses */

static void
common_test_scfg_response (const gchar *response,
                           MMModemCharset charset,
                           GArray *expected_bands)
{
    GArray *bands = NULL;
    gchar *expected_bands_str;
    gchar *bands_str;
    GError *error = NULL;
    gboolean res;

    res = mm_cinterion_parse_scfg_response (response, charset, &bands, &error);
    g_assert_no_error (error);
    g_assert (res == TRUE);
    g_assert (bands != NULL);

    g_array_sort (bands, (GCompareFunc)sort_band);
    g_array_sort (expected_bands, (GCompareFunc)sort_band);

    expected_bands_str = mm_common_build_bands_string ((const MMModemBand *)expected_bands->data,
                                                       expected_bands->len);
    bands_str = mm_common_build_bands_string ((const MMModemBand *)bands->data,
                                              bands->len);

    /* Instead of comparing the array one by one, compare the strings built from the mask
     * (we get a nicer error if it fails) */
    g_assert_cmpstr (bands_str, ==, expected_bands_str);

    g_free (bands_str);
    g_free (expected_bands_str);
}

static void
test_scfg_response_2g (void)
{
    GArray *expected_bands;
    MMModemBand single;
    const gchar *response =
        "^SCFG: \"Radio/Band\",\"3\",\"3\"\r\n"
        "\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 9);
    single = MM_MODEM_BAND_EGSM,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,   g_array_append_val (expected_bands, single);

    common_test_scfg_response (response, MM_MODEM_CHARSET_UNKNOWN, expected_bands);

    g_array_unref (expected_bands);
}

static void
test_scfg_response_2g_ucs2 (void)
{
    GArray *expected_bands;
    MMModemBand single;
    const gchar *response =
        "^SCFG: \"Radio/Band\",\"0031\",\"0031\"\r\n"
        "\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 9);
    single = MM_MODEM_BAND_EGSM,  g_array_append_val (expected_bands, single);

    common_test_scfg_response (response, MM_MODEM_CHARSET_UCS2, expected_bands);

    g_array_unref (expected_bands);
}

static void
test_scfg_response_3g (void)
{
    GArray *expected_bands;
    MMModemBand single;
    const gchar *response =
        "^SCFG: \"Radio/Band\",127\r\n"
        "\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 9);
    single = MM_MODEM_BAND_EGSM,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_PCS,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_G850,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U2100, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U1900, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U850,  g_array_append_val (expected_bands, single);

    common_test_scfg_response (response, MM_MODEM_CHARSET_UNKNOWN, expected_bands);

    g_array_unref (expected_bands);
}

/*****************************************************************************/
/* Test ^SIND responses */

static void
common_test_sind_response (const gchar *response,
                           const gchar *expected_description,
                           guint expected_mode,
                           guint expected_value)
{
    GError *error = NULL;
    gboolean res;
    gchar *description;
    guint mode;
    guint value;

    res = mm_cinterion_parse_sind_response (response,
                                            &description,
                                            &mode,
                                            &value,
                                            &error);
    g_assert_no_error (error);
    g_assert (res == TRUE);

    g_assert_cmpstr (description, ==, expected_description);
    g_assert_cmpuint (mode, ==, expected_mode);
    g_assert_cmpuint (value, ==, expected_value);

    g_free (description);
}

static void
test_sind_response_simstatus (void)
{
    common_test_sind_response ("^SIND: simstatus,1,5", "simstatus", 1, 5);
}

/*****************************************************************************/

void
_mm_log (const char *loc,
         const char *func,
         guint32 level,
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

    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/cinterion/scfg",                    test_scfg);
    g_test_add_func ("/MM/cinterion/scfg/response/3g",        test_scfg_response_3g);
    g_test_add_func ("/MM/cinterion/scfg/response/2g",        test_scfg_response_2g);
    g_test_add_func ("/MM/cinterion/scfg/response/2g/ucs2",   test_scfg_response_2g_ucs2);
    g_test_add_func ("/MM/cinterion/sind/response/simstatus", test_sind_response_simstatus);

    return g_test_run ();
}
