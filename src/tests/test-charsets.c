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
 * Copyright (C) 2010 Red Hat, Inc.
 */

#include <glib.h>
#include <string.h>

#include "mm-modem-helpers.h"
#include "mm-log.h"

static void
test_def_chars (void *f, gpointer d)
{
    /* Test that a string with all the characters in the GSM 03.38 charset
     * are converted from UTF-8 to GSM and back to UTF-8 successfully.
     */
    static const char *s = "@£$¥èéùìòÇ\nØø\rÅåΔ_ΦΓΛΩΠΨΣΘΞÆæßÉ !\"#¤%&'()*+,-./0123456789:;<=>?¡ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÑÜ§¿abcdefghijklmnopqrstuvwxyzäöñüà";
    guint8 *gsm, *utf8;
    guint32 len = 0;

    /* Convert to GSM */
    gsm = mm_charset_utf8_to_unpacked_gsm (s, &len);
    g_assert (gsm);
    g_assert_cmpint (len, ==, 127);

    /* And back to UTF-8 */
    utf8 = mm_charset_gsm_unpacked_to_utf8 (gsm, len);
    g_assert (utf8);
    g_assert_cmpstr (s, ==, (const char *) utf8);

    g_free (gsm);
    g_free (utf8);
}

static void
test_esc_chars (void *f, gpointer d)
{
    /* Test that a string with all the characters in the extended GSM 03.38
     * charset are converted from UTF-8 to GSM and back to UTF-8 successfully.
     */
    static const char *s = "\f^{}\\[~]|€";
    guint8 *gsm, *utf8;
    guint32 len = 0;

    /* Convert to GSM */
    gsm = mm_charset_utf8_to_unpacked_gsm (s, &len);
    g_assert (gsm);
    g_assert_cmpint (len, ==, 20);

    /* And back to UTF-8 */
    utf8 = mm_charset_gsm_unpacked_to_utf8 (gsm, len);
    g_assert (utf8);
    g_assert_cmpstr (s, ==, (const char *) utf8);

    g_free (gsm);
    g_free (utf8);
}

static void
test_mixed_chars (void *f, gpointer d)
{
    /* Test that a string with a mix of GSM 03.38 default and extended characters
     * is converted from UTF-8 to GSM and back to UTF-8 successfully.
     */
    static const char *s = "@£$¥èéùìø\fΩΠΨΣΘ{ΞÆæß(})789\\:;<=>[?¡QRS]TUÖ|ÑÜ§¿abpqrstuvöñüà€";
    guint8 *gsm, *utf8;
    guint32 len = 0;

    /* Convert to GSM */
    gsm = mm_charset_utf8_to_unpacked_gsm (s, &len);
    g_assert (gsm);
    g_assert_cmpint (len, ==, 69);

    /* And back to UTF-8 */
    utf8 = mm_charset_gsm_unpacked_to_utf8 (gsm, len);
    g_assert (utf8);
    g_assert_cmpstr (s, ==, (const char *) utf8);

    g_free (gsm);
    g_free (utf8);
}

static void
test_unpack_gsm7 (void *f, gpointer d)
{
    static const guint8 gsm[] = { 0xC8, 0xF7, 0x1D, 0x14, 0x96, 0x97, 0x41, 0xF9, 0x77, 0xFD, 0x07 };
    static const guint8 expected[] = { 0x48, 0x6f, 0x77, 0x20, 0x61, 0x72, 0x65, 0x20, 0x79, 0x6f, 0x75, 0x3f };
    guint8 *unpacked;
    guint32 unpacked_len = 0;

    unpacked = gsm_unpack (gsm, (sizeof (gsm) * 8) / 7, 0, &unpacked_len);
    g_assert (unpacked);
    g_assert_cmpint (unpacked_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (unpacked, expected, unpacked_len), ==, 0);

    g_free (unpacked);
}

static void
test_unpack_gsm7_7_chars (void *f, gpointer d)
{
    static const guint8 gsm[] = { 0xF1, 0x7B, 0x59, 0x4E, 0xCF, 0xD7, 0x01 };
    static const guint8 expected[] = { 0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75};
    guint8 *unpacked;
    guint32 unpacked_len = 0;

    /* Tests the edge case where there are 7 bits left in the packed
     * buffer but those 7 bits do not contain a character.  In this case
     * we expect to get the number of characters that were specified.
     */

    unpacked = gsm_unpack (gsm, 7 , 0, &unpacked_len);
    g_assert (unpacked);
    g_assert_cmpint (unpacked_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (unpacked, expected, unpacked_len), ==, 0);

    g_free (unpacked);
}

