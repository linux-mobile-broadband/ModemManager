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
    g_array_unref (bands);
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

static void
test_scfg_ehs5 (void)
{
    GArray *expected_bands;
    MMModemBand single;
    const gchar *response =
        "^SCFG: \"Audio/Loop\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Call/ECC\",(\"0\"-\"255\")\r\n"
        "^SCFG: \"Call/Ecall/AckTimeout\",(\"0-2147483646\")\r\n"
        "^SCFG: \"Call/Ecall/Callback\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Call/Ecall/CallbackTimeout\",(\"0-2147483646\")\r\n"
        "^SCFG: \"Call/Ecall/Msd\",(\"280\")\r\n"
        "^SCFG: \"Call/Ecall/Pullmode\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Call/Ecall/SessionTimeout\",(\"0-2147483646\")\r\n"
        "^SCFG: \"Call/Ecall/StartTimeout\",(\"0-2147483646\")\r\n"
        "^SCFG: \"Call/Speech/Codec\",(\"0\",\"1\")\r\n"
        "^SCFG: \"GPRS/AutoAttach\",(\"disabled\",\"enabled\")\r\n"
        "^SCFG: \"Gpio/mode/ASC1\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/DAI\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/DCD0\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/DSR0\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/DTR0\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/FSR\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/PULSE\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/PWM\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/RING0\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/SPI\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/SYNC\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Ident/Manufacturer\",(25)\r\n"
        "^SCFG: \"Ident/Product\",(25)\r\n"
        "^SCFG: \"MEShutdown/Fso\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEShutdown/sVsup/threshold\",(\"-4\",\"-3\",\"-2\",\"-1\",\"0\",\"1\",\"2\",\"3\",\"4\"),(\"0\")\r\n"
        "^SCFG: \"MEopMode/CFUN\",(\"0\",\"1\"),(\"1\",\"4\")\r\n"
        "^SCFG: \"MEopMode/Dormancy\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/SoR\",(\"off\",\"on\")\r\n"
        "^SCFG: \"Radio/Band\",(\"1\"-\"147\")\r\n"
        "^SCFG: \"Radio/Mtpl\",(\"0\"-\"3\"),(\"1\"-\"8\"),(\"1\",\"8\"),(\"18\"-\"33\"),(\"18\"-\"27\")\r\n"
        "^SCFG: \"Radio/Mtpl\",(\"0\"-\"3\"),(\"1\"-\"8\"),(\"16\",\"32\",\"64\",\"128\",\"256\"),(\"18\"-\"24\")\r\n"
        "^SCFG: \"Radio/Mtpl\",(\"0\"-\"3\"),(\"1\"-\"8\"),(\"2\",\"4\"),(\"18\"-\"30\"),(\"18\"-\"26\")\r\n"
        "^SCFG: \"Radio/OutputPowerReduction\",(\"0\",\"1\",\"2\",\"3\",\"4\")\r\n"
        "^SCFG: \"Serial/Interface/Allocation\",(\"0\",\"1\",\"2\"),(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"Serial/USB/DDD\",(\"0\",\"1\"),(\"0\"),(4),(4),(4),(63),(63),(4)\r\n"
        "^SCFG: \"Tcp/IRT\",(\"1\"-\"60\")\r\n"
        "^SCFG: \"Tcp/MR\",(\"1\"-\"30\")\r\n"
        "^SCFG: \"Tcp/OT\",(\"1\"-\"6000\")\r\n"
        "^SCFG: \"Tcp/WithURCs\",(\"on\",\"off\")\r\n"
        "^SCFG: \"Trace/Syslog/OTAP\",(\"0\",\"1\"),(\"null\",\"asc0\",\"asc1\",\"usb\",\"usb1\",\"usb2\",\"usb3\",\"usb4\",\"usb5\",\"file\",\"udp\",\"system\"),(\"1\"-\"65535\"),(125),(\"buffered\",\"secure\"),(\"off\",\"on\")\r\n"
        "^SCFG: \"URC/Ringline\",(\"off\",\"local\",\"asc0\")\r\n"
        "^SCFG: \"URC/Ringline/ActiveTime\",(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"Userware/Autostart\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Userware/Autostart/Delay\",(\"0\"-\"10000\")\r\n"
        "^SCFG: \"Userware/DebugInterface\",(\"0\"-\"255\")|(\"FE80::\"-\"FE80::FFFFFFFFFFFFFFFF\"),(\"0\"-\"255\")|(\"FE80::\"-\"FE80::FFFFFFFFFFFFFFFF\"),(\"0\",\"1\")\r\n"
        "^SCFG: \"Userware/DebugMode\",(\"off\",\"on\")\r\n"
        "^SCFG: \"Userware/Passwd\",(\"0\"-\"8\")\r\n"
        "^SCFG: \"Userware/Stdout\",(\"null\",\"asc0\",\"asc1\",\"usb\",\"usb1\",\"usb2\",\"usb3\",\"usb4\",\"usb5\",\"file\",\"udp\",\"system\"),(\"1\"-\"65535\"),(\"0\"-\"125\"),(\"buffered\",\"secure\"),(\"off\",\"on\")\r\n"
        "^SCFG: \"Userware/Watchdog\",(\"0\",\"1\",\"2\")\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 4);
    single = MM_MODEM_BAND_EGSM,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U2100, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_U900,  g_array_append_val (expected_bands, single);

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
    g_array_unref (bands);
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
/* Test ^SCFG test */

static void
compare_arrays (const GArray *supported,
                const GArray *expected)
{
    guint i;

    g_assert_cmpuint (supported->len, ==, expected->len);
    for (i = 0; i < supported->len; i++) {
        gboolean found = FALSE;
        guint j;

        for (j = 0; j < expected->len && !found; j++) {
            if (g_array_index (supported, guint, i) == g_array_index (expected, guint, j))
                found = TRUE;
        }
        g_assert (found);
    }
}

static void
common_test_cnmi (const gchar *response,
                  const GArray *expected_mode,
                  const GArray *expected_mt,
                  const GArray *expected_bm,
                  const GArray *expected_ds,
                  const GArray *expected_bfr)
{
    GArray *supported_mode = NULL;
    GArray *supported_mt = NULL;
    GArray *supported_bm = NULL;
    GArray *supported_ds = NULL;
    GArray *supported_bfr = NULL;
    GError *error = NULL;
    gboolean res;

    g_assert (expected_mode != NULL);
    g_assert (expected_mt != NULL);
    g_assert (expected_bm != NULL);
    g_assert (expected_ds != NULL);
    g_assert (expected_bfr != NULL);

    res = mm_cinterion_parse_cnmi_test (response,
                                        &supported_mode,
                                        &supported_mt,
                                        &supported_bm,
                                        &supported_ds,
                                        &supported_bfr,
                                        &error);
    g_assert_no_error (error);
    g_assert (res == TRUE);
    g_assert (supported_mode != NULL);
    g_assert (supported_mt != NULL);
    g_assert (supported_bm != NULL);
    g_assert (supported_ds != NULL);
    g_assert (supported_bfr != NULL);

    compare_arrays (supported_mode, expected_mode);
    compare_arrays (supported_mt,   expected_mt);
    compare_arrays (supported_bm,   expected_bm);
    compare_arrays (supported_ds,   expected_ds);
    compare_arrays (supported_bfr,  expected_bfr);

    g_array_unref (supported_mode);
    g_array_unref (supported_mt);
    g_array_unref (supported_bm);
    g_array_unref (supported_ds);
    g_array_unref (supported_bfr);
}

static void
test_cnmi_phs8 (void)
{
    GArray *expected_mode;
    GArray *expected_mt;
    GArray *expected_bm;
    GArray *expected_ds;
    GArray *expected_bfr;
    guint val;
    const gchar *response =
        "+CNMI: (0,1,2),(0,1),(0,2),(0),(1)\r\n"
        "\r\n";

    expected_mode = g_array_sized_new (FALSE, FALSE, sizeof (guint), 3);
    val = 0, g_array_append_val (expected_mode, val);
    val = 1, g_array_append_val (expected_mode, val);
    val = 2, g_array_append_val (expected_mode, val);

    expected_mt = g_array_sized_new (FALSE, FALSE, sizeof (guint), 2);
    val = 0, g_array_append_val (expected_mt, val);
    val = 1, g_array_append_val (expected_mt, val);

    expected_bm = g_array_sized_new (FALSE, FALSE, sizeof (guint), 2);
    val = 0, g_array_append_val (expected_bm, val);
    val = 2, g_array_append_val (expected_bm, val);

    expected_ds = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
    val = 0, g_array_append_val (expected_ds, val);

    expected_bfr = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
    val = 1, g_array_append_val (expected_bfr, val);

    common_test_cnmi (response,
                      expected_mode,
                      expected_mt,
                      expected_bm,
                      expected_ds,
                      expected_bfr);

    g_array_unref (expected_mode);
    g_array_unref (expected_mt);
    g_array_unref (expected_bm);
    g_array_unref (expected_ds);
    g_array_unref (expected_bfr);}

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
/* Test ^SMONG responses */

static void
common_test_smong_response (const gchar             *response,
                            MMModemAccessTechnology  expected_access_tech)
{
    GError                  *error = NULL;
    gboolean                 res;
    MMModemAccessTechnology  access_tech;

    res = mm_cinterion_parse_smong_response (response, &access_tech, &error);
    g_assert_no_error (error);
    g_assert (res == TRUE);

    g_assert_cmpuint (access_tech, ==, expected_access_tech);
}

static void
test_smong_response_tc63i (void)
{
    const gchar *response =
        "\r\n"
        "GPRS Monitor\r\n"
        "BCCH  G  PBCCH  PAT MCC  MNC  NOM  TA      RAC                               # Cell #\r\n"
        "0073  1  -      -   262   02  2    00 01\r\n";
    common_test_smong_response (response, MM_MODEM_ACCESS_TECHNOLOGY_GPRS);
}

static void
test_smong_response_other (void)
{
    const gchar *response =
        "\r\n"
        "GPRS Monitor\r\n"
        "\r\n"
        "BCCH  G  PBCCH  PAT MCC  MNC  NOM  TA      RAC                              # Cell #\r\n"
        "  44  1  -      -   234   10  -    -       -                                             \r\n";
    common_test_smong_response (response, MM_MODEM_ACCESS_TECHNOLOGY_GPRS);
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
    g_test_add_func ("/MM/cinterion/scfg/ehs5",               test_scfg_ehs5);
    g_test_add_func ("/MM/cinterion/scfg/response/3g",        test_scfg_response_3g);
    g_test_add_func ("/MM/cinterion/scfg/response/2g",        test_scfg_response_2g);
    g_test_add_func ("/MM/cinterion/scfg/response/2g/ucs2",   test_scfg_response_2g_ucs2);
    g_test_add_func ("/MM/cinterion/cnmi/phs8",               test_cnmi_phs8);
    g_test_add_func ("/MM/cinterion/sind/response/simstatus", test_sind_response_simstatus);
    g_test_add_func ("/MM/cinterion/smong/response/tc63i",    test_smong_response_tc63i);
    g_test_add_func ("/MM/cinterion/smong/response/other",    test_smong_response_other);

    return g_test_run ();
}
