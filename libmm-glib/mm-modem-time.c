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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-time.h"

/**
 * SECTION: mm-modem-time
 * @title: MMModemTime
 * @short_description: The Time interface
 *
 * The #MMModemTime is an object providing access to the methods, signals and
 * properties of the Time interface.
 *
 * The Time interface is exposed on modems which support network time retrieval.
 */

G_DEFINE_TYPE (MMModemTime, mm_modem_time, MM_GDBUS_TYPE_MODEM_TIME_PROXY)

struct _MMModemTimePrivate {
    /* Timezone */
    GMutex timezone_mutex;
    guint timezone_id;
    MMNetworkTimezone *timezone;
};

/*****************************************************************************/

/**
 * mm_modem_time_get_path:
 * @self: A #MMModemTime.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_time_get_path (MMModemTime *self)
{
    g_return_val_if_fail (MM_IS_MODEM_TIME (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_time_dup_path:
 * @self: A #MMModemTime.
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
mm_modem_time_dup_path (MMModemTime *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_TIME (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_time_get_network_time_finish:
 * @self: A #MMModemTime.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_enable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_time_get_network_time().
 *
 * Returns: (transfer full): A string containing the network time, or %NULL if
 * @error is set. The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_time_get_network_time_finish (MMModemTime *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    gchar *network_time = NULL;

    g_return_val_if_fail (MM_IS_MODEM_TIME (self), NULL);

    if (!mm_gdbus_modem_time_call_get_network_time_finish (MM_GDBUS_MODEM_TIME (self), &network_time, res, error))
        return NULL;

    return network_time;
}

/**
 * mm_modem_time_get_network_time:
 * @self: A #MMModemTime.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests the current network time.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_time_get_network_time_finish() to get the result of the operation.
 *
 * See mm_modem_time_get_network_time_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.0
 */
void
mm_modem_time_get_network_time (MMModemTime *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_TIME (self));

    mm_gdbus_modem_time_call_get_network_time (MM_GDBUS_MODEM_TIME (self),
                                               cancellable,
                                               callback,
                                               user_data);
}

/**
 * mm_modem_time_get_network_time_sync:
 * @self: A #MMModemTime.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests the current network time.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_time_get_network_time() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A string containing the network time, or %NULL if
 * @error is set. The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_time_get_network_time_sync (MMModemTime *self,
                                     GCancellable *cancellable,
                                     GError **error)
{
    gchar *network_time = NULL;

    g_return_val_if_fail (MM_IS_MODEM_TIME (self), NULL);

    if (!mm_gdbus_modem_time_call_get_network_time_sync (MM_GDBUS_MODEM_TIME (self), &network_time, cancellable, error))
        return NULL;

    return network_time;
}

/*****************************************************************************/

static void
timezone_updated (MMModemTime *self,
                  GParamSpec *pspec)
{
    g_mutex_lock (&self->priv->timezone_mutex);
    {
        GVariant *dictionary;

        g_clear_object (&self->priv->timezone);
        dictionary = mm_gdbus_modem_time_get_network_timezone (MM_GDBUS_MODEM_TIME (self));
        self->priv->timezone = (dictionary ?
                                mm_network_timezone_new_from_dictionary (dictionary, NULL) :
                                NULL);
    }
    g_mutex_unlock (&self->priv->timezone_mutex);
}

static void
ensure_internal_timezone (MMModemTime *self,
                          MMNetworkTimezone **dup)
{
    g_mutex_lock (&self->priv->timezone_mutex);
    {
        /* If this is the first time ever asking for the object, setup the
         * update listener and the initial object, if any. */
        if (!self->priv->timezone_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_modem_time_dup_network_timezone (MM_GDBUS_MODEM_TIME (self));
            if (dictionary) {
                self->priv->timezone = mm_network_timezone_new_from_dictionary (dictionary, NULL);
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->timezone_id =
                g_signal_connect (self,
                                  "notify::network-timezone",
                                  G_CALLBACK (timezone_updated),
                                  NULL);
        }

        if (dup && self->priv->timezone)
            *dup = g_object_ref (self->priv->timezone);
    }
    g_mutex_unlock (&self->priv->timezone_mutex);
}

/**
 * mm_modem_time_get_network_timezone:
 * @self: A #MMModemTime.
 *
 * Gets the network timezone information.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_time_get_network_timezone() again to get a new #MMNetworkTimezone
 * with the new values.</warning>
 *
 * Returns: (transfer full): A #MMNetworkTimezone that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.0
 */
MMNetworkTimezone *
mm_modem_time_get_network_timezone (MMModemTime *self)
{
    MMNetworkTimezone *tz = NULL;

    g_return_val_if_fail (MM_IS_MODEM_TIME (self), NULL);

    ensure_internal_timezone (self, &tz);
    return tz;
}

/**
 * mm_modem_time_peek_network_timezone:
 * @self: A #MMModemTime.
 *
 * Gets the network timezone information.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_time_get_network_timezone() if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMNetworkTimezone. Do not free the returned
 * value, it belongs to @self.
 *
 * Since: 1.0
 */
MMNetworkTimezone *
mm_modem_time_peek_network_timezone (MMModemTime *self)
{
    g_return_val_if_fail (MM_IS_MODEM_TIME (self), NULL);

    ensure_internal_timezone (self, NULL);
    return self->priv->timezone;
}

/*****************************************************************************/

static void
mm_modem_time_init (MMModemTime *self)
{
    /* Setup private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_MODEM_TIME,
                                              MMModemTimePrivate);
    g_mutex_init (&self->priv->timezone_mutex);
}

static void
dispose (GObject *object)
{
    MMModemTime *self = MM_MODEM_TIME (object);

    g_clear_object (&self->priv->timezone);

    G_OBJECT_CLASS (mm_modem_time_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    MMModemTime *self = MM_MODEM_TIME (object);

    g_mutex_clear (&self->priv->timezone_mutex);

    G_OBJECT_CLASS (mm_modem_time_parent_class)->finalize (object);
}

static void
mm_modem_time_class_init (MMModemTimeClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModemTimePrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
    object_class->dispose = dispose;
}
