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
 * Copyright (C) 2015 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2015 Marco Bascetta <marco.bascetta@sadel.it>
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-common-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-voice.h"

/**
 * SECTION: mm-modem-voice
 * @title: MMModemVoice
 * @short_description: The Voice interface
 *
 * The #MMModemVoice is an object providing access to the methods, signals and
 * properties of the Voice interface.
 *
 * The Voice interface is exposed whenever a modem has voice capabilities.
 */

G_DEFINE_TYPE (MMModemVoice, mm_modem_voice, MM_GDBUS_TYPE_MODEM_VOICE_PROXY)

/*****************************************************************************/

/**
 * mm_modem_voice_get_path:
 * @self: A #MMModemVoice.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.6
 */
const gchar *
mm_modem_voice_get_path (MMModemVoice *self)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_voice_dup_path:
 * @self: A #MMModemVoice.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.6
 */
gchar *
mm_modem_voice_dup_path (MMModemVoice *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/**
 * mm_modem_voice_get_emergency_only:
 * @self: A #MMModemVoice.
 *
 * Checks whether emergency calls only are allowed.
 *
 * Returns: %TRUE if only emergency calls are allowed, %FALSE otherwise.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_get_emergency_only (MMModemVoice *self)
{
    return mm_gdbus_modem_voice_get_emergency_only (MM_GDBUS_MODEM_VOICE (self));
}

/*****************************************************************************/

typedef struct {
    gchar **call_paths;
    GList *call_objects;
    guint i;
} ListCallsContext;

static void
call_object_list_free (GList *list)
{
    g_list_free_full (list, g_object_unref);
}

static void
list_call_context_free (ListCallsContext *ctx)
{
    g_strfreev (ctx->call_paths);
    call_object_list_free (ctx->call_objects);
    g_slice_free (ListCallsContext, ctx);
}

/**
 * mm_modem_voice_list_calls_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_list_calls().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_list_calls().
 *
 * Returns: (element-type ModemManager.Call) (transfer full): A list of #MMCall
 * objects, or #NULL if either not found or @error is set. The returned value
 * should be freed with g_list_free_full() using g_object_unref() as
 * #GDestroyNotify function.
 *
 * Since: 1.6
 */
GList *
mm_modem_voice_list_calls_finish (MMModemVoice *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void create_next_call (GTask *task);

static void
list_build_object_ready (GDBusConnection *connection,
                         GAsyncResult *res,
                         GTask *task)
{
    GError *error = NULL;
    GObject *call;
    GObject *source_object;
    ListCallsContext *ctx;

    source_object = g_async_result_get_source_object (res);
    call = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Keep the object */
    ctx->call_objects = g_list_prepend (ctx->call_objects, call);

    /* If no more calls, just end here. */
    if (!ctx->call_paths[++ctx->i]) {
        GList *call_objects;

        call_objects = g_list_copy_deep (ctx->call_objects, (GCopyFunc)g_object_ref, NULL);
        g_task_return_pointer (task, call_objects, (GDestroyNotify)call_object_list_free);
        g_object_unref (task);
        return;
    }

    /* Keep on creating next object */
    create_next_call (task);
}

static void
create_next_call (GTask *task)
{
    MMModemVoice *self;
    ListCallsContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    g_async_initable_new_async (MM_TYPE_CALL,
                                G_PRIORITY_DEFAULT,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback)list_build_object_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    ctx->call_paths[ctx->i],
                                "g-interface-name", "org.freedesktop.ModemManager1.Call",
                                NULL);
}

/**
 * mm_modem_voice_list_calls:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously lists the #MMCall objects in the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_list_calls_finish() to get the result of the operation.
 *
 * See mm_modem_voice_list_calls_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.6
 */
void
mm_modem_voice_list_calls (MMModemVoice *self,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    ListCallsContext *ctx;
    GTask *task;

    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    ctx = g_slice_new0 (ListCallsContext);
    ctx->call_paths = mm_gdbus_modem_voice_dup_calls (MM_GDBUS_MODEM_VOICE (self));

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)list_call_context_free);

    /* If no CALL, just end here. */
    if (!ctx->call_paths || !ctx->call_paths[0]) {
        g_task_return_pointer (task, NULL, NULL);
        g_object_unref (task);
        return;
    }

    /* Got list of paths. If at least one found, start creating objects for each */
    ctx->i = 0;
    create_next_call (task);
}

