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

struct _MMCallPrivate {
    /* Audio Format */
    GMutex audio_format_mutex;
    guint audio_format_id;
    MMCallAudioFormat *audio_format;
};

/*****************************************************************************/

/**
 * mm_call_get_path:
 * @self: A #MMCall.
 *
 * Gets the DBus path of the #MMCall object.
 *
 * Returns: (transfer none): The DBus path of the #MMCall object.
 *
 * Since: 1.6
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
 * Returns: (transfer full): The DBus path of the #MMCall object.
 * The returned value should be freed with g_free().
 *
 * Since: 1.6
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
 *
 * Since: 1.6
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
 * Returns: (transfer full): The number, or %NULL if it couldn't be retrieved.
 * The returned value should be freed with g_free().
 *
 * Since: 1.6
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
 *
 * Since: 1.6
 */
MMCallDirection
mm_call_get_direction (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), MM_CALL_DIRECTION_INCOMING);

    return (MMCallDirection) mm_gdbus_call_get_direction (MM_GDBUS_CALL (self));
}

/*****************************************************************************/

/**
 * mm_call_get_multiparty:
 * @self: A #MMCall.
 *
 * Gets whether the call is part of a multiparty call.
 *
 * Returns: %TRUE if the call is part of a multiparty call, %FALSE otherwise.
 *
 * Since: 1.12
 */
gboolean
mm_call_get_multiparty (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_get_multiparty (MM_GDBUS_CALL (self));
}

/*****************************************************************************/

/**
 * mm_call_get_state:
 * @self: A #MMCall.
 *
 * Gets the current state of call.
 *
 * Returns: a #MMCallState.
 *
 * Since: 1.6
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
 *
 * Since: 1.6
 */
MMCallStateReason
mm_call_get_state_reason (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), MM_CALL_STATE_REASON_UNKNOWN);

    return (MMCallStateReason) mm_gdbus_call_get_state_reason (MM_GDBUS_CALL (self));
}

/*****************************************************************************/

/**
 * mm_call_get_audio_port:
 * @self: A #MMCall.
 *
 * Gets the kernel device used for audio (if any).
 *
 * Returns: (transfer none): The audio port, or %NULL if call audio is not
 * routed via the host or couldn't be retrieved.
 *
 * Since: 1.10
 */
const gchar *
mm_call_get_audio_port (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_call_get_audio_port (MM_GDBUS_CALL (self)));
}

/**
 * mm_call_dup_audio_port:
 * @self: A #MMCall.
 *
 * Gets the kernel device used for audio (if any).
 *
 * Returns: (transfer full): The audio port, or %NULL if call audio is not
 * routed via the host or couldn't be retrieved.
 *
 * Since: 1.10
 */
gchar *
mm_call_dup_audio_port (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_call_dup_audio_port (MM_GDBUS_CALL (self)));
}

/*****************************************************************************/

static void
audio_format_updated (MMCall *self,
                      GParamSpec *pspec)
{
    g_mutex_lock (&self->priv->audio_format_mutex);
    {
        GVariant *dictionary;

        g_clear_object (&self->priv->audio_format);

        /* TODO: update existing object instead of re-creating? */
        dictionary = mm_gdbus_call_get_audio_format (MM_GDBUS_CALL (self));
        if (dictionary) {
            GError *error = NULL;

            self->priv->audio_format = mm_call_audio_format_new_from_dictionary (dictionary, &error);
            if (error) {
                g_warning ("Invalid audio format update received: %s", error->message);
                g_error_free (error);
            }
        }
    }
    g_mutex_unlock (&self->priv->audio_format_mutex);
}

