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

#include "libmm-glib.h"

#include "mmcli.h"

/* Context */
typedef struct {
    MMManager *manager;
} Context;
static Context ctxt;

/* Options */
static gboolean list_modems_flag;
static gboolean monitor_modems_flag;
static gboolean scan_modems_flag;
static gchar *set_logging_str;

static GOptionEntry entries[] = {
    { "set-logging", 'G', 0, G_OPTION_ARG_STRING, &set_logging_str,
      "Set logging level in the ModemManager daemon",
      "[ERR,WARN,INFO,DEBUG]",
    },
    { "list-modems", 'L', 0, G_OPTION_ARG_NONE, &list_modems_flag,
      "List available modems",
      NULL
    },
    { "monitor-modems", 'M', 0, G_OPTION_ARG_NONE, &monitor_modems_flag,
      "List available modems and monitor additions and removals",
      NULL
    },
    { "scan-modems", 'S', 0, G_OPTION_ARG_NONE, &scan_modems_flag,
      "Request to re-scan looking for modems",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_manager_get_option_group (void)
{
	GOptionGroup *group;

	/* Status options */
	group = g_option_group_new ("manager",
	                            "Manager options",
	                            "Show manager options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

gboolean
mmcli_manager_options_enabled (void)
{
    guint n_actions;

    n_actions = (list_modems_flag +
                 monitor_modems_flag +
                 scan_modems_flag +
                 (set_logging_str ? 1 : 0));

    if (n_actions > 1) {
        g_printerr ("error, too many manager actions requested\n");
        exit (EXIT_FAILURE);
    }

    return !!n_actions;
}

static void
init (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Create new manager */
    ctxt.manager = mm_manager_new (connection, NULL, &error);
    if (!ctxt.manager) {
        g_printerr ("couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }
}

void
mmcli_manager_shutdown (void)
{
    g_object_unref (ctxt.manager);
}

static void
scan_devices_process_reply (gboolean      result,
                            const GError *error)
{
    if (!result) {
        g_printerr ("couldn't request to scan devices: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully requested to scan devices\n");
}

static void
scan_devices_ready (MMManager    *manager,
                    GAsyncResult *result,
                    gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_manager_scan_devices_finish (manager,
                                                       result,
                                                       &error);
    scan_devices_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
enumerate_devices_process_reply (const GStrv   paths,
                                 const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't enumerate devices: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("\n");
    if (!paths) {
        g_print ("No modems were found");
    } else {
        guint i;

        for (i = 0; paths[i]; i++) {
            g_print ("%s: '%s'\n",
                     "Found modem",
                     paths[i]);
        }
    }
    g_print ("\n");
}

static void
enumerate_devices_ready (MMManager    *manager,
                         GAsyncResult *result,
                         gpointer      nothing)
{
    GStrv paths;
    GError *error = NULL;

    paths = mm_manager_enumerate_devices_finish (manager, result, &error);
    enumerate_devices_process_reply (paths, error);
    g_strfreev (paths);

    mmcli_async_operation_done ();
}

static void
device_added (MMManager   *manager,
              const gchar *path)
{
    g_print ("%s: '%s'\n",
             "Added modem",
             path);
    fflush (stdout);
}

static void
device_removed (MMManager   *manager,
                const gchar *path)
{
    g_print ("%s: '%s'\n",
             "Removed modem",
             path);
    fflush (stdout);
}

gboolean
mmcli_manager_run_asynchronous (GDBusConnection *connection,
                                GCancellable    *cancellable)
{
    gboolean keep_loop = FALSE;

    if (set_logging_str) {
        g_printerr ("error: logging level cannot be set asynchronously\n");
        exit (EXIT_FAILURE);
    }

    /* Initialize context */
    init (connection);

    /* Request to scan modems? */
    if (scan_modems_flag) {
        mm_manager_scan_devices_async (ctxt.manager,
                                       cancellable,
                                       (GAsyncReadyCallback)scan_devices_ready,
                                       NULL);
        return keep_loop;
    }

    /* Request to monitor modems? */
    if (monitor_modems_flag) {
        g_signal_connect (ctxt.manager,
                          "device-added",
                          G_CALLBACK (device_added),
                          NULL);
        g_signal_connect (ctxt.manager,
                          "device-removed",
                          G_CALLBACK (device_removed),
                          NULL);

        /* We need to keep the loop */
        keep_loop = TRUE;
    }

    /* Request to list modems? */
    if (monitor_modems_flag || list_modems_flag) {
        mm_manager_enumerate_devices_async (ctxt.manager,
                                            cancellable,
                                            (GAsyncReadyCallback)enumerate_devices_ready,
                                            NULL);
        return keep_loop;
    }

    g_warn_if_reached ();
    return FALSE;
}

void
mmcli_manager_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    if (monitor_modems_flag) {
        g_printerr ("error: monitoring modems cannot be done synchronously\n");
        exit (EXIT_FAILURE);
    }

    /* Initialize context */
    init (connection);

    /* Request to set log level? */
    if (set_logging_str) {
        MMLogLevel level;

        if (g_strcmp0 (set_logging_str, "ERR") == 0)
            level = MM_LOG_LEVEL_ERROR;
        else if (g_strcmp0 (set_logging_str, "WARN") == 0)
            level = MM_LOG_LEVEL_WARNING;
        else if (g_strcmp0 (set_logging_str, "INFO") == 0)
            level = MM_LOG_LEVEL_INFO;
        else if (g_strcmp0 (set_logging_str, "DEBUG") == 0)
            level = MM_LOG_LEVEL_DEBUG;
        else {
            g_printerr ("couldn't set unknown logging level: '%s'\n",
                        set_logging_str);
            exit (EXIT_FAILURE);
        }

        if (mm_manager_set_logging (ctxt.manager, level, &error)) {
            g_printerr ("couldn't set logging level: '%s'\n",
                        error ? error->message : "unknown error");
            exit (EXIT_FAILURE);
        }
        g_print ("successfully set log level '%s'\n", set_logging_str);
        return;
    }

    /* Request to scan modems? */
    if (scan_modems_flag) {
        gboolean result;

        result = mm_manager_scan_devices (ctxt.manager, &error);
        scan_devices_process_reply (result, error);
        return;
    }

    /* Request to list modems? */
    if (list_modems_flag) {
        GStrv paths;

        paths = mm_manager_enumerate_devices (ctxt.manager, &error);
        enumerate_devices_process_reply (paths, error);
        g_strfreev (paths);
        return;
    }

    g_warn_if_reached ();
}
