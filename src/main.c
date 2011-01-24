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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 */

#include <config.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <stdlib.h>

#include "mm-manager.h"
#include "mm-log.h"

#if !defined(MM_DIST_VERSION)
# define MM_DIST_VERSION VERSION
#endif

static GMainLoop *loop = NULL;

static void
mm_signal_handler (int signo)
{
    if (signo == SIGUSR1)
        mm_log_usr1 ();
	else if (signo == SIGINT || signo == SIGTERM) {
		mm_info ("Caught signal %d, shutting down...", signo);
        if (loop)
            g_main_loop_quit (loop);
        else
            _exit (0);
    }
}

static void
setup_signals (void)
{
    struct sigaction action;
    sigset_t mask;

    sigemptyset (&mask);
    action.sa_handler = mm_signal_handler;
    action.sa_mask = mask;
    action.sa_flags = 0;
    sigaction (SIGUSR1, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
}

static void
destroy_cb (DBusGProxy *proxy, gpointer user_data)
{
    mm_warn ("disconnected from the system bus, exiting.");
    g_main_loop_quit (loop);
}

static DBusGProxy *
create_dbus_proxy (DBusGConnection *bus)
{
    DBusGProxy *proxy;
    GError *err = NULL;
    int request_name_result;

    proxy = dbus_g_proxy_new_for_name (bus,
                                       "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus");

    if (!dbus_g_proxy_call (proxy, "RequestName", &err,
                            G_TYPE_STRING, MM_DBUS_SERVICE,
                            G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                            G_TYPE_INVALID,
                            G_TYPE_UINT, &request_name_result,
                            G_TYPE_INVALID)) {
        mm_warn ("Could not acquire the %s service.\n"
                 "  Message: '%s'", MM_DBUS_SERVICE, err->message);

        g_error_free (err);
        g_object_unref (proxy);
        proxy = NULL;
    } else if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        mm_warn ("Could not acquire the " MM_DBUS_SERVICE
                 " service as it is already taken. Return: %d",
                 request_name_result);

        g_object_unref (proxy);
        proxy = NULL;
    } else {
        dbus_g_proxy_add_signal (proxy, "NameOwnerChanged",
                                 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                 G_TYPE_INVALID);
    }

    return proxy;
}

static gboolean
start_manager (gpointer user_data)
{
    mm_manager_start (MM_MANAGER (user_data));
    return FALSE;
}

int
main (int argc, char *argv[])
{
    DBusGConnection *bus;
    DBusGProxy *proxy;
    MMManager *manager;
    GError *err = NULL;
    GOptionContext *opt_ctx;
    guint id;
    const char *log_level = NULL, *log_file = NULL;
    gboolean debug = FALSE, show_ts = FALSE, rel_ts = FALSE;

    GOptionEntry entries[] = {
		{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Output to console rather than syslog", NULL },
		{ "log-level", 0, 0, G_OPTION_ARG_STRING, &log_level, "Log level: one of [ERR, WARN, INFO, DEBUG]", "INFO" },
		{ "log-file", 0, 0, G_OPTION_ARG_STRING, &log_file, "Path to log file", NULL },
		{ "timestamps", 0, 0, G_OPTION_ARG_NONE, &show_ts, "Show timestamps in log output", NULL },
		{ "relative-timestamps", 0, 0, G_OPTION_ARG_NONE, &rel_ts, "Use relative timestamps (from MM start)", NULL },
		{ NULL }
	};

    g_type_init ();

	opt_ctx = g_option_context_new (NULL);
	g_option_context_set_summary (opt_ctx, "DBus system service to communicate with modems.");
	g_option_context_add_main_entries (opt_ctx, entries, NULL);

	if (!g_option_context_parse (opt_ctx, &argc, &argv, &err)) {
		g_warning ("%s\n", err->message);
		g_error_free (err);
		exit (1);
	}

	g_option_context_free (opt_ctx);

    if (debug) {
        log_level = "DEBUG";
        if (!show_ts && !rel_ts)
            show_ts = TRUE;
    }

    if (!mm_log_setup (log_level, log_file, show_ts, rel_ts, &err)) {
        g_warning ("Failed to set up logging: %s", err->message);
        g_error_free (err);
        exit (1);
    }

    setup_signals ();

    mm_info ("ModemManager (version " MM_DIST_VERSION ") starting...");

    bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &err);
    if (!bus) {
        g_warning ("Could not get the system bus. Make sure "
                   "the message bus daemon is running! Message: %s",
                   err->message);
        g_error_free (err);
        return -1;
    }

#ifndef HAVE_DBUS_GLIB_DISABLE_LEGACY_PROP_ACCESS
#error HAVE_DBUS_GLIB_DISABLE_LEGACY_PROP_ACCESS not defined
#endif

#if HAVE_DBUS_GLIB_DISABLE_LEGACY_PROP_ACCESS
    /* Ensure that non-exported properties don't leak out, and that the
     * introspection 'access' permissions are respected.
     */
    dbus_glib_global_set_disable_legacy_property_access ();
#endif

    proxy = create_dbus_proxy (bus);
    if (!proxy)
        return -1;

    manager = mm_manager_new (bus);
    g_idle_add (start_manager, manager);

    loop = g_main_loop_new (NULL, FALSE);
    id = g_signal_connect (proxy, "destroy", G_CALLBACK (destroy_cb), loop);

    g_main_loop_run (loop);

    g_signal_handler_disconnect (proxy, id);

    mm_manager_shutdown (manager);

    /* Wait for all modems to be removed */
    while (mm_manager_num_modems (manager)) {
        GMainContext *ctx = g_main_loop_get_context (loop);

        g_main_context_iteration (ctx, FALSE);
        g_usleep (50);
    }

    g_object_unref (manager);
    g_object_unref (proxy);
    dbus_g_connection_unref (bus);    

    mm_log_shutdown ();

    return 0;
}
