/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control cbm information from the command line
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
    MMManager *manager;
    MMObject *object;
    GCancellable *cancellable;
    MMCbm *cbm;
} Context;
static Context *ctx;

/* Options */
static gboolean info_flag; /* set when no action found */

static GOptionEntry entries[] = {
    /* no options just info */
    { NULL }
};

GOptionGroup *
mmcli_cbm_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("cbm",
                                "CBM options:",
                                "Show CBM options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_cbm_options_enabled (void)
{
    static gboolean checked = FALSE;
    int n_actions = 0;

    if (checked)
      return n_actions;

    if (mmcli_get_common_cbm_string ()) {
        /* default to info */
        info_flag = TRUE;
        n_actions++;
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
    if (ctx->cbm)
        g_object_unref (ctx->cbm);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

void
mmcli_cbm_shutdown (void)
{
    context_free ();
}

static void
print_cbm_info (MMCbm *cbm)
{
    const gchar *lang;
    gchar *channel;
    gchar *update;
    gchar *code;

    update = g_strdup_printf ("%u", mm_cbm_get_update (cbm));
    channel = g_strdup_printf ("%u", mm_cbm_get_channel (cbm));
    code = g_strdup_printf ("%u", mm_cbm_get_message_code (cbm));
    lang = mm_cbm_get_language (cbm);

    mmcli_output_string           (MMC_F_CBM_GENERAL_DBUS_PATH,           mm_cbm_get_path (cbm));
    mmcli_output_string           (MMC_F_CBM_CONTENT_TEXT,                mm_cbm_get_text (cbm));
    if (lang)
        mmcli_output_string       (MMC_F_CBM_PROPERTIES_LANG,             mm_cbm_get_language (cbm));
    mmcli_output_string_take      (MMC_F_CBM_PROPERTIES_CHANNEL,          channel);
    mmcli_output_string_take      (MMC_F_CBM_PROPERTIES_UPDATE,           update);
    mmcli_output_string_take      (MMC_F_CBM_PROPERTIES_MESSAGE_CODE,     code);

    mmcli_output_dump ();
}

void
mmcli_cbm_run_synchronous (GDBusConnection *connection)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->cbm = mmcli_get_cbm_sync (connection,
                                   mmcli_get_common_cbm_string (),
                                   &ctx->manager,
                                   &ctx->object);

    /* Setup operation timeout */
    mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->cbm));

    g_debug ("Printing CBM info...");
    print_cbm_info (ctx->cbm);
}