/**
 * mm_modem_voice_list_calls_sync:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously lists the #MMCall objects in the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_list_calls() for the asynchronous version of this method.
 *
 * Returns: (element-type ModemManager.Call) (transfer full): A list of #MMCall
 * objects, or #NULL if either not found or @error is set. The returned value
 * should be freed with g_list_free_full() using g_object_unref() as
 * #GDestroyNotify function.
 *
 * Since: 1.6
 */
GList *
mm_modem_voice_list_calls_sync (MMModemVoice *self,
                                GCancellable *cancellable,
                                GError **error)
{
    GList *call_objects = NULL;
    gchar **call_paths = NULL;
    guint i;

    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), NULL);

    call_paths = mm_gdbus_modem_voice_dup_calls (MM_GDBUS_MODEM_VOICE (self));

    /* Only non-empty lists are returned */
    if (!call_paths)
        return NULL;

    for (i = 0; call_paths[i]; i++) {
        GObject *call;

        call = g_initable_new (MM_TYPE_CALL,
                               cancellable,
                               error,
                               "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                               "g-name",           MM_DBUS_SERVICE,
                               "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                               "g-object-path",    call_paths[i],
                               "g-interface-name", "org.freedesktop.ModemManager1.Call",
                               NULL);
        if (!call) {
            call_object_list_free (call_objects);
            g_strfreev (call_paths);
            return NULL;
        }

        /* Keep the object */
        call_objects = g_list_prepend (call_objects, call);
    }

    g_strfreev (call_paths);
    return call_objects;
}

/*****************************************************************************/

/**
 * mm_modem_voice_create_call_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_create_call().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_create_call().
 *
 * Returns: (transfer full): A newly created #MMCall, or %NULL if @error is set.
 * The returned value should be freed with g_object_unref().
 *
 * Since: 1.6
 */
MMCall *
mm_modem_voice_create_call_finish (MMModemVoice *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), NULL);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
new_call_object_ready (GDBusConnection *connection,
                       GAsyncResult *res,
                       GTask *task)
{
    GError *error = NULL;
    GObject *call;
    GObject *source_object;

    source_object = g_async_result_get_source_object (res);
    call = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, call, g_object_unref);

    g_object_unref (task);
}

static void
create_call_ready (MMModemVoice *self,
                   GAsyncResult *res,
                   GTask *task)
{
    GError *error = NULL;
    gchar *call_path = NULL;

    if (!mm_gdbus_modem_voice_call_create_call_finish (MM_GDBUS_MODEM_VOICE (self),
                                                       &call_path,
                                                       res,
                                                       &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        g_free (call_path);
        return;
    }

    g_async_initable_new_async (MM_TYPE_CALL,
                                G_PRIORITY_DEFAULT,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback)new_call_object_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    call_path,
                                "g-interface-name", "org.freedesktop.ModemManager1.Call",
                                NULL);
    g_free (call_path);
}

/**
 * mm_modem_voice_create_call:
 * @self: A #MMModemVoice.
 * @properties: A ##MMCallProperties object with the properties to use.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously creates a new #MMCall in the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_create_call_finish() to get the result of the operation.
 *
 * See mm_modem_voice_create_call_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.6
 */
