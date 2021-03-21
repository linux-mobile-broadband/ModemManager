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
#include <math.h>

#include "mm-log-test.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-cinterion.h"

#define g_assert_cmpfloat_tolerance(val1, val2, tolerance)  \
    g_assert_cmpfloat (fabs (val1 - val2), <, tolerance)

/*****************************************************************************/
/* Test ^SCFG test responses */

static void
common_test_scfg (const gchar *response,
                  GArray *expected_bands,
                  MMModemCharset charset,
                  MMCinterionModemFamily modem_family)
{
    GArray *bands = NULL;
    gchar *expected_bands_str;
    gchar *bands_str;
    GError *error = NULL;
    gboolean res;
    MMCinterionRadioBandFormat format;

    res = mm_cinterion_parse_scfg_test (response,
                                        modem_family,
                                        charset,
                                        &bands,
                                        &format,
                                        &error);
    g_assert_no_error (error);
    g_assert (res == TRUE);
    g_assert (bands != NULL);

    mm_common_bands_garray_sort (bands);
    mm_common_bands_garray_sort (expected_bands);

    expected_bands_str = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)expected_bands->data,
                                                       expected_bands->len);
    bands_str = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)bands->data,
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

    common_test_scfg (response, expected_bands, MM_MODEM_CHARSET_UNKNOWN, MM_CINTERION_MODEM_FAMILY_DEFAULT);

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

    common_test_scfg (response, expected_bands, MM_MODEM_CHARSET_UNKNOWN, MM_CINTERION_MODEM_FAMILY_DEFAULT);

    g_array_unref (expected_bands);
}

