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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include <config.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "mm-manager.h"
#include "mm-options.h"

#if !defined(MM_DIST_VERSION)
# define MM_DIST_VERSION VERSION
#endif

static GMainLoop *loop = NULL;

static void
mm_signal_handler (int signo)
{
    if (signo == SIGUSR1)
        mm_options_set_debug (!mm_options_debug ());
	else if (signo == SIGINT || signo == SIGTERM) {
		g_message ("Caught signal %d, shutting down...", signo);
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
log_handler (const gchar *log_domain,
             GLogLevelFlags log_level,
             const gchar *message,
             gpointer ignored)
{
    int syslog_priority;    

    switch (log_level) {
    case G_LOG_LEVEL_ERROR:
        syslog_priority = LOG_CRIT;
        break;

    case G_LOG_LEVEL_CRITICAL:
        syslog_priority = LOG_ERR;
        break;

    case G_LOG_LEVEL_WARNING:
        syslog_priority = LOG_WARNING;
        break;

    case G_LOG_LEVEL_MESSAGE:
        syslog_priority = LOG_NOTICE;
        break;

    case G_LOG_LEVEL_DEBUG:
        syslog_priority = LOG_DEBUG;
        break;

    case G_LOG_LEVEL_INFO:
    default:
        syslog_priority = LOG_INFO;
        break;
    }

    syslog (syslog_priority, "%s", message);
}


static void
logging_setup (void)
{
    openlog (G_LOG_DOMAIN, LOG_CONS, LOG_DAEMON);
    g_log_set_handler (G_LOG_DOMAIN, 
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler,
                       NULL);
}

static void
logging_shutdown (void)
{
    closelog ();
}

static void
destroy_cb (DBusGProxy *proxy, gpointer user_data)
{
    g_message ("disconnected from the system bus, exiting.");
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
        g_warning ("Could not acquire the %s service.\n"
                   "  Message: '%s'", MM_DBUS_SERVICE, err->message);

        g_error_free (err);
        g_object_unref (proxy);
        proxy = NULL;
    } else if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        g_warning ("Could not acquire the " MM_DBUS_SERVICE
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
    guint id;

    mm_options_parse (argc, argv);
    g_type_init ();

    setup_signals ();

    if (!mm_options_debug ())
        logging_setup ();

    g_message ("ModemManager (version " MM_DIST_VERSION ") starting...");

    bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &err);
    if (!bus) {
        g_warning ("Could not get the system bus. Make sure "
                   "the message bus daemon is running! Message: %s",
                   err->message);
        g_error_free (err);
        return -1;
    }

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

    logging_shutdown ();

    return 0;
}
