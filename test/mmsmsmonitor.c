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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <libmm-glib.h>

#define PROGRAM_NAME    "mmsmsmonitor"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Globals */
static GMainLoop *loop;
static GList     *monitored_sms_list;

/* Context */
static gboolean version_flag;

static GOptionEntry main_entries[] = {
    { "version", 'V', 0, G_OPTION_ARG_NONE, &version_flag,
      "Print version",
      NULL
    },
    { NULL }
};

static gboolean
signals_handler (void)
{
    if (loop && g_main_loop_is_running (loop)) {
        g_printerr ("%s\n",
                    "cancelling the main loop...\n");
        g_main_loop_quit (loop);
    }
    return TRUE;
}

static void
print_version_and_exit (void)
{
    g_print ("\n"
             PROGRAM_NAME " " PROGRAM_VERSION "\n"
             "Copyright (2019) Aleksander Morgado\n"
             "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>\n"
             "This is free software: you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by law.\n"
             "\n");
    exit (EXIT_SUCCESS);
}

static void
sms_state_updated (MMSms *sms)
{
    g_print ("[%s] sms updated: %s\n",
             mm_sms_get_path (sms),
             mm_sms_state_get_string (mm_sms_get_state (sms)));
}

static gboolean
sms_added (MMModemMessaging *modem_messaging,
           const gchar      *sms_path,
           gboolean          received)
{
    GList *sms_list;
    GList *l;
    MMSms *new_sms = NULL;

    sms_list = mm_modem_messaging_list_sync (modem_messaging, NULL, NULL);
    for (l = sms_list; l && !new_sms; l = g_list_next (l)) {
        MMSms *l_sms = MM_SMS  (l->data);

        if (g_strcmp0 (mm_sms_get_path (l_sms), sms_path) == 0)
            new_sms = l_sms;
    }
    g_assert (new_sms);

    g_print ("[%s] new sms: %s\n",
             mm_sms_get_path (new_sms),
             mm_sms_state_get_string (mm_sms_get_state (new_sms)));
    g_signal_connect (new_sms, "notify::state", G_CALLBACK (sms_state_updated), NULL);
    monitored_sms_list = g_list_append (monitored_sms_list, g_object_ref (new_sms));

    g_list_free_full (sms_list, g_object_unref);
    return TRUE;
}

static void
list_all_sms_found (MMModemMessaging *modem_messaging)
{
    GList *sms_list;
    GList *l;

    sms_list = mm_modem_messaging_list_sync (modem_messaging, NULL, NULL);
    for (l = sms_list; l; l = g_list_next (l)) {
        MMSms *l_sms = MM_SMS  (l->data);

        g_print ("[%s] sms found: %s\n",
                 mm_sms_get_path (l_sms),
                 mm_sms_state_get_string (mm_sms_get_state (l_sms)));
        g_signal_connect (l_sms, "notify::state", G_CALLBACK (sms_state_updated), NULL);
        monitored_sms_list = g_list_append (monitored_sms_list, g_object_ref (l_sms));
    }
    g_list_free_full (sms_list, g_object_unref);
}

int main (int argc, char **argv)
{
    GOptionContext  *context;
    GDBusConnection *connection;
    MMManager       *manager;
    GList           *modem_list;
    GList           *l;
    gchar           *name_owner;
    GError          *error = NULL;

    setlocale (LC_ALL, "");

    /* Setup option context, process it and destroy it */
    context = g_option_context_new ("- ModemManager SMS monitor");
    g_option_context_add_main_entries (context, main_entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (version_flag)
        print_version_and_exit ();

    /* Setup dbus connection to use */
    connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!connection) {
        g_printerr ("error: couldn't get bus: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    manager = mm_manager_new_sync (connection,
                                   G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                                   NULL,
                                   &error);
    if (!manager) {
        g_printerr ("error: couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (manager));
    if (!name_owner) {
        g_printerr ("error: couldn't find the ModemManager process in the bus\n");
        exit (EXIT_FAILURE);
    }
    g_free (name_owner);

    modem_list = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    for (l = modem_list; l; l = g_list_next (l)) {
        MMObject         *obj;
        MMModemMessaging *modem_messaging;

        obj             = MM_OBJECT (l->data);
        modem_messaging = MM_MODEM_MESSAGING (mm_object_peek_modem_messaging (obj));

        g_signal_connect (modem_messaging, "added", G_CALLBACK (sms_added), NULL);
        list_all_sms_found (modem_messaging);
    }

    g_unix_signal_add (SIGINT,  (GSourceFunc) signals_handler, NULL);
    g_unix_signal_add (SIGHUP,  (GSourceFunc) signals_handler, NULL);
    g_unix_signal_add (SIGTERM, (GSourceFunc) signals_handler, NULL);

    /* Setup main loop and shedule start in idle */
    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    /* Cleanup */
    g_main_loop_unref (loop);
    g_list_free_full (modem_list, g_object_unref);
    g_object_unref (manager);
    return EXIT_SUCCESS;
}
