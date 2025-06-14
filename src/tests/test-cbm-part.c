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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers.h"
#include "mm-cbm-part.h"
#include "mm-charsets.h"
#include "mm-log-test.h"

static void
test_cbm_ca (void)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(MMCbmPart) part = NULL;
    guint16 serial;

    static const guint8 pdu[] = {
        0x67, 0x60, 0x11, 0x12, 0x0F, 0x16,
        0x54, 0x74, 0x7A, 0x0E, 0x4A, 0xCF, 0x41, 0x61,
        0x10, 0xBD, 0x3C, 0xA7, 0x83, 0xDE, 0x66, 0x10,
        0x1D, 0x5D, 0x06, 0x3D, 0xDD, 0xF4, 0xB0, 0x3C,
        0xFD, 0x06, 0x05, 0xD9, 0x65, 0x39, 0x1D, 0x24,
        0x2D, 0x87, 0xC9, 0x79, 0xD0, 0x34, 0x3F, 0xA7,
        0x97, 0xDB, 0x2E, 0x10, 0x15, 0x5D, 0x96, 0x97,
        0x41, 0xE9, 0x39, 0xC8, 0xFD, 0x06, 0x91, 0xC3,
        0xEE, 0x73, 0x59, 0x0E, 0xA2, 0xBF, 0x41, 0xF9,
        0x77, 0x5D, 0x0E, 0x42, 0x97, 0xC3, 0x6C, 0x3A,
        0x1A, 0xF4, 0x96, 0x83, 0xE6, 0x61, 0x73, 0x99,
        0x9E, 0x07
    };

    part = mm_cbm_part_new_from_binary_pdu (pdu, G_N_ELEMENTS (pdu), NULL, &err);
    g_assert_no_error (err);
    g_assert_nonnull (part);

    serial = mm_cbm_part_get_serial (part);
    g_assert_cmpuint (CBM_SERIAL_GEO_SCOPE (serial), ==, MM_CBM_GEO_SCOPE_PLMN);
    g_assert_true (CBM_SERIAL_MESSAGE_CODE_ALERT (serial));
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_POPUP (serial));
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE (serial), ==, 0x76);
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE_UPDATE (serial), ==, 0);

    /* CA: Emergency alert */
    g_assert_cmpuint (mm_cbm_part_get_channel (part), ==, 4370);

    g_assert_cmpuint (mm_cbm_part_get_num_parts (part), ==, 6);
    g_assert_cmpuint (mm_cbm_part_get_part_num (part), ==, 1);

    g_assert_cmpstr (mm_cbm_part_get_text (part), ==,
                     "This is a test of the Ontario Alert Ready System. There is no danger to your health or safety" );

    g_assert_null (mm_cbm_part_get_language (part));
}

static void
test_cbm_ucs2 (void)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(MMCbmPart) part = NULL;
    guint16 serial;

    static const guint8 pdu [] = {
        0x63, 0x40, 0x00, 0x32, 0x59, 0x14,
        0x00, 0x20, 0x04, 0x1f, 0x04, 0x40, 0x04, 0x3e,
        0x04, 0x42, 0x04, 0x4f, 0x04, 0x33, 0x04, 0x3e,
        0x04, 0x3c, 0x00, 0x20, 0x04, 0x34, 0x04, 0x3d,
        0x04, 0x4f, 0x00, 0x20, 0x04, 0x54, 0x00, 0x20,
        0x04, 0x32, 0x04, 0x38, 0x04, 0x41, 0x04, 0x3e,
        0x04, 0x3a, 0x04, 0x30, 0x00, 0x20, 0x04, 0x56,
        0x04, 0x3c, 0x04, 0x3e, 0x04, 0x32, 0x04, 0x56,
        0x04, 0x40, 0x04, 0x3d, 0x04, 0x56, 0x04, 0x41,
        0x04, 0x42, 0x04, 0x4c, 0x00, 0x20, 0x04, 0x40,
        0x04, 0x30, 0x04, 0x3a, 0x04, 0x35, 0x04, 0x42,
        0x04, 0x3d, 0x16, 0x01, 0x00, 0x00};

    part = mm_cbm_part_new_from_binary_pdu (pdu, G_N_ELEMENTS (pdu), NULL, &err);
    g_assert_no_error (err);
    g_assert_nonnull (part);

    serial = mm_cbm_part_get_serial (part);
    g_assert_cmpuint (CBM_SERIAL_GEO_SCOPE (serial), ==, MM_CBM_GEO_SCOPE_PLMN);
    g_assert_true (CBM_SERIAL_MESSAGE_CODE_ALERT (serial));
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_POPUP (serial));
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE (serial), ==, 0x34);
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE_UPDATE (serial), ==, 0);

    g_assert_cmpuint (mm_cbm_part_get_channel (part), ==, 50);

    g_assert_cmpuint (mm_cbm_part_get_num_parts (part), ==, 4);
    g_assert_cmpuint (mm_cbm_part_get_part_num (part), ==, 1);

    g_assert_cmpstr (mm_cbm_part_get_text (part), ==,
                     " Протягом дня є висока імовірність ракетнᘁ");

    g_assert_null (mm_cbm_part_get_language (part));
}


