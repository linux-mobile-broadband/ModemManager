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
#include <locale.h>

#include "mm-modem-helpers.h"
#include "mm-log-test.h"

static void
common_test_gsm7 (const gchar *in_utf8)
{
    guint32 packed_gsm_len = 0;
    guint32 unpacked_gsm_len_2 = 0;
    g_autoptr(GByteArray) unpacked_gsm = NULL;
    g_autofree guint8 *packed_gsm = NULL;
    guint8 *unpacked_gsm_2 = NULL;
    g_autoptr(GByteArray) unpacked_gsm_2_array = NULL;
    g_autofree gchar *built_utf8 = NULL;
    g_autoptr(GError) error = NULL;

    /* Convert to GSM */
    unpacked_gsm = mm_modem_charset_bytearray_from_utf8 (in_utf8, MM_MODEM_CHARSET_GSM, FALSE, &error);
    g_assert_nonnull (unpacked_gsm);
    g_assert_no_error (error);

    /* Pack */
    packed_gsm = mm_charset_gsm_pack (unpacked_gsm->data, unpacked_gsm->len, 0, &packed_gsm_len);
    g_assert_nonnull (packed_gsm);
    g_assert_cmpuint (packed_gsm_len, <=, unpacked_gsm->len);

#if 0
    {
        g_autofree gchar *hex_packed = NULL;

        /* Print */
        hex_packed = mm_utils_bin2hexstr (packed_gsm, packed_gsm_len);
        g_print ("----------\n");
        g_print ("input string: '%s'\n", in_utf8);
        g_print ("gsm-7 encoded string: %s\n", hex_packed);
    }
#endif

    /* Unpack */
    unpacked_gsm_2 = mm_charset_gsm_unpack (packed_gsm, packed_gsm_len * 8 / 7, 0, &unpacked_gsm_len_2);
    g_assert_nonnull (unpacked_gsm_2);
    unpacked_gsm_2_array = g_byte_array_new_take (unpacked_gsm_2, unpacked_gsm_len_2);

    /* And back to UTF-8 */
    built_utf8 = mm_modem_charset_bytearray_to_utf8 (unpacked_gsm_2_array, MM_MODEM_CHARSET_GSM, FALSE, &error);
    g_assert_nonnull (built_utf8);
    g_assert_no_error (error);
    g_assert_cmpstr (built_utf8, ==, in_utf8);
}

static void
test_gsm7_default_chars (void)
{
    /* Test that a string with all the characters in the GSM 03.38 charset
     * are converted from UTF-8 to GSM and back to UTF-8 successfully.
     */
    static const char *s = "@£$¥èéùìòÇ\nØø\rÅåΔ_ΦΓΛΩΠΨΣΘΞÆæßÉ !\"#¤%&'()*+,-./0123456789:;<=>?¡ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÑÜ§¿abcdefghijklmnopqrstuvwxyzäöñüà";

    common_test_gsm7 (s);
}

static void
test_gsm7_extended_chars (void)
{
    /* Test that a string with all the characters in the extended GSM 03.38
     * charset are converted from UTF-8 to GSM and back to UTF-8 successfully.
     */
    static const char *s = "\f^{}\\[~]|€";

    common_test_gsm7 (s);
}

static void
test_gsm7_mixed_chars (void)
{
    /* Test that a string with a mix of GSM 03.38 default and extended characters
     * is converted from UTF-8 to GSM and back to UTF-8 successfully.
     */
    static const char *s = "@£$¥èéùìø\fΩΠΨΣΘ{ΞÆæß(})789\\:;<=>[?¡QRS]TUÖ|ÑÜ§¿abpqrstuvöñüà€";

    common_test_gsm7 (s);
}

static void
test_gsm7_unpack_basic (void)
{
    static const guint8 gsm[] = { 0xC8, 0xF7, 0x1D, 0x14, 0x96, 0x97, 0x41, 0xF9, 0x77, 0xFD, 0x07 };
    static const guint8 expected[] = { 0x48, 0x6f, 0x77, 0x20, 0x61, 0x72, 0x65, 0x20, 0x79, 0x6f, 0x75, 0x3f };
    guint8 *unpacked;
    guint32 unpacked_len = 0;

    unpacked = mm_charset_gsm_unpack (gsm, (sizeof (gsm) * 8) / 7, 0, &unpacked_len);
    g_assert (unpacked);
    g_assert_cmpint (unpacked_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (unpacked, expected, unpacked_len), ==, 0);

    g_free (unpacked);
}

