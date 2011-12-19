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
 * Copyright (C) 2011 Google, Inc.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libmm-glib.h>

#include "mmcli.h"

#define PROGRAM_NAME    "mmcli"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Globals */
static GMainLoop *loop;
static GCancellable *cancellable;

/* Context */
static gboolean verbose_flag;
static gboolean version_flag;
static gboolean async_flag;

static GOptionEntry main_entries[] = {
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose_flag,
      "Run action with verbose logs",
      NULL
    },
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
log_handler (const gchar *log_domain,
             GLogLevelFlags log_level,
             const gchar *message,
             gpointer user_data)
{
    const gchar *log_level_str;
	time_t now;
	gchar time_str[64];
	struct tm    *local_time;

	now = time ((time_t *) NULL);
	local_time = localtime (&now);
	strftime (time_str, 64, "%d %b %Y, %H:%M:%S", local_time);

	switch (log_level) {
	case G_LOG_LEVEL_WARNING:
		log_level_str = "-Warning **";
		break;

	case G_LOG_LEVEL_CRITICAL:
    case G_LOG_FLAG_FATAL:
	case G_LOG_LEVEL_ERROR:
		log_level_str = "-Error **";
		break;

	case G_LOG_LEVEL_DEBUG:
		log_level_str = "[Debug]";
		break;

    default:
		log_level_str = "";
		break;
    }

    g_print ("[%s] %s %s\n", time_str, log_level_str, message);
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

    g_main_loop_quit (loop);
}

void
mmcli_force_async_operation (void)
{
    if (!async_flag) {
        g_debug ("Forcing request to be run asynchronously");
        async_flag = TRUE;
    }
}

void
mmcli_force_sync_operation (void)
{
    if (async_flag) {
        g_debug ("Ignoring request to run asynchronously");
        async_flag = FALSE;
    }
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
	g_option_context_add_group (context,
	                            mmcli_modem_get_option_group ());
	g_option_context_add_group (context,
	                            mmcli_bearer_get_option_group ());
    g_option_context_add_main_entries (context, main_entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

    if (version_flag)
        print_version_and_exit ();

    if (verbose_flag)
        g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_MASK, log_handler, NULL);

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

    /* Create requirements for async options */
    cancellable = g_cancellable_new ();
    loop = g_main_loop_new (NULL, FALSE);

    /* Manager options? */
    if (mmcli_manager_options_enabled ()) {
        /* Ensure options from different groups are not enabled */
        if (mmcli_modem_options_enabled ()) {
            g_printerr ("error: cannot use manager and modem options "
                        "at the same time\n");
            exit (EXIT_FAILURE);
        }

        if (async_flag)
            mmcli_manager_run_asynchronous (connection, cancellable);
        else
            mmcli_manager_run_synchronous (connection);
    }
    /* Bearer options? */
    else if (mmcli_bearer_options_enabled ()) {
        if (async_flag)
            mmcli_bearer_run_asynchronous (connection, cancellable);
        else
            mmcli_bearer_run_synchronous (connection);
    }
    /* Modem options?
     * NOTE: let this check be always the last one, as other groups also need
     * having a modem specified, and therefore if -m is set, modem options
     * are always enabled. */
    else if (mmcli_modem_options_enabled ()) {
        if (async_flag)
            mmcli_modem_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_run_synchronous (connection);
    }
    /* No options? */
    else {
        g_printerr ("error: no actions specified\n");
        exit (EXIT_FAILURE);
    }

    /* Run loop only in async operations */
    if (async_flag)
        g_main_loop_run (loop);

    if (mmcli_manager_options_enabled ()) {
        mmcli_manager_shutdown ();
    } else if (mmcli_modem_options_enabled ()) {
        mmcli_modem_shutdown ();
    }  else if (mmcli_bearer_options_enabled ()) {
        mmcli_bearer_shutdown ();
    }

    if (cancellable)
        g_object_unref (cancellable);
    g_main_loop_unref (loop);
    g_object_unref (connection);

    return EXIT_SUCCESS;
}
