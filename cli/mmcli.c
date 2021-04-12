/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
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

#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"
#include "mmcli-output.h"

#define PROGRAM_NAME    "mmcli"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Globals */
static GMainLoop *loop;
static GCancellable *cancellable;

/* Context */
static gboolean output_keyvalue_flag;
static gboolean output_json_flag;
static gboolean verbose_flag;
static gboolean version_flag;
static gboolean async_flag;
static gint timeout = 30; /* by default, use 30s for all operations */

static GOptionEntry main_entries[] = {
    { "output-keyvalue", 'K', 0, G_OPTION_ARG_NONE, &output_keyvalue_flag,
      "Run action with machine-friendly key-value output",
      NULL
    },
    { "output-json", 'J', 0, G_OPTION_ARG_NONE, &output_json_flag,
      "Run action with machine-friendly json output",
      NULL
    },
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
    { "timeout", 0, 0, G_OPTION_ARG_INT, &timeout,
      "Timeout for the operation",
      "[SECONDS]"
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
                        "cancelling the operation...\n");
            g_cancellable_cancel (cancellable);
        }
        return;
    }

    if (loop &&
        g_main_loop_is_running (loop)) {
        g_printerr ("%s\n",
                    "cancelling the main loop...\n");
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
    case G_LOG_LEVEL_ERROR:
        log_level_str = "-Error **";
        break;

    case G_LOG_LEVEL_DEBUG:
        log_level_str = "[Debug]";
        break;

    case G_LOG_LEVEL_MESSAGE:
    case G_LOG_LEVEL_INFO:
        log_level_str = "";
        break;

    case G_LOG_FLAG_FATAL:
    case G_LOG_LEVEL_MASK:
    case G_LOG_FLAG_RECURSION:
    default:
        g_assert_not_reached ();
    }

    g_print ("[%s] %s %s\n", time_str, log_level_str, message);
}

