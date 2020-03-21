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
#include "mmcli-output.h"

/* Context */
typedef struct {
    MMManager *manager;
    MMObject *object;
    GCancellable *cancellable;
    MMCall *call;
} Context;
static Context *ctx;

/* Options */
static gboolean  info_flag; /* set when no action found */
static gboolean  start_flag;
static gboolean  accept_flag;
static gchar    *deflect_str;
static gboolean  join_multiparty_flag;
static gboolean  leave_multiparty_flag;
static gboolean  hangup_flag;
static gchar    *dtmf_request;

static GOptionEntry entries[] = {
    { "start", 0, 0, G_OPTION_ARG_NONE, &start_flag,
      "Start the call.",
      NULL,
    },
    { "accept", 0, 0, G_OPTION_ARG_NONE, &accept_flag,
      "Accept the incoming call",
      NULL,
    },
    { "deflect", 0, 0, G_OPTION_ARG_STRING, &deflect_str,
      "Deflect the incoming call",
      "[NUMBER]",
    },
    { "join-multiparty", 0, 0, G_OPTION_ARG_NONE, &join_multiparty_flag,
      "Join multiparty call",
      NULL,
    },
    { "leave-multiparty", 0, 0, G_OPTION_ARG_NONE, &leave_multiparty_flag,
      "Leave multiparty call",
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
                                "Call options:",
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
                 !!deflect_str +
                 join_multiparty_flag +
                 leave_multiparty_flag +
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
context_free (void)
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
    context_free ();
}

static void
print_call_info (MMCall *call)
{
    MMCallAudioFormat *audio_format;
    const gchar       *encoding = NULL;
    const gchar       *resolution = NULL;
    gchar             *rate = NULL;

    audio_format = mm_call_peek_audio_format (call);

    mmcli_output_string (MMC_F_CALL_GENERAL_DBUS_PATH,       mm_call_get_path (call));
    mmcli_output_string (MMC_F_CALL_PROPERTIES_NUMBER,       mm_call_get_number (call));
    mmcli_output_string (MMC_F_CALL_PROPERTIES_DIRECTION,    mm_call_direction_get_string (mm_call_get_direction (call)));
    mmcli_output_string (MMC_F_CALL_PROPERTIES_MULTIPARTY,   mm_call_get_multiparty (call) ? "yes" : "no");
    mmcli_output_string (MMC_F_CALL_PROPERTIES_STATE,        mm_call_state_get_string (mm_call_get_state (call)));
    mmcli_output_string (MMC_F_CALL_PROPERTIES_STATE_REASON, mm_call_state_reason_get_string (mm_call_get_state_reason (call)));
    mmcli_output_string (MMC_F_CALL_PROPERTIES_AUDIO_PORT,   mm_call_get_audio_port (call));

    if (audio_format) {
        rate       = g_strdup_printf ("%u", mm_call_audio_format_get_rate (audio_format));
        encoding   = mm_call_audio_format_get_encoding (audio_format);
        resolution = mm_call_audio_format_get_resolution (audio_format);
    }

    mmcli_output_string      (MMC_F_CALL_AUDIO_FORMAT_ENCODING,   encoding);
    mmcli_output_string      (MMC_F_CALL_AUDIO_FORMAT_RESOLUTION, resolution);
    mmcli_output_string_take (MMC_F_CALL_AUDIO_FORMAT_RATE,       rate);

    mmcli_output_dump ();
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
deflect_process_reply (gboolean      result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't deflect the call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully deflected the call\n");
}

static void
deflect_ready (MMCall        *call,
               GAsyncResult *result,
               gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_call_deflect_finish (call, result, &error);
    deflect_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
join_multiparty_process_reply (gboolean      result,
                               const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't join multiparty call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully joined multiparty call\n");
}

static void
join_multiparty_ready (MMCall        *call,
                       GAsyncResult *result,
                       gpointer      nothing)
{
    gboolean  operation_result;
    GError   *error = NULL;

    operation_result = mm_call_join_multiparty_finish (call, result, &error);
    join_multiparty_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
leave_multiparty_process_reply (gboolean      result,
                                const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't leave multiparty call: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully left multiparty call\n");
}

static void
leave_multiparty_ready (MMCall        *call,
                        GAsyncResult *result,
                        gpointer      nothing)
{
    gboolean  operation_result;
    GError   *error = NULL;

    operation_result = mm_call_leave_multiparty_finish (call, result, &error);
    leave_multiparty_process_reply (operation_result, error);

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

    /* Requesting to deflect the call? */
    if (deflect_str) {
        mm_call_deflect (ctx->call,
                         deflect_str,
                         ctx->cancellable,
                         (GAsyncReadyCallback)deflect_ready,
                         NULL);
        return;
    }

    /* Requesting to join multiparty call? */
    if (join_multiparty_flag) {
        mm_call_join_multiparty (ctx->call,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)join_multiparty_ready,
                                 NULL);
        return;
    }

    /* Requesting to leave multiparty call? */
    if (leave_multiparty_flag) {
        mm_call_leave_multiparty (ctx->call,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)leave_multiparty_ready,
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

    /* Requesting to deflect the call? */
    if (deflect_str) {
        gboolean operation_result;

        operation_result = mm_call_deflect_sync (ctx->call,
                                                 deflect_str,
                                                 NULL,
                                                 &error);
        deflect_process_reply (operation_result, error);
        return;
    }

    /* Requesting to join multiparty call? */
    if (join_multiparty_flag) {
        gboolean operation_result;

        operation_result = mm_call_join_multiparty_sync (ctx->call,
                                                         NULL,
                                                         &error);
        join_multiparty_process_reply (operation_result, error);
        return;
    }

    /* Requesting to leave multiparty call? */
    if (leave_multiparty_flag) {
        gboolean operation_result;

        operation_result = mm_call_leave_multiparty_sync (ctx->call,
                                                          NULL,
                                                          &error);
        leave_multiparty_process_reply (operation_result, error);
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
