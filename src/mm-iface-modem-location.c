/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 * Copyright (C) 2012-2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"

#define MM_LOCATION_GPS_REFRESH_TIME_SECS 30

#define LOCATION_CONTEXT_TAG "location-context-tag"

static GQuark location_context_quark;

/*****************************************************************************/

void
mm_iface_modem_location_bind_simple_status (MMIfaceModemLocation *self,
                                            MMSimpleStatus *status)
{
}

/*****************************************************************************/

typedef struct {
    /* 3GPP location */
    MMLocation3gpp *location_3gpp;
    /* GPS location */
    time_t location_gps_nmea_last_time;
    MMLocationGpsNmea *location_gps_nmea;
    time_t location_gps_raw_last_time;
    MMLocationGpsRaw *location_gps_raw;
    /* CDMA BS location */
    MMLocationCdmaBs *location_cdma_bs;
} LocationContext;

static void
location_context_free (LocationContext *ctx)
{
    if (ctx->location_3gpp)
        g_object_unref (ctx->location_3gpp);
    if (ctx->location_gps_nmea)
        g_object_unref (ctx->location_gps_nmea);
    if (ctx->location_gps_raw)
        g_object_unref (ctx->location_gps_raw);
    if (ctx->location_cdma_bs)
        g_object_unref (ctx->location_cdma_bs);
    g_free (ctx);
}

static void
clear_location_context (MMIfaceModemLocation *self)
{
    if (G_UNLIKELY (!location_context_quark))
        location_context_quark =  (g_quark_from_static_string (
                                       LOCATION_CONTEXT_TAG));

    /* Clear all location data */
    g_object_set_qdata (G_OBJECT (self),
                        location_context_quark,
                        NULL);
}

static LocationContext *
get_location_context (MMIfaceModemLocation *self)
{
    LocationContext *ctx;

    if (G_UNLIKELY (!location_context_quark))
        location_context_quark =  (g_quark_from_static_string (
                                       LOCATION_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), location_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_new0 (LocationContext, 1);

        g_object_set_qdata_full (
            G_OBJECT (self),
            location_context_quark,
            ctx,
            (GDestroyNotify)location_context_free);
    }

    return ctx;
}

/*****************************************************************************/

static GVariant *
build_location_dictionary (GVariant *previous,
                           MMLocation3gpp *location_3gpp,
                           MMLocationGpsNmea *location_gps_nmea,
                           MMLocationGpsRaw *location_gps_raw,
                           MMLocationCdmaBs *location_cdma_bs)
{
    GVariant *location_3gpp_value = NULL;
    GVariant *location_gps_nmea_value = NULL;
    GVariant *location_gps_raw_value = NULL;
    GVariant *location_cdma_bs_value = NULL;
    GVariantBuilder builder;

    /* If a previous dictionary given, parse its values */
    if (previous) {
        guint source;
        GVariant *value;
        GVariantIter iter;

        g_variant_iter_init (&iter, previous);
        while (g_variant_iter_next (&iter, "{uv}", &source, &value)) {
            switch (source) {
            case MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI:
                g_assert (!location_3gpp_value);
                location_3gpp_value = value;
                break;
            case MM_MODEM_LOCATION_SOURCE_GPS_NMEA:
                g_assert (!location_gps_nmea_value);
                location_gps_nmea_value = value;
                break;
            case MM_MODEM_LOCATION_SOURCE_GPS_RAW:
                g_assert (!location_gps_raw_value);
                location_gps_raw_value = value;
                break;
            case MM_MODEM_LOCATION_SOURCE_CDMA_BS:
                g_assert (!location_cdma_bs_value);
                location_cdma_bs_value = value;
                break;
            case MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED:
                g_assert_not_reached ();
            case MM_MODEM_LOCATION_SOURCE_AGPS_MSA:
                g_assert_not_reached ();
            case MM_MODEM_LOCATION_SOURCE_AGPS_MSB:
                g_assert_not_reached ();
            default:
                g_warn_if_reached ();
                g_variant_unref (value);
                break;
            }
        }
    }

    /* Build the new one */
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{uv}"));

    /* If a new one given, use it */
    if (location_3gpp) {
        if (location_3gpp_value)
            g_variant_unref (location_3gpp_value);
        location_3gpp_value = mm_location_3gpp_get_string_variant (location_3gpp);
    }

    if (location_3gpp_value) {
        g_assert (!g_variant_is_floating (location_3gpp_value));
        g_variant_builder_add (&builder,
                               "{uv}",
                               MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI,
                               location_3gpp_value);
        g_variant_unref (location_3gpp_value);
    }

    /* If a new one given, use it */
    if (location_gps_nmea) {
        if (location_gps_nmea_value)
            g_variant_unref (location_gps_nmea_value);
        location_gps_nmea_value = mm_location_gps_nmea_get_string_variant (location_gps_nmea);
    }

    if (location_gps_nmea_value) {
        g_assert (!g_variant_is_floating (location_gps_nmea_value));
        g_variant_builder_add (&builder,
                               "{uv}",
                               MM_MODEM_LOCATION_SOURCE_GPS_NMEA,
                               location_gps_nmea_value);
        g_variant_unref (location_gps_nmea_value);
    }

    /* If a new one given, use it */
    if (location_gps_raw) {
        if (location_gps_raw_value)
            g_variant_unref (location_gps_raw_value);
        location_gps_raw_value = mm_location_gps_raw_get_dictionary (location_gps_raw);
    }

    if (location_gps_raw_value) {
        g_assert (!g_variant_is_floating (location_gps_raw_value));
        g_variant_builder_add (&builder,
                               "{uv}",
                               MM_MODEM_LOCATION_SOURCE_GPS_RAW,
                               location_gps_raw_value);
        g_variant_unref (location_gps_raw_value);
    }

    /* If a new one given, use it */
    if (location_cdma_bs) {
        if (location_cdma_bs_value)
            g_variant_unref (location_cdma_bs_value);
        location_cdma_bs_value = mm_location_cdma_bs_get_dictionary (location_cdma_bs);
    }

    if (location_cdma_bs_value) {
        g_assert (!g_variant_is_floating (location_cdma_bs_value));
        g_variant_builder_add (&builder,
                               "{uv}",
                               MM_MODEM_LOCATION_SOURCE_CDMA_BS,
                               location_cdma_bs_value);
        g_variant_unref (location_cdma_bs_value);
    }

    return g_variant_builder_end (&builder);
}

