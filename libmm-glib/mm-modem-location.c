/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
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

struct _MMModemLocationPrivate {
    /* Common mutex to sync access */
    GMutex mutex;

    MMLocation3gpp    *signaled_3gpp;
    MMLocationGpsNmea *signaled_gps_nmea;
    MMLocationGpsRaw  *signaled_gps_raw;
    MMLocationCdmaBs  *signaled_cdma_bs;

    PROPERTY_COMMON_DECLARE (signaled_location)
};

/*****************************************************************************/

/**
 * mm_modem_location_get_path:
 * @self: A #MMModemLocation.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.0
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
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.0
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
 * Gets a bitmask of the location capabilities supported by this
 * #MMModemLocation.
 *
 * Returns: A #MMModemLocationSource.
 *
 * Since: 1.0
 */
MMModemLocationSource
mm_modem_location_get_capabilities (MMModemLocation *self)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), MM_MODEM_LOCATION_SOURCE_NONE);

    return (MMModemLocationSource) mm_gdbus_modem_location_get_capabilities (MM_GDBUS_MODEM_LOCATION (self));
}

/*****************************************************************************/

/**
 * mm_modem_location_get_supported_assistance_data:
 * @self: A #MMModemLocation.
 *
 * Gets a bitmask of the supported assistance data types.
 *
 * Returns: A #MMModemLocationAssistanceDataType.
 *
 * Since: 1.10
 */
MMModemLocationAssistanceDataType
mm_modem_location_get_supported_assistance_data (MMModemLocation *self)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), MM_MODEM_LOCATION_ASSISTANCE_DATA_TYPE_NONE);

    return (MMModemLocationAssistanceDataType) mm_gdbus_modem_location_get_supported_assistance_data (MM_GDBUS_MODEM_LOCATION (self));
}

/*****************************************************************************/

/**
 * mm_modem_location_get_enabled:
 * @self: A #MMModemLocation.
 *
 * Gets a bitmask of the location capabilities which are enabled in this #MMModemLocation.
 *
 * Returns: A #MMModemLocationSource.
 *
 * Since: 1.0
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
 * Returns: %TRUE if location changes are signaled, %FALSE otherwise.
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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
 * @sources: Bitmask of #MMModemLocationSource values specifying which locations
 *  should get enabled.
 * @signal_location: Flag to enable or disable location signaling.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously configures the location sources to use when gathering location
 * information. Also enable or disable location information gathering.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_setup_finish() to get the result of the operation.
 *
 * See mm_modem_location_setup_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
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
 * @sources: Bitmask of #MMModemLocationSource values specifying which locations
 *  should get enabled.
 * @signal_location: Flag to enable or disable location signaling.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously configures the location sources to use when gathering location
 * information. Also enable or disable location information gathering.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_setup() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.0
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

/**
 * mm_modem_location_set_supl_server_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_location_set_supl_server().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_set_supl_server().
 *
 * Returns: %TRUE if setting the SUPL server was successful, %FALSE if @error is
 * set.
 *
 * Since: 1.6
 */
gboolean
mm_modem_location_set_supl_server_finish (MMModemLocation *self,
                                          GAsyncResult *res,
                                          GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_set_supl_server_finish (MM_GDBUS_MODEM_LOCATION (self), res, error);
}

/**
 * mm_modem_location_set_supl_server:
 * @self: A #MMModemLocation.
 * @supl: The SUPL server address, given as IP:PORT or with a full URL.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously configures the address of the SUPL server for A-GPS operation.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_set_supl_server_finish() to get the result of the operation.
 *
 * See mm_modem_location_set_supl_server_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.6
 */
void
mm_modem_location_set_supl_server (MMModemLocation *self,
                                   const gchar *supl,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_set_supl_server (MM_GDBUS_MODEM_LOCATION (self),
                                                  supl,
                                                  cancellable,
                                                  callback,
                                                  user_data);
}

