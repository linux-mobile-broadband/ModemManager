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
#include "mm-log.h"

static MMIfaceModemLocation *iface_modem_location_parent;

/*****************************************************************************/

#define CINTERION_LOCATION_CONTEXT_TAG "cinterion-location-tag"
static GQuark cinterion_location_context_quark;

/*****************************************************************************/

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED,
} FeatureSupport;

typedef struct {
    MMModemLocationSource enabled_sources;
    FeatureSupport        sgpss_support;
    FeatureSupport        sgpsc_support;
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
        ctx->sgpss_support = FEATURE_SUPPORT_UNKNOWN;
        ctx->sgpsc_support = FEATURE_SUPPORT_UNKNOWN;

        g_object_set_qdata_full (
            G_OBJECT (self),
            cinterion_location_context_quark,
            ctx,
            (GDestroyNotify)location_context_free);
    }

    return ctx;
}

/*****************************************************************************/
/* GPS trace received */

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

/*****************************************************************************/
/* Location capabilities loading (Location interface) */

typedef struct {
    MMModemLocationSource sources;
} LoadCapabilitiesContext;

static void
load_capabilities_context_free (LoadCapabilitiesContext *ctx)
{
    g_slice_free (LoadCapabilitiesContext, ctx);
}

MMModemLocationSource
mm_common_cinterion_location_load_capabilities_finish (MMIfaceModemLocation  *self,
                                                       GAsyncResult          *res,
                                                       GError               **error)
{
    gssize aux;

    if ((aux = g_task_propagate_int (G_TASK (res), error)) < 0)
        return MM_MODEM_LOCATION_SOURCE_NONE;

    return (MMModemLocationSource) aux;
}

static void probe_gps_features (GTask *task);

static void
sgpsc_test_ready (MMBaseModem  *self,
                  GAsyncResult *res,
                  GTask        *task)
{
    LocationContext *location_ctx;

    location_ctx = get_location_context (self);
    if (!mm_base_modem_at_command_finish (self, res, NULL))
        location_ctx->sgpsc_support = FEATURE_NOT_SUPPORTED;
    else {
        /* ^SGPSC supported! */
        location_ctx->sgpsc_support = FEATURE_SUPPORTED;
        /* It may happen that the modem was started with GPS already enabled, or
         * maybe ModemManager got rebooted and it was left enabled before. We'll
         * make sure that it is disabled when we initialize the modem. */
        mm_base_modem_at_command (MM_BASE_MODEM (self), "AT^SGPSC=\"Engine\",\"0\"",          3, FALSE, NULL, NULL);
        mm_base_modem_at_command (MM_BASE_MODEM (self), "AT^SGPSC=\"Power/Antenna\",\"off\"", 3, FALSE, NULL, NULL);
        mm_base_modem_at_command (MM_BASE_MODEM (self), "AT^SGPSC=\"NMEA/Output\",\"off\"",   3, FALSE, NULL, NULL);
    }

    probe_gps_features (task);
}

static void
sgpss_test_ready (MMBaseModem  *self,
                  GAsyncResult *res,
                  GTask        *task)
{
    LocationContext *location_ctx;

    location_ctx = get_location_context (self);
    if (!mm_base_modem_at_command_finish (self, res, NULL))
        location_ctx->sgpss_support = FEATURE_NOT_SUPPORTED;
    else {
        /* ^SGPSS supported! */
        location_ctx->sgpss_support = FEATURE_SUPPORTED;

        /* Flag ^SGPSC as unsupported, even if it may be supported, so that we
         * only use one set of commands to enable/disable GPS. */
        location_ctx->sgpsc_support = FEATURE_NOT_SUPPORTED;

        /* It may happen that the modem was started with GPS already enabled, or
         * maybe ModemManager got rebooted and it was left enabled before. We'll
         * make sure that it is disabled when we initialize the modem. */
        mm_base_modem_at_command (MM_BASE_MODEM (self), "AT^SGPSS=0", 3, FALSE, NULL, NULL);
    }

    probe_gps_features (task);
}