/*****************************************************************************/

static void
notify_gps_location_update (MMIfaceModemLocation *self,
                            MmGdbusModemLocation *skeleton,
                            MMLocationGpsNmea *location_gps_nmea,
                            MMLocationGpsRaw *location_gps_raw)
{
    mm_obj_dbg (self, "GPS location updated");

    /* We only update the property if we are supposed to signal
     * location */
    if (mm_gdbus_modem_location_get_signals_location (skeleton))
        mm_gdbus_modem_location_set_location (
            skeleton,
            build_location_dictionary (mm_gdbus_modem_location_get_location (skeleton),
                                       NULL,
                                       location_gps_nmea,
                                       location_gps_raw,
                                       NULL));
}

static void
location_gps_update_nmea (MMIfaceModemLocation *self,
                          const gchar          *nmea_trace)
{
    MmGdbusModemLocation *skeleton;
    LocationContext      *ctx;
    gboolean              update_nmea = FALSE;
    gboolean              update_raw = FALSE;

    ctx = get_location_context (self);
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (mm_gdbus_modem_location_get_enabled (skeleton) & MM_MODEM_LOCATION_SOURCE_GPS_NMEA) {
        g_assert (ctx->location_gps_nmea != NULL);
        if (mm_location_gps_nmea_add_trace (ctx->location_gps_nmea, nmea_trace) &&
            (ctx->location_gps_nmea_last_time == 0 ||
             time (NULL) - ctx->location_gps_nmea_last_time >= (glong)mm_gdbus_modem_location_get_gps_refresh_rate (skeleton))) {
            ctx->location_gps_nmea_last_time = time (NULL);
            update_nmea = TRUE;
        }
    }

    if (mm_gdbus_modem_location_get_enabled (skeleton) & MM_MODEM_LOCATION_SOURCE_GPS_RAW) {
        g_assert (ctx->location_gps_raw != NULL);
        if (mm_location_gps_raw_add_trace (ctx->location_gps_raw, nmea_trace) &&
            (ctx->location_gps_raw_last_time == 0 ||
             time (NULL) - ctx->location_gps_raw_last_time >= (glong)mm_gdbus_modem_location_get_gps_refresh_rate (skeleton))) {
            ctx->location_gps_raw_last_time = time (NULL);
            update_raw = TRUE;
        }
    }

    if (update_nmea || update_raw)
        notify_gps_location_update (self,
                                    skeleton,
                                    update_nmea ? ctx->location_gps_nmea : NULL,
                                    update_raw ? ctx->location_gps_raw : NULL);

    g_object_unref (skeleton);
}

void
mm_iface_modem_location_gps_update (MMIfaceModemLocation *self,
                                    const gchar          *nmea_trace)
{
    /* Helper to debug GPS location related issues. Don't depend on a real GPS
     * fix for debugging, just use some random values to update */
#if 0
    {
        const gchar *prefix = NULL;
        const gchar *lat = NULL;

        /* lat N/S just to test which one is used */
        if (g_str_has_prefix (nmea_trace, "$GPGGA")) {
            prefix = "GPGGA";
            lat = "S";
        } else if (g_str_has_prefix (nmea_trace, "$GNGGA")) {
            prefix = "GNGGA";
            lat = "N";
        }

        if (prefix && lat) {
            g_autoptr(GString)   str = NULL;
            g_autoptr(GDateTime) now = NULL;

            mm_obj_dbg (self, "GGA trace detected: %s", nmea_trace);

            now = g_date_time_new_now_utc ();
            str = g_string_new ("");
            g_string_append_printf (str,
                                    "$%s,%02u%02u%02u,4807.038,%s,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
                                    prefix,
                                    g_date_time_get_hour (now),
                                    g_date_time_get_minute (now),
                                    g_date_time_get_second (now),
                                    lat);
            location_gps_update_nmea (self, str->str);
            return;
        }
    }
#endif

    location_gps_update_nmea (self, nmea_trace);
}

/*****************************************************************************/

static void
notify_3gpp_location_update (MMIfaceModemLocation *self,
                             MmGdbusModemLocation *skeleton,
                             MMLocation3gpp *location_3gpp)
{
    mm_obj_dbg (self, "3GPP location updated "
                "(MCC: '%u', MNC: '%u', location area code: '%lX', tracking area code: '%lX', cell ID: '%lX')",
                mm_location_3gpp_get_mobile_country_code (location_3gpp),
                mm_location_3gpp_get_mobile_network_code (location_3gpp),
                mm_location_3gpp_get_location_area_code (location_3gpp),
                mm_location_3gpp_get_tracking_area_code (location_3gpp),
                mm_location_3gpp_get_cell_id (location_3gpp));

    /* We only update the property if we are supposed to signal
     * location */
    if (mm_gdbus_modem_location_get_signals_location (skeleton))
        mm_gdbus_modem_location_set_location (
            skeleton,
            build_location_dictionary (mm_gdbus_modem_location_get_location (skeleton),
                                       location_3gpp,
                                       NULL, NULL,
                                       NULL));
}

void
mm_iface_modem_location_3gpp_update_mcc_mnc (MMIfaceModemLocation *self,
                                             guint mobile_country_code,
                                             guint mobile_network_code)
{
    MmGdbusModemLocation *skeleton;
    LocationContext *ctx;

    ctx = get_location_context (self);
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (mm_gdbus_modem_location_get_enabled (skeleton) & MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI) {
        guint changed = 0;

        g_assert (ctx->location_3gpp != NULL);
        changed += mm_location_3gpp_set_mobile_country_code (ctx->location_3gpp,
                                                             mobile_country_code);
        changed += mm_location_3gpp_set_mobile_network_code (ctx->location_3gpp,
                                                             mobile_network_code);
        if (changed)
            notify_3gpp_location_update (self, skeleton, ctx->location_3gpp);
    }

    g_object_unref (skeleton);
}