/**
 * mm_modem_location_set_supl_server_sync:
 * @self: A #MMModemLocation.
 * @supl: The SUPL server address, given as IP:PORT or with a full URL.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously configures the address of the SUPL server for A-GPS operation.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_set_supl_server() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if setting the SUPL server was successful, %FALSE if @error is
 * set.
 *
 * Since: 1.6
 */
gboolean
mm_modem_location_set_supl_server_sync (MMModemLocation *self,
                                        const gchar *supl,
                                        GCancellable *cancellable,
                                        GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_set_supl_server_sync (MM_GDBUS_MODEM_LOCATION (self),
                                                              supl,
                                                              cancellable,
                                                              error);
}

/*****************************************************************************/

/**
 * mm_modem_location_inject_assistance_data_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_location_inject_assistance_data().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with
 * mm_modem_location_inject_assistance_data().
 *
 * Returns: %TRUE if the injection was successful, %FALSE if @error is set.
 *
 * Since: 1.10
 */
gboolean
mm_modem_location_inject_assistance_data_finish (MMModemLocation  *self,
                                                 GAsyncResult     *res,
                                                 GError          **error)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_inject_assistance_data_finish (MM_GDBUS_MODEM_LOCATION (self), res, error);
}

/**
 * mm_modem_location_inject_assistance_data:
 * @self: A #MMModemLocation.
 * @data: (array length=data_size): Data to inject.
 * @data_size: size of @data.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Aynchronously injects assistance data to the GNSS module.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_inject_assistance_data_finish() to get the result of the
 * operation.
 *
 * See mm_modem_location_inject_assistance_data_sync() for the synchronous,
 * blocking version of this method.
 *
 * Since: 1.10
 */
void
mm_modem_location_inject_assistance_data (MMModemLocation     *self,
                                          const guint8        *data,
                                          gsize                data_size,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_inject_assistance_data (MM_GDBUS_MODEM_LOCATION (self),
                                                         g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, data, data_size, sizeof (guint8)),
                                                         cancellable,
                                                         callback,
                                                         user_data);
}

/**
 * mm_modem_location_inject_assistance_data_sync:
 * @self: A #MMModemLocation.
 * @data: (array length=data_size): Data to inject.
 * @data_size: size of @data.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously injects assistance data to the GNSS module.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_inject_assistance_data() for the asynchronous version of
 * this method.
 *
 * Returns: %TRUE if the injection was successful, %FALSE if @error is set.
 *
 * Since: 1.10
 */
gboolean
mm_modem_location_inject_assistance_data_sync (MMModemLocation  *self,
                                               const guint8     *data,
                                               gsize             data_size,
                                               GCancellable     *cancellable,
                                               GError          **error)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_inject_assistance_data_sync (MM_GDBUS_MODEM_LOCATION (self),
                                                                     g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, data, data_size, sizeof (guint8)),
                                                                     cancellable,
                                                                     error);
}

/*****************************************************************************/

/**
 * mm_modem_location_set_gps_refresh_rate_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_location_set_gps_refresh_rate().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_set_gps_refresh_rate().
 *
 * Returns: %TRUE if setting the GPS refresh rate was successful, %FALSE if
 * @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_location_set_gps_refresh_rate_finish (MMModemLocation *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_set_gps_refresh_rate_finish (MM_GDBUS_MODEM_LOCATION (self), res, error);
}

/**
 * mm_modem_location_set_gps_refresh_rate:
 * @self: A #MMModemLocation.
 * @rate: The GPS refresh rate, in seconds.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously configures the GPS refresh rate.

 * If a 0 rate is used, the GPS location updates will be immediately propagated
 * to the interface.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_set_gps_refresh_rate_finish() to get the result of the
 * operation.
 *
 * See mm_modem_location_set_gps_refresh_rate_sync() for the synchronous,
 * blocking version of this method.
 *
 * Since: 1.0
 */
void
mm_modem_location_set_gps_refresh_rate (MMModemLocation *self,
                                        guint rate,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_set_gps_refresh_rate (MM_GDBUS_MODEM_LOCATION (self),
                                                       rate,
                                                       cancellable,
                                                       callback,
                                                       user_data);
}