static void
probe_gps_features (GTask *task)
{
    LoadCapabilitiesContext *ctx;
    MMBaseModem             *self;
    LocationContext         *location_ctx;

    ctx = (LoadCapabilitiesContext *) g_task_get_task_data (task);
    self = MM_BASE_MODEM (g_task_get_source_object (task));
    location_ctx = get_location_context (self);

    /* Need to check if SGPSS supported... */
    if (location_ctx->sgpss_support == FEATURE_SUPPORT_UNKNOWN) {
        mm_base_modem_at_command (self, "AT^SGPSS=?", 3, TRUE, (GAsyncReadyCallback) sgpss_test_ready, task);
        return;
    }

    /* Need to check if SGPSC supported... */
    if (location_ctx->sgpsc_support == FEATURE_SUPPORT_UNKNOWN) {
        mm_base_modem_at_command (self, "AT^SGPSC=?", 3, TRUE, (GAsyncReadyCallback) sgpsc_test_ready, task);
        return;
    }

    /* All GPS features probed, check if GPS supported */
    if (location_ctx->sgpss_support == FEATURE_SUPPORTED || location_ctx->sgpsc_support == FEATURE_SUPPORTED) {
        mm_dbg ("GPS commands supported: GPS capabilities enabled");
        ctx->sources |= (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                         MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                         MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED);

        /* Add handler for the NMEA traces in the GPS data port */
        mm_port_serial_gps_add_trace_handler (mm_base_modem_peek_port_gps (MM_BASE_MODEM (self)),
                                              (MMPortSerialGpsTraceFn)trace_received,
                                              self,
                                              NULL);
    } else
        mm_dbg ("No GPS command supported: no GPS capabilities");

    g_task_return_int (task, (gssize) ctx->sources);
    g_object_unref (task);
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult         *res,
                                GTask                *task)
{
    LoadCapabilitiesContext *ctx;
    GError                  *error = NULL;

    ctx = (LoadCapabilitiesContext *) g_task_get_task_data (task);

    ctx->sources = iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own check. If we don't have any GPS port, we're done */
    if (!mm_base_modem_peek_port_gps (MM_BASE_MODEM (self))) {
        mm_dbg ("No GPS data port found: no GPS capabilities");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Probe all GPS features */
    probe_gps_features (task);
}

void
mm_common_cinterion_location_load_capabilities (MMIfaceModemLocation *self,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data)
{
    GTask                   *task;
    LoadCapabilitiesContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (LoadCapabilitiesContext);
    ctx->sources = MM_MODEM_LOCATION_SOURCE_NONE;
    g_task_set_task_data (task, ctx, (GDestroyNotify) load_capabilities_context_free);

    /* Chain up parent's setup */
    iface_modem_location_parent->load_capabilities (
        self,
        (GAsyncReadyCallback)parent_load_capabilities_ready,
        task);
}

/*****************************************************************************/
/* Disable location gathering (Location interface) */

typedef enum {
    DISABLE_LOCATION_GATHERING_GPS_STEP_FIRST,
    DISABLE_LOCATION_GATHERING_GPS_STEP_SGPSS,
    DISABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_ENGINE,
    DISABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_ANTENNA,
    DISABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_OUTPUT,
    DISABLE_LOCATION_GATHERING_GPS_STEP_LAST,
} DisableLocationGatheringGpsStep;

typedef struct {
    MMModemLocationSource            source;
    DisableLocationGatheringGpsStep  gps_step;
    GError                          *sgpss_error;
    GError                          *sgpsc_error;
} DisableLocationGatheringContext;

static void
disable_location_gathering_context_free (DisableLocationGatheringContext *ctx)
{
    if (ctx->sgpss_error)
        g_error_free (ctx->sgpss_error);
    if (ctx->sgpsc_error)
        g_error_free (ctx->sgpsc_error);
    g_slice_free (DisableLocationGatheringContext, ctx);
}

gboolean
mm_common_cinterion_disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                       GAsyncResult          *res,
                                                       GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void disable_location_gathering_context_gps_step (GTask *task);

static void
disable_sgpsc_ready (MMBaseModem  *self,
                     GAsyncResult *res,
                     GTask        *task)
{
    DisableLocationGatheringContext *ctx;
    GError                          *error = NULL;

    ctx = (DisableLocationGatheringContext *) g_task_get_task_data (task);

    /* Store error, if not one available already, and continue */
    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        if (!ctx->sgpsc_error)
            ctx->sgpsc_error = error;
        else
            g_error_free (error);
    }

    ctx->gps_step++;
    disable_location_gathering_context_gps_step (task);
}