void
mm_iface_modem_location_3gpp_update_lac_tac_ci (MMIfaceModemLocation *self,
                                                gulong                location_area_code,
                                                gulong                tracking_area_code,
                                                gulong                cell_id)
{
    g_autoptr(MmGdbusModemLocationSkeleton)  skeleton = NULL;
    LocationContext                         *ctx;
    guint                                    changed = 0;
    gulong                                   old_location_area_code;
    gulong                                   old_tracking_area_code;
    gulong                                   old_cell_id;

    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton || !(mm_gdbus_modem_location_get_enabled (MM_GDBUS_MODEM_LOCATION (skeleton)) & MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI))
        return;

    ctx = get_location_context (self);
    g_assert (ctx->location_3gpp != NULL);

    old_location_area_code = mm_location_3gpp_get_location_area_code (ctx->location_3gpp);
    old_tracking_area_code = mm_location_3gpp_get_tracking_area_code (ctx->location_3gpp);
    old_cell_id            = mm_location_3gpp_get_cell_id            (ctx->location_3gpp);

    /* Update LAC if given, and clear TAC unless a TAC is also given */
    if (location_area_code) {
        if (old_location_area_code != location_area_code) {
            mm_obj_dbg (self, "3GPP location area code updated: '%lX->%lX'", old_location_area_code, location_area_code);
            mm_location_3gpp_set_location_area_code (ctx->location_3gpp, location_area_code);
            changed++;
        }
        if (!tracking_area_code) {
            if (old_tracking_area_code != 0) {
                mm_obj_dbg (self, "3GPP tracking area code cleared: '%lX->%lX'", old_tracking_area_code, tracking_area_code);
                mm_location_3gpp_set_tracking_area_code (ctx->location_3gpp, 0);
                changed++;
            }
        }
    }
    /* Update TAC if given, and clear LAC unless a LAC is also given */
    if (tracking_area_code) {
        if (old_tracking_area_code != tracking_area_code) {
            mm_obj_dbg (self, "3GPP tracking area code updated: '%lX->%lX'", old_tracking_area_code, tracking_area_code);
            mm_location_3gpp_set_tracking_area_code (ctx->location_3gpp, tracking_area_code);
            changed++;
        }
        if (!location_area_code) {
            if (old_location_area_code != 0) {
                mm_obj_dbg (self, "3GPP location area code cleared: '%lX->%lX'", old_location_area_code, location_area_code);
                mm_location_3gpp_set_location_area_code (ctx->location_3gpp, 0);
                changed++;
            }
        }
    }

    /* Cell ID only updated if given. It is assumed that if LAC or TAC are given, CID is also given */
    if (cell_id && (old_cell_id != cell_id)) {
        mm_obj_dbg (self, "3GPP cell id updated: '%lX->%lX'", old_cell_id, cell_id);
        mm_location_3gpp_set_cell_id (ctx->location_3gpp, cell_id);
        changed++;
    }

    if (changed)
        notify_3gpp_location_update (self, MM_GDBUS_MODEM_LOCATION (skeleton), ctx->location_3gpp);
}

void
mm_iface_modem_location_3gpp_clear (MMIfaceModemLocation *self)
{
    MmGdbusModemLocation *skeleton;
    LocationContext *ctx;

    ctx = get_location_context (self);
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (mm_gdbus_modem_location_get_enabled (skeleton) & MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI) {
        g_assert (ctx->location_3gpp != NULL);
        if (mm_location_3gpp_reset (ctx->location_3gpp))
            notify_3gpp_location_update (self, skeleton, ctx->location_3gpp);
    }

    g_object_unref (skeleton);
}

/*****************************************************************************/

static void
notify_cdma_bs_location_update (MMIfaceModemLocation *self,
                                MmGdbusModemLocation *skeleton,
                                MMLocationCdmaBs *location_cdma_bs)
{
    mm_obj_dbg (self, "CDMA base station location updated (longitude: '%lf', latitude: '%lf')",
                mm_location_cdma_bs_get_longitude (location_cdma_bs),
                mm_location_cdma_bs_get_latitude (location_cdma_bs));

    /* We only update the property if we are supposed to signal
     * location */
    if (mm_gdbus_modem_location_get_signals_location (skeleton))
        mm_gdbus_modem_location_set_location (
            skeleton,
            build_location_dictionary (mm_gdbus_modem_location_get_location (skeleton),
                                       NULL,
                                       NULL, NULL,
                                       location_cdma_bs));
}

void
mm_iface_modem_location_cdma_bs_update (MMIfaceModemLocation *self,
                                        gdouble longitude,
                                        gdouble latitude)
{
    MmGdbusModemLocation *skeleton;
    LocationContext *ctx;

    ctx = get_location_context (self);
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (mm_gdbus_modem_location_get_enabled (skeleton) & MM_MODEM_LOCATION_SOURCE_CDMA_BS) {
        if (mm_location_cdma_bs_set (ctx->location_cdma_bs, longitude, latitude))
            notify_cdma_bs_location_update (self, skeleton, ctx->location_cdma_bs);
    }

    g_object_unref (skeleton);
}

void
mm_iface_modem_location_cdma_bs_clear (MMIfaceModemLocation *self)
{
    mm_iface_modem_location_cdma_bs_update (self,
                                            MM_LOCATION_LONGITUDE_UNKNOWN,
                                            MM_LOCATION_LATITUDE_UNKNOWN);
}

/*****************************************************************************/

