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

#include "test-qcdm-crc.h"
#include "test-qcdm-escaping.h"
#include "test-qcdm-com.h"
#include "test-qcdm-result.h"
#include "test-qcdm-utils.h"

typedef struct {
    gpointer com_data;
} TestData;

typedef GTestFixtureFunc TCFunc;

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

static TestData *
test_data_new (const char *port)
{
    TestData *d;

    d = g_malloc0 (sizeof (TestData));
    g_assert (d);

    if (port)
        d->com_data = test_com_setup (port);

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

    g_test_init (&argc, &argv, NULL);

    /* See if we got passed a serial port for live testing */
    for (i = 0; i < argc; i++) {
        if (!strcmp (argv[i], "--port")) {
            /* Make sure there's actually a port in the next arg */
            g_assert (argc > i + 1);
            port = argv[++i];
        }
    }

    data = test_data_new (port);

    suite = g_test_get_root ();

    g_test_suite_add (suite, TESTCASE (test_crc16_1, NULL));
    g_test_suite_add (suite, TESTCASE (test_crc16_2, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape1, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape2, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape_unescape, NULL));
    g_test_suite_add (suite, TESTCASE (test_utils_decapsulate_buffer, NULL));
    g_test_suite_add (suite, TESTCASE (test_utils_encapsulate_buffer, NULL));
    g_test_suite_add (suite, TESTCASE (test_utils_decapsulate_sierra_cns, NULL));
    g_test_suite_add (suite, TESTCASE (test_result_string, NULL));
    g_test_suite_add (suite, TESTCASE (test_result_uint32, NULL));
    g_test_suite_add (suite, TESTCASE (test_result_uint8, NULL));
    g_test_suite_add (suite, TESTCASE (test_result_uint8_array, NULL));

    /* Live tests */
    if (port) {
        g_test_suite_add (suite, TESTCASE (test_com_port_init, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_version_info, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_esn, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_mdn, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_read_roam_pref, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_read_mode_pref, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_read_hybrid_pref, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_read_ipv6_enabled, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_read_hdr_rev_pref, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_status, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_sw_version, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_status_snapshot, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_pilot_sets, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_cm_subsys_state_info, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_hdr_subsys_state_info, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_ext_logmask, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_event_report, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_log_config, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_zte_subsys_status, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_nw_subsys_modem_snapshot_cdma, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_nw_subsys_eri, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_wcdma_subsys_state_info, data->com_data));
        g_test_suite_add (suite, TESTCASE (test_com_gsm_subsys_state_info, data->com_data));
    }

    result = g_test_run ();

    test_data_free (data);

    return result;
}
