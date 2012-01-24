/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"

/* Context */
typedef struct {
    MMManager *manager;
    GCancellable *cancellable;
    MMObject *object;
    MMModemLocation *modem_location;
} Context;
static Context *ctx;

/* Options */
static gboolean enable_flag;
static gboolean disable_flag;
static gboolean get_3gpp_flag;

static GOptionEntry entries[] = {
    { "location-enable", 0, 0, G_OPTION_ARG_NONE, &enable_flag,
      "Enable location gathering.",
      NULL
    },
    { "location-disable", 0, 0, G_OPTION_ARG_NONE, &disable_flag,
      "Disable location gathering.",
      NULL
    },
    { "location-get-3gpp", 0, 0, G_OPTION_ARG_NONE, &get_3gpp_flag,
      "Get 3GPP-based location.",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_location_get_option_group (void)
{
	GOptionGroup *group;

	group = g_option_group_new ("location",
	                            "Location options",
	                            "Show Location options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

gboolean
mmcli_modem_location_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (enable_flag +
                 disable_flag +
                 get_3gpp_flag);

    if (n_actions > 1) {
        g_printerr ("error: too many Location actions requested\n");
        exit (EXIT_FAILURE);
    }

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
    if (ctx->modem_location)
        g_object_unref (ctx->modem_location);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

static void
ensure_modem_location (void)
{
    if (ctx->modem_location)
        return;

    g_printerr ("error: modem has no location capabilities");
    exit (EXIT_FAILURE);
}

void
mmcli_modem_location_shutdown (void)
{
    context_free (ctx);
}

static void
enable_process_reply (gboolean result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't enable location gathering: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully enabled location gathering\n");
}

static void
enable_ready (MMModemLocation *modem_location,
              GAsyncResult    *result)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_location_enable_finish (modem_location, result, &error);
    enable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
disable_process_reply (gboolean result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disable location gathering: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disabled location gathering\n");
}

static void
disable_ready (MMModemLocation *modem_location,
               GAsyncResult    *result)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_location_disable_finish (modem_location, result, &error);
    disable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_3gpp_process_reply (MMModemLocation3gpp *location,
                        const GError *error)
{
    if (!location) {
        g_printerr ("error: couldn't get 3GPP-based location from the modem: '%s'\n",
                    error ? error->message : "not available");
        exit (EXIT_FAILURE);
    }

    g_print ("  -------------------------\n"
             "  3GPP location | Mobile country code: '%u'\n"
             "                | Mobile network code: '%u'\n"
             "                |  Location area code: '%lu'\n"
             "                |             Cell ID: '%lu'\n",
             mm_modem_location_3gpp_get_mobile_country_code (location),
             mm_modem_location_3gpp_get_mobile_network_code (location),
             mm_modem_location_3gpp_get_location_area_code (location),
             mm_modem_location_3gpp_get_cell_id (location));

    g_object_unref (location);
}

static void
get_3gpp_ready (MMModemLocation  *modem_location,
                GAsyncResult *result)
{
    MMModemLocation3gpp *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_location_get_3gpp_finish (modem_location, result, &error);
    get_3gpp_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_location = mm_object_get_modem_location (ctx->object);

    ensure_modem_location ();

    /* Request to enable location gathering? */
    if (enable_flag) {
        g_debug ("Asynchronously enabling location gathering...");

        mm_modem_location_enable (ctx->modem_location,
                                  ctx->cancellable,
                                  (GAsyncReadyCallback)enable_ready,
                                  NULL);
        return;
    }

    /* Request to disable location gathering? */
    if (disable_flag) {
        g_debug ("Asynchronously enabling location gathering...");

        mm_modem_location_disable (ctx->modem_location,
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)disable_ready,
                                   NULL);
        return;
    }

    /* Request to get 3GPP location from the modem? */
    if (get_3gpp_flag) {
        g_debug ("Asynchronously getting 3GPP-based location from the modem...");

        mm_modem_location_get_3gpp (ctx->modem_location,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)get_3gpp_ready,
                                    NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_location_run_asynchronous (GDBusConnection *connection,
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
mmcli_modem_location_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_location = mm_object_get_modem_location (ctx->object);

    ensure_modem_location ();

    /* Request to enable location gathering? */
    if (enable_flag) {
        gboolean result;

        g_debug ("Asynchronously enabling location gathering...");

        result = mm_modem_location_enable_sync (ctx->modem_location,
                                                NULL,
                                                &error);
        enable_process_reply (result, error);
        return;
    }

    /* Request to disable location gathering? */
    if (disable_flag) {
        gboolean result;

        g_debug ("Asynchronously enabling location gathering...");

        result = mm_modem_location_disable_sync (ctx->modem_location,
                                                 NULL,
                                                 &error);
        disable_process_reply (result, error);
        return;
    }

    /* Request to get 3GPP based location from the modem? */
    if (get_3gpp_flag) {
        MMModemLocation3gpp *result;

        g_debug ("Synchronously getting 3GPP-based location from the modem...");

        result = mm_modem_location_get_3gpp_sync (ctx->modem_location,
                                                  NULL,
                                                  &error);
        get_3gpp_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
