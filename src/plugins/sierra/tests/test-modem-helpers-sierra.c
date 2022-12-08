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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-test.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-sierra.h"

/*****************************************************************************/
/* Test !SCACT?  responses */

static void
test_scact_read_results (const gchar *desc,
                         const gchar *reply,
                         MM3gppPdpContextActive *expected_results,
                         guint32 expected_results_len)
{
    GList *l;
    GError *error = NULL;
    GList *results;

    g_debug ("\nTesting %s !SCACT response...\n", desc);

    results = mm_sierra_parse_scact_read_response (reply, &error);
    g_assert_no_error (error);
    if (expected_results_len) {
        g_assert (results);
        g_assert_cmpuint (g_list_length (results), ==, expected_results_len);
    }

    for (l = results; l; l = g_list_next (l)) {
        MM3gppPdpContextActive *pdp = l->data;
        gboolean found = FALSE;
        guint i;

        for (i = 0; !found && i < expected_results_len; i++) {
            MM3gppPdpContextActive *expected;

            expected = &expected_results[i];
            if (pdp->cid == expected->cid) {
                found = TRUE;
                g_assert_cmpuint (pdp->active, ==, expected->active);
            }
        }

        g_assert (found == TRUE);
    }

    mm_3gpp_pdp_context_active_list_free (results);
}

static void
test_scact_read_response_none (void)
{
    test_scact_read_results ("none", "", NULL, 0);
}

static void
test_scact_read_response_single_inactive (void)
{
    const gchar *reply = "!SCACT: 1,0\r\n";
    static MM3gppPdpContextActive expected[] = {
        { 1, FALSE },
    };

    test_scact_read_results ("single inactive", reply, &expected[0], G_N_ELEMENTS (expected));
}

static void
test_scact_read_response_single_active (void)
{
    const gchar *reply = "!SCACT: 1,1\r\n";
    static MM3gppPdpContextActive expected[] = {
        { 1, TRUE },
    };

    test_scact_read_results ("single active", reply, &expected[0], G_N_ELEMENTS (expected));
}

static void
test_scact_read_response_multiple (void)
{
    const gchar *reply =
        "!SCACT: 1,0\r\n"
        "!SCACT: 4,1\r\n"
        "!SCACT: 5,0\r\n";
    static MM3gppPdpContextActive expected[] = {
        { 1, FALSE },
        { 4, TRUE },
        { 5, FALSE },
    };

    test_scact_read_results ("multiple", reply, &expected[0], G_N_ELEMENTS (expected));
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/sierra/scact/read/none",            test_scact_read_response_none);
    g_test_add_func ("/MM/sierra/scact/read/single-inactive", test_scact_read_response_single_inactive);
    g_test_add_func ("/MM/sierra/scact/read/single-active",   test_scact_read_response_single_active);
    g_test_add_func ("/MM/sierra/scact/read/multiple",        test_scact_read_response_multiple);

    return g_test_run ();
}
