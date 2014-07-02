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
 * Copyright (C) 2014 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@aleksander.es>
 */

#include "mm-common-cinterion.h"
#include "mm-base-modem-at.h"

static MMIfaceModemLocation *iface_modem_location_parent;

/*****************************************************************************/

#define CINTERION_LOCATION_CONTEXT_TAG "cinterion-location-tag"
static GQuark cinterion_location_context_quark;

/*****************************************************************************/

typedef struct {
    MMModemLocationSource enabled_sources;
} LocationContext;

static void
location_context_free (LocationContext *ctx)
{
    g_slice_free (LocationContext, ctx);
}

static LocationContext *
get_location_context (MMBaseModem *self)
{
    LocationContext *ctx;

    if (G_UNLIKELY (!cinterion_location_context_quark))
        cinterion_location_context_quark =  (g_quark_from_static_string (
                                                 CINTERION_LOCATION_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), cinterion_location_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_slice_new (LocationContext);
        ctx->enabled_sources = MM_MODEM_LOCATION_SOURCE_NONE;

        g_object_set_qdata_full (
            G_OBJECT (self),
            cinterion_location_context_quark,
            ctx,
            (GDestroyNotify)location_context_free);
    }

    return ctx;
}


/*****************************************************************************/
/* Location capabilities loading (Location interface) */

MMModemLocationSource
mm_common_cinterion_location_load_capabilities_finish (MMIfaceModemLocation *self,
                                                       GAsyncResult *res,
                                                       GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_LOCATION_SOURCE_NONE;

    return (MMModemLocationSource) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (
                                                         G_SIMPLE_ASYNC_RESULT (res)));
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    MMModemLocationSource sources;
    GError *error = NULL;

    sources = iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Now our own check. */
    if (mm_base_modem_peek_port_gps (MM_BASE_MODEM (self)))
        sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                    MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                    MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED);

    /* So we're done, complete */
    g_simple_async_result_set_op_res_gpointer (simple,
                                               GUINT_TO_POINTER (sources),
                                               NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_common_cinterion_location_load_capabilities (MMIfaceModemLocation *self,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_common_cinterion_location_load_capabilities);

    /* Chain up parent's setup */
    iface_modem_location_parent->load_capabilities (
        self,
        (GAsyncReadyCallback)parent_load_capabilities_ready,
        result);
}

/*****************************************************************************/
/* Enable/Disable location gathering (Location interface) */

typedef struct {
    MMBaseModem *self;
    GSimpleAsyncResult *result;
    MMModemLocationSource source;
} LocationGatheringContext;

static void
location_gathering_context_complete_and_free (LocationGatheringContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_slice_free (LocationGatheringContext, ctx);
}

/******************************/
/* Disable location gathering */

gboolean
mm_common_cinterion_disable_location_gathering_finish (MMIfaceModemLocation *self,
                                                       GAsyncResult *res,
                                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
gps_disabled_ready (MMBaseModem *self,
                    GAsyncResult *res,
                    LocationGatheringContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (self, res, &error))
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    /* Only use the GPS port in NMEA/RAW setups */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        MMPortSerialGps *gps_port;

        /* Even if we get an error here, we try to close the GPS port */
        gps_port = mm_base_modem_peek_port_gps (self);
        if (gps_port)
            mm_port_serial_close (MM_PORT_SERIAL (gps_port));
    }

    location_gathering_context_complete_and_free (ctx);
}

static void
internal_disable_location_gathering (LocationGatheringContext *ctx)
{
    LocationContext *location_ctx;
    gboolean stop_gps = FALSE;

    location_ctx = get_location_context (MM_BASE_MODEM (ctx->self));

    /* Only stop GPS engine if no GPS-related sources enabled */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                       MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        location_ctx->enabled_sources &= ~ctx->source;

        if (!(location_ctx->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                               MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                               MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)))
            stop_gps = TRUE;
    }

    if (stop_gps) {
        /* We disable continuous GPS fixes */
        mm_base_modem_at_command_full (MM_BASE_MODEM (ctx->self),
                                       mm_base_modem_peek_best_at_port (MM_BASE_MODEM (ctx->self), NULL),
                                       "AT^SGPSS=0",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)gps_disabled_ready,
                                       ctx);
        return;
    }

    /* For any other location (e.g. 3GPP), or if still some GPS needed, just return */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    location_gathering_context_complete_and_free (ctx);
}

static void
parent_disable_location_gathering_ready (MMIfaceModemLocation *self,
                                         GAsyncResult *res,
                                         LocationGatheringContext *ctx)
{
    GError *error = NULL;

    if (!iface_modem_location_parent->disable_location_gathering_finish (self, res, &error)) {
        if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                           MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                           MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
            /* Ignore errors when disabling GPS, we can try with AT commands */
            g_error_free (error);
        } else {
            /* Fatal */
            g_simple_async_result_take_error (ctx->result, error);
            location_gathering_context_complete_and_free (ctx);
            return;
        }
    }

    internal_disable_location_gathering (ctx);
}

