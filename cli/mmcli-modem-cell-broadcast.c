/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control cell broadcast information from the command line
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
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
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
    MMModemCellBroadcast *modem_cell_broadcast;
} Context;
static Context *ctx;

/* Options */
static gboolean status_flag;
static gboolean list_flag;
static gchar *delete_str;

static GOptionEntry entries[] = {
    { "cell-broadcast-list-cbm", 0, 0, G_OPTION_ARG_NONE, &list_flag,
      "List cell broadcast messages available in a given modem",
      NULL
    },
    { "cell-broadcast-delete-cbm", 0, 0, G_OPTION_ARG_STRING, &delete_str,
      "Delete a cell broadcast message from a given modem",
      "[PATH|INDEX]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_cell_broadcast_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("cell-broadcast",
                                "Cell Broadcast options:",
                                "Show Cell Broadcast options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_cell_broadcast_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (list_flag +
                 !!delete_str);

    if (n_actions > 1) {
        g_printerr ("error: too many Cell Broadcast actions requested\n");
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
    if (ctx->modem_cell_broadcast)
        g_object_unref (ctx->modem_cell_broadcast);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->connection)
        g_object_unref (ctx->connection);
    g_free (ctx);
}

static void
ensure_modem_cell_broadcast (void)
{
    if (mm_modem_get_state (mm_object_peek_modem (ctx->object)) < MM_MODEM_STATE_ENABLED) {
        g_printerr ("error: modem not enabled yet\n");
        exit (EXIT_FAILURE);
    }

    if (!ctx->modem_cell_broadcast) {
        g_printerr ("error: modem has no cell broadcast capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_cell_broadcast_shutdown (void)
{
    context_free ();
}

static void
output_cbm_info (MMCbm *cbm)
{
    gchar *extra;

    extra = g_strdup_printf ("(%s)", mm_cbm_state_get_string (mm_cbm_get_state (cbm)));
    mmcli_output_listitem (MMC_F_CBM_LIST_DBUS_PATH,
                           "    ",
                           mm_cbm_get_path (cbm),
                           extra);
    g_free (extra);
}

static void
list_process_reply (GList        *result,
                    const GError *error)
{
    GList *l;

    if (error) {
        g_printerr ("error: couldn't list CBM: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    for (l = result; l; l = g_list_next (l))
        output_cbm_info (MM_CBM (l->data));
    mmcli_output_list_dump (MMC_F_CBM_LIST_DBUS_PATH);
}

static void
list_ready (MMModemCellBroadcast *modem,
            GAsyncResult         *result,
            gpointer              nothing)
{
    GList *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_cell_broadcast_list_finish (modem, result, &error);
    list_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
delete_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't delete CBM: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully deleted CBM from modem\n");
}

static void
delete_ready (MMModemCellBroadcast *modem,
              GAsyncResult         *result,
              gpointer              nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_cell_broadcast_delete_finish (modem, result, &error);
    delete_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_cbm_to_delete_ready (GDBusConnection *connection,
                         GAsyncResult *res)
{
    MMCbm *cbm;
    MMObject *obj = NULL;

    cbm = mmcli_get_cbm_finish (res, NULL, &obj);
    if (!g_str_equal (mm_object_get_path (obj), mm_modem_cell_broadcast_get_path (ctx->modem_cell_broadcast))) {
        g_printerr ("error: CBM '%s' not owned by modem '%s'",
                    mm_cbm_get_path (cbm),
                    mm_modem_cell_broadcast_get_path (ctx->modem_cell_broadcast));
        exit (EXIT_FAILURE);
    }

    mm_modem_cell_broadcast_delete (ctx->modem_cell_broadcast,
                               mm_cbm_get_path (cbm),
                               ctx->cancellable,
                               (GAsyncReadyCallback)delete_ready,
                               NULL);
    g_object_unref (cbm);
    g_object_unref (obj);
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_cell_broadcast = mm_object_get_modem_cell_broadcast (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_cell_broadcast)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_cell_broadcast));

    ensure_modem_cell_broadcast ();

    if (status_flag)
        g_assert_not_reached ();

    /* Request to list CBM? */
    if (list_flag) {
        g_debug ("Asynchronously listing CBM in modem...");
        mm_modem_cell_broadcast_list (ctx->modem_cell_broadcast,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)list_ready,
                                 NULL);
        return;
    }

    /* Request to delete a given CBM? */
    if (delete_str) {
        mmcli_get_cbm (ctx->connection,
                       delete_str,
                       ctx->cancellable,
                       (GAsyncReadyCallback)get_cbm_to_delete_ready,
                       NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_cell_broadcast_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_cell_broadcast_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_cell_broadcast = mm_object_get_modem_cell_broadcast (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_cell_broadcast)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_cell_broadcast));

    ensure_modem_cell_broadcast ();

    /* Request to list the CBM? */
    if (list_flag) {
        GList *result;

        g_debug ("Synchronously listing CBM messages...");
        result = mm_modem_cell_broadcast_list_sync (ctx->modem_cell_broadcast, NULL, &error);
        list_process_reply (result, error);
        return;
    }

    /* Request to delete a given CBM? */
    if (delete_str) {
        gboolean result;
        MMCbm *cbm;
        MMObject *obj = NULL;

        cbm = mmcli_get_cbm_sync (connection,
                                  delete_str,
                                  NULL,
                                  &obj);
        if (!g_str_equal (mm_object_get_path (obj), mm_modem_cell_broadcast_get_path (ctx->modem_cell_broadcast))) {
            g_printerr ("error: CBM '%s' not owned by modem '%s'",
                        mm_cbm_get_path (cbm),
                        mm_modem_cell_broadcast_get_path (ctx->modem_cell_broadcast));
            exit (EXIT_FAILURE);
        }

        result = mm_modem_cell_broadcast_delete_sync (ctx->modem_cell_broadcast,
                                                 mm_cbm_get_path (cbm),
                                                 NULL,
                                                 &error);
        g_object_unref (cbm);
        g_object_unref (obj);

        delete_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
