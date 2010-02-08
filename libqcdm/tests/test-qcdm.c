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

typedef void (*TCFunc)(void);

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

int main (int argc, char **argv)
{
    GTestSuite *suite;

    g_test_init (&argc, &argv, NULL);

    suite = g_test_get_root ();

    g_test_suite_add (suite, TESTCASE (test_crc16_1, NULL));
    g_test_suite_add (suite, TESTCASE (test_crc16_2, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape1, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape2, NULL));
    g_test_suite_add (suite, TESTCASE (test_escape_unescape, NULL));

    return g_test_run ();
}