void
mm_common_cinterion_disable_location_gathering (MMIfaceModemLocation *self,
                                                MMModemLocationSource source,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data)
{
    LocationGatheringContext *ctx;

    ctx = g_slice_new (LocationGatheringContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_common_cinterion_disable_location_gathering);
    ctx->source = source;

    /* Chain up parent's gathering enable */
    if (iface_modem_location_parent->disable_location_gathering) {
        iface_modem_location_parent->disable_location_gathering (
            self,
            source,
            (GAsyncReadyCallback)parent_disable_location_gathering_ready,
            ctx);
        return;
    }

    internal_disable_location_gathering (ctx);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

gboolean
mm_common_cinterion_enable_location_gathering_finish (MMIfaceModemLocation *self,
                                                      GAsyncResult *res,
                                                      GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
gps_enabled_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   LocationGatheringContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (self, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        location_gathering_context_complete_and_free (ctx);
        return;
    }

    /* Only use the GPS port in NMEA/RAW setups */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        MMPortSerialGps *gps_port;

        gps_port = mm_base_modem_peek_port_gps (self);
        if (!gps_port ||
            !mm_port_serial_open (MM_PORT_SERIAL (gps_port), &error)) {
            if (error)
                g_simple_async_result_take_error (ctx->result, error);
            else
                g_simple_async_result_set_error (ctx->result,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_FAILED,
                                                 "Couldn't open raw GPS serial port");
        } else
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    } else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    location_gathering_context_complete_and_free (ctx);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *self,
                                        GAsyncResult *res,
                                        LocationGatheringContext *ctx)
{
    gboolean start_gps = FALSE;
    GError *error = NULL;
    LocationContext *location_ctx;

    if (!iface_modem_location_parent->enable_location_gathering_finish (self, res, &error)) {
        if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                           MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                           MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
            /* Ignore errors when enabling GPS, we can try with AT commands */
            g_error_free (error);
        } else {
            /* Fatal */
            g_simple_async_result_take_error (ctx->result, error);
            location_gathering_context_complete_and_free (ctx);
            return;
        }
    }

    /* Now our own enabling */

    location_ctx = get_location_context (MM_BASE_MODEM (self));

    /* NMEA and RAW are both enabled in the same way */
    if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                       MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                       MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        /* Only start GPS engine if not done already */
        if (!(location_ctx->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                               MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                                               MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)))
            start_gps = TRUE;
        location_ctx->enabled_sources |= ctx->source;
    }

    if (start_gps) {
        /* We enable continuous GPS fixes */
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       mm_base_modem_peek_best_at_port (MM_BASE_MODEM (self), NULL),
                                       "AT^SGPSS=4",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)gps_enabled_ready,
                                       ctx);
        return;
    }

    /* For any other location (e.g. 3GPP), or if GPS already running just return */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    location_gathering_context_complete_and_free (ctx);
}

void
mm_common_cinterion_enable_location_gathering (MMIfaceModemLocation *self,
                                               MMModemLocationSource source,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
    LocationGatheringContext *ctx;

    ctx = g_slice_new (LocationGatheringContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_common_cinterion_enable_location_gathering);
    ctx->source = source;

    /* Chain up parent's gathering enable */
    iface_modem_location_parent->enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        ctx);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
trace_received (MMPortSerialGps *port,
                const gchar *trace,
                MMIfaceModemLocation *self)
{
    /* Helper to debug GPS location related issues. Don't depend on a real GPS
     * fix for debugging, just use some random values to update */
#if 0
    if (g_str_has_prefix (trace, "$GPGGA")) {
        GString *str;
        GDateTime *now;

        now = g_date_time_new_now_utc ();
        str = g_string_new ("");
        g_string_append_printf (str,
                                "$GPGGA,%02u%02u%02u,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
                                g_date_time_get_hour (now),
                                g_date_time_get_minute (now),
                                g_date_time_get_second (now));
        mm_iface_modem_location_gps_update (self, str->str);
        g_string_free (str, TRUE);
        g_date_time_unref (now);
        return;
    }
#endif

    mm_iface_modem_location_gps_update (self, trace);
}

void
mm_common_cinterion_setup_gps_port (MMBroadbandModem *self)
{
    MMPortSerialGps *gps_data_port;

    gps_data_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
    if (gps_data_port) {
        /* It may happen that the modem was started with GPS already enabled, or
         * maybe ModemManager got rebooted and it was left enabled before. We'll make
         * sure that it is disabled when we initialize the modem */
        mm_base_modem_at_command_full (MM_BASE_MODEM (self),
                                       mm_base_modem_peek_best_at_port (MM_BASE_MODEM (self), NULL),
                                       "AT^SGPSS=0",
                                       3, FALSE, FALSE, NULL, NULL, NULL);

        /* Add handler for the NMEA traces */
        mm_port_serial_gps_add_trace_handler (gps_data_port,
                                              (MMPortSerialGpsTraceFn)trace_received,
                                              self,
                                              NULL);
    }
}

/*****************************************************************************/

void
mm_common_cinterion_peek_parent_location_interface (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);
}
