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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-base-modem.h"
#include "mm-base-modem-at.h"
#include "mm-shared-simtech.h"

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "shared-simtech-private-tag"
static GQuark private_quark;

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED,
} FeatureSupport;

typedef struct {
    /* location */
    MMIfaceModemLocation  *iface_modem_location_parent;
    MMModemLocationSource  supported_sources;
    MMModemLocationSource  enabled_sources;
    FeatureSupport         cgps_support;
} Private;

static Private *
get_private (MMSharedSimtech *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark =  (g_quark_from_static_string (PRIVATE_TAG));

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_new0 (Private, 1);
        priv->supported_sources = MM_MODEM_LOCATION_SOURCE_NONE;
        priv->enabled_sources = MM_MODEM_LOCATION_SOURCE_NONE;
        priv->cgps_support = FEATURE_SUPPORT_UNKNOWN;

        /* Setup parent class' MMIfaceModemLocation */
        g_assert (MM_SHARED_SIMTECH_GET_INTERFACE (self)->peek_parent_location_interface);
        priv->iface_modem_location_parent = MM_SHARED_SIMTECH_GET_INTERFACE (self)->peek_parent_location_interface (self);

        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, g_free);
    }

    return priv;
}

/*****************************************************************************/
/* GPS trace received */

static void
trace_received (MMPortSerialGps      *port,
                const gchar          *trace,
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

MMModemLocationSource
mm_shared_simtech_location_load_capabilities_finish (MMIfaceModemLocation  *self,
                                                     GAsyncResult          *res,
                                                     GError               **error)
{
    GError *inner_error = NULL;
    gssize aux;

    aux = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_LOCATION_SOURCE_NONE;
    }
    return (MMModemLocationSource) aux;
}

static void probe_gps_features (GTask *task);

static void
cgps_test_ready (MMBaseModem  *self,
                 GAsyncResult *res,
                 GTask        *task)
{
    Private *priv;

    priv = get_private (MM_SHARED_SIMTECH (self));

    if (!mm_base_modem_at_command_finish (self, res, NULL))
        priv->cgps_support = FEATURE_NOT_SUPPORTED;
    else
        priv->cgps_support = FEATURE_SUPPORTED;

    probe_gps_features (task);
}

static void
probe_gps_features (GTask *task)
{
    MMSharedSimtech       *self;
    MMModemLocationSource  sources;
    Private               *priv;

    self = MM_SHARED_SIMTECH (g_task_get_source_object (task));
    priv = get_private (self);

    /* Need to check if CGPS supported... */
    if (priv->cgps_support == FEATURE_SUPPORT_UNKNOWN) {
        mm_base_modem_at_command (MM_BASE_MODEM (self), "+CGPS=?", 3, TRUE, (GAsyncReadyCallback) cgps_test_ready, task);
        return;
    }

    /* All GPS features probed */

    /* Recover parent sources */
    sources = GPOINTER_TO_UINT (g_task_get_task_data (task));

    if (priv->cgps_support == FEATURE_SUPPORTED) {
        mm_dbg ("GPS commands supported: GPS capabilities enabled");

        /* We only flag as supported by this implementation those sources not already
         * supported by the parent implementation */
        if (!(sources & MM_MODEM_LOCATION_SOURCE_GPS_NMEA))
            priv->supported_sources |= MM_MODEM_LOCATION_SOURCE_GPS_NMEA;
        if (!(sources & MM_MODEM_LOCATION_SOURCE_GPS_RAW))
            priv->supported_sources |= MM_MODEM_LOCATION_SOURCE_GPS_RAW;
        if (!(sources & MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED))
            priv->supported_sources |= MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED;

        sources |= priv->supported_sources;

        /* Add handler for the NMEA traces in the GPS data port */
        mm_port_serial_gps_add_trace_handler (mm_base_modem_peek_port_gps (MM_BASE_MODEM (self)),
                                              (MMPortSerialGpsTraceFn)trace_received,
                                              self,
                                              NULL);
    } else
        mm_dbg ("No GPS command supported: no GPS capabilities");

    g_task_return_int (task, (gssize) sources);
    g_object_unref (task);
}

