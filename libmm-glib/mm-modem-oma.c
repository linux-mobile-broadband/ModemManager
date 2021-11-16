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
 * Copyright (C) 2013 Google, Inc.
 */

#include <gio/gio.h>
#include <string.h>

#include "mm-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-oma.h"
#include "mm-common-helpers.h"

/**
 * SECTION: mm-modem-oma
 * @title: MMModemOma
 * @short_description: The OMA interface
 *
 * The #MMModemOma is an object providing access to the methods, signals and
 * properties of the OMA interface.
 *
 * The OMA interface is exposed whenever a modem has OMA device management
 * capabilities.
 */

G_DEFINE_TYPE (MMModemOma, mm_modem_oma, MM_GDBUS_TYPE_MODEM_OMA_PROXY)

struct _MMModemOmaPrivate {
    /* Common mutex to sync access */
    GMutex mutex;

    PROPERTY_ARRAY_DECLARE (pending_network_initiated_sessions)
};

/*****************************************************************************/

/**
 * mm_modem_oma_get_path:
 * @self: A #MMModemOma.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.2
 */
const gchar *
mm_modem_oma_get_path (MMModemOma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_oma_dup_path:
 * @self: A #MMModemOma.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.2
 */
gchar *
mm_modem_oma_dup_path (MMModemOma *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_OMA (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_oma_setup_finish:
 * @self: A #MMModemOma.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_oma_setup().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_oma_setup().
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_oma_setup_finish (MMModemOma *self,
                           GAsyncResult *res,
                           GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), FALSE);

    return mm_gdbus_modem_oma_call_setup_finish (MM_GDBUS_MODEM_OMA (self), res, error);
}

/**
 * mm_modem_oma_setup:
 * @self: A #MMModemOma.
 * @features: Mask of #MMOmaFeature values to enable.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sets up the OMA device management service.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_oma_setup_finish() to get the result of the operation.
 *
 * See mm_modem_oma_setup_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.2
 */
void
mm_modem_oma_setup (MMModemOma *self,
                    MMOmaFeature features,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_OMA (self));

    mm_gdbus_modem_oma_call_setup (MM_GDBUS_MODEM_OMA (self), features, cancellable, callback, user_data);
}

/**
 * mm_modem_oma_setup_sync:
 * @self: A #MMModemOma.
 * @features: Mask of #MMOmaFeature values to enable.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sets up the OMA device management service.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_oma_setup() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the setup was successful, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_oma_setup_sync (MMModemOma *self,
                         MMOmaFeature features,
                         GCancellable *cancellable,
                         GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), FALSE);

    return mm_gdbus_modem_oma_call_setup_sync (MM_GDBUS_MODEM_OMA (self), features, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_oma_start_client_initiated_session_finish:
 * @self: A #MMModemOma.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_oma_start_client_initiated_session().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with
 * mm_modem_oma_start_client_initiated_session().
 *
 * Returns: %TRUE if the session was started, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_oma_start_client_initiated_session_finish (MMModemOma *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), FALSE);

    return mm_gdbus_modem_oma_call_start_client_initiated_session_finish (MM_GDBUS_MODEM_OMA (self), res, error);
}

/**
 * mm_modem_oma_start_client_initiated_session:
 * @self: A #MMModemOma.
 * @session_type: A #MMOmaSessionType.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously starts a client-initiated OMA device management session.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_oma_start_client_initiated_session_finish() to get the result of the
 * operation.
 *
 * See mm_modem_oma_start_client_initiated_session_sync() for the synchronous,
 * blocking version of this method.
 *
 * Since: 1.2
 */
void
mm_modem_oma_start_client_initiated_session (MMModemOma *self,
                                             MMOmaSessionType session_type,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_OMA (self));

    mm_gdbus_modem_oma_call_start_client_initiated_session (MM_GDBUS_MODEM_OMA (self), session_type, cancellable, callback, user_data);
}

