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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#include "mm-helpers.h"
#include "mm-common-helpers.h"
#include "mm-bearer.h"

/**
 * SECTION: mm-bearer
 * @title: MMBearer
 * @short_description: The Bearer interface
 *
 * The #MMBearer is an object providing access to the methods, signals and
 * properties of the Bearer interface.
 *
 * When the bearer is exposed and available in the bus, it is ensured that at
 * least this interface is also available.
 */

G_DEFINE_TYPE (MMBearer, mm_bearer, MM_GDBUS_TYPE_BEARER_PROXY)

struct _MMBearerPrivate {
    /* Common mutex to sync access */
    GMutex mutex;

    PROPERTY_OBJECT_DECLARE (ipv4_config, MMBearerIpConfig)
    PROPERTY_OBJECT_DECLARE (ipv6_config, MMBearerIpConfig)
    PROPERTY_OBJECT_DECLARE (properties,  MMBearerProperties)
    PROPERTY_OBJECT_DECLARE (stats,       MMBearerStats)

    PROPERTY_ERROR_DECLARE (connection_error)
};

/*****************************************************************************/

/**
 * mm_bearer_get_path:
 * @self: A #MMBearer.
 *
 * Gets the DBus path of the #MMBearer object.
 *
 * Returns: (transfer none): The DBus path of the #MMBearer object.
 *
 * Since: 1.0
 */
const gchar *
mm_bearer_get_path (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_bearer_dup_path:
 * @self: A #MMBearer.
 *
 * Gets a copy of the DBus path of the #MMBearer object.
 *
 * Returns: (transfer full): The DBus path of the #MMBearer object. The returned
 * value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_bearer_dup_path (MMBearer *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_bearer_get_interface:
 * @self: A #MMBearer.
 *
 * Gets the operating system name for the network data interface that provides
 * packet data using this #MMBearer. This will only be available once the #MMBearer
 * is in connected state.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_bearer_dup_interface() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The name of the interface, or %NULL if it couldn't
 * be retrieved.
 *
 * Since: 1.0
 */
const gchar *
mm_bearer_get_interface (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_bearer_get_interface (MM_GDBUS_BEARER (self)));
}

/**
 * mm_bearer_dup_interface:
 * @self: A #MMBearer.
 *
 * Gets a copy of the operating system name for the network data interface that
 * provides packet data using this #MMBearer. This will only be available once
 * the #MMBearer is in connected state.
 *
 * Returns: (transfer full): The name of the interface, or %NULL if it couldn't
 * be retrieved. The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_bearer_dup_interface (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_bearer_dup_interface (MM_GDBUS_BEARER (self)));
}

/*****************************************************************************/

/**
 * mm_bearer_get_connected:
 * @self: A #MMBearer.
 *
 * Checks whether or not the #MMBearer is connected and thus whether packet data
 * communication is possible.
 *
 * Returns: %TRUE if the #MMBearer is connected, #FALSE otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_bearer_get_connected (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_get_connected (MM_GDBUS_BEARER (self));
}

/*****************************************************************************/

/**
 * mm_bearer_get_reload_stats_supported:
 * @self: A #MMBearer.
 *
 * Checks whether or not the #MMBearer supporting stats reload (to have
 * RX and TX bytes of the ongoing connection).
 *
 * Returns: %TRUE if the #MMBearer supports these stats, #FALSE otherwise.
 *
 * Since: 1.20
 */
gboolean
mm_bearer_get_reload_stats_supported (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_get_reload_stats_supported (MM_GDBUS_BEARER (self));
}

/*****************************************************************************/

/**
 * mm_bearer_get_suspended:
 * @self: A #MMBearer.
 *
 * Checks whether or not the #MMBearer is suspended (but not deactivated) while
 * the device is handling other communications, like a voice call.
 *
 * Returns: %TRUE if packet data service is suspended in the #MMBearer, #FALSE
 * otherwise.
 *
 * Since: 1.0
 */
gboolean
mm_bearer_get_suspended (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_get_suspended (MM_GDBUS_BEARER (self));
}

/*****************************************************************************/

/**
 * mm_bearer_get_multiplexed:
 * @self: A #MMBearer.
 *
 * Checks whether or not the #MMBearer is connected through a multiplexed
 * network likn.
 *
 * Returns: %TRUE if packet data service is connected via a multiplexed network
 * link in the #MMBearer, #FALSE otherwise.
 *
 * Since: 1.18
 */
gboolean
mm_bearer_get_multiplexed (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_get_multiplexed (MM_GDBUS_BEARER (self));
}

/*****************************************************************************/

/**
 * mm_bearer_get_ip_timeout:
 * @self: A #MMBearer.
 *
 * Gets the maximum time to wait for the bearer to retrieve a valid IP address.
 *
 * Returns: The IP timeout, or 0 if no specific one given.
 *
 * Since: 1.0
 */
guint
mm_bearer_get_ip_timeout (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), 0);

    return mm_gdbus_bearer_get_ip_timeout (MM_GDBUS_BEARER (self));
}