static void
test_cbm_eu (void)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(MMCbmPart) part = NULL;
    guint16 serial;

    static const guint8 pdu[] = {
        0x40, 0xC0, 0x11, 0x1F, 0x01, 0x13,
        0xD4, 0xE2, 0x94, 0x0A, 0x0A, 0x32, 0x8B, 0x52,
        0x2A, 0x0B, 0xE4, 0x0C, 0x52, 0x93, 0x4F, 0xE7,
        0x35, 0x49, 0x2C, 0x82, 0x82, 0xCC, 0xA2, 0x94,
        0x0A, 0x22, 0x06, 0xB3, 0x20, 0x19, 0x4C, 0x26,
        0x03, 0x51, 0xD1, 0x75, 0x90, 0x0C, 0x26, 0x93,
        0xBD, 0x62, 0xB2, 0x17, 0x0C, 0x07, 0x6A, 0x81,
        0x62, 0x30, 0x5D, 0x2D, 0x07, 0x0A, 0xB7, 0x41,
        0x2D, 0x10, 0xB5, 0x3C, 0xA7, 0x83, 0xC2, 0xEC,
        0xB2, 0x9C, 0x0E, 0x6A, 0x81, 0xCC, 0x6F, 0x39,
        0x88, 0x58, 0xAE, 0xD3, 0xE7, 0x63, 0x34, 0x3B,
        0xEC, 0x06,
    };

    part = mm_cbm_part_new_from_binary_pdu (pdu, G_N_ELEMENTS (pdu), NULL, &err);
    g_assert_no_error (err);
    g_assert_nonnull (part);

    serial = mm_cbm_part_get_serial (part);
    g_assert_cmpuint (CBM_SERIAL_GEO_SCOPE (serial), ==, MM_CBM_GEO_SCOPE_PLMN);
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_ALERT (serial));
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_POPUP (serial));
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE (serial), ==, 0x0C);
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE_UPDATE (serial), ==, 0);

    /* DE: Emergency alert */
    g_assert_cmpuint (mm_cbm_part_get_channel (part), ==, 4383);

    g_assert_cmpuint (mm_cbm_part_get_num_parts (part), ==, 3);
    g_assert_cmpuint (mm_cbm_part_get_part_num (part), ==, 1);

    g_assert_cmpstr (mm_cbm_part_get_text (part), ==,
                     "TEST ALERT, NATIONWIDE ALERT DAY 2022 Thu 2022/12/08 - 10:59 am - Test alert - for Deutschlan");

    g_assert_cmpstr (mm_cbm_part_get_language (part), ==, "en");
}

static void
parse_cbm (const char *str, MMCbmPart **part)
{
    g_autoptr(GRegex) r = mm_3gpp_cbm_regex_get ();
    g_autoptr(GMatchInfo)  match_info = NULL;
    g_autoptr(GError) err = NULL;
    g_autofree char *pdu = NULL;

    g_assert_true (g_regex_match (r, str, 0, &match_info));
    g_assert_true (g_match_info_matches (match_info));

    pdu = g_match_info_fetch (match_info, 2);
    g_assert (pdu);

    *part = mm_cbm_part_new_from_pdu (pdu, NULL, &err);
    g_assert_no_error (err);
    g_assert (*part);
}

