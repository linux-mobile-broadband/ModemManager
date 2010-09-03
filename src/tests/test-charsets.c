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


#if GLIB_CHECK_VERSION(2,25,12)
typedef GTestFixtureFunc TCFunc;
#else
typedef void (*TCFunc)(void);
#endif

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

    result = g_test_run ();

    return result;
}