static void
test_gsm7_unpack_7_chars (void)
{
    static const guint8 gsm[] = { 0xF1, 0x7B, 0x59, 0x4E, 0xCF, 0xD7, 0x01 };
    static const guint8 expected[] = { 0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75};
    guint8 *unpacked;
    guint32 unpacked_len = 0;

    /* Tests the edge case where there are 7 bits left in the packed
     * buffer but those 7 bits do not contain a character.  In this case
     * we expect to get the number of characters that were specified.
     */

    unpacked = mm_charset_gsm_unpack (gsm, 7 , 0, &unpacked_len);
    g_assert (unpacked);
    g_assert_cmpint (unpacked_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (unpacked, expected, unpacked_len), ==, 0);

    g_free (unpacked);
}

static void
test_gsm7_unpack_all_chars (void)
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

    unpacked = mm_charset_gsm_unpack (gsm, (sizeof (gsm) * 8) / 7, 0, &unpacked_len);
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
test_gsm7_pack_basic (void)
{
    static const guint8 unpacked[] = { 0x48, 0x6f, 0x77, 0x20, 0x61, 0x72, 0x65, 0x20, 0x79, 0x6f, 0x75, 0x3f };
    static const guint8 expected[] = { 0xC8, 0xF7, 0x1D, 0x14, 0x96, 0x97, 0x41, 0xF9, 0x77, 0xFD, 0x07 };
    guint8 *packed;
    guint32 packed_len = 0;

    packed = mm_charset_gsm_pack (unpacked, sizeof (unpacked), 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (packed, expected, packed_len), ==, 0);

    g_free (packed);
}

static void
test_gsm7_pack_7_chars (void)
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

    packed = mm_charset_gsm_pack (unpacked, sizeof (unpacked), 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (packed, expected, packed_len), ==, 0);

    g_free (packed);
}

static void
test_gsm7_pack_all_chars (void)
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

    packed = mm_charset_gsm_pack (unpacked->data, unpacked->len, 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (packed, expected, packed_len), ==, 0);

    g_free (packed);
    g_byte_array_free (unpacked, TRUE);
}

static void
test_gsm7_pack_24_chars (void)
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

    packed = mm_charset_gsm_pack (unpacked, sizeof (unpacked), 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, 21);

    g_free (packed);
}

static void
test_gsm7_pack_last_septet_alone (void)
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

    packed = mm_charset_gsm_pack (unpacked, sizeof (unpacked), 0, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));

    g_free (packed);
}

static void
test_gsm7_pack_7_chars_offset (void)
{
    static const guint8 unpacked[] = { 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x10, 0x2F };
    static const guint8 expected[] = { 0x00, 0x5D, 0x66, 0xB3, 0xDF, 0x90, 0x17 };
    guint8 *packed;
    guint32 packed_len = 0;

    packed = mm_charset_gsm_pack (unpacked, sizeof (unpacked), 5, &packed_len);
    g_assert (packed);
    g_assert_cmpint (packed_len, ==, sizeof (expected));
    g_assert_cmpint (memcmp (packed, expected, packed_len), ==, 0);

    g_free (packed);
}