/**
 * mm_modem_location_set_gps_refresh_rate_sync:
 * @self: A #MMModemLocation.
 * @rate: The GPS refresh rate, in seconds.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously configures the GPS refresh rate.
 *
 * If a 0 rate is used, the GPS location updates will be immediately propagated
 * to the interface.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_set_gps_refresh_rate() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if setting the refresh rate was successful, %FALSE if @error
 * is set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_location_set_gps_refresh_rate_sync (MMModemLocation *self,
                                             guint rate,
                                             GCancellable *cancellable,
                                             GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_set_gps_refresh_rate_sync (MM_GDBUS_MODEM_LOCATION (self),
                                                                   rate,
                                                                   cancellable,
                                                                   error);
}

/*****************************************************************************/

static gboolean
build_locations (GVariant           *dictionary,
                 MMLocation3gpp    **location_3gpp,
                 MMLocationGpsNmea **location_gps_nmea,
                 MMLocationGpsRaw  **location_gps_raw,
                 MMLocationCdmaBs  **location_cdma_bs,
                 GError            **error)
{
    GError       *inner_error = NULL;
    GVariant     *value;
    guint         source;
    GVariantIter  iter;

    if (!dictionary)
        /* No location provided. Not actually an error. */
        return TRUE;

    g_variant_iter_init (&iter, dictionary);
    while (!inner_error && g_variant_iter_next (&iter, "{uv}", &source, &value)) {
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

    if (inner_error) {
        g_propagate_error (error, inner_error);
        g_prefix_error (error, "Couldn't build locations result: ");
        return FALSE;
    }

    return TRUE;
}

/**
 * mm_modem_location_get_full_finish:
 * @self: A #MMModemLocation.
 * @location_3gpp: (out) (allow-none) (transfer full): Return location for a
 *  #MMLocation3gpp if 3GPP location is requested, or #NULL if not required. The
 *  returned value should be freed with g_object_unref().
 * @location_gps_nmea: (out) (allow-none) (transfer full): Return location for a
 *  #MMLocationGpsNmea if GPS NMEA location is requested, or #NULL if not
 *  required. The returned value should be freed with g_object_unref().
 * @location_gps_raw: (out) (allow-none) (transfer full): Return location for a
 *  #MMLocationGpsRaw if GPS raw location is requested, or #NULL if not required.
 *  The returned value should be freed with g_object_unref().
 * @location_cdma_bs: (out) (allow-none) (transfer full): Return location for a
 *  #MMLocationCdmaBs if CDMA Base Station location is requested, or #NULL if
 *  not required. The returned value should be freed with g_object_unref().
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_location_get_full().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_full().
 *
 * Returns: %TRUE if the retrieval was successful, %FALSE if @error is set.
 *
 * Since: 1.0
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
    g_autoptr(GVariant) dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    if (!mm_gdbus_modem_location_call_get_location_finish (MM_GDBUS_MODEM_LOCATION (self), &dictionary, res, error))
        return FALSE;

    return build_locations (dictionary, location_3gpp, location_gps_nmea, location_gps_raw, location_cdma_bs, error);
}

/**
 * mm_modem_location_get_full:
 * @self: A #MMModemLocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current location information.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_get_full_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_full_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
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
 * @location_3gpp: (out) (allow-none) (transfer full): Return location for a
 *  #MMLocation3gpp if 3GPP location is requested, or #NULL if not required. The
 *  returned value should be freed with g_object_unref().
 * @location_gps_nmea: (out) (allow-none) (transfer full): Return location for a
 *  #MMLocationGpsNmea if GPS NMEA location is requested, or #NULL if not
 *  required. The returned value should be freed with g_object_unref().
 * @location_gps_raw: (out) (allow-none) (transfer full): Return location for a
 *  #MMLocationGpsRaw if GPS raw location is requested, or #NULL if not required.
 *  The returned value should be freed with g_object_unref().
 * @location_cdma_bs: (out) (allow-none) (transfer full): Return location for a
 *  #MMLocationCdmaBs if CDMA Base Station location is requested, or #NULL if
 *  not required. The returned value should be freed with g_object_unref().
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the current location information.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_get_full() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.0
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
    g_autoptr(GVariant) dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), FALSE);

    if (!mm_gdbus_modem_location_call_get_location_sync (MM_GDBUS_MODEM_LOCATION (self), &dictionary, cancellable, error))
        return FALSE;

    return build_locations (dictionary, location_3gpp, location_gps_nmea, location_gps_raw, location_cdma_bs, error);
}

/*****************************************************************************/

/**
 * mm_modem_location_get_3gpp_finish:
 * @self: A #MMModemLocation.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_location_get_3gpp().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_3gpp().
 *
 * Returns: (transfer full): A #MMLocation3gpp, or #NULL if not available. The
 *  returned value should be freed with g_object_unref().
 *
 * Since: 1.0
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current 3GPP location information.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_get_3gpp_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_3gpp_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
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
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_get_3gpp() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A #MMLocation3gpp, or #NULL if not available. The
 * returned value should be freed with g_object_unref().
 *
 * Since: 1.0
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
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_location_get_gps_nmea().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_gps_nmea().
 *
 * Returns: (transfer full): A #MMLocationGpsNmea, or #NULL if not available.
 * The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current GPS NMEA location information.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_get_gps_nmea_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_gps_nmea_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.0
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
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_get_gps_nmea() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A #MMLocationGpsNmea, or #NULL if not available.
 * The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
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
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_location_get_gps_raw().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_gps_raw().
 *
 * Returns: (transfer full): A #MMLocationGpsRaw, or #NULL if not available.
 * The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current GPS raw location information.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_get_gps_raw_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_gps_raw_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.0
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
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_get_gps_raw() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A #MMLocationGpsRaw, or #NULL if not available.
 * The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
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
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_location_get_cdma_bs().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_location_get_cdma_bs().
 *
 * Returns: (transfer full): A #MMLocationCdmaBs, or #NULL if not available.
 * The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the current CDMA base station location information.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_location_get_cdma_bs_finish() to get the result of the operation.
 *
 * See mm_modem_location_get_cdma_bs_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.0
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
 * The calling thread is blocked until a reply is received. See
 * mm_modem_location_get_cdma_bs() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A #MMLocationCdmaBs, or #NULL if not available.
 * The returned value should be freed with g_object_unref().
 *
 * Since: 1.0
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

/**
 * mm_modem_location_get_supl_server:
 * @self: A #MMModemLocation.
 *
 * Gets the address of the SUPL server.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_location_dup_supl_server() if on another thread.</warning>
 *
 * Returns: (transfer none): The SUPL server address, or %NULL if none
 * available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.6
 */
const gchar *
mm_modem_location_get_supl_server (MMModemLocation *self)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_location_get_supl_server (MM_GDBUS_MODEM_LOCATION (self)));
}