static void
ensure_internal_audio_format (MMCall *self,
                             MMCallAudioFormat **dup)
{
    g_mutex_lock (&self->priv->audio_format_mutex);
    {
        /* If this is the first time ever asking for the object, setup the
         * update listener and the initial object, if any. */
        if (!self->priv->audio_format_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_call_dup_audio_format (MM_GDBUS_CALL (self));
            if (dictionary) {
                GError *error = NULL;

                self->priv->audio_format = mm_call_audio_format_new_from_dictionary (dictionary, &error);
                if (error) {
                    g_warning ("Invalid initial audio format: %s", error->message);
                    g_error_free (error);
                }
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->audio_format_id =
                g_signal_connect (self,
                                  "notify::audio-format",
                                  G_CALLBACK (audio_format_updated),
                                  NULL);
        }

        if (dup && self->priv->audio_format)
            *dup = g_object_ref (self->priv->audio_format);
    }
    g_mutex_unlock (&self->priv->audio_format_mutex);
}

/**
 * mm_call_get_audio_format:
 * @self: A #MMCall.
 *
 * Gets a #MMCallAudioFormat object specifying the audio format used by the
 * audio port if call audio is routed via the host.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_call_get_audio_format() again to get a new #MMCallAudioFormat with the
 * new values.</warning>
 *
 * Returns: (transfer full): A #MMCallAudioFormat that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.10
 */
MMCallAudioFormat *
mm_call_get_audio_format (MMCall *self)
{
    MMCallAudioFormat *format = NULL;

    g_return_val_if_fail (MM_IS_CALL (self), NULL);

    ensure_internal_audio_format (self, &format);
    return format;
}

/**
 * mm_call_peek_audio_format:
 * @self: A #MMCall.
 *
 * Gets a #MMCallAudioFormat object specifying the audio format used by the
 * audio port if call audio is routed via the host.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_call_get_audio_format() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): A #MMCallAudioFormat. Do not free the returned
 * value, it belongs to @self.
 *
 * Since: 1.10
 */
MMCallAudioFormat *
mm_call_peek_audio_format (MMCall *self)
{
    g_return_val_if_fail (MM_IS_CALL (self), NULL);

    ensure_internal_audio_format (self, NULL);
    return self->priv->audio_format;
}

/*****************************************************************************/

/**
 * mm_call_start_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_call_start().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_start().
 *
 * Returns:  %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.6
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to queue the call.
 *
 * Call objects can only be executed once.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_call_start_finish() to get the result of the operation.
 *
 * See mm_call_start_sync() for the synchronous, blocking version of this method.
 *
 * Since: 1.6
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
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.6
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
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_call_accept().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_accept().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.6
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to accept the incoming call.
 *
 * Call objects can only be executed once.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_call_accept_finish() to get the result of the operation.
 *
 * See mm_call_accept_sync() for the synchronous, blocking version of this method.
 *
 * Since: 1.6
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
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.6
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
 * mm_call_deflect_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_call_deflect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_deflect().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_call_deflect_finish (MMCall *self,
                        GAsyncResult *res,
                        GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_deflect_finish (MM_GDBUS_CALL (self), res, error);
}

/**
 * mm_call_deflect:
 * @self: A #MMCall.
 * @number: new number where the call will be deflected.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to deflect the incoming call.
 *
 * This call will be considered terminated once the deflection is performed.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_call_deflect_finish() to get the result of the operation.
 *
 * See mm_call_deflect_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.12
 */
void
mm_call_deflect (MMCall *self,
                 const gchar *number,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_return_if_fail (MM_IS_CALL (self));

    mm_gdbus_call_call_deflect (MM_GDBUS_CALL (self),
                                number,
                                cancellable,
                                callback,
                                user_data);
}

/**
 * mm_call_deflect_sync:
 * @self: A #MMCall.
 * @number: new number where the call will be deflected.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to deflect the incoming call.
 *
 * This call will be considered terminated once the deflection is performed.
 *
 * The calling thread is blocked until an incoming call is ready.
 * See mm_call_deflect() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_call_deflect_sync (MMCall *self,
                      const gchar *number,
                      GCancellable *cancellable,
                      GError **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_deflect_sync (MM_GDBUS_CALL (self),
                                            number,
                                            cancellable,
                                            error);
}

/*****************************************************************************/

/**
 * mm_call_join_multiparty_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_call_join_multiparty().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_join_multiparty().
 *
 * Returns:  %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_call_join_multiparty_finish (MMCall        *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_join_multiparty_finish (MM_GDBUS_CALL (self), res, error);
}

/**
 * mm_call_join_multiparty:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Synchronously requests to join this call into a multiparty call.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_call_join_multiparty_finish() to get the result of the operation.
 *
 * See mm_call_join_multiparty_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.12
 */
void
mm_call_join_multiparty (MMCall              *self,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    g_return_if_fail (MM_IS_CALL (self));

    mm_gdbus_call_call_join_multiparty (MM_GDBUS_CALL (self),
                                        cancellable,
                                        callback,
                                        user_data);
}

/**
 * mm_call_join_multiparty_sync:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to join this call into a multiparty call.
 *
 * The calling thread is blocked until an incoming call is ready.
 * See mm_call_join_multiparty() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_call_join_multiparty_sync (MMCall       *self,
                              GCancellable *cancellable,
                              GError       **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_join_multiparty_sync (MM_GDBUS_CALL (self),
                                                    cancellable,
                                                    error);
}

/**
 * mm_call_leave_multiparty_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_call_leave_multiparty().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_leave_multiparty().
 *
 * Returns:  %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_call_leave_multiparty_finish (MMCall        *self,
                                 GAsyncResult  *res,
                                 GError       **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_leave_multiparty_finish (MM_GDBUS_CALL (self), res, error);
}

/**
 * mm_call_leave_multiparty:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Synchronously requests to make this call private again by leaving the
 * multiparty call.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_call_leave_multiparty_finish() to get the result of the operation.
 *
 * See mm_call_leave_multiparty_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.12
 */
void
mm_call_leave_multiparty (MMCall              *self,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    g_return_if_fail (MM_IS_CALL (self));

    mm_gdbus_call_call_leave_multiparty (MM_GDBUS_CALL (self),
                                         cancellable,
                                         callback,
                                         user_data);
}

/**
 * mm_call_leave_multiparty_sync:
 * @self: A #MMCall.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to make this call private again by leaving the
 * multiparty call.
 *
 * The calling thread is blocked until an incoming call is ready.
 * See mm_call_leave_multiparty() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_call_leave_multiparty_sync (MMCall       *self,
                               GCancellable *cancellable,
                               GError       **error)
{
    g_return_val_if_fail (MM_IS_CALL (self), FALSE);

    return mm_gdbus_call_call_leave_multiparty_sync (MM_GDBUS_CALL (self),
                                                     cancellable,
                                                     error);
}

 /*****************************************************************************/

/**
 * mm_call_hangup_finish:
 * @self: A #MMCall.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_call_hangup().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_hangup().
 *
 * Returns:  %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.6
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to hangup the call.
 *
 * Call objects can only be executed once.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_call_hangup_finish() to get the result of the operation.
 *
 * See mm_call_hangup_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.6
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
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.6
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
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_call_send_dtmf().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_call_send_dtmf().
 *
 * Returns:  %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.6
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to send a DTMF tone the call.
 *
 * Call objects can only be executed once.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_call_send_dtmf_finish() to get the result of the operation.
 *
 * See mm_call_send_dtmf_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.6
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
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.6
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
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_CALL,
                                              MMCallPrivate);
    g_mutex_init (&self->priv->audio_format_mutex);
}

static void
finalize (GObject *object)
{
    MMCall *self = MM_CALL (object);

    g_mutex_clear (&self->priv->audio_format_mutex);

    G_OBJECT_CLASS (mm_call_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMCall *self = MM_CALL (object);

    g_clear_object (&self->priv->audio_format);

    G_OBJECT_CLASS (mm_call_parent_class)->dispose (object);
}

static void
mm_call_class_init (MMCallClass *call_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (call_class);

    g_type_class_add_private (object_class, sizeof (MMCallPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
    object_class->finalize = finalize;
}