static void
test_unpack_gsm7_all_chars (void *f, gpointer d)
{
    /* Packed array of all chars in GSM default and extended charset */
    static const guint8 gsm[] = {
        0x80, 0x80, 0x60, 0x40, 0x28, 0x18, 0x0E, 0x88, 0x84, 0x62, 0xC1, 0x68,
        0x38, 0x1E, 0x90, 0x88, 0x64, 0x42, 0xA9, 0x58, 0x2E, 0x98, 0x8C, 0x66,
        0xC3, 0xE9, 0x78, 0x3E, 0xA0, 0x90, 0x68, 0x44, 0x2A, 0x99, 0x4E, 0xA8,
        0x94, 0x6A, 0xC5, 0x6A, 0xB9, 0x5E, 0xB0, 0x98, 0x6C, 0x46, 0xAB, 0xD9,
        0x6E, 0xB8, 0x9C, 0x6E, 0xC7, 0xEB, 0xF9, 0x7E, 0xC0, 0xA0, 0x70, 0x48,
        0x2C, 0x1A, 0x8F, 0xC8, 0xA4, 0x72, 0xC9, 0x6C, 0x3A, 0x9F, 0xD0, 0xA8,
        0x74, 0x4A, 0xAD, 0x5A, 0xAF, 0xD8, 0xAC, 0x76, 0xCB, 0xED, 0x7A, 0xBF,
        0xE0, 0xB0, 0x78, 0x4C, 0x2E, 0x9B, 0xCF, 0xE8, 0xB4, 0x7A, 0xCD, 0x6E,
        0xBB, 0xDF, 0xF0, 0xB8, 0x7C, 0x4E, 0xAF, 0xDB, 0xEF, 0xF8, 0xBC, 0x7E,
        0xCF, 0xEF, 0xFB, 0xFF, 0x1B, 0xC5, 0x86, 0xB2, 0x41, 0x6D, 0x52, 0x9B,
        0xD7, 0x86, 0xB7, 0xE9, 0x6D, 0x7C, 0x1B, 0xE0, 0xA6, 0x0C
    };
    static const guint8 ext[] = {
        0x1B, 0x0A, 0x1B, 0x14, 0x1B, 0x28, 0x1B, 0x29, 0x1B, 0x2F, 0x1B, 0x3C,
        0x1B, 0x3D, 0x1B, 0x3E, 0x1B, 0x40, 0x1B, 0x65
    };
    guint8 *unpacked;
    guint32 unpacked_len = 0;
    int i;

    unpacked = gsm_unpack (gsm, (sizeof (gsm) * 8) / 7, 0, &unpacked_len);
    g_assert (unpacked);
    g_assert_cmpint (unpacked_len, ==, 148);

    /* Test default chars */
    for (i = 0; i < 128; i++)
        g_assert_cmpint (unpacked[i], ==, i);

    /* Text extended chars */
    g_assert_cmpint (memcmp ((guint8 *) (unpacked + 128), &ext[0], sizeof (ext)), ==, 0);

    g_free (unpacked);
}

static void
test_pack_gsm7 (void *f, gpointer d)
{
    static const guint8 unpacked[] = { 0x48, 0x6f, 0x77, 0x20, 0x61, 0x72, 0x65, 0x20, 0x79, 0x6f, 0x75, 0x3f };
    static const guint8 expected[] = { 0xC8, 0xF7, 0x1D, 0x14, 0x96, 0x97, 0x41, 0xF9, 0x77, 0xFD, 0x07 };
    guint8 *packed;
    guint32 packed_len = 0;

    packed = gsm_pack (unpacked, sizeof (unpacked), 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (packed, expected, packed_len), ==, 0);

    g_free (packed);
}

static void
test_pack_gsm7_7_chars (void *f, gpointer d)
{
    static const guint8 unpacked[] = { 0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75 };
    static const guint8 expected[] = { 0xF1, 0x7B, 0x59, 0x4E, 0xCF, 0xD7, 0x01 };
    guint8 *packed;
    guint32 packed_len = 0;

    /* Tests the edge case where there are 7 bits left in the packed
     * buffer but those 7 bits do not contain a character.  In this case
     * we expect a trailing NULL byte and the caller must know enough about
     * the intended message to remove it when required.
     */

    packed = gsm_pack (unpacked, sizeof (unpacked), 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (packed, expected, packed_len), ==, 0);

    g_free (packed);
}