static void
test_str_ucs2_to_from_utf8 (void)
{
    const gchar       *src = "0054002D004D006F00620069006C0065";
    g_autofree gchar  *utf8 = NULL;
    g_autofree gchar  *dst = NULL;
    g_autoptr(GError)  error = NULL;

    utf8 = mm_modem_charset_str_to_utf8 (src, -1, MM_MODEM_CHARSET_UCS2, FALSE, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (utf8, ==, "T-Mobile");

    dst = mm_modem_charset_str_from_utf8 (utf8, MM_MODEM_CHARSET_UCS2, FALSE, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (dst, ==, src);
}

static void
test_str_gsm_to_from_utf8 (void)
{
    const gchar       *src = "T-Mobile";
    g_autofree gchar  *utf8 = NULL;
    g_autofree gchar  *dst = NULL;
    g_autoptr(GError)  error = NULL;

    /* Note: as long as the GSM string doesn't contain the '@' character, str_to_utf8()
     * and str_from_utf8() can safely be used */

    utf8 = mm_modem_charset_str_to_utf8 (src, -1, MM_MODEM_CHARSET_GSM, FALSE, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (utf8, ==, src);

    dst = mm_modem_charset_str_from_utf8 (utf8, MM_MODEM_CHARSET_GSM, FALSE, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (dst, ==, src);
}

static void
test_str_gsm_to_from_utf8_with_at (void)
{
    /* The NULs are '@' chars, except for the trailing one which is always taken as end-of-string */
    const gchar        src[] = { 'T', '-', 'M', 0x00, 'o', 'b', 'i', 0x00, 'l', 'e', 0x00 };
    const gchar       *utf8_expected = "T-M@obi@le";
    const gchar       *src_translit = "T-M?obi?le";
    g_autofree gchar  *utf8 = NULL;
    g_autofree gchar  *dst = NULL;
    g_autoptr(GError)  error = NULL;

    /* Note: as long as the GSM string doesn't contain the '@' character, str_to_utf8()
     * and str_from_utf8() can safely be used */

    utf8 = mm_modem_charset_str_to_utf8 (src, G_N_ELEMENTS (src), MM_MODEM_CHARSET_GSM, FALSE, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (utf8, ==, utf8_expected);

    /* if charset conversion from UTF-8 contains '@' chars, running without transliteration
     * will return an error */
    dst = mm_modem_charset_str_from_utf8 (utf8, MM_MODEM_CHARSET_GSM, FALSE, &error);
    g_assert_nonnull (error);
    g_assert_null (dst);
    g_clear_error (&error);

    /* with transliteration, '@'->'?' */
    dst = mm_modem_charset_str_from_utf8 (utf8, MM_MODEM_CHARSET_GSM, TRUE, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (dst, ==, src_translit);
}

struct charset_can_convert_to_test_s {
    const char *utf8;
    gboolean    to_gsm;
    gboolean    to_ira;
    gboolean    to_8859_1;
    gboolean    to_ucs2;
    gboolean    to_utf16;
    gboolean    to_pccp437;
    gboolean    to_pcdn;
};

static void
test_charset_can_covert_to (void)
{
    static const struct charset_can_convert_to_test_s charset_can_convert_to_test[] = {
        {
            .utf8 = "",
            .to_gsm = TRUE, .to_ira = TRUE, .to_8859_1 = TRUE, .to_ucs2 = TRUE, .to_utf16 = TRUE, .to_pccp437 = TRUE, .to_pcdn = TRUE,
        },
        {
            .utf8 = " ",
            .to_gsm = TRUE, .to_ira = TRUE, .to_8859_1 = TRUE, .to_ucs2 = TRUE, .to_utf16 = TRUE, .to_pccp437 = TRUE, .to_pcdn = TRUE,
        },
        {
            .utf8 = "some basic ascii",
            .to_gsm = TRUE, .to_ira = TRUE, .to_8859_1 = TRUE, .to_ucs2 = TRUE, .to_utf16 = TRUE, .to_pccp437 = TRUE, .to_pcdn = TRUE,
        },
        {
            .utf8 = "ホモ・サピエンス 喂人类 katakana, chinese, english: UCS2 takes it all",
            .to_gsm = FALSE, .to_ira = FALSE, .to_8859_1 = FALSE, .to_ucs2 = TRUE, .to_utf16 = TRUE, .to_pccp437 = FALSE, .to_pcdn = FALSE,
        },
        {
            .utf8 = "Some from the GSM7 basic set: a % Ψ Ω ñ ö è æ",
            .to_gsm = TRUE, .to_ira = FALSE, .to_8859_1 = FALSE, .to_ucs2 = TRUE, .to_utf16 = TRUE, .to_pccp437 = FALSE, .to_pcdn = FALSE,
        },
        {
            .utf8 = "More from the GSM7 extended set: {} [] ~ € |",
            .to_gsm = TRUE, .to_ira = FALSE, .to_8859_1 = FALSE, .to_ucs2 = TRUE, .to_utf16 = TRUE, .to_pccp437 = FALSE, .to_pcdn = FALSE,
        },
        {
            .utf8 = "patín cannot be encoded in GSM7 or IRA, but is valid UCS2, ISO-8859-1, CP437 and CP850",
            .to_gsm = FALSE, .to_ira = FALSE, .to_8859_1 = TRUE, .to_ucs2 = TRUE, .to_utf16 = TRUE, .to_pccp437 = TRUE, .to_pcdn = TRUE,
        },
        {
            .utf8 = "ècole can be encoded in multiple ways, but not in IRA",
            .to_gsm = TRUE, .to_ira = FALSE, .to_8859_1 = TRUE, .to_ucs2 = TRUE, .to_utf16 = TRUE, .to_pccp437 = TRUE, .to_pcdn = TRUE,
        },
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (charset_can_convert_to_test); i++) {
        g_debug ("testing charset conversion: '%s'", charset_can_convert_to_test[i].utf8);
        g_assert (mm_charset_can_convert_to (charset_can_convert_to_test[i].utf8, MM_MODEM_CHARSET_GSM)     == charset_can_convert_to_test[i].to_gsm);
        g_assert (mm_charset_can_convert_to (charset_can_convert_to_test[i].utf8, MM_MODEM_CHARSET_IRA)     == charset_can_convert_to_test[i].to_ira);
        g_assert (mm_charset_can_convert_to (charset_can_convert_to_test[i].utf8, MM_MODEM_CHARSET_8859_1)  == charset_can_convert_to_test[i].to_8859_1);
        g_assert (mm_charset_can_convert_to (charset_can_convert_to_test[i].utf8, MM_MODEM_CHARSET_UCS2)    == charset_can_convert_to_test[i].to_ucs2);
        g_assert (mm_charset_can_convert_to (charset_can_convert_to_test[i].utf8, MM_MODEM_CHARSET_UTF16)   == charset_can_convert_to_test[i].to_utf16);
        g_assert (mm_charset_can_convert_to (charset_can_convert_to_test[i].utf8, MM_MODEM_CHARSET_PCCP437) == charset_can_convert_to_test[i].to_pccp437);
        g_assert (mm_charset_can_convert_to (charset_can_convert_to_test[i].utf8, MM_MODEM_CHARSET_PCDN)    == charset_can_convert_to_test[i].to_pcdn);
    }
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/charsets/gsm7/default-chars",          test_gsm7_default_chars);
    g_test_add_func ("/MM/charsets/gsm7/extended-chars",         test_gsm7_extended_chars);
    g_test_add_func ("/MM/charsets/gsm7/mixed-chars",            test_gsm7_mixed_chars);
    g_test_add_func ("/MM/charsets/gsm7/unpack/basic",           test_gsm7_unpack_basic);
    g_test_add_func ("/MM/charsets/gsm7/unpack/7-chars",         test_gsm7_unpack_7_chars);
    g_test_add_func ("/MM/charsets/gsm7/unpack/all-chars",       test_gsm7_unpack_all_chars);
    g_test_add_func ("/MM/charsets/gsm7/pack/basic",             test_gsm7_pack_basic);
    g_test_add_func ("/MM/charsets/gsm7/pack/7-chars",           test_gsm7_pack_7_chars);
    g_test_add_func ("/MM/charsets/gsm7/pack/all-chars",         test_gsm7_pack_all_chars);
    g_test_add_func ("/MM/charsets/gsm7/pack/24-chars",          test_gsm7_pack_24_chars);
    g_test_add_func ("/MM/charsets/gsm7/pack/last-septet-alone", test_gsm7_pack_last_septet_alone);
    g_test_add_func ("/MM/charsets/gsm7/pack/7-chars-offset",    test_gsm7_pack_7_chars_offset);

    g_test_add_func ("/MM/charsets/str-from-to/ucs2",         test_str_ucs2_to_from_utf8);
    g_test_add_func ("/MM/charsets/str-from-to/gsm",          test_str_gsm_to_from_utf8);
    g_test_add_func ("/MM/charsets/str-from-to/gsm-with-at",  test_str_gsm_to_from_utf8_with_at);

    g_test_add_func ("/MM/charsets/can-convert-to", test_charset_can_covert_to);

    return g_test_run ();
}
