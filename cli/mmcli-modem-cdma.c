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
 * Copyright (C) 2012 - Google Inc.
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
    MMModemCdma *modem_cdma;
} Context;
static Context *ctx;

/* Options */
static gchar *activate_str;
static gchar *activate_manual_str;
static gchar *activate_manual_with_prl_str;

static GOptionEntry entries[] = {
    { "cdma-activate", 0, 0, G_OPTION_ARG_STRING, &activate_str,
      "Provision the modem to use with a given carrier using OTA settings.",
      "[CARRIER]"
    },
    { "cdma-activate-manual", 0, 0, G_OPTION_ARG_STRING, &activate_manual_str,
      "Provision the modem with the given settings. 'spc', 'sid', 'mdn' and 'min' are mandatory, 'mn-ha-key' and 'mn-aaa-key' are optional.",
      "[\"key=value,...\"]"
    },
    { "cdma-activate-manual-with-prl", 0, 0, G_OPTION_ARG_STRING, &activate_manual_with_prl_str,
      "Use the given file contents as data for the PRL.",
      "[File path]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_cdma_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("cdma",
                                "CDMA options:",
                                "Show CDMA related options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_cdma_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (!!activate_str +
                 !!activate_manual_str);

    if (n_actions > 1) {
        g_printerr ("error: too many CDMA actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (activate_manual_with_prl_str && !activate_manual_str) {
        g_printerr ("error: `--cdma-activate-manual-with-prl' must be given along "
                    "with `--cdma-activate-manual'\n");
        exit (EXIT_FAILURE);
    }

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
    if (ctx->modem_cdma)
        g_object_unref (ctx->modem_cdma);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_cdma (void)
{
    if (mm_modem_get_state (mm_object_peek_modem (ctx->object)) < MM_MODEM_STATE_ENABLED) {
        g_printerr ("error: modem not enabled yet\n");
        exit (EXIT_FAILURE);
    }

    if (!ctx->modem_cdma) {
        g_printerr ("error: modem has no CDMA capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_cdma_shutdown (void)
{
    context_free ();
}

static void
activate_process_reply (gboolean result,
                        const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't activate the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully activated the modem\n");
}

static void
activate_ready (MMModemCdma  *modem_cdma,
                GAsyncResult *result,
                gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_cdma_activate_finish (modem_cdma, result, &error);
    activate_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
activate_manual_process_reply (gboolean result,
                               const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't manually activate the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully activated the modem manually\n");
}

static void
activate_manual_ready (MMModemCdma  *modem_cdma,
                       GAsyncResult *result,
                       gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_cdma_activate_manual_finish (modem_cdma, result, &error);
    activate_manual_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static MMCdmaManualActivationProperties *
build_activate_manual_properties_from_input (const gchar *properties_string,
                                             const gchar *prl_file)
{
    GError *error = NULL;
    MMCdmaManualActivationProperties *properties;

    properties = mm_cdma_manual_activation_properties_new_from_string (properties_string, &error);

    if (!properties) {
        g_printerr ("error: cannot parse properties string: '%s'\n", error->message);
        exit (EXIT_FAILURE);
    }

    if (prl_file) {
        gchar *path;
        GFile *file;
        gchar *contents;
        gsize contents_size;

        g_debug ("Reading data from file '%s'", prl_file);

        file = g_file_new_for_commandline_arg (prl_file);
        path = g_file_get_path (file);
        if (!g_file_get_contents (path,
                                  &contents,
                                  &contents_size,
                                  &error)) {
            g_printerr ("error: cannot read from file '%s': '%s'\n",
                        prl_file, error->message);
            exit (EXIT_FAILURE);
        }
        g_free (path);
        g_object_unref (file);

        if (!mm_cdma_manual_activation_properties_set_prl (properties,
                                                           (guint8 *)contents,
                                                           contents_size,
                                                           &error)) {
            g_printerr ("error: cannot set PRL: '%s'\n", error->message);
            exit (EXIT_FAILURE);
        }
        g_free (contents);
    }

    return properties;
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_cdma = mm_object_get_modem_cdma (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_cdma)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_cdma));

    ensure_modem_cdma ();

    /* Request to activate the modem? */
    if (activate_str) {
        g_debug ("Asynchronously activating the modem...");
        mm_modem_cdma_activate (ctx->modem_cdma,
                                activate_str,
                                ctx->cancellable,
                                (GAsyncReadyCallback)activate_ready,
                                NULL);
        return;
    }

    /* Request to manually activate the modem? */
    if (activate_manual_str) {
        MMCdmaManualActivationProperties *properties;

        properties = build_activate_manual_properties_from_input (activate_manual_str,
                                                                  activate_manual_with_prl_str);

        g_debug ("Asynchronously manually activating the modem...");
        mm_modem_cdma_activate_manual (ctx->modem_cdma,
                                       properties,
                                       ctx->cancellable,
                                       (GAsyncReadyCallback)activate_manual_ready,
                                       NULL);
        g_object_unref (properties);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_cdma_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_cdma_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_cdma = mm_object_get_modem_cdma (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_cdma)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_cdma));

    ensure_modem_cdma ();

    /* Request to activate the modem? */
    if (activate_str) {
        gboolean result;

        g_debug ("Synchronously activating the modem...");
        result = mm_modem_cdma_activate_sync (
            ctx->modem_cdma,
            activate_str,
            NULL,
            &error);
        activate_process_reply (result, error);
        return;
    }

    /* Request to manually activate the modem? */
    if (activate_manual_str) {
        MMCdmaManualActivationProperties *properties;
        gboolean result;

        properties = build_activate_manual_properties_from_input (activate_manual_str,
                                                                  activate_manual_with_prl_str);

        g_debug ("Synchronously manually activating the modem...");
        result = mm_modem_cdma_activate_manual_sync (
            ctx->modem_cdma,
            properties,
            NULL,
            &error);
        activate_manual_process_reply (result, error);
        g_object_unref (properties);
        return;
    }

    g_warn_if_reached ();
}
