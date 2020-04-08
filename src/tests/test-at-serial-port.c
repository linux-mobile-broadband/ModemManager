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
#include "mm-log-test.h"

typedef struct {
    const gchar *original;
    const gchar *without_echo;
} EchoRemovalTest;

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

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/ModemManager/AT-serial/echo-removal", at_serial_echo_removal);

    return g_test_run ();
}
