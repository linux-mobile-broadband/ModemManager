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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>*
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <libmm-glib.h>

#define PROGRAM_NAME    "mmcbmmonitor"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Globals */
static GMainLoop *loop;
static GList     *monitored_cbm_list;

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
             "          (2024) Guido Günther\n"
             "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>\n"
             "This is free software: you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by law.\n"
             "\n");
    exit (EXIT_SUCCESS);
}


static void
print_cbm (MMCbm *cbm)
{
    g_print ("[%s] new cbm: %s\n",
             mm_cbm_get_path (cbm),
             mm_cbm_state_get_string (mm_cbm_get_state (cbm)));
    if (mm_cbm_get_state (cbm) == MM_CBM_STATE_RECEIVED) {
        const char *lang = mm_cbm_get_language (cbm);
        g_autofree gchar *lang_info = g_strdup ("");

        if (lang) {
            g_free (lang_info);
            lang_info = g_strdup_printf ("[%s]", lang);
        }
        g_print("    %u%s: %s\n", mm_cbm_get_channel (cbm), lang_info, mm_cbm_get_text (cbm));
    }
}


static void
cbm_state_updated (MMCbm *cbm)
{
    print_cbm (cbm);
}

static gboolean
cbm_added (MMModemCellBroadcast *modem_cell_broadcast,
           const gchar *cbm_path)
{
    g_autolist (MMObject) cbm_list = NULL;
    GList *l;
    MMCbm *new_cbm = NULL;

    cbm_list = mm_modem_cell_broadcast_list_sync (modem_cell_broadcast, NULL, NULL);
    for (l = cbm_list; l && !new_cbm; l = g_list_next (l)) {
        MMCbm *l_cbm = MM_CBM (l->data);

        if (g_strcmp0 (mm_cbm_get_path (l_cbm), cbm_path) == 0)
            new_cbm = l_cbm;
    }
    g_assert (new_cbm);

    print_cbm (new_cbm);
    g_signal_connect (new_cbm, "notify::state", G_CALLBACK (cbm_state_updated), NULL);
    monitored_cbm_list = g_list_append (monitored_cbm_list, g_object_ref (new_cbm));

    return TRUE;
}

static void
list_all_cbm_found (MMModemCellBroadcast *modem_cell_broadcast)
{
    g_autolist (MMObject) cbm_list= NULL;
    GList *l;

    cbm_list = mm_modem_cell_broadcast_list_sync (modem_cell_broadcast, NULL, NULL);
    for (l = cbm_list; l; l = g_list_next (l)) {
        MMCbm *l_cbm = MM_CBM  (l->data);

        print_cbm (l_cbm);
        g_signal_connect (l_cbm, "notify::state", G_CALLBACK (cbm_state_updated), NULL);
        monitored_cbm_list = g_list_append (monitored_cbm_list, g_object_ref (l_cbm));
    }
}

static void
on_modem_added (MMManager *manager,
                MMObject *object,
                gpointer unused)
{
  MMModemCellBroadcast *modem_cell_broadcast;

  g_print ("Modem found at path: %s\n", mm_object_get_path (object));

  modem_cell_broadcast = MM_MODEM_CELL_BROADCAST (mm_object_peek_modem_cell_broadcast (object));

  g_signal_connect (modem_cell_broadcast, "added", G_CALLBACK (cbm_added), NULL);
  list_all_cbm_found (modem_cell_broadcast);
}


int main (int argc, char **argv)
{
    g_autoptr (GOptionContext)  context = NULL;
    g_autoptr (GDBusConnection) connection = NULL;
    g_autoptr (MMManager) manager = NULL;
    g_autofree gchar *name_owner = NULL;
    g_autoptr (GError) error = NULL;
    g_autolist (MMObject) modem_list = NULL;
    GList *l;

    setlocale (LC_ALL, "");

    /* Setup option context, process it and destroy it */
    context = g_option_context_new ("- ModemManager CBM monitor");
    g_option_context_add_main_entries (context, main_entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);

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

    modem_list = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    for (l = modem_list; l; l = g_list_next (l))
        on_modem_added (manager, MM_OBJECT (l->data), NULL);

    g_signal_connect (manager, "object-added", G_CALLBACK (on_modem_added), NULL);

    g_unix_signal_add (SIGINT,  (GSourceFunc) signals_handler, NULL);
    g_unix_signal_add (SIGHUP,  (GSourceFunc) signals_handler, NULL);
    g_unix_signal_add (SIGTERM, (GSourceFunc) signals_handler, NULL);

    /* Setup main loop and schedule start in idle */
    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    /* Cleanup */
    g_list_free_full (monitored_cbm_list, g_object_unref);
    g_main_loop_unref (loop);
    return EXIT_SUCCESS;
}
