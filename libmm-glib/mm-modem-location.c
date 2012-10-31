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
#include "mm-errors-types.h"
#include "mm-modem-location.h"

/**
 * SECTION: mm-modem-location
 * @title: MMModemLocation
 * @short_description: The Location interface
 *
 * The #MMModemLocation is an object providing access to the methods, signals and
 * properties of the Location interface.
 *
 * The Location interface is exposed whenever a modem has location capabilities.
 */

G_DEFINE_TYPE (MMModemLocation, mm_modem_location, MM_GDBUS_TYPE_MODEM_LOCATION_PROXY)

/*****************************************************************************/

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
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), NULL);

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

    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_location_get_capabilities:
 * @self: A #MMModemLocation.
 *
 * Gets a bitmask of the location capabilities supported by this #MMModemLocation.
 *
 * Returns: A #MMModemLocationSource.
 */
MMModemLocationSource
mm_modem_location_get_capabilities (MMModemLocation *self)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), MM_MODEM_LOCATION_SOURCE_NONE);

    return (MMModemLocationSource) mm_gdbus_modem_location_get_capabilities (MM_GDBUS_MODEM_LOCATION (self));
}

/*****************************************************************************/

/**
 * mm_modem_location_get_capabilities:
 * @self: A #MMModemLocation.
 *
 * Gets a bitmask of the location capabilities which are enabled in this #MMModemLocation.
 *
 * Returns: A #MMModemLocationSource.
 */
MMModemLocationSource
mm_modem_location_get_enabled (MMModemLocation *self)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return (MMModemLocationSource) mm_gdbus_modem_location_get_enabled (MM_GDBUS_MODEM_LOCATION (self));
}

/*****************************************************************************/

/**
 * mm_modem_location_signals_location:
 * @self: A #MMModemLocation.
 *
 * Gets the status of the location signaling in the #MMModemLocation.
 *
 * Returns: %TRUE if location changes are signaled, %FALSE otherwise..
 */
gboolean
mm_modem_location_signals_location (MMModemLocation *self)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_get_signals_location (MM_GDBUS_MODEM_LOCATION (self));
}

/*****************************************************************************/

/**
 * mm_modem_location_setup_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_location_setup().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_setup().
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 */
gboolean
mm_modem_location_setup_finish (MMModemLocation *self,
                                GAsyncResult *res,
                                GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_setup_finish (MM_GDBUS_MODEM_LOCATION (self), res, error);
}

/**
 * mm_modem_location_setup:
 * @self: A #MMModemLocation.
 * @sources: Bitmask of #MMModemLocationSource values specifying which locations should get enabled.
 * @signal_location: Flag to enable or disable location signaling.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously configures the location sources to use when gathering location
 * information. Also enable or disable location information gathering.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_location_setup_finish() to get the result of the operation.
 *
 * See mm_modem_location_setup_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_location_setup (MMModemLocation *self,
                         MMModemLocationSource sources,
                         gboolean signal_location,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_setup (MM_GDBUS_MODEM_LOCATION (self),
                                        sources,
                                        signal_location,
                                        cancellable,
                                        callback,
                                        user_data);
}

/**
 * mm_modem_location_setup_sync:
 * @self: A #MMModemLocation.
 * @sources: Bitmask of #MMModemLocationSource values specifying which locations should get enabled.
 * @signal_location: Flag to enable or disable location signaling.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously configures the location sources to use when gathering location
 * information. Also enable or disable location information gathering.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_location_setup()
 * for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 */
gboolean
mm_modem_location_setup_sync (MMModemLocation *self,
                              MMModemLocationSource sources,
                              gboolean signal_location,
                              GCancellable *cancellable,
                              GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_setup_sync (MM_GDBUS_MODEM_LOCATION (self),
                                                    sources,
                                                    signal_location,
                                                    cancellable,
                                                    error);
}

/*****************************************************************************/