static void
update_location_source_status (MMIfaceModemLocation *self,
                               MMModemLocationSource source,
                               gboolean enabled)
{
    MMModemLocationSource mask;
    MmGdbusModemLocation *skeleton = NULL;
    LocationContext *ctx;

    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    /* Update status in the interface */
    mask = mm_gdbus_modem_location_get_enabled (skeleton);
    if (enabled)
        mask |= source;
    else
        mask &= ~source;

    /* Update status in the context */
    ctx = get_location_context (self);

    switch (source) {
    case MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI:
        if (enabled) {
            if (!ctx->location_3gpp)
                ctx->location_3gpp = mm_location_3gpp_new ();
        } else
            g_clear_object (&ctx->location_3gpp);
        break;
    case MM_MODEM_LOCATION_SOURCE_GPS_NMEA:
        if (enabled) {
            if (!ctx->location_gps_nmea)
                ctx->location_gps_nmea = mm_location_gps_nmea_new ();
        } else
            g_clear_object (&ctx->location_gps_nmea);
        break;
    case MM_MODEM_LOCATION_SOURCE_GPS_RAW:
        if (enabled) {
            if (!ctx->location_gps_raw)
                ctx->location_gps_raw = mm_location_gps_raw_new ();
        } else
            g_clear_object (&ctx->location_gps_raw);
        break;
    case MM_MODEM_LOCATION_SOURCE_CDMA_BS:
        if (enabled) {
            if (!ctx->location_cdma_bs)
                ctx->location_cdma_bs = mm_location_cdma_bs_new ();
        } else
            g_clear_object (&ctx->location_cdma_bs);
        break;
    case MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED:
    case MM_MODEM_LOCATION_SOURCE_AGPS_MSA:
    case MM_MODEM_LOCATION_SOURCE_AGPS_MSB:
    case MM_MODEM_LOCATION_SOURCE_NONE:
        /* Nothing to setup in the context */
    default:
        break;
    }

    mm_gdbus_modem_location_set_enabled (skeleton, mask);

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemLocation *skeleton;
    MMModemLocationSource to_enable;
    MMModemLocationSource to_disable;
    MMModemLocationSource current;
} SetupGatheringContext;

static void setup_gathering_step (GTask *task);

static void
setup_gathering_context_free (SetupGatheringContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
setup_gathering_finish (MMIfaceModemLocation *self,
                        GAsyncResult *res,
                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enable_location_gathering_ready (MMIfaceModemLocation *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    SetupGatheringContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering_finish (self, res, &error)) {
        gchar *str;

        update_location_source_status (self, ctx->current, FALSE);

        str = mm_modem_location_source_build_string_from_mask (ctx->current);
        g_prefix_error (&error,
                        "Couldn't enable location '%s' gathering: ",
                        str);
        g_task_return_error (task, error);
        g_object_unref (task);
        g_free (str);
        return;
    }

    /* Keep on with next ones... */
    ctx->current = ctx->current << 1;
    setup_gathering_step (task);
}

static void
disable_location_gathering_ready (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    SetupGatheringContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering_finish (self, res, &error)) {
        gchar *str;

        /* Back to enabled then */
        update_location_source_status (self, ctx->current, TRUE);

        str = mm_modem_location_source_build_string_from_mask (ctx->current);
        g_prefix_error (&error,
                        "Couldn't disable location '%s' gathering: ",
                        str);
        g_task_return_error (task, error);
        g_object_unref (task);
        g_free (str);
        return;
    }

    /* Keep on with next ones... */
    ctx->current = ctx->current << 1;
    setup_gathering_step (task);
}

static void
setup_gathering_step (GTask *task)
{
    MMIfaceModemLocation *self;
    SetupGatheringContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* Are we done? */
    if (ctx->to_enable == MM_MODEM_LOCATION_SOURCE_NONE &&
        ctx->to_disable == MM_MODEM_LOCATION_SOURCE_NONE) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    while (ctx->current <= MM_MODEM_LOCATION_SOURCE_LAST) {
        gchar *source_str;

        if (ctx->to_enable & ctx->current) {
            /* Remove from mask */
            ctx->to_enable &= ~ctx->current;

            /* We update the location source status before launching the
             * specific actions to enable the gathering, so that we are
             * able to get location updates while the gathering gets
             * enabled. */
            update_location_source_status (self, ctx->current, TRUE);

            /* Plugins can run custom actions to enable location gathering */
            if (MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering &&
                MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering_finish) {
                MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering (
                    MM_IFACE_MODEM_LOCATION (self),
                    ctx->current,
                    (GAsyncReadyCallback)enable_location_gathering_ready,
                    task);
                return;
            }

            source_str = mm_modem_location_source_build_string_from_mask (ctx->current);
            mm_obj_dbg (self, "enabled location '%s' gathering...", source_str);
            g_free (source_str);
        } else if (ctx->to_disable & ctx->current) {
            /* Remove from mask */
            ctx->to_disable &= ~ctx->current;

            update_location_source_status (self, ctx->current, FALSE);

            /* Plugins can run custom actions to disable location gathering */
            if (MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering &&
                MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering_finish) {
                MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering (
                    MM_IFACE_MODEM_LOCATION (self),
                    ctx->current,
                    (GAsyncReadyCallback)disable_location_gathering_ready,
                    task);
                return;
            }

            source_str = mm_modem_location_source_build_string_from_mask (ctx->current);
            mm_obj_dbg (self, "disabled location '%s' gathering...", source_str);
            g_free (source_str);
        }

        /* go on... */
        ctx->current = ctx->current << 1;
    }

    /* We just need to finish now */
    g_assert (ctx->to_enable == MM_MODEM_LOCATION_SOURCE_NONE);
    g_assert (ctx->to_disable == MM_MODEM_LOCATION_SOURCE_NONE);
    setup_gathering_step (task);
}

