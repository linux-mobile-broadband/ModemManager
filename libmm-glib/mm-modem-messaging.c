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
 * Copyright (C) 2012 Google, Inc.
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-modem-messaging.h"

/**
 * mm_modem_messaging_get_path:
 * @self: A #MMModemMessaging.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_messaging_get_path (MMModemMessaging *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_messaging_dup_path:
 * @self: A #MMModemMessaging.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_messaging_dup_path (MMModemMessaging *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/**
 * mm_modem_messaging_get_supported_storages:
 * @self: A #MMModem.
 * @storages: (out): Return location for the array of #MMSmsStorage values.
 * @n_storages: (out): Return location for the number of values in @storages.
 *
 * Gets the list of SMS storages supported by the #MMModem.
 */
void
mm_modem_messaging_get_supported_storages (MMModemMessaging *self,
                                           MMSmsStorage **storages,
                                           guint *n_storages)
{
    GArray *array;

    g_return_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self));
    g_return_if_fail (storages != NULL);
    g_return_if_fail (n_storages != NULL);

    array = mm_common_sms_storages_variant_to_garray (mm_gdbus_modem_messaging_get_supported_storages (self));
    *n_storages = array->len;
    *storages = (MMSmsStorage *)g_array_free (array, FALSE);
}

/**
 * mm_modem_messaging_get_default_storage:
 * @self: A #MMModem.
 *
 * Gets the default SMS storage used when storing or receiving SMS messages.
 *
 * Returns: the default #MMSmsStorage.
 */
MMSmsStorage
mm_modem_messaging_get_default_storage (MMModemMessaging *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self), MM_SMS_STORAGE_UNKNOWN);

    return (MMSmsStorage)mm_gdbus_modem_messaging_get_default_storage (self);
}

typedef struct {
    MMModemMessaging *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar **sms_paths;
    GList *sms_objects;
    guint i;
} ListSmsContext;

static void
sms_object_list_free (GList *list)
{
    g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
list_sms_context_complete_and_free (ListSmsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);

    g_strfreev (ctx->sms_paths);
    sms_object_list_free (ctx->sms_objects);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_object_ref (ctx->self);
    g_slice_free (ListSmsContext, ctx);
}

/**
 * mm_modem_messaging_list_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_messaging_list().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_messaging_list().
 *
 * Returns: (transfer-full): The list of #MMSms objects, or %NULL if either none found or if @error is set.
 */
GList *
mm_modem_messaging_list_finish (MMModemMessaging *self,
                                GAsyncResult *res,
                                GError **error)
{
    GList *list;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self), FALSE);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    list = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    /* The list we got, including the objects within, is owned by the async result;
     * so we'll make sure we return a new list */
    g_list_foreach (list, (GFunc)g_object_ref, NULL);
    return g_list_copy (list);
}

static void
list_build_object_ready (GDBusConnection *connection,
                         GAsyncResult *res,
                         ListSmsContext *ctx)
{
    MMSms *sms;
    GError *error = NULL;

    sms = mm_sms_new_finish (res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        list_sms_context_complete_and_free (ctx);
        return;
    }

    /* Keep the object */
    ctx->sms_objects = g_list_prepend (ctx->sms_objects, sms);

    /* If no more smss, just end here. */
    if (!ctx->sms_paths[++ctx->i]) {
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   ctx->sms_objects,
                                                   (GDestroyNotify)sms_object_list_free);
        ctx->sms_objects = NULL;
        list_sms_context_complete_and_free (ctx);
        return;
    }

    /* Keep on creating next object */
    mm_sms_new (g_dbus_proxy_get_connection (
                    G_DBUS_PROXY (ctx->self)),
                ctx->sms_paths[ctx->i],
                ctx->cancellable,
                (GAsyncReadyCallback)list_build_object_ready,
                ctx);
}

