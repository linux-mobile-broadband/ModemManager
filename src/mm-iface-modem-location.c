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
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 */

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-log.h"

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
                location_3gpp_value = value;
                break;
            case MM_MODEM_LOCATION_SOURCE_GPS_NMEA:
                location_gps_nmea_value = value;
                break;
            case MM_MODEM_LOCATION_SOURCE_GPS_RAW:
                location_gps_raw_value = value;
                break;
            case MM_MODEM_LOCATION_SOURCE_CDMA_BS:
                location_cdma_bs_value = value;
                break;
            default:
                g_warn_if_reached ();
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
        g_variant_builder_add (&builder,
                               "{uv}",
                               MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI,
                               location_3gpp_value);
    }

    /* If a new one given, use it */
    if (location_gps_nmea) {
        if (location_gps_nmea_value)
            g_variant_unref (location_gps_nmea_value);
        location_gps_nmea_value = mm_location_gps_nmea_get_string_variant (location_gps_nmea);
    }

    if (location_gps_nmea_value)
        g_variant_builder_add (&builder,
                               "{uv}",
                               MM_MODEM_LOCATION_SOURCE_GPS_NMEA,
                               location_gps_nmea_value);

    /* If a new one given, use it */
    if (location_gps_raw) {
        if (location_gps_raw_value)
            g_variant_unref (location_gps_raw_value);
        location_gps_raw_value = mm_location_gps_raw_get_dictionary (location_gps_raw);
    }

    if (location_gps_raw_value)
        g_variant_builder_add (&builder,
                               "{uv}",
                               MM_MODEM_LOCATION_SOURCE_GPS_RAW,
                               location_gps_raw_value);

    /* If a new one given, use it */
    if (location_cdma_bs) {
        if (location_cdma_bs_value)
            g_variant_unref (location_cdma_bs_value);
        location_cdma_bs_value = mm_location_cdma_bs_get_dictionary (location_cdma_bs);
    }

    if (location_cdma_bs_value)
        g_variant_builder_add (&builder,
                               "{uv}",
                               MM_MODEM_LOCATION_SOURCE_CDMA_BS,
                               location_cdma_bs_value);

    return g_variant_builder_end (&builder);
}

/*****************************************************************************/