#if 0
static void
print_array (const guint8 *array, guint32 len)
{
    int col;
    guint8 c;

    g_print ("\n");
    for (c = 0, col = 0; c < len; c++) {
        g_print ("0x%02X, ", array[c] & 0xFF);
        if (col++ == 11) {
            col = 0;
            g_print ("\n");
        }
    }
    g_print ("\n");
}
#endif

static void
test_pack_gsm7_all_chars (void *f, gpointer d)
{
    /* Packed array of all chars in GSM default and extended charset */
    static const guint8 expected[] = {
        0x80, 0x80, 0x60, 0x40, 0x28, 0x18, 0x0E, 0x88, 0x84, 0x62, 0xC1, 0x68,
        0x38, 0x1E, 0x90, 0x88, 0x64, 0x42, 0xA9, 0x58, 0x2E, 0x98, 0x8C, 0x66,
        0xC3, 0xE9, 0x78, 0x3E, 0xA0, 0x90, 0x68, 0x44, 0x2A, 0x99, 0x4E, 0xA8,
        0x94, 0x6A, 0xC5, 0x6A, 0xB9, 0x5E, 0xB0, 0x98, 0x6C, 0x46, 0xAB, 0xD9,
        0x6E, 0xB8, 0x9C, 0x6E, 0xC7, 0xEB, 0xF9, 0x7E, 0xC0, 0xA0, 0x70, 0x48,
        0x2C, 0x1A, 0x8F, 0xC8, 0xA4, 0x72, 0xC9, 0x6C, 0x3A, 0x9F, 0xD0, 0xA8,
        0x74, 0x4A, 0xAD, 0x5A, 0xAF, 0xD8, 0xAC, 0x76, 0xCB, 0xED, 0x7A, 0xBF,
        0xE0, 0xB0, 0x78, 0x4C, 0x2E, 0x9B, 0xCF, 0xE8, 0xB4, 0x7A, 0xCD, 0x6E,
        0xBB, 0xDF, 0xF0, 0xB8, 0x7C, 0x4E, 0xAF, 0xDB, 0xEF, 0xF8, 0xBC, 0x7E,
        0xCF, 0xEF, 0xFB, 0xFF, 0x1B, 0xC5, 0x86, 0xB2, 0x41, 0x6D, 0x52, 0x9B,
        0xD7, 0x86, 0xB7, 0xE9, 0x6D, 0x7C, 0x1B, 0xE0, 0xA6, 0x0C
    };
    static const guint8 ext[] = {
        0x1B, 0x0A, 0x1B, 0x14, 0x1B, 0x28, 0x1B, 0x29, 0x1B, 0x2F, 0x1B, 0x3C,
        0x1B, 0x3D, 0x1B, 0x3E, 0x1B, 0x40, 0x1B, 0x65
    };
    guint8 *packed, c;
    guint32 packed_len = 0;
    GByteArray *unpacked;

    unpacked = g_byte_array_sized_new (148);
    for (c = 0; c < 128; c++)
        g_byte_array_append (unpacked, &c, 1);
    for (c = 0; c < sizeof (ext); c++)
        g_byte_array_append (unpacked, &ext[c], 1);

    packed = gsm_pack (unpacked->data, unpacked->len, 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (packed, expected, packed_len), ==, 0);

    g_free (packed);
    g_byte_array_free (unpacked, TRUE);
}

static void
test_pack_gsm7_24_chars (void *f, gpointer d)
{
    static const guint8 unpacked[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
        0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
    };
    guint8 *packed;
    guint32 packed_len = 0;

    /* Tests that no empty trailing byte is added when all the 7-bit characters
     * are packed into an exact number of bytes.
     */

    packed = gsm_pack (unpacked, sizeof (unpacked), 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, 21);

    g_free (packed);
}