void
mm_modem_voice_create_call (MMModemVoice *self,
                            MMCallProperties *properties,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    GTask *task;
    GVariant *dictionary;

    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    task = g_task_new (self, cancellable, callback, user_data);
    dictionary = mm_call_properties_get_dictionary (properties);
    mm_gdbus_modem_voice_call_create_call (
        MM_GDBUS_MODEM_VOICE (self),
        dictionary,
        cancellable,
        (GAsyncReadyCallback)create_call_ready,
        task);

    g_variant_unref (dictionary);
}

/**
 * mm_modem_voice_create_call_sync:
 * @self: A #MMModemVoice.
 * @properties: A ##MMCallProperties object with the properties to use.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously creates a new #MMCall in the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_create_call() for the asynchronous version of this method.
 *
 * Returns: (transfer full): A newly created #MMCall, or %NULL if @error is set.
 * The returned value should be freed with g_object_unref().
 *
 * Since: 1.6
 */
MMCall *
mm_modem_voice_create_call_sync (MMModemVoice *self,
                                 MMCallProperties *properties,
                                 GCancellable *cancellable,
                                 GError **error)
{
    MMCall *call = NULL;
    gchar *call_path = NULL;
    GVariant *dictionary;

    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), NULL);

    dictionary = mm_call_properties_get_dictionary (properties);
    mm_gdbus_modem_voice_call_create_call_sync (MM_GDBUS_MODEM_VOICE (self),
                                                dictionary,
                                                &call_path,
                                                cancellable,
                                                error);
    if (call_path) {
        call = g_initable_new (MM_TYPE_CALL,
                               cancellable,
                               error,
                               "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                               "g-name",           MM_DBUS_SERVICE,
                               "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                               "g-object-path",    call_path,
                               "g-interface-name", "org.freedesktop.ModemManager1.Call",
                               NULL);
        g_free (call_path);
    }

    g_variant_unref (dictionary);

    return (call ? MM_CALL (call) : NULL);
}

/*****************************************************************************/

/**
 * mm_modem_voice_delete_call_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_delete_call().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_delete_call().
 *
 * Returns: %TRUE if the call was deleted, %FALSE if @error is set.
 *
 * Since: 1.6
 */
gboolean
mm_modem_voice_delete_call_finish (MMModemVoice *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_delete_call_finish (MM_GDBUS_MODEM_VOICE (self), res, error);
}

/**
 * mm_modem_voice_delete_call:
 * @self: A #MMModemVoice.
 * @call: Path of the #MMCall to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously deletes a given #MMCall from the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_delete_call_finish() to get the result of the operation.
 *
 * See mm_modem_voice_delete_call_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.6
 */
void
mm_modem_voice_delete_call (MMModemVoice *self,
                            const gchar *call,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    mm_gdbus_modem_voice_call_delete_call (MM_GDBUS_MODEM_VOICE (self),
                                           call,
                                           cancellable,
                                           callback,
                                           user_data);
}

/**
 * mm_modem_voice_delete_call_sync:
 * @self: A #MMModemVoice.
 * @call: Path of the #MMCall to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.

 * Synchronously deletes a given #MMCall from the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_delete_call() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the CALL was deleted, %FALSE if @error is set.
 *
 * Since: 1.6
 */
gboolean
mm_modem_voice_delete_call_sync (MMModemVoice *self,
                                 const gchar *call,
                                 GCancellable *cancellable,
                                 GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_delete_call_sync (MM_GDBUS_MODEM_VOICE (self),
                                                       call,
                                                       cancellable,
                                                       error);
}

/*****************************************************************************/

/**
 * mm_modem_voice_hold_and_accept_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_hold_and_accept().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_hold_and_accept().
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_hold_and_accept_finish (MMModemVoice  *self,
                                       GAsyncResult  *res,
                                       GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_hold_and_accept_finish (MM_GDBUS_MODEM_VOICE (self), res, error);
}

/**
 * mm_modem_voice_hold_and_accept:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously puts all active calls on hold and accepts the next waiting or
 * held call.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_hold_and_accept_finish() to get the result of the operation.
 *
 * See mm_modem_voice_hold_and_accept_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.12
 */