/*****************************************************************************/

/**
 * mm_bearer_get_bearer_type:
 * @self: A #MMBearer.
 *
 * Gets the type of bearer.
 *
 * Returns: a #MMBearerType.
 *
 * Since: 1.0
 */
MMBearerType
mm_bearer_get_bearer_type (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), MM_BEARER_TYPE_UNKNOWN);

    return mm_gdbus_bearer_get_bearer_type (MM_GDBUS_BEARER (self));
}

/*****************************************************************************/

/**
 * mm_bearer_get_profile_id:
 * @self: A #MMBearer.
 *
 * Gets profile ID associated to the bearer connection, if known.
 *
 * If the bearer is disconnected or the modem doesn't support profile management
 * features, %MM_3GPP_PROFILE_ID_UNKNOWN.
 *
 * Returns: a profile id.
 *
 * Since: 1.18
 */
gint
mm_bearer_get_profile_id (MMBearer *self)
{
    g_return_val_if_fail (MM_IS_BEARER (self), MM_3GPP_PROFILE_ID_UNKNOWN);

    return mm_gdbus_bearer_get_profile_id (MM_GDBUS_BEARER (self));
}
/*****************************************************************************/

/**
 * mm_bearer_get_ipv4_config:
 * @self: A #MMBearer.
 *
 * Gets a #MMBearerIpConfig object specifying the IPv4 configuration to use in
 * the bearer.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_bearer_get_ipv4_config() again to get a new #MMBearerIpConfig with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #MMBearerIpConfig that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.0
 */


/**
 * mm_bearer_peek_ipv4_config:
 * @self: A #MMBearer.
 *
 * Gets a #MMBearerIpConfig object specifying the IPv4 configuration to use in
 * the bearer.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_bearer_get_ipv4_config() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): A #MMBearerIpConfig. Do not free the returned
 * value, it belongs to @self.
 *
 * Since: 1.0
 */

/* helpers to match the property substring name with the one in our API */
#define mm_gdbus_bearer_dup_ipv4_config mm_gdbus_bearer_dup_ip4_config