static void
test_pack_gsm7_last_septet_alone (void *f, gpointer d)
{
    static const guint8 unpacked[] = {
        0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x72, 0x65, 0x61, 0x6C,
        0x6C, 0x79, 0x20, 0x63, 0x6F, 0x6F, 0x6C, 0x20, 0x10, 0x10, 0x10, 0x10,
        0x10
    };
    static const guint8 expected[] = {
        0x54, 0x74, 0x7A, 0x0E, 0x4A, 0xCF, 0x41, 0xF2, 0x72, 0x98, 0xCD, 0xCE,
        0x83, 0xC6, 0xEF, 0x37, 0x1B, 0x04, 0x81, 0x40, 0x20, 0x10
    };
    guint8 *packed;
    guint32 packed_len = 0;

    /* Tests that a 25-character unpacked string (where, when packed, the last
     * septet will be in an octet by itself) packs correctly.
     */

    packed = gsm_pack (unpacked, sizeof (unpacked), 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));

    g_free (packed);
}

static void
test_pack_gsm7_7_chars_offset (void *f, gpointer d)
{
    static const guint8 unpacked[] = { 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x10, 0x2F };
    static const guint8 expected[] = { 0x00, 0x5D, 0x66, 0xB3, 0xDF, 0x90, 0x17 };
    guint8 *packed;
    guint32 packed_len = 0;

    packed = gsm_pack (unpacked, sizeof (unpacked), 5, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (packed, expected, packed_len), ==, 0);

    g_free (packed);
}

static void
test_take_convert_ucs2_hex_utf8 (void *f, gpointer d)
{
    gchar *src, *converted;

    /* Ensure hex-encoded UCS-2 works */
    src = g_strdup ("0054002d004d006f00620069006c0065");
    converted = mm_charset_take_and_convert_to_utf8 (src, MM_MODEM_CHARSET_UCS2);
    g_assert_cmpstr (converted, ==, "T-Mobile");
    g_free (converted);
}

static void
test_take_convert_ucs2_bad_ascii (void *f, gpointer d)
{
    gchar *src, *converted;

    /* Test that something mostly ASCII returns most of the original string */
    src = g_strdup ("Orange\241");
    converted = mm_charset_take_and_convert_to_utf8 (src, MM_MODEM_CHARSET_UCS2);
    g_assert_cmpstr (converted, ==, "Orange");
    g_free (converted);
}

static void
test_take_convert_ucs2_bad_ascii2 (void *f, gpointer d)
{
    gchar *src, *converted;

    /* Ensure something completely screwed up doesn't crash */
    src = g_strdup ("\241\255\254\250\244\234");
    converted = mm_charset_take_and_convert_to_utf8 (src, MM_MODEM_CHARSET_UCS2);
    g_assert (converted == NULL);
}

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

typedef GTestFixtureFunc TCFunc;

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

int main (int argc, char **argv)
{
    GTestSuite *suite;
    gint result;

    g_test_init (&argc, &argv, NULL);

    suite = g_test_get_root ();

    g_test_suite_add (suite, TESTCASE (test_def_chars, NULL));
    g_test_suite_add (suite, TESTCASE (test_esc_chars, NULL));
    g_test_suite_add (suite, TESTCASE (test_mixed_chars, NULL));

    g_test_suite_add (suite, TESTCASE (test_unpack_gsm7, NULL));
    g_test_suite_add (suite, TESTCASE (test_unpack_gsm7_7_chars, NULL));
    g_test_suite_add (suite, TESTCASE (test_unpack_gsm7_all_chars, NULL));

    g_test_suite_add (suite, TESTCASE (test_pack_gsm7, NULL));
    g_test_suite_add (suite, TESTCASE (test_pack_gsm7_7_chars, NULL));
    g_test_suite_add (suite, TESTCASE (test_pack_gsm7_all_chars, NULL));
    g_test_suite_add (suite, TESTCASE (test_pack_gsm7_24_chars, NULL));
    g_test_suite_add (suite, TESTCASE (test_pack_gsm7_last_septet_alone, NULL));

    g_test_suite_add (suite, TESTCASE (test_pack_gsm7_7_chars_offset, NULL));

    g_test_suite_add (suite, TESTCASE (test_take_convert_ucs2_hex_utf8, NULL));
    g_test_suite_add (suite, TESTCASE (test_take_convert_ucs2_bad_ascii, NULL));
    g_test_suite_add (suite, TESTCASE (test_take_convert_ucs2_bad_ascii2, NULL));

    result = g_test_run ();

    return result;
}
