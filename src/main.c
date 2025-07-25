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
#include <stdlib.h>

#include <gio/gio.h>
#include <glib-unix.h>

#include "ModemManager.h"

#define MM_LOG_NO_OBJECT
#include "mm-log.h"
#include "mm-base-manager.h"
#include "mm-context.h"

#if defined WITH_SUSPEND_RESUME
# include "mm-sleep-monitor.h"
#endif

/* Maximum time to wait for all modems to get disabled and removed */
#define MAX_SHUTDOWN_TIME_SECS 20

static GMainLoop *loop;
static MMBaseManager *manager;

static gboolean
quit_cb (gpointer user_data)
{
    mm_msg ("caught signal, shutting down...");

    if (manager)
        g_object_set (manager, MM_BASE_MANAGER_CONNECTION, NULL, NULL);

    if (loop)
        g_idle_add ((GSourceFunc) g_main_loop_quit, loop);
    else
        exit (0);
    return FALSE;
}

#if defined WITH_SUSPEND_RESUME

static void
sleeping_cb (MMSleepMonitor *sleep_monitor,
             MMSleepContext *ctx)
{
    if (mm_context_get_test_low_power_suspend_resume ()) {
        mm_dbg ("removing devices and setting them in low power mode... (sleeping)");
        mm_base_manager_cleanup (manager,
                                 MM_BASE_MANAGER_CLEANUP_DISABLE |
                                     MM_BASE_MANAGER_CLEANUP_LOW_POWER |
                                     MM_BASE_MANAGER_CLEANUP_REMOVE,
                                 ctx);
    } else {
        mm_dbg ("removing devices... (sleeping)");
        mm_base_manager_cleanup (manager, MM_BASE_MANAGER_CLEANUP_REMOVE, ctx);
    }
}

static void
resuming_cb (MMSleepMonitor *sleep_monitor)
{
    mm_dbg ("re-scanning (resuming)");
    mm_base_manager_start (manager, FALSE);
}

static void
sleeping_quick_cb (MMSleepMonitor *sleep_monitor,
                   MMSleepContext *ctx)
{
    if (mm_context_get_test_low_power_suspend_resume ()) {
        mm_dbg ("setting modem in low power mode... (sleeping)");
        mm_base_manager_cleanup (manager,
                                 MM_BASE_MANAGER_CLEANUP_DISABLE |
                                     MM_BASE_MANAGER_CLEANUP_LOW_POWER,
                                 ctx);
    } else {
        mm_dbg ("setting modem in terse mode... (only send important signals (call/text))");
        mm_base_manager_cleanup (manager,
                                 MM_BASE_MANAGER_CLEANUP_TERSE,
                                 ctx);
    }
}

static void
resuming_quick_cb (MMSleepMonitor *sleep_monitor)
{
    mm_dbg ("syncing modem state (quick resuming)");
    mm_base_manager_sync (manager);
}

#endif

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar *name,
                 gpointer user_data)
{
    GError *error = NULL;

    mm_dbg ("bus acquired, creating manager...");

    /* Create Manager object */
    g_assert (!manager);
    manager = mm_base_manager_new (connection,
#if !defined WITH_BUILTIN_PLUGINS
                                   mm_context_get_test_plugin_dir (),
#endif
                                   !mm_context_get_no_auto_scan (),
                                   mm_context_get_filter_policy (),
                                   mm_context_get_initial_kernel_events (),
#if defined WITH_TESTS
                                   mm_context_get_test_enable (),
#endif
                                   &error);
    if (!manager) {
        mm_warn ("could not create manager: %s", error->message);
        g_error_free (error);
        g_main_loop_quit (loop);
        return;
    }
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data)
{
    mm_dbg ("service name '%s' was acquired", name);

    /* Launch automatic scan for devices */
    g_assert (manager);
    mm_base_manager_start (manager, FALSE);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
    /* Note that we're not allowing replacement, so once the name acquired, the
     * process won't lose it. */
    if (!name)
        mm_warn ("could not get the system bus; make sure the message bus daemon is running!");
    else
        mm_warn ("could not acquire the '%s' service name", name);

    if (manager)
        g_object_set (manager, MM_BASE_MANAGER_CONNECTION, NULL, NULL);

    g_main_loop_quit (loop);
}

static void
register_dbus_errors (void)
{
    /* This method will always return success once during runtime */
    if (!mm_common_register_errors ())
        return;

    /* We no longer use MM_CORE_ERROR_CANCELLED in the daemon, we rely on
     * G_IO_ERROR_CANCELLED internally */
    g_dbus_error_unregister_error (MM_CORE_ERROR, MM_CORE_ERROR_CANCELLED, MM_CORE_ERROR_DBUS_PREFIX ".Cancelled");
    g_dbus_error_register_error   (G_IO_ERROR,    G_IO_ERROR_CANCELLED,    MM_CORE_ERROR_DBUS_PREFIX ".Cancelled");
}

