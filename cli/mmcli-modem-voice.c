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
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
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

/* Context */
typedef struct {
    GDBusConnection *connection;
    MMManager *manager;
    GCancellable *cancellable;
    MMObject *object;
    MMModemVoice *modem_voice;
} Context;
static Context *ctx;

/* Options */
static gboolean status_flag;
static gboolean list_flag;
static gchar *create_str;
static gchar *delete_str;
static gboolean hold_and_accept_flag;
static gboolean hangup_and_accept_flag;
static gboolean hangup_all_flag;
static gboolean transfer_flag;
static gboolean call_waiting_enable_flag;
static gboolean call_waiting_disable_flag;
static gboolean call_waiting_query_flag;

static GOptionEntry entries[] = {
    { "voice-status", 0, 0, G_OPTION_ARG_NONE, &status_flag,
      "Show status of voice support.",
      NULL
    },
    { "voice-list-calls", 0, 0, G_OPTION_ARG_NONE, &list_flag,
      "List calls available in a given modem",
      NULL
    },
    { "voice-create-call", 0, 0, G_OPTION_ARG_STRING, &create_str,
      "Create a new call in a given modem",
      "[\"key=value,...\"]"
    },
    { "voice-delete-call", 0, 0, G_OPTION_ARG_STRING, &delete_str,
      "Delete a call from a given modem",
      "[PATH|INDEX]"
    },
    { "voice-hold-and-accept", 0, 0, G_OPTION_ARG_NONE, &hold_and_accept_flag,
      "Places all active calls in hold and accepts the next waiting or held call",
      NULL
    },
    { "voice-hangup-and-accept", 0, 0, G_OPTION_ARG_NONE, &hangup_and_accept_flag,
      "Hangs up all active calls and accepts the next waiting or held call",
      NULL
    },
    { "voice-hangup-all", 0, 0, G_OPTION_ARG_NONE, &hangup_all_flag,
      "Hangs up all ongoing (active, waiting, held) calls",
      NULL
    },
    { "voice-transfer", 0, 0, G_OPTION_ARG_NONE, &transfer_flag,
      "Joins active and held calls and disconnects from them",
      NULL
    },
    { "voice-enable-call-waiting", 0, 0, G_OPTION_ARG_NONE, &call_waiting_enable_flag,
      "Enables the call waiting network service",
      NULL
    },
    { "voice-disable-call-waiting", 0, 0, G_OPTION_ARG_NONE, &call_waiting_disable_flag,
      "Disables the call waiting network service",
      NULL
    },
    { "voice-query-call-waiting", 0, 0, G_OPTION_ARG_NONE, &call_waiting_query_flag,
      "Queries the status of the call waiting network service",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_voice_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("voice",
                                "Voice options:",
                                "Show Voice options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_voice_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (status_flag +
                 list_flag +
                 !!create_str +
                 !!delete_str +
                 hold_and_accept_flag +
                 hangup_and_accept_flag +
                 hangup_all_flag +
                 transfer_flag +
                 call_waiting_enable_flag +
                 call_waiting_disable_flag +
                 call_waiting_query_flag);

    if (n_actions > 1) {
        g_printerr ("error: too many Voice actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (status_flag)
        mmcli_force_sync_operation ();

    checked = TRUE;
    return !!n_actions;
}

static void
context_free (void)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->modem_voice)
        g_object_unref (ctx->modem_voice);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->connection)
        g_object_unref (ctx->connection);
    g_free (ctx);
}