static void
test_scfg_pls62_gsm (void)
{
    GArray *expected_bands;
    MMModemBand single;
   const gchar *response =
        "^SCFG: \"MEopMode/Prov/AutoSelect\",(\"off\",\"on\")\r\n"
        "^SCFG: \"MEopMode/Prov/Cfg\",(\"fallback\",\"attus\")\r\n"
        "^SCFG: \"Serial/Ifc\",(\"Current\",\"ASC0\",\"USB0\",\"USB1\",\"USB2\",\"MUX1\",\"MUX2\",\"MUX3\",\"0\"),(\"0\",\"3\"),(\"1200\",\"2400\",\"4800\",\"9600\",\"19200\",\"38400\",\"57600\",\"115200\",\"230400\",\"460800\",\"500000\",\"750000\",\"921600\"),(\"0)\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",(\"current\",\"powerup\"),(\"asc0\",\"acm1\",\"acm2\",\"acm3\",\"rmnet0\",\"rmnet1\")\r\n"
        "^SCFG: \"Gpio/mode/ASC1\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/DCD0\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/DSR0\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/DTR0\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/FSR\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/PULSE\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/PWM\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/HWAKEUP\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/RING0\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/SPI\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"Gpio/mode/SYNC\",(\"std\",\"gpio\",\"rsv\")\r\n"
        "^SCFG: \"GPRS/AutoAttach\",(\"disabled\",\"enabled\")\r\n"
        "^SCFG: \"Ident/Manufacturer\",(25)\r\n"
        "^SCFG: \"Ident/Product\",(25)\r\n"
        "^SCFG: \"MEopMode/SoR\",(\"off\",\"on\")\r\n"
        "^SCFG: \"MEopMode/CregRoam\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MeOpMode/SRPOM\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/RingOnData\",(\"off\",\"on\")\r\n"
        "^SCFG: \"MEShutdown/Fso\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEShutdown/sVsup/threshold\",(\"-4\",\"-3\",\"-2\",\"-1\",\"0\",\"1\",\"2\",\"3\",\"4\"),(\"0\")\r\n"
        "^SCFG: \"Radio/Band/2G\",(\"0x00000004\"-\"0x00000074\")\r\n"
        "^SCFG: \"Radio/Band/3G\",(\"0x00000001\"-\"0x0004019B\")\r\n"
        "^SCFG: \"Radio/Band/4G\",(\"0x00000001\"-\"0x080E08DF\")\r\n"
        "^SCFG: \"Radio/Mtpl/2G\",(\"0\"-\"3\"),(\"1\"-\"8\"),(\"0x00000004\",\"0x00000010\",\"0x00000020\",\"0x00000040\"),,(\"18\"-\"33\"),(\"18\"-\"27\")\r\n"
        "^SCFG: \"Radio/Mtpl/3G\",(\"0\"-\"3\"),(\"1\"-\"8\"),(\"0x00000001\",\"0x00000002\",\"0x00000008\",\"0x00000010\",\"0x00000080\",\"0x00000100\",\"0x00040000\"),,(\"18\"-\"24\")\r\n"
        "^SCFG: \"Radio/Mtpl/4G\",(\"0\"-\"3\"),(\"1\"-\"8\"),(\"0x00000001\",\"0x00000002\",\"0x00000004\",\"0x00000008\",\"0x00000010\",\"0x00000040\",\"0x00000080\",\"0x00000800\",\"0x00020000\",\"0x00040000\",\"0x00080000\",\"0x08000000\"),,(\"18)\r\n"
        "^SCFG: \"Radio/OutputPowerReduction\",(\"0\",\"1\",\"2\",\"3\",\"4\")\r\n"
        "^SCFG: \"Serial/Interface/Allocation\",(\"0\",\"1\"),(\"0\",\"1\")\r\n"
        "^SCFG: \"Serial/USB/DDD\",(\"0\",\"1\"),(\"0\"),(4),(4),(4),(63),(63),(4)\r\n"
        "^SCFG: \"Tcp/IRT\",(\"1\"-\"60\")\r\n"
        "^SCFG: \"Tcp/MR\",(\"2\"-\"30\")\r\n"
        "^SCFG: \"Tcp/OT\",(\"1\"-\"6000\")\r\n"
        "^SCFG: \"Tcp/WithURCs\",(\"on\",\"off\")\r\n"
        "^SCFG: \"Trace/Syslog/OTAP\",(\"0\",\"1\"),(\"null\",\"asc0\",\"asc1\",\"usb\",\"usb1\",\"usb2\",\"file\",\"system\"),(\"1\"-\"65535\"),(125),(\"buffered\",\"secure\"),(\"off\",\"on\")\r\n"
        "^SCFG: \"Urc/Ringline\",(\"off\",\"local\",\"asc0\",\"wakeup\")\r\n"
        "^SCFG: \"Urc/Ringline/ActiveTime\",(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"Userware/Autostart\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Userware/Autostart/Delay\",(\"0\"-\"10000\")\r\n"
        "^SCFG: \"Userware/DebugInterface\",(\"0\"-\"255\")|(\"FE80::\"-\"FE80::FFFFFFFFFFFFFFFF\"),(\"0\"-\"255\")|(\"FE80::\"-\"FE80::FFFFFFFFFFFFFFFF\"),(\"0\",\"1\")\r\n"
        "^SCFG: \"Userware/DebugMode\",(\"off\",\"on\")\r\n"
        "^SCFG: \"Userware/Passwd\",(\"0\"-\"8\")\r\n"
        "^SCFG: \"Userware/Stdout\",(\"null\",\"asc0\",\"asc1\",\"usb\",\"usb1\",\"usb2\",\"file\",\"system\"),(\"1\"-\"65535\"),(\"0\"-\"125\"),(\"buffered\",\"secure\"),(\"off\",\"on\")\r\n"
        "^SCFG: \"Userware/Watchdog\",(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",(\"current\",\"powerup\"),(\"asc0\",\"acm1\",\"acm2\",\"acm3\")\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 23);
    single = MM_MODEM_BAND_EGSM,        g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,         g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_PCS,         g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_G850,        g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_1,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_2,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_4,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_5,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_8,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_9,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_19,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_1,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_2,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_3,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_4,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_5,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_7,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_8,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_12,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_18,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_19,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_20,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_28,   g_array_append_val (expected_bands, single);

    common_test_scfg (response, expected_bands, MM_MODEM_CHARSET_GSM, MM_CINTERION_MODEM_FAMILY_IMT);

    g_array_unref (expected_bands);
}

static void
test_scfg_pls62_ucs2 (void)
{
    GArray *expected_bands;
    MMModemBand single;
    const gchar *response =
        "^SCFG: \"MEopMode/Prov/AutoSelect\",(\"006F00660066\",\"006F006E\")\r\n"
        "^SCFG: \"MEopMode/Prov/Cfg\",(\"fallback\",\"attus\")\r\n"
        "^SCFG: \"Serial/Ifc\",(\"00430075007200720065006E0074\",\"0041005300430030\",\"0055005300420030\",\"0055005300420031\",\"0055005300420032\",\"004D005500580031\",\"004D005500580032\",\"004D005500580033\",\"0030\"),(\"0030\",\"0033)\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",(\"00630075007200720065006E0074\",\"0070006F00770065007200750070\"),(\"0061007300630030\",\"00610063006D0031\",\"00610063006D0032\",\"00610063006D0033\",\"0072006D006E006500740030\",\"0072006D0)\r\n"
        "^SCFG: \"Gpio/mode/ASC1\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/DCD0\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/DSR0\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/DTR0\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/FSR\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/PULSE\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/PWM\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/HWAKEUP\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/RING0\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/SPI\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"Gpio/mode/SYNC\",(\"007300740064\",\"006700700069006F\",\"007200730076\")\r\n"
        "^SCFG: \"GPRS/AutoAttach\",(\"00640069007300610062006C00650064\",\"0065006E00610062006C00650064\")\r\n"
        "^SCFG: \"Ident/Manufacturer\",(25)\r\n"
        "^SCFG: \"Ident/Product\",(25)\r\n"
        "^SCFG: \"MEopMode/SoR\",(\"006F00660066\",\"006F006E\")\r\n"
        "^SCFG: \"MEopMode/CregRoam\",(\"0030\",\"0031\")\r\n"
        "^SCFG: \"MeOpMode/SRPOM\",(\"0030\",\"0031\")\r\n"
        "^SCFG: \"MEopMode/RingOnData\",(\"006F00660066\",\"006F006E\")\r\n"
        "^SCFG: \"MEShutdown/Fso\",(\"0030\",\"0031\")\r\n"
        "^SCFG: \"MEShutdown/sVsup/threshold\",(\"002D0034\",\"002D0033\",\"002D0032\",\"002D0031\",\"0030\",\"0031\",\"0032\",\"0033\",\"0034\"),(\"0030\")\r\n"
        "^SCFG: \"Radio/Band/2G\",(\"0030007800300030003000300030003000300034\"-\"0030007800300030003000300030003000370034\")\r\n"
        "^SCFG: \"Radio/Band/3G\",(\"0030007800300030003000300030003000300031\"-\"0030007800300030003000340030003100390042\")\r\n"
        "^SCFG: \"Radio/Band/4G\",(\"0030007800300030003000300030003000300031\"-\"0030007800300038003000450030003800440046\")\r\n"
        "^SCFG: \"Radio/Mtpl/2G\",(\"00300022002D00220033\"),(\"00310022002D00220038\"),(\"00300078003000300030003000300030003000340022002C002200300078003000300030003000300030003100300022002C0022003000780030003000300030003)\r\n"
        "^SCFG: \"Radio/Mtpl/3G\",(\"00300022002D00220033\"),(\"00310022002D00220038\"),(\"00300078003000300030003000300030003000310022002C002200300078003000300030003000300030003000320022002C0022003000780030003000300030003)\r\n"
        "^SCFG: \"Radio/Mtpl/4G\",(\"00300022002D00220033\"),(\"00310022002D00220038\"),(\"00310022002D00220038\"),,(\"003100380022002D002200320033\")\r\n"
        "^SCFG: \"Radio/OutputPowerReduction\",(\"0030\",\"0031\",\"0032\",\"0033\",\"0034\")\r\n"
        "^SCFG: \"Serial/Interface/Allocation\",(\"0030\",\"0031\"),(\"0030\",\"0031\")\r\n"
        "^SCFG: \"Serial/USB/DDD\",(\"0030\",\"0031\"),(\"0030\"),(4),(4),(4),(63),(63),(4)\r\n"
        "^SCFG: \"Tcp/IRT\",(\"0031\"-\"00360030\")\r\n"
        "^SCFG: \"Tcp/MR\",(\"0032\"-\"00330030\")\r\n"
        "^SCFG: \"Tcp/OT\",(\"0031\"-\"0036003000300030\")\r\n"
        "^SCFG: \"Tcp/WithURCs\",(\"006F006E\",\"006F00660066\")\r\n"
        "^SCFG: \"Trace/Syslog/OTAP\",(\"0030\",\"0031\"),(\"006E0075006C006C\",\"0061007300630030\",\"0061007300630031\",\"007500730062\",\"0075007300620031\",\"0075007300620032\",\"00660069006C0065\",\"00730079007300740065006D\"),(\"003)\r\n"
        "^SCFG: \"Urc/Ringline\",(\"006F00660066\",\"006C006F00630061006C\",\"0061007300630030\",\"00770061006B006500750070\")\r\n"
        "^SCFG: \"Urc/Ringline/ActiveTime\",(\"0030\",\"0031\",\"0032\")\r\n"
        "^SCFG: \"Userware/Autostart\",(\"0030\",\"0031\")\r\n"
        "^SCFG: \"Userware/Autostart/Delay\",(\"00300022002D002200310030003000300030\")\r\n"
        "^SCFG: \"Userware/DebugInterface\",(\"0030\"-\"003200350035\")|(\"0046004500380030003A003A\"-\"0046004500380030003A003A0046004600460046004600460046004600460046004600460046004600460046\"),(\"0030\"-\"003200350035\")|(\"004)\r\n"
        "^SCFG: \"Userware/DebugMode\",(\"006F00660066\",\"006F006E\")\r\n"
        "^SCFG: \"Userware/Passwd\",(\"0030\"-\"0038\")\r\n"
        "^SCFG: \"Userware/Stdout\",(\"006E0075006C006C\",\"0061007300630030\",\"0061007300630031\",\"007500730062\",\"0075007300620031\",\"0075007300620032\",\"00660069006C0065\",\"00730079007300740065006D\"),(\"0031\"-\"00360035003500)\r\n"
        "^SCFG: \"Userware/Watchdog\",(\"0030\",\"0031\",\"0032\")\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",(\"00630075007200720065006E0074\",\"0070006F00770065007200750070\"),(\"0061007300630030\",\"00610063006D0031\",\"00610063006D0032\",\"00610063006D0033\")\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 23);
    single = MM_MODEM_BAND_EGSM,        g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,         g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_PCS,         g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_G850,        g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_1,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_2,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_4,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_5,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_8,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_9,     g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_19,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_1,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_2,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_3,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_4,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_5,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_7,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_8,    g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_12,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_18,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_19,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_20,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_28,   g_array_append_val (expected_bands, single);

    common_test_scfg (response, expected_bands, MM_MODEM_CHARSET_UCS2, MM_CINTERION_MODEM_FAMILY_IMT);

    g_array_unref (expected_bands);
}

static void
test_scfg_alas5 (void)
{
    GArray *expected_bands;
    MMModemBand single;
   const gchar *response =
        "^SCFG: \"Audio/Loop\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Audio/SvTone\",(\"0-2047\")\r\n"
        "^SCFG: \"Call/Ecall/AckTimeout\",(\"0-60000\")\r\n"
        "^SCFG: \"Call/Ecall/BlockSMSPP\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Call/Ecall/Callback\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Call/Ecall/CallbackTimeout\",(\"0-86400000\")\r\n"
        "^SCFG: \"Call/Ecall/Force\",(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"Call/Ecall/Msd\",(280)\r\n"
        "^SCFG: \"Call/Ecall/Pullmode\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Call/Ecall/SessionTimeout\",(\"0-300000\")\r\n"
        "^SCFG: \"Call/Ecall/StartTimeout\",(\"0-600000\")\r\n"
        "^SCFG: \"Call/ECC\",(\"0\"-\"255\")\r\n"
        "^SCFG: \"Call/Speech/Codec\",(\"0\",\"2\")\r\n"
        "^SCFG: \"GPRS/Auth\",(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"GPRS/AutoAttach\",(\"disabled\",\"enabled\")\r\n"
        "^SCFG: \"GPRS/MTU/Mode\",(\"0-1\")\r\n"
        "^SCFG: \"GPRS/MTU/Size\",(\"1280-4096\")\r\n"
        "^SCFG: \"MEopMode/CFUN\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/CregRoam\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/Dormancy\",(\"0\",\"1\",\"9\")\r\n"
        "^SCFG: \"MEopMode/DTM/Mode\",(\"0\",\"1\",\"2\")\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",(\"current\",\"powerup\"),(\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"diag\",\"mbim\",\"asc0\")\r\n"
        "^SCFG: \"MEopMode/FGI/Split\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/IMS\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/NonBlock/Cops\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/PowerMgmt/LCI\",(\"disabled\",\"enabled\"),(\"GPIO1\",\"GPIO3\",\"GPIO4\",\"GPIO5\",\"GPIO6\",\"GPIO7\",\"GPIO8\",\"GPIO11\",\"GPIO12\",\"GPIO13\",\"GPIO14\",\"GPIO15\",\"GPIO16\",\"GPIO17\",\"GPIO22\")\r\n"
        "^SCFG: \"MEopMode/Prov/AutoFallback\",(\"on\",\"off\")\r\n"
        "^SCFG: \"MEopMode/Prov/AutoSelect\",(\"on\",\"off\")\r\n"
        "^SCFG: \"MEopMode/Prov/Cfg\",(\"vdfde\",\"tmode\",\"clarobr\",\"telenorno\",\"telenorse\",\"vdfpt\",\"fallb3gpp*\",\"vdfww\",\"vdfes\",\"swisscomch\",\"eeuk\",\"orangero\",\"orangees\",\"tefde\",\"telenordk\",\"timit\",\"tn1de\",\"tefes\",\"tels)\r\n"
        "^SCFG: \"MEopMode/PwrSave\",(\"disabled\",\"enabled\"),(\"0-36000\"),(\"0-36000\"),(\"CPU-A\",\"CPU-M\"),(\"powerup\",\"current\")\r\n"
        "^SCFG: \"MEopMode/SRPOM\",(\"0\",\"1\")\r\n"
        "^SCFG: \"MEopMode/USB/KeepData\",(\"current\",\"powerup\"),(\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"diag\",\"mbim\",\"asc0\")\r\n"
        "^SCFG: \"MEShutdown/OnIgnition\",(\"on\",\"off\")\r\n"
        "^SCFG: \"MEShutdown/Timer\",(\"off\",\"0\"-\"525600\")\r\n"
        "^SCFG: \"Misc/CId\",(290)\r\n"
        "^SCFG: \"Radio/Band/2G\",(\"00000001-0000000f\"),,(\"0\",\"1\")\r\n"
        "^SCFG: \"Radio/Band/3G\",(\"00000001-000400b5\"),,(\"0\",\"1\")\r\n"
        "^SCFG: \"Radio/Band/4G\",(\"00000001-8a0e00d5\"),(\"00000002-000001e2\"),(\"0\",\"1\")\r\n"
        "^SCFG: \"Radio/CNS\",(\"0\",\"1\")\r\n"
        "^SCFG: \"Radio/Mtpl\",(\"0-1\"),(\"1-8\")\r\n"
        "^SCFG: \"Radio/Mtpl/2G\",(\"2-3\"),(\"1-8\"),(\"00000001-0000000f\"),,(\"18-33\"),(\"18-27\")\r\n"
        "^SCFG: \"Radio/Mtpl/3G\",(\"2-3\"),(\"1-8\"),(\"00000001-000000b5\"),,(\"18-24\")\r\n"
        "^SCFG: \"Radio/Mtpl/4G\",(\"2-3\"),(\"1-8\"),(\"00000001-8a0e00d5\"),(\"00000002-000000e2\"),(\"18-24\")\r\n"
        "^SCFG: \"Radio/OutputPowerReduction\",(\"4\"-\"8\")\r\n"
        "^SCFG: \"RemoteWakeUp/Event/ASC\",(\"none\",\"GPIO1\",\"GPIO3\",\"GPIO4\",\"GPIO5\",\"GPIO6\",\"GPIO7\",\"GPIO8\",\"GPIO11\",\"GPIO12\",\"GPIO13\",\"GPIO14\",\"GPIO15\",\"GPIO16\",\"GPIO17\",\"GPIO22\")\r\n"
        "^SCFG: \"RemoteWakeUp/Event/URC\",(\"none\",\"GPIO1\",\"GPIO3\",\"GPIO4\",\"GPIO5\",\"GPIO6\",\"GPIO7\",\"GPIO8\",\"GPIO11\",\"GPIO12\",\"GPIO13\",\"GPIO14\",\"GPIO15\",\"GPIO16\",\"GPIO17\",\"GPIO22\")\r\n"
        "^SCFG: \"RemoteWakeUp/Event/USB\",(\"none\",\"GPIO1\",\"GPIO3\",\"GPIO4\",\"GPIO5\",\"GPIO6\",\"GPIO7\",\"GPIO8\",\"GPIO11\",\"GPIO12\",\"GPIO13\",\"GPIO14\",\"GPIO15\",\"GPIO16\",\"GPIO17\",\"GPIO22\")\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",(\"current\",\"powerup\"),(\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"diag\",\"mbim\",\"asc0\")\r\n"
        "^SCFG: \"RemoteWakeUp/Pulse\",(\"1\"-\"100\")\r\n"
        "^SCFG: \"Serial/USB/DDD\",(\"0-1\"),(\"0\"),(\"0001-ffff\"),(\"0000-ffff\"),(\"0000-ffff\"),(63),(63),(4)\r\n"
        "^SCFG: \"SIM/CS\",(\"NOSIM\",\"SIM1\",\"SIM2\")\r\n"
        "^SCFG: \"SMS/4GPREF\",(\"IMS\",\"CSPS\")\r\n"
        "^SCFG: \"SMS/AutoAck\",(\"0\",\"1\")\r\n"
        "^SCFG: \"SMS/RETRM\",(\"1-45\")\r\n"
        "^SCFG: \"URC/Ringline\",(\"off\",\"local\",\"asc0\")\r\n"
        "^SCFG: \"URC/Ringline/ActiveTime\",(\"2\",\"on\",\"off\")\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 23);
    single = MM_MODEM_BAND_EGSM,      g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,       g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_PCS,       g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_G850,      g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_1,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_3,   g_array_append_val (expected_bands, single); //
    single = MM_MODEM_BAND_UTRAN_5,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_6,   g_array_append_val (expected_bands, single); //
    single = MM_MODEM_BAND_UTRAN_8,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_19,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_1,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_3,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_5,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_7,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_8,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_18, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_19, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_20, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_26, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_28, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_38, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_39, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_40, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_41, g_array_append_val (expected_bands, single);

    common_test_scfg (response, expected_bands, MM_MODEM_CHARSET_GSM, MM_CINTERION_MODEM_FAMILY_DEFAULT);

    g_array_unref (expected_bands);
}

/*****************************************************************************/
/* Test ^SCFG responses */

static void
common_test_scfg_response (const gchar *response,
                           MMModemCharset charset,
                           GArray *expected_bands,
                           MMCinterionModemFamily modem_family,
                           MMCinterionRadioBandFormat rbf)
{
    GArray *bands = NULL;
    gchar *expected_bands_str;
    gchar *bands_str;
    GError *error = NULL;
    gboolean res;

    res = mm_cinterion_parse_scfg_response (response, modem_family, charset, &bands, rbf, &error);
    g_assert_no_error (error);
    g_assert (res == TRUE);
    g_assert (bands != NULL);

    mm_common_bands_garray_sort (bands);
    mm_common_bands_garray_sort (expected_bands);

    expected_bands_str = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)expected_bands->data,
                                                       expected_bands->len);
    bands_str = mm_common_build_bands_string ((const MMModemBand *)(gconstpointer)bands->data,
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

    common_test_scfg_response (response, MM_MODEM_CHARSET_UNKNOWN, expected_bands, MM_CINTERION_MODEM_FAMILY_DEFAULT, MM_CINTERION_RADIO_BAND_FORMAT_SINGLE);

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

    common_test_scfg_response (response, MM_MODEM_CHARSET_UNKNOWN, expected_bands, MM_CINTERION_MODEM_FAMILY_DEFAULT, MM_CINTERION_RADIO_BAND_FORMAT_SINGLE);

    g_array_unref (expected_bands);
}

static void
test_scfg_response_pls62_gsm (void)
{
    GArray *expected_bands;
    MMModemBand single;
     const gchar *response =
        "^SCFG: \"MEopMode/Prov/AutoSelect\",\"off\"\r\n"
        "^SCFG: \"MEopMode/Prov/Cfg\",\"attus\"\r\n"
        "^SCFG: \"Serial/Ifc\",\"0\"\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",\"current\"\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",\"powerup\"\r\n"
        "^SCFG: \"Gpio/mode/ASC1\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/DCD0\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/DSR0\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/DTR0\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/FSR\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/PULSE\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/PWM\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/HWAKEUP\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/RING0\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/SPI\",\"gpio\"\r\n"
        "^SCFG: \"Gpio/mode/SYNC\",\"gpio\"\r\n"
        "^SCFG: \"GPRS/AutoAttach\",\"enabled\"\r\n"
        "^SCFG: \"Ident/Manufacturer\",\"Cinterion\"\r\n"
        "^SCFG: \"Ident/Product\",\"PLS62-W\"\r\n"
        "^SCFG: \"MEopMode/SoR\",\"off\"\r\n"
        "^SCFG: \"MEopMode/CregRoam\",\"0\"\r\n"
        "^SCFG: \"MeOpMode/SRPOM\",\"0\"\r\n"
        "^SCFG: \"MEopMode/RingOnData\",\"off\"\r\n"
        "^SCFG: \"MEShutdown/Fso\",\"0\"\r\n"
        "^SCFG: \"MEShutdown/sVsup/threshold\",\"0\",\"0\"\r\n"
        "^SCFG: \"Radio/Band/2G\",\"0x00000014\"\r\n"
        "^SCFG: \"Radio/Band/3G\",\"0x00000182\"\r\n"
        "^SCFG: \"Radio/Band/4G\",\"0x080E0000\"\r\n"
        "^SCFG: \"Radio/Mtpl/2G\",\"0\"\r\n"
        "^SCFG: \"Radio/Mtpl/3G\",\"0\"\r\n"
        "^SCFG: \"Radio/Mtpl/4G\",\"0\"\r\n"
        "^SCFG: \"Radio/OutputPowerReduction\",\"4\"\r\n"
        "^SCFG: \"Serial/Interface/Allocation\",\"0\",\"0\"\r\n"
        "^SCFG: \"Serial/USB/DDD\",\"0\",\"0\",\"0409\",\"1E2D\",\"005B\",\"Cinterion Wireless Modules\",\"PLSx\",\"\"\r\n"
        "^SCFG: \"Tcp/IRT\",\"3\"\r\n"
        "^SCFG: \"Tcp/MR\",\"10\"\r\n"
        "^SCFG: \"Tcp/OT\",\"6000\"\r\n"
        "^SCFG: \"Tcp/WithURCs\",\"on\"\r\n"
        "^SCFG: \"Trace/Syslog/OTAP\",\"0\"\r\n"
        "^SCFG: \"Urc/Ringline\",\"local\"\r\n"
        "^SCFG: \"Urc/Ringline/ActiveTime\",\"2\"\r\n"
        "^SCFG: \"Userware/Autostart\",\"0\"\r\n"
        "^SCFG: \"Userware/Autostart/Delay\",\"0\"\r\n"
        "^SCFG: \"Userware/DebugInterface\",\"0.0.0.0\",\"0.0.0.0\",\"0\"\r\n"
        "^SCFG: \"Userware/DebugMode\",\"off\"\r\n"
        "^SCFG: \"Userware/Passwd\",\r\n"
        "^SCFG: \"Userware/Stdout\",\"null\",,,,\"off\"\r\n"
        "^SCFG: \"Userware/Watchdog\",\"0\"\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",\"current\"\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",\"powerup\"\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 9);
    single = MM_MODEM_BAND_EGSM,      g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,       g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_2,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_8,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_9,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_18,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_19,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_20, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_28,  g_array_append_val (expected_bands, single);

    common_test_scfg_response (response, MM_MODEM_CHARSET_GSM, expected_bands, MM_CINTERION_MODEM_FAMILY_IMT, MM_CINTERION_RADIO_BAND_FORMAT_MULTIPLE);

    g_array_unref (expected_bands);
}

static void
test_scfg_response_pls62_ucs2 (void)
{
    GArray *expected_bands;
    MMModemBand single;
     const gchar *response =
        "^SCFG: \"MEopMode/Prov/AutoSelect\",\"006F00660066\"\r\n"
        "^SCFG: \"MEopMode/Prov/Cfg\",\"00610074007400750073\"\r\n"
        "^SCFG: \"Serial/Ifc\",\"0\"\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",\"00630075007200720065006E0074\"\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",\"0070006F00770065007200750070\"\r\n"
        "^SCFG: \"Gpio/mode/ASC1\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/DCD0\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/DSR0\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/DTR0\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/FSR\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/PULSE\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/PWM\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/HWAKEUP\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/RING0\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/SPI\",\"006700700069006F\"\r\n"
        "^SCFG: \"Gpio/mode/SYNC\",\"006700700069006F\"\r\n"
        "^SCFG: \"GPRS/AutoAttach\",\"0065006E00610062006C00650064\"\r\n"
        "^SCFG: \"Ident/Manufacturer\",\"Cinterion\"\r\n"
        "^SCFG: \"Ident/Product\",\"PLS62-W\"\r\n"
        "^SCFG: \"MEopMode/SoR\",\"006F00660066\"\r\n"
        "^SCFG: \"MEopMode/CregRoam\",\"0030\"\r\n"
        "^SCFG: \"MeOpMode/SRPOM\",\"0030\"\r\n"
        "^SCFG: \"MEopMode/RingOnData\",\"006F00660066\"\r\n"
        "^SCFG: \"MEShutdown/Fso\",\"0030\"\r\n"
        "^SCFG: \"MEShutdown/sVsup/threshold\",\"0030\",\"0030\"\r\n"
        "^SCFG: \"Radio/Band/2G\",\"0030007800300030003000300030003000310034\"\r\n"
        "^SCFG: \"Radio/Band/3G\",\"0030007800300030003000300030003100380032\"\r\n"
        "^SCFG: \"Radio/Band/4G\",\"0030007800300038003000450030003000300030\"\r\n"
        "^SCFG: \"Radio/Mtpl/2G\",\"0030\"\r\n"
        "^SCFG: \"Radio/Mtpl/3G\",\"0030\"\r\n"
        "^SCFG: \"Radio/Mtpl/4G\",\"0030\"\r\n"
        "^SCFG: \"Radio/OutputPowerReduction\",\"0034\"\r\n"
        "^SCFG: \"Serial/Interface/Allocation\",\"0030\",\"0030\"\r\n"
        "^SCFG: \"Serial/USB/DDD\",\"0030\",\"0030\",\"0030003400300039\",\"0031004500320044\",\"0030003000350042\",\"00430069006E0074006500720069006F006E00200057006900720065006C0065007300730020004D006F00640075006C00650073\",\"005\"\r\n"
        "^SCFG: \"Tcp/IRT\",\"0033\"\r\n"
        "^SCFG: \"Tcp/MR\",\"00310030\"\r\n"
        "^SCFG: \"Tcp/OT\",\"0036003000300030\"\r\n"
        "^SCFG: \"Tcp/WithURCs\",\"006F006E\"\r\n"
        "^SCFG: \"Trace/Syslog/OTAP\",\"0030\"\r\n"
        "^SCFG: \"Urc/Ringline\",\"006C006F00630061006C\"\r\n"
        "^SCFG: \"Urc/Ringline/ActiveTime\",\"0032\"\r\n"
        "^SCFG: \"Userware/Autostart\",\"0030\"\r\n"
        "^SCFG: \"Userware/Autostart/Delay\",\"0030\"\r\n"
        "^SCFG: \"Userware/DebugInterface\",\"0030002E0030002E0030002E0030\",\"0030002E0030002E0030002E0030\",\"0030\"\r\n"
        "^SCFG: \"Userware/DebugMode\",\"006F00660066\"\r\n"
        "^SCFG: \"Userware/Passwd\",\r\n"
        "^SCFG: \"Userware/Stdout\",\"006E0075006C006C\",,,,\"006F00660066\"\r\n"
        "^SCFG: \"Userware/Watchdog\",\"0030\"\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",\"00630075007200720065006E0074\"\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",\"0070006F00770065007200750070\"\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 9);
    single = MM_MODEM_BAND_EGSM,      g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,       g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_2,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_8,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_9,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_18,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_19,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_20, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_28,  g_array_append_val (expected_bands, single);

    common_test_scfg_response (response, MM_MODEM_CHARSET_UCS2, expected_bands, MM_CINTERION_MODEM_FAMILY_IMT, MM_CINTERION_RADIO_BAND_FORMAT_MULTIPLE);

    g_array_unref (expected_bands);
}

static void
test_scfg_response_alas5 (void)
{
    GArray *expected_bands;
    MMModemBand single;
     const gchar *response =
        "^SCFG: \"Audio/Loop\",\"0\"\r\n"
        "^SCFG: \"Audio/SvTone\",\"0\"\r\n"
        "^SCFG: \"Call/Ecall/AckTimeout\",\"5000\"\r\n"
        "^SCFG: \"Call/Ecall/BlockSMSPP\",\"0\"\r\n"
        "^SCFG: \"Call/Ecall/Callback\",\"0\"\r\n"
        "^SCFG: \"Call/Ecall/CallbackTimeout\",\"43200000\"\r\n"
        "^SCFG: \"Call/Ecall/Force\",\"1\"\r\n"
        "^SCFG: \"Call/Ecall/Msd\",\"\"\r\n"
        "^SCFG: \"Call/Ecall/Pullmode\",\"0\"\r\n"
        "^SCFG: \"Call/Ecall/SessionTimeout\",\"20000\"\r\n"
        "^SCFG: \"Call/Ecall/StartTimeout\",\"5000\"\r\n"
        "^SCFG: \"Call/ECC\",\"0\"\r\n"
        "^SCFG: \"Call/Speech/Codec\",\"0\"\r\n"
        "^SCFG: \"GPRS/Auth\",\"2\"\r\n"
        "^SCFG: \"GPRS/AutoAttach\",\"enabled\"\r\n"
        "^SCFG: \"GPRS/MTU/Mode\",\"0\"\r\n"
        "^SCFG: \"GPRS/MTU/Size\",1500\r\n"
        "^SCFG: \"MEopMode/CFUN\",\"1\",\"1\"\r\n"
        "^SCFG: \"MEopMode/CregRoam\",\"0\"\r\n"
        "^SCFG: \"MEopMode/Dormancy\",\"0\",\"0\"\r\n"
        "^SCFG: \"MEopMode/DTM/Mode\",\"2\"\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",\"current\",\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"mbim\",\"asc0\"\r\n"
        "^SCFG: \"MEopMode/ExpectDTR\",\"powerup\",\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"mbim\",\"asc0\"\r\n"
        "^SCFG: \"MEopMode/FGI/Split\",\"1\"\r\n"
        "^SCFG: \"MEopMode/IMS\",\"1\"\r\n"
        "^SCFG: \"MEopMode/NonBlock/Cops\",\"0\"\r\n"
        "^SCFG: \"MEopMode/PowerMgmt/LCI\",\"disabled\"\r\n"
        "^SCFG: \"MEopMode/Prov/AutoFallback\",\"off\"\r\n"
        "^SCFG: \"MEopMode/Prov/AutoSelect\",\"on\"\r\n"
        "^SCFG: \"MEopMode/Prov/Cfg\",\"vdfde\"\r\n"
        "^SCFG: \"MEopMode/PwrSave\",\"enabled\",\"52\",\"50\",\"CPU-A\",\"powerup\"\r\n"
        "^SCFG: \"MEopMode/PwrSave\",\"enabled\",\"52\",\"50\",\"CPU-A\",\"current\"\r\n"
        "^SCFG: \"MEopMode/PwrSave\",\"enabled\",\"0\",\"0\",\"CPU-M\",\"powerup\"\r\n"
        "^SCFG: \"MEopMode/PwrSave\",\"enabled\",\"0\",\"0\",\"CPU-M\",\"current\"\r\n"
        "^SCFG: \"MEopMode/SRPOM\",\"0\"\r\n"
        "^SCFG: \"MEopMode/USB/KeepData\",\"current\",\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"diag\",\"mbim\",\"asc0\"\r\n"
        "^SCFG: \"MEopMode/USB/KeepData\",\"powerup\",\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"diag\",\"mbim\",\"asc0\"\r\n"
        "^SCFG: \"MEShutdown/OnIgnition\",\"off\"\r\n"
        "^SCFG: \"MEShutdown/Timer\",\"off\"\r\n"
        "^SCFG: \"Misc/CId\",\"\"\r\n"
        "^SCFG: \"Radio/Band/2G\",\"0000000f\"\r\n"
        "^SCFG: \"Radio/Band/3G\",\"000400b5\"\r\n"
        "^SCFG: \"Radio/Band/4G\",\"8a0e00d5\",\"000000e2\"\r\n"
        "^SCFG: \"Radio/CNS\",\"0\"\r\n"
        "^SCFG: \"Radio/Mtpl\",\"0\"\r\n"
        "^SCFG: \"Radio/Mtpl/2G\",\"0\"\r\n"
        "^SCFG: \"Radio/Mtpl/3G\",\"0\"\r\n"
        "^SCFG: \"Radio/Mtpl/4G\",\"0\"\r\n"
        "^SCFG: \"Radio/OutputPowerReduction\",\"4\"\r\n"
        "^SCFG: \"RemoteWakeUp/Event/ASC\",\"none\"\r\n"
        "^SCFG: \"RemoteWakeUp/Event/URC\",\"none\"\r\n"
        "^SCFG: \"RemoteWakeUp/Event/USB\",\"GPIO4\"\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",\"current\",\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"diag\",\"mbim\",\"asc0\"\r\n"
        "^SCFG: \"RemoteWakeUp/Ports\",\"powerup\",\"acm0\",\"acm1\",\"acm2\",\"acm3\",\"diag\",\"mbim\",\"asc0\"\r\n"
        "^SCFG: \"RemoteWakeUp/Pulse\",\"10\"\r\n"
        "^SCFG: \"Serial/USB/DDD\",\"0\",\"0\",\"0409\",\"1e2d\",\"0065\",\"Cinterion\",\"LTE Modem\",\"8d8f\"\r\n"
        "^SCFG: \"SIM/CS\",\"SIM1\"\r\n"
        "^SCFG: \"SMS/4GPREF\",\"IMS\"\r\n"
        "^SCFG: \"SMS/AutoAck\",\"0\"\r\n"
        "^SCFG: \"SMS/RETRM\",\"30\"\r\n"
        "^SCFG: \"URC/Ringline\",\"local\"\r\n"
        "^SCFG: \"URC/Ringline/ActiveTime\",\"2\"\r\n";

    expected_bands = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), 25);
    single = MM_MODEM_BAND_EGSM,      g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_DCS,       g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_PCS,       g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_G850,      g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_1,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_3,   g_array_append_val (expected_bands, single); //
    single = MM_MODEM_BAND_UTRAN_5,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_6,   g_array_append_val (expected_bands, single); //
    single = MM_MODEM_BAND_UTRAN_8,   g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_UTRAN_19,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_1,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_3,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_5,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_7,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_8,  g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_18, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_19, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_20, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_26, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_28, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_38, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_39, g_array_append_val (expected_bands, single);
    single = MM_MODEM_BAND_EUTRAN_40, g_array_append_val (expected_bands, single);

    common_test_scfg_response (response, MM_MODEM_CHARSET_GSM, expected_bands, MM_CINTERION_MODEM_FAMILY_DEFAULT, MM_CINTERION_RADIO_BAND_FORMAT_MULTIPLE);

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
            read_state = mm_cinterion_parse_swwan_response (swwan_tests[i].response, swwan_tests[i].expected_items[j].cid, NULL, &error);
            if (swwan_tests[i].expected_items[j].state == MM_BEARER_CONNECTION_STATUS_UNKNOWN) {
                g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
                g_clear_error (&error);
            } else
                g_assert_no_error (error);
            g_assert_cmpint (read_state, ==, swwan_tests[i].expected_items[j].state);
        }

        /* Query for a CID which isn't replied (e.g. 12) */
        if (!swwan_tests[i].skip_test_other_cids) {
            read_state = mm_cinterion_parse_swwan_response (swwan_tests[i].response, 12, NULL, &error);
            g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
            g_assert_cmpint (read_state, ==, MM_BEARER_CONNECTION_STATUS_UNKNOWN);
            g_clear_error (&error);
        }
    }

    /* Additional tests for errors */
    read_state = mm_cinterion_parse_swwan_response ("^GARBAGE", 2, NULL, &error);
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
                            gboolean                 success,
                            MMModemAccessTechnology  expected_access_tech)
{
    GError                  *error = NULL;
    gboolean                 res;
    MMModemAccessTechnology  access_tech;

    res = mm_cinterion_parse_smong_response (response, &access_tech, &error);

    if (success) {
        g_assert_no_error (error);
        g_assert (res);
        g_assert_cmpuint (access_tech, ==, expected_access_tech);
    } else {
        g_assert (error);
        g_assert (!res);
    }
}

static void
test_smong_response_tc63i (void)
{
    const gchar *response =
        "\r\n"
        "GPRS Monitor\r\n"
        "BCCH  G  PBCCH  PAT MCC  MNC  NOM  TA      RAC                               # Cell #\r\n"
        "0073  1  -      -   262   02  2    00 01\r\n";
    common_test_smong_response (response, TRUE, MM_MODEM_ACCESS_TECHNOLOGY_GPRS);
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
    common_test_smong_response (response, TRUE, MM_MODEM_ACCESS_TECHNOLOGY_GPRS);
}

static void
test_smong_response_no_match (void)
{
    const gchar *response =
        "\r\n"
        "GPRS Monitor\r\n"
        "\r\n"
        "BCCH  K  PBCCH  PAT MCC  MNC  NOM  TA      RAC                              # Cell #\r\n"
        "  44  1  -      -   234   10  -    -       -                                             \r\n";
    common_test_smong_response (response, FALSE, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
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

    result = mm_cinterion_parse_slcc_list (str, NULL, &call_info_list, &error);
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
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE, (gchar *) "123456789" }
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
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE, NULL        },
        { 2, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE, (gchar *) "123456789" },
        { 3, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE, (gchar *) "987654321" },
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
        { 1, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_ACTIVE,  (gchar *) "123456789" },
        { 2, MM_CALL_DIRECTION_INCOMING, MM_CALL_STATE_WAITING, (gchar *) "987654321" },
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
/* Test ^SMONI responses */

typedef struct {
    const gchar            *str;
    MMCinterionRadioGen     tech;
    gdouble                 rssi;
    gdouble                 ecn0;
    gdouble                 rscp;
    gdouble                 rsrp;
    gdouble                 rsrq;
} SMoniResponseTest;

static const SMoniResponseTest smoni_response_tests[] = {
    {
        .str       = "^SMONI: 2G,71,-61,262,02,0143,83BA,33,33,3,6,G,NOCONN",
        .tech      = MM_CINTERION_RADIO_GEN_2G,
        .rssi      = -61.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 2G,SEARCH,SEARCH",
        .tech      = MM_CINTERION_RADIO_GEN_NONE,
        .rssi      = 0.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 2G,673,-89,262,07,4EED,A500,16,16,7,4,G,5,-107,LIMSRV",
        .tech      = MM_CINTERION_RADIO_GEN_2G,
        .rssi      =  -89.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 2G,673,-80,262,07,4EED,A500,35,35,7,4,G,643,4,0,-80,0,S_FR",
        .tech      = MM_CINTERION_RADIO_GEN_2G,
        .rssi      = -80.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 3G,10564,296,-7.5,-79,262,02,0143,00228FF,-92,-78,NOCONN",
        .tech      = MM_CINTERION_RADIO_GEN_3G,
        .rssi      = 0.0,
        .ecn0      = -7.5,
        .rscp      = -79.0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 3G,SEARCH,SEARCH",
        .tech      = MM_CINTERION_RADIO_GEN_NONE,
        .rssi      = 0.0,
        .ecn0      = 0,
        .rscp      = 0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 3G,10564,96,-6.5,-77,262,02,0143,00228FF,-92,-78,LIMSRV",
        .tech      = MM_CINTERION_RADIO_GEN_3G,
        .rssi      =  0.0,
        .ecn0      = -6.5,
        .rscp      = -77.0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 3G,10737,131,-5,-93,260,01,7D3D,C80BC9A,--,--,----,---,-,-5,-93,0,01,06",
        .tech      = MM_CINTERION_RADIO_GEN_3G,
        .rssi      = 0.0,
        .ecn0      = -5.0,
        .rscp      = -93.0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 4G,6300,20,10,10,FDD,262,02,BF75,0345103,350,33,-94,-7,NOCONN",
        .tech      = MM_CINTERION_RADIO_GEN_4G,
        .rssi      = 0.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = -94.0,
        .rsrq      = -7.0
    },
    {
        .str       = "^SMONI: 4G,SEARCH",
        .tech      = MM_CINTERION_RADIO_GEN_NONE,
        .rssi      = 0.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = 0.0,
        .rsrq      = 0.0
    },
    {
        .str       = "^SMONI: 4G,6300,20,10,10,FDD,262,02,BF75,0345103,350,33,-90,-6,LIMSRV",
        .tech      = MM_CINTERION_RADIO_GEN_4G,
        .rssi      = 0.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = -90.0,
        .rsrq      = -6.0
    },
    {
        .str       = "^SMONI: 4G,6300,20,10,10,FDD,262,02,BF75,0345103,350,90,-101,-7,CONN",
        .tech      = MM_CINTERION_RADIO_GEN_4G,
        .rssi      = 0.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = -101.0,
        .rsrq      = -7.0
    },
    {
        .str       = "^SMONI: 4G,2850,7,20,20,FDD,262,02,C096,027430F,275,11,-114,-9,NOCONN",
        .tech      = MM_CINTERION_RADIO_GEN_4G,
        .rssi      = 0.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = -114.0,
        .rsrq      = -9.0
    },
    {
        .str       = "^SMONI: 4G,2850,7,20,20,FDD,262,02,C096,027430F,275,-,-113,-8,CONN",
        .tech      = MM_CINTERION_RADIO_GEN_4G,
        .rssi      = 0.0,
        .ecn0      = 0.0,
        .rscp      = 0.0,
        .rsrp      = -113.0,
        .rsrq      = -8.0
    }
};

static void
test_smoni_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (smoni_response_tests); i++) {
        GError                 *error = NULL;
        gboolean                success;
        MMCinterionRadioGen     tech = MM_CINTERION_RADIO_GEN_NONE;
        gdouble                 rssi = MM_SIGNAL_UNKNOWN;
        gdouble                 ecn0 = MM_SIGNAL_UNKNOWN;
        gdouble                 rscp = MM_SIGNAL_UNKNOWN;
        gdouble                 rsrp = MM_SIGNAL_UNKNOWN;
        gdouble                 rsrq = MM_SIGNAL_UNKNOWN;

        success = mm_cinterion_parse_smoni_query_response (smoni_response_tests[i].str,
                                                            &tech, &rssi,
                                                            &ecn0, &rscp,
                                                            &rsrp, &rsrq,
                                                            &error);
        g_assert_no_error (error);
        g_assert (success);

        g_assert_cmpuint (smoni_response_tests[i].tech,      ==, tech);
        switch (smoni_response_tests[i].tech) {
        case MM_CINTERION_RADIO_GEN_2G:
            g_assert_cmpfloat_tolerance (rssi, smoni_response_tests[i].rssi, 0.1);
            break;
        case MM_CINTERION_RADIO_GEN_3G:
            g_assert_cmpfloat_tolerance (ecn0, smoni_response_tests[i].ecn0, 0.1);
            g_assert_cmpfloat_tolerance (rscp, smoni_response_tests[i].rscp, 0.1);
            break;
        case MM_CINTERION_RADIO_GEN_4G:
            g_assert_cmpfloat_tolerance (rsrp, smoni_response_tests[i].rsrp, 0.1);
            g_assert_cmpfloat_tolerance (rsrq, smoni_response_tests[i].rsrq, 0.1);
            break;
        case MM_CINTERION_RADIO_GEN_NONE:
        default:
            break;
        }
    }
}