static void
shutdown_done (MMSleepContext *sleep_ctx,
               GError         *error,
               GMainLoop      *quit_loop)
{
    if (error)
        mm_warn ("shutdown failed: %s", error->message);
    else
        mm_msg ("shutdown complete");
    g_main_loop_quit (quit_loop);
}

int
main (int argc, char *argv[])
{
    GMainLoop *inner;
    GError    *error = NULL;
    guint      name_id;

    /* Setup application context */
    mm_context_init (argc, argv);

    if (!mm_log_setup (mm_context_get_log_level (),
                       mm_context_get_log_file (),
                       mm_context_get_log_journal (),
                       mm_context_get_log_timestamps (),
                       mm_context_get_log_relative_timestamps (),
                       mm_context_get_log_personal_info (),
                       &error)) {
        g_printerr ("error: failed to set up logging: %s\n", error->message);
        g_error_free (error);
        exit (1);
    }

    g_unix_signal_add (SIGTERM, quit_cb, NULL);
    g_unix_signal_add (SIGINT, quit_cb, NULL);

    /* Early register all known errors */
    register_dbus_errors ();

    mm_msg ("ModemManager (version " MM_DIST_VERSION ") starting in %s bus...",
            mm_context_get_test_session () ? "session" : "system");

    /* Detect runtime charset conversion support */
    mm_modem_charsets_init ();

    /* Acquire name, don't allow replacement */
    name_id = g_bus_own_name (mm_context_get_test_session () ? G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM,
                              MM_DBUS_SERVICE,
                              G_BUS_NAME_OWNER_FLAGS_NONE,
                              bus_acquired_cb,
                              name_acquired_cb,
                              name_lost_cb,
                              NULL,
                              NULL);
#if defined WITH_SUSPEND_RESUME
    {
        MMSleepMonitor *sleep_monitor;

        if (mm_context_get_test_no_suspend_resume ())
            mm_dbg ("Suspend/resume support disabled at runtime");
        else if (mm_context_get_test_quick_suspend_resume ()) {
            mm_dbg ("Quick suspend/resume hooks enabled");
            sleep_monitor = mm_sleep_monitor_get ();
            g_signal_connect (sleep_monitor, MM_SLEEP_MONITOR_SLEEPING, G_CALLBACK (sleeping_quick_cb), NULL);
            g_signal_connect (sleep_monitor, MM_SLEEP_MONITOR_RESUMING, G_CALLBACK (resuming_quick_cb), NULL);
        } else {
            mm_dbg ("Full suspend/resume hooks enabled");
            sleep_monitor = mm_sleep_monitor_get ();
            g_signal_connect (sleep_monitor, MM_SLEEP_MONITOR_SLEEPING, G_CALLBACK (sleeping_cb), NULL);
            g_signal_connect (sleep_monitor, MM_SLEEP_MONITOR_RESUMING, G_CALLBACK (resuming_cb), NULL);
        }
    }
#endif

    /* Go into the main loop */
    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    /* Clear the global variable, so that subsequent requests to
     * exit succeed. */
    inner = loop;
    loop = NULL;

    if (manager) {
        MMSleepContext *ctx;
        guint           sleep_done_id;

        ctx = mm_sleep_context_new (MAX_SHUTDOWN_TIME_SECS);
        sleep_done_id = g_signal_connect (ctx,
                                          MM_SLEEP_CONTEXT_DONE,
                                          (GCallback)shutdown_done,
                                          inner);

        mm_base_manager_cleanup (manager,
                                 MM_BASE_MANAGER_CLEANUP_DISABLE | MM_BASE_MANAGER_CLEANUP_REMOVE,
                                 ctx);

        /* Wait for all modems to be disabled and removed, but don't wait
         * forever: if disabling the modems takes longer than 20s, just
         * shutdown anyway. */
        while (mm_base_manager_num_modems (manager))
            g_main_loop_run (inner);

        g_signal_handler_disconnect (ctx, sleep_done_id);
        g_object_unref (ctx);

        if (mm_base_manager_num_modems (manager))
            mm_warn ("disabling modems took too long, shutting down with %u modems around",
                     mm_base_manager_num_modems (manager));

        g_object_unref (manager);
    }

    g_main_loop_unref (inner);

    g_bus_unown_name (name_id);

    mm_msg ("ModemManager is shut down");

    mm_log_shutdown ();

    return 0;
}
