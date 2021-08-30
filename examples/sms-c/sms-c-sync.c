/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libmm-glib.h>

static gboolean
loop_ready (GMainLoop *loop)
{
    g_main_loop_quit (loop);
    return G_SOURCE_REMOVE;
}

static void
send_sms (MMObject        *obj,
          MMSmsProperties *properties)
{
    g_autoptr(MMModemMessaging) messaging = NULL;
    g_autoptr(MMSms)            sms = NULL;
    g_autoptr(GError)           error = NULL;

    messaging = mm_object_get_modem_messaging (obj);
    if (!messaging) {
        g_printerr ("error: modem %s does not have messaging capabilities\n",
                    mm_object_get_path (obj));
        return;
    }

    sms = mm_modem_messaging_create_sync (messaging, properties, NULL, &error);
    if (!sms) {
        g_printerr ("error: couldn't create sms in modem %s: %s\n",
                    mm_object_get_path (obj),
                    error->message);
        return;
    }

    if (!mm_sms_send_sync (sms, NULL, &error)) {
        g_printerr ("error: couldn't send sms in modem %s: %s\n",
                    mm_object_get_path (obj),
                    error->message);
        return;
    }

    g_print ("successfully sent sms in modem %s\n",
             mm_object_get_path (obj));
}

int main (int argc, char **argv)
{
    g_autoptr(GDBusConnection)  connection = NULL;
    g_autoptr(MMManager)        manager = NULL;
    g_autoptr(GError)           error = NULL;
    g_autolist(MMObject)        objects = NULL;
    g_autoptr(MMSmsProperties)  properties = NULL;
    g_autofree gchar           *name_owner = NULL;

    if (argc < 3) {
        g_printerr ("error: missing arguments\n");
        g_printerr ("usage: %s <NUMBER> <TEXT>\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!connection) {
        g_printerr ("error: couldn't get bus: %s\n", error->message);
        exit (EXIT_FAILURE);
    }

    manager = mm_manager_new_sync (connection,
                                   G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                                   NULL,
                                   &error);
    if (!manager) {
        g_printerr ("error: couldn't get manager: %s\n", error->message);
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (manager));
    if (!name_owner) {
        g_printerr ("error: ModemManager not found in the system bus\n");
        exit (EXIT_FAILURE);
    }

    objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!objects) {
        g_printerr ("error: no modems found\n");
        exit (EXIT_FAILURE);
    }

    properties = mm_sms_properties_new ();
	mm_sms_properties_set_number (properties, argv[1]);
	mm_sms_properties_set_text   (properties, argv[2]);

    g_list_foreach (objects, (GFunc)send_sms, properties);

    g_clear_object (&connection);
    g_clear_object (&manager);
    g_list_free_full (g_steal_pointer (&objects), g_object_unref);

    /* This main loop is responsible for processing all async events that may
     * have been scheduled in the previous sync operations, e.g. when disposing
     * some of the GDBus related objects created before. Without this loop,
     * the events scheduled in the main context would never be fully released.
     * This is obviously not a problem in this example as it will just exit, but
     * it can serve as guideline for others integrating sync-only libmm-glib
     * opertions in other programs. By creating an ephimeral main loop here and
     * appending an idle task (that will be run after all the other idle tasks
     * that may have been already scheduled), we make sure all are processed and
     * removed before exiting.*/
    {
        g_autoptr(GMainLoop) loop = NULL;

        loop = g_main_loop_new (NULL, FALSE);
        g_idle_add ((GSourceFunc)loop_ready, loop);
        g_main_loop_run (loop);
    }

    return 0;
}