static void
parent_load_capabilities_ready (MMIfaceModemLocation *self,
                                GAsyncResult         *res,
                                GTask                *task)
{
    MMModemLocationSource  sources;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_SIMTECH (self));

    sources = priv->iface_modem_location_parent->load_capabilities_finish (self, res, &error);
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Now our own check. If we don't have any GPS port, we're done */
    if (!mm_base_modem_peek_port_gps (MM_BASE_MODEM (self))) {
        mm_dbg ("No GPS data port found: no GPS capabilities");
        g_task_return_int (task, sources);
        g_object_unref (task);
        return;
    }

    /* Cache sources supported by the parent */
    g_task_set_task_data (task, GUINT_TO_POINTER (sources), NULL);

    /* Probe all GPS features */
    probe_gps_features (task);
}

void
mm_shared_simtech_location_load_capabilities (MMIfaceModemLocation *self,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data)
{
    Private *priv;
    GTask   *task;

    priv = get_private (MM_SHARED_SIMTECH (self));
    task = g_task_new (self, NULL, callback, user_data);

    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->load_capabilities);
    g_assert (priv->iface_modem_location_parent->load_capabilities_finish);

    priv->iface_modem_location_parent->load_capabilities (self,
                                                          (GAsyncReadyCallback)parent_load_capabilities_ready,
                                                          task);
}

/*****************************************************************************/
/* Disable location gathering (Location interface) */

gboolean
mm_shared_simtech_disable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                     GAsyncResult          *res,
                                                     GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_cgps_ready (MMBaseModem  *self,
                    GAsyncResult *res,
                    GTask        *task)
{
    MMModemLocationSource  source;
    Private               *priv;
    GError                *error = NULL;

    priv = get_private (MM_SHARED_SIMTECH (self));

    mm_base_modem_at_command_finish (self, res, &error);

    /* Only use the GPS port in NMEA/RAW setups */
    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    if (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA | MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        MMPortSerialGps *gps_port;

        /* Even if we get an error here, we try to close the GPS port */
        gps_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
        if (gps_port)
            mm_port_serial_close (MM_PORT_SERIAL (gps_port));
    }

    if (error)
        g_task_return_error (task, error);
    else {
        priv->enabled_sources &= source;
        g_task_return_boolean (task, TRUE);
    }
    g_object_unref (task);
}

