/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <string.h>

#include "test-wmc-crc.h"
#include "test-wmc-escaping.h"
#include "test-wmc-utils.h"
#include "test-wmc-com.h"

typedef struct {
    gpointer com_data;
} TestData;

typedef GTestFixtureFunc TCFunc;

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

static TestData *
test_data_new (const char *port, gboolean uml290, gboolean debug)
{
    TestData *d;

    d = g_malloc0 (sizeof (TestData));
    g_assert (d);

    if (port)
        d->com_data = test_com_setup (port, uml290, debug);

    return d;
}

static void
test_data_free (TestData *d)
{
    if (d->com_data)
        test_com_teardown (d->com_data);

    g_free (d);
}

int main (int argc, char **argv)
{
    GTestSuite *suite;
    TestData *data;
    int i;
    const char *port = NULL;
    gint result;
    gboolean uml290 = FALSE, debug = FALSE;

    g_test_init (&argc, &argv, NULL);

    /* See if we got passed a serial port for live testing */
    for (i = 0; i < argc; i++) {
        if (!strcmp (argv[i], "--port")) {
            /* Make sure there's actually a port in the next arg */
            g_assert (argc > i + 1);
            port = argv[++i];
        } else if (!strcmp (argv[i], "--uml290"))
            uml290 = TRUE;
        else if (!strcmp (argv[i], "--debug"))
            debug = TRUE;
    }

    data = test_data_new (port, uml290, debug);

    suite = g_test_get_root ();
    g_test_suite_add (suite, TESTCASE (test_crc16_1, NULL));
    g_test_suite_add (suite, TESTCASE (test_crc16_2, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape1, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape2, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape_ctrl, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape_unescape, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape_unescape_ctrl, NULL));
    g_test_suite_add (suite, TESTCASE (test_utils_decapsulate_basic_buffer, NULL));
    g_test_suite_add (suite, TESTCASE (test_utils_encapsulate_basic_buffer, NULL));
    g_test_suite_add (suite, TESTCASE (test_utils_decapsulate_sierra_cns, NULL));
    g_test_suite_add (suite, TESTCASE (test_utils_decapsulate_uml290_wmc1, NULL));
    g_test_suite_add (suite, TESTCASE (test_utils_decapsulate_pc5740_wmc1, NULL));

    /* Live tests */
    if (port) {
        g_test_suite_add (suite, TESTCASE (test_com_port_init, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_init, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_device_info, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_network_info, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_get_global_mode, data->com_data));
    }

    result = g_test_run ();

    test_data_free (data);

    return result;
}
