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
#include "mm-modem-3gpp-ussd.h"

/**
 * SECTION: mm-modem-3gpp-ussd
 * @title: MMModem3gppUssd
 * @short_description: The 3GPP USSD interface
 *
 * The #MMModem3gppUssd is an object providing access to the methods, signals and
 * properties of the 3GPP USSD interface.
 *
 * This interface is only exposed when the 3GPP modem is known to handle USSD operations.
 */

G_DEFINE_TYPE (MMModem3gppUssd, mm_modem_3gpp_ussd, MM_GDBUS_TYPE_MODEM3GPP_USSD_PROXY)

/*****************************************************************************/

/**
 * mm_modem_3gpp_ussd_get_path:
 * @self: A #MMModem3gppUssd.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_3gpp_ussd_get_path (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_3gpp_ussd_dup_path:
 * @self: A #MMModem3gppUssd.
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
mm_modem_3gpp_ussd_dup_path (MMModem3gppUssd *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_ussd_get_network_request:
 * @self: A #MMModem3gppUssd.
 *
 * Gets any pending network-initiated request.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_3gpp_ussd_dup_network_request() if on
 * another thread.</warning>
 *
 * Returns: (transfer none): The network request, or %NULL if none available.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_3gpp_ussd_get_network_request (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem3gpp_ussd_get_network_request (MM_GDBUS_MODEM3GPP_USSD (self)));
}

/**
 * mm_modem_3gpp_ussd_dup_network_request:
 * @self: A #MMModem3gppUssd.
 *
 * Gets a copy of any pending network-initiated request.
 *
 * Returns: (transfer full): The network request, or %NULL if none available.
 * The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_3gpp_ussd_dup_network_request (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_ussd_dup_network_request (MM_GDBUS_MODEM3GPP_USSD (self)));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_ussd_get_network_notification:
 * @self: A #MMModem3gppUssd.
 *
 * Gets any pending network-initiated request to which no USSD response is
 * required.
 *
 * <warning>The returned value is only valid until the property changes so it is
 * only safe to use this function on the thread where @self was constructed. Use
 * mm_modem_3gpp_ussd_dup_network_notification() if on another thread.</warning>
 *
 * Returns: (transfer none): The network notification, or %NULL if none
 * available.
 *
 * Since: 1.0
 */
const gchar *
mm_modem_3gpp_ussd_get_network_notification (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem3gpp_ussd_get_network_notification (MM_GDBUS_MODEM3GPP_USSD (self)));
}

/**
 * mm_modem_3gpp_ussd_dup_network_notification:
 * @self: A #MMModem3gppUssd.
 *
 * Gets a copy of any pending network-initiated request to which no USSD
 * response is required.
 *
 * Returns: (transfer full): The network notification, or %NULL if none
 * available. The returned value should be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_3gpp_ussd_dup_network_notification (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_ussd_dup_network_notification (MM_GDBUS_MODEM3GPP_USSD (self)));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_ussd_get_state:
 * @self: A #MMModem.
 *
 * Get the state of the ongoing USSD session, if any.
 *
 * Returns: A #MMModem3gppUssdSessionState value, specifying the current state.
 *
 * Since: 1.0
 */
MMModem3gppUssdSessionState
mm_modem_3gpp_ussd_get_state (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN);

    return mm_gdbus_modem3gpp_ussd_get_state (MM_GDBUS_MODEM3GPP_USSD (self));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_ussd_initiate_finish:
 * @self: A #MMModem3gppUssd.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_ussd_initiate().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_ussd_initiate().
 *
 * Returns: The response from the network, if any. The returned value should be
 * freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_3gpp_ussd_initiate_finish (MMModem3gppUssd *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    gchar *reply = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    mm_gdbus_modem3gpp_ussd_call_initiate_finish (MM_GDBUS_MODEM3GPP_USSD (self), &reply, res, error);

    return reply;
}

/**
 * mm_modem_3gpp_ussd_initiate:
 * @self: A #MMModem3gppUssd.
 * @command: The command to start the USSD session with.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously sends a USSD command string to the network initiating a USSD
 * session.
 *
 * When the request is handled by the network, the method returns the
 * response or an appropriate error. The network may be awaiting further
 * response from the ME after returning from this method and no new command.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_ussd_initiate_finish() to get the result of the operation.
 *
 * See mm_modem_3gpp_ussd_initiate_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
 */
void
mm_modem_3gpp_ussd_initiate (MMModem3gppUssd *self,
                             const gchar *command,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_3GPP_USSD (self));

    mm_gdbus_modem3gpp_ussd_call_initiate (MM_GDBUS_MODEM3GPP_USSD (self), command, cancellable, callback, user_data);
}