static void
parent_disable_location_gathering_ready (MMIfaceModemLocation *self,
                                         GAsyncResult         *res,
                                         GTask                *task)
{
    GError  *error;
    Private *priv;

    priv = get_private (MM_SHARED_SIMTECH (self));

    g_assert (priv->iface_modem_location_parent);
    if (!priv->iface_modem_location_parent->disable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_simtech_disable_location_gathering (MMIfaceModemLocation  *self,
                                              MMModemLocationSource  source,
                                              GAsyncReadyCallback    callback,
                                              gpointer               user_data)
{
    MMModemLocationSource  enabled_sources;
    Private               *priv;
    GTask                 *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    priv = get_private (MM_SHARED_SIMTECH (self));
    g_assert (priv->iface_modem_location_parent);

    /* Only consider request if it applies to one of the sources we are
     * supporting, otherwise run parent disable */
    if (!(priv->supported_sources & source)) {
        /* If disabling implemented by the parent, run it. */
        if (priv->iface_modem_location_parent->disable_location_gathering &&
            priv->iface_modem_location_parent->disable_location_gathering_finish) {
            priv->iface_modem_location_parent->disable_location_gathering (self,
                                                                           source,
                                                                           (GAsyncReadyCallback)parent_disable_location_gathering_ready,
                                                                           task);
            return;
        }
        /* Otherwise, we're done */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* We only expect GPS sources here */
    g_assert (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                        MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                        MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED));

    /* Flag as disabled to see how many others we would have left enabled */
    enabled_sources = priv->enabled_sources;
    enabled_sources &= ~source;

    /* If there are still GPS-related sources enabled, do nothing else */
    if (enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                           MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                           MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        priv->enabled_sources &= ~source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Stop GPS engine if all GPS-related sources are disabled */
    g_assert (priv->cgps_support == FEATURE_SUPPORTED);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CGPS=0",
                              10,
                              FALSE,
                              (GAsyncReadyCallback) disable_cgps_ready,
                              task);
}

/*****************************************************************************/
/* Enable location gathering (Location interface) */

gboolean
mm_shared_simtech_enable_location_gathering_finish (MMIfaceModemLocation  *self,
                                                    GAsyncResult          *res,
                                                    GError               **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enable_cgps_ready (MMBaseModem  *self,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMModemLocationSource  source;
    GError                *error = NULL;
    Private               *priv;

    priv = get_private (MM_SHARED_SIMTECH (self));

    if (!mm_base_modem_at_command_finish (self, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Only use the GPS port in NMEA/RAW setups */
    source = (MMModemLocationSource) GPOINTER_TO_UINT (g_task_get_task_data (task));
    if (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                  MM_MODEM_LOCATION_SOURCE_GPS_RAW)) {
        MMPortSerialGps *gps_port;

        gps_port = mm_base_modem_peek_port_gps (MM_BASE_MODEM (self));
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

    priv->enabled_sources |= source;
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
parent_enable_location_gathering_ready (MMIfaceModemLocation *self,
                                        GAsyncResult         *res,
                                        GTask                *task)
{
    GError  *error;
    Private *priv;

    priv = get_private (MM_SHARED_SIMTECH (self));

    g_assert (priv->iface_modem_location_parent);
    if (!priv->iface_modem_location_parent->enable_location_gathering_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_shared_simtech_enable_location_gathering (MMIfaceModemLocation  *self,
                                             MMModemLocationSource  source,
                                             GAsyncReadyCallback    callback,
                                             gpointer               user_data)
{
    Private *priv;
    GTask   *task;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, GUINT_TO_POINTER (source), NULL);

    priv = get_private (MM_SHARED_SIMTECH (self));
    g_assert (priv->iface_modem_location_parent);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering);
    g_assert (priv->iface_modem_location_parent->enable_location_gathering_finish);

    /* Only consider request if it applies to one of the sources we are
     * supporting, otherwise run parent enable */
    if (!(priv->supported_sources & source)) {
        priv->iface_modem_location_parent->enable_location_gathering (self,
                                                                      source,
                                                                      (GAsyncReadyCallback)parent_enable_location_gathering_ready,
                                                                      task);
        return;
    }

    /* We only expect GPS sources here */
    g_assert (source & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                        MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                        MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED));

    /* If GPS already started, store new flag and we're done */
    if (priv->enabled_sources & (MM_MODEM_LOCATION_SOURCE_GPS_NMEA |
                                 MM_MODEM_LOCATION_SOURCE_GPS_RAW |
                                 MM_MODEM_LOCATION_SOURCE_GPS_UNMANAGED)) {
        priv->enabled_sources |= source;
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    g_assert (priv->cgps_support == FEATURE_SUPPORTED);
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "+CGPS=1,1",
                              10,
                              FALSE,
                              (GAsyncReadyCallback) enable_cgps_ready,
                              task);
}

/*****************************************************************************/

static void
shared_simtech_init (gpointer g_iface)
{
}

GType
mm_shared_simtech_get_type (void)
{
    static GType shared_simtech_type = 0;

    if (!G_UNLIKELY (shared_simtech_type)) {
        static const GTypeInfo info = {
            sizeof (MMSharedSimtech),  /* class_size */
            shared_simtech_init,       /* base_init */
            NULL,                  /* base_finalize */
        };

        shared_simtech_type = g_type_register_static (G_TYPE_INTERFACE, "MMSharedSimtech", &info, 0);
        g_type_interface_add_prerequisite (shared_simtech_type, MM_TYPE_IFACE_MODEM_LOCATION);
    }

    return shared_simtech_type;
}
