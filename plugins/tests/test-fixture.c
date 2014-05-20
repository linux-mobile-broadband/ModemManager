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

#include "test-fixture.h"

void
test_fixture_setup (TestFixture *fixture)
{
    GError *error = NULL;
    GVariant *result;

    /* Create the global dbus-daemon for this test suite */
    fixture->dbus = g_test_dbus_new (G_TEST_DBUS_NONE);

    /* Add the private directory with our in-tree service files,
     * TEST_SERVICES is defined by the build system to point
     * to the right directory. */
    g_test_dbus_add_service_dir (fixture->dbus, TEST_SERVICES);

    /* Start the private DBus daemon */
    g_test_dbus_up (fixture->dbus);

    /* Create DBus connection */
    fixture->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    if (fixture->connection == NULL)
        g_error ("Error getting connection to test bus: %s", error->message);

    /* Ping to autostart MM; wait up to 3s */
    result = g_dbus_connection_call_sync (fixture->connection,
                                          "org.freedesktop.ModemManager1",
                                          "/org/freedesktop/ModemManager1",
                                          "org.freedesktop.DBus.Peer",
                                          "Ping",
                                          NULL, /* inputs */
                                          NULL, /* outputs */
                                          G_DBUS_CALL_FLAGS_NONE,
                                          3000, /* timeout, ms */
                                          NULL, /* cancellable */
                                          &error);
    if (!result)
        g_error ("Error starting ModemManager in test bus: %s", error->message);
    g_variant_unref (result);

    /* Create the proxy that we're going to test */
    fixture->test = mm_gdbus_test_proxy_new_sync (fixture->connection,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  "org.freedesktop.ModemManager1",
                                                  "/org/freedesktop/ModemManager1",
                                                  NULL, /* cancellable */
                                                  &error);
    if (fixture->test == NULL)
        g_error ("Error getting ModemManager test proxy: %s", error->message);
}

void
test_fixture_teardown (TestFixture *fixture)
{
    g_object_unref (fixture->connection);

    /* Tear down the proxy */
    if (fixture->test)
        g_object_unref (fixture->test);

    /* Stop the private D-Bus daemon; stopping the bus will stop MM as well */
    g_test_dbus_down (fixture->dbus);
    g_object_unref (fixture->dbus);
}

void
test_fixture_set_profile (TestFixture *fixture,
                          const gchar *profile_name,
                          const gchar *plugin,
                          const gchar *const *ports)
{
    GError *error = NULL;

    /* Set the test profile */
    g_assert (fixture->test != NULL);
    if (!mm_gdbus_test_call_set_profile_sync (fixture->test,
                                              profile_name,
                                              plugin,
                                              ports,
                                              NULL, /* cancellable */
                                              &error))
        g_error ("Error setting test profile: %s", error->message);
}

MMObject *
test_fixture_get_modem (TestFixture *fixture)
{
    GError *error = NULL;
    MMManager *manager;
    MMObject *found = NULL;
    guint wait_time = 0;

    /* Create manager */
    g_assert (fixture->connection != NULL);
    manager = mm_manager_new_sync (fixture->connection,
                                   G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                   NULL, /* cancellable */
                                   &error);
    if (!manager)
        g_error ("Couldn't create manager: %s", error->message);

    /* Find new modem object */
    while (!found) {
        GList *modems;
        guint n_modems;

        modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
        n_modems = g_list_length (modems);
        g_assert_cmpuint (n_modems, <=, 1);

        if (n_modems == 0) {
            /* Wait a bit before re-checking. We can do this kind of wait
             * because properties in the manager are updated in another
             * thread */
            g_assert_cmpuint (wait_time, <=, 20);
            wait_time++;
            sleep (1);
        } else
            found = MM_OBJECT (g_object_ref (modems->data));

        g_list_free_full (modems, (GDestroyNotify) g_object_unref);
    }

    g_message ("Found modem at '%s'", mm_object_get_path (found));

    g_object_unref (manager);

    return found;
}

void
test_fixture_no_modem (TestFixture *fixture)
{
    GError *error = NULL;
    MMManager *manager;
    guint wait_time = 0;
    gboolean no_modems = FALSE;

    /* Create manager */
    g_assert (fixture->connection != NULL);
    manager = mm_manager_new_sync (fixture->connection,
                                   G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                   NULL, /* cancellable */
                                   &error);
    if (!manager)
        g_error ("Couldn't create manager: %s", error->message);

    /* Find new modem object */
    while (!no_modems) {
        GList *modems;
        guint n_modems;

        modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
        n_modems = g_list_length (modems);
        g_assert_cmpuint (n_modems, <=, 1);

        if (n_modems == 1) {
            /* Wait a bit before re-checking. We can do this kind of wait
             * because properties in the manager are updated in another
             * thread */
            g_assert_cmpuint (wait_time, <=, 20);
            wait_time++;
            sleep (1);
        } else
            no_modems = TRUE;

        g_list_free_full (modems, (GDestroyNotify) g_object_unref);
    }

    g_object_unref (manager);
}