/**
 * mm_modem_location_dup_supl_server:
 * @self: A #MMModemLocation.
 *
 * Gets the address of the SUPL server.
 *
 * Returns: (transfer full): The SUPL server address, or %NULL if none
 * available. The returned value should be freed with g_free().
 *
 * Since: 1.6
 */
gchar *
mm_modem_location_dup_supl_server (MMModemLocation *self)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_location_dup_supl_server (MM_GDBUS_MODEM_LOCATION (self)));
}

/*****************************************************************************/

/**
 * mm_modem_location_get_assistance_data_servers:
 * @self: A #MMModemLocation.
 *
 * Gets the list of assistance data servers.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_location_dup_assistance_data_servers() if on another thread.
 * </warning>
 *
 * Returns: (transfer none): a %NULL-terminated array of server addresses, or
 * %NULL if none available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.10
 */
const gchar **
mm_modem_location_get_assistance_data_servers (MMModemLocation *self)
{
    const gchar **tmp;

    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), NULL);

    tmp = (const gchar **) mm_gdbus_modem_location_get_assistance_data_servers (MM_GDBUS_MODEM_LOCATION (self));

    return ((tmp && tmp[0]) ? tmp : NULL);
}

/**
 * mm_modem_location_dup_assistance_data_servers:
 * @self: A #MMModemLocation.
 *
 * Gets the list of assistance data servers.
 *
 * Returns: (transfer full): a %NULL-terminated array of server addresses, or
 * %NULL if none available. The returned value should be freed with
 * g_strfreev().
 *
 * Since: 1.10
 */