static void
setup_gathering (MMIfaceModemLocation *self,
                 MMModemLocationSource mask,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    SetupGatheringContext *ctx;
    GTask *task;
    MMModemLocationSource currently_enabled;
    MMModemLocationSource source;
    gchar *str;
    gboolean allow_gps_unmanaged_always = FALSE;

    ctx = g_new (SetupGatheringContext, 1);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)setup_gathering_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON,              &ctx->skeleton,
                  MM_IFACE_MODEM_LOCATION_ALLOW_GPS_UNMANAGED_ALWAYS, &allow_gps_unmanaged_always,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    /* Get current list of enabled sources */
    currently_enabled = mm_gdbus_modem_location_get_enabled (ctx->skeleton);

    /* Reset the list of sources to enable or disable */
    ctx->to_enable = MM_MODEM_LOCATION_SOURCE_NONE;
    ctx->to_disable = MM_MODEM_LOCATION_SOURCE_NONE;

    /* Loop through all known bits in the bitmask to enable/disable specific location sources */
    for (source = MM_MODEM_LOCATION_SOURCE_FIRST;
         source <= MM_MODEM_LOCATION_SOURCE_LAST;
         source = source << 1) {
        /* skip unsupported sources */
        if (!(mm_gdbus_modem_location_get_capabilities (ctx->skeleton) & source))
            continue;

        str = mm_modem_location_source_build_string_from_mask (source);

        if (mask & source) {
            /* Source set in mask, need to enable if disabled */
            if (currently_enabled & source)
                mm_obj_dbg (self, "location '%s' gathering is already enabled...", str);
            else
                ctx->to_enable |= source;
        } else {
            /* Source unset in mask, need to disable if enabled */
            if (currently_enabled & source)
                ctx->to_disable |= source;
            else
                mm_obj_dbg (self, "location '%s' gathering is already disabled...", str);
        }

        g_free (str);
    }

    /* When standard GPS retrieval (RAW/NMEA) is enabled, we cannot enable the
     * UNMANAGED setup, and viceversa, unless explicitly allowed to do so by the
     * plugin implementation (e.g. if the RAW/NMEA sources don't use the same TTY
     * as the GPS UNMANAGED setup. */
    if (!allow_gps_unmanaged_always &&
        ((ctx->to_enable & MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED &&
          currently_enabled & (MM_MODEM_LOCATION_SOURCE_GPS_RAW | MM_MODEM_LOCATION_SOURCE_GPS_NMEA)) ||
         (ctx->to_enable & (MM_MODEM_LOCATION_SOURCE_GPS_RAW | MM_MODEM_LOCATION_SOURCE_GPS_NMEA) &&
          currently_enabled & MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED) ||
         (ctx->to_enable & (MM_MODEM_LOCATION_SOURCE_GPS_RAW | MM_MODEM_LOCATION_SOURCE_GPS_NMEA) &&
          ctx->to_enable & MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED))) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot have both unmanaged GPS and raw/nmea GPS enabled at the same time");
        g_object_unref (task);
        return;
    }

    /* MSA A-GPS and MSB A-GPS cannot be set at the same time */
    if ((ctx->to_enable & MM_MODEM_LOCATION_SOURCE_AGPS_MSA &&
         currently_enabled & MM_MODEM_LOCATION_SOURCE_AGPS_MSB) ||
        (ctx->to_enable & MM_MODEM_LOCATION_SOURCE_AGPS_MSB &&
         currently_enabled & MM_MODEM_LOCATION_SOURCE_AGPS_MSA) ||
        (ctx->to_enable & MM_MODEM_LOCATION_SOURCE_AGPS_MSA &&
         ctx->to_enable & MM_MODEM_LOCATION_SOURCE_AGPS_MSB)) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Cannot have both MSA A-GPS and MSB A-GPS enabled at the same time");
        g_object_unref (task);
        return;
    }

    if (ctx->to_enable != MM_MODEM_LOCATION_SOURCE_NONE) {
        str = mm_modem_location_source_build_string_from_mask (ctx->to_enable);
        mm_obj_dbg (self, "need to enable the following location sources: '%s'", str);
        g_free (str);
    }

    if (ctx->to_disable != MM_MODEM_LOCATION_SOURCE_NONE) {
        str = mm_modem_location_source_build_string_from_mask (ctx->to_disable);
        mm_obj_dbg (self, "need to disable the following location sources: '%s'", str);
        g_free (str);
    }

    /* Start enabling/disabling location sources */
    ctx->current = MM_MODEM_LOCATION_SOURCE_FIRST;
    setup_gathering_step (task);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemLocation *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemLocation *self;
    guint32 sources;
    gboolean signal_location;
} HandleSetupContext;

static void
handle_setup_context_free (HandleSetupContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
setup_gathering_ready (MMIfaceModemLocation *self,
                       GAsyncResult *res,
                       HandleSetupContext *ctx)
{
    GError *error = NULL;

    if (!setup_gathering_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_location_complete_setup (ctx->skeleton, ctx->invocation);

    handle_setup_context_free (ctx);
}

static void
handle_setup_auth_ready (MMBaseModem *self,
                         GAsyncResult *res,
                         HandleSetupContext *ctx)
{
    GError *error = NULL;
    MMModemState modem_state;
    MMModemLocationSource not_supported;
    LocationContext *location_ctx;
    gchar *str;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_setup_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot setup location: "
                                               "device not yet enabled");
        handle_setup_context_free (ctx);
        return;
    }

    /* If any of the location sources being enabled is NOT supported, set error */
    not_supported = ((mm_gdbus_modem_location_get_capabilities (ctx->skeleton) ^ ctx->sources) & ctx->sources);
    if (not_supported != MM_MODEM_LOCATION_SOURCE_NONE) {
        str = mm_modem_location_source_build_string_from_mask (not_supported);
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot enable unsupported location sources: '%s'",
                                               str);
        handle_setup_context_free (ctx);
        g_free (str);
        return;
    }

    /* Enable/disable location signaling */
    location_ctx = get_location_context (ctx->self);
    if (mm_gdbus_modem_location_get_signals_location (ctx->skeleton) != ctx->signal_location) {
        mm_obj_dbg (self, "%s location signaling",
                    ctx->signal_location ? "enabling" : "disabling");
        mm_gdbus_modem_location_set_signals_location (ctx->skeleton,
                                                      ctx->signal_location);
        if (ctx->signal_location)
            mm_gdbus_modem_location_set_location (
                ctx->skeleton,
                build_location_dictionary (mm_gdbus_modem_location_get_location (ctx->skeleton),
                                           location_ctx->location_3gpp,
                                           location_ctx->location_gps_nmea,
                                           location_ctx->location_gps_raw,
                                           location_ctx->location_cdma_bs));
        else
            mm_gdbus_modem_location_set_location (
                ctx->skeleton,
                build_location_dictionary (NULL, NULL, NULL, NULL, NULL));
    }

    str = mm_modem_location_source_build_string_from_mask (ctx->sources);
    mm_obj_dbg (self, "setting up location sources: '%s'", str);
    g_free (str);

    /* Go on to enable or disable the requested sources */
    setup_gathering (ctx->self,
                     ctx->sources,
                     (GAsyncReadyCallback)setup_gathering_ready,
                     ctx);
}

