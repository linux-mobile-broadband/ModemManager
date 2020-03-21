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
 * Copyright (C) 2013 Google, Inc.
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
    GCancellable *cancellable;
    MMObject *object;
    MMModemOma *modem_oma;
} Context;
static Context *ctx;

/* Options */
static gboolean status_flag;
static gchar *setup_str;
static gchar *start_str;
static gchar *accept_str;
static gchar *reject_str;
static gboolean cancel_flag;

static GOptionEntry entries[] = {
    { "oma-status", 0, 0, G_OPTION_ARG_NONE, &status_flag,
      "Current status of the OMA device management",
      NULL
    },
    { "oma-setup", 0, 0, G_OPTION_ARG_STRING, &setup_str,
      "Setup OMA features",
      "[FEATURE1|FEATURE2...]"
    },
    { "oma-start-client-initiated-session", 0, 0, G_OPTION_ARG_STRING, &start_str,
      "Start client initiated OMA DM session",
      "[Session type]"
    },
    { "oma-accept-network-initiated-session", 0, 0, G_OPTION_ARG_STRING, &accept_str,
      "Accept network initiated OMA DM session",
      "[Session ID]"
    },
    { "oma-reject-network-initiated-session", 0, 0, G_OPTION_ARG_STRING, &reject_str,
      "Reject network initiated OMA DM session",
      "[Session ID]"
    },
    { "oma-cancel-session", 0, 0, G_OPTION_ARG_NONE, &cancel_flag,
      "Cancel current OMA DM session",
      NULL
    },

    { NULL }
};