static void
test_smoni_response_to_signal (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (smoni_response_tests); i++) {
        GError   *error = NULL;
        gboolean  success;
        MMSignal *gsm  = NULL;
        MMSignal *umts = NULL;
        MMSignal *lte  = NULL;

        success = mm_cinterion_smoni_response_to_signal_info (smoni_response_tests[i].str,
                                                              &gsm, &umts, &lte,
                                                              &error);
        g_assert_no_error (error);
        g_assert (success);

        switch (smoni_response_tests[i].tech) {
        case MM_CINTERION_RADIO_GEN_2G:
            g_assert (gsm);
            g_assert_cmpfloat_tolerance (mm_signal_get_rssi (gsm), smoni_response_tests[i].rssi, 0.1);
            g_object_unref (gsm);
            g_assert (!umts);
            g_assert (!lte);
            break;
        case MM_CINTERION_RADIO_GEN_3G:
            g_assert (umts);
            g_assert_cmpfloat_tolerance (mm_signal_get_rscp (umts), smoni_response_tests[i].rscp, 0.1);
            g_assert_cmpfloat_tolerance (mm_signal_get_ecio (umts), smoni_response_tests[i].ecn0, 0.1);
            g_object_unref (umts);
            g_assert (!gsm);
            g_assert (!lte);
            break;
        case MM_CINTERION_RADIO_GEN_4G:
            g_assert (lte);
            g_assert_cmpfloat_tolerance (mm_signal_get_rsrp (lte), smoni_response_tests[i].rsrp, 0.1);
            g_assert_cmpfloat_tolerance (mm_signal_get_rsrq (lte), smoni_response_tests[i].rsrq, 0.1);
            g_object_unref (lte);
            g_assert (!gsm);
            g_assert (!umts);
            break;
        case MM_CINTERION_RADIO_GEN_NONE:
        default:
            g_assert (!gsm);
            g_assert (!umts);
            g_assert (!lte);
            break;
        }
    }
}