static gboolean
handle_setup (MmGdbusModemLocation *skeleton,
              GDBusMethodInvocation *invocation,
              guint32 sources,
              gboolean signal_location,
              MMIfaceModemLocation *self)
{
    HandleSetupContext *ctx;

    ctx = g_new (HandleSetupContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->sources = sources;
    ctx->signal_location = signal_location;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_setup_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemLocation *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemLocation *self;
    gchar *supl;
} HandleSetSuplServerContext;

static void
handle_set_supl_server_context_free (HandleSetSuplServerContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->supl);
    g_slice_free (HandleSetSuplServerContext, ctx);
}

static void
set_supl_server_ready (MMIfaceModemLocation *self,
                       GAsyncResult *res,
                       HandleSetSuplServerContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->set_supl_server_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem_location_set_supl_server (ctx->skeleton, ctx->supl);
        mm_gdbus_modem_location_complete_set_supl_server (ctx->skeleton, ctx->invocation);
    }

    handle_set_supl_server_context_free (ctx);
}

static void
handle_set_supl_server_auth_ready (MMBaseModem *self,
                                   GAsyncResult *res,
                                   HandleSetSuplServerContext *ctx)
{
    GError *error = NULL;
    MMModemState modem_state;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_supl_server_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set SUPL server: "
                                               "device not yet enabled");
        handle_set_supl_server_context_free (ctx);
        return;
    }

    /* If A-GPS is NOT supported, set error */
    if (!(mm_gdbus_modem_location_get_capabilities (ctx->skeleton) & (MM_MODEM_LOCATION_SOURCE_AGPS_MSA | MM_MODEM_LOCATION_SOURCE_AGPS_MSB))) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set SUPL server: A-GPS not supported");
        handle_set_supl_server_context_free (ctx);
        return;
    }

    /* Validate SUPL address string: either FQDN:PORT or IP:PORT */
    if (!mm_parse_supl_address (ctx->supl, NULL, NULL, NULL, &error)) {
        g_dbus_method_invocation_return_gerror (ctx->invocation, error);
        handle_set_supl_server_context_free (ctx);
        return;
    }

    /* Check if plugin implements it */
    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->set_supl_server ||
        !MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->set_supl_server_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set SUPL server: not implemented");
        handle_set_supl_server_context_free (ctx);
        return;
    }

    /* Request to change SUPL server */
    MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->set_supl_server (ctx->self,
                                                                   ctx->supl,
                                                                   (GAsyncReadyCallback)set_supl_server_ready,
                                                                   ctx);
}

static gboolean
handle_set_supl_server (MmGdbusModemLocation *skeleton,
                        GDBusMethodInvocation *invocation,
                        const gchar *supl,
                        MMIfaceModemLocation *self)
{
    HandleSetSuplServerContext *ctx;

    ctx = g_slice_new (HandleSetSuplServerContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->supl = g_strdup (supl);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_supl_server_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemLocation  *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemLocation  *self;
    GVariant              *datav;
} HandleInjectAssistanceDataContext;

static void
handle_inject_assistance_data_context_free (HandleInjectAssistanceDataContext *ctx)
{
    g_object_unref  (ctx->skeleton);
    g_object_unref  (ctx->invocation);
    g_object_unref  (ctx->self);
    g_variant_unref (ctx->datav);
    g_slice_free    (HandleInjectAssistanceDataContext, ctx);
}

static void
inject_assistance_data_ready (MMIfaceModemLocation              *self,
                              GAsyncResult                      *res,
                              HandleInjectAssistanceDataContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->inject_assistance_data_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_location_complete_inject_assistance_data (ctx->skeleton, ctx->invocation);

    handle_inject_assistance_data_context_free (ctx);
}

static void
handle_inject_assistance_data_auth_ready (MMBaseModem                       *self,
                                          GAsyncResult                      *res,
                                          HandleInjectAssistanceDataContext *ctx)
{
    GError       *error = NULL;
    const guint8 *data;
    gsize         data_size;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_inject_assistance_data_context_free (ctx);
        return;
    }

    /* If the type is NOT supported, set error */
    if (mm_gdbus_modem_location_get_supported_assistance_data (ctx->skeleton) == MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot inject assistance data: ununsupported");
        handle_inject_assistance_data_context_free (ctx);
        return;
    }

    /* Check if plugin implements it */
    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->inject_assistance_data ||
        !MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->inject_assistance_data_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot inject assistance data: not implemented");
        handle_inject_assistance_data_context_free (ctx);
        return;
    }

    data = (const guint8 *) g_variant_get_fixed_array (ctx->datav, &data_size, sizeof (guint8));

    /* Request to inject assistance data */
    MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->inject_assistance_data (ctx->self,
                                                                          data,
                                                                          data_size,
                                                                          (GAsyncReadyCallback)inject_assistance_data_ready,
                                                                          ctx);
}

static gboolean
handle_inject_assistance_data (MmGdbusModemLocation  *skeleton,
                               GDBusMethodInvocation *invocation,
                               GVariant              *datav,
                               MMIfaceModemLocation  *self)
{
    HandleInjectAssistanceDataContext *ctx;

    ctx = g_slice_new (HandleInjectAssistanceDataContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);
    ctx->datav      = g_variant_ref (datav);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_inject_assistance_data_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemLocation *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemLocation *self;
    guint rate;
} HandleSetGpsRefreshRateContext;

static void
handle_set_gps_refresh_rate_context_free (HandleSetGpsRefreshRateContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetGpsRefreshRateContext, ctx);
}