/**
 * mm_modem_oma_start_client_initiated_session_sync:
 * @self: A #MMModemOma.
 * @session_type: A #MMOmaSessionType.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously starts a client-initiated OMA device management session.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_oma_start_client_initiated_session() for the asynchronous version
 * of this method.
 *
 * Returns: %TRUE if the session was started, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_oma_start_client_initiated_session_sync (MMModemOma *self,
                                                  MMOmaSessionType session_type,
                                                  GCancellable *cancellable,
                                                  GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), FALSE);

    return mm_gdbus_modem_oma_call_start_client_initiated_session_sync (MM_GDBUS_MODEM_OMA (self), session_type, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_oma_accept_network_initiated_session_finish:
 * @self: A #MMModemOma.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_oma_accept_network_initiated_session().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with
 * mm_modem_oma_accept_network_initiated_session().
 *
 * Returns: %TRUE if the session was started, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_oma_accept_network_initiated_session_finish (MMModemOma *self,
                                                      GAsyncResult *res,
                                                      GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), FALSE);

    return mm_gdbus_modem_oma_call_accept_network_initiated_session_finish (MM_GDBUS_MODEM_OMA (self), res, error);
}

/**
 * mm_modem_oma_accept_network_initiated_session:
 * @self: A #MMModemOma.
 * @session_id: The unique ID of the network-initiated session.
 * @accept: %TRUE if the session is to be accepted, %FALSE otherwise.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously accepts a nework-initiated OMA device management session.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_oma_accept_network_initiated_session_finish() to get the result of
 * the operation.
 *
 * See mm_modem_oma_accept_network_initiated_session_sync() for the synchronous,
 * blocking version of this method.
 *
 * Since: 1.2
 */
void
mm_modem_oma_accept_network_initiated_session (MMModemOma *self,
                                               guint session_id,
                                               gboolean accept,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_OMA (self));

    mm_gdbus_modem_oma_call_accept_network_initiated_session (MM_GDBUS_MODEM_OMA (self), session_id, accept, cancellable, callback, user_data);
}

/**
 * mm_modem_oma_accept_network_initiated_session_sync:
 * @self: A #MMModemOma.
 * @session_id: The unique ID of the network-initiated session.
 * @accept: %TRUE if the session is to be accepted, %FALSE otherwise.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously accepts a nework-initiated OMA device management session.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_oma_accept_network_initiated_session() for the asynchronous version
 * of this method.
 *
 * Returns: %TRUE if the session was started, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_oma_accept_network_initiated_session_sync (MMModemOma *self,
                                                    guint session_id,
                                                    gboolean accept,
                                                    GCancellable *cancellable,
                                                    GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), FALSE);

    return mm_gdbus_modem_oma_call_accept_network_initiated_session_sync (MM_GDBUS_MODEM_OMA (self), session_id, accept, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_oma_cancel_session_finish:
 * @self: A #MMModemOma.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_oma_cancel_session().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_oma_cancel_session().
 *
 * Returns: %TRUE if the session was started, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_oma_cancel_session_finish (MMModemOma *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), FALSE);

    return mm_gdbus_modem_oma_call_cancel_session_finish (MM_GDBUS_MODEM_OMA (self), res, error);
}

/**
 * mm_modem_oma_cancel_session:
 * @self: A #MMModemOma.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously cancels the current OMA device management session.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_oma_cancel_session_finish() to get the result of the operation.
 *
 * See mm_modem_oma_cancel_session_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.2
 */
void
mm_modem_oma_cancel_session (MMModemOma *self,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_OMA (self));

    mm_gdbus_modem_oma_call_cancel_session (MM_GDBUS_MODEM_OMA (self), cancellable, callback, user_data);
}

/**
 * mm_modem_oma_cancel_session_sync:
 * @self: A #MMModemOma.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously cancels the current OMA device management session.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_oma_cancel_session() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the session was started, %FALSE if @error is set.
 *
 * Since: 1.2
 */
