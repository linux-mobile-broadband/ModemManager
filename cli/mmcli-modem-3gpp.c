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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
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
    GCancellable *cancellable;
    MMObject *object;
    MMModem3gpp *modem_3gpp;
    MMModem3gppUssd *modem_3gpp_ussd;
} Context;
static Context *ctx;

/* Options */
static gboolean scan_flag;
static gboolean register_home_flag;
static gchar *register_in_operator_str;
static gboolean ussd_status_flag;
static gchar *ussd_initiate_str;
static gchar *ussd_respond_str;
static gboolean ussd_cancel_flag;

static GOptionEntry entries[] = {
    { "3gpp-scan", 0, 0, G_OPTION_ARG_NONE, &scan_flag,
      "Scan for available networks in a given modem.",
      NULL
    },
    { "3gpp-register-home", 0, 0, G_OPTION_ARG_NONE, &register_home_flag,
      "Request a given modem to register in its home network",
      NULL
    },
    { "3gpp-register-in-operator", 0, 0, G_OPTION_ARG_STRING, &register_in_operator_str,
      "Request a given modem to register in the network of the given operator",
      "[MCCMNC]"
    },
    { "3gpp-ussd-status", 0, 0, G_OPTION_ARG_NONE, &ussd_status_flag,
      "Show status of any ongoing USSD session",
      NULL
    },
    { "3gpp-ussd-initiate", 0, 0, G_OPTION_ARG_STRING, &ussd_initiate_str,
      "Request a given modem to initiate a USSD session",
      "[command]"
    },
    { "3gpp-ussd-respond", 0, 0, G_OPTION_ARG_STRING, &ussd_respond_str,
      "Request a given modem to respond to a USSD request",
      "[response]"
    },
    { "3gpp-ussd-cancel", 0, 0, G_OPTION_ARG_NONE, &ussd_cancel_flag,
      "Request to cancel any ongoing USSD session",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_3gpp_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("3gpp",
                                "3GPP options",
                                "Show 3GPP related options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_3gpp_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (scan_flag +
                 register_home_flag +
                 !!register_in_operator_str +
                 ussd_status_flag +
                 !!ussd_initiate_str +
                 !!ussd_respond_str +
                 ussd_cancel_flag);

    if (n_actions > 1) {
        g_printerr ("error: too many 3GPP actions requested\n");
        exit (EXIT_FAILURE);
    }

    /* Scanning networks takes really a long time, so we do it asynchronously
     * always to avoid DBus timeouts */
    if (scan_flag)
        mmcli_force_async_operation ();

    /* USSD initiate and respond will wait for URCs to get finished, so
     * these are truly async. */
    if (ussd_initiate_str || ussd_respond_str)
        mmcli_force_async_operation ();

    if (ussd_status_flag)
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
    if (ctx->modem_3gpp)
        g_object_unref (ctx->modem_3gpp);
    if (ctx->modem_3gpp_ussd)
        g_object_unref (ctx->modem_3gpp_ussd);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_3gpp (void)
{
    if (mm_modem_get_state (mm_object_peek_modem (ctx->object)) < MM_MODEM_STATE_ENABLED) {
        g_printerr ("error: modem not enabled yet\n");
        exit (EXIT_FAILURE);
    }

    if (!ctx->modem_3gpp) {
        g_printerr ("error: modem has no 3GPP capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

static void
ensure_modem_3gpp_ussd (void)
{
    if (mm_modem_get_unlock_required (mm_object_peek_modem (ctx->object)) != MM_MODEM_LOCK_NONE) {
        g_printerr ("error: modem not unlocked yet\n");
        exit (EXIT_FAILURE);
    }

    if (!ctx->modem_3gpp_ussd) {
        g_printerr ("error: modem has no USSD capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_3gpp_shutdown (void)
{
    context_free (ctx);
}

static void
print_network_info (MMModem3gppNetwork *network)
{
    const gchar *name;
    gchar *access_technologies;

    /* Not the best thing to do, as we may be doing _get() calls twice, but
     * easiest to maintain */
#undef VALIDATE
#define VALIDATE(str) (str ? str : "unknown")

    access_technologies = (mm_modem_access_technology_build_string_from_mask (
                               mm_modem_3gpp_network_get_access_technology (network)));

    /* Prefer long name */
    name = mm_modem_3gpp_network_get_operator_long (network);
    if (!name)
        name = mm_modem_3gpp_network_get_operator_short (network);

    g_print ("%s - %s (%s, %s)\n",
             VALIDATE (mm_modem_3gpp_network_get_operator_code (network)),
             VALIDATE (name),
             access_technologies,
             mm_modem_3gpp_network_availability_get_string (
                 mm_modem_3gpp_network_get_availability (network)));
    g_free (access_technologies);
}

static void
scan_process_reply (GList *result,
                    const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't scan networks in the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("\n");
    if (!result)
        g_print ("No networks were found\n");
    else {
        GList *l;

        g_print ("Found %u networks:\n", g_list_length (result));
        for (l = result; l; l = g_list_next (l)) {
            print_network_info ((MMModem3gppNetwork *)(l->data));
        }
        g_list_free_full (result, (GDestroyNotify) mm_modem_3gpp_network_free);
    }
    g_print ("\n");
}

static void
scan_ready (MMModem3gpp  *modem_3gpp,
            GAsyncResult *result,
            gpointer      nothing)
{
    GList *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_scan_finish (modem_3gpp, result, &error);
    scan_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
register_process_reply (gboolean result,
                        const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't register the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully registered the modem\n");
}

static void
register_ready (MMModem3gpp  *modem_3gpp,
                GAsyncResult *result,
                gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_register_finish (modem_3gpp, result, &error);
    register_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
print_ussd_status (void)
{
    /* Not the best thing to do, as we may be doing _get() calls twice, but
     * easiest to maintain */
#undef VALIDATE
#define VALIDATE(str) (str ? str : "none")

    g_print ("\n"
             "%s\n"
             "  ----------------------------\n"
             "  USSD |               status: '%s'\n"
             "       |      network request: '%s'\n"
             "       | network notification: '%s'\n",
             mm_modem_3gpp_ussd_get_path (ctx->modem_3gpp_ussd),
             mm_modem_3gpp_ussd_session_state_get_string (
                 mm_modem_3gpp_ussd_get_state (ctx->modem_3gpp_ussd)),
             VALIDATE (mm_modem_3gpp_ussd_get_network_request (ctx->modem_3gpp_ussd)),
             VALIDATE (mm_modem_3gpp_ussd_get_network_notification (ctx->modem_3gpp_ussd)));
}

static void
ussd_initiate_process_reply (gchar *result,
                             const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't initiate USSD session: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("USSD session initiated; "
             "new reply from network: '%s'\n", result);
    g_free (result);
}

static void
ussd_initiate_ready (MMModem3gppUssd *modem_3gpp_ussd,
                     GAsyncResult    *result,
                     gpointer         nothing)
{
    gchar *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_ussd_initiate_finish (modem_3gpp_ussd, result, &error);
    ussd_initiate_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
ussd_respond_process_reply (gchar *result,
                            const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't send response in USSD session: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("response successfully sent in USSD session; "
             "new reply from network: '%s'\n", result);
    g_free (result);
}

static void
ussd_respond_ready (MMModem3gppUssd *modem_3gpp_ussd,
                    GAsyncResult    *result,
                    gpointer         nothing)
{
    gchar *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_ussd_respond_finish (modem_3gpp_ussd, result, &error);
    ussd_respond_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
ussd_cancel_process_reply (gboolean result,
                           const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't cancel USSD session: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully cancelled USSD session\n");
}

static void
ussd_cancel_ready (MMModem3gppUssd *modem_3gpp_ussd,
                   GAsyncResult    *result,
                   gpointer         nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_3gpp_ussd_cancel_finish (modem_3gpp_ussd, result, &error);
    ussd_cancel_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_3gpp = mm_object_get_modem_3gpp (ctx->object);
    ctx->modem_3gpp_ussd = mm_object_get_modem_3gpp_ussd (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_3gpp)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp));
    if (ctx->modem_3gpp_ussd)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp_ussd));

    ensure_modem_3gpp ();

    if (ussd_status_flag)
        g_assert_not_reached ();

    /* Request to scan networks? */
    if (scan_flag) {
        g_debug ("Asynchronously scanning for networks...");
        mm_modem_3gpp_scan (ctx->modem_3gpp,
                            ctx->cancellable,
                            (GAsyncReadyCallback)scan_ready,
                            NULL);
        return;
    }

    /* Request to register the modem? */
    if (register_in_operator_str || register_home_flag) {
        g_debug ("Asynchronously registering the modem...");
        mm_modem_3gpp_register (ctx->modem_3gpp,
                                (register_in_operator_str ? register_in_operator_str : ""),
                                ctx->cancellable,
                                (GAsyncReadyCallback)register_ready,
                                NULL);
        return;
    }

    /* Request to initiate USSD session? */
    if (ussd_initiate_str) {
        ensure_modem_3gpp_ussd ();

        g_debug ("Asynchronously initiating USSD session...");
        mm_modem_3gpp_ussd_initiate (ctx->modem_3gpp_ussd,
                                     ussd_initiate_str,
                                     ctx->cancellable,
                                     (GAsyncReadyCallback)ussd_initiate_ready,
                                     NULL);
        return;
    }

    /* Request to respond in USSD session? */
    if (ussd_respond_str) {
        ensure_modem_3gpp_ussd ();

        g_debug ("Asynchronously sending response in USSD session...");
        mm_modem_3gpp_ussd_respond (ctx->modem_3gpp_ussd,
                                    ussd_respond_str,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)ussd_respond_ready,
                                    NULL);
        return;
    }

    /* Request to cancel USSD session? */
    if (ussd_cancel_flag) {
        ensure_modem_3gpp_ussd ();

        g_debug ("Asynchronously cancelling USSD session...");
        mm_modem_3gpp_ussd_cancel (ctx->modem_3gpp_ussd,
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)ussd_cancel_ready,
                                   NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_3gpp_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_3gpp_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_3gpp = mm_object_get_modem_3gpp (ctx->object);
    ctx->modem_3gpp_ussd = mm_object_get_modem_3gpp_ussd (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_3gpp)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp));
    if (ctx->modem_3gpp_ussd)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_3gpp_ussd));

    ensure_modem_3gpp ();

    if (scan_flag)
        g_assert_not_reached ();
    if (ussd_initiate_str)
        g_assert_not_reached ();
    if (ussd_respond_str)
        g_assert_not_reached ();

    /* Request to register the modem? */
    if (register_in_operator_str || register_home_flag) {
        gboolean result;

        g_debug ("Synchronously registering the modem...");
        result = mm_modem_3gpp_register_sync (
            ctx->modem_3gpp,
            (register_in_operator_str ? register_in_operator_str : ""),
            NULL,
            &error);
        register_process_reply (result, error);
        return;
    }

    /* Request to show USSD status? */
    if (ussd_status_flag) {
        ensure_modem_3gpp_ussd ();

        g_debug ("Printing USSD status...");
        print_ussd_status ();
        return;
    }

    /* Request to cancel USSD session? */
    if (ussd_cancel_flag) {
        gboolean result;

        ensure_modem_3gpp_ussd ();

        g_debug ("Asynchronously cancelling USSD session...");
        result = mm_modem_3gpp_ussd_cancel_sync (ctx->modem_3gpp_ussd,
                                                 NULL,
                                                 &error);
        ussd_cancel_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