static void
list_ready (MMModemMessaging *self,
            GAsyncResult *res,
            ListSmsContext *ctx)
{
    GError *error = NULL;

    mm_gdbus_modem_messaging_call_list_finish (self, &ctx->sms_paths, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        list_sms_context_complete_and_free (ctx);
        return;
    }

    /* If no SMS, just end here. */
    if (!ctx->sms_paths || !ctx->sms_paths[0]) {
        g_simple_async_result_set_op_res_gpointer (ctx->result, NULL, NULL);
        list_sms_context_complete_and_free (ctx);
        return;
    }

    /* Got list of paths. If at least one found, start creating objects for each */
    ctx->i = 0;
    mm_sms_new (g_dbus_proxy_get_connection (
                    G_DBUS_PROXY (self)),
                ctx->sms_paths[ctx->i],
                ctx->cancellable,
                (GAsyncReadyCallback)list_build_object_ready,
                ctx);
}

/**
 * mm_modem_messaging_list:
 * @self: A #MMModemMessaging.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously lists the #MMSms objects in the modem.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_messaging_list_finish() to get the result of the operation.
 *
 * See mm_modem_messaging_list_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_messaging_list (MMModemMessaging *self,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    ListSmsContext *ctx;

    g_return_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self));

    ctx = g_slice_new0 (ListSmsContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_modem_messaging_list);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    mm_gdbus_modem_messaging_call_list (self,
                                        cancellable,
                                        (GAsyncReadyCallback)list_ready,
                                        ctx);
}

/**
 * mm_modem_messaging_list_sync:
 * @self: A #MMModemMessaging.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously lists the #MMSms objects in the modem.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_messaging_list()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer-full): The list of #MMSms objects, or %NULL if either none found or if @error is set.
 */
GList *
mm_modem_messaging_list_sync (MMModemMessaging *self,
                              GCancellable *cancellable,
                              GError **error)
{
    GList *sms_objects = NULL;
    gchar **sms_paths = NULL;
    guint i;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self), NULL);

    if (!mm_gdbus_modem_messaging_call_list_sync (self,
                                                  &sms_paths,
                                                  cancellable,
                                                  error))
        return NULL;

    /* Only non-empty lists are returned */
    if (!sms_paths)
        return NULL;

    for (i = 0; sms_paths[i]; i++) {
        MMSms *sms;

        sms = mm_sms_new_sync (g_dbus_proxy_get_connection (
                                   G_DBUS_PROXY (self)),
                               sms_paths[i],
                               cancellable,
                               error);

        if (!sms) {
            sms_object_list_free (sms_objects);
            g_strfreev (sms_paths);
            return NULL;
        }

        /* Keep the object */
        sms_objects = g_list_prepend (sms_objects, sms);
    }

    g_strfreev (sms_paths);
    return sms_objects;
}

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
} CreateSmsContext;

static void
create_sms_context_complete_and_free (CreateSmsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_slice_free (CreateSmsContext, ctx);
}

/**
 * mm_modem_messaging_create_finish:
 * @self: A #MMModemMessaging.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_messaging_create().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_messaging_create().
 *
 * Returns: (transfer-full): A newly created #MMSms, or %NULL if @error is set.
 */
MMSms *
mm_modem_messaging_create_finish (MMModemMessaging *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self), NULL);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
new_sms_object_ready (GDBusConnection *connection,
                      GAsyncResult *res,
                      CreateSmsContext *ctx)
{
    GError *error = NULL;
    MMSms *sms;

    sms = mm_sms_new_finish (res, &error);
    if (error)
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   sms,
                                                   (GDestroyNotify)g_object_unref);

    create_sms_context_complete_and_free (ctx);
}

