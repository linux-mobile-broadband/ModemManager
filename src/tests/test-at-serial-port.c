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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>
#include <string.h>
#include <glib.h>

#include "mm-port-serial-at.h"
#include "mm-serial-parsers.h"
#include "mm-log-test.h"

typedef struct {
    const gchar *original;
    const gchar *without_echo;
} EchoRemovalTest;

typedef struct {
    const gchar *response;
    const gboolean found;
} ParseOkTest;

static const EchoRemovalTest echo_removal_tests[] = {
    { "\r\n", "\r\n" },
    { "\r", "\r" },
    { "\n", "\n" },
    { "this is a string that ends just with <CR>\r", "this is a string that ends just with <CR>\r" },
    { "this is a string that ends just with <CR>\n", "this is a string that ends just with <CR>\n" },
    { "\r\nthis is valid", "\r\nthis is valid" },
    { "a\r\nthis is valid", "\r\nthis is valid" },
    { "a\r\n", "\r\n" },
    { "all this string is to be considered echo\r\n", "\r\n" },
    { "all this string is to be considered echo\r\nthis is valid", "\r\nthis is valid" },
    { "echo echo\r\nthis is valid\r\nand so is this", "\r\nthis is valid\r\nand so is this" },
    { "\r\nthis is valid\r\nand so is this", "\r\nthis is valid\r\nand so is this" },
    { "\r\nthis is valid\r\nand so is this\r\n", "\r\nthis is valid\r\nand so is this\r\n" },
};

static const ParseOkTest parse_ok_tests[] = {
    { "\r\nOK\r\n", TRUE},
    { "\r\nOK\r\n\r\n+CMTI: \"ME\",1\r\n", TRUE},
    { "\r\nOK\r\n\r\n+CIEV: 7,1\r\n\r\n+CRING: VOICE\r\n\r\n+CLIP: \"+0123456789\",145,,,,0\r\n", TRUE},
    { "\r\nERROR\r\n", FALSE}
};

static void
at_serial_echo_removal (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (echo_removal_tests); i++) {
        GByteArray *ba;

        /* Note that we add last NUL also to the byte array, so that we can compare
         * C strings later on */
        ba = g_byte_array_sized_new (strlen (echo_removal_tests[i].original) + 1);
        g_byte_array_prepend (ba,
                              (guint8 *)echo_removal_tests[i].original,
                              strlen (echo_removal_tests[i].original) + 1);

        mm_port_serial_at_remove_echo (ba);

        g_assert_cmpstr ((gchar *)ba->data, ==, echo_removal_tests[i].without_echo);

        g_byte_array_unref (ba);
    }
}

static void
at_serial_parse_ok (void)
{
    guint i;
    gpointer parser;
    GError *error = NULL;
    gboolean found = FALSE;
    GString *response;

    for (i = 0; i < G_N_ELEMENTS (parse_ok_tests); i++) {
        parser = mm_serial_parser_v1_new ();
        response = g_string_new (parse_ok_tests[i].response);
        found = mm_serial_parser_v1_parse (parser, response, NULL, &error);

        /* Match found */
        if (parse_ok_tests[i].found) {
            g_assert_cmpint (found, ==, parse_ok_tests[i].found);
            g_assert_no_error (error);
        }
        /* Not found: error */
        else {
            g_assert (error != NULL);
        }

        g_string_free (response, TRUE);
    }
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/ModemManager/AT-serial/echo-removal", at_serial_echo_removal);
    g_test_add_func ("/ModemManager/AT-serial/parse-ok", at_serial_parse_ok);

    return g_test_run ();
}
