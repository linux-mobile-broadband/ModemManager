/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control call status & access information from the command line
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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
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

/* Context */
typedef struct {
    MMManager *manager;
    MMObject *object;
    GCancellable *cancellable;
    MMCall *call;
} Context;
static Context *ctx;

/* Options */
static gboolean info_flag; /* set when no action found */
static gboolean start_flag;
static gboolean accept_flag;
static gboolean hangup_flag;
static gchar *dtmf_request;

static GOptionEntry entries[] = {
    { "start", 0, 0, G_OPTION_ARG_NONE, &start_flag,
      "Start the call.",
      NULL,
    },
    { "accept", 0, 0, G_OPTION_ARG_NONE, &accept_flag,
      "Accept the incoming call",
      NULL,
    },
    { "hangup", 0, 0, G_OPTION_ARG_NONE, &hangup_flag,
      "Hang up the call",
      NULL,
    },
    { "send-dtmf", 0, 0, G_OPTION_ARG_STRING, &dtmf_request,
      "Send specified DTMF tone",
      "[0-9A-D*#]"
    },
    { NULL }
};

GOptionGroup *
mmcli_call_get_option_group (void)
{
    GOptionGroup *group;

    /* Status options */
    group = g_option_group_new ("call",
                                "Call options",
                                "Show call options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_call_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (start_flag +
                 accept_flag +
                 hangup_flag +
                 !!dtmf_request);

    if (n_actions == 0 && mmcli_get_common_call_string ()) {
        /* default to info */
        info_flag = TRUE;
        n_actions++;
    }

    if (n_actions > 1) {
        g_printerr ("error: too many call actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (info_flag)
        mmcli_force_sync_operation ();

    checked = TRUE;
    return !!n_actions;
}

static void
context_free (Context *ctx)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->call)
        g_object_unref (ctx->call);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

void
mmcli_call_shutdown (void)
{
    context_free (ctx);
}

static void
print_call_info (MMCall *call)
{
    /* Not the best thing to do, as we may be doing _get() calls twice, but
     * easiest to maintain */
#undef VALIDATE
#define VALIDATE(str) (str ? str : "unknown")

    g_print ("CALL '%s'\n", mm_call_get_path (call));
    g_print ("  -------------------------------\n"
             "  Global     |          number: '%s'\n", VALIDATE (mm_call_get_number (call)));
    g_print ("             |       direction: '%s'\n", mm_call_direction_get_string (mm_call_get_direction (call)) );

    g_print ("  -------------------------------\n"
             "  Properties |           state: '%s'\n", mm_call_state_get_string (mm_call_get_state (call)));

    if (mm_call_get_state_reason(call) != MM_CALL_STATE_REASON_UNKNOWN)
        g_print ("             |    state reason: '%s'\n",
                 mm_call_state_reason_get_string(mm_call_get_state_reason (call)));
}

static void
start_process_reply (gboolean      result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't start the call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully started the call\n");
}

static void
start_ready (MMCall        *call,
             GAsyncResult *result,
             gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_call_start_finish (call, result, &error);
    start_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
accept_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't accept the call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully accepted the call\n");
}

static void
accept_ready (MMCall        *call,
              GAsyncResult *result,
              gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_call_accept_finish (call, result, &error);
    accept_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
hangup_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't hang up the call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully hung up the call\n");
}

static void
hangup_ready (MMCall        *call,
              GAsyncResult *result,
              gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_call_hangup_finish (call, result, &error);
    hangup_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
send_dtmf_process_reply (gboolean      result,
                         const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't send dtmf to call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully send dtmf\n");
}

static void
send_dtmf_ready (MMCall       *call,
                 GAsyncResult *result,
                 gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_call_send_dtmf_finish (call, result, &error);
    send_dtmf_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_call_ready (GObject      *source,
                GAsyncResult *result,
                gpointer      none)
{
    ctx->call = mmcli_get_call_finish (result,
                                       &ctx->manager,
                                       &ctx->object);
    /* Setup operation timeout */
    mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->call));

    if (info_flag)
        g_assert_not_reached ();

    /* Requesting to start the call? */
    if (start_flag) {
        mm_call_start (ctx->call,
                       ctx->cancellable,
                       (GAsyncReadyCallback)start_ready,
                       NULL);
        return;
    }

    /* Requesting to accept the call? */
    if (accept_flag) {
        mm_call_accept (ctx->call,
                        ctx->cancellable,
                        (GAsyncReadyCallback)accept_ready,
                        NULL);
        return;
    }

    /* Requesting to hangup the call? */
    if (hangup_flag) {
        mm_call_hangup (ctx->call,
                        ctx->cancellable,
                        (GAsyncReadyCallback)hangup_ready,
                        NULL);
        return;
    }

    /* Requesting to send dtmf the call? */
    if (dtmf_request) {
        mm_call_send_dtmf (ctx->call,
                           dtmf_request,
                           ctx->cancellable,
                           (GAsyncReadyCallback)send_dtmf_ready,
                           NULL);
        return;
    }



    g_warn_if_reached ();
}

void
mmcli_call_run_asynchronous (GDBusConnection *connection,
                             GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper call */
    mmcli_get_call (connection,
                    mmcli_get_common_call_string (),
                    cancellable,
                    (GAsyncReadyCallback)get_call_ready,
                    NULL);
}

void
mmcli_call_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->call = mmcli_get_call_sync (connection,
                                     mmcli_get_common_call_string (),
                                     &ctx->manager,
                                     &ctx->object);

    /* Setup operation timeout: 2 minutes */
    g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (ctx->call), 2 * 60 * 1000);

    /* Request to get info from call? */
    if (info_flag) {
        g_debug ("Printing call info...");
        print_call_info (ctx->call);
        return;
    }

    /* Requesting to start the call? */
    if (start_flag) {
        gboolean operation_result;

        operation_result = mm_call_start_sync (ctx->call,
                                               NULL,
                                               &error);
        start_process_reply (operation_result, error);
        return;
    }

    /* Requesting to accept the call? */
    if (accept_flag) {
        gboolean operation_result;

        operation_result = mm_call_accept_sync (ctx->call,
                                                NULL,
                                                &error);
        accept_process_reply (operation_result, error);
        return;
    }

    /* Requesting to hangup the call? */
    if (hangup_flag) {
        gboolean operation_result;

        operation_result = mm_call_hangup_sync (ctx->call,
                                                NULL,
                                                &error);
        hangup_process_reply (operation_result, error);
        return;
    }

    /* Requesting to send a dtmf? */
    if (dtmf_request) {
        gboolean operation_result;

        operation_result = mm_call_send_dtmf_sync (ctx->call,
                                                   dtmf_request,
                                                   NULL,
                                                   &error);
        send_dtmf_process_reply (operation_result, error);
        return;
    }

    g_warn_if_reached ();
}
