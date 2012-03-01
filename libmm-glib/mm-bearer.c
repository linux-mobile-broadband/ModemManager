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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include "mm-helpers.h"
#include "mm-bearer.h"
#include "mm-modem.h"

/**
 * mm_bearer_get_path:
 * @self: A #MMBearer.
 *
 * Gets the DBus path of the #MMBearer object.
 *
 * Returns: (transfer none): The DBus path of the #MMBearer object.
 */
const gchar *
mm_bearer_get_path (MMBearer *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_bearer_dup_path:
 * @self: A #MMBearer.
 *
 * Gets a copy of the DBus path of the #MMBearer object.
 *
 * Returns: (transfer full): The DBus path of the #MMBearer object. The returned value should be freed with g_free().
 */
gchar *
mm_bearer_dup_path (MMBearer *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/**
 * mm_bearer_get_interface:
 * @self: A #MMBearer.
 *
 * Gets the operating system name for the network data interface that provides
 * packet data using this #MMBearer. This will only be available once the #MMBearer
 * is in connected state.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_bearer_dup_interface() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the interface, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_bearer_get_interface (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_bearer_get_interface (self));
}

/**
 * mm_bearer_dup_interface:
 * @self: A #MMBearer.
 *
 * Gets a copy of the operating system name for the network data interface that provides
 * packet data using this #MMBearer. This will only be available once the #MMBearer
 * is in connected state.
 *
 * Returns: (transfer full): The name of the interface, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_bearer_dup_interface (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_bearer_dup_interface (self));
}

/**
 * mm_bearer_get_connected:
 * @self: A #MMBearer.
 *
 * Checks whether or not the #MMBearer is connected and thus whether packet data
 * communication is possible.
 *
 * Returns: %TRUE if the #MMBearer is connected, #FALSE otherwise.
 */
gboolean
mm_bearer_get_connected (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_get_connected (self);
}

/**
 * mm_bearer_get_suspended:
 * @self: A #MMBearer.
 *
 * Checks whether or not the #MMBearer is suspended (but not deactivated) while the
 * device is handling other communications, like a voice call.
 *
 * Returns: %TRUE if packet data service is suspended in the #MMBearer, #FALSE otherwise.
 */
gboolean
mm_bearer_get_suspended (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_get_suspended (self);
}

guint
mm_bearer_get_ip_timeout (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), 0);

    return mm_gdbus_bearer_get_ip_timeout (self);
}

MMBearerIpConfig *
mm_bearer_get_ipv4_config (MMBearer *self)
{
    MMBearerIpConfig *config;
    GVariant *variant;
    GError *error = NULL;

    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    variant = mm_gdbus_bearer_dup_ip4_config (MM_GDBUS_BEARER (self));
    config = mm_bearer_ip_config_new_from_dictionary (variant, &error);
    if (!config) {
        g_warning ("Couldn't create IP config: '%s'", error->message);
        g_error_free (error);
    }
    g_variant_unref (variant);

    return config;
}

MMBearerIpConfig *
mm_bearer_get_ipv6_config (MMBearer *self)
{
    MMBearerIpConfig *config;
    GVariant *variant;
    GError *error = NULL;

    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    variant = mm_gdbus_bearer_dup_ip6_config (MM_GDBUS_BEARER (self));
    config = mm_bearer_ip_config_new_from_dictionary (variant, &error);
    if (!config) {
        g_warning ("Couldn't create IP config: '%s'", error->message);
        g_error_free (error);
    }
    g_variant_unref (variant);

    return config;
}

MMBearerProperties *
mm_bearer_get_properties (MMBearer *self)
{
    GError *error = NULL;
    MMBearerProperties *properties;

    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    properties = (mm_bearer_properties_new_from_dictionary (
                      mm_gdbus_bearer_get_properties (MM_GDBUS_BEARER (self)),
                      &error));
    if (!properties) {
        g_warning ("Couldn't create properties: '%s'", error->message);
        g_error_free (error);
    }

    return properties;
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
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_bearer_connect_finish() to get the result of the operation.
 *
 * See mm_bearer_connect_sync() for the synchronous, blocking version of this method.
 */
void
mm_bearer_connect (MMBearer *self,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_BEARER (self));

    mm_gdbus_bearer_call_connect (self,
                                  cancellable,
                                  callback,
                                  user_data);
}

/**
 * mm_bearer_connect_finish:
 * @self: A #MMBearer.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_bearer_connect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_bearer_connect().
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_bearer_connect_finish (MMBearer *self,
                          GAsyncResult *res,
                          GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_connect_finish (self, res, error);
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
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_bearer_connect_sync (MMBearer *self,
                        GCancellable *cancellable,
                        GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_connect_sync (self,
                                              cancellable,
                                              error);
}

/**
 * mm_bearer_disconnect:
 * @self: A #MMBearer.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Synchronously requests disconnection and deactivation of the packet data connection.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_bearer_disconnect_finish() to get the result of the operation.
 *
 * See mm_bearer_disconnect_sync() for the synchronous, blocking version of this method.
 */
void
mm_bearer_disconnect (MMBearer *self,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_BEARER (self));

    mm_gdbus_bearer_call_disconnect (self,
                                     cancellable,
                                     callback,
                                     user_data);
}

/**
 * mm_bearer_disconnect_finish:
 * @self: A #MMBearer.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_bearer_disconnect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_bearer_disconnect().
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_bearer_disconnect_finish (MMBearer *self,
                             GAsyncResult *res,
                             GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_disconnect_finish (self, res, error);
}

/**
 * mm_bearer_disconnect_sync:
 * @self: A #MMBearer.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests disconnection and deactivation of the packet data connection.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_bearer_disconnect() for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_bearer_disconnect_sync (MMBearer *self,
                           GCancellable *cancellable,
                           GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_disconnect_sync (self,
                                                 cancellable,
                                                 error);
}
