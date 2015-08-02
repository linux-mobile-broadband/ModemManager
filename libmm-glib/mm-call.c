/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
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
 * Copyright (C) 2015 Riccardo Vangelisti <riccardo.vangelisti@sadel.it>
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
 */

#include "string.h"

#include "mm-helpers.h"
#include "mm-call.h"
#include "mm-modem.h"

/**
 * SECTION: mm-call
 * @title: MMCall
 * @short_description: The call interface
 *
 * The #MMCall is an object providing access to the methods, signals and
 * properties of the call interface.
 *
 * When the call is exposed and available in the bus, it is ensured that at
 * least this interface is also available.
 */

G_DEFINE_TYPE (MMCall, mm_call, MM_GDBUS_TYPE_CALL_PROXY)

/*****************************************************************************/

/**
 * mm_call_get_path:
 * @self: A #MMCall.
 *
 * Gets the DBus path of the #MMCall object.
 *
 * Returns: (transfer none): The DBus path of the #MMCall object.
 */
const gchar *
mm_call_get_path (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_call_dup_path:
 * @self: A #MMCall.
 *
 * Gets a copy of the DBus path of the #MMCall object.
 *
 * Returns: (transfer full): The DBus path of the #MMCall object. The returned value should be freed with g_free().
 */
gchar *
mm_call_dup_path (MMCall *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_CALL (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_call_get_number:
 * @self: A #MMCall.
 *
 * Gets the call number. In outgoing calls contains the dialing number or
 * the remote number in incoming calls
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_call_dup_number() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The number, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_call_get_number (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_call_get_number (MM_GDBUS_CALL (self)));
}

/**
 * mm_call_dup_number:
 * @self: A #MMCall.
 *
 * Gets the call number. In outgoing calls contains the dialing number or
 * the remote number in incoming calls
 *
 * Returns: (transfer full): The number, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_call_dup_number (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_call_dup_number (MM_GDBUS_CALL (self)));
}

/*****************************************************************************/

/**
 * mm_call_get_direction:
 * @self: A #MMCall.
 *
 * Gets the call direction.
 *
 * Returns: a #MMCallDirection.
 */
MMCallDirection
mm_call_get_direction (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), MM_CALL_DIRECTION_INCOMING);

    return (MMCallDirection) mm_gdbus_call_get_direction (MM_GDBUS_CALL (self));
}

/*****************************************************************************/

/**
 * mm_call_get_state:
 * @self: A #MMCall.
 *
 * Gets the current state of call.
 *
 * Returns: a #MMCallState.
 */
MMCallState
mm_call_get_state (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), MM_CALL_STATE_UNKNOWN);

    return (MMCallState) mm_gdbus_call_get_state (MM_GDBUS_CALL (self));
}

/*****************************************************************************/

/**
 * mm_call_get_state_reason:
 * @self: A #MMCall.
 *
 * Gets the reason of why the call changes its state.
 *
 * Returns: a #MMCallStateReason.
 */
MMCallStateReason
mm_call_get_state_reason (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), MM_CALL_STATE_REASON_UNKNOWN);

    return (MMCallStateReason) mm_gdbus_call_get_state_reason (MM_GDBUS_CALL (self));
}

/*****************************************************************************/

/**
 * mm_call_start_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_call_start().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_start().
 *
 * Returns:  %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_call_start_finish (MMCall *self,
                      GAsyncResult *res,
                      GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_start_finish (MM_GDBUS_CALL (self), res, error);
}

/**
 * mm_call_start:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to queue the call.
 *
 * Call objects can only be executed once.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_call_start_finish() to get the result of the operation.
 *
 * See mm_call_start_sync() for the synchronous, blocking version of this method.
 */
void
mm_call_start (MMCall *self,
               GCancellable *cancellable,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    g_return_if_fail (MM_IS_CALL (self));

    mm_gdbus_call_call_start (MM_GDBUS_CALL (self),
                              cancellable,
                              callback,
                              user_data);
}

/**
 * mm_call_start_sync:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to queue the call for delivery.
 *
 * Call objects can only be sent once.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_call_start() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_call_start_sync (MMCall *self,
                    GCancellable *cancellable,
                    GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_start_sync (MM_GDBUS_CALL (self),
                                          cancellable,
                                          error);
}

/*****************************************************************************/