static void
test_cbm_nl_2023 (void)
{
    MMCbmPart *part;
    guint16 serial;

    parse_cbm ("\r\n+CBM: 88\r\n46A0111305134E662BC82ECBE92018AD1593B56430D90C1493E960301D885A9C528545697288A4BA40C432E86D2FCBD1E53419740F87E5F331BA7EA783D465103DAD2697DD7390FBFD26CFD3F47A989E2ECF41F67418E404\r\n", &part);
    serial = mm_cbm_part_get_serial (part);
    g_assert_cmpuint (CBM_SERIAL_GEO_SCOPE (serial), ==, MM_CBM_GEO_SCOPE_PLMN);
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_ALERT (serial));
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_POPUP (serial));
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE (serial), ==, 0x6A);
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE_UPDATE (serial), ==, 0);
    /* NL: NL-Alert */
    g_assert_cmpuint (mm_cbm_part_get_channel (part), ==, 4371);
    g_assert_cmpuint (mm_cbm_part_get_num_parts (part), ==, 3);
    g_assert_cmpuint (mm_cbm_part_get_part_num (part), ==, 1);
    g_assert_cmpstr (mm_cbm_part_get_text (part), ==,
                     "NL-Alert 04-12-2023 12:00: TESTBERICHT. De overheid waarschuwt je tijdens noodsituaties via N");
    g_assert_cmpstr (mm_cbm_part_get_language (part), ==, "nl");
    mm_cbm_part_free (part);

    parse_cbm ("\r\n+CBM: 88\r\n46A011130523CC56905D96D35D206519C42E97E7741039EC06DDC37490BA0C6ABFCB7410F95D7683CA6ED03D1C9683D46550BB5C9683D26EF35BDE0ED3D365D03AEC06D9D36E72D9ED02A9542A10B538A5829AC5E9347804\r\n", &part);
    serial = mm_cbm_part_get_serial (part);
    g_assert_cmpuint (CBM_SERIAL_GEO_SCOPE (serial), ==, MM_CBM_GEO_SCOPE_PLMN);
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_ALERT (serial));
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_POPUP (serial));
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE (serial), ==, 0x6A);
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE_UPDATE (serial), ==, 0);
    g_assert_cmpuint (mm_cbm_part_get_channel (part), ==, 4371);
    g_assert_cmpuint (mm_cbm_part_get_num_parts (part), ==, 3);
    g_assert_cmpuint (mm_cbm_part_get_part_num (part), ==, 2);
    g_assert_cmpstr (mm_cbm_part_get_text (part), ==,
                     "L-Alert. Je leest dan wat je moet doen en waar je meer informatie kan vinden. *** TEST MESSAG");
    g_assert_cmpstr (mm_cbm_part_get_language (part), ==, "nl");
    mm_cbm_part_free (part);

    parse_cbm ("\r\n+CBM: 65\r\n46A0111305334590B34C4797E5ECB09B3C071DDFF6B2DCDD2EBBE920685DCC4E8F41D7B0DC9D769F41D3FC9C5E6EBB40CE37283CA6A7DF6E90BC1CAFA7E565B2AB\r\n", &part);
    serial = mm_cbm_part_get_serial (part);
    g_assert_cmpuint (CBM_SERIAL_GEO_SCOPE (serial), ==, MM_CBM_GEO_SCOPE_PLMN);
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_ALERT (serial));
    g_assert_false (CBM_SERIAL_MESSAGE_CODE_POPUP (serial));
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE (serial), ==, 0x6A);
    g_assert_cmpuint (CBM_SERIAL_MESSAGE_CODE_UPDATE (serial), ==, 0);
    g_assert_cmpuint (mm_cbm_part_get_channel (part), ==, 4371);
    g_assert_cmpuint (mm_cbm_part_get_num_parts (part), ==, 3);
    g_assert_cmpuint (mm_cbm_part_get_part_num (part), ==, 3);
    g_assert_cmpstr (mm_cbm_part_get_text (part), ==,
                     "E Netherlands Government Public Warning System. No action required." );
    g_assert_cmpstr (mm_cbm_part_get_language (part), ==, "nl");
    mm_cbm_part_free (part);
}