static void
create_sms_ready (MMModemMessaging *self,
                  GAsyncResult *res,
                  CreateSmsContext *ctx)
{
    GError *error = NULL;
    gchar *sms_path = NULL;

    if (!mm_gdbus_modem_messaging_call_create_finish (self,
                                                      &sms_path,
                                                      res,
                                                      &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        create_sms_context_complete_and_free (ctx);
        g_free (sms_path);
        return;
    }

    mm_sms_new (g_dbus_proxy_get_connection (
                    G_DBUS_PROXY (self)),
                sms_path,
                ctx->cancellable,
                (GAsyncReadyCallback)new_sms_object_ready,
                ctx);
    g_free (sms_path);
}

/**
 * mm_modem_messaging_create_sms:
 * @self: A #MMModemMessaging.
 * @properties: A ##MMSmsProperties object with the properties to use.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously creates a new #MMSms in the modem.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_messaging_create_finish() to get the result of the operation.
 *
 * See mm_modem_messaging_create_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_messaging_create (MMModemMessaging *self,
                           MMSmsProperties *properties,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    CreateSmsContext *ctx;
    GVariant *dictionary;

    g_return_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self));

    ctx = g_slice_new0 (CreateSmsContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_modem_messaging_create);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    dictionary = (mm_sms_properties_get_dictionary (properties));
    mm_gdbus_modem_messaging_call_create (
        self,
        dictionary,
        cancellable,
        (GAsyncReadyCallback)create_sms_ready,
        ctx);

    g_variant_unref (dictionary);
}

/**
 * mm_modem_create_sms_sync:
 * @self: A #MMModemMessaging.
 * @properties: A ##MMSmsProperties object with the properties to use.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously creates a new #MMSms in the modem.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_messaging_create()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer-full): A newly created #MMSms, or %NULL if @error is set.
 */
MMSms *
mm_modem_messaging_create_sync (MMModemMessaging *self,
                                MMSmsProperties *properties,
                                GCancellable *cancellable,
                                GError **error)
{
    MMSms *sms = NULL;
    gchar *sms_path = NULL;
    GVariant *dictionary;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self), NULL);

    dictionary = (mm_sms_properties_get_dictionary (properties));
    mm_gdbus_modem_messaging_call_create_sync (self,
                                               dictionary,
                                               &sms_path,
                                               cancellable,
                                               error);
    if (sms_path) {
        sms = mm_sms_new_sync (g_dbus_proxy_get_connection (
                                   G_DBUS_PROXY (self)),
                               sms_path,
                               cancellable,
                               error);
        g_free (sms_path);
    }

    g_variant_unref (dictionary);

    return sms;
}

/**
 * mm_modem_messaging_delete:
 * @self: A #MMModemMessaging.
 * @sms: Path of the #MMSms to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously deletes a given #MMSms from the modem.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_messaging_delete_finish() to get the result of the operation.
 *
 * See mm_modem_messaging_delete_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_messaging_delete (MMModemMessaging *self,
                           const gchar *sms,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self));

    mm_gdbus_modem_messaging_call_delete (self,
                                          sms,
                                          cancellable,
                                          callback,
                                          user_data);
}

/**
 * mm_modem_messaging_delete_finish:
 * @self: A #MMModemMessaging.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_messaging_delete().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_messaging_delete().
 *
 * Returns: (skip): %TRUE if the sms was deleted, %FALSE if @error is set.
 */
gboolean
mm_modem_messaging_delete_finish (MMModemMessaging *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self), FALSE);

    return mm_gdbus_modem_messaging_call_delete_finish (self,
                                                        res,
                                                        error);
}

/**
 * mm_modem_messaging_delete_sync:
 * @self: A #MMModemMessaging.
 * @sms: Path of the #MMSms to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.

 * Synchronously deletes a given #MMSms from the modem.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_messaging_delete()
 * for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the SMS was deleted, %FALSE if @error is set.
 */
gboolean
mm_modem_messaging_delete_sync (MMModemMessaging *self,
                                const gchar *sms,
                                GCancellable *cancellable,
                                GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_MESSAGING (self), FALSE);

    return mm_gdbus_modem_messaging_call_delete_sync (self,
                                                      sms,
                                                      cancellable,
                                                      error);
}
