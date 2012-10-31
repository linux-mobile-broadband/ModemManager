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
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
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
    MMModemLocation *modem_location;
} Context;
static Context *ctx;

/* Options */
static gboolean status_flag;
static gboolean enable_3gpp_flag;
static gboolean disable_3gpp_flag;
static gboolean get_3gpp_flag;
static gboolean enable_gps_nmea_flag;
static gboolean disable_gps_nmea_flag;
static gboolean get_gps_nmea_flag;
static gboolean enable_gps_raw_flag;
static gboolean disable_gps_raw_flag;
static gboolean get_gps_raw_flag;
static gboolean enable_cdma_bs_flag;
static gboolean disable_cdma_bs_flag;
static gboolean get_cdma_bs_flag;
static gboolean get_all_flag;

static GOptionEntry entries[] = {
    { "location-status", 0, 0, G_OPTION_ARG_NONE, &status_flag,
      "Show status of location gathering.",
      NULL
    },
    { "location-get", 0, 0, G_OPTION_ARG_NONE, &get_all_flag,
      "Get all available location information.",
      NULL
    },
    { "location-enable-3gpp", 0, 0, G_OPTION_ARG_NONE, &enable_3gpp_flag,
      "Enable 3GPP location gathering.",
      NULL
    },
    { "location-disable-3gpp", 0, 0, G_OPTION_ARG_NONE, &disable_3gpp_flag,
      "Disable 3GPP location gathering.",
      NULL
    },
    { "location-get-3gpp", 0, 0, G_OPTION_ARG_NONE, &get_3gpp_flag,
      "Get 3GPP-based location.",
      NULL
    },
    { "location-enable-gps-nmea", 0, 0, G_OPTION_ARG_NONE, &enable_gps_nmea_flag,
      "Enable NMEA-based GPS location gathering.",
      NULL
    },
    { "location-disable-gps-nmea", 0, 0, G_OPTION_ARG_NONE, &disable_gps_nmea_flag,
      "Disable NMEA-based GPS location gathering.",
      NULL
    },
    { "location-get-gps-nmea", 0, 0, G_OPTION_ARG_NONE, &get_gps_nmea_flag,
      "Get NMEA GPS traces.",
      NULL
    },
    { "location-enable-gps-raw", 0, 0, G_OPTION_ARG_NONE, &enable_gps_raw_flag,
      "Enable raw GPS location gathering.",
      NULL
    },
    { "location-disable-gps-raw", 0, 0, G_OPTION_ARG_NONE, &disable_gps_raw_flag,
      "Disable raw GPS location gathering.",
      NULL
    },
    { "location-get-gps-raw", 0, 0, G_OPTION_ARG_NONE, &get_gps_raw_flag,
      "Get raw GPS location.",
      NULL
    },
    { "location-enable-cdma-bs", 0, 0, G_OPTION_ARG_NONE, &enable_cdma_bs_flag,
      "Enable CDMA base station location gathering.",
      NULL
    },
    { "location-disable-cdma-bs", 0, 0, G_OPTION_ARG_NONE, &disable_cdma_bs_flag,
      "Disable CDMA base station location gathering.",
      NULL
    },
    { "location-get-cdma-bs", 0, 0, G_OPTION_ARG_NONE, &get_cdma_bs_flag,
      "Get CDMA base station location.",
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

    if ((enable_3gpp_flag && disable_3gpp_flag) ||
        (enable_gps_nmea_flag && disable_gps_nmea_flag) ||
        (enable_gps_raw_flag && disable_gps_raw_flag) ||
        (enable_cdma_bs_flag && disable_cdma_bs_flag)) {
        g_printerr ("error: cannot enable and disable the same source\n");
        exit (EXIT_FAILURE);
    }

    if (get_all_flag) {
        get_3gpp_flag = TRUE;
        get_gps_nmea_flag = TRUE;
        get_gps_raw_flag = TRUE;
        get_cdma_bs_flag = TRUE;
    }

    n_actions = (status_flag +
                 !!(enable_3gpp_flag +
                    disable_3gpp_flag +
                    enable_gps_nmea_flag +
                    disable_gps_nmea_flag +
                    enable_gps_raw_flag +
                    disable_gps_raw_flag +
                    enable_cdma_bs_flag +
                    disable_cdma_bs_flag) +
                 !!(get_3gpp_flag +
                    get_gps_nmea_flag +
                    get_gps_raw_flag +
                    get_cdma_bs_flag));

    if (n_actions > 1) {
        g_printerr ("error: too many Location actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (status_flag)
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
    if (mm_modem_get_state (mm_object_peek_modem (ctx->object)) < MM_MODEM_STATE_ENABLED) {
        g_printerr ("error: modem not enabled yet\n");
        exit (EXIT_FAILURE);
    }

    if (!ctx->modem_location) {
        g_printerr ("error: modem has no location capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_location_shutdown (void)
{
    context_free (ctx);
}

static void
print_location_status (void)
{
    gchar *capabilities_str;
    gchar *enabled_str;

    capabilities_str = (mm_modem_location_source_build_string_from_mask (
                            mm_modem_location_get_capabilities (ctx->modem_location)));
    enabled_str = (mm_modem_location_source_build_string_from_mask (
                       mm_modem_location_get_enabled (ctx->modem_location)));
    g_print ("\n"
             "%s\n"
             "  ----------------------------\n"
             "  Location | capabilities: '%s'\n"
             "           |      enabled: '%s'\n"
             "           |      signals: '%s'\n",
             mm_modem_location_get_path (ctx->modem_location),
             capabilities_str,
             enabled_str,
             mm_modem_location_signals_location (ctx->modem_location) ? "yes" : "no");
    g_free (capabilities_str);
    g_free (enabled_str);
}

static void
setup_process_reply (gboolean result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't setup location gathering: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully setup location gathering\n");
}

static void
setup_ready (MMModemLocation *modem_location,
             GAsyncResult    *result)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_location_setup_finish (modem_location, result, &error);
    setup_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static MMModemLocationSource
build_sources_from_flags (void)
{
    MMModemLocationSource sources;

    /* Base the new actions on the previously enabled sources */
    sources = mm_modem_location_get_enabled (ctx->modem_location);

    if (enable_3gpp_flag)
        sources |= MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI;
    if (disable_3gpp_flag)
        sources &= ~MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI;

    if (enable_gps_nmea_flag)
        sources |= MM_MODEM_LOCATION_SOURCE_GPS_NMEA;
    if (disable_gps_nmea_flag)
        sources &= ~MM_MODEM_LOCATION_SOURCE_GPS_NMEA;

    if (enable_gps_raw_flag)
        sources |= MM_MODEM_LOCATION_SOURCE_GPS_RAW;
    if (disable_gps_raw_flag)
        sources &= ~MM_MODEM_LOCATION_SOURCE_GPS_RAW;

    if (enable_cdma_bs_flag)
        sources |= MM_MODEM_LOCATION_SOURCE_CDMA_BS;
    if (disable_cdma_bs_flag)
        sources &= ~MM_MODEM_LOCATION_SOURCE_CDMA_BS;

    return sources;
}

static void
get_location_process_reply (MMLocation3gpp *location_3gpp,
                            MMLocationGpsNmea *location_gps_nmea,
                            MMLocationGpsRaw *location_gps_raw,
                            MMLocationCdmaBs *location_cdma_bs,
                            const GError *error)
{
    /* First, check for failures */
    if (!get_all_flag) {
        gboolean exit_error = FALSE;

        if (get_3gpp_flag && !location_3gpp) {
            g_printerr ("error: couldn't get 3GPP-based location from the modem: '%s'\n",
                        error ? error->message : "not available");
            exit_error = TRUE;
        }

        if (get_gps_nmea_flag && !location_gps_nmea) {
            g_printerr ("error: couldn't get NMEA GPS traces from the modem: '%s'\n",
                        error ? error->message : "not available");
            exit_error = TRUE;
        }

        if (get_gps_raw_flag && !location_gps_raw) {
            g_printerr ("error: couldn't get raw GPS location from the modem: '%s'\n",
                        error ? error->message : "not available");
            exit_error = TRUE;
        }

        if (get_cdma_bs_flag && !location_cdma_bs) {
            g_printerr ("error: couldn't get CDMA base station location from the modem: '%s'\n",
                        error ? error->message : "not available");
            exit_error = TRUE;
        }

        if (exit_error)
            exit (EXIT_FAILURE);
    } else if (error) {
        g_printerr ("error: couldn't get location from the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("\n"
             "%s\n",
             mm_modem_location_get_path (ctx->modem_location));

    if (get_3gpp_flag) {
        if (location_3gpp)
            g_print ("  -------------------------\n"
                     "  3GPP location   | Mobile country code: '%u'\n"
                     "                  | Mobile network code: '%u'\n"
                     "                  |  Location area code: '%lu'\n"
                     "                  |             Cell ID: '%lu'\n",
                     mm_location_3gpp_get_mobile_country_code (location_3gpp),
                     mm_location_3gpp_get_mobile_network_code (location_3gpp),
                     mm_location_3gpp_get_location_area_code (location_3gpp),
                     mm_location_3gpp_get_cell_id (location_3gpp));
        else
            g_print ("  -------------------------\n"
                     "  3GPP location   | Not available\n");
    }

    if (get_gps_nmea_flag) {
        gchar *full = NULL;

        if (location_gps_nmea)
            full = mm_location_gps_nmea_build_full (location_gps_nmea);

        if (full) {
            gchar *prefixed;

            prefixed = mmcli_prefix_newlines ("                  | ", full);
            g_print ("  -------------------------\n"
                     "  GPS NMEA traces | %s\n",
                     prefixed);
            g_free (prefixed);
            g_free (full);
        } else
            g_print ("  -------------------------\n"
                     "  GPS NMEA traces | Not available\n");
    }

    if (get_gps_raw_flag) {
        if (location_gps_raw)
            g_print ("  -------------------------\n"
                     "  Raw GPS         |  UTC time: '%s'\n"
                     "                  | Longitude: '%lf'\n"
                     "                  |  Latitude: '%lf'\n"
                     "                  |  Altitude: '%lf'\n",
                     mm_location_gps_raw_get_utc_time (location_gps_raw),
                     mm_location_gps_raw_get_longitude (location_gps_raw),
                     mm_location_gps_raw_get_latitude (location_gps_raw),
                     mm_location_gps_raw_get_altitude (location_gps_raw));
        else
            g_print ("  -------------------------\n"
                     "  Raw GPS         | Not available\n");
    }

    if (get_cdma_bs_flag) {
        if (location_cdma_bs)
            g_print ("  -------------------------\n"
                     "  CDMA BS         | Longitude: '%lf'\n"
                     "                  |  Latitude: '%lf'\n",
                     mm_location_cdma_bs_get_longitude (location_cdma_bs),
                     mm_location_cdma_bs_get_latitude (location_cdma_bs));
        else
            g_print ("  -------------------------\n"
                     "  CDMA BS         | Not available\n");
    }

    if (location_3gpp)
        g_object_unref (location_3gpp);
    if (location_gps_nmea)
        g_object_unref (location_gps_nmea);
    if (location_gps_raw)
        g_object_unref (location_gps_raw);
    if (location_cdma_bs)
        g_object_unref (location_cdma_bs);
}

static void
get_location_ready (MMModemLocation  *modem_location,
                    GAsyncResult *result)
{
    MMLocation3gpp *location_3gpp = NULL;
    MMLocationGpsNmea *location_gps_nmea = NULL;
    MMLocationGpsRaw *location_gps_raw = NULL;
    MMLocationCdmaBs *location_cdma_bs = NULL;
    GError *error = NULL;

    mm_modem_location_get_full_finish (modem_location,
                                       result,
                                       get_3gpp_flag ? &location_3gpp : NULL,
                                       get_gps_nmea_flag ? &location_gps_nmea : NULL,
                                       get_gps_raw_flag ? &location_gps_raw : NULL,
                                       get_cdma_bs_flag ? &location_cdma_bs : NULL,
                                       &error);
    get_location_process_reply (location_3gpp, location_gps_nmea, location_gps_raw, location_cdma_bs, error);

    mmcli_async_operation_done ();
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_location = mm_object_get_modem_location (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_location)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_location));

    ensure_modem_location ();

    if (status_flag)
        g_assert_not_reached ();

    /* Request to setup location gathering? */
    if (enable_3gpp_flag ||
        disable_3gpp_flag ||
        enable_gps_nmea_flag ||
        disable_gps_nmea_flag ||
        enable_gps_raw_flag ||
        disable_gps_raw_flag ||
        enable_cdma_bs_flag ||
        disable_cdma_bs_flag) {
        g_debug ("Asynchronously setting up location gathering...");
        mm_modem_location_setup (ctx->modem_location,
                                 build_sources_from_flags (),
                                 mm_modem_location_signals_location (ctx->modem_location),
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)setup_ready,
                                 NULL);
        return;
    }

    /* Request to get location from the modem? */
    if (get_3gpp_flag ||
        get_gps_nmea_flag ||
        get_gps_raw_flag ||
        get_cdma_bs_flag) {
        g_debug ("Asynchronously getting location from the modem...");
        mm_modem_location_get_full (ctx->modem_location,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)get_location_ready,
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

    /* Setup operation timeout */
    if (ctx->modem_location)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_location));

    ensure_modem_location ();

    /* Request to get location status? */
    if (status_flag) {
        g_debug ("Printing location status...");
        print_location_status ();
        return;
    }

    /* Request to setup location gathering? */
    if (enable_3gpp_flag ||
        disable_3gpp_flag ||
        enable_gps_nmea_flag ||
        disable_gps_nmea_flag ||
        enable_gps_raw_flag ||
        disable_gps_raw_flag ||
        enable_cdma_bs_flag ||
        disable_cdma_bs_flag) {
        gboolean result;

        g_debug ("Synchronously setting up location gathering...");
        result = mm_modem_location_setup_sync (ctx->modem_location,
                                               build_sources_from_flags (),
                                               mm_modem_location_signals_location (ctx->modem_location),
                                               NULL,
                                               &error);
        setup_process_reply (result, error);
        return;
    }

    /* Request to get location from the modem? */
    if (get_3gpp_flag ||
        get_gps_nmea_flag ||
        get_gps_raw_flag ||
        get_cdma_bs_flag) {
        MMLocation3gpp *location_3gpp = NULL;
        MMLocationGpsNmea *location_gps_nmea = NULL;
        MMLocationGpsRaw *location_gps_raw = NULL;
        MMLocationCdmaBs *location_cdma_bs = NULL;

        g_debug ("Synchronously getting location from the modem...");
        mm_modem_location_get_full_sync (ctx->modem_location,
                                         get_3gpp_flag ? &location_3gpp : NULL,
                                         get_gps_nmea_flag ? &location_gps_nmea : NULL,
                                         get_gps_raw_flag ? &location_gps_raw : NULL,
                                         get_cdma_bs_flag ? &location_cdma_bs : NULL,
                                         NULL,
                                         &error);
        get_location_process_reply (location_3gpp, location_gps_nmea, location_gps_raw, location_cdma_bs, error);
        return;
    }

    g_warn_if_reached ();
}
