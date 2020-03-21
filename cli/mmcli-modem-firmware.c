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
    MMModemFirmware *modem_firmware;
} Context;
static Context *ctx;

/* Options */
static gboolean status_flag;
static gboolean list_flag;
static gchar *select_str;

static GOptionEntry entries[] = {
    { "firmware-status", 0, 0, G_OPTION_ARG_NONE, &status_flag,
      "Show status of firmware management.",
      NULL
    },
    { "firmware-list", 0, 0, G_OPTION_ARG_NONE, &list_flag,
      "List firmware images installed in a given modem",
      NULL
    },
    { "firmware-select", 0, 0, G_OPTION_ARG_STRING, &select_str,
      "Select a given firmware image",
      "[Unique ID]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_firmware_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("firmware",
                                "Firmware options:",
                                "Show Firmware options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_firmware_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (status_flag +
                 list_flag +
                 !!select_str);

    if (n_actions > 1) {
        g_printerr ("error: too many Firmware actions requested\n");
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
    if (ctx->modem_firmware)
        g_object_unref (ctx->modem_firmware);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_firmware (void)
{
    if (!ctx->modem_firmware) {
        g_printerr ("error: modem has no firmware capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_firmware_shutdown (void)
{
    context_free ();
}

static void
print_firmware_status (void)
{
    MMFirmwareUpdateSettings  *update_settings;
    gchar                     *method = NULL;
    const gchar              **device_ids = NULL;
    const gchar               *version = NULL;
    const gchar               *fastboot_at = NULL;

    update_settings = mm_modem_firmware_peek_update_settings (ctx->modem_firmware);
    if (update_settings) {
        MMModemFirmwareUpdateMethod m;

        m = mm_firmware_update_settings_get_method (update_settings);
        if (m != MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
            method = mm_modem_firmware_update_method_build_string_from_mask (m);
            device_ids = mm_firmware_update_settings_get_device_ids (update_settings);
            version = mm_firmware_update_settings_get_version (update_settings);
        }

        if (m & MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT)
            fastboot_at = mm_firmware_update_settings_get_fastboot_at (update_settings);
    }

    /* There's not much to print in this status info, and if the modem
     * does not support any firmware update method, we would just be returning
     * an empty response to the --firmware-status action. So, instead, just
     * return an error message explicitly when in human output type.
     * We can remove this error message as soon as there is some parameter
     * that will always be printed.
     */
    if (!method && !fastboot_at && mmcli_output_get () == MMC_OUTPUT_TYPE_HUMAN) {
        g_printerr ("error: firmware status unsupported\n");
        exit (EXIT_FAILURE);
    }

    mmcli_output_string_list_take (MMC_F_FIRMWARE_METHOD,      method);
    mmcli_output_string_array     (MMC_F_FIRMWARE_DEVICE_IDS,  device_ids, TRUE);
    mmcli_output_string           (MMC_F_FIRMWARE_VERSION,     version);
    mmcli_output_string           (MMC_F_FIRMWARE_FASTBOOT_AT, fastboot_at);
    mmcli_output_dump ();
}

static void
list_process_reply (MMFirmwareProperties *selected,
                    GList                *result,
                    const GError         *error)
{
#undef VALIDATE_UNKNOWN
#define VALIDATE_UNKNOWN(str) (str ? str : "unknown")

    if (error) {
        g_printerr ("error: couldn't list firmware images: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    mmcli_output_firmware_list (result, selected);
    mmcli_output_dump ();

    g_list_free_full (result, g_object_unref);
    g_clear_object (&selected);
}

static void
list_ready (MMModemFirmware *modem,
            GAsyncResult    *result,
            gpointer         nothing)
{
    GList *installed = NULL;
    MMFirmwareProperties *selected = NULL;
    GError *error = NULL;

    mm_modem_firmware_list_finish (modem, result, &selected, &installed, &error);
    list_process_reply (selected, installed, error);

    mmcli_async_operation_done ();
}

static void
select_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't select firmware image: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully selected firmware image\n");
}

static void
create_ready (MMModemFirmware *modem,
              GAsyncResult    *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_firmware_select_finish (modem, result, &error);
    select_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_firmware = mm_object_get_modem_firmware (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_firmware)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_firmware));

    ensure_modem_firmware ();

    if (status_flag)
        g_assert_not_reached ();

    /* Request to list images? */
    if (list_flag) {
        g_debug ("Asynchronously listing firmware images in modem...");
        mm_modem_firmware_list (ctx->modem_firmware,
                                ctx->cancellable,
                                (GAsyncReadyCallback)list_ready,
                                NULL);
        return;
    }

    /* Request to select a given image? */
    if (select_str) {
        g_debug ("Asynchronously selecting firmware image in modem...");
        mm_modem_firmware_select (ctx->modem_firmware,
                                  select_str,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)create_ready,
                                  NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_firmware_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_firmware_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_firmware = mm_object_get_modem_firmware (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_firmware)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_firmware));

    ensure_modem_firmware ();

    /* Request to get firmware status? */
    if (status_flag) {
        g_debug ("Printing firmware status...");
        print_firmware_status ();
        return;
    }

    /* Request to list firmware images? */
    if (list_flag) {
        GList *installed = NULL;
        MMFirmwareProperties *selected = NULL;

        g_debug ("Synchronously listing firmware images in modem...");
        mm_modem_firmware_list_sync (ctx->modem_firmware,
                                     &selected,
                                     &installed,
                                     NULL,
                                     &error);
        list_process_reply (selected, installed, error);
        return;
    }

    /* Request to select a given image? */
    if (select_str) {
        gboolean result;

        g_debug ("Synchronously selecting firmware image in modem...");
        result = mm_modem_firmware_select_sync (ctx->modem_firmware,
                                                select_str,
                                                NULL,
                                                &error);
        select_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