static void
disable_sgpss_ready (MMBaseModem  *self,
                     GAsyncResult *res,
                     GTask        *task)
{
    DisableLocationGatheringContext *ctx;

    ctx = (DisableLocationGatheringContext *) g_task_get_task_data (task);

    /* Store error, if any, and continue */
    g_assert (!ctx->sgpss_error);
    mm_base_modem_at_command_finish (self, res, &ctx->sgpss_error);

    ctx->gps_step++;
    disable_location_gathering_context_gps_step (task);
}

static void
disable_location_gathering_context_gps_step (GTask *task)
{
    DisableLocationGatheringContext *ctx;
    MMBaseModem                     *self;
    LocationContext                 *location_ctx;

    self = MM_BASE_MODEM (g_task_get_source_object (task));
    ctx = (DisableLocationGatheringContext *) g_task_get_task_data (task);
    location_ctx = get_location_context (MM_BASE_MODEM (self));

    /* Only one of both supported */
    g_assert ((location_ctx->sgpss_support == FEATURE_SUPPORTED) || (location_ctx->sgpsc_support == FEATURE_SUPPORTED));
    g_assert (!((location_ctx->sgpss_support == FEATURE_SUPPORTED) && (location_ctx->sgpsc_support == FEATURE_SUPPORTED)));

    switch (ctx->gps_step) {
    case DISABLE_LOCATION_GATHERING_GPS_STEP_FIRST:
        ctx->gps_step++;
        /* Fall down to next step */

    case DISABLE_LOCATION_GATHERING_GPS_STEP_SGPSS:
        if (location_ctx->sgpss_support == FEATURE_SUPPORTED) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "AT^SGPSS=0",
                                      3, FALSE, (GAsyncReadyCallback) disable_sgpss_ready, task);
            return;
        }
        ctx->gps_step++;
        /* Fall down to next step */

    case DISABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_ENGINE:
        if (location_ctx->sgpsc_support == FEATURE_SUPPORTED) {
            /* Engine off */
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "AT^SGPSC=\"Engine\",\"0\"",
                                      3, FALSE, (GAsyncReadyCallback) disable_sgpsc_ready, task);
            return;
        }
        ctx->gps_step++;
        /* Fall down to next step */

    case DISABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_ANTENNA:
        if (location_ctx->sgpsc_support == FEATURE_SUPPORTED) {
            /* Antenna off */
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "AT^SGPSC=\"Power/Antenna\",\"off\"",
                                      3, FALSE, (GAsyncReadyCallback) disable_sgpsc_ready, task);
            return;
        }
        ctx->gps_step++;
        /* Fall down to next step */

    case DISABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_OUTPUT:
        if (location_ctx->sgpsc_support == FEATURE_SUPPORTED) {
            /* NMEA output off */
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "AT^SGPSC=\"NMEA/Output\",\"off\"",
                                      3, FALSE, (GAsyncReadyCallback) disable_sgpsc_ready, task);
            return;
        }
        ctx->gps_step++;
        /* Fall down to next step */

    case DISABLE_LOCATION_GATHERING_GPS_STEP_LAST:
        /* Only use the GPS port in NMEA/RAW setups */
        if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
            MMPortSerialGps *gps_port;

            /* Even if we get an error here, we try to close the GPS port */
            gps_port = mm_base_modem_peek_port_gps (self);
            if (gps_port)
                mm_port_serial_close (MM_PORT_SERIAL (gps_port));
        }

        if (ctx->sgpss_error) {
            g_task_return_error (task, ctx->sgpss_error);
            g_clear_error (&ctx->sgpss_error);
        } else if (ctx->sgpsc_error) {
            g_task_return_error (task, ctx->sgpsc_error);
            g_clear_error (&ctx->sgpsc_error);
        } else
            g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
}

