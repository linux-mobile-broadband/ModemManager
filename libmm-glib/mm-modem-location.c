/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2012 Lanedo GmbH <aleksander@lanedo.com>
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-modem-location.h"

/**
 * mm_modem_location_get_path:
 * @self: A #MMModemLocation.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_location_get_path (MMModemLocation *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_location_dup_path:
 * @self: A #MMModemLocation.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_location_dup_path (MMModemLocation *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

MMModemLocationSource
mm_modem_location_get_capabilities (MMModemLocation *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self),
                          MM_MODEM_LOCATION_SOURCE_NONE);

    return (MMModemLocationSource) mm_gdbus_modem_location_get_capabilities (self);
}

MMModemLocationSource
mm_modem_location_get_enabled (MMModemLocation *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return (MMModemLocationSource) mm_gdbus_modem_location_get_enabled (self);
}

gboolean
mm_modem_location_signals_location (MMModemLocation *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_get_signals_location (self);
}

gboolean
mm_modem_location_setup_finish (MMModemLocation *self,
                                GAsyncResult *res,
                                GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_setup_finish (self, res, error);
}

void
mm_modem_location_setup (MMModemLocation *self,
                         MMModemLocationSource sources,
                         gboolean signal_location,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_setup (self,
                                        sources,
                                        signal_location,
                                        cancellable,
                                        callback,
                                        user_data);
}

gboolean
mm_modem_location_setup_sync (MMModemLocation *self,
                              MMModemLocationSource sources,
                              gboolean signal_location,
                              GCancellable *cancellable,
                              GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_setup_sync (self,
                                                    sources,
                                                    signal_location,
                                                    cancellable,
                                                    error);
}

static gboolean
build_locations (GVariant *dictionary,
                 MMLocation3gpp **location_3gpp,
                 MMLocationGpsNmea **location_gps_nmea,
                 MMLocationGpsRaw **location_gps_raw,
                 GError **error)
{
    GError *inner_error = NULL;
    GVariant *value;
    guint source;
    GVariantIter iter;

    if (!dictionary)
        /* No location provided. Not actually an error. */
        return TRUE;

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error &&
           g_variant_iter_next (&iter, "{uv}", &source, &value)) {
        switch (source) {
        case MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI:
            if (location_3gpp)
                *location_3gpp = mm_location_3gpp_new_from_string_variant (value, &inner_error);
            break;
        case MM_MODEM_LOCATION_SOURCE_GPS_NMEA:
            if (location_gps_nmea)
                *location_gps_nmea = mm_location_gps_nmea_new_from_string_variant (value, &inner_error);
            break;
        case MM_MODEM_LOCATION_SOURCE_GPS_RAW:
            if (location_gps_raw)
                *location_gps_raw = mm_location_gps_raw_new_from_dictionary (value, &inner_error);
            break;
        default:
            g_warn_if_reached ();
            break;
        }

        g_variant_unref (value);
    }

    g_variant_unref (dictionary);

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_prefix_error (error,
                        "Couldn't build locations result: ");
        return FALSE;
    }

    return TRUE;
}

gboolean
mm_modem_location_get_full_finish (MMModemLocation *self,
                                   GAsyncResult *res,
                                   MMLocation3gpp **location_3gpp,
                                   MMLocationGpsNmea **location_gps_nmea,
                                   MMLocationGpsRaw **location_gps_raw,
                                   GError **error)
{
    GVariant *dictionary = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    if (!mm_gdbus_modem_location_call_get_location_finish (self, &dictionary, res, error))
        return FALSE;

    return build_locations (dictionary, location_3gpp, location_gps_nmea, location_gps_raw, error);
}

void
mm_modem_location_get_full (MMModemLocation *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_get_location (self,
                                               cancellable,
                                               callback,
                                               user_data);
}

gboolean
mm_modem_location_get_full_sync (MMModemLocation *self,
                                 MMLocation3gpp **location_3gpp,
                                 MMLocationGpsNmea **location_gps_nmea,
                                 MMLocationGpsRaw **location_gps_raw,
                                 GCancellable *cancellable,
                                 GError **error)
{
    GVariant *dictionary = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    if (!mm_gdbus_modem_location_call_get_location_sync (self, &dictionary, cancellable, error))
        return FALSE;

    return build_locations (dictionary, location_3gpp, location_gps_nmea, location_gps_raw, error);
}

MMLocation3gpp *
mm_modem_location_get_3gpp_finish (MMModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    MMLocation3gpp *location = NULL;

    mm_modem_location_get_full_finish (self, res, &location, NULL, NULL, error);
    return location;
}

void
mm_modem_location_get_3gpp (MMModemLocation *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    mm_modem_location_get_full (self, cancellable, callback, user_data);
}

MMLocation3gpp *
mm_modem_location_get_3gpp_sync (MMModemLocation *self,
                                 GCancellable *cancellable,
                                 GError **error)
{
    MMLocation3gpp *location = NULL;

    mm_modem_location_get_full_sync (self, &location, NULL, NULL, cancellable, error);
    return location;
}

MMLocationGpsNmea *
mm_modem_location_get_gps_nmea_finish (MMModemLocation *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    MMLocationGpsNmea *location = NULL;

    mm_modem_location_get_full_finish (self, res, NULL, &location, NULL, error);
    return location;
}

void
mm_modem_location_get_gps_nmea (MMModemLocation *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    mm_modem_location_get_full (self, cancellable, callback, user_data);
}

MMLocationGpsNmea *
mm_modem_location_get_gps_nmea_sync (MMModemLocation *self,
                                     GCancellable *cancellable,
                                     GError **error)
{
    MMLocationGpsNmea *location = NULL;

    mm_modem_location_get_full_sync (self, NULL, &location, NULL, cancellable, error);
    return location;
}

MMLocationGpsRaw *
mm_modem_location_get_gps_raw_finish (MMModemLocation *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    MMLocationGpsRaw *location = NULL;

    mm_modem_location_get_full_finish (self, res, NULL, NULL, &location, error);
    return location;
}

void
mm_modem_location_get_gps_raw (MMModemLocation *self,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    mm_modem_location_get_full (self, cancellable, callback, user_data);
}

MMLocationGpsRaw *
mm_modem_location_get_gps_raw_sync (MMModemLocation *self,
                                    GCancellable *cancellable,
                                    GError **error)
{
    MMLocationGpsRaw *location = NULL;

    mm_modem_location_get_full_sync (self, NULL, NULL, &location, cancellable, error);
    return location;
}
