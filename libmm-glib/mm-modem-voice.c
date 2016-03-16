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
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
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

/*****************************************************************************/

typedef struct {
    MMModemVoice *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar **call_paths;
    GList *call_objects;
    guint i;
} ListCallsContext;

static void
call_object_list_free (GList *list)
{
    g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
list_call_context_complete_and_free (ListCallsContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    g_strfreev (ctx->call_paths);
    call_object_list_free (ctx->call_objects);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_ref (ctx->self);
    g_slice_free (ListCallsContext, ctx);
}

/**
 * mm_modem_voice_list_calls_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_voice_list_calls().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_list_calls().
 *
 * Returns: (element-type ModemManager.Call) (transfer full): A list of #MMCall objects, or #NULL if either not found or @error is set. The returned value should be freed with g_list_free_full() using g_object_unref() as #GDestroyNotify function.
 */
GList *
mm_modem_voice_list_calls_finish (MMModemVoice *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    GList *list;

    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), FALSE);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    list = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    /* The list we got, including the objects within, is owned by the async result;
     * so we'll make sure we return a new list */
    g_list_foreach (list, (GFunc)g_object_ref, NULL);
    return g_list_copy (list);
}

static void create_next_call (ListCallsContext *ctx);

static void
list_build_object_ready (GDBusConnection *connection,
                         GAsyncResult *res,
                         ListCallsContext *ctx)
{
    GError *error = NULL;
    GObject *call;
    GObject *source_object;

    source_object = g_async_result_get_source_object (res);
    call = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        list_call_context_complete_and_free (ctx);
        return;
    }

    /* Keep the object */
    ctx->call_objects = g_list_prepend (ctx->call_objects, call);

    /* If no more calls, just end here. */
    if (!ctx->call_paths[++ctx->i]) {
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   ctx->call_objects,
                                                   (GDestroyNotify)call_object_list_free);
        ctx->call_objects = NULL;
        list_call_context_complete_and_free (ctx);
        return;
    }

    /* Keep on creating next object */
    create_next_call (ctx);
}

static void
create_next_call (ListCallsContext *ctx)
{
    g_async_initable_new_async (MM_TYPE_CALL,
                                G_PRIORITY_DEFAULT,
                                ctx->cancellable,
                                (GAsyncReadyCallback)list_build_object_ready,
                                ctx,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (ctx->self)),
                                "g-object-path",    ctx->call_paths[ctx->i],
                                "g-interface-name", "org.freedesktop.ModemManager1.Call",
                                NULL);
}

/**
 * mm_modem_voice_list_calls:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously lists the #MMCall objects in the modem.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_voice_list_calls_finish() to get the result of the operation.
 *
 * See mm_modem_voice_list_calls_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_voice_list_calls (MMModemVoice *self,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    ListCallsContext *ctx;

    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    ctx = g_slice_new0 (ListCallsContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_modem_voice_list_calls);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    ctx->call_paths = mm_gdbus_modem_voice_dup_calls (MM_GDBUS_MODEM_VOICE (self));

    /* If no CALL, just end here. */
    if (!ctx->call_paths || !ctx->call_paths[0]) {
        g_simple_async_result_set_op_res_gpointer (ctx->result, NULL, NULL);
        list_call_context_complete_and_free (ctx);
        return;
    }

    /* Got list of paths. If at least one found, start creating objects for each */
    ctx->i = 0;
    create_next_call (ctx);
}

/**
 * mm_modem_voice_list_calls_sync:
 * @self: A #MMModemVoice.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously lists the #MMCall objects in the modem.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_voice_list_calls()
 * for the asynchronous version of this method.
 *
 * Returns: (element-type MMCall) (transfer full): A list of #MMCall objects, or #NULL if either not found or @error is set. The returned value should be freed with g_list_free_full() using g_object_unref() as #GDestroyNotify function.
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

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
} CreateCallContext;

static void
create_call_context_complete_and_free (CreateCallContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_slice_free (CreateCallContext, ctx);
}

/**
 * mm_modem_voice_create_call_finish:
 * @self: A #MMModemVoice.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_voice_create_call().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_create_call().
 *
 * Returns: (transfer full): A newly created #MMCall, or %NULL if @error is set. The returned value should be freed with g_object_unref().
 */
MMCall *
mm_modem_voice_create_call_finish (MMModemVoice *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_VOICE (self), NULL);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
new_call_object_ready (GDBusConnection *connection,
                      GAsyncResult *res,
                      CreateCallContext *ctx)
{
    GError *error = NULL;
    GObject *call;
    GObject *source_object;

    source_object = g_async_result_get_source_object (res);
    call = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error)
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   call,
                                                   (GDestroyNotify)g_object_unref);

    create_call_context_complete_and_free (ctx);
}

static void
create_call_ready (MMModemVoice *self,
                  GAsyncResult *res,
                  CreateCallContext *ctx)
{
    GError *error = NULL;
    gchar *call_path = NULL;

    if (!mm_gdbus_modem_voice_call_create_call_finish (MM_GDBUS_MODEM_VOICE (self),
                                                       &call_path,
                                                       res,
                                                       &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        create_call_context_complete_and_free (ctx);
        g_free (call_path);
        return;
    }

    g_async_initable_new_async (MM_TYPE_CALL,
                                G_PRIORITY_DEFAULT,
                                ctx->cancellable,
                                (GAsyncReadyCallback)new_call_object_ready,
                                ctx,
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously creates a new #MMCall in the modem.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_voice_create_call_finish() to get the result of the operation.
 *
 * See mm_modem_voice_create_call_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_voice_create_call (MMModemVoice *self,
                            MMCallProperties *properties,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    CreateCallContext *ctx;
    GVariant *dictionary;

    g_return_if_fail (MM_IS_MODEM_VOICE (self));

    ctx = g_slice_new0 (CreateCallContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_modem_voice_create_call);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    dictionary = mm_call_properties_get_dictionary (properties);
    mm_gdbus_modem_voice_call_create_call (
        MM_GDBUS_MODEM_VOICE (self),
        dictionary,
        cancellable,
        (GAsyncReadyCallback)create_call_ready,
        ctx);

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
 * The calling thread is blocked until a reply is received. See mm_modem_voice_create_call()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer full): A newly created #MMCall, or %NULL if @error is set. The returned value should be freed with g_object_unref().
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
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_voice_delete_call().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_voice_delete_call().
 *
 * Returns: %TRUE if the call was deleted, %FALSE if @error is set.
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously deletes a given #MMCall from the modem.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_voice_delete_call_finish() to get the result of the operation.
 *
 * See mm_modem_voice_delete_call_sync() for the synchronous, blocking version of this method.
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
 * The calling thread is blocked until a reply is received. See mm_modem_voice_delete_call()
 * for the asynchronous version of this method.
 *
 * Returns: %TRUE if the CALL was deleted, %FALSE if @error is set.
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

static void
mm_modem_voice_init (MMModemVoice *self)
{
}

static void
mm_modem_voice_class_init (MMModemVoiceClass *modem_class)
{
}