gchar **
mm_modem_location_dup_assistance_data_servers (MMModemLocation *self)
{
    gchar **tmp;

    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), NULL);

    tmp = mm_gdbus_modem_location_dup_assistance_data_servers (MM_GDBUS_MODEM_LOCATION (self));
    if (tmp && tmp[0])
        return tmp;

    g_strfreev (tmp);
    return NULL;
}

/*****************************************************************************/

/**
 * mm_modem_location_get_gps_refresh_rate:
 * @self: A #MMModemLocation.
 *
 * Gets the GPS refresh rate, in seconds.
 *
 * Returns: The GPS refresh rate, or 0 if no fixed rate is used.
 *
 * Since: 1.0
 */
guint
mm_modem_location_get_gps_refresh_rate (MMModemLocation *self)
{
    g_return_val_if_fail (MM_IS_MODEM_LOCATION (self), 0);

    return mm_gdbus_modem_location_get_gps_refresh_rate (MM_GDBUS_MODEM_LOCATION (self));
}

/*****************************************************************************/

/* custom refresh method instead of PROPERTY_OBJECT_DEFINE_REFRESH() */
static void
signaled_location_refresh (MMModemLocation *self)
{
    g_autoptr(GVariant) variant = NULL;
    g_autoptr(GError)   inner_error = NULL;

    g_clear_object (&self->priv->signaled_3gpp);
    g_clear_object (&self->priv->signaled_gps_nmea);
    g_clear_object (&self->priv->signaled_gps_raw);
    g_clear_object (&self->priv->signaled_cdma_bs);

    variant = mm_gdbus_modem_location_dup_location (MM_GDBUS_MODEM_LOCATION (self));
    if (!variant)
        return;

    if (!build_locations (variant,
                          &self->priv->signaled_3gpp,
                          &self->priv->signaled_gps_nmea,
                          &self->priv->signaled_gps_raw,
                          &self->priv->signaled_cdma_bs,
                          &inner_error))
        g_warning ("Invalid signaled location received: %s", inner_error->message);
}

PROPERTY_DEFINE_UPDATED (signaled_location, ModemLocation)

/**
 * mm_modem_location_peek_signaled_3gpp:
 * @self: A #MMModemLocation.
 *
 * Gets a #MMLocation3gpp object with the current 3GPP location information.
 *
 * Unlike mm_modem_location_get_3gpp() or mm_modem_location_get_3gpp_sync(),
 * this method does not perform an explicit query. Instead, this method will
 * return the location information that may have been signaled by the modem.
 * Therefore, this method will only succeed if location signaling is enabled
 * (e.g. with mm_modem_location_setup() in the #MMModemLocation).
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_location_get_signaled_3gpp() if on
 * another thread.</warning>
 *
 * Returns: (transfer none): A #MMLocation3gpp, or %NULL if none available. Do
 * not free the returned value, it belongs to @self.
 *
 * Since: 1.18
 */

PROPERTY_OBJECT_DEFINE_PEEK (signaled_location, signaled_3gpp, ModemLocation, modem_location, MODEM_LOCATION, MMLocation3gpp)