static void
print_version_and_exit (void)
{
    g_print (PROGRAM_NAME " " PROGRAM_VERSION "\n"
             "Copyright (2011 - 2021) Aleksander Morgado\n"
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

void
mmcli_force_operation_timeout (GDBusProxy *proxy)
{
    if (proxy)
        g_dbus_proxy_set_default_timeout (proxy, timeout * 1000);
}

gint
main (gint argc, gchar **argv)
{
    GDBusConnection *connection;
    GOptionContext *context;
    GError *error = NULL;

    setlocale (LC_ALL, "");

    /* Setup option context, process it and destroy it */
    context = g_option_context_new ("- Control and monitor the ModemManager");
    g_option_context_add_group (context,
                                mmcli_manager_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_get_common_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_3gpp_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_cdma_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_simple_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_location_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_messaging_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_voice_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_time_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_firmware_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_signal_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_modem_oma_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_sim_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_bearer_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_sms_get_option_group ());
    g_option_context_add_group (context,
                                mmcli_call_get_option_group ());
    g_option_context_add_main_entries (context, main_entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (version_flag)
        print_version_and_exit ();

    if (verbose_flag)
        g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_MASK, log_handler, NULL);

    /* Setup output */
    if (output_keyvalue_flag && output_json_flag) {
        g_printerr ("error: only one output type supported at the same time\n");
        exit (EXIT_FAILURE);
    }
    if (output_keyvalue_flag) {
        if (verbose_flag) {
            g_printerr ("error: cannot set verbose output in keyvalue output type\n");
            exit (EXIT_FAILURE);
        }
        mmcli_output_set (MMC_OUTPUT_TYPE_KEYVALUE);
    }
    else if (output_json_flag) {
        if (verbose_flag) {
            g_printerr ("error: cannot set verbose output in JSON output type\n");
            exit (EXIT_FAILURE);
        }
        mmcli_output_set (MMC_OUTPUT_TYPE_JSON);
    } else {
        mmcli_output_set (MMC_OUTPUT_TYPE_HUMAN);
    }

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
    /* Sim options? */
    else if (mmcli_sim_options_enabled ()) {
        if (async_flag)
            mmcli_sim_run_asynchronous (connection, cancellable);
        else
            mmcli_sim_run_synchronous (connection);
    }
    /* Bearer options? */
    else if (mmcli_bearer_options_enabled ()) {
        if (async_flag)
            mmcli_bearer_run_asynchronous (connection, cancellable);
        else
            mmcli_bearer_run_synchronous (connection);
    }
    /* Sms options? */
    else if (mmcli_sms_options_enabled ()) {
        if (async_flag)
            mmcli_sms_run_asynchronous (connection, cancellable);
        else
            mmcli_sms_run_synchronous (connection);
    }
    /* Call options? */
    else if (mmcli_call_options_enabled ()) {
        if (async_flag)
            mmcli_call_run_asynchronous (connection, cancellable);
        else
            mmcli_call_run_synchronous (connection);
    }
    /* Modem 3GPP options? */
    else if (mmcli_modem_3gpp_options_enabled ()) {
        if (async_flag)
            mmcli_modem_3gpp_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_3gpp_run_synchronous (connection);
    }
    /* Modem CDMA options? */
    else if (mmcli_modem_cdma_options_enabled ()) {
        if (async_flag)
            mmcli_modem_cdma_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_cdma_run_synchronous (connection);
    }
    /* Modem Simple options? */
    else if (mmcli_modem_simple_options_enabled ()) {
        if (async_flag)
            mmcli_modem_simple_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_simple_run_synchronous (connection);
    }
    /* Modem Location options? */
    else if (mmcli_modem_location_options_enabled ()) {
        if (async_flag)
            mmcli_modem_location_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_location_run_synchronous (connection);
    }
    /* Modem Messaging options? */
    else if (mmcli_modem_messaging_options_enabled ()) {
        if (async_flag)
            mmcli_modem_messaging_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_messaging_run_synchronous (connection);
    }
    /* Voice options? */
    else if (mmcli_modem_voice_options_enabled ()) {
        if (async_flag)
            mmcli_modem_voice_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_voice_run_synchronous (connection);
    }
    /* Modem Time options? */
    else if (mmcli_modem_time_options_enabled ()) {
        if (async_flag)
            mmcli_modem_time_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_time_run_synchronous (connection);
    }
    /* Modem Firmware options? */
    else if (mmcli_modem_firmware_options_enabled ()) {
        if (async_flag)
            mmcli_modem_firmware_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_firmware_run_synchronous (connection);
    }
    /* Modem Signal options? */
    else if (mmcli_modem_signal_options_enabled ()) {
        if (async_flag)
            mmcli_modem_signal_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_signal_run_synchronous (connection);
    }
    /* Modem Oma options? */
    else if (mmcli_modem_oma_options_enabled ()) {
        if (async_flag)
            mmcli_modem_oma_run_asynchronous (connection, cancellable);
        else
            mmcli_modem_oma_run_synchronous (connection);
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
    } else if (mmcli_modem_3gpp_options_enabled ()) {
        mmcli_modem_3gpp_shutdown ();
    } else if (mmcli_modem_cdma_options_enabled ()) {
        mmcli_modem_cdma_shutdown ();
    } else if (mmcli_modem_simple_options_enabled ()) {
        mmcli_modem_simple_shutdown ();
    } else if (mmcli_modem_location_options_enabled ()) {
        mmcli_modem_location_shutdown ();
    } else if (mmcli_modem_messaging_options_enabled ()) {
        mmcli_modem_messaging_shutdown ();
    } else if (mmcli_modem_voice_options_enabled ()) {
        mmcli_modem_voice_shutdown ();
    } else if (mmcli_modem_time_options_enabled ()) {
        mmcli_modem_time_shutdown ();
    } else if (mmcli_modem_firmware_options_enabled ()) {
        mmcli_modem_firmware_shutdown ();
    } else if (mmcli_modem_signal_options_enabled ()) {
        mmcli_modem_signal_shutdown ();
    } else if (mmcli_modem_oma_options_enabled ()) {
        mmcli_modem_oma_shutdown ();
    }  else if (mmcli_sim_options_enabled ()) {
        mmcli_sim_shutdown ();
    } else if (mmcli_bearer_options_enabled ()) {
        mmcli_bearer_shutdown ();
    }  else if (mmcli_sms_options_enabled ()) {
        mmcli_sms_shutdown ();
    }  else if (mmcli_call_options_enabled ()) {
        mmcli_call_shutdown ();
    } else if (mmcli_modem_options_enabled ()) {
        mmcli_modem_shutdown ();
    }

    if (cancellable)
        g_object_unref (cancellable);
    g_main_loop_unref (loop);
    g_object_unref (connection);

    return EXIT_SUCCESS;
}
