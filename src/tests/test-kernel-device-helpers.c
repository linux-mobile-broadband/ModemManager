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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-kernel-device-helpers.h"
#include "mm-log-test.h"

/*****************************************************************************/

typedef struct {
    const gchar *pattern;
    const gchar *str;
    gboolean     match;
} StringMatchTest;

static const StringMatchTest string_match_tests[] = {
    {
        .pattern = "/sys/devices/pci0000:00/0000:00:1f.6/net/eno1",
        .str     = "/sys/devices/pci0000:00/0000:00:1f.6/net/eno1",
        .match   = TRUE,
    },
    {
        .pattern = "/sys/devices/pci0000:00/0000:00:1f.6/net/*",
        .str     = "/sys/devices/pci0000:00/0000:00:1f.6/net/eno1",
        .match   = TRUE,
    },
    {
        .pattern = "*MBIM",
        .str     = "wwan0p1MBIM",
        .match   = TRUE,
    },
    /* Don't match leading extra characters */
    {
        .pattern = "/sys/devices/pci0000:00/0000:00:1f.6/net",
        .str     = "aaaaa/sys/devices/pci0000:00/0000:00:1f.6/net",
        .match   = FALSE,
    },
    /* Don't match trailing extra characters */
    {
        .pattern = "/sys/devices/pci0000:00/0000:00:1f.6/net",
        .str     = "/sys/devices/pci0000:00/0000:00:1f.6/netaaa",
        .match   = FALSE,
    },
    /* The ASTERISK in a pattern given must be treated as a wildcard by
     * itself, not as "the previous character 0-N times", as the input
     * pattern is not a regex pattern. */
    {
        .pattern = "/sys/devices/pci0000:00/0000:00:1f.6/net/*",
        .str     = "/sys/devices/pci0000:00/0000:00:1ff6/net",
        .match   = FALSE,
    },
    {
        .pattern = "/sys/devices/pci0000:00/0000:00:1f.6/net/*",
        .str     = "/sys/devices/pci0000:00/0000:00:1f.6/netaaa",
        .match   = FALSE,
    },
    /* The DOT in a pattern given must match the exact DOT character,
     * as the input pattern is not a regex pattern. */
    {
        .pattern = "/sys/devices/pci0000:00/0000:00:1f.6/net/eno1",
        .str     = "/sys/devices/pci0000:00/0000:00:1ff6/net/eno1",
        .match   = FALSE,
    },
};

static void
test_string_match (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (string_match_tests); i++) {
        gboolean match;

        match = mm_kernel_device_generic_string_match (string_match_tests[i].str,
                                                       string_match_tests[i].pattern,
                                                       NULL);
        if (match != string_match_tests[i].match)
            mm_obj_warn (NULL, "string match failure: pattern '%s' should%s match str '%s'",
                         string_match_tests[i].pattern,
                         string_match_tests[i].match ? "" : " NOT",
                         string_match_tests[i].str);

        g_assert_cmpuint (match, ==, string_match_tests[i].match);
    }
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/kernel-device-helpers/string-match", test_string_match);

    return g_test_run ();
}
