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
    MMManager *manager;
    GCancellable *cancellable;
    MMObject *object;
    MMModem *modem;
} Context;
static Context *ctx;

/* Options */
static gboolean info_flag; /* set when no action found */
static gboolean monitor_state_flag;
static gboolean enable_flag;
static gboolean disable_flag;
static gboolean reset_flag;
static gchar *factory_reset_str;
static gboolean list_bearers_flag;
static gchar *create_bearer_str;
static gchar *delete_bearer_str;

static GOptionEntry entries[] = {
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
    { "list-bearers", 0, 0, G_OPTION_ARG_NONE, &list_bearers_flag,
      "List packet data bearers available in a given modem",
      NULL
    },
    { "create-bearer", 0, 0, G_OPTION_ARG_STRING, &create_bearer_str,
      "Create a new packet data bearer in a given modem",
      "[\"key=value,...\"]"
    },
    { "delete-bearer", 0, 0, G_OPTION_ARG_STRING, &delete_bearer_str,
      "Delete a data bearer from a given modem",
      "[PATH]"
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

    n_actions = (monitor_state_flag +
                 enable_flag +
                 disable_flag +
                 reset_flag +
                 list_bearers_flag +
                 !!create_bearer_str +
                 !!delete_bearer_str +
                 !!factory_reset_str);

    if (n_actions == 0 && mmcli_get_common_modem_string ()) {
        /* default to info */
        info_flag = TRUE;
        n_actions++;
    }

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
    if (ctx->manager)
        g_object_unref (ctx->manager);
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
print_bearer_short_info (MMBearer *bearer)
{
    g_print ("\t%s\n",
             mm_bearer_get_path (bearer));
}

static void
print_modem_info (void)
{
    GError *error = NULL;
    MMSim *sim;
    MMModemLock unlock_required;
    gchar *prefixed_revision;
    gchar *unlock;
    gchar *capabilities_string;
    gchar *access_technologies_string;

    /* Not the best thing to do, as we may be doing _get() calls twice, but
     * easiest to maintain */
#undef VALIDATE
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

    /* Strings in heap */
    capabilities_string = mm_modem_get_capabilities_string (
        mm_modem_get_modem_capabilities (ctx->modem));
    access_technologies_string = mm_modem_get_access_technologies_string (
        mm_modem_get_access_technologies (ctx->modem));

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
             VALIDATE (capabilities_string),
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
             "           |          state: '%s'\n"
             "           |    access tech: '%s'\n",
             VALIDATE (unlock),
             VALIDATE (mmcli_get_state_string (mm_modem_get_state (ctx->modem))),
             VALIDATE (access_technologies_string));

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

    g_free (access_technologies_string);
    g_free (capabilities_string);
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
list_bearers_process_reply (GList        *result,
                            const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't list bearers: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("\n");
    if (!result) {
        g_print ("No bearers were found\n");
    } else {
        GList *l;

        g_print ("Found %u bearers:\n", g_list_length (result));
        for (l = result; l; l = g_list_next (l)) {
            MMBearer *bearer = MM_BEARER (l->data);

            g_print ("\n");
            print_bearer_short_info (bearer);
            g_object_unref (bearer);
        }
        g_list_free (result);
    }
}

static void
list_bearers_ready (MMModem      *modem,
                    GAsyncResult *result,
                    gpointer      nothing)
{
    GList *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_list_bearers_finish (modem, result, &error);
    list_bearers_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
create_bearer_process_reply (MMBearer     *bearer,
                             const GError *error)
{
    if (!bearer) {
        g_printerr ("error: couldn't create new bearer: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully created new bearer in modem:\n");
    print_bearer_short_info (bearer);
    g_object_unref (bearer);
}

static void
create_bearer_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    MMBearer *bearer;
    GError *error = NULL;

    bearer = mm_modem_create_bearer_finish (modem, result, &error);
    create_bearer_process_reply (bearer, error);

    mmcli_async_operation_done ();
}

static void
create_bearer_parse_known_input (const gchar  *input,
                                 gchar       **apn,
                                 gchar       **ip_type,
                                 gboolean     *allow_roaming,
                                 gchar       **user,
                                 gchar       **password,
                                 gchar       **number)
{
    gchar **words;
    gchar *key;
    gchar *value;
    guint i;

    /* Expecting input as:
     *   key1=string,key2=true,key3=false...
     * */

    words = g_strsplit_set (input, ",= ", -1);
    if (!words)
        return;

    i = 0;
    key = words[i];
    while (key) {
        value = words[++i];
        if (!value) {
            g_printerr ("error: invalid properties string, no value for key '%s'\n", key);
            exit (EXIT_FAILURE);
        }

        if (g_str_equal (key, MM_BEARER_PROPERTY_APN)) {
            g_debug ("APN: %s", value);
            *apn = value;
        } else if (g_str_equal (key, MM_BEARER_PROPERTY_IP_TYPE)) {
            g_debug ("IP type: %s", value);
            *ip_type = value;
        } else if (g_str_equal (key, MM_BEARER_PROPERTY_ALLOW_ROAMING)) {
            if (!g_ascii_strcasecmp (value, "true") ||
                g_str_equal (value, "1")) {
                g_debug ("Roaming: allowed");
                *allow_roaming = TRUE;
            } else if (!g_ascii_strcasecmp (value, "false") ||
                g_str_equal (value, "0")) {
                g_debug ("Roaming: forbidden");
                *allow_roaming = FALSE;
            } else
                g_printerr ("error: invalid value '%s' for boolean property '%s'",
                            value, key);
            g_free (value);
        } else if (g_str_equal (key, MM_BEARER_PROPERTY_USER)) {
            g_debug ("User: %s", value);
            *user = value;
        } else if (g_str_equal (key, MM_BEARER_PROPERTY_PASSWORD)) {
            g_debug ("Password: %s", value);
            *password = value;
        } else if (g_str_equal (key, MM_BEARER_PROPERTY_NUMBER)) {
            g_debug ("Number: %s", value);
            *number = value;
        } else {
            g_printerr ("error: invalid key '%s' in properties string", key);
            g_free (value);
        }

        g_free (key);
        key = words[++i];
    }

    g_free (words);
}

static void
delete_bearer_process_reply (gboolean      result,
                             const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't delete bearer: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully deleted bearer from modem\n");
}

static void
delete_bearer_ready (MMModem      *modem,
                     GAsyncResult *result,
                     gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_delete_bearer_finish (modem, result, &error);
    delete_bearer_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
state_changed (MMModem                  *modem,
               MMModemState              old_state,
               MMModemState              new_state,
               MMModemStateChangeReason  reason)
{
    g_print ("\t%s: State changed, '%s' --> '%s' (Reason: %s)\n",
             mm_modem_get_path (modem),
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
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
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

    /* Request to list bearers? */
    if (list_bearers_flag) {
        g_debug ("Asynchronously listing bearers in modem...");
        mm_modem_list_bearers (ctx->modem,
                               ctx->cancellable,
                               (GAsyncReadyCallback)list_bearers_ready,
                               NULL);
        return;
    }

    /* Request to create a new bearer? */
    if (create_bearer_str) {
        gchar *apn = NULL;
        gchar *ip_type = NULL;
        gchar *user = NULL;
        gchar *password = NULL;
        gchar *number = NULL;
        gboolean allow_roaming = TRUE;

        create_bearer_parse_known_input (create_bearer_str,
                                         &apn,
                                         &ip_type,
                                         &allow_roaming,
                                         &user,
                                         &password,
                                         &number);

        g_debug ("Asynchronously creating new bearer in modem...");
        mm_modem_create_bearer (ctx->modem,
                                ctx->cancellable,
                                (GAsyncReadyCallback)create_bearer_ready,
                                NULL,
                                MM_BEARER_PROPERTY_APN,           apn,
                                MM_BEARER_PROPERTY_IP_TYPE,       ip_type,
                                MM_BEARER_PROPERTY_USER,          user,
                                MM_BEARER_PROPERTY_PASSWORD,      password,
                                MM_BEARER_PROPERTY_NUMBER,        number,
                                MM_BEARER_PROPERTY_ALLOW_ROAMING, allow_roaming,
                                NULL);

        g_free (apn);
        g_free (ip_type);
        g_free (user);
        g_free (password);
        g_free (number);
        return;
    }

    /* Request to delete a given bearer? */
    if (delete_bearer_str) {
        mm_modem_delete_bearer (ctx->modem,
                                delete_bearer_str,
                                ctx->cancellable,
                                (GAsyncReadyCallback)delete_bearer_ready,
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
                      mmcli_get_common_modem_string (),
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
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
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

    /* Request to list the bearers? */
    if (list_bearers_flag) {
        GList *result;

        g_debug ("Synchronously listing bearers...");
        result = mm_modem_list_bearers_sync (ctx->modem, NULL, &error);
        list_bearers_process_reply (result, error);
        return;
    }

    /* Request to create a new bearer? */
    if (create_bearer_str) {
        gchar *apn = NULL;
        gchar *ip_type = NULL;
        gchar *user = NULL;
        gchar *password = NULL;
        gchar *number = NULL;
        gboolean allow_roaming = TRUE;
        MMBearer *bearer;

        create_bearer_parse_known_input (create_bearer_str,
                                         &apn,
                                         &ip_type,
                                         &allow_roaming,
                                         &user,
                                         &password,
                                         &number);

        g_debug ("Synchronously creating new bearer in modem...");
        bearer = mm_modem_create_bearer_sync (
            ctx->modem,
            NULL,
            &error,
            MM_BEARER_PROPERTY_APN,           apn,
            MM_BEARER_PROPERTY_IP_TYPE,       ip_type,
            MM_BEARER_PROPERTY_USER,          user,
            MM_BEARER_PROPERTY_PASSWORD,      password,
            MM_BEARER_PROPERTY_NUMBER,        number,
            MM_BEARER_PROPERTY_ALLOW_ROAMING, allow_roaming,
            NULL);

        g_free (apn);
        g_free (ip_type);
        g_free (user);
        g_free (password);
        g_free (number);

        create_bearer_process_reply (bearer, error);
        return;
    }

    /* Request to delete a given bearer? */
    if (delete_bearer_str) {
        gboolean result;

        result = mm_modem_delete_bearer_sync (ctx->modem,
                                              delete_bearer_str,
                                              NULL,
                                              &error);

        delete_bearer_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