/*****************************************************************************/
/* Test ^SCFG="MEopMode/Prov/Cfg" responses */

typedef struct {
    const gchar            *str;
    MMCinterionModemFamily  modem_family;
    gboolean                success;
    guint                   expected_cid;
} ProvcfgResponseTest;

static const ProvcfgResponseTest provcfg_response_tests[] = {
    {

        .str          = "^SCFG: \"MEopMode/Prov/Cfg\",\"vdfde\"",
        .modem_family = MM_CINTERION_MODEM_FAMILY_DEFAULT,
        .success      = TRUE,
        .expected_cid = 1,
    },
    {

        .str          = "* ^SCFG: \"MEopMode/Prov/Cfg\",\"attus\"",
        .modem_family = MM_CINTERION_MODEM_FAMILY_IMT,
        .success      = TRUE,
        .expected_cid = 1,
    },
    {

        .str          = "* ^SCFG: \"MEopMode/Prov/Cfg\",\"2\"",
        .modem_family = MM_CINTERION_MODEM_FAMILY_DEFAULT,
        .success      = TRUE,
        .expected_cid = 3,
    },
    {

        .str          = "* ^SCFG: \"MEopMode/Prov/Cfg\",\"vzwdcus\"",
        .modem_family = MM_CINTERION_MODEM_FAMILY_DEFAULT,
        .success      = TRUE,
        .expected_cid = 3,
    },
    {

        .str          = "* ^SCFG: \"MEopMode/Prov/Cfg\",\"tmode\"",
        .modem_family = MM_CINTERION_MODEM_FAMILY_DEFAULT,
        .success      = TRUE,
        .expected_cid = 2,
    },
    {
        .str          = "* ^SCFG: \"MEopMode/Prov/Cfg\",\"fallback*\"",
        .modem_family = MM_CINTERION_MODEM_FAMILY_DEFAULT,
        .success      = TRUE,
        .expected_cid = 1,
    },
    {
        /* commas not allowed by the regex */
        .str          = "* ^SCFG: \"MEopMode/Prov/Cfg\",\"something,with,commas\"",
        .modem_family = MM_CINTERION_MODEM_FAMILY_DEFAULT,
        .success      = FALSE,
    }
};

