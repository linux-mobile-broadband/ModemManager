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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include "mm-modem-helpers-huawei.h"

/*****************************************************************************/
/* Test ^NDISSTATQRY responses */

typedef struct {
    const gchar *str;
    gboolean expected_ipv4_available;
    gboolean expected_ipv4_connected;
    gboolean expected_ipv6_available;
    gboolean expected_ipv6_connected;
} NdisstatqryTest;

static const NdisstatqryTest ndisstatqry_tests[] = {
    { "^NDISSTATQRY: 1,,,IPV4\r\n", TRUE,  TRUE,  FALSE, FALSE },
    { "^NDISSTATQRY: 0,,,IPV4\r\n", TRUE,  FALSE, FALSE, FALSE },
    { "^NDISSTATQRY: 1,,,IPV6\r\n", FALSE, FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,IPV6\r\n", FALSE, FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,IPV4\r\n"
      "^NDISSTATQRY: 1,,,IPV6\r\n", TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTATQRY: 1,,,IPV4\r\n"
      "^NDISSTATQRY: 0,,,IPV6\r\n", TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTATQRY: 0,,,IPV4\r\n"
      "^NDISSTATQRY: 1,,,IPV6\r\n", TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,IPV4\r\n"
      "^NDISSTATQRY: 0,,,IPV6\r\n", TRUE,  FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,IPV4",     TRUE,  TRUE,  FALSE, FALSE },
    { "^NDISSTATQRY: 0,,,IPV4",     TRUE,  FALSE, FALSE, FALSE },
    { "^NDISSTATQRY: 1,,,IPV6",     FALSE, FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,IPV6",     FALSE, FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,IPV4\r\n"
      "^NDISSTATQRY: 1,,,IPV6",     TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTATQRY: 1,,,IPV4\r\n"
      "^NDISSTATQRY: 0,,,IPV6",     TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTATQRY: 0,,,IPV4\r\n"
      "^NDISSTATQRY: 1,,,IPV6",     TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,IPV4\r\n"
      "^NDISSTATQRY: 0,,,IPV6",     TRUE,  FALSE, TRUE,  FALSE },
    { NULL,                         FALSE, FALSE, FALSE, FALSE }
};

static void
test_ndisstatqry (void)
{
    guint i;

    for (i = 0; ndisstatqry_tests[i].str; i++) {
        GError *error = NULL;
        gboolean ipv4_available;
        gboolean ipv4_connected;
        gboolean ipv6_available;
        gboolean ipv6_connected;

        g_assert (mm_huawei_parse_ndisstatqry_response (
                      ndisstatqry_tests[i].str,
                      &ipv4_available,
                      &ipv4_connected,
                      &ipv6_available,
                      &ipv6_connected,
                      &error) == TRUE);
        g_assert_no_error (error);

        g_assert (ipv4_available == ndisstatqry_tests[i].expected_ipv4_available);
        if (ipv4_available)
            g_assert (ipv4_connected == ndisstatqry_tests[i].expected_ipv4_connected);
        g_assert (ipv6_available == ndisstatqry_tests[i].expected_ipv6_available);
        if (ipv6_available)
            g_assert (ipv6_connected == ndisstatqry_tests[i].expected_ipv6_connected);
    }
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/huawei/ndisstatqry", test_ndisstatqry);

    return g_test_run ();
}
