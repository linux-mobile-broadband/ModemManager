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

    mm_common_bands_garray_sort (bands);
    mm_common_bands_garray_sort (expected_bands);

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
    single = MM_MODEM_BAND_EGSM,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_PCS,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_G850,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_1, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_2, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_5, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_8, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_6, g_array_append_val (expected_bands, single);

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
    single = MM_MODEM_BAND_EGSM,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_1, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_8, g_array_append_val (expected_bands, single);

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

    mm_common_bands_garray_sort (bands);
    mm_common_bands_garray_sort (expected_bands);

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
    single = MM_MODEM_BAND_EGSM,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_PCS,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_G850,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_1, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_2, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_5, g_array_append_val (expected_bands, single);

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
    g_array_unref (expected_bfr);
}

static void
test_cnmi_other (void)
{
    GArray *expected_mode;
    GArray *expected_mt;
    GArray *expected_bm;
    GArray *expected_ds;
    GArray *expected_bfr;
    guint val;
    const gchar *response =
        "+CNMI: (0-3),(0,1),(0,2,3),(0,2),(1)\r\n"
        "\r\n";

    expected_mode = g_array_sized_new (FALSE, FALSE, sizeof (guint), 3);
    val = 0, g_array_append_val (expected_mode, val);
    val = 1, g_array_append_val (expected_mode, val);
    val = 2, g_array_append_val (expected_mode, val);
    val = 3, g_array_append_val (expected_mode, val);

    expected_mt = g_array_sized_new (FALSE, FALSE, sizeof (guint), 2);
    val = 0, g_array_append_val (expected_mt, val);
    val = 1, g_array_append_val (expected_mt, val);

    expected_bm = g_array_sized_new (FALSE, FALSE, sizeof (guint), 2);
    val = 0, g_array_append_val (expected_bm, val);
    val = 2, g_array_append_val (expected_bm, val);
    val = 3, g_array_append_val (expected_bm, val);

    expected_ds = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
    val = 0, g_array_append_val (expected_ds, val);
    val = 2, g_array_append_val (expected_ds, val);

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
    g_array_unref (expected_bfr);
}

/*****************************************************************************/
/* Test ^SWWAN read */

#define SWWAN_TEST_MAX_CIDS 2

typedef struct {
    guint                    cid;
    MMBearerConnectionStatus state;
} PdpContextState;

typedef struct {
    const gchar     *response;
    PdpContextState  expected_items[SWWAN_TEST_MAX_CIDS];
    gboolean         skip_test_other_cids;
} SwwanTest;

/* Note: all tests are based on checking CIDs 2 and 3 */
static const SwwanTest swwan_tests[] = {
    /* No active PDP context reported (all disconnected) */
    {
        .response = "",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED }
        },
        /* Don't test other CIDs because for those we would also return
         * DISCONNECTED, not UNKNOWN. */
        .skip_test_other_cids = TRUE
    },
    /* Single PDP context active (short version without interface index) */
    {
        .response = "^SWWAN: 3,1\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_UNKNOWN   },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_CONNECTED }
        }
    },
    /* Single PDP context active (long version with interface index) */
    {
        .response = "^SWWAN: 3,1,1\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_UNKNOWN   },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_CONNECTED }
        }
    },
    /* Single PDP context inactive (short version without interface index) */
    {
        .response = "^SWWAN: 3,0\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_UNKNOWN      },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED }
        }
    },
    /* Single PDP context inactive (long version with interface index) */
    {
        .response = "^SWWAN: 3,0,1\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_UNKNOWN      },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED }
        }
    },
    /* Multiple PDP contexts active (short version without interface index) */
    {
        .response = "^SWWAN: 2,1\r\n^SWWAN: 3,1\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_CONNECTED },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_CONNECTED }
        }
    },
    /* Multiple PDP contexts active (long version with interface index) */
    {
        .response = "^SWWAN: 2,1,3\r\n^SWWAN: 3,1,1\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_CONNECTED },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_CONNECTED }
        }
    },
    /* Multiple PDP contexts inactive (short version without interface index) */
    {
        .response = "^SWWAN: 2,0\r\n^SWWAN: 3,0\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED }
        }
    },
    /* Multiple PDP contexts inactive (long version with interface index) */
    {
        .response = "^SWWAN: 2,0,3\r\n^SWWAN: 3,0,1\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED }
        }
    },
    /* Multiple PDP contexts active/inactive (short version without interface index) */
    {
        .response = "^SWWAN: 2,0\r\n^SWWAN: 3,1\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_CONNECTED    }
        }
    },
    /* Multiple PDP contexts active/inactive (long version with interface index) */
    {
        .response = "^SWWAN: 2,0,3\r\n^SWWAN: 3,1,1\r\n",
        .expected_items = {
            { .cid = 2, .state = MM_BEARER_CONNECTION_STATUS_DISCONNECTED },
            { .cid = 3, .state = MM_BEARER_CONNECTION_STATUS_CONNECTED    }
        }
    }
};