PROPERTY_OBJECT_DEFINE_FAILABLE (ipv4_config,
                                 Bearer, bearer, BEARER,
                                 MMBearerIpConfig,
                                 mm_bearer_ip_config_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_bearer_get_ipv6_config:
 * @self: A #MMBearer.
 *
 * Gets a #MMBearerIpConfig object specifying the IPv6 configuration to use in
 * the bearer.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_bearer_get_ipv6_config() again to get a new #MMBearerIpConfig with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #MMBearerIpConfig that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.0
 */

/**
 * mm_bearer_peek_ipv6_config:
 * @self: A #MMBearer.
 *
 * Gets a #MMBearerIpConfig object specifying the IPv6 configuration to use in
 * the bearer.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_bearer_get_ipv6_config() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): A #MMBearerIpConfig. Do not free the returned
 * value, it belongs to @self.
 *
 * Since: 1.0
 */

/* helpers to match the property substring name with the one in our API */
#define mm_gdbus_bearer_dup_ipv6_config mm_gdbus_bearer_dup_ip6_config

PROPERTY_OBJECT_DEFINE_FAILABLE (ipv6_config,
                                 Bearer, bearer, BEARER,
                                 MMBearerIpConfig,
                                 mm_bearer_ip_config_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_bearer_get_properties:
 * @self: A #MMBearer.
 *
 * Gets a #MMBearerProperties object specifying the properties which were used
 * to create the bearer.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_bearer_get_properties() again to get a new #MMBearerProperties with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #MMBearerProperties that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.0
 */

/**
 * mm_bearer_peek_properties:
 * @self: A #MMBearer.
 *
 * Gets a #MMBearerProperties object specifying the properties which were used
 * to create the bearer.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_bearer_get_properties() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): A #MMBearerProperties. Do not free the returned
 * value, it belongs to @self.
 *
 * Since: 1.0
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (properties,
                                 Bearer, bearer, BEARER,
                                 MMBearerProperties,
                                 mm_bearer_properties_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_bearer_get_stats:
 * @self: A #MMBearer.
 *
 * Gets a #MMBearerStats object specifying the statistics of the current bearer
 * connection.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_bearer_get_stats() again to get a new #MMBearerStats with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #MMBearerStats that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.6
 */

/**
 * mm_bearer_peek_stats:
 * @self: A #MMBearer.
 *
 * Gets a #MMBearerStats object specifying the statistics of the current bearer
 * connection.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_bearer_get_stats() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): A #MMBearerStats. Do not free the returned value,
 * it belongs to @self.
 *
 * Since: 1.6
 */

PROPERTY_OBJECT_DEFINE_FAILABLE (stats,
                                 Bearer, bearer, BEARER,
                                 MMBearerStats,
                                 mm_bearer_stats_new_from_dictionary)

/*****************************************************************************/

/**
 * mm_bearer_get_connection_error:
 * @self: A #MMBearer.
 *
 * Gets a #GError specifying the connection error details, if any.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_bearer_get_connection_error() again to get a new #GError with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #GError that must be freed with
 * g_error_free() or %NULL if none.
 *
 * Since: 1.18
 */

/**
 * mm_bearer_peek_connection_error:
 * @self: A #MMBearer.
 *
 * Gets a #GError specifying the connection error details, if any.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_bearer_get_connection_error() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): A #GError, or %NULL if none. Do not
 * free the returned value, it belongs to @self.
 *
 * Since: 1.18
 */

PROPERTY_ERROR_DEFINE_FAILABLE (connection_error,
                                Bearer, bearer, BEARER,
                                mm_common_error_from_tuple)

/*****************************************************************************/

/**
 * mm_bearer_connect_finish:
 * @self: A #MMBearer.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_bearer_connect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_bearer_connect().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_bearer_connect_finish (MMBearer *self,
                          GAsyncResult *res,
                          GError **error)
{
    g_return_val_if_fail (MM_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_connect_finish (MM_GDBUS_BEARER (self), res, error);
}

/**
 * mm_bearer_connect:
 * @self: A #MMBearer.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests activation of a packet data connection with the
 * network using this #MMBearer properties.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_bearer_connect_finish() to get the result of the operation.
 *
 * See mm_bearer_connect_sync() for the synchronous, blocking version of this method.
 *
 * Since: 1.0
 */
void
mm_bearer_connect (MMBearer *self,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_BEARER (self));

    mm_gdbus_bearer_call_connect (MM_GDBUS_BEARER (self), cancellable, callback, user_data);
}

/**
 * mm_bearer_connect_sync:
 * @self: A #MMBearer.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests activation of a packet data connection with the
 * network using this #MMBearer properties.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_bearer_connect() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_bearer_connect_sync (MMBearer *self,
                        GCancellable *cancellable,
                        GError **error)
{
    g_return_val_if_fail (MM_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_connect_sync (MM_GDBUS_BEARER (self), cancellable, error);
}

/*****************************************************************************/

/**
 * mm_bearer_disconnect:
 * @self: A #MMBearer.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Synchronously requests disconnection and deactivation of the packet data
 * connection.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_bearer_disconnect_finish() to get the result of the operation.
 *
 * See mm_bearer_disconnect_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
 */
void
mm_bearer_disconnect (MMBearer *self,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    g_return_if_fail (MM_IS_BEARER (self));

    mm_gdbus_bearer_call_disconnect (MM_GDBUS_BEARER (self), cancellable, callback, user_data);
}

/**
 * mm_bearer_disconnect_finish:
 * @self: A #MMBearer.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_bearer_disconnect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_bearer_disconnect().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_bearer_disconnect_finish (MMBearer *self,
                             GAsyncResult *res,
                             GError **error)
{
    g_return_val_if_fail (MM_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_disconnect_finish (MM_GDBUS_BEARER (self), res, error);
}

/**
 * mm_bearer_disconnect_sync:
 * @self: A #MMBearer.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests disconnection and deactivation of the packet data
 * connection.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_bearer_disconnect() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_bearer_disconnect_sync (MMBearer *self,
                           GCancellable *cancellable,
                           GError **error)
{
    g_return_val_if_fail (MM_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_disconnect_sync (MM_GDBUS_BEARER (self), cancellable, error);
}

/*****************************************************************************/

static void
mm_bearer_init (MMBearer *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BEARER, MMBearerPrivate);
    g_mutex_init (&self->priv->mutex);

    PROPERTY_INITIALIZE (ipv4_config,      "ip4-config")
    PROPERTY_INITIALIZE (ipv6_config,      "ip6-config")
    PROPERTY_INITIALIZE (properties,       "properties")
    PROPERTY_INITIALIZE (stats,            "stats")
    PROPERTY_INITIALIZE (connection_error, "connection-error")
}

static void
finalize (GObject *object)
{
    MMBearer *self = MM_BEARER (object);

    g_mutex_clear (&self->priv->mutex);

    PROPERTY_OBJECT_FINALIZE (ipv4_config)
    PROPERTY_OBJECT_FINALIZE (ipv6_config)
    PROPERTY_OBJECT_FINALIZE (properties)
    PROPERTY_OBJECT_FINALIZE (stats)

    PROPERTY_ERROR_FINALIZE (connection_error)

    G_OBJECT_CLASS (mm_bearer_parent_class)->finalize (object);
}

static void
mm_bearer_class_init (MMBearerClass *bearer_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (bearer_class);

    g_type_class_add_private (object_class, sizeof (MMBearerPrivate));

    object_class->finalize = finalize;
}