static void
handle_set_gps_refresh_rate_auth_ready (MMBaseModem *self,
                                        GAsyncResult *res,
                                        HandleSetGpsRefreshRateContext *ctx)
{
    GError *error = NULL;
    MMModemState modem_state;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_gps_refresh_rate_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set SUPL server: "
                                               "device not yet enabled");
        handle_set_gps_refresh_rate_context_free (ctx);
        return;
    }

    /* If GPS is NOT supported, set error */
    if (!(mm_gdbus_modem_location_get_capabilities (ctx->skeleton) & ((MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                                                       MM_MODEM_LOCATION_SOURCE_GPS_NMEA)))) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set GPS refresh rate: GPS not supported");
        handle_set_gps_refresh_rate_context_free (ctx);
        return;
    }

    /* Set the new rate in the interface */
    mm_gdbus_modem_location_set_gps_refresh_rate (ctx->skeleton, ctx->rate);
    mm_gdbus_modem_location_complete_set_gps_refresh_rate (ctx->skeleton, ctx->invocation);
    handle_set_gps_refresh_rate_context_free (ctx);
}

static gboolean
handle_set_gps_refresh_rate (MmGdbusModemLocation *skeleton,
                             GDBusMethodInvocation *invocation,
                             guint rate,
                             MMIfaceModemLocation *self)
{
    HandleSetGpsRefreshRateContext *ctx;

    ctx = g_slice_new (HandleSetGpsRefreshRateContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->rate = rate;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_gps_refresh_rate_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemLocation *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemLocation *self;
} HandleGetLocationContext;

static void
handle_get_location_context_free (HandleGetLocationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_get_location_auth_ready (MMBaseModem *self,
                                GAsyncResult *res,
                                HandleGetLocationContext *ctx)
{
    MMModemState modem_state;
    LocationContext *location_ctx;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_get_location_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);
    if (modem_state < MM_MODEM_STATE_ENABLED) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot get location: "
                                               "device not yet enabled");
        handle_get_location_context_free (ctx);
        return;
    }

    location_ctx = get_location_context (ctx->self);
    mm_gdbus_modem_location_complete_get_location (
        ctx->skeleton,
        ctx->invocation,
        build_location_dictionary (NULL,
                                   location_ctx->location_3gpp,
                                   location_ctx->location_gps_nmea,
                                   location_ctx->location_gps_raw,
                                   location_ctx->location_cdma_bs));
    handle_get_location_context_free (ctx);
}