static void
test_swwan_pls8 (void)
{
    MMBearerConnectionStatus  read_state;
    GError                   *error = NULL;
    guint                     i;

    /* Base tests for successful responses */
    for (i = 0; i < G_N_ELEMENTS (swwan_tests); i++) {
        guint j;

        /* Query for the expected items (CIDs 2 and 3) */
        for (j = 0; j < SWWAN_TEST_MAX_CIDS; j++) {
            read_state = mm_cinterion_parse_swwan_response (swwan_tests[i].response, swwan_tests[i].expected_items[j].cid, &error);
            if (swwan_tests[i].expected_items[j].state == MM_BEARER_CONNECTION_STATUS_UNKNOWN) {
                g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
                g_clear_error (&error);
            } else
                g_assert_no_error (error);
            g_assert_cmpint (read_state, ==, swwan_tests[i].expected_items[j].state);
        }

        /* Query for a CID which isn't replied (e.g. 12) */
        if (!swwan_tests[i].skip_test_other_cids) {
            read_state = mm_cinterion_parse_swwan_response (swwan_tests[i].response, 12, &error);
            g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
            g_assert_cmpint (read_state, ==, MM_BEARER_CONNECTION_STATUS_UNKNOWN);
            g_clear_error (&error);
        }
    }

    /* Additional tests for errors */
    read_state = mm_cinterion_parse_swwan_response ("^GARBAGE", 2, &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
    g_assert_cmpint (read_state, ==, MM_BEARER_CONNECTION_STATUS_UNKNOWN);
    g_clear_error (&error);
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
/* Test ^SLCC URCs */

static void
common_test_slcc_urc (const gchar               *urc,
                      const MMCallInfo *expected_call_info_list,
                      guint                      expected_call_info_list_size)
{
    GError     *error = NULL;
    GRegex     *slcc_regex = NULL;
    gboolean    result;
    GMatchInfo *match_info = NULL;
    gchar      *str;
    GList      *call_info_list = NULL;
    GList      *l;


    slcc_regex = mm_cinterion_get_slcc_regex ();

    /* Same matching logic as done in MMSerialPortAt when processing URCs! */
    result = g_regex_match_full (slcc_regex, urc, -1, 0, 0, &match_info, &error);
    g_assert_no_error (error);
    g_assert (result);

    /* read full matched content */
    str = g_match_info_fetch (match_info, 0);
    g_assert (str);

    result = mm_cinterion_parse_slcc_list (str, &call_info_list, &error);
    g_assert_no_error (error);
    g_assert (result);

    g_debug ("found %u calls", g_list_length (call_info_list));

    if (expected_call_info_list) {
        g_assert (call_info_list);
        g_assert_cmpuint (g_list_length (call_info_list), ==, expected_call_info_list_size);
    } else
        g_assert (!call_info_list);

    for (l = call_info_list; l; l = g_list_next (l)) {
        const MMCallInfo *call_info = (const MMCallInfo *)(l->data);
        gboolean                   found = FALSE;
        guint                      i;

        g_debug ("call at index %u: direction %s, state %s, number %s",
                 call_info->index,
                 mm_call_direction_get_string (call_info->direction),
                 mm_call_state_get_string (call_info->state),
                 call_info->number ? call_info->number : "n/a");

        for (i = 0; !found && i < expected_call_info_list_size; i++)
            found = ((call_info->index == expected_call_info_list[i].index) &&
                     (call_info->direction  == expected_call_info_list[i].direction) &&
                     (call_info->state  == expected_call_info_list[i].state) &&
                     (g_strcmp0 (call_info->number, expected_call_info_list[i].number) == 0));

        g_assert (found);
    }

    g_match_info_free (match_info);
    g_regex_unref (slcc_regex);
    g_free (str);

    mm_cinterion_call_info_list_free (call_info_list);
}

static void
test_slcc_urc_empty (void)
{
    const gchar *urc = "\r\n^SLCC: \r\n";

    common_test_slcc_urc (urc, NULL, 0);
}

static void
test_slcc_urc_single (void)
{
    static const MMCallInfo expected_call_info_list[] = {
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE, "123456789" }
    };

    const gchar *urc =
        "\r\n^SLCC: 1,1,0,0,0,0,\"123456789\",161"
        "\r\n^SLCC: \r\n";

    common_test_slcc_urc (urc, expected_call_info_list, G_N_ELEMENTS (expected_call_info_list));
}

static void
test_slcc_urc_multiple (void)
{
    static const MMCallInfo expected_call_info_list[] = {
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  NULL        },
        { 2, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  "123456789" },
        { 3, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  "987654321" },
    };

    const gchar *urc =
        "\r\n^SLCC: 1,1,0,0,1,0" /* number unknown */
        "\r\n^SLCC: 2,1,0,0,1,0,\"123456789\",161"
        "\r\n^SLCC: 3,1,0,0,1,0,\"987654321\",161,\"Alice\""
        "\r\n^SLCC: \r\n";

    common_test_slcc_urc (urc, expected_call_info_list, G_N_ELEMENTS (expected_call_info_list));
}