/**
 * mm_modem_3gpp_ussd_initiate_sync:
 * @self: A #MMModem3gppUssd.
 * @command: The command to start the USSD session with.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously sends a USSD command string to the network initiating a USSD
 * session.
 *
 * When the request is handled by the network, the method returns the
 * response or an appropriate error. The network may be awaiting further
 * response from the ME after returning from this method and no new command.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_ussd_initiate() for the asynchronous version of this method.
 *
 * Returns: The response from the network, if any. The returned value should be
 * freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_3gpp_ussd_initiate_sync (MMModem3gppUssd *self,
                                  const gchar *command,
                                  GCancellable *cancellable,
                                  GError **error)
{
    gchar *reply = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    mm_gdbus_modem3gpp_ussd_call_initiate_sync (MM_GDBUS_MODEM3GPP_USSD (self), command, &reply, cancellable, error);

    return reply;
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_ussd_respond_finish:
 * @self: A #MMModem3gppUssd.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_ussd_respond().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_ussd_respond().
 *
 * Returns: The network reply to this response to the network-initiated USSD
 * command. The reply may require further responses. The returned value should
 * be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_3gpp_ussd_respond_finish (MMModem3gppUssd *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    gchar *reply = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    mm_gdbus_modem3gpp_ussd_call_respond_finish (MM_GDBUS_MODEM3GPP_USSD (self), &reply, res, error);

    return reply;
}

/**
 * mm_modem_3gpp_ussd_respond:
 * @self: A #MMModem3gppUssd.
 * @response: The response to network-initiated USSD command, or a response to a
 *  request for further input.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously responds to a USSD request that is either initiated by the
 * mobile network, or that is awaiting further input after a previous call to
 * mm_modem_3gpp_ussd_initiate().
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_ussd_respond_finish() to get the result of the operation.
 *
 * See mm_modem_3gpp_ussd_respond_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
 */
void
mm_modem_3gpp_ussd_respond (MMModem3gppUssd *self,
                            const gchar *response,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_3GPP_USSD (self));

    mm_gdbus_modem3gpp_ussd_call_respond (MM_GDBUS_MODEM3GPP_USSD (self), response, cancellable, callback, user_data);
}

/**
 * mm_modem_3gpp_ussd_respond_sync:
 * @self: A #MMModem3gppUssd.
 * @response: The response to network-initiated USSD command, or a response to a
 *  request for further input.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously responds to a USSD request that is either initiated by the
 * mobile network, or that is awaiting further input after a previous call to
 * mm_modem_3gpp_ussd_initiate().
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_ussd_respond() for the asynchronous version of this method.
 *
 * Returns: The network reply to this response to the network-initiated USSD
 * command. The reply may require further responses. The returned value should
 * be freed with g_free().
 *
 * Since: 1.0
 */
gchar *
mm_modem_3gpp_ussd_respond_sync (MMModem3gppUssd *self,
                                 const gchar *response,
                                 GCancellable *cancellable,
                                 GError **error)
{
    gchar *reply = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), NULL);

    mm_gdbus_modem3gpp_ussd_call_respond_sync (MM_GDBUS_MODEM3GPP_USSD (self), response, &reply, cancellable, error);

    return reply;
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_ussd_cancel_finish:
 * @self: A #MMModem3gppUssd.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_ussd_cancel().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_ussd_cancel().
 *
 * Returns: %TRUE if the session was successfully cancelled, %FALSE if @error is
 * set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_3gpp_ussd_cancel_finish (MMModem3gppUssd *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), FALSE);

    return mm_gdbus_modem3gpp_ussd_call_cancel_finish (MM_GDBUS_MODEM3GPP_USSD (self), res, error);
}

/**
 * mm_modem_3gpp_ussd_cancel:
 * @self: A #MMModem3gppUssd.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously cancels an ongoing USSD session, either mobile or network
 * initiated.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_ussd_cancel_finish() to get the result of the operation.
 *
 * See mm_modem_3gpp_ussd_cancel_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.0
 */
void
mm_modem_3gpp_ussd_cancel (MMModem3gppUssd *self,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_3GPP_USSD (self));

    mm_gdbus_modem3gpp_ussd_call_cancel (MM_GDBUS_MODEM3GPP_USSD (self), cancellable, callback, user_data);
}

/**
 * mm_modem_3gpp_ussd_cancel_sync:
 * @self: A #MMModem3gppUssd.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously cancels an ongoing USSD session, either mobile or network
 * initiated.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_ussd_cancel() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the session was successfully cancelled, %FALSE if @error is
 * set.
 *
 * Since: 1.0
 */
gboolean
mm_modem_3gpp_ussd_cancel_sync (MMModem3gppUssd *self,
                                GCancellable *cancellable,
                                GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP_USSD (self), FALSE);

    return mm_gdbus_modem3gpp_ussd_call_cancel_sync (MM_GDBUS_MODEM3GPP_USSD (self), cancellable, error);
}

/*****************************************************************************/

static void
mm_modem_3gpp_ussd_init (MMModem3gppUssd *self)
{
}

static void
mm_modem_3gpp_ussd_class_init (MMModem3gppUssdClass *modem_class)
{
}
