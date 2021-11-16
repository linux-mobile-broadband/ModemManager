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

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-simple.h"

/**
 * SECTION: mm-modem-simple
 * @title: MMModemSimple
 * @short_description: The Simple interface
 *
 * The #MMModemSimple is an object providing access to the methods, signals and
 * properties of the Simple interface.
 *
 * The Simple interface is exposed on modems which are not in
 * %MM_MODEM_STATE_FAILED state.
 */

G_DEFINE_TYPE (MMModemSimple, mm_modem_simple, MM_GDBUS_TYPE_MODEM_SIMPLE_PROXY)

/*****************************************************************************/

/**
 * mm_modem_simple_get_path:
 * @self: A #MMModemSimple.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_simple_get_path (MMModemSimple *self)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_simple_dup_path:
 * @self: A #MMModemSimple.
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
mm_modem_simple_dup_path (MMModemSimple *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_SIMPLE (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_simple_connect_finish:
 * @self: A #MMModemSimple.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_simple_connect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_simple_connect().
 *
 * Returns: (transfer full): A #MMBearer, or %FALSE if @error is set. The
 * returned value must be freed with g_object_unref().
 *
 * Since: 1.0
 */
MMBearer *
mm_modem_simple_connect_finish (MMModemSimple *self,
                                GAsyncResult *res,
                                GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE (self), NULL);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
new_bearer_ready (GDBusConnection *connection,
                  GAsyncResult *res,
                  GTask *task)
{
    GError *error = NULL;
    GObject *bearer;
    GObject *source_object;

    source_object = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);

    g_object_unref (task);
}

static void
simple_connect_ready (MMModemSimple *self,
                      GAsyncResult *res,
                      GTask *task)
{
    GError *error = NULL;
    gchar *bearer_path = NULL;

    if (!mm_gdbus_modem_simple_call_connect_finish (MM_GDBUS_MODEM_SIMPLE (self),
                                                    &bearer_path,
                                                    res,
                                                    &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    g_async_initable_new_async (MM_TYPE_BEARER,
                                G_PRIORITY_DEFAULT,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback)new_bearer_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    bearer_path,
                                "g-interface-name", "org.freedesktop.ModemManager1.Bearer",
                                NULL);
    g_free (bearer_path);
}

/**
 * mm_modem_simple_connect:
 * @self: A #MMModemSimple.
 * @properties: (transfer none): A #MMSimpleConnectProperties bundle.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to connect the modem using the given @properties.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_simple_connect_finish() to get the result of the operation.
 *
 * See mm_modem_simple_connect_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_modem_simple_connect (MMModemSimple *self,
                         MMSimpleConnectProperties *properties,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GTask *task;
    GVariant *variant;

    g_return_if_fail (MM_IS_MODEM_SIMPLE (self));

    task = g_task_new (self, cancellable, callback, user_data);

    variant = mm_simple_connect_properties_get_dictionary (properties);
    mm_gdbus_modem_simple_call_connect (
        MM_GDBUS_MODEM_SIMPLE (self),
        variant,
        cancellable,
        (GAsyncReadyCallback)simple_connect_ready,
        task);

    g_variant_unref (variant);
}

/**
 * mm_modem_simple_connect_sync:
 * @self: A #MMModemSimple.
 * @properties: (transfer none): A #MMSimpleConnectProperties bundle.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to connect the modem using the given @properties.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_simple_connect() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A #MMBearer, or %FALSE if @error is set. The
 * returned value must be freed with g_object_unref().
 *
 * Since: 1.0
 */
MMBearer *
mm_modem_simple_connect_sync (MMModemSimple *self,
                              MMSimpleConnectProperties *properties,
                              GCancellable *cancellable,
                              GError **error)
{
    GObject *bearer = NULL;
    gchar *bearer_path = NULL;
    GVariant *variant;

    g_return_val_if_fail (MM_IS_MODEM_SIMPLE (self), NULL);

    variant = mm_simple_connect_properties_get_dictionary (properties);
    mm_gdbus_modem_simple_call_connect_sync (MM_GDBUS_MODEM_SIMPLE (self),
                                             variant,
                                             &bearer_path,
                                             cancellable,
                                             error);
    if (bearer_path) {
        bearer = g_initable_new (MM_TYPE_BEARER,
                                 cancellable,
                                 error,
                                 "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                 "g-name",           MM_DBUS_SERVICE,
                                 "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                 "g-object-path",    bearer_path,
                                 "g-interface-name", "org.freedesktop.ModemManager1.Bearer",
                                 NULL);
        g_free (bearer_path);
    }

    g_variant_unref (variant);

    return (bearer ? MM_BEARER (bearer) : NULL);
}

/*****************************************************************************/

/**
 * mm_modem_simple_disconnect_finish:
 * @self: A #MMModemSimple.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_simple_disconnect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_simple_disconnect().
 *
 * Returns: %TRUE if the modem is successfully disconnected, %FALSE if @error is
 * set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_simple_disconnect_finish (MMModemSimple *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE (self), FALSE);

    return mm_gdbus_modem_simple_call_disconnect_finish (MM_GDBUS_MODEM_SIMPLE (self), res, error);
}

/**
 * mm_modem_simple_disconnect:
 * @self: A #MMModemSimple.
 * @bearer: (allow-none): Path of the bearer to disconnect, or %NULL to
 *  disconnect all connected bearers.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to disconnect the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_simple_disconnect_finish() to get the result of the operation.
 *
 * See mm_modem_simple_disconnect_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
 */
void
mm_modem_simple_disconnect (MMModemSimple *self,
                            const gchar *bearer,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE (self));

    mm_gdbus_modem_simple_call_disconnect (MM_GDBUS_MODEM_SIMPLE (self),
                                           bearer ? bearer : "/",
                                           cancellable,
                                           callback,
                                           user_data);
}

