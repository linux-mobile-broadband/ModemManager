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
    MMModemTime *modem_time;
} Context;
static Context *ctx;

/* Options */
static gboolean time_flag;

static GOptionEntry entries[] = {
    { "time", 0, 0, G_OPTION_ARG_NONE, &time_flag,
      "Get current network time",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_time_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("time",
                                "Time options:",
                                "Show Time options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_time_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (time_flag);

    if (n_actions > 1) {
        g_printerr ("error: too many Time actions requested\n");
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
    if (ctx->modem_time)
        g_object_unref (ctx->modem_time);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_time (void)
{
    if (mm_modem_get_state (mm_object_peek_modem (ctx->object)) < MM_MODEM_STATE_ENABLED) {
        g_printerr ("error: modem not enabled yet\n");
        exit (EXIT_FAILURE);
    }

    if (!ctx->modem_time) {
        g_printerr ("error: modem has no time capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_time_shutdown (void)
{
    context_free ();
}

static void
get_network_time_process_reply (gchar *time_string,
                                MMNetworkTimezone *timezone,
                                const GError *error)
{
    gchar *offset = NULL;
    gchar *dst_offset = NULL;
    gchar *leap_seconds = NULL;

    if (error) {
        g_printerr ("error: couldn't get current network time: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    if (timezone) {
        if (mm_network_timezone_get_offset (timezone) != MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN)
            offset = g_strdup_printf ("%" G_GINT32_FORMAT, mm_network_timezone_get_offset (timezone));

        if (mm_network_timezone_get_dst_offset (timezone) != MM_NETWORK_TIMEZONE_OFFSET_UNKNOWN)
            dst_offset = g_strdup_printf ("%" G_GINT32_FORMAT, mm_network_timezone_get_dst_offset (timezone));

        if (mm_network_timezone_get_leap_seconds (timezone) != MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN)
            leap_seconds = g_strdup_printf ("%" G_GINT32_FORMAT, mm_network_timezone_get_leap_seconds (timezone));
    }

    mmcli_output_string_take (MMC_F_TIME_CURRENT,          time_string);
    mmcli_output_string_take (MMC_F_TIMEZONE_CURRENT,      offset);
    mmcli_output_string_take (MMC_F_TIMEZONE_DST_OFFSET,   dst_offset);
    mmcli_output_string_take (MMC_F_TIMEZONE_LEAP_SECONDS, leap_seconds);
    mmcli_output_dump ();
}

static void
get_network_time_ready (MMModemTime *modem_time,
                        GAsyncResult    *result)
{
    MMNetworkTimezone *timezone;
    gchar *time_string;
    GError *error = NULL;

    time_string = mm_modem_time_get_network_time_finish (modem_time, result, &error);
    timezone = mm_modem_time_get_network_timezone (modem_time);
    get_network_time_process_reply (time_string, timezone, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_time = mm_object_get_modem_time (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_time)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_time));

    ensure_modem_time ();

    /* Request to get network time from the modem? */
    if (time_flag) {
        g_debug ("Asynchronously getting network time from the modem...");

        mm_modem_time_get_network_time (ctx->modem_time,
                                        ctx->cancellable,
                                        (GAsyncReadyCallback)get_network_time_ready,
                                        NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_time_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_time_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_time = mm_object_get_modem_time (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_time)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_time));

    ensure_modem_time ();

    /* Request to get network time from the modem? */
    if (time_flag) {
        gchar *time_string;
        MMNetworkTimezone *timezone;

        g_debug ("Synchronously getting network time from the modem...");

        time_string = mm_modem_time_get_network_time_sync (ctx->modem_time,
                                                           NULL,
                                                           &error);
        timezone = mm_modem_time_get_network_timezone (ctx->modem_time);
        get_network_time_process_reply (time_string, timezone, error);
        return;
    }

    g_warn_if_reached ();
}