static gboolean
build_locations (GVariant *dictionary,
                 MMLocation3gpp **location_3gpp,
                 MMLocationGpsNmea **location_gps_nmea,
                 MMLocationGpsRaw **location_gps_raw,
                 MMLocationCdmaBs **location_cdma_bs,
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
        case MM_MODEM_LOCATION_SOURCE_CDMA_BS:
            if (location_cdma_bs)
                *location_cdma_bs = mm_location_cdma_bs_new_from_dictionary (value, &inner_error);
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

/**
 * mm_modem_location_get_full_finish:
 * @self: A #MMModemLocation.
 * @location_3gpp: (out) (allow-none) (transfer full): Return location for a #MMLocation3gpp if 3GPP location is requested, or #NULL if not required. The returned value should be freed with g_object_unref().
 * @location_gps_nmea: (out) (allow-none) (transfer full): Return location for a #MMLocationGpsNmea if GPS NMEA location is requested, or #NULL if not required. The returned value should be freed with g_object_unref().
 * @location_gps_raw: (out) (allow-none) (transfer full): Return location for a #MMLocationGpsRaw if GPS raw location is requested, or #NULL if not required. The returned value should be freed with g_object_unref().
 * @location_cdma_bs: (out) (allow-none) (transfer full): Return location for a #MMLocationCdmaBs if CDMA Base Station location is requested, or #NULL if not required. The returned value should be freed with g_object_unref().
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_location_get_full().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_full().
 *
 * Returns: %TRUE if the retrieval was successful, %FALSE if @error is set.
 */
gboolean
mm_modem_location_get_full_finish (MMModemLocation *self,
                                   GAsyncResult *res,
                                   MMLocation3gpp **location_3gpp,
                                   MMLocationGpsNmea **location_gps_nmea,
                                   MMLocationGpsRaw **location_gps_raw,
                                   MMLocationCdmaBs **location_cdma_bs,
                                   GError **error)
{
    GVariant *dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    if (!mm_gdbus_modem_location_call_get_location_finish (MM_GDBUS_MODEM_LOCATION (self), &dictionary, res, error))
        return FALSE;

    return build_locations (dictionary, location_3gpp, location_gps_nmea, location_gps_raw, location_cdma_bs, error);
}

/**
 * mm_modem_location_get_full:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current location information.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_location_get_full_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_full_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_location_get_full (MMModemLocation *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_get_location (MM_GDBUS_MODEM_LOCATION (self),
                                               cancellable,
                                               callback,
                                               user_data);
}

/**
 * mm_modem_location_get_full_sync:
 * @self: A #MMModemLocation.
 * @location_3gpp: (out) (allow-none) (transfer full): Return location for a #MMLocation3gpp if 3GPP location is requested, or #NULL if not required. The returned value should be freed with g_object_unref().
 * @location_gps_nmea: (out) (allow-none) (transfer full): Return location for a #MMLocationGpsNmea if GPS NMEA location is requested, or #NULL if not required. The returned value should be freed with g_object_unref().
 * @location_gps_raw: (out) (allow-none) (transfer full): Return location for a #MMLocationGpsRaw if GPS raw location is requested, or #NULL if not required. The returned value should be freed with g_object_unref().
 * @location_cdma_bs: (out) (allow-none) (transfer full): Return location for a #MMLocationCdmaBs if CDMA Base Station location is requested, or #NULL if not required. The returned value should be freed with g_object_unref().
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the current location information.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_location_get_full()
 * for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 */
gboolean
mm_modem_location_get_full_sync (MMModemLocation *self,
                                 MMLocation3gpp **location_3gpp,
                                 MMLocationGpsNmea **location_gps_nmea,
                                 MMLocationGpsRaw **location_gps_raw,
                                 MMLocationCdmaBs **location_cdma_bs,
                                 GCancellable *cancellable,
                                 GError **error)
{
    GVariant *dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    if (!mm_gdbus_modem_location_call_get_location_sync (MM_GDBUS_MODEM_LOCATION (self), &dictionary, cancellable, error))
        return FALSE;

    return build_locations (dictionary, location_3gpp, location_gps_nmea, location_gps_raw, location_cdma_bs, error);
}

/*****************************************************************************/

/**
 * mm_modem_location_get_3gpp_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_location_get_3gpp().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_3gpp().
 *
 * Returns: (transfer full) A #MMLocation3gpp, or #NULL if not available. The returned value should be freed with g_object_unref().
 */
MMLocation3gpp *
mm_modem_location_get_3gpp_finish (MMModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    MMLocation3gpp *location = NULL;

    mm_modem_location_get_full_finish (self, res, &location, NULL, NULL, NULL, error);

    return location;
}

/**
 * mm_modem_location_get_3gpp:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current 3GPP location information.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_location_get_3gpp_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_3gpp_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_location_get_3gpp (MMModemLocation *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    mm_modem_location_get_full (self, cancellable, callback, user_data);
}

/**
 * mm_modem_location_get_3gpp_sync:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the current 3GPP location information.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_location_get_3gpp()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer full) A #MMLocation3gpp, or #NULL if not available. The returned value should be freed with g_object_unref().
 */
MMLocation3gpp *
mm_modem_location_get_3gpp_sync (MMModemLocation *self,
                                 GCancellable *cancellable,
                                 GError **error)
{
    MMLocation3gpp *location = NULL;

    mm_modem_location_get_full_sync (self, &location, NULL, NULL, NULL, cancellable, error);

    return location;
}

/*****************************************************************************/

/**
 * mm_modem_location_get_gps_nmea_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_location_get_gps_nmea().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_gps_nmea().
 *
 * Returns: (transfer full) A #MMLocationGpsNmea, or #NULL if not available. The returned value should be freed with g_object_unref().
 */
MMLocationGpsNmea *
mm_modem_location_get_gps_nmea_finish (MMModemLocation *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    MMLocationGpsNmea *location = NULL;

    mm_modem_location_get_full_finish (self, res, NULL, &location, NULL, NULL, error);

    return location;
}

/**
 * mm_modem_location_get_gps_nmea:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current GPS NMEA location information.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_location_get_gps_nmea_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_gps_nmea_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_location_get_gps_nmea (MMModemLocation *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    mm_modem_location_get_full (self, cancellable, callback, user_data);
}

/**
 * mm_modem_location_get_gps_nmea_sync:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the current GPS NMEA location information.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_location_get_gps_nmea()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer full) A #MMLocationGpsNmea, or #NULL if not available. The returned value should be freed with g_object_unref().
 */
MMLocationGpsNmea *
mm_modem_location_get_gps_nmea_sync (MMModemLocation *self,
                                     GCancellable *cancellable,
                                     GError **error)
{
    MMLocationGpsNmea *location = NULL;

    mm_modem_location_get_full_sync (self, NULL, &location, NULL, NULL, cancellable, error);

    return location;
}

/*****************************************************************************/

/**
 * mm_modem_location_get_gps_raw_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_location_get_gps_raw().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_gps_raw().
 *
 * Returns: (transfer full) A #MMLocationGpsRaw, or #NULL if not available. The returned value should be freed with g_object_unref().
 */
MMLocationGpsRaw *
mm_modem_location_get_gps_raw_finish (MMModemLocation *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    MMLocationGpsRaw *location = NULL;

    mm_modem_location_get_full_finish (self, res, NULL, NULL, &location, NULL, error);

    return location;
}

/**
 * mm_modem_location_get_gps_raw:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current GPS raw location information.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_location_get_gps_raw_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_gps_raw_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_location_get_gps_raw (MMModemLocation *self,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    mm_modem_location_get_full (self, cancellable, callback, user_data);
}

/**
 * mm_modem_location_get_gps_raw_sync:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the current GPS raw location information.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_location_get_gps_raw()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer full) A #MMLocationGpsRaw, or #NULL if not available. The returned value should be freed with g_object_unref().
 */
MMLocationGpsRaw *
mm_modem_location_get_gps_raw_sync (MMModemLocation *self,
                                    GCancellable *cancellable,
                                    GError **error)
{
    MMLocationGpsRaw *location = NULL;

    mm_modem_location_get_full_sync (self, NULL, NULL, &location, NULL, cancellable, error);

    return location;
}

/*****************************************************************************/

/**
 * mm_modem_location_get_cdma_bs_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_location_get_cdma_bs().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_cdma_bs().
 *
 * Returns: (transfer full) A #MMLocationCdmaBs, or #NULL if not available. The returned value should be freed with g_object_unref().
 */
MMLocationCdmaBs *
mm_modem_location_get_cdma_bs_finish (MMModemLocation *self,
                                      GAsyncResult *res,
                                      GError **error)
{
    MMLocationCdmaBs *location = NULL;

    mm_modem_location_get_full_finish (self, res, NULL, NULL, NULL, &location, error);

    return location;
}

/**
 * mm_modem_location_get_cdma_bs:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current CDMA base station location information.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_location_get_cdma_bs_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_cdma_bs_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_location_get_cdma_bs (MMModemLocation *self,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    mm_modem_location_get_full (self, cancellable, callback, user_data);
}

/**
 * mm_modem_location_get_cdma_bs_sync:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the current CDMA base station location information.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_location_get_cdma_bs()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer full) A #MMLocationCdmaBs, or #NULL if not available. The returned value should be freed with g_object_unref().
 */
MMLocationCdmaBs *
mm_modem_location_get_cdma_bs_sync (MMModemLocation *self,
                                    GCancellable *cancellable,
                                    GError **error)
{
    MMLocationCdmaBs *location = NULL;

    mm_modem_location_get_full_sync (self, NULL, NULL, NULL, &location, cancellable, error);

    return location;
}

/*****************************************************************************/

static void
mm_modem_location_init (MMModemLocation *self)
{
}

static void
mm_modem_location_class_init (MMModemLocationClass *modem_class)
{
}
