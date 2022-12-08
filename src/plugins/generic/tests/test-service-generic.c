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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@gnu.org>
 */

#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>

#include <libmm-glib.h>

#include "test-port-context.h"
#include "test-fixture.h"

/*****************************************************************************/

static void
test_enable_disable (TestFixture *fixture)
{
    GError *error = NULL;
    MMObject *obj;
    MMModem *modem;
    TestPortContext *port0;
    gchar *ports [] = { NULL, NULL };

    /* Create port name, and add process ID so that multiple runs of this test
     * in the same system don't clash with each other */
    ports[0] = g_strdup_printf ("abstract:port0:%ld", (glong) getpid ());
    g_debug ("test service generic: using abstract port at '%s'", ports[0]);

    /* Setup new port context */
    port0 = test_port_context_new (ports[0]);
    test_port_context_load_commands (port0, COMMON_GSM_PORT_CONF);
    test_port_context_start (port0);

    /* Ensure no modem is modem exported */
    test_fixture_no_modem (fixture);

    /* Set the test profile */
    test_fixture_set_profile (fixture,
                              "test-enable-disable",
                              "generic",
                              (const gchar *const *)ports);

    /* Wait and get the modem object */
    obj = test_fixture_get_modem (fixture);

    /* Get Modem interface, and enable */
    modem = mm_object_get_modem (obj);
    g_assert (modem != NULL);
    mm_modem_enable_sync (modem, NULL, &error);
    g_assert_no_error (error);

    /* And disable */
    mm_modem_disable_sync (modem, NULL, &error);
    g_assert_no_error (error);

    g_object_unref (modem);
    g_object_unref (obj);

    /* Stop port context */
    test_port_context_stop (port0);
    test_port_context_free (port0);

    g_free (ports[0]);
}

/*****************************************************************************/

int main (int   argc,
          char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    TEST_ADD ("/MM/Service/Generic/enable-disable", test_enable_disable);

    return g_test_run ();
}
