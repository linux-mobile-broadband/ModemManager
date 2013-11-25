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

#ifndef TEST_FIXTURE_H
#define TEST_FIXTURE_H

#include <gio/gio.h>
#include <libmm-glib.h>
#include "mm-gdbus-test.h"

/*****************************************************************************/
/* Test fixture setup */

/* The fixture contains a GTestDBus object and
 * a proxy to the service we're going to be testing.
 */
typedef struct {
    GTestDBus *dbus;
    MmGdbusTest *test;
    GDBusConnection *connection;
} TestFixture;

void test_fixture_setup    (TestFixture *fixture);
void test_fixture_teardown (TestFixture *fixture);

typedef void (*TCFunc) (TestFixture *, gconstpointer);
#define TEST_ADD(path,method)                        \
    g_test_add (path,                                \
                TestFixture,                         \
                NULL,                                \
                (TCFunc)test_fixture_setup,          \
                (TCFunc)method,                      \
                (TCFunc)test_fixture_teardown)

void      test_fixture_set_profile (TestFixture *fixture,
                                    const gchar *profile_name,
                                    const gchar *plugin,
                                    const gchar *const *ports);
MMObject *test_fixture_get_modem   (TestFixture *fixture);
void      test_fixture_no_modem    (TestFixture *fixture);

#endif /* TEST_FIXTURE_H */