static void
test_provcfg_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (provcfg_response_tests); i++) {
        gint      cid = -1;
        gboolean  result;
        GError   *error = NULL;

        result = mm_cinterion_provcfg_response_to_cid (provcfg_response_tests[i].str,
                                                       provcfg_response_tests[i].modem_family,
                                                       MM_MODEM_CHARSET_GSM,
                                                       NULL,
                                                       &cid,
                                                       &error);
        if (provcfg_response_tests[i].success) {
            g_assert_no_error (error);
            g_assert (result);
            g_assert_cmpuint (cid, ==, provcfg_response_tests[i].expected_cid);
        } else {
            g_assert (error);
            g_assert (!result);
        }
    }
}

/*****************************************************************************/
/* Test ^SGAUTH responses */

static void
test_sgauth_response (void)
{
    gboolean             result;
    MMBearerAllowedAuth  auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;
    gchar               *username = NULL;
    GError              *error = NULL;

    const gchar *response =
        "^SGAUTH: 1,2,\"vf\"\r\n"
        "^SGAUTH: 2,1,\"\"\r\n"
        "^SGAUTH: 3,0\r\n";

    /* CID 1 */
    result = mm_cinterion_parse_sgauth_response (response, 1, &auth, &username, &error);
    g_assert_no_error (error);
    g_assert (result);
    g_assert_cmpuint (auth, ==, MM_BEARER_ALLOWED_AUTH_CHAP);
    g_assert_cmpstr (username, ==, "vf");

    auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;
    g_clear_pointer (&username, g_free);

    /* CID 2 */
    result = mm_cinterion_parse_sgauth_response (response, 2, &auth, &username, &error);
    g_assert_no_error (error);
    g_assert (result);
    g_assert_cmpuint (auth, ==, MM_BEARER_ALLOWED_AUTH_PAP);
    g_assert_null (username);

    auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;

    /* CID 3 */
    result = mm_cinterion_parse_sgauth_response (response, 3, &auth, &username, &error);
    g_assert_no_error (error);
    g_assert (result);
    g_assert_cmpuint (auth, ==, MM_BEARER_ALLOWED_AUTH_NONE);
    g_assert_null (username);

    auth = MM_BEARER_ALLOWED_AUTH_UNKNOWN;

    /* CID 4 */
    result = mm_cinterion_parse_sgauth_response (response, 4, &auth, &username, &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND);
    g_assert (!result);
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/cinterion/scfg",                    test_scfg);
    g_test_add_func ("/MM/cinterion/scfg/ehs5",               test_scfg_ehs5);
    g_test_add_func ("/MM/cinterion/scfg/pls62/gsm",          test_scfg_pls62_gsm);
    g_test_add_func ("/MM/cinterion/scfg/pls62/ucs2",         test_scfg_pls62_ucs2);
    g_test_add_func ("/MM/cinterion/scfg/alas5",              test_scfg_alas5);
    g_test_add_func ("/MM/cinterion/scfg/response/3g",        test_scfg_response_3g);
    g_test_add_func ("/MM/cinterion/scfg/response/2g",        test_scfg_response_2g);
    g_test_add_func ("/MM/cinterion/scfg/response/pls62/gsm", test_scfg_response_pls62_gsm);
    g_test_add_func ("/MM/cinterion/scfg/response/pls62/ucs2",test_scfg_response_pls62_ucs2);
    g_test_add_func ("/MM/cinterion/scfg/response/alas5",     test_scfg_response_alas5);
    g_test_add_func ("/MM/cinterion/cnmi/phs8",               test_cnmi_phs8);
    g_test_add_func ("/MM/cinterion/cnmi/other",              test_cnmi_other);
    g_test_add_func ("/MM/cinterion/swwan/pls8",              test_swwan_pls8);
    g_test_add_func ("/MM/cinterion/sind/response/simstatus", test_sind_response_simstatus);
    g_test_add_func ("/MM/cinterion/smong/response/tc63i",    test_smong_response_tc63i);
    g_test_add_func ("/MM/cinterion/smong/response/other",    test_smong_response_other);
    g_test_add_func ("/MM/cinterion/smong/response/no-match", test_smong_response_no_match);
    g_test_add_func ("/MM/cinterion/slcc/urc/empty",          test_slcc_urc_empty);
    g_test_add_func ("/MM/cinterion/slcc/urc/single",         test_slcc_urc_single);
    g_test_add_func ("/MM/cinterion/slcc/urc/multiple",       test_slcc_urc_multiple);
    g_test_add_func ("/MM/cinterion/slcc/urc/complex",        test_slcc_urc_complex);
    g_test_add_func ("/MM/cinterion/ctzu/urc/simple",         test_ctzu_urc_simple);
    g_test_add_func ("/MM/cinterion/ctzu/urc/full",           test_ctzu_urc_full);
    g_test_add_func ("/MM/cinterion/smoni/query_response",    test_smoni_response);
    g_test_add_func ("/MM/cinterion/smoni/query_response_to_signal", test_smoni_response_to_signal);
    g_test_add_func ("/MM/cinterion/scfg/provcfg",            test_provcfg_response);
    g_test_add_func ("/MM/cinterion/sgauth",                  test_sgauth_response);

    return g_test_run ();
}