void
mm_modem_voice_hold_and_accept (MMModemVoice        *self,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    mm_gdbus_modem_voice_call_hold_and_accept (MM_GDBUS_MODEM_VOICE (self),
                                               cancellable,
                                               callback,
                                               user_data);
}

/**
 * mm_modem_voice_hold_and_accept_sync:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously puts all active calls on hold and accepts the next waiting or
 * held call.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_hold_and_accept() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_hold_and_accept_sync (MMModemVoice  *self,
                                     GCancellable  *cancellable,
                                     GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_hold_and_accept_sync (MM_GDBUS_MODEM_VOICE (self),
                                                           cancellable,
                                                           error);
}

/*****************************************************************************/

/**
 * mm_modem_voice_hangup_and_accept_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_hangup_and_accept().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_hangup_and_accept().
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_hangup_and_accept_finish (MMModemVoice  *self,
                                         GAsyncResult  *res,
                                         GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_hangup_and_accept_finish (MM_GDBUS_MODEM_VOICE (self), res, error);
}

/**
 * mm_modem_voice_hangup_and_accept:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously hangs up all active calls and accepts the next waiting or held
 * call.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_hangup_and_accept_finish() to get the result of the operation.
 *
 * See mm_modem_voice_hangup_and_accept_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.12
 */
void
mm_modem_voice_hangup_and_accept (MMModemVoice        *self,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    mm_gdbus_modem_voice_call_hangup_and_accept (MM_GDBUS_MODEM_VOICE (self),
                                                 cancellable,
                                                 callback,
                                                 user_data);
}

/**
 * mm_modem_voice_hangup_and_accept_sync:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously hangs up all active calls and accepts the next waiting or held
 * call.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_hangup_and_accept() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_hangup_and_accept_sync (MMModemVoice  *self,
                                       GCancellable  *cancellable,
                                       GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_hangup_and_accept_sync (MM_GDBUS_MODEM_VOICE (self),
                                                             cancellable,
                                                             error);
}

/*****************************************************************************/

/**
 * mm_modem_voice_hangup_all_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_hangup_all().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_hangup_all().
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_hangup_all_finish (MMModemVoice  *self,
                                  GAsyncResult  *res,
                                  GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_hangup_all_finish (MM_GDBUS_MODEM_VOICE (self), res, error);
}

/**
 * mm_modem_voice_hangup_all:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously hangs up all ongoing (active, waiting, held) calls.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_hangup_all_finish() to get the result of the operation.
 *
 * See mm_modem_voice_hangup_all_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.12
 */
void
mm_modem_voice_hangup_all (MMModemVoice        *self,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    mm_gdbus_modem_voice_call_hangup_all (MM_GDBUS_MODEM_VOICE (self),
                                          cancellable,
                                          callback,
                                          user_data);
}

/**
 * mm_modem_voice_hangup_all_sync:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously hangs up all ongoing (active, waiting, held) calls.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_hangup_all() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_hangup_all_sync (MMModemVoice  *self,
                                GCancellable  *cancellable,
                                GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_hangup_all_sync (MM_GDBUS_MODEM_VOICE (self),
                                                      cancellable,
                                                      error);
}

/*****************************************************************************/

/**
 * mm_modem_voice_transfer_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_transfer().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_transfer().
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_transfer_finish (MMModemVoice  *self,
                                GAsyncResult  *res,
                                GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_transfer_finish (MM_GDBUS_MODEM_VOICE (self), res, error);
}

/**
 * mm_modem_voice_transfer:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously joins all active and held calls, and disconnects from them.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_transfer_finish() to get the result of the operation.
 *
 * See mm_modem_voice_transfer_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.12
 */
void
mm_modem_voice_transfer (MMModemVoice        *self,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    mm_gdbus_modem_voice_call_transfer (MM_GDBUS_MODEM_VOICE (self),
                                        cancellable,
                                        callback,
                                        user_data);
}

