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
 * Copyright (C) 2016 Thomas Sailer <t.sailer@alumni.ethz.ch>
 *
 */
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-modem-helpers.h"
#include "mm-modem-helpers-thuraya.h"
#include "mm-log-test.h"

/*****************************************************************************/
/* Test CPMS response */

static gboolean
is_storage_supported (GArray *supported,
                      MMSmsStorage storage)
{
    guint i;

    for (i = 0; i < supported->len; i++) {
        if (storage == g_array_index (supported, MMSmsStorage, i))
            return TRUE;
    }

    return FALSE;
}

static void
test_cpms_response_thuraya (void *f, gpointer d)
{
    /*
     * First:    ("ME","MT")  2-item group
     * Second:   "ME"         1 item
     * Third:    ("SM")       1-item group
     */
    const gchar *reply = "+CPMS: \"MT\",\"SM\",\"BM\",\"ME\",\"SR\", \"MT\",\"SM\",\"BM\",\"ME\",\"SR\", \"MT\",\"SM\",\"BM\",\"ME\",\"SR\" ";
    GArray *mem1 = NULL;
    GArray *mem2 = NULL;
    GArray *mem3 = NULL;
    GError *error = NULL;

    g_debug ("Testing thuraya +CPMS=? response...");

    g_assert (mm_thuraya_3gpp_parse_cpms_test_response (reply, &mem1, &mem2, &mem3, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (mem1->len, ==, 5);
    g_assert (is_storage_supported (mem1, MM_SMS_STORAGE_MT));
    g_assert (is_storage_supported (mem1, MM_SMS_STORAGE_SM));
    g_assert (is_storage_supported (mem1, MM_SMS_STORAGE_BM));
    g_assert (is_storage_supported (mem1, MM_SMS_STORAGE_ME));
    g_assert (is_storage_supported (mem1, MM_SMS_STORAGE_SR));
    g_assert_cmpuint (mem2->len, ==, 5);
    g_assert (is_storage_supported (mem2, MM_SMS_STORAGE_MT));
    g_assert (is_storage_supported (mem2, MM_SMS_STORAGE_SM));
    g_assert (is_storage_supported (mem2, MM_SMS_STORAGE_BM));
    g_assert (is_storage_supported (mem2, MM_SMS_STORAGE_ME));
    g_assert (is_storage_supported (mem2, MM_SMS_STORAGE_SR));
    g_assert_cmpuint (mem3->len, ==, 5);
    g_assert (is_storage_supported (mem3, MM_SMS_STORAGE_MT));
    g_assert (is_storage_supported (mem3, MM_SMS_STORAGE_SM));
    g_assert (is_storage_supported (mem3, MM_SMS_STORAGE_BM));
    g_assert (is_storage_supported (mem3, MM_SMS_STORAGE_ME));
    g_assert (is_storage_supported (mem3, MM_SMS_STORAGE_SR));

    g_array_unref (mem1);
    g_array_unref (mem2);
    g_array_unref (mem3);
}

/*****************************************************************************/

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (GTestFixtureFunc) t, NULL)

int main (int argc, char **argv)
{
    GTestSuite *suite;
    gint result;

    g_test_init (&argc, &argv, NULL);

    suite = g_test_get_root ();

    g_test_suite_add (suite, TESTCASE (test_cpms_response_thuraya,      NULL));

    result = g_test_run ();

    return result;
}
