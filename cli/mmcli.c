/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libmm.h>

#include "mmcli.h"

#define PROGRAM_NAME    "mmcli"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Globals */
static GMainLoop *loop;
static gboolean keep_loop;
static GCancellable *cancellable;

/* Context */
static gboolean version_flag;
static gboolean async_flag;

static GOptionEntry main_entries[] = {
    { "version", 'V', 0, G_OPTION_ARG_NONE, &version_flag,
      "Print version",
      NULL
    },
    { "async", 'a', 0, G_OPTION_ARG_NONE, &async_flag,
      "Use asynchronous methods",
      NULL
    },
    { NULL }
};

static void
signals_handler (int signum)
{
    if (cancellable) {
        /* Ignore consecutive requests of cancellation */
        if (!g_cancellable_is_cancelled (cancellable)) {
            g_printerr ("%s\n",
                        "cancelling the operation...");
            g_cancellable_cancel (cancellable);
        }
        return;
    }

    if (loop &&
        g_main_loop_is_running (loop)) {
        g_printerr ("%s\n",
                    "cancelling the main loop...");
        g_main_loop_quit (loop);
    }
}

static void
print_version_and_exit (void)
{
    g_print ("\n"
             PROGRAM_NAME " " PROGRAM_VERSION "\n"
             "Copyright (2011) Aleksander Morgado\n"
             "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>\n"
             "This is free software: you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by law.\n"
             "\n");
    exit (EXIT_SUCCESS);
}

void
mmcli_async_operation_done (void)
{
    if (cancellable) {
        g_object_unref (cancellable);
        cancellable = NULL;
    }

    if (!keep_loop)
        g_main_loop_quit (loop);
}

gint
main (gint argc, gchar **argv)
{
    GDBusConnection *connection;
    GOptionContext *context;
    GError *error = NULL;

    setlocale (LC_ALL, "");

    g_type_init ();

    /* Setup option context, process it and destroy it */
    context = g_option_context_new ("- Control and monitor the ModemManager");
	g_option_context_add_group (context,
	                            mmcli_manager_get_option_group ());
    g_option_context_add_main_entries (context, main_entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

    if (version_flag)
        print_version_and_exit ();

    /* Setup signals */
    signal (SIGINT, signals_handler);
    signal (SIGHUP, signals_handler);
    signal (SIGTERM, signals_handler);

    /* Setup dbus connection to use */
    connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!connection) {
        g_printerr ("error: couldn't get bus: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    /* Setup context for asynchronous operations */
    if (async_flag) {
        g_debug ("Running asynchronous operations...");
        loop = g_main_loop_new (NULL, FALSE);
        cancellable = g_cancellable_new ();
    }

    /* Manager options? */
    if (mmcli_manager_options_enabled ()) {
        if (async_flag)
            keep_loop = mmcli_manager_run_asynchronous (connection, cancellable);
        else
            mmcli_manager_run_synchronous (connection);
    }

    /* Run loop only in async operations */
    if (async_flag) {
        g_main_loop_run (loop);
        if (cancellable)
            g_object_unref (cancellable);
        g_main_loop_unref (loop);
    }

    if (mmcli_manager_options_enabled ()) {
        mmcli_manager_shutdown ();
    }

    g_object_unref (connection);

    return EXIT_SUCCESS;
}