static void
test_slcc_urc_complex (void)
{
    static const MMCallInfo expected_call_info_list[] = {
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  "123456789" },
        { 2, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_WAITING, "987654321" },
    };

    const gchar *urc =
        "\r\n^CIEV: 1,0" /* some different URC before our match */
        "\r\n^SLCC: 1,1,0,0,0,0,\"123456789\",161"
        "\r\n^SLCC: 2,1,5,0,0,0,\"987654321\",161"
        "\r\n^SLCC: \r\n"
        "\r\n^CIEV: 1,0" /* some different URC after our match */;

    common_test_slcc_urc (urc, expected_call_info_list, G_N_ELEMENTS (expected_call_info_list));
}

/*****************************************************************************/
/* Test +CTZU URCs */

static void
common_test_ctzu_urc (const gchar *urc,
                      const gchar *expected_iso8601,
                      gint         expected_offset,
                      gint         expected_dst_offset)
{
    GError            *error = NULL;
    GRegex            *ctzu_regex = NULL;
    gboolean           result;
    GMatchInfo        *match_info = NULL;
    gchar             *iso8601;
    MMNetworkTimezone *tz = NULL;

    ctzu_regex = mm_cinterion_get_ctzu_regex ();

    /* Same matching logic as done in MMSerialPortAt when processing URCs! */
    result = g_regex_match_full (ctzu_regex, urc, -1, 0, 0, &match_info, &error);
    g_assert_no_error (error);
    g_assert (result);

    result = mm_cinterion_parse_ctzu_urc (match_info, &iso8601, &tz, &error);
    g_assert_no_error (error);
    g_assert (result);

    g_assert (iso8601);
    g_assert_cmpstr (expected_iso8601, ==, iso8601);
    g_free (iso8601);

    g_assert (tz);
    g_assert_cmpint (expected_offset, ==, mm_network_timezone_get_offset (tz));

    if (expected_dst_offset >= 0)
        g_assert_cmpuint ((guint)expected_dst_offset, ==, mm_network_timezone_get_dst_offset (tz));

    g_object_unref (tz);
    g_match_info_free (match_info);
    g_regex_unref (ctzu_regex);
}

static void
test_ctzu_urc_simple (void)
{
    const gchar *urc = "\r\n+CTZU: \"19/07/09,11:15:40\",+08\r\n";
    const gchar *expected_iso8601    = "2019-07-09T11:15:40+02:00";
    gint         expected_offset     = 120;
    gint         expected_dst_offset = -1; /* not given */

    common_test_ctzu_urc (urc, expected_iso8601, expected_offset, expected_dst_offset);
}

static void
test_ctzu_urc_full (void)
{
    const gchar *urc = "\r\n+CTZU: \"19/07/09,11:15:40\",+08,1\r\n";
    const gchar *expected_iso8601    = "2019-07-09T11:15:40+02:00";
    gint         expected_offset     = 120;
    gint         expected_dst_offset = 60;

    common_test_ctzu_urc (urc, expected_iso8601, expected_offset, expected_dst_offset);
}

/*****************************************************************************/

void
_mm_log (const char *loc,
         const char *func,
         guint32 level,
         const char *fmt,
         ...)
{
    va_list args;
    gchar *msg;

    if (!g_test_verbose ())
        return;

    va_start (args, fmt);
    msg = g_strdup_vprintf (fmt, args);
    va_end (args);
    g_print ("%s\n", msg);
    g_free (msg);
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/cinterion/scfg",                    test_scfg);
    g_test_add_func ("/MM/cinterion/scfg/ehs5",               test_scfg_ehs5);
    g_test_add_func ("/MM/cinterion/scfg/response/3g",        test_scfg_response_3g);
    g_test_add_func ("/MM/cinterion/scfg/response/2g",        test_scfg_response_2g);
    g_test_add_func ("/MM/cinterion/scfg/response/2g/ucs2",   test_scfg_response_2g_ucs2);
    g_test_add_func ("/MM/cinterion/cnmi/phs8",               test_cnmi_phs8);
    g_test_add_func ("/MM/cinterion/cnmi/other",              test_cnmi_other);
    g_test_add_func ("/MM/cinterion/swwan/pls8",              test_swwan_pls8);
    g_test_add_func ("/MM/cinterion/sind/response/simstatus", test_sind_response_simstatus);
    g_test_add_func ("/MM/cinterion/smong/response/tc63i",    test_smong_response_tc63i);
    g_test_add_func ("/MM/cinterion/smong/response/other",    test_smong_response_other);
    g_test_add_func ("/MM/cinterion/slcc/urc/empty",          test_slcc_urc_empty);
    g_test_add_func ("/MM/cinterion/slcc/urc/single",         test_slcc_urc_single);
    g_test_add_func ("/MM/cinterion/slcc/urc/multiple",       test_slcc_urc_multiple);
    g_test_add_func ("/MM/cinterion/slcc/urc/complex",        test_slcc_urc_complex);
    g_test_add_func ("/MM/cinterion/ctzu/urc/simple",         test_ctzu_urc_simple);
    g_test_add_func ("/MM/cinterion/ctzu/urc/full",           test_ctzu_urc_full);

    return g_test_run ();
}