/**
 * mm_modem_voice_transfer_sync:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously joins all active and held calls, and disconnects from them.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_transfer() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_transfer_sync (MMModemVoice  *self,
                              GCancellable  *cancellable,
                              GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_transfer_sync (MM_GDBUS_MODEM_VOICE (self),
                                                    cancellable,
                                                    error);
}

/*****************************************************************************/

/**
 * mm_modem_voice_call_waiting_setup_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_call_waiting_setup().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_call_waiting_setup().
 *
 * Returns: %TRUE if  @status is set, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_call_waiting_setup_finish (MMModemVoice  *self,
                                          GAsyncResult  *res,
                                          GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_call_waiting_setup_finish (MM_GDBUS_MODEM_VOICE (self), res, error);
}

/**
 * mm_modem_voice_call_waiting_setup:
 * @self: A #MMModemVoice.
 * @enable: Whether the call waiting service should be enabled.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously enables or disables the call waiting network service.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_call_waiting_setup_finish() to get the result of the
 * operation.
 *
 * See mm_modem_voice_call_waiting_setup_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.12
 */
void
mm_modem_voice_call_waiting_setup (MMModemVoice        *self,
                                   gboolean             enable,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    mm_gdbus_modem_voice_call_call_waiting_setup (MM_GDBUS_MODEM_VOICE (self),
                                                  enable,
                                                  cancellable,
                                                  callback,
                                                  user_data);
}

/**
 * mm_modem_voice_call_waiting_setup_sync:
 * @self: A #MMModemVoice.
 * @enable: Whether the call waiting service should be enabled.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously enables or disables the call waiting network service.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_call_waiting_setup() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if the operation is successful, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_call_waiting_setup_sync (MMModemVoice  *self,
                                        gboolean       enable,
                                        GCancellable  *cancellable,
                                        GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_call_waiting_setup_sync (MM_GDBUS_MODEM_VOICE (self),
                                                              enable,
                                                              cancellable,
                                                              error);
}

/*****************************************************************************/

/**
 * mm_modem_voice_call_waiting_query_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_voice_call_waiting_query().
 * @status: Output location where to store the status.
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_call_waiting_query().
 *
 * Returns: %TRUE if @status is set, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_call_waiting_query_finish (MMModemVoice  *self,
                                          GAsyncResult  *res,
                                          gboolean      *status,
                                          GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_call_waiting_query_finish (MM_GDBUS_MODEM_VOICE (self), status, res, error);
}

/**
 * mm_modem_voice_call_waiting_query:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously queries the status of the call waiting network service.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_voice_call_waiting_query_finish() to get the result of the
 * operation.
 *
 * See mm_modem_voice_call_waiting_query_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.12
 */
void
mm_modem_voice_call_waiting_query (MMModemVoice        *self,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    mm_gdbus_modem_voice_call_call_waiting_query (MM_GDBUS_MODEM_VOICE (self),
                                                  cancellable,
                                                  callback,
                                                  user_data);
}

/**
 * mm_modem_voice_call_waiting_query_sync:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @status: Output location where to store the status.
 * @error: Return location for error or %NULL.
 *
 * Synchronously queries the status of the call waiting network service.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_voice_call_waiting_query() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if @status is set, %FALSE if @error is set.
 *
 * Since: 1.12
 */
gboolean
mm_modem_voice_call_waiting_query_sync (MMModemVoice  *self,
                                        GCancellable  *cancellable,
                                        gboolean      *status,
                                        GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    return mm_gdbus_modem_voice_call_call_waiting_query_sync (MM_GDBUS_MODEM_VOICE (self),
                                                              status,
                                                              cancellable,
                                                              error);
}

/*****************************************************************************/

static void
mm_modem_voice_init (MMModemVoice *self)
{
}

static void
mm_modem_voice_class_init (MMModemVoiceClass *modem_class)
{
}