static void
ensure_modem_voice (void)
{
    if (!ctx->modem_voice) {
        g_printerr ("error: modem has no voice capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_voice_shutdown (void)
{
    context_free ();
}

static MMCallProperties *
build_call_properties_from_input (const gchar *properties_string)
{
    GError *error = NULL;
    MMCallProperties *properties;

    properties = mm_call_properties_new_from_string (properties_string, &error);
    if (!properties) {
        g_printerr ("error: cannot parse properties string: '%s'\n", error->message);
        exit (EXIT_FAILURE);
    }

    return properties;
}

static void
print_voice_status (void)
{
    mmcli_output_string (MMC_F_VOICE_EMERGENCY_ONLY, mm_modem_voice_get_emergency_only (ctx->modem_voice) ? "yes" : "no");
    mmcli_output_dump ();
}

static void
output_call_info (MMCall *call)
{
    gchar *extra;

    extra = g_strdup_printf ("%s (%s)",
                             mm_call_direction_get_string (mm_call_get_direction (call)),
                             mm_call_state_get_string (mm_call_get_state (call)));
    mmcli_output_listitem (MMC_F_CALL_LIST_DBUS_PATH,
                           "    ",
                           mm_call_get_path (call),
                           extra);
    g_free (extra);
}

static void
call_waiting_query_process_reply (const GError *error,
                                  gboolean      status)
{
    if (error) {
        g_printerr ("error: couldn't query call waiting network service status: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("call waiting service is %s\n", status ? "enabled" : "disabled");
}

static void
call_waiting_query_ready (MMModemVoice *modem,
                          GAsyncResult *result,
                          gpointer     nothing)
{
    GError   *error = NULL;
    gboolean  status = FALSE;

    mm_modem_voice_call_waiting_query_finish (modem, result, &status, &error);
    call_waiting_query_process_reply (error, status);

    mmcli_async_operation_done ();
}

static void
call_waiting_setup_process_reply (const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't setup call waiting network service: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("operation successful\n");
}

static void
call_waiting_setup_ready (MMModemVoice *modem,
                          GAsyncResult *result,
                          gpointer     nothing)
{
    GError *error = NULL;

    mm_modem_voice_call_waiting_setup_finish (modem, result, &error);
    call_waiting_setup_process_reply (error);

    mmcli_async_operation_done ();
}

static void
transfer_process_reply (const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't hangup all: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("operation successful\n");
}

static void
transfer_ready (MMModemVoice *modem,
                GAsyncResult *result,
                gpointer     nothing)
{
    GError *error = NULL;

    mm_modem_voice_transfer_finish (modem, result, &error);
    transfer_process_reply (error);

    mmcli_async_operation_done ();
}

static void
hangup_all_process_reply (const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't hangup all: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("operation successful\n");
}

static void
hangup_all_ready (MMModemVoice *modem,
                  GAsyncResult *result,
                  gpointer     nothing)
{
    GError *error = NULL;

    mm_modem_voice_hangup_all_finish (modem, result, &error);
    hangup_all_process_reply (error);

    mmcli_async_operation_done ();
}

static void
hangup_and_accept_process_reply (const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't hangup and accept: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("operation successful\n");
}

static void
hangup_and_accept_ready (MMModemVoice *modem,
                         GAsyncResult *result,
                         gpointer     nothing)
{
    GError *error = NULL;

    mm_modem_voice_hangup_and_accept_finish (modem, result, &error);
    hangup_and_accept_process_reply (error);

    mmcli_async_operation_done ();
}

static void
hold_and_accept_process_reply (const GError *error)
{
    if (error) {
        g_printerr ("error: couldn't hold and accept: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("operation successful\n");
}

static void
hold_and_accept_ready (MMModemVoice *modem,
                         GAsyncResult *result,
                         gpointer     nothing)
{
    GError *error = NULL;

    mm_modem_voice_hold_and_accept_finish (modem, result, &error);
    hold_and_accept_process_reply (error);

    mmcli_async_operation_done ();
}

static void
list_process_reply (GList        *result,
                    const GError *error)
{
    GList *l;

    if (error) {
        g_printerr ("error: couldn't list call: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    for (l = result; l; l = g_list_next (l))
        output_call_info (MM_CALL (l->data));
    mmcli_output_list_dump (MMC_F_CALL_LIST_DBUS_PATH);
}

static void
list_ready (MMModemVoice *modem,
            GAsyncResult *result,
            gpointer     nothing)
{
    GList *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_voice_list_calls_finish (modem, result, &error);
    list_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
create_process_reply (MMCall        *call,
                      const GError *error)
{
    if (!call) {
        g_printerr ("error: couldn't create new call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully created new call: %s\n", mm_call_get_path (call));
    g_object_unref (call);
}

static void
create_ready (MMModemVoice  *modem,
              GAsyncResult  *result,
              gpointer      nothing)
{
    MMCall *call;
    GError *error = NULL;

    call = mm_modem_voice_create_call_finish (modem, result, &error);
    create_process_reply (call, error);

    mmcli_async_operation_done ();
}

static void
delete_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't delete call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully deleted call from modem\n");
}

static void
delete_ready (MMModemVoice *modem,
              GAsyncResult *result,
              gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_voice_delete_call_finish (modem, result, &error);
    delete_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_call_to_delete_ready (GDBusConnection *connection,
                          GAsyncResult *res)
{
    MMCall *call;
    MMObject *obj = NULL;

    call = mmcli_get_call_finish (res, NULL, &obj);
    if (!g_str_equal (mm_object_get_path (obj), mm_modem_voice_get_path (ctx->modem_voice))) {
        g_printerr ("error: call '%s' not owned by modem '%s'",
                    mm_call_get_path (call),
                    mm_modem_voice_get_path (ctx->modem_voice));
        exit (EXIT_FAILURE);
    }

    mm_modem_voice_delete_call (ctx->modem_voice,
                                mm_call_get_path (call),
                                ctx->cancellable,
                                (GAsyncReadyCallback)delete_ready,
                                NULL);
    g_object_unref (call);
    g_object_unref (obj);
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_voice = mm_object_get_modem_voice (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_voice)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_voice));

    ensure_modem_voice ();

    if (status_flag)
        g_assert_not_reached ();

    /* Request to list call? */
    if (list_flag) {
        g_debug ("Asynchronously listing calls in modem...");
        mm_modem_voice_list_calls (ctx->modem_voice,
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)list_ready,
                                   NULL);
        return;
    }

    /* Request to create a new call? */
    if (create_str) {
        MMCallProperties *properties;

        properties = build_call_properties_from_input (create_str);
        g_debug ("Asynchronously creating new call in modem...");
        mm_modem_voice_create_call (ctx->modem_voice,
                                    properties,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)create_ready,
                                    NULL);
        g_object_unref (properties);
        return;
    }

    /* Request to delete a given call? */
    if (delete_str) {
        mmcli_get_call (ctx->connection,
                        delete_str,
                        ctx->cancellable,
                        (GAsyncReadyCallback)get_call_to_delete_ready,
                        NULL);
        return;
    }

    /* Request to hold and accept? */
    if (hold_and_accept_flag) {
        g_debug ("Asynchronously holding and accepting next call...");
        mm_modem_voice_hold_and_accept (ctx->modem_voice,
                                          ctx->cancellable,
                                          (GAsyncReadyCallback)hold_and_accept_ready,
                                          NULL);
        return;
    }

    /* Request to hangup and accept? */
    if (hangup_and_accept_flag) {
        g_debug ("Asynchronously hanging up and accepting next call...");
        mm_modem_voice_hangup_and_accept (ctx->modem_voice,
                                          ctx->cancellable,
                                          (GAsyncReadyCallback)hangup_and_accept_ready,
                                          NULL);
        return;
    }

    /* Request to hangup all? */
    if (hangup_all_flag) {
        g_debug ("Asynchronously hanging up all calls...");
        mm_modem_voice_hangup_all (ctx->modem_voice,
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)hangup_all_ready,
                                   NULL);
        return;
    }

    /* Request to transfer? */
    if (transfer_flag) {
        g_debug ("Asynchronously transferring calls...");
        mm_modem_voice_transfer (ctx->modem_voice,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)transfer_ready,
                                 NULL);
        return;
    }

    /* Request to enable call waiting? */
    if (call_waiting_enable_flag) {
        g_debug ("Asynchronously enabling call waiting...");
        mm_modem_voice_call_waiting_setup (ctx->modem_voice,
                                           TRUE,
                                           ctx->cancellable,
                                           (GAsyncReadyCallback)call_waiting_setup_ready,
                                           NULL);
        return;
    }

    /* Request to disable call waiting? */
    if (call_waiting_disable_flag) {
        g_debug ("Asynchronously enabling call waiting...");
        mm_modem_voice_call_waiting_setup (ctx->modem_voice,
                                           FALSE,
                                           ctx->cancellable,
                                           (GAsyncReadyCallback)call_waiting_setup_ready,
                                           NULL);
        return;
    }

    /* Request to query call waiting? */
    if (call_waiting_query_flag) {
        g_debug ("Asynchronously querying call waiting status...");
        mm_modem_voice_call_waiting_query (ctx->modem_voice,
                                           ctx->cancellable,
                                           (GAsyncReadyCallback)call_waiting_query_ready,
                                           NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_voice_run_asynchronous (GDBusConnection *connection,
                                    GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);
    ctx->connection = g_object_ref (connection);

    /* Get proper modem */
    mmcli_get_modem (connection,
                     mmcli_get_common_modem_string (),
                     cancellable,
                     (GAsyncReadyCallback)get_modem_ready,
                     NULL);
}

void
mmcli_modem_voice_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_voice = mm_object_get_modem_voice (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_voice)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_voice));

    ensure_modem_voice ();

    /* Request to get voice status? */
    if (status_flag) {
        g_debug ("Printing voice status...");
        print_voice_status ();
        return;
    }

    /* Request to list the call? */
    if (list_flag) {
        GList *result;

        g_debug ("Synchronously listing call...");
        result = mm_modem_voice_list_calls_sync (ctx->modem_voice, NULL, &error);
        list_process_reply (result, error);
        return;
    }

    /* Request to create a new call? */
    if (create_str) {
        MMCall *call;
        MMCallProperties *properties;

        properties = build_call_properties_from_input (create_str);

        g_debug ("Synchronously creating new call in modem...");
        call = mm_modem_voice_create_call_sync (ctx->modem_voice,
                                                properties,
                                                NULL,
                                                &error);
        g_object_unref (properties);

        create_process_reply (call, error);
        return;
    }

    /* Request to delete a given call? */
    if (delete_str) {
        gboolean result;
        MMCall *call;
        MMObject *obj = NULL;

        call = mmcli_get_call_sync (connection,
                                    delete_str,
                                    NULL,
                                    &obj);
        if (!g_str_equal (mm_object_get_path (obj), mm_modem_voice_get_path (ctx->modem_voice))) {
            g_printerr ("error: call '%s' not owned by modem '%s'",
                        mm_call_get_path (call),
                        mm_modem_voice_get_path (ctx->modem_voice));
            exit (EXIT_FAILURE);
        }

        result = mm_modem_voice_delete_call_sync (ctx->modem_voice,
                                                  mm_call_get_path (call),
                                                  NULL,
                                                  &error);
        g_object_unref (call);
        g_object_unref (obj);

        delete_process_reply (result, error);
        return;
    }

    /* Request to hold and accept? */
    if (hold_and_accept_flag) {
        g_debug ("Synchronously holding and accepting call...");
        mm_modem_voice_hold_and_accept_sync (ctx->modem_voice, NULL, &error);
        hold_and_accept_process_reply (error);
        return;
    }

    /* Request to hangup and accept? */
    if (hangup_and_accept_flag) {
        g_debug ("Synchronously hanging up and accepting call...");
        mm_modem_voice_hangup_and_accept_sync (ctx->modem_voice, NULL, &error);
        hangup_and_accept_process_reply (error);
        return;
    }

    /* Request to hangup all? */
    if (hangup_all_flag) {
        g_debug ("Synchronously hanging up all calls...");
        mm_modem_voice_hangup_all_sync (ctx->modem_voice, NULL, &error);
        hangup_all_process_reply (error);
        return;
    }

    /* Request to transfer? */
    if (transfer_flag) {
        g_debug ("Synchronously transferring calls...");
        mm_modem_voice_transfer_sync (ctx->modem_voice, NULL, &error);
        transfer_process_reply (error);
        return;
    }

    /* Request to enable call waiting? */
    if (call_waiting_enable_flag) {
        g_debug ("Synchronously enabling call waiting...");
        mm_modem_voice_call_waiting_setup_sync (ctx->modem_voice, TRUE, NULL, &error);
        call_waiting_setup_process_reply (error);
        return;
    }

    /* Request to disable call waiting? */
    if (call_waiting_disable_flag) {
        g_debug ("Synchronously disabling call waiting...");
        mm_modem_voice_call_waiting_setup_sync (ctx->modem_voice, FALSE, NULL, &error);
        call_waiting_setup_process_reply (error);
        return;
    }

    /* Request to query call waiting? */
    if (call_waiting_query_flag) {
        gboolean status = FALSE;

        g_debug ("Synchronously querying call waiting status...");
        mm_modem_voice_call_waiting_query_sync (ctx->modem_voice, NULL, &status, &error);
        call_waiting_query_process_reply (error, status);
        return;
    }

    g_warn_if_reached ();
}
