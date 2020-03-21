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
 * Copyright (C) 2012 Lanedo GmbH
 * Copyright (C) 2012-2019 Aleksander Morgado <aleksander@aleksander.es>
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
    MMModemLocation *modem_location;
} Context;
static Context *ctx;

/* Options */
static gboolean status_flag;
static gboolean enable_3gpp_flag;
static gboolean disable_3gpp_flag;
static gboolean enable_agps_msa_flag;
static gboolean disable_agps_msa_flag;
static gboolean enable_agps_msb_flag;
static gboolean disable_agps_msb_flag;
static gboolean enable_gps_nmea_flag;
static gboolean disable_gps_nmea_flag;
static gboolean enable_gps_raw_flag;
static gboolean disable_gps_raw_flag;
static gboolean enable_cdma_bs_flag;
static gboolean disable_cdma_bs_flag;
static gboolean enable_gps_unmanaged_flag;
static gboolean disable_gps_unmanaged_flag;
static gboolean set_enable_signal_flag;
static gboolean set_disable_signal_flag;
static gboolean get_flag;
static gchar *set_supl_server_str;
static gchar *inject_assistance_data_str;
static gchar *set_gps_refresh_rate_str;

static GOptionEntry entries[] = {
    { "location-status", 0, 0, G_OPTION_ARG_NONE, &status_flag,
      "Show status of location gathering.",
      NULL
    },
    { "location-get", 0, 0, G_OPTION_ARG_NONE, &get_flag,
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
    { "location-enable-agps-msa", 0, 0, G_OPTION_ARG_NONE, &enable_agps_msa_flag,
      "Enable MSA A-GPS location gathering.",
      NULL
    },
    { "location-disable-agps-msa", 0, 0, G_OPTION_ARG_NONE, &disable_agps_msa_flag,
      "Disable MSA A-GPS location gathering.",
      NULL
    },
    { "location-enable-agps-msb", 0, 0, G_OPTION_ARG_NONE, &enable_agps_msb_flag,
      "Enable MSB A-GPS location gathering.",
      NULL
    },
    { "location-disable-agps-msb", 0, 0, G_OPTION_ARG_NONE, &disable_agps_msb_flag,
      "Disable MSB A-GPS location gathering.",
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
    { "location-enable-gps-raw", 0, 0, G_OPTION_ARG_NONE, &enable_gps_raw_flag,
      "Enable raw GPS location gathering.",
      NULL
    },
    { "location-disable-gps-raw", 0, 0, G_OPTION_ARG_NONE, &disable_gps_raw_flag,
      "Disable raw GPS location gathering.",
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
    { "location-enable-gps-unmanaged", 0, 0, G_OPTION_ARG_NONE, &enable_gps_unmanaged_flag,
      "Enable unmanaged GPS location gathering.",
      NULL
    },
    { "location-disable-gps-unmanaged", 0, 0, G_OPTION_ARG_NONE, &disable_gps_unmanaged_flag,
      "Disable unmanaged GPS location gathering.",
      NULL
    },
    { "location-set-supl-server", 0, 0, G_OPTION_ARG_STRING, &set_supl_server_str,
      "Set SUPL server address",
      "[IP:PORT] or [FQDN:PORT]"
    },
    { "location-inject-assistance-data", 0, 0, G_OPTION_ARG_FILENAME, &inject_assistance_data_str,
      "Inject assistance data in the GNSS module",
      "[PATH]"
    },
    { "location-set-gps-refresh-rate", 0, 0, G_OPTION_ARG_STRING, &set_gps_refresh_rate_str,
      "Set GPS refresh rate in seconds, or 0 disable the explicit rate.",
      "[RATE]"
    },
    { "location-set-enable-signal", 0, 0, G_OPTION_ARG_NONE, &set_enable_signal_flag,
      "Enable location update signaling in DBus property.",
      NULL
    },
    { "location-set-disable-signal", 0, 0, G_OPTION_ARG_NONE, &set_disable_signal_flag,
      "Disable location update signaling in DBus property.",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_location_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("location",
                                "Location options:",
                                "Show Location options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

#define any_location_setup_flag (                              \
    enable_3gpp_flag          || disable_3gpp_flag          || \
    enable_agps_msa_flag      || disable_agps_msa_flag      || \
    enable_agps_msb_flag      || disable_agps_msb_flag      || \
    enable_gps_nmea_flag      || disable_gps_nmea_flag      || \
    enable_gps_raw_flag       || disable_gps_raw_flag       || \
    enable_cdma_bs_flag       || disable_cdma_bs_flag       || \
    enable_gps_unmanaged_flag || disable_gps_unmanaged_flag || \
    set_enable_signal_flag    || set_disable_signal_flag)

gboolean
mmcli_modem_location_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    if ((enable_3gpp_flag          && disable_3gpp_flag)          ||
        (enable_agps_msa_flag      && disable_agps_msa_flag)      ||
        (enable_agps_msb_flag      && disable_agps_msb_flag)      ||
        (enable_gps_nmea_flag      && disable_gps_nmea_flag)      ||
        (enable_gps_raw_flag       && disable_gps_raw_flag)       ||
        (enable_gps_unmanaged_flag && disable_gps_unmanaged_flag) ||
        (enable_cdma_bs_flag       && disable_cdma_bs_flag)) {
        g_printerr ("error: cannot enable and disable the same source\n");
        exit (EXIT_FAILURE);
    }

    if (set_enable_signal_flag && set_disable_signal_flag) {
        g_printerr ("error: cannot enable and disable location signaling\n");
        exit (EXIT_FAILURE);
    }

    n_actions = (status_flag +
                 any_location_setup_flag +
                 get_flag +
                 !!set_supl_server_str +
                 !!inject_assistance_data_str +
                 !!set_gps_refresh_rate_str);

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
context_free (void)
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
    context_free ();
}

static void
print_location_status (void)
{
    gchar        *capabilities;
    gchar        *enabled;
    gchar        *gps_refresh_rate = NULL;
    const gchar  *gps_supl_server = NULL;
    gchar        *gps_assistance = NULL;
    const gchar **gps_assistance_servers = NULL;

    capabilities = (mm_modem_location_source_build_string_from_mask (
                        mm_modem_location_get_capabilities (ctx->modem_location)));
    enabled = (mm_modem_location_source_build_string_from_mask (
                   mm_modem_location_get_enabled (ctx->modem_location)));

    /* If GPS supported, show GPS refresh rate and supported assistance data */
    if (mm_modem_location_get_capabilities (ctx->modem_location) & (MM_MODEM_LOCATION_SOURCE_GPS_RAW | MM_MODEM_LOCATION_SOURCE_GPS_NMEA)) {
        guint                             rate;
        MMModemLocationAssistanceDataType mask;

        rate = mm_modem_location_get_gps_refresh_rate (ctx->modem_location);
        gps_refresh_rate = g_strdup_printf ("%u", rate);

        /* If A-GPS supported, show SUPL server setup */
        if (mm_modem_location_get_capabilities (ctx->modem_location) & (MM_MODEM_LOCATION_SOURCE_AGPS_MSA | MM_MODEM_LOCATION_SOURCE_AGPS_MSB))
            gps_supl_server = mm_modem_location_get_supl_server (ctx->modem_location);

        mask = mm_modem_location_get_supported_assistance_data (ctx->modem_location);
        gps_assistance = mm_modem_location_assistance_data_type_build_string_from_mask (mask);

        /* If any assistance data supported, show server list */
        if (mask != MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE)
            gps_assistance_servers = mm_modem_location_get_assistance_data_servers (ctx->modem_location);
    }

    mmcli_output_string_list_take  (MMC_F_LOCATION_CAPABILITIES,           capabilities);
    mmcli_output_string_list_take  (MMC_F_LOCATION_ENABLED,                enabled);
    mmcli_output_string            (MMC_F_LOCATION_SIGNALS,                mm_modem_location_signals_location (ctx->modem_location) ? "yes" : "no");
    mmcli_output_string_take_typed (MMC_F_LOCATION_GPS_REFRESH_RATE,       gps_refresh_rate, "seconds");
    mmcli_output_string            (MMC_F_LOCATION_GPS_SUPL_SERVER,        gps_supl_server);
    mmcli_output_string_list_take  (MMC_F_LOCATION_GPS_ASSISTANCE,         gps_assistance);
    mmcli_output_string_array      (MMC_F_LOCATION_GPS_ASSISTANCE_SERVERS, gps_assistance_servers, TRUE);
    mmcli_output_dump ();
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

static void
set_supl_server_process_reply (gboolean result,
                               const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set SUPL servert address: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set SUPL server address\n");
}

static void
set_supl_server_ready (MMModemLocation *modem_location,
                       GAsyncResult    *result)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_location_set_supl_server_finish (modem_location, result, &error);
    set_supl_server_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static gboolean
parse_inject_assistance_data (guint8 **o_data,
                              gsize   *o_data_size)
{
    gboolean  result = FALSE;
    GFile    *file = NULL;
    gchar    *data;
    gsize     data_size;
    GError   *error = NULL;

    file = g_file_new_for_commandline_arg (inject_assistance_data_str);

    if (!g_file_load_contents (file, NULL, &data, &data_size, NULL, &error)) {
        g_printerr ("error: cannot load file contents: %s\n", error->message);
        goto out;
    }

    if (data_size == 0) {
        g_printerr ("error: file is empty\n");
        goto out;
    }

    *o_data = (guint8 *)data;
    *o_data_size = data_size;
    result = TRUE;

out:
    if (error)
        g_error_free (error);
    g_object_unref (file);
    return result;
}

static void
inject_assistance_data_process_reply (gboolean result,
                                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't inject assistance data: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully injected assistance data\n");
}

static void
inject_assistance_data_ready (MMModemLocation *modem_location,
                              GAsyncResult    *result)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_location_inject_assistance_data_finish (modem_location, result, &error);
    inject_assistance_data_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
set_gps_refresh_rate_process_reply (gboolean result,
                                    const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set GPS refresh rate: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully set GPS refresh rate\n");
}

static void
set_gps_refresh_rate_ready (MMModemLocation *modem_location,
                            GAsyncResult    *result)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_location_set_gps_refresh_rate_finish (modem_location, result, &error);
    set_gps_refresh_rate_process_reply (operation_result, error);

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

    if (enable_agps_msa_flag)
        sources |= MM_MODEM_LOCATION_SOURCE_AGPS_MSA;
    if (disable_agps_msa_flag)
        sources &= ~MM_MODEM_LOCATION_SOURCE_AGPS_MSA;

    if (enable_agps_msb_flag)
        sources |= MM_MODEM_LOCATION_SOURCE_AGPS_MSB;
    if (disable_agps_msb_flag)
        sources &= ~MM_MODEM_LOCATION_SOURCE_AGPS_MSB;

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

    if (enable_gps_unmanaged_flag)
        sources |= MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED;
    if (disable_gps_unmanaged_flag)
        sources &= ~MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED;

    return sources;
}

static gboolean
build_signals_location_from_flags (void)
{
    if (set_enable_signal_flag)
        return TRUE;
    if (set_disable_signal_flag)
        return FALSE;
    return mm_modem_location_signals_location (ctx->modem_location);
}

static void
get_location_process_reply (MMLocation3gpp *location_3gpp,
                            MMLocationGpsNmea *location_gps_nmea,
                            MMLocationGpsRaw *location_gps_raw,
                            MMLocationCdmaBs *location_cdma_bs,
                            const GError *error)
{
    gchar       **nmea = NULL;
    gchar        *mcc = NULL;
    gchar        *mnc = NULL;
    gchar        *lac = NULL;
    gchar        *tac = NULL;
    gchar        *cid = NULL;
    const gchar  *gps_utc = NULL;
    gchar        *gps_longitude = NULL;
    gchar        *gps_latitude = NULL;
    gchar        *gps_altitude = NULL;
    gchar        *cdma_bs_longitude = NULL;
    gchar        *cdma_bs_latitude = NULL;

    if (error) {
        g_printerr ("error: couldn't get location from the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    if (location_3gpp) {
        mcc = g_strdup_printf ("%u", mm_location_3gpp_get_mobile_country_code (location_3gpp));
        mnc = g_strdup_printf ("%u", mm_location_3gpp_get_mobile_network_code (location_3gpp));
        lac = g_strdup_printf ("%04lX", mm_location_3gpp_get_location_area_code (location_3gpp));
        tac = g_strdup_printf ("%04lX", mm_location_3gpp_get_tracking_area_code (location_3gpp));
        cid = g_strdup_printf ("%08lX", mm_location_3gpp_get_cell_id (location_3gpp));
    }

    if (location_gps_nmea)
        nmea = mm_location_gps_nmea_get_traces (location_gps_nmea);

    if (location_gps_raw) {
        gps_utc = mm_location_gps_raw_get_utc_time (location_gps_raw);
        gps_longitude = g_strdup_printf ("%lf", mm_location_gps_raw_get_longitude (location_gps_raw));
        gps_latitude  = g_strdup_printf ("%lf", mm_location_gps_raw_get_latitude  (location_gps_raw));
        gps_altitude  = g_strdup_printf ("%lf", mm_location_gps_raw_get_altitude  (location_gps_raw));
    }

    if (location_cdma_bs) {
        cdma_bs_longitude = g_strdup_printf ("%lf", mm_location_cdma_bs_get_longitude (location_cdma_bs));
        cdma_bs_latitude  = g_strdup_printf ("%lf", mm_location_cdma_bs_get_latitude  (location_cdma_bs));
    }

    mmcli_output_string_take           (MMC_F_LOCATION_3GPP_MCC,    mcc);
    mmcli_output_string_take           (MMC_F_LOCATION_3GPP_MNC,    mnc);
    mmcli_output_string_take           (MMC_F_LOCATION_3GPP_LAC,    lac);
    mmcli_output_string_take           (MMC_F_LOCATION_3GPP_TAC,    tac);
    mmcli_output_string_take           (MMC_F_LOCATION_3GPP_CID,    cid);
    mmcli_output_string_array_take     (MMC_F_LOCATION_GPS_NMEA,    nmea, TRUE);
    mmcli_output_string                (MMC_F_LOCATION_GPS_UTC,     gps_utc);
    mmcli_output_string_take           (MMC_F_LOCATION_GPS_LONG,    gps_longitude);
    mmcli_output_string_take           (MMC_F_LOCATION_GPS_LAT,     gps_latitude);
    mmcli_output_string_take           (MMC_F_LOCATION_GPS_ALT,     gps_altitude);
    mmcli_output_string_take           (MMC_F_LOCATION_CDMABS_LONG, cdma_bs_longitude);
    mmcli_output_string_take           (MMC_F_LOCATION_CDMABS_LAT,  cdma_bs_latitude);
    mmcli_output_dump ();

    g_clear_object (&location_3gpp);
    g_clear_object (&location_gps_nmea);
    g_clear_object (&location_gps_raw);
    g_clear_object (&location_cdma_bs);
}

static void
get_location_ready (MMModemLocation *modem_location,
                    GAsyncResult    *result)
{
    MMLocation3gpp *location_3gpp = NULL;
    MMLocationGpsNmea *location_gps_nmea = NULL;
    MMLocationGpsRaw *location_gps_raw = NULL;
    MMLocationCdmaBs *location_cdma_bs = NULL;
    GError *error = NULL;

    mm_modem_location_get_full_finish (modem_location,
                                       result,
                                       &location_3gpp,
                                       &location_gps_nmea,
                                       &location_gps_raw,
                                       &location_cdma_bs,
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
    if (any_location_setup_flag) {
        g_debug ("Asynchronously setting up location gathering...");
        mm_modem_location_setup (ctx->modem_location,
                                 build_sources_from_flags (),
                                 build_signals_location_from_flags (),
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)setup_ready,
                                 NULL);
        return;
    }

    /* Request to get location from the modem? */
    if (get_flag) {
        g_debug ("Asynchronously getting location from the modem...");
        mm_modem_location_get_full (ctx->modem_location,
                                    ctx->cancellable,
                                    (GAsyncReadyCallback)get_location_ready,
                                    NULL);
        return;
    }

    /* Request to set SUPL server? */
    if (set_supl_server_str) {
        g_debug ("Asynchronously setting SUPL server...");
        mm_modem_location_set_supl_server (ctx->modem_location,
                                           set_supl_server_str,
                                           ctx->cancellable,
                                           (GAsyncReadyCallback)set_supl_server_ready,
                                           NULL);
        return;
    }

    /* Request to inject assistance data? */
    if (inject_assistance_data_str) {
        guint8 *data;
        gsize   data_size;

        if (!parse_inject_assistance_data (&data, &data_size)) {
            g_printerr ("error: couldn't inject assistance data: invalid parameters given: '%s'\n",
                        inject_assistance_data_str);
            exit (EXIT_FAILURE);
        }

        g_debug ("Asynchronously injecting assistance data...");
        mm_modem_location_inject_assistance_data (ctx->modem_location,
                                                  data, data_size,
                                                  ctx->cancellable,
                                                  (GAsyncReadyCallback)inject_assistance_data_ready,
                                                  NULL);
        g_free (data);
        return;
    }

    /* Request to set GPS refresh rate? */
    if (set_gps_refresh_rate_str) {
        guint rate;

        if (!mm_get_uint_from_str (set_gps_refresh_rate_str, &rate)) {
            g_printerr ("error: couldn't set GPS refresh rate: invalid rate given: '%s'\n",
                        set_gps_refresh_rate_str);
            exit (EXIT_FAILURE);
        }
        g_debug ("Asynchronously setting GPS refresh rate...");
        mm_modem_location_set_gps_refresh_rate (ctx->modem_location,
                                                rate,
                                                ctx->cancellable,
                                                (GAsyncReadyCallback)set_gps_refresh_rate_ready,
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
    if (any_location_setup_flag) {
        gboolean result;

        g_debug ("Synchronously setting up location gathering...");
        result = mm_modem_location_setup_sync (ctx->modem_location,
                                               build_sources_from_flags (),
                                               build_signals_location_from_flags (),
                                               NULL,
                                               &error);
        setup_process_reply (result, error);
        return;
    }

    /* Request to get location from the modem? */
    if (get_flag) {
        MMLocation3gpp *location_3gpp = NULL;
        MMLocationGpsNmea *location_gps_nmea = NULL;
        MMLocationGpsRaw *location_gps_raw = NULL;
        MMLocationCdmaBs *location_cdma_bs = NULL;

        g_debug ("Synchronously getting location from the modem...");
        mm_modem_location_get_full_sync (ctx->modem_location,
                                         &location_3gpp,
                                         &location_gps_nmea,
                                         &location_gps_raw,
                                         &location_cdma_bs,
                                         NULL,
                                         &error);
        get_location_process_reply (location_3gpp, location_gps_nmea, location_gps_raw, location_cdma_bs, error);
        return;
    }

    /* Request to set SUPL server? */
    if (set_supl_server_str) {
        gboolean result;

        g_debug ("Synchronously setting SUPL server...");
        result = mm_modem_location_set_supl_server_sync (ctx->modem_location,
                                                         set_supl_server_str,
                                                         NULL,
                                                         &error);
        set_supl_server_process_reply (result, error);
        return;
    }

    /* Request to inject assistance data? */
    if (inject_assistance_data_str) {
        gboolean  result;
        guint8   *data;
        gsize     data_size;

        if (!parse_inject_assistance_data (&data, &data_size)) {
            g_printerr ("error: couldn't inject assistance data: invalid parameters given: '%s'\n",
                        inject_assistance_data_str);
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously setting assistance data...");
        result = mm_modem_location_inject_assistance_data_sync (ctx->modem_location,
                                                                data, data_size,
                                                                NULL,
                                                                &error);
        inject_assistance_data_process_reply (result, error);
        g_free (data);
        return;
    }

    /* Request to set GPS refresh rate? */
    if (set_gps_refresh_rate_str) {
        gboolean result;
        guint rate;

        if (!mm_get_uint_from_str (set_gps_refresh_rate_str, &rate)) {
            g_printerr ("error: couldn't set GPS refresh rate: invalid rate given: '%s'\n",
                        set_gps_refresh_rate_str);
            exit (EXIT_FAILURE);
        }

        g_debug ("Synchronously setting GPS refresh rate...");
        result = mm_modem_location_set_gps_refresh_rate_sync (ctx->modem_location,
                                                              rate,
                                                              NULL,
                                                              &error);
        set_gps_refresh_rate_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