GOptionGroup *
mmcli_modem_oma_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("oma",
                                "OMA options:",
                                "Show OMA options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_oma_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (status_flag +
                 !!setup_str +
                 !!start_str +
                 !!accept_str +
                 !!reject_str +
                 cancel_flag);

    if (n_actions > 1) {
        g_printerr ("error: too many OMA actions requested\n");
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
    if (ctx->modem_oma)
        g_object_unref (ctx->modem_oma);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_oma (void)
{
    if (!ctx->modem_oma) {
        g_printerr ("error: modem has no OMA capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_oma_shutdown (void)
{
    context_free ();
}

static void
print_oma_status (void)
{
    gchar                                     *features_str;
    const MMOmaPendingNetworkInitiatedSession *pending_sessions;
    guint                                      n_pending_sessions;
    const gchar                               *current_session_type = NULL;
    const gchar                               *current_session_state = NULL;
    GPtrArray                                 *aux = NULL;

    features_str = mm_oma_feature_build_string_from_mask (mm_modem_oma_get_features (ctx->modem_oma));

    /* Current session */
    if (mm_modem_oma_get_session_type (ctx->modem_oma) != MM_OMA_SESSION_TYPE_UNKNOWN) {
        current_session_type = mm_oma_session_type_get_string (mm_modem_oma_get_session_type (ctx->modem_oma));
        current_session_state = mm_oma_session_state_get_string (mm_modem_oma_get_session_state (ctx->modem_oma));
    }

    /* If 1 or more pending sessions... */
    if (mm_modem_peek_pending_network_initiated_sessions (ctx->modem_oma, &pending_sessions, &n_pending_sessions) &&
        n_pending_sessions > 0) {
        guint i;

        aux = g_ptr_array_new ();

        for (i = 0; i < n_pending_sessions; i++) {
            gchar *info;

            info = g_strdup_printf ("id: %u, type: %s",
                                    pending_sessions[i].session_id,
                                    mm_oma_session_type_get_string (pending_sessions[i].session_type));
            g_ptr_array_add (aux, info);
        }
        g_ptr_array_add (aux, NULL);
    }

    mmcli_output_string_take       (MMC_F_OMA_FEATURES,         features_str);
    mmcli_output_string            (MMC_F_OMA_CURRENT_TYPE,     current_session_type);
    mmcli_output_string            (MMC_F_OMA_CURRENT_STATE,    current_session_state);
    mmcli_output_string_array_take (MMC_F_OMA_PENDING_SESSIONS, aux ? (gchar **) g_ptr_array_free (aux, FALSE) : NULL, TRUE);
    mmcli_output_dump ();
}

static void
setup_process_reply (gboolean result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't setup OMA features: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully setup OMA features\n");
}

static void
setup_ready (MMModemOma *modem,
             GAsyncResult *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_oma_setup_finish (modem, result, &error);
    setup_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
start_process_reply (gboolean result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't start OMA session: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully started OMA session\n");
}

static void
start_ready (MMModemOma *modem,
             GAsyncResult *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_oma_start_client_initiated_session_finish (modem, result, &error);
    start_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
accept_process_reply (gboolean result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't %s OMA session: '%s'\n",
                    accept_str ? "accept" : "reject",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully %s OMA session\n",
             accept_str ? "accepted" : "rejected");
}

static void
accept_ready (MMModemOma *modem,
              GAsyncResult *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_oma_accept_network_initiated_session_finish (modem, result, &error);
    accept_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
cancel_process_reply (gboolean result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't cancel OMA session: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully cancelled OMA session\n");
}

static void
cancel_ready (MMModemOma *modem,
              GAsyncResult *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_oma_cancel_session_finish (modem, result, &error);
    cancel_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_oma = mm_object_get_modem_oma (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_oma)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_oma));

    ensure_modem_oma ();

    g_assert (!status_flag);

    /* Request to setup OMA features? */
    if (setup_str) {
        GError *error = NULL;
        MMOmaFeature features;

        features = mm_common_get_oma_features_from_string (setup_str, &error);
        if (error) {
            g_printerr ("Error parsing OMA features string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously setting up OMA features...");
        mm_modem_oma_setup (ctx->modem_oma,
                            features,
                            ctx->cancellable,
                            (GAsyncReadyCallback)setup_ready,
                            NULL);
        return;
    }

    /* Request to start session? */
    if (start_str) {
        GError *error = NULL;
        MMOmaSessionType session_type;

        session_type = mm_common_get_oma_session_type_from_string (start_str, &error);
        if (error) {
            g_printerr ("Error parsing OMA session type string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously starting OMA session...");
        mm_modem_oma_start_client_initiated_session (ctx->modem_oma,
                                                     session_type,
                                                     ctx->cancellable,
                                                     (GAsyncReadyCallback)start_ready,
                                                     NULL);
        return;
    }

    /* Request to accept or reject session? */
    if (accept_str || reject_str) {
        guint session_id;

        if (!mm_get_uint_from_str (accept_str ? accept_str : reject_str, &session_id)) {
            g_printerr ("Error parsing OMA session id string: not a number");
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously %s OMA session...", accept_str ? "accepting" : "rejecting");
        mm_modem_oma_accept_network_initiated_session (ctx->modem_oma,
                                                       session_id,
                                                       !!accept_str,
                                                       ctx->cancellable,
                                                       (GAsyncReadyCallback)accept_ready,
                                                       NULL);
        return;
    }

    /* Request to cancel a session? */
    if (cancel_flag) {
        g_debug ("Asynchronously cancelling OMA session...");
        mm_modem_oma_cancel_session (ctx->modem_oma,
                                     ctx->cancellable,
                                     (GAsyncReadyCallback)cancel_ready,
                                     NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_oma_run_asynchronous (GDBusConnection *connection,
                                  GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper modem */
    mmcli_get_modem (connection,
                     mmcli_get_common_modem_string (),
                     cancellable,
                     (GAsyncReadyCallback)get_modem_ready,
                     NULL);
}

void
mmcli_modem_oma_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_oma = mm_object_get_modem_oma (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_oma)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_oma));

    ensure_modem_oma ();

    /* Request to get status? */
    if (status_flag) {
        g_debug ("Printing OMA status...");
        print_oma_status ();
        return;
    }

    /* Request to setup OMA features? */
    if (setup_str) {
        gboolean result;
        MMOmaFeature features;

        features = mm_common_get_oma_features_from_string (setup_str, &error);
        if (error) {
            g_printerr ("Error parsing OMA features string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously setting up OMA features...");
        result = mm_modem_oma_setup_sync (ctx->modem_oma,
                                          features,
                                          NULL,
                                          &error);
        setup_process_reply (result, error);
        return;
    }

    /* Request to start session? */
    if (start_str) {
        gboolean result;
        MMOmaSessionType session_type;

        session_type = mm_common_get_oma_session_type_from_string (start_str, &error);
        if (error) {
            g_printerr ("Error parsing OMA session type string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously starting OMA session...");
        result = mm_modem_oma_start_client_initiated_session_sync (ctx->modem_oma,
                                                                   session_type,
                                                                   NULL,
                                                                   &error);
        start_process_reply (result, error);
        return;
    }

    /* Request to accept or reject session? */
    if (accept_str || reject_str) {
        gboolean result;
        guint session_id;

        if (!mm_get_uint_from_str (accept_str ? accept_str : reject_str, &session_id)) {
            g_printerr ("Error parsing OMA session id string: not a number");
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously %s OMA session...", accept_str ? "accepting" : "rejecting");
        result = mm_modem_oma_accept_network_initiated_session_sync (ctx->modem_oma,
                                                                     session_id,
                                                                     !!accept_str,
                                                                     NULL,
                                                                     &error);
        accept_process_reply (result, error);
        return;
    }

    /* Request to cancel a session? */
    if (cancel_flag) {
        gboolean result;

        g_debug ("Synchronously cancelling OMA session...");
        result = mm_modem_oma_cancel_session_sync (ctx->modem_oma,
                                                   NULL,
                                                   &error);
        cancel_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