/**
 * mm_modem_location_get_signaled_3gpp:
 * @self: A #MMModemLocation.
 *
 * Gets a #MMLocation3gpp object with the current 3GPP location information.
 *
 * Unlike mm_modem_location_get_3gpp() or mm_modem_location_get_3gpp_sync(),
 * this method does not perform an explicit query. Instead, this method will
 * return the location information that may have been signaled by the modem.
 * Therefore, this method will only succeed if location signaling is enabled
 * (e.g. with mm_modem_location_setup() in the #MMModemLocation).
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_location_get_signaled_3gpp() again to get a new #MMLocation3gpp
 * with the new values.</warning>
 *
 * Returns: (transfer full): A #MMLocation3gpp that must be freed with
 * g_object_unref() or %NULL if none available.
 *
 * Since: 1.18
 */

PROPERTY_OBJECT_DEFINE_GET (signaled_location, signaled_3gpp, ModemLocation, modem_location, MODEM_LOCATION, MMLocation3gpp)

/**
 * mm_modem_location_peek_signaled_gps_nmea:
 * @self: A #MMModemLocation.
 *
 * Gets a #MMLocationGpsNmea object with the current GPS NMEA location
 * information.
 *
 * Unlike mm_modem_location_get_gps_nmea() or
 * mm_modem_location_get_gps_nmea_sync(), this method does not perform an
 * explicit query. Instead, this method will return the location information
 * that may have been signaled by the modem. Therefore, this method will only
 * succeed if location signaling is enabled (e.g. with mm_modem_location_setup()
 * in the #MMModemLocation).
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_location_get_signaled_gps_nmea() if on
 * another thread.</warning>
 *
 * Returns: (transfer none): A #MMLocationGpsNmea, or %NULL if none available. Do
 * not free the returned value, it belongs to @self.
 *
 * Since: 1.18
 */

PROPERTY_OBJECT_DEFINE_PEEK (signaled_location, signaled_gps_nmea, ModemLocation, modem_location, MODEM_LOCATION, MMLocationGpsNmea)

/**
 * mm_modem_location_get_signaled_gps_nmea:
 * @self: A #MMModemLocation.
 *
 * Gets a #MMLocationGpsNmea object with the current GPS NMEA location
 * information.
 *
 * Unlike mm_modem_location_get_gps_nmea() or
 * mm_modem_location_get_gps_nmea_sync(), this method does not perform an
 * explicit query. Instead, this method will return the location information
 * that may have been signaled by the modem. Therefore, this method will only
 * succeed if location signaling is enabled (e.g. with mm_modem_location_setup()
 * in the #MMModemLocation).
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_location_get_signaled_gps_nmea() again to get a new #MMLocationGpsNmea
 * with the new values.</warning>
 *
 * Returns: (transfer full): A #MMLocationGpsNmea that must be freed with
 * g_object_unref() or %NULL if none available.
 *
 * Since: 1.18
 */
PROPERTY_OBJECT_DEFINE_GET (signaled_location, signaled_gps_nmea, ModemLocation, modem_location, MODEM_LOCATION, MMLocationGpsNmea)

/**
 * mm_modem_location_peek_signaled_gps_raw:
 * @self: A #MMModemLocation.
 *
 * Gets a #MMLocationGpsRaw object with the current GPS raw location
 * information.
 *
 * Unlike mm_modem_location_get_gps_raw() or
 * mm_modem_location_get_gps_raw_sync(), this method does not perform an
 * explicit query. Instead, this method will return the location information
 * that may have been signaled by the modem. Therefore, this method will only
 * succeed if location signaling is enabled (e.g. with mm_modem_location_setup()
 * in the #MMModemLocation).
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_location_get_signaled_gps_raw() if on
 * another thread.</warning>
 *
 * Returns: (transfer none): A #MMLocationGpsRaw, or %NULL if none available. Do
 * not free the returned value, it belongs to @self.
 *
 * Since: 1.18
 */

PROPERTY_OBJECT_DEFINE_PEEK (signaled_location, signaled_gps_raw, ModemLocation, modem_location, MODEM_LOCATION, MMLocationGpsRaw)