static void
internal_disable_location_gathering (GTask *task)
{
    DisableLocationGatheringContext *ctx;
    LocationContext                 *location_ctx;
    gboolean                         stop_gps = FALSE;

    ctx = (DisableLocationGatheringContext *) g_task_get_task_data (task);

    location_ctx = get_location_context (MM_BASE_MODEM (g_task_get_source_object (task)));

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

    /* Run GPS stop sequence only if required to do so */
    if (stop_gps) {
        disable_location_gathering_context_gps_step (task);
        return;
    }

    /* For any other location (e.g. 3GPP), or if still some GPS needed, just return */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_disable_location_gathering_ready (MMIfaceModemLocation *self,
                                         GAsyncResult         *res,
                                         GTask                *task)
{
    DisableLocationGatheringContext *ctx;
    GError                          *error = NULL;

    ctx = (DisableLocationGatheringContext *) g_task_get_task_data (task);

    if (!iface_modem_location_parent->disable_location_gathering_finish (self, res, &error)) {
        /* Errors when disabling non-GPS sources are fatal */
        if (!(ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                             MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                             MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED))) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        /* Ignore errors when disabling GPS, we can try with AT commands */
        g_error_free (error);
    }

    internal_disable_location_gathering (task);
}

void
mm_common_cinterion_disable_location_gathering (MMIfaceModemLocation  *self,
                                                MMModemLocationSource  source,
                                                GAsyncReadyCallback    callback,
                                                gpointer               user_data)
{
    GTask                           *task;
    DisableLocationGatheringContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (DisableLocationGatheringContext);
    ctx->source = source;
    ctx->gps_step = DISABLE_LOCATION_GATHERING_GPS_STEP_FIRST;
    g_task_set_task_data (task, ctx, (GDestroyNotify) disable_location_gathering_context_free);

    /* Chain up parent's gathering enable */
    if (iface_modem_location_parent->disable_location_gathering) {
        iface_modem_location_parent->disable_location_gathering (
            self,
            source,
            (GAsyncReadyCallback)parent_disable_location_gathering_ready,
            task);
        return;
    }

    internal_disable_location_gathering (task);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

typedef enum {
    ENABLE_LOCATION_GATHERING_GPS_STEP_FIRST,
    ENABLE_LOCATION_GATHERING_GPS_STEP_SGPSS,
    ENABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_OUTPUT,
    ENABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_ANTENNA,
    ENABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_ENGINE,
    ENABLE_LOCATION_GATHERING_GPS_STEP_LAST,
} EnableLocationGatheringGpsStep;

typedef struct {
    MMModemLocationSource          source;
    EnableLocationGatheringGpsStep gps_step;
} EnableLocationGatheringContext;

static void
enable_location_gathering_context_free (EnableLocationGatheringContext *ctx)
{
    g_slice_free (EnableLocationGatheringContext, ctx);
}

gboolean
mm_common_cinterion_enable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                      GAsyncResult          *res,
                                                      GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void enable_location_gathering_context_gps_step (GTask *task);

static gboolean
enable_location_gathering_context_gps_step_next_cb (GTask *task)
{
    EnableLocationGatheringContext *ctx;

    ctx = (EnableLocationGatheringContext *) g_task_get_task_data (task);

    /* We jump to the next step */
    ctx->gps_step++;
    enable_location_gathering_context_gps_step (task);

    return G_SOURCE_REMOVE;
}

static void
enable_sgpsc_or_sgpss_ready (MMBaseModem  *self,
                             GAsyncResult *res,
                             GTask        *task)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Cinterion asks for 100ms between GPS commands... */
    g_timeout_add (100, (GSourceFunc) enable_location_gathering_context_gps_step_next_cb, task);
}