/**
 * mm_call_accept_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_call_accept().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_accept().
 *
 * Returns:  %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_call_accept_finish (MMCall *self,
                       GAsyncResult *res,
                       GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_accept_finish (MM_GDBUS_CALL (self), res, error);
}

/**
 * mm_call_accept:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to accept the incoming call.
 *
 * Call objects can only be executed once.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_call_accept_finish() to get the result of the operation.
 *
 * See mm_call_accept_sync() for the synchronous, blocking version of this method.
 */
void
mm_call_accept (MMCall *self,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    g_return_if_fail (MM_IS_CALL (self));

    mm_gdbus_call_call_accept (MM_GDBUS_CALL (self),
                               cancellable,
                               callback,
                               user_data);
}

/**
 * mm_call_accept_sync:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to accept the incoming call.
 *
 * Call objects can only be sent once.
 *
 * The calling thread is blocked until an incoming call is ready.
 * See mm_call_accept() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_call_accept_sync (MMCall *self,
                     GCancellable *cancellable,
                     GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_accept_sync (MM_GDBUS_CALL (self),
                                        cancellable,
                                        error);
}

/*****************************************************************************/

/**
 * mm_call_hangup_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_call_hangup().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_hangup().
 *
 * Returns:  %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_call_hangup_finish (MMCall *self,
                       GAsyncResult *res,
                       GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_hangup_finish (MM_GDBUS_CALL (self), res, error);
}

/**
 * mm_call_hangup:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to hangup the call.
 *
 * Call objects can only be executed once.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_call_hangup_finish() to get the result of the operation.
 *
 * See mm_call_hangup_sync() for the synchronous, blocking version of this method.
 */
void
mm_call_hangup (MMCall *self,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    g_return_if_fail (MM_IS_CALL (self));

    mm_gdbus_call_call_hangup (MM_GDBUS_CALL (self),
                               cancellable,
                               callback,
                               user_data);
}

/**
 * mm_call_hangup_sync:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to hangup the call.
 *
 * Call objects can only be sent once.
 *
 * The calling thread is blocked until an incoming call is ready.
 * See mm_call_hangup() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_call_hangup_sync (MMCall *self,
                     GCancellable *cancellable,
                     GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_hangup_sync (MM_GDBUS_CALL (self),
                                           cancellable,
                                           error);
}

/*****************************************************************************/

/**
 * mm_call_send_dtmf_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_call_send_dtmf().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_send_dtmf().
 *
 * Returns:  %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_call_send_dtmf_finish (MMCall *self,
                          GAsyncResult *res,
                          GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_send_dtmf_finish (MM_GDBUS_CALL (self), res, error);
}

/**
 * mm_call_send_dtmf:
 * @self: A #MMCall.
 * @dtmf: the DMTF tone.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to send a DTMF tone the call.
 *
 * Call objects can only be executed once.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_call_send_dtmf_finish() to get the result of the operation.
 *
 * See mm_call_send_dtmf_sync() for the synchronous, blocking version of this method.
 */
void
mm_call_send_dtmf (MMCall *self,
                   const gchar *dtmf,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_CALL (self));

    mm_gdbus_call_call_send_dtmf (MM_GDBUS_CALL (self),
                                  dtmf,
                                  cancellable,
                                  callback,
                                  user_data);
}

/**
 * mm_call_send_dtmf_sync:
 * @self: A #MMCall.
 * @dtmf: the DMTF tone.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to send a DTMF tone the call.
 *
 * Call objects can only be sent once.
 *
 * The calling thread is blocked until an incoming call is ready.
 * See mm_call_send_dtmf() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_call_send_dtmf_sync (MMCall *self,
                        const gchar *dtmf,
                        GCancellable *cancellable,
                        GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_send_dtmf_sync (MM_GDBUS_CALL (self),
                                              dtmf,
                                              cancellable,
                                              error);
}

/*****************************************************************************/
static void
mm_call_init (MMCall *self)
{
}

static void
mm_call_class_init (MMCallClass *call_class)
{
}