/**
 * mm_modem_location_get_signaled_gps_raw:
 * @self: A #MMModemLocation.
 *
 * Gets a #MMLocationGpsRaw object with the current GPS raw location
 * information.
 *
 * Unlike mm_modem_location_get_gps_raw() or
 * mm_modem_location_get_gps_raw_sync(), this method does not perform an
 * explicit query. Instead, this method will return the location information
 * that may have been signaled by the modem. Therefore, this method will only
 * succeed if location signaling is enabled (e.g. with mm_modem_location_setup()
 * in the #MMModemLocation).
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_location_get_signaled_gps_raw() again to get a new #MMLocationGpsRaw
 * with the new values.</warning>
 *
 * Returns: (transfer full): A #MMLocationGpsRaw that must be freed with
 * g_object_unref() or %NULL if none available.
 *
 * Since: 1.18
 */

PROPERTY_OBJECT_DEFINE_GET (signaled_location, signaled_gps_raw, ModemLocation, modem_location, MODEM_LOCATION, MMLocationGpsRaw)

/**
 * mm_modem_location_peek_signaled_cdma_bs:
 * @self: A #MMModemLocation.
 *
 * Gets a #MMLocationCdmaBs object with the current CDMA base station location
 * information.
 *
 * Unlike mm_modem_location_get_cdma_bs() or
 * mm_modem_location_get_cdma_bs_sync(), this method does not perform an
 * explicit query. Instead, this method will return the location information
 * that may have been signaled by the modem. Therefore, this method will only
 * succeed if location signaling is enabled (e.g. with mm_modem_location_setup()
 * in the #MMModemLocation).
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_location_get_signaled_cdma_bs() if on
 * another thread.</warning>
 *
 * Returns: (transfer none): A #MMLocationCdmaBs, or %NULL if none available. Do
 * not free the returned value, it belongs to @self.
 *
 * Since: 1.18
 */

PROPERTY_OBJECT_DEFINE_PEEK (signaled_location, signaled_cdma_bs, ModemLocation, modem_location, MODEM_LOCATION, MMLocationCdmaBs)

/**
 * mm_modem_location_get_signaled_cdma_bs:
 * @self: A #MMModemLocation.
 *
 * Gets a #MMLocationCdmaBs object with the current CDMA base station location
 * information.
 *
 * Unlike mm_modem_location_get_cdma_bs() or
 * mm_modem_location_get_cdma_bs_sync(), this method does not perform an
 * explicit query. Instead, this method will return the location information
 * that may have been signaled by the modem. Therefore, this method will only
 * succeed if location signaling is enabled (e.g. with mm_modem_location_setup()
 * in the #MMModemLocation).
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_location_get_signaled_cdma_bs() again to get a new #MMLocationCdmaBs
 * with the new values.</warning>
 *
 * Returns: (transfer full): A #MMLocationCdmaBs that must be freed with
 * g_object_unref() or %NULL if none available.
 *
 * Since: 1.18
 */
PROPERTY_OBJECT_DEFINE_GET (signaled_location, signaled_cdma_bs, ModemLocation, modem_location, MODEM_LOCATION, MMLocationCdmaBs)

/*****************************************************************************/

static void
mm_modem_location_init (MMModemLocation *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_MODEM_LOCATION, MMModemLocationPrivate);
    g_mutex_init (&self->priv->mutex);

    PROPERTY_INITIALIZE (signaled_location, "location")
}

static void
finalize (GObject *object)
{
    MMModemLocation *self = MM_MODEM_LOCATION (object);

    g_mutex_clear (&self->priv->mutex);

    PROPERTY_OBJECT_FINALIZE (signaled_3gpp)
    PROPERTY_OBJECT_FINALIZE (signaled_gps_nmea)
    PROPERTY_OBJECT_FINALIZE (signaled_gps_raw)
    PROPERTY_OBJECT_FINALIZE (signaled_cdma_bs)

    G_OBJECT_CLASS (mm_modem_location_parent_class)->finalize (object);
}

static void
mm_modem_location_class_init (MMModemLocationClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModemLocationPrivate));

    object_class->finalize = finalize;
}
