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
    MMModemSimple *modem_simple;
} Context;
static Context *ctx;

/* Options */
static gchar *connect_str;
static gboolean disconnect_flag;

static GOptionEntry entries[] = {
    { "simple-connect", 0, 0, G_OPTION_ARG_STRING, &connect_str,
      "Run full connection sequence.",
      "[\"key=value,...\"]"
    },
    { "simple-disconnect", 0, 0, G_OPTION_ARG_NONE, &disconnect_flag,
      "Disconnect all connected bearers.",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_simple_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("simple",
                                "Simple options:",
                                "Show Simple options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_simple_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (!!connect_str +
                 disconnect_flag);

    if (n_actions > 1) {
        g_printerr ("error: too many Simple actions requested\n");
        exit (EXIT_FAILURE);
    }

    /* Simple connection may take really a long time, so we do it asynchronously
     * always to avoid DBus timeouts */
    if (connect_str)
        mmcli_force_async_operation ();

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
    if (ctx->modem_simple)
        g_object_unref (ctx->modem_simple);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_simple (void)
{
    if (!ctx->modem_simple) {
        g_printerr ("error: modem has no Simple capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_simple_shutdown (void)
{
    context_free ();
}

static void
connect_process_reply (MMBearer *result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't connect the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully connected the modem\n");
    g_object_unref (result);
}

static void
connect_ready (MMModemSimple  *modem_simple,
               GAsyncResult *result,
               gpointer      nothing)
{
    MMBearer *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_simple_connect_finish (modem_simple, result, &error);
    connect_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
disconnect_process_reply (gboolean result,
                          const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disconnect all bearers in the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disconnected all bearers in the modem\n");
}

static void
disconnect_ready (MMModemSimple  *modem_simple,
                  GAsyncResult *result,
                  gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_simple_disconnect_finish (modem_simple, result, &error);
    disconnect_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_simple = mm_object_get_modem_simple (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_simple)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_simple));

    ensure_modem_simple ();

    /* Request to connect the modem? */
    if (connect_str) {
        GError *error = NULL;
        MMSimpleConnectProperties *properties;

        g_debug ("Asynchronously connecting the modem...");

        properties = mm_simple_connect_properties_new_from_string (connect_str, &error);
        if (!properties) {
            g_printerr ("Error parsing connect string: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }

        mm_modem_simple_connect (ctx->modem_simple,
                                 properties,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)connect_ready,
                                 NULL);
        g_object_unref (properties);
        return;
    }

    /* Request to disconnect all bearers in the modem? */
    if (disconnect_flag) {
        g_debug ("Asynchronously disconnecting all bearers in the modem...");

        mm_modem_simple_disconnect (ctx->modem_simple,
                                    NULL,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)disconnect_ready,
                                    NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_simple_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_simple_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_simple = mm_object_get_modem_simple (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_simple)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_simple));

    ensure_modem_simple ();

    if (connect_str)
        g_assert_not_reached ();

    /* Request to disconnect all bearers in the modem? */
    if (disconnect_flag) {
        gboolean result;

        g_debug ("Synchronously disconnecting all bearers in the modem...");

        result = mm_modem_simple_disconnect_sync (ctx->modem_simple,
                                                  NULL,
                                                  NULL,
                                                  &error);
        disconnect_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