/**
 * mm_modem_simple_disconnect_sync:
 * @self: A #MMModemSimple.
 * @bearer: (allow-none): Path of the bearer to disconnect, or %NULL to
 *  disconnect all connected bearers.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to disconnect the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_simple_disconnect() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the modem is successfully disconnected, %FALSE if @error is
 * set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_simple_disconnect_sync (MMModemSimple *self,
                                 const gchar *bearer,
                                 GCancellable *cancellable,
                                 GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_SIMPLE (self), FALSE);

    return mm_gdbus_modem_simple_call_disconnect_sync (MM_GDBUS_MODEM_SIMPLE (self),
                                                       bearer ? bearer : "/",
                                                       cancellable,
                                                       error);
}

/*****************************************************************************/

/**
 * mm_modem_simple_get_status_finish:
 * @self: A #MMModemSimple.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_simple_connect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_simple_get_status().
 *
 * Returns: (transfer full): A #MMSimpleStatus, or %FALSE if @error is set. The
 * returned value must be freed with g_object_unref().
 *
 * Since: 1.0
 */
MMSimpleStatus *
mm_modem_simple_get_status_finish (MMModemSimple *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    MMSimpleStatus *status;
    GVariant *dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIMPLE (self), NULL);

    if (!mm_gdbus_modem_simple_call_get_status_finish (MM_GDBUS_MODEM_SIMPLE (self), &dictionary, res, error))
        return NULL;

    status = mm_simple_status_new_from_dictionary (dictionary, error);
    g_variant_unref (dictionary);
    return status;
}

/**
 * mm_modem_simple_get_status:
 * @self: A #MMModemSimple.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests a compilation of the status of the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_simple_get_status_finish() to get the result of the operation.
 *
 * See mm_modem_simple_get_status_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
 */
void
mm_modem_simple_get_status (MMModemSimple *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE (self));

    mm_gdbus_modem_simple_call_get_status (MM_GDBUS_MODEM_SIMPLE (self),
                                           cancellable,
                                           callback,
                                           user_data);
}

/**
 * mm_modem_simple_get_status_sync:
 * @self: A #MMModemSimple.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests a compilation of the status of the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_simple_get_status() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A #MMSimpleStatus, or %FALSE if @error is set. The
 * returned value must be freed with g_object_unref().
 *
 * Since: 1.0
 */
MMSimpleStatus *
mm_modem_simple_get_status_sync (MMModemSimple *self,
                                 GCancellable *cancellable,
                                 GError **error)
{
    MMSimpleStatus *status;
    GVariant *dictionary = NULL;

    g_return_val_if_fail (MM_IS_MODEM_SIMPLE (self), NULL);

    if (!mm_gdbus_modem_simple_call_get_status_sync (MM_GDBUS_MODEM_SIMPLE (self), &dictionary, cancellable, error))
        return NULL;

    status = mm_simple_status_new_from_dictionary (dictionary, error);
    g_variant_unref (dictionary);
    return status;
}

/*****************************************************************************/

static void
mm_modem_simple_init (MMModemSimple *self)
{
}

static void
mm_modem_simple_class_init (MMModemSimpleClass *modem_class)
{
}
