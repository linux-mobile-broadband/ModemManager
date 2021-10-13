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
 * Copyright (C) 2021 Fibocom Wireless Inc.
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
    MMModemSar *modem_sar;
} Context;
static Context *ctx;

/* Options */
static gboolean status_flag;
static gboolean sar_enable_flag;
static gboolean sar_disable_flag;
static gint power_level_int = -1;

static GOptionEntry entries[] = {
    { "sar-status", 0, 0, G_OPTION_ARG_NONE, &status_flag,
      "Current status of the SAR",
      NULL
    },
    { "sar-enable", 0, 0, G_OPTION_ARG_NONE, &sar_enable_flag,
      "Enable dynamic SAR",
      NULL
    },
    { "sar-disable", 0, 0, G_OPTION_ARG_NONE, &sar_disable_flag,
      "Disable dynamic SAR",
      NULL
    },
    { "sar-set-power-level", 0, 0, G_OPTION_ARG_INT, &power_level_int,
      "Set current dynamic SAR power level for all antennas on the device",
      "[power level]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_sar_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("sar",
                                "SAR options:",
                                "Show SAR options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_sar_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (status_flag +
                 sar_enable_flag +
                 sar_disable_flag +
                 (power_level_int >= 0));

    if (n_actions > 1) {
        g_printerr ("error: too many SAR actions requested\n");
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
    if (ctx->modem_sar)
        g_object_unref (ctx->modem_sar);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_sar (void)
{
    if (!ctx->modem_sar) {
        g_printerr ("error: modem has no SAR capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_sar_shutdown (void)
{
    context_free ();
}

static void
print_sar_status (void)
{
    guint    power_level = 0;
    gboolean sar_state;

    sar_state = mm_modem_sar_get_state (ctx->modem_sar);
    power_level = mm_modem_sar_get_power_level (ctx->modem_sar);

    mmcli_output_string      (MMC_F_SAR_STATE, sar_state ? "yes" : "no");
    mmcli_output_string_take (MMC_F_SAR_POWER_LEVEL, g_strdup_printf ("%d", power_level));
    mmcli_output_dump ();
}

static void
enable_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't enable SAR: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully enabled SAR\n");
}

static void
disable_process_reply (gboolean      result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disable SAR: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully disabled SAR\n");
}

static void
enable_ready (MMModemSar   *modem,
              GAsyncResult *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_sar_enable_finish (modem, result, &error);
    enable_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
disable_ready (MMModemSar   *modem,
               GAsyncResult *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_sar_enable_finish (modem, result, &error);
    disable_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
set_power_level_process_reply (gboolean      result,
                               const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set the SAR power level: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully set the SAR power level\n");
}

static void
set_power_level_ready (MMModemSar   *modem,
                       GAsyncResult *result)
{
    gboolean res;
    GError *error = NULL;

    res = mm_modem_sar_set_power_level_finish (modem, result, &error);
    set_power_level_process_reply (res, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_sar = mm_object_get_modem_sar (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_sar)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_sar));

    ensure_modem_sar ();

    g_assert (!status_flag);

    /* Request to enable SAR */
    if (sar_enable_flag) {
        g_debug ("Asynchronously enabling SAR ...");
        mm_modem_sar_enable (ctx->modem_sar,
                             TRUE,
                             ctx->cancellable,
                             (GAsyncReadyCallback)enable_ready,
                             NULL);
        return;
    }

    /* Request to disable SAR */
    if (sar_disable_flag) {
        g_debug ("Asynchronously disabling SAR ...");
        mm_modem_sar_enable (ctx->modem_sar,
                             FALSE,
                             ctx->cancellable,
                             (GAsyncReadyCallback)disable_ready,
                             NULL);
        return;
    }

    /* Request to set power level of SAR */
    if (power_level_int >= 0) {
        g_debug ("Asynchronously starting set sar power level to %u ...", power_level_int);
        mm_modem_sar_set_power_level (ctx->modem_sar,
                                      power_level_int,
                                      ctx->cancellable,
                                      (GAsyncReadyCallback)set_power_level_ready,
                                      NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_sar_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_sar_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_sar = mm_object_get_modem_sar (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_sar)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_sar));

    ensure_modem_sar ();

    /* Request to get status? */
    if (status_flag) {
        g_debug ("Printing SAR status...");
        print_sar_status ();
        return;
    }

    /* Request to enable SAR */
    if (sar_enable_flag) {
        gboolean result;
        g_debug ("Synchronously enabling SAR ...");
        result = mm_modem_sar_enable_sync (ctx->modem_sar,
                                           TRUE,
                                           ctx->cancellable,
                                           &error);
        enable_process_reply (result, error);
        return;
    }

    /* Request to disable SAR */
    if (sar_disable_flag) {
        gboolean result;
        g_debug ("Synchronously disabling SAR ...");
        result = mm_modem_sar_enable_sync (ctx->modem_sar,
                                           FALSE,
                                           ctx->cancellable,
                                           &error);
        disable_process_reply (result, error);
        return;
    }

    /* Request to set power level of SAR */
    if (power_level_int >=0 ) {
        gboolean result;
        g_debug ("Synchronously starting set power level to %u ...", power_level_int);
        result = mm_modem_sar_set_power_level_sync (ctx->modem_sar,
                                                    power_level_int,
                                                    ctx->cancellable,
                                                    &error);
        set_power_level_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