gboolean
mm_modem_oma_cancel_session_sync (MMModemOma *self,
                                  GCancellable *cancellable,
                                  GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), FALSE);

    return mm_gdbus_modem_oma_call_cancel_session_sync (MM_GDBUS_MODEM_OMA (self), cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_oma_get_features:
 * @self: A #MMModemOma.
 *
 * Gets the currently enabled OMA features.
 *
 * Returns: a bitmask of #MMOmaFeature values.
 *
 * Since: 1.2
 */
MMOmaFeature
mm_modem_oma_get_features  (MMModemOma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), MM_OMA_FEATURE_NONE);

    return (MMOmaFeature) mm_gdbus_modem_oma_get_features (MM_GDBUS_MODEM_OMA (self));
}

/*****************************************************************************/

/**
 * mm_modem_oma_get_session_type:
 * @self: A #MMModemOma.
 *
 * Gets the type of the current OMA device management session.
 *
 * Returns: a #MMOmaSessionType.
 *
 * Since: 1.2
 */
MMOmaSessionType
mm_modem_oma_get_session_type  (MMModemOma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), MM_OMA_SESSION_TYPE_UNKNOWN);

    return (MMOmaSessionType) mm_gdbus_modem_oma_get_session_type (MM_GDBUS_MODEM_OMA (self));
}

/*****************************************************************************/

/**
 * mm_modem_oma_get_session_state:
 * @self: A #MMModemOma.
 *
 * Gets the state of the current OMA device management session.
 *
 * Returns: a #MMOmaSessionState.
 *
 * Since: 1.2
 */
MMOmaSessionState
mm_modem_oma_get_session_state (MMModemOma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_OMA (self), MM_OMA_SESSION_STATE_UNKNOWN);

    return (MMOmaSessionState) mm_gdbus_modem_oma_get_session_state (MM_GDBUS_MODEM_OMA (self));
}

/*****************************************************************************/

/**
 * mm_modem_oma_get_pending_network_initiated_sessions:
 * @self: A #MMModem.
 * @sessions: (out) (array length=n_sessions): Return location for the array of
 *  #MMOmaPendingNetworkInitiatedSession structs. The returned array should be
 *  freed with g_free() when no longer needed.
 * @n_sessions: (out): Return location for the number of values in @sessions.
 *
 * Gets the list of pending network-initiated OMA sessions.
 *
 * Returns: %TRUE if @sessions and @n_sessions are set, %FALSE otherwise.
 *
 * Since: 1.18
 */


/**
 * mm_modem_oma_peek_pending_network_initiated_sessions:
 * @self: A #MMModem.
 * @sessions: (out) (array length=n_sessions): Return location for the array of
 *  #MMOmaPendingNetworkInitiatedSession values. Do not free the returned array,
 *  it is owned by @self.
 * @n_sessions: (out): Return location for the number of values in @sessions.
 *
 * Gets the list of pending network-initiated OMA sessions.
 *
 * Returns: %TRUE if @sessions and @n_sessions are set, %FALSE otherwise.
 *
 * Since: 1.18
 */

PROPERTY_ARRAY_DEFINE (pending_network_initiated_sessions,
                       ModemOma, modem_oma, MODEM_OMA,
                       MMOmaPendingNetworkInitiatedSession,
                       mm_common_oma_pending_network_initiated_sessions_variant_to_garray)

/*****************************************************************************/

static void
mm_modem_oma_init (MMModemOma *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_MODEM_OMA, MMModemOmaPrivate);
    g_mutex_init (&self->priv->mutex);

    PROPERTY_INITIALIZE (pending_network_initiated_sessions, "pending-network-initiated-sessions")
}

static void
finalize (GObject *object)
{
    MMModemOma *self = MM_MODEM_OMA (object);

    g_mutex_clear (&self->priv->mutex);

    PROPERTY_ARRAY_FINALIZE (pending_network_initiated_sessions)

    G_OBJECT_CLASS (mm_modem_oma_parent_class)->finalize (object);
}

static void
mm_modem_oma_class_init (MMModemOmaClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModemOmaPrivate));

    object_class->finalize = finalize;
}