static void
test_cbm_gsm7bit_with_lang (void)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(MMCbmPart) part = NULL;

    static const guint8 pdu[] = {
        0x40, 0xC0, 0x11, 0x1F, 0x10 /* GSM 7Bit with language */, 0x13,
        0x64 /* d */, 0x65 /* e */, 0x0D /* \r */, 0x0A, 0x0A, 0x32, 0x8B, 0x52,
        0x2A, 0x0B, 0xE4, 0x0C, 0x52, 0x93, 0x4F, 0xE7,
    };

    part = mm_cbm_part_new_from_binary_pdu (pdu, G_N_ELEMENTS (pdu), NULL, &err);
    g_assert_no_error (err);
    g_assert_nonnull (part);

    g_assert_cmpuint (mm_cbm_part_get_channel (part), ==, 4383);
    g_assert_cmpuint (mm_cbm_part_get_num_parts (part), ==, 3);
    g_assert_cmpuint (mm_cbm_part_get_part_num (part), ==, 1);
    g_assert_cmpstr (mm_cbm_part_get_language (part), ==, "de");
}


static void
test_cbm_ucs2_with_7bit_lang (void)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(MMCbmPart) part = NULL;

    static const guint8 pdu [] = {
        0x63, 0x40, 0x00, 0x32, 0x11 /* UCS2 with 7Bit language */, 0x14,
        0xF2 /* ru …*/ , 0x7A /* … in GSM 7bit */, 0x04, 0x1f, 0x04, 0x40, 0x04, 0x3e,
    };

    part = mm_cbm_part_new_from_binary_pdu (pdu, G_N_ELEMENTS (pdu), NULL, &err);
    g_assert_no_error (err);
    g_assert_nonnull (part);

    g_assert_cmpuint (mm_cbm_part_get_channel (part), ==, 50);
    g_assert_cmpuint (mm_cbm_part_get_num_parts (part), ==, 4);
    g_assert_cmpuint (mm_cbm_part_get_part_num (part), ==, 1);
    g_assert_cmpstr (mm_cbm_part_get_language (part), ==, "ru");
}


int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    /* First part of ontario alert: */
    /* https://gitlab.freedesktop.org/mobile-broadband/ModemManager/-/issues/253#note_1161764 */
    g_test_add_func ("/MM/CBM/PDU-Parser/CBM-CA", test_cbm_ca);
    /* UCS2 message: */
    /* https://github.com/the-modem-distro/meta-qcom/blob/9d17dfa55599fe4708806a6f4103fee6cc2830f8/recipes-modem/openqti/files/src/chat_helpers.c#L416 */
    g_test_add_func ("/MM/CBM/PDU-Parser/UCS2", test_cbm_ucs2);
    /* First part of EU alert: */
    /* https://source.puri.sm/Librem5/OS-issues/-/issues/303 */
    g_test_add_func ("/MM/CBM/PDU-Parser/CBM-EU", test_cbm_eu);
    /* 2023 NL alert: */
    /* https://gitlab.freedesktop.org/mobile-broadband/ModemManager/-/issues/253#note_2192474 */
    g_test_add_func ("/MM/CBM/PDU-Parser/CBM-NL", test_cbm_nl_2023);
    /* GSM7 bit encoding with language in the first 3 bytes of the message */
    g_test_add_func ("/MM/CBM/PDU-Parser/has-lang", test_cbm_gsm7bit_with_lang);
    /* UCS2 encoding with 7bit language in first 2 bytes of the message */
    g_test_add_func ("/MM/CBM/PDU-Parser/has-7bit-lang", test_cbm_ucs2_with_7bit_lang);

    return g_test_run ();
}