static void
notify_gps_location_update (MMIfaceModemLocation *self,
                            MmGdbusModemLocation *skeleton,
                            MMLocationGpsNmea *location_gps_nmea,
                            MMLocationGpsRaw *location_gps_raw)
{
    const gchar *dbus_path;

    dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
    mm_info ("Modem %s: GPS location updated",
             dbus_path);

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

void
mm_iface_modem_location_gps_update (MMIfaceModemLocation *self,
                                    const gchar *nmea_trace)
{
    MmGdbusModemLocation *skeleton;
    LocationContext *ctx;
    gboolean update_nmea = FALSE;
    gboolean update_raw = FALSE;

    ctx = get_location_context (self);
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (mm_gdbus_modem_location_get_enabled (skeleton) & MM_MODEM_LOCATION_SOURCE_GPS_NMEA) {
        g_assert (ctx->location_gps_nmea != NULL);
        if (mm_location_gps_nmea_add_trace (ctx->location_gps_nmea, nmea_trace) &&
            ctx->location_gps_nmea_last_time >= MM_LOCATION_GPS_REFRESH_TIME_SECS) {
            ctx->location_gps_nmea_last_time = time (NULL);
            update_nmea = TRUE;
        }
    }

    if (mm_gdbus_modem_location_get_enabled (skeleton) & MM_MODEM_LOCATION_SOURCE_GPS_RAW) {
        g_assert (ctx->location_gps_raw != NULL);
        if (mm_location_gps_raw_add_trace (ctx->location_gps_raw, nmea_trace) &&
            ctx->location_gps_raw_last_time >= MM_LOCATION_GPS_REFRESH_TIME_SECS) {
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

/*****************************************************************************/

static void
notify_3gpp_location_update (MMIfaceModemLocation *self,
                             MmGdbusModemLocation *skeleton,
                             MMLocation3gpp *location_3gpp)
{
    const gchar *dbus_path;

    dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
    mm_info ("Modem %s: 3GPP location updated "
             "(MCC: '%u', MNC: '%u', Location area code: '%lX', Cell ID: '%lX')",
             dbus_path,
             mm_location_3gpp_get_mobile_country_code (location_3gpp),
             mm_location_3gpp_get_mobile_network_code (location_3gpp),
             mm_location_3gpp_get_location_area_code (location_3gpp),
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
mm_iface_modem_location_3gpp_update_lac_ci (MMIfaceModemLocation *self,
                                            gulong location_area_code,
                                            gulong cell_id)
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
        changed += mm_location_3gpp_set_location_area_code (ctx->location_3gpp,
                                                            location_area_code);
        changed += mm_location_3gpp_set_cell_id (ctx->location_3gpp,
                                                 cell_id);
        if (changed)
            notify_3gpp_location_update (self, skeleton, ctx->location_3gpp);
    }

    g_object_unref (skeleton);
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
        guint changed = 0;

        g_assert (ctx->location_3gpp != NULL);
        changed += mm_location_3gpp_set_location_area_code (ctx->location_3gpp, 0);
        changed += mm_location_3gpp_set_cell_id (ctx->location_3gpp, 0);
        changed += mm_location_3gpp_set_mobile_country_code (ctx->location_3gpp, 0);
        changed += mm_location_3gpp_set_mobile_network_code (ctx->location_3gpp, 0);
        if (changed)
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
    const gchar *dbus_path;

    dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
    mm_info ("Modem %s: CDMA BS location updated "
             "(Longitude: '%lf', Latitude: '%lf')",
             dbus_path,
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
    default:
        break;
    }

    mm_gdbus_modem_location_set_enabled (skeleton, mask);

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    MMIfaceModemLocation *self;
    MmGdbusModemLocation *skeleton;
    GSimpleAsyncResult *result;
    MMModemLocationSource to_enable;
    MMModemLocationSource to_disable;
    MMModemLocationSource current;
} SetupGatheringContext;

static void setup_gathering_step (SetupGatheringContext *ctx);

static void
setup_gathering_context_complete_and_free (SetupGatheringContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
setup_gathering_finish (MMIfaceModemLocation *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
enable_location_gathering_ready (MMIfaceModemLocation *self,
                                 GAsyncResult *res,
                                 SetupGatheringContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering_finish (self, res, &error)) {
        gchar *str;

        update_location_source_status (ctx->self, ctx->current, FALSE);

        str = mm_modem_location_source_build_string_from_mask (ctx->current);
        g_prefix_error (&error,
                        "Couldn't enable location '%s' gathering: ",
                        str);
        g_simple_async_result_take_error (ctx->result, error);
        setup_gathering_context_complete_and_free (ctx);
        g_free (str);
        return;
    }

    /* Keep on with next ones... */
    ctx->current = ctx->current << 1;
    setup_gathering_step (ctx);
}

static void
disable_location_gathering_ready (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  SetupGatheringContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering_finish (self, res, &error)) {
        gchar *str;

        /* Back to enabled then */
        update_location_source_status (ctx->self, ctx->current, TRUE);

        str = mm_modem_location_source_build_string_from_mask (ctx->current);
        g_prefix_error (&error,
                        "Couldn't disable location '%s' gathering: ",
                        str);
        g_simple_async_result_take_error (ctx->result, error);
        setup_gathering_context_complete_and_free (ctx);
        g_free (str);
        return;
    }

    /* Keep on with next ones... */
    ctx->current = ctx->current << 1;
    setup_gathering_step (ctx);
}

static void
setup_gathering_step (SetupGatheringContext *ctx)
{
    /* Are we done? */
    if (ctx->to_enable == MM_MODEM_LOCATION_SOURCE_NONE &&
        ctx->to_disable == MM_MODEM_LOCATION_SOURCE_NONE) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        setup_gathering_context_complete_and_free (ctx);
        return;
    }

    while (ctx->current <= MM_MODEM_LOCATION_SOURCE_CDMA_BS) {
        gchar *source_str;

        if (ctx->to_enable & ctx->current) {
            /* Remove from mask */
            ctx->to_enable &= ~ctx->current;

            /* We update the location source status before launching the
             * specific actions to enable the gathering, so that we are
             * able to get location updates while the gathering gets
             * enabled. */
            update_location_source_status (ctx->self, ctx->current, TRUE);

            /* Plugins can run custom actions to enable location gathering */
            if (MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->enable_location_gathering &&
                MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->enable_location_gathering_finish) {
                MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->enable_location_gathering (
                    MM_IFACE_MODEM_LOCATION (ctx->self),
                    ctx->current,
                    (GAsyncReadyCallback)enable_location_gathering_ready,
                    ctx);
                return;
            }

            source_str = mm_modem_location_source_build_string_from_mask (ctx->current);
            mm_dbg ("Enabled location '%s' gathering...", source_str);
            g_free (source_str);
        } else if (ctx->to_disable & ctx->current) {
            /* Remove from mask */
            ctx->to_disable &= ~ctx->current;

            update_location_source_status (ctx->self, ctx->current, FALSE);

            /* Plugins can run custom actions to disable location gathering */
            if (MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->disable_location_gathering &&
                MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->disable_location_gathering_finish) {
                MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->disable_location_gathering (
                    MM_IFACE_MODEM_LOCATION (ctx->self),
                    ctx->current,
                    (GAsyncReadyCallback)disable_location_gathering_ready,
                    ctx);
                return;
            }

            source_str = mm_modem_location_source_build_string_from_mask (ctx->current);
            mm_dbg ("Disabled location '%s' gathering...", source_str);
            g_free (source_str);
        }

        /* go on... */
        ctx->current = ctx->current << 1;
    }

    /* We just need to finish now */
    g_assert (ctx->to_enable == MM_MODEM_LOCATION_SOURCE_NONE);
    g_assert (ctx->to_disable == MM_MODEM_LOCATION_SOURCE_NONE);
    setup_gathering_step (ctx);
}

static void
setup_gathering (MMIfaceModemLocation *self,
                 MMModemLocationSource mask,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    SetupGatheringContext *ctx;
    MMModemLocationSource currently_enabled;
    MMModemLocationSource source;
    gchar *str;

    ctx = g_new (SetupGatheringContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             setup_gathering);
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        setup_gathering_context_complete_and_free (ctx);
        return;
    }

    /* Get current list of enabled sources */
    currently_enabled = mm_gdbus_modem_location_get_enabled (ctx->skeleton);

    /* Reset the list of sources to enable or disable */
    ctx->to_enable = MM_MODEM_LOCATION_SOURCE_NONE;
    ctx->to_disable = MM_MODEM_LOCATION_SOURCE_NONE;

    /* Loop through all known bits in the bitmask to enable/disable specific location sources */
    for (source = MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI;
         source <= MM_MODEM_LOCATION_SOURCE_CDMA_BS;
         source = source << 1) {
        /* skip unsupported sources */
        if (!(mm_gdbus_modem_location_get_capabilities (ctx->skeleton) & source))
            continue;

        str = mm_modem_location_source_build_string_from_mask (source);

        if (mask & source) {
            /* Source set in mask, need to enable if disabled */
            if (currently_enabled & source)
                mm_dbg ("Location '%s' gathering is already enabled...", str);
            else
                ctx->to_enable |= source;
        } else {
            /* Source unset in mask, need to disable if enabled */
            if (currently_enabled & source)
                ctx->to_disable |= source;
            else
                mm_dbg ("Location '%s' gathering is already disabled...", str);
        }

        g_free (str);
    }

    if (ctx->to_enable != MM_MODEM_LOCATION_SOURCE_NONE) {
        str = mm_modem_location_source_build_string_from_mask (ctx->to_enable);
        mm_dbg ("Need to enable the following location sources: '%s'", str);
        g_free (str);
    }

    if (ctx->to_disable != MM_MODEM_LOCATION_SOURCE_NONE) {
        str = mm_modem_location_source_build_string_from_mask (ctx->to_disable);
        mm_dbg ("Need to disable the following location sources: '%s'", str);
        g_free (str);
    }

    /* Start enabling/disabling location sources */
    ctx->current = MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI;
    setup_gathering_step (ctx);
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
        mm_dbg ("%s location signaling",
                ctx->signal_location ? "Enabling" : "Disabling");
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
    mm_dbg ("Setting up location sources: '%s'", str);
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
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_GATHERING,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModemLocation *self;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemLocation *skeleton;
};

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_location_disable_finish (MMIfaceModemLocation *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disabling_location_gathering_ready (MMIfaceModemLocation *self,
                                    GAsyncResult *res,
                                    DisablingContext *ctx)
{
    GError *error = NULL;

    if (!setup_gathering_finish (self, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        disabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_GATHERING:
        setup_gathering (ctx->self,
                         MM_MODEM_LOCATION_SOURCE_NONE,
                         (GAsyncReadyCallback)disabling_location_gathering_ready,
                         ctx);
        return;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        clear_location_context (ctx->self);
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_location_disable (MMIfaceModemLocation *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_location_disable);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        disabling_context_complete_and_free (ctx);
        return;
    }

    interface_disabling_step (ctx);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_ENABLE_GATHERING,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModemLocation *self;
    EnablingStep step;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MmGdbusModemLocation *skeleton;
};

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
enabling_context_complete_and_free_if_cancelled (EnablingContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface enabling cancelled");
    enabling_context_complete_and_free (ctx);
    return TRUE;
}

gboolean
mm_iface_modem_location_enable_finish (MMIfaceModemLocation *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
enabling_location_gathering_ready (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   EnablingContext *ctx)
{
    GError *error = NULL;

    if (!setup_gathering_finish (self, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
interface_enabling_step (EnablingContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (enabling_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_GATHERING: {
        MMModemLocationSource default_sources;

        /* By default, we'll enable all NON-GPS sources */
        default_sources = mm_gdbus_modem_location_get_capabilities (ctx->skeleton);
        default_sources &= ~(MM_MODEM_LOCATION_SOURCE_GPS_RAW | MM_MODEM_LOCATION_SOURCE_GPS_NMEA);

        setup_gathering (ctx->self,
                         default_sources,
                         (GAsyncReadyCallback)enabling_location_gathering_ready,
                         ctx);
        return;
    }

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        enabling_context_complete_and_free (ctx);
        return;
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

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_location_enable);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        enabling_context_complete_and_free (ctx);
        return;
    }

    interface_enabling_step (ctx);
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CAPABILITIES,
    INITIALIZATION_STEP_VALIDATE_CAPABILITIES,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModemLocation *self;
    MmGdbusModemLocation *skeleton;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    InitializationStep step;
    MMModemLocationSource capabilities;
};

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
initialization_context_complete_and_free_if_cancelled (InitializationContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface initialization cancelled");
    initialization_context_complete_and_free (ctx);
    return TRUE;
}

static void
load_capabilities_ready (MMIfaceModemLocation *self,
                         GAsyncResult *res,
                         InitializationContext *ctx)
{
    GError *error = NULL;

    ctx->capabilities = MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_capabilities_finish (self, res, &error);
    if (error) {
        mm_warn ("couldn't load location capabilities: '%s'", error->message);
        g_error_free (error);
    }

    mm_gdbus_modem_location_set_capabilities (ctx->skeleton, ctx->capabilities);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization_step (InitializationContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (initialization_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CAPABILITIES:
        /* Location capabilities value is meant to be loaded only once during
         * the whole lifetime of the modem. Therefore, if we already have it
         * loaded, don't try to load it again. */
        if (!mm_gdbus_modem_location_get_capabilities (ctx->skeleton) &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->load_capabilities &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->load_capabilities_finish) {
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->load_capabilities (
                ctx->self,
                (GAsyncReadyCallback)load_capabilities_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_VALIDATE_CAPABILITIES:
        /* If the modem doesn't support any location capabilities, we won't export
         * the interface. We just report an UNSUPPORTED error. */
        if (ctx->capabilities == MM_MODEM_LOCATION_SOURCE_NONE) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "The modem doesn't have location capabilities");
            initialization_context_complete_and_free (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-setup",
                          G_CALLBACK (handle_setup),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-get-location",
                          G_CALLBACK (handle_get_location),
                          ctx->self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_location (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                     MM_GDBUS_MODEM_LOCATION (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_location_initialize_finish (MMIfaceModemLocation *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_location_initialize (MMIfaceModemLocation *self,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemLocation *skeleton = NULL;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_location_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem_location_set_capabilities (skeleton, MM_MODEM_LOCATION_SOURCE_NONE);
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
    ctx->self = g_object_ref (self);
    ctx->capabilities = MM_MODEM_LOCATION_SOURCE_NONE;
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_location_initialize);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    interface_initialization_step (ctx);
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
