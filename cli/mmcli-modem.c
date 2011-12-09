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

#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"

/* Context */
typedef struct {
    GCancellable *cancellable;
    MMObject *object;
    MMModem *modem;
} Context;
static Context *ctx;

/* Options */
static gchar *modem_str;
static gboolean info_flag;
static gboolean monitor_state_flag;
static gboolean enable_flag;
static gboolean disable_flag;
static gboolean reset_flag;
static gchar *factory_reset_str;

static GOptionEntry entries[] = {
    { "modem", 'm', 0, G_OPTION_ARG_STRING, &modem_str,
      "Specify modem by path or index",
      NULL
    },
    { "info", 'i', 0, G_OPTION_ARG_NONE, &info_flag,
      "Get information of a given modem",
      NULL
    },
    { "monitor-state", 'w', 0, G_OPTION_ARG_NONE, &monitor_state_flag,
      "Monitor state of a given modem",
      NULL
    },
    { "enable", 'e', 0, G_OPTION_ARG_NONE, &enable_flag,
      "Enable a given modem",
      NULL
    },
    { "disable", 'd', 0, G_OPTION_ARG_NONE, &disable_flag,
      "Disable a given modem",
      NULL
    },
    { "reset", 'r', 0, G_OPTION_ARG_NONE, &reset_flag,
      "Reset a given modem",
      NULL
    },
    { "factory-reset", 0, 0, G_OPTION_ARG_STRING, &factory_reset_str,
      "Reset a given modem to its factory state",
      "[CODE]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_get_option_group (void)
{
	GOptionGroup *group;

	/* Status options */
	group = g_option_group_new ("modem",
	                            "Modem options",
	                            "Show modem options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

gboolean
mmcli_modem_options_enabled (void)
{
    guint n_actions;

    n_actions = (info_flag +
                 monitor_state_flag +
                 enable_flag +
                 disable_flag +
                 reset_flag +
                 !!factory_reset_str);

    if (n_actions > 1) {
        g_printerr ("error: too many modem actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (monitor_state_flag)
        mmcli_force_async_operation ();

    if (info_flag)
        mmcli_force_sync_operation ();

    return !!n_actions;
}

static void
context_free (Context *ctx)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->modem)
        g_object_unref (ctx->modem);
    if (ctx->object)
        g_object_unref (ctx->object);
    g_free (ctx);
}

void
mmcli_modem_shutdown (void)
{
    context_free (ctx);
}

static void
cancelled (GCancellable *cancellable)
{
    mmcli_async_operation_done ();
}

static gchar *
prefix_newlines (const gchar *prefix,
                 const gchar *str)
{
    GString *prefixed_string = NULL;
    const gchar *line_start = str;
    const gchar *line_end;

    while ((line_end = strchr (line_start, '\n'))) {
        gssize line_length;

        line_length = line_end - line_start;
        if (line_start[line_length - 1] == '\r')
            line_length--;

        if (line_length > 0) {
            if (prefixed_string) {
                /* If not the first line, add the prefix */
                g_string_append_printf (prefixed_string,
                                        "\n%s", prefix);
            } else {
                prefixed_string = g_string_new ("");
            }

            g_string_append_len (prefixed_string,
                                 line_start,
                                 line_length);
        }

        line_start = line_end + 1;
    }

    return (prefixed_string ?
            g_string_free (prefixed_string, FALSE) :
            NULL);
}

static void
print_modem_info (void)
{
    GError *error = NULL;
    MMSim *sim;
    MMModemLock unlock_required;
    gchar *prefixed_revision;
    gchar *unlock;

    /* Not the best thing to do, as we may be doing _get() calls twice, but
     * easiest to maintain */
#define VALIDATE(str) (str ? str : "unknown")

    /* Strings with mixed properties */
    unlock_required = mm_modem_get_unlock_required (ctx->modem);
    switch (unlock_required) {
    case MM_MODEM_LOCK_NONE:
        unlock = g_strdup ("not required");
        break;
    case MM_MODEM_LOCK_UNKNOWN:
        unlock = g_strdup ("unknown");
        break;
    default:
        unlock = g_strdup_printf ("%s (%u retries)",
                                  mmcli_get_lock_string (unlock_required),
                                  mm_modem_get_unlock_retries (ctx->modem));
        break;
    }

    /* Rework possible multiline strings */
    prefixed_revision = prefix_newlines ("           |                 ",
                                         mm_modem_get_revision (ctx->modem));

    /* Global IDs */
    g_print ("\n"
             "%s (device id '%s')\n",
             VALIDATE (mm_modem_get_path (ctx->modem)),
             VALIDATE (mm_modem_get_device_identifier (ctx->modem)));

    /* Hardware related stuff */
    g_print ("  -------------------------\n"
             "  Hardware |   manufacturer: '%s'\n"
             "           |          model: '%s'\n"
             "           |       revision: '%s'\n"
             "           |   capabilities: '%s'\n"
             "           |   equipment id: '%s'\n",
             VALIDATE (mm_modem_get_manufacturer (ctx->modem)),
             VALIDATE (mm_modem_get_model (ctx->modem)),
             VALIDATE (prefixed_revision),
             VALIDATE (mm_modem_get_capabilities_string (mm_modem_get_modem_capabilities (ctx->modem))),
             VALIDATE (mm_modem_get_equipment_identifier (ctx->modem)));

    /* System related stuff */
    g_print ("  -------------------------\n"
             "  System   |         device: '%s'\n"
             "           |         driver: '%s'\n"
             "           |         plugin: '%s'\n",
             VALIDATE (mm_modem_get_device (ctx->modem)),
             VALIDATE (mm_modem_get_driver (ctx->modem)),
             VALIDATE (mm_modem_get_plugin (ctx->modem)));

    /* Status related stuff */
    g_print ("  -------------------------\n"
             "  Status   |         unlock: '%s'\n"
             "           |          state: '%s'\n",
             VALIDATE (unlock),
             VALIDATE (mmcli_get_state_string (mm_modem_get_state (ctx->modem))));

    /* SIM related stuff */
    sim = mm_modem_get_sim_sync (ctx->modem, NULL, &error);
    if (error) {
        g_warning ("Couldn't get SIM: '%s'", error->message);
        g_error_free (error);
    }
    if (sim) {
        g_print ("  -------------------------\n"
                 "  SIM      |          imsi : '%s'\n"
                 "           |            id : '%s'\n"
                 "           |   operator id : '%s'\n"
                 "           | operator name : '%s'\n",
                 VALIDATE (mm_sim_get_imsi (sim)),
                 VALIDATE (mm_sim_get_identifier (sim)),
                 VALIDATE (mm_sim_get_operator_identifier (sim)),
                 VALIDATE (mm_sim_get_operator_name (sim)));
        g_object_unref (sim);
    }

    g_print ("\n");

    g_free (prefixed_revision);
    g_free (unlock);
}

static void
enable_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't enable the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully enabled the modem\n");
}

static void
enable_ready (MMModem      *modem,
              GAsyncResult *result,
              gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_enable_finish (modem, result, &error);
    enable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
disable_process_reply (gboolean      result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disable the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disabled the modem\n");
}

static void
disable_ready (MMModem      *modem,
               GAsyncResult *result,
               gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_disable_finish (modem, result, &error);
    disable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
reset_process_reply (gboolean      result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't reset the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully reseted the modem\n");
}

static void
reset_ready (MMModem      *modem,
             GAsyncResult *result,
             gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_reset_finish (modem, result, &error);
    reset_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
factory_reset_process_reply (gboolean      result,
                             const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't reset the modem to factory state: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully reseted the modem to factory state\n");
}

static void
factory_reset_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_factory_reset_finish (modem, result, &error);
    factory_reset_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
state_changed (MMObject                 *modem,
               MMModemState              old_state,
               MMModemState              new_state,
               MMModemStateChangeReason  reason)
{
    g_print ("\t%s: State changed, '%s' --> '%s' (Reason: %s)\n",
             mm_object_get_path (modem),
             mmcli_get_state_string (old_state),
             mmcli_get_state_string (new_state),
             mmcli_get_state_reason_string (reason));
    fflush (stdout);
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result);
    ctx->modem = mm_object_get_modem (ctx->object);

    if (info_flag)
        g_assert_not_reached ();

    /* Request to monitor modems? */
    if (monitor_state_flag) {
        MMModemState current;

        g_signal_connect (ctx->modem,
                          "state-changed",
                          G_CALLBACK (state_changed),
                          NULL);

        current = mm_modem_get_state (ctx->modem);
        g_print ("\t%s: Initial state, '%s'\n",
                 mm_object_get_path (ctx->object),
                 mmcli_get_state_string (current));

        /* If we get cancelled, operation done */
        g_cancellable_connect (ctx->cancellable,
                               G_CALLBACK (cancelled),
                               NULL,
                               NULL);
        return;
    }

    /* Request to enable the modem? */
    if (enable_flag) {
        g_debug ("Asynchronously enabling modem...");
        mm_modem_enable (ctx->modem,
                         ctx->cancellable,
                         (GAsyncReadyCallback)enable_ready,
                         NULL);
        return;
    }

    /* Request to disable the modem? */
    if (disable_flag) {
        g_debug ("Asynchronously disabling modem...");
        mm_modem_disable (ctx->modem,
                          ctx->cancellable,
                          (GAsyncReadyCallback)disable_ready,
                          NULL);
        return;
    }

    /* Request to reset the modem? */
    if (reset_flag) {
        g_debug ("Asynchronously reseting modem...");
        mm_modem_reset (ctx->modem,
                        ctx->cancellable,
                        (GAsyncReadyCallback)reset_ready,
                        NULL);
        return;
    }

    /* Request to reset the modem to factory state? */
    if (factory_reset_str) {
        g_debug ("Asynchronously factory-reseting modem...");
        mm_modem_factory_reset (ctx->modem,
                                factory_reset_str,
                                ctx->cancellable,
                                (GAsyncReadyCallback)factory_reset_ready,
                                NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_run_asynchronous (GDBusConnection *connection,
                              GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper modem */
    mmcli_get_modem  (connection,
                      modem_str,
                      cancellable,
                      (GAsyncReadyCallback)get_modem_ready,
                      NULL);
}

void
mmcli_modem_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    if (monitor_state_flag)
        g_assert_not_reached ();

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection, modem_str);
    ctx->modem = mm_object_get_modem (ctx->object);

    /* Request to get info from modem? */
    if (info_flag) {
        g_debug ("Printing modem info...");
        print_modem_info ();
        return;
    }

    /* Request to enable the modem? */
    if (enable_flag) {
        gboolean result;

        g_debug ("Synchronously enabling modem...");
        result = mm_modem_enable_sync (ctx->modem, NULL, &error);
        enable_process_reply (result, error);
        return;
    }

    /* Request to disable the modem? */
    if (disable_flag) {
        gboolean result;

        g_debug ("Synchronously disabling modem...");
        result = mm_modem_disable_sync (ctx->modem, NULL, &error);
        disable_process_reply (result, error);
        return;
    }

    /* Request to reset the modem? */
    if (reset_flag) {
        gboolean result;

        g_debug ("Synchronously reseting modem...");
        result = mm_modem_reset_sync (ctx->modem, NULL, &error);
        reset_process_reply (result, error);
        return;
    }

    /* Request to reset the modem to factory state? */
    if (factory_reset_str) {
        gboolean result;

        g_debug ("Synchronously factory-reseting modem...");
        result = mm_modem_factory_reset_sync (ctx->modem,
                                              factory_reset_str,
                                              NULL,
                                              &error);
        factory_reset_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