static void
enable_location_gathering_context_gps_step (GTask *task)
{
    EnableLocationGatheringContext *ctx;
    MMBaseModem                    *self;
    LocationContext                *location_ctx;

    self = MM_BASE_MODEM (g_task_get_source_object (task));
    ctx = (EnableLocationGatheringContext *) g_task_get_task_data (task);
    location_ctx = get_location_context (MM_BASE_MODEM (self));

    /* Only one of both supported */
    g_assert ((location_ctx->sgpss_support == FEATURE_SUPPORTED) || (location_ctx->sgpsc_support == FEATURE_SUPPORTED));
    g_assert (!((location_ctx->sgpss_support == FEATURE_SUPPORTED) && (location_ctx->sgpsc_support == FEATURE_SUPPORTED)));

    switch (ctx->gps_step) {
    case ENABLE_LOCATION_GATHERING_GPS_STEP_FIRST:
        ctx->gps_step++;
        /* Fall down to next step */

    case ENABLE_LOCATION_GATHERING_GPS_STEP_SGPSS:
        if (location_ctx->sgpss_support == FEATURE_SUPPORTED) {
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "AT^SGPSS=4",
                                      3, FALSE, (GAsyncReadyCallback) enable_sgpsc_or_sgpss_ready, task);
            return;
        }
        ctx->gps_step++;
        /* Fall down to next step */

    case ENABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_OUTPUT:
        if (location_ctx->sgpsc_support == FEATURE_SUPPORTED) {
            /* NMEA output off */
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "AT^SGPSC=\"NMEA/Output\",\"on\"",
                                      3, FALSE, (GAsyncReadyCallback) enable_sgpsc_or_sgpss_ready, task);
            return;
        }
        ctx->gps_step++;
        /* Fall down to next step */


    case ENABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_ANTENNA:
        if (location_ctx->sgpsc_support == FEATURE_SUPPORTED) {
            /* Antenna off */
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "AT^SGPSC=\"Power/Antenna\",\"on\"",
                                      3, FALSE, (GAsyncReadyCallback) enable_sgpsc_or_sgpss_ready, task);
            return;
        }
        ctx->gps_step++;
        /* Fall down to next step */

    case ENABLE_LOCATION_GATHERING_GPS_STEP_SGPSC_ENGINE:
        if (location_ctx->sgpsc_support == FEATURE_SUPPORTED) {
            /* Engine off */
            mm_base_modem_at_command (MM_BASE_MODEM (self),
                                      "AT^SGPSC=\"Engine\",\"1\"",
                                      3, FALSE, (GAsyncReadyCallback) enable_sgpsc_or_sgpss_ready, task);
            return;
        }
        ctx->gps_step++;
        /* Fall down to next step */

    case ENABLE_LOCATION_GATHERING_GPS_STEP_LAST:
        /* Only use the GPS port in NMEA/RAW setups */
        if (ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                           MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
            MMPortSerialGps *gps_port;
            GError          *error = NULL;

            gps_port = mm_base_modem_peek_port_gps (self);
            if (!gps_port || !mm_port_serial_open (MM_PORT_SERIAL (gps_port), &error)) {
                if (error)
                    g_task_return_error (task, error);
                else
                    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                             "Couldn't open raw GPS serial port");
                g_object_unref (task);
                return;
            }
        }

        /* Success */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }
}

static void
internal_enable_location_gathering (GTask *task)
{
    EnableLocationGatheringContext *ctx;
    LocationContext                *location_ctx;
    gboolean                        start_gps = FALSE;

    ctx = (EnableLocationGatheringContext *) g_task_get_task_data (task);

    location_ctx = get_location_context (MM_BASE_MODEM (g_task_get_source_object (task)));

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
        enable_location_gathering_context_gps_step (task);
        return;
    }

    /* For any other location (e.g. 3GPP), or if GPS already running just return */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *self,
                                        GAsyncResult         *res,
                                        GTask                *task)
{
    EnableLocationGatheringContext *ctx;
    GError                         *error = NULL;

    ctx = (EnableLocationGatheringContext *) g_task_get_task_data (task);

    if (!iface_modem_location_parent->enable_location_gathering_finish (self, res, &error)) {
        /* Errors when enabling non-GPS sources are fatal */
        if (!(ctx->source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                             MM_MODEM_LOCATION_SOURCE_GPS_RAW  |
                             MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED))) {
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        /* Ignore errors when enabling GPS, we can try with AT commands */
        g_error_free (error);
    }

    /* Now our own enabling */
    internal_enable_location_gathering (task);
}

void
mm_common_cinterion_enable_location_gathering (MMIfaceModemLocation *self,
                                               MMModemLocationSource source,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
    GTask                          *task;
    EnableLocationGatheringContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (EnableLocationGatheringContext);
    ctx->source   = source;
    ctx->gps_step = ENABLE_LOCATION_GATHERING_GPS_STEP_FIRST;
    g_task_set_task_data (task, ctx, (GDestroyNotify) enable_location_gathering_context_free);

    /* Chain up parent's gathering enable */
    iface_modem_location_parent->enable_location_gathering (
        self,
        source,
        (GAsyncReadyCallback)parent_enable_location_gathering_ready,
        task);
}

/*****************************************************************************/

void
mm_common_cinterion_peek_parent_location_interface (MMIfaceModemLocation *iface)
{
    iface_modem_location_parent = g_type_interface_peek_parent (iface);
}