static gboolean
handle_get_location (MmGdbusModemLocation *skeleton,
                     GDBusMethodInvocation *invocation,
                     MMIfaceModemLocation *self)
{
    HandleGetLocationContext *ctx;

    ctx = g_new (HandleGetLocationContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_LOCATION,
                             (GAsyncReadyCallback)handle_get_location_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (GTask *task);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_GATHERING,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    DisablingStep step;
    MmGdbusModemLocation *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_location_disable_finish (MMIfaceModemLocation *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disabling_location_gathering_ready (MMIfaceModemLocation *self,
                                    GAsyncResult *res,
                                    GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    if (!setup_gathering_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
interface_disabling_step (GTask *task)
{
    MMIfaceModemLocation *self;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_GATHERING:
        setup_gathering (self,
                         MM_MODEM_LOCATION_SOURCE_NONE,
                         (GAsyncReadyCallback)disabling_location_gathering_ready,
                         task);
        return;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        clear_location_context (self);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_location_disable (MMIfaceModemLocation *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DisablingContext *ctx;
    GTask *task;

    ctx = g_new0 (DisablingContext, 1);
    ctx->step = DISABLING_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)disabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_disabling_step (task);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (GTask *task);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_ENABLE_GATHERING,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModemLocation *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_location_enable_finish (MMIfaceModemLocation *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enabling_location_gathering_ready (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    if (!setup_gathering_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
interface_enabling_step (GTask *task)
{
    MMIfaceModemLocation *self;
    EnablingContext *ctx;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_GATHERING: {
        MMModemLocationSource default_sources;

        /* By default, we'll enable all NON-GPS sources */
        default_sources = mm_gdbus_modem_location_get_capabilities (ctx->skeleton);
        default_sources &= ~(MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                             MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                             MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED |
                             MM_MODEM_LOCATION_SOURCE_AGPS_MSA |
                             MM_MODEM_LOCATION_SOURCE_AGPS_MSB);

        setup_gathering (self,
                         default_sources,
                         (GAsyncReadyCallback)enabling_location_gathering_ready,
                         task);
        return;
    }

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_location_enable (MMIfaceModemLocation *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    EnablingContext *ctx;
    GTask *task;

    ctx = g_new0 (EnablingContext, 1);
    ctx->step = ENABLING_STEP_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)enabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_enabling_step (task);
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CAPABILITIES,
    INITIALIZATION_STEP_VALIDATE_CAPABILITIES,
    INITIALIZATION_STEP_SUPL_SERVER,
    INITIALIZATION_STEP_SUPPORTED_ASSISTANCE_DATA,
    INITIALIZATION_STEP_ASSISTANCE_DATA_SERVERS,
    INITIALIZATION_STEP_GPS_REFRESH_RATE,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModemLocation *skeleton;
    InitializationStep step;
    MMModemLocationSource capabilities;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static void
load_assistance_data_servers_ready (MMIfaceModemLocation *self,
                                    GAsyncResult         *res,
                                    GTask                *task)
{
    GError                 *error = NULL;
    InitializationContext  *ctx;
    gchar                 **servers;

    ctx = g_task_get_task_data (task);

    servers = MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_assistance_data_servers_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load assistance data servers: %s", error->message);
        g_error_free (error);
    }

    mm_gdbus_modem_location_set_assistance_data_servers (ctx->skeleton, (const gchar *const *)servers);
    g_strfreev (servers);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_supported_assistance_data_ready (MMIfaceModemLocation *self,
                                      GAsyncResult         *res,
                                      GTask                *task)
{
    GError                            *error = NULL;
    MMModemLocationAssistanceDataType  mask;
    InitializationContext             *ctx;

    ctx = g_task_get_task_data (task);

    mask = MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_supported_assistance_data_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load supported assistance data types: %s", error->message);
        g_error_free (error);
    }

    mm_gdbus_modem_location_set_supported_assistance_data (ctx->skeleton, (guint32) mask);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_supl_server_ready (MMIfaceModemLocation *self,
                        GAsyncResult *res,
                        GTask *task)
{
    GError *error = NULL;
    gchar *supl;
    InitializationContext *ctx;

    ctx = g_task_get_task_data (task);

    supl = MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_supl_server_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load SUPL server: %s", error->message);
        g_error_free (error);
    }

    mm_gdbus_modem_location_set_supl_server (ctx->skeleton, supl ? supl : "");
    g_free (supl);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_capabilities_ready (MMIfaceModemLocation *self,
                         GAsyncResult *res,
                         GTask *task)
{
    GError *error = NULL;
    InitializationContext *ctx;

    ctx = g_task_get_task_data (task);

    ctx->capabilities = MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_capabilities_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load location capabilities: %s", error->message);
        g_error_free (error);
    }

    mm_gdbus_modem_location_set_capabilities (ctx->skeleton, ctx->capabilities);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModemLocation *self;
    InitializationContext *ctx;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_CAPABILITIES:
        /* Location capabilities value is meant to be loaded only once during
         * the whole lifetime of the modem. Therefore, if we already have it
         * loaded, don't try to load it again. */
        if (!mm_gdbus_modem_location_get_capabilities (ctx->skeleton) &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_capabilities &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_capabilities_finish) {
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_capabilities (
                self,
                (GAsyncReadyCallback)load_capabilities_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_VALIDATE_CAPABILITIES:
        /* If the modem doesn't support any location capabilities, we won't export
         * the interface. We just report an UNSUPPORTED error. */
        if (ctx->capabilities == MM_MODEM_LOCATION_SOURCE_NONE) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "The modem doesn't have location capabilities");
            g_object_unref (task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_SUPL_SERVER:
        /* If the modem supports A-GPS, load SUPL server */
        if ((ctx->capabilities & (MM_MODEM_LOCATION_SOURCE_AGPS_MSA |
                                  MM_MODEM_LOCATION_SOURCE_AGPS_MSB)) &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_supl_server &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_supl_server_finish) {
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_supl_server (
                self,
                (GAsyncReadyCallback)load_supl_server_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_SUPPORTED_ASSISTANCE_DATA:
        /* If the modem supports any GPS-related technology, check assistance data types supported */
        if ((ctx->capabilities & (MM_MODEM_LOCATION_SOURCE_AGPS_MSA |
                                  MM_MODEM_LOCATION_SOURCE_AGPS_MSB |
                                  MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                  MM_MODEM_LOCATION_SOURCE_GPS_NMEA)) &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_supported_assistance_data &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_supported_assistance_data_finish) {
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_supported_assistance_data (
                self,
                (GAsyncReadyCallback)load_supported_assistance_data_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_ASSISTANCE_DATA_SERVERS:
        /* If any assistance data supported, load servers */
        if ((mm_gdbus_modem_location_get_supported_assistance_data (ctx->skeleton) != MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE) &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_assistance_data_servers &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_assistance_data_servers_finish) {
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_assistance_data_servers (
                self,
                (GAsyncReadyCallback)load_assistance_data_servers_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_GPS_REFRESH_RATE:
        /* If we have GPS capabilities, expose the GPS refresh rate */
        if (ctx->capabilities & ((MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                  MM_MODEM_LOCATION_SOURCE_GPS_NMEA)))
            /* Set the default rate in the interface */
            mm_gdbus_modem_location_set_gps_refresh_rate (ctx->skeleton, MM_LOCATION_GPS_REFRESH_TIME_SECS);

        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-setup",
                          G_CALLBACK (handle_setup),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-supl-server",
                          G_CALLBACK (handle_set_supl_server),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-inject-assistance-data",
                          G_CALLBACK (handle_inject_assistance_data),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-gps-refresh-rate",
                          G_CALLBACK (handle_set_gps_refresh_rate),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-get-location",
                          G_CALLBACK (handle_get_location),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_location (MM_GDBUS_OBJECT_SKELETON (self),
                                                     MM_GDBUS_MODEM_LOCATION (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_location_initialize_finish (MMIfaceModemLocation *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_location_initialize (MMIfaceModemLocation *self,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemLocation *skeleton = NULL;
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_location_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem_location_set_capabilities (skeleton, MM_MODEM_LOCATION_SOURCE_NONE);
        mm_gdbus_modem_location_set_supported_assistance_data (skeleton, MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE);
        mm_gdbus_modem_location_set_enabled (skeleton, MM_MODEM_LOCATION_SOURCE_NONE);
        mm_gdbus_modem_location_set_signals_location (skeleton, FALSE);
        mm_gdbus_modem_location_set_location (skeleton,
                                              build_location_dictionary (NULL, NULL, NULL, NULL, NULL));

        g_object_set (self,
                      MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */

    ctx = g_new0 (InitializationContext, 1);
    ctx->capabilities = MM_MODEM_LOCATION_SOURCE_NONE;
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    interface_initialization_step (task);
}

void
mm_iface_modem_location_shutdown (MMIfaceModemLocation *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_location (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_location_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_LOCATION_DBUS_SKELETON,
                              "Location DBus skeleton",
                              "DBus skeleton for the Location interface",
                              MM_GDBUS_TYPE_MODEM_LOCATION_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_LOCATION_ALLOW_GPS_UNMANAGED_ALWAYS,
                               "Allow unmanaged GPS always",
                               "Whether to always allow GPS unmanaged, even when raw/nmea GPS sources are enabled",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    initialized = TRUE;
}

GType
mm_iface_modem_location_get_type (void)
{
    static GType iface_modem_location_type = 0;

    if (!G_UNLIKELY (iface_modem_location_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemLocation), /* class_size */
            iface_modem_location_init,     /* base_init */
            NULL,                          /* base_finalize */
        };

        iface_modem_location_type = g_type_register_static (G_TYPE_INTERFACE,
                                                            "MMIfaceModemLocation",
                                                            &info,
                                                            0);

        g_type_interface_add_prerequisite (iface_modem_location_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_location_type;
}
