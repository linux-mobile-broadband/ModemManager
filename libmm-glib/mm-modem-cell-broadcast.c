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
 * Copyright (C) 2024-2025 Guido Günther <agx@sigxcpu.org>
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-common-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-cell-broadcast.h"

/**
 * SECTION: mm-modem-cell-broadcast
 * @title: MMModemCellBroadcast
 * @short_description: The CellBroadcast interface
 *
 * The #MMModemCellBroadcast is an object providing access to the methods, signals and
 * properties of the CellBroadcast interface.
 *
 * The CellBroadcast interface is exposed whenever a modem has cell broadcast capabilities.
 */

G_DEFINE_TYPE (MMModemCellBroadcast, mm_modem_cell_broadcast, MM_GDBUS_TYPE_MODEM_CELL_BROADCAST_PROXY)

struct _MMModemCellBroadcastPrivate {
    /* Common mutex to sync access */
    GMutex mutex;

    PROPERTY_ARRAY_DECLARE (channels)
};

/*****************************************************************************/

/**
 * mm_modem_cell_broadcast_get_path:
 * @self: A #MMModemCellBroadcast.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.24
 */
const gchar *
mm_modem_cell_broadcast_get_path (MMModemCellBroadcast *self)
{
    g_return_val_if_fail (MM_IS_MODEM_CELL_BROADCAST (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_cell_broadcast_dup_path:
 * @self: A #MMModemCellBroadcast.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.24
 */
gchar *
mm_modem_cell_broadcast_dup_path (MMModemCellBroadcast *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_CELL_BROADCAST (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

PROPERTY_ARRAY_DEFINE (channels,
                       ModemCellBroadcast, modem_cell_broadcast, MODEM_CELL_BROADCAST,
                       MMCellBroadcastChannels,
                       mm_common_cell_broadcast_channels_variant_to_garray)

/*****************************************************************************/

typedef struct {
    gchar **cbm_paths;
    GList *cbm_objects;
    guint i;
} ListCbmContext;

static void
cbm_object_list_free (GList *list)
{
    g_list_free_full (list, g_object_unref);
}

static void
list_cbm_context_free (ListCbmContext *ctx)
{
    g_strfreev (ctx->cbm_paths);
    cbm_object_list_free (ctx->cbm_objects);
    g_slice_free (ListCbmContext, ctx);
}

/**
 * mm_modem_cell_broadcast_list_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_cell_broadcast_list().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_cell_broadcast_list().
 *
 * Returns: (element-type ModemManager.Cbm) (transfer full): A list of #MMCbm
 * objects, or #NULL if either not found or @error is set. The returned value
 * should be freed with g_list_free_full() using g_object_unref() as
 * #GDestroyNotify function.
 *
 * Since: 1.24
 */
GList *
mm_modem_cell_broadcast_list_finish (MMModemCellBroadcast *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_CELL_BROADCAST (self), FALSE);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void create_next_cbm (GTask *task);

static void
list_build_object_ready (GDBusConnection *connection,
                         GAsyncResult *res,
                         GTask *task)
{
    GError *error = NULL;
    GObject *cbm;
    GObject *source_object;
    ListCbmContext *ctx;

    source_object = g_async_result_get_source_object (res);
    cbm = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);

    /* Keep the object */
    ctx->cbm_objects = g_list_prepend (ctx->cbm_objects, cbm);

    /* If no more cbms, just end here. */
    if (!ctx->cbm_paths[++ctx->i]) {
        GList *cbm_objects;

        cbm_objects = g_list_copy_deep (ctx->cbm_objects, (GCopyFunc)g_object_ref, NULL);
        g_task_return_pointer (task, cbm_objects, (GDestroyNotify)cbm_object_list_free);
        g_object_unref (task);
        return;
    }

    /* Keep on creating next object */
    create_next_cbm (task);
}

static void
create_next_cbm (GTask *task)
{
    MMModemCellBroadcast *self;
    ListCbmContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    g_async_initable_new_async (MM_TYPE_CBM,
                                G_PRIORITY_DEFAULT,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback)list_build_object_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    ctx->cbm_paths[ctx->i],
                                "g-interface-name", "org.freedesktop.ModemManager1.Cbm",
                                NULL);
}

/**
 * mm_modem_cell_broadcast_list:
 * @self: A #MMModemCellBroadcast.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously lists the #MMCbm objects in the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_cell_broadcast_list_finish() to get the result of the operation.
 *
 * See mm_modem_cell_broadcast_list_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.24
 */
void
mm_modem_cell_broadcast_list (MMModemCellBroadcast *self,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    ListCbmContext *ctx;
    GTask *task;

    g_return_if_fail (MM_IS_MODEM_CELL_BROADCAST (self));

    ctx = g_slice_new0 (ListCbmContext);
    ctx->cbm_paths = mm_gdbus_modem_cell_broadcast_dup_cell_broadcasts (MM_GDBUS_MODEM_CELL_BROADCAST (self));

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)list_cbm_context_free);

    /* If no CBM, just end here. */
    if (!ctx->cbm_paths || !ctx->cbm_paths[0]) {
        g_task_return_pointer (task, NULL, NULL);
        g_object_unref (task);
        return;
    }

    /* Got list of paths. If at least one found, start creating objects for each */
    ctx->i = 0;
    create_next_cbm (task);
}

/**
 * mm_modem_cell_broadcast_list_sync:
 * @self: A #MMModemCellBroadcast.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously lists the #MMCbm objects in the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_cell_broadcast_list() for the asynchronous version of this method.
 *
 * Returns: (element-type ModemManager.Cbm) (transfer full): A list of #MMCbm
 * objects, or #NULL if either not found or @error is set. The returned value
 * should be freed with g_list_free_full() using g_object_unref() as
 * #GDestroyNotify function.
 *
 * Since: 1.24
 */
GList *
mm_modem_cell_broadcast_list_sync (MMModemCellBroadcast *self,
                                   GCancellable *cancellable,
                                   GError **error)
{
    GList *cbm_objects = NULL;
    gchar **cbm_paths = NULL;
    guint i;

    g_return_val_if_fail (MM_IS_MODEM_CELL_BROADCAST (self), NULL);

    cbm_paths = mm_gdbus_modem_cell_broadcast_dup_cell_broadcasts (MM_GDBUS_MODEM_CELL_BROADCAST (self));

    /* Only non-empty lists are returned */
    if (!cbm_paths)
        return NULL;

    for (i = 0; cbm_paths[i]; i++) {
        GObject *cbm;

        cbm = g_initable_new (MM_TYPE_CBM,
                                 cancellable,
                                 error,
                                 "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                 "g-name",           MM_DBUS_SERVICE,
                                 "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                 "g-object-path",    cbm_paths[i],
                                 "g-interface-name", "org.freedesktop.ModemManager1.Cbm",
                              NULL);
        if (!cbm) {
            cbm_object_list_free (cbm_objects);
            g_strfreev (cbm_paths);
            return NULL;
        }

        /* Keep the object */
        cbm_objects = g_list_prepend (cbm_objects, cbm);
    }

    g_strfreev (cbm_paths);
    return cbm_objects;
}

/*****************************************************************************/

/**
 * mm_modem_cell_broadcast_delete_finish:
 * @self: A #MMModemCellBroadcast.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_cell_broadcast_delete().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_cell_broadcast_delete().
 *
 * Returns: %TRUE if the cbm was deleted, %FALSE if @error is set.
 *
 * Since: 1.24
 */
gboolean
mm_modem_cell_broadcast_delete_finish (MMModemCellBroadcast *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_CELL_BROADCAST (self), FALSE);

    return mm_gdbus_modem_cell_broadcast_call_delete_finish (MM_GDBUS_MODEM_CELL_BROADCAST (self), res, error);
}

/**
 * mm_modem_cell_broadcast_delete:
 * @self: A #MMModemCellBroadcast.
 * @cbm: Path of the #MMCbm to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously deletes a given #MMCbm from the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_cell_broadcast_delete_finish() to get the result of the operation.
 *
 * See mm_modem_cell_broadcast_delete_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.24
 */
void
mm_modem_cell_broadcast_delete (MMModemCellBroadcast *self,
                           const gchar *cbm,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_CELL_BROADCAST (self));

    mm_gdbus_modem_cell_broadcast_call_delete (MM_GDBUS_MODEM_CELL_BROADCAST (self),
                                               cbm,
                                               cancellable,
                                               callback,
                                               user_data);
}

/**
 * mm_modem_cell_broadcast_delete_sync:
 * @self: A #MMModemCellBroadcast.
 * @cbm: Path of the #MMCbm to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.

 * Synchronously deletes a given #MMCbm from the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_cell_broadcast_delete() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the CBM was deleted, %FALSE if @error is set.
 *
 * Since: 1.24
 */
gboolean
mm_modem_cell_broadcast_delete_sync (MMModemCellBroadcast *self,
                                     const gchar *cbm,
                                     GCancellable *cancellable,
                                     GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_CELL_BROADCAST (self), FALSE);

    return mm_gdbus_modem_cell_broadcast_call_delete_sync (MM_GDBUS_MODEM_CELL_BROADCAST (self),
                                                           cbm,
                                                           cancellable,
                                                           error);
}

/*****************************************************************************/

/**
 * mm_modem_cell_broadcast_set_channels_finish
 * @self: A #MMModemCellBroadcast.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_cell_broadcast_set_channels()
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_cell_broadcast_set_channels().
 *
 * Returns: %TRUE if set default storage is success, %FALSE if @error is set.
 *
 * Since: 1.24
 */
gboolean
mm_modem_cell_broadcast_set_channels_finish (MMModemCellBroadcast *self,
                                             GAsyncResult *res,
                                             GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_CELL_BROADCAST (self), FALSE);

    return mm_gdbus_modem_cell_broadcast_call_set_channels_finish (MM_GDBUS_MODEM_CELL_BROADCAST (self),
                                                                   res,
                                                                   error);
}

/**
 * mm_modem_cell_broadcast_set_channels
 * @self: A #MMModemCellBroadcast.
 * @channels: The #MMCellbroadcastChannels to set
 * @n_channels: The number of elements in `channels`
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously set the #MMCellbroadcastChannel s in the modem.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_cell_broadcast_set_channels_finish() to get the result of the operation.
 *
 * See mm_modem_cell_broadcast_set_channels_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.24
 */
void
mm_modem_cell_broadcast_set_channels (MMModemCellBroadcast *self,
                                      const MMCellBroadcastChannels *channels,
                                      guint n_channels,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
    GVariant *channels_variant;

    g_return_if_fail (MM_IS_MODEM_CELL_BROADCAST (self));

    channels_variant = mm_common_cell_broadcast_channels_array_to_variant (channels, n_channels);
    mm_gdbus_modem_cell_broadcast_call_set_channels (MM_GDBUS_MODEM_CELL_BROADCAST (self),
                                                     channels_variant,
                                                     cancellable,
                                                     callback,
                                                     user_data);
}

/**
 * mm_modem_cell_broadcast_set_channels_sync
 * @self: A #MMModemCellBroadcast.
 * @channels: The #MMCellbroadcastChannels to set
 * @n_channels: The number of elements in `channels`
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Asynchronously set the #MMCellbroadcastChannel s in the modem.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_cell_broadcast_set_channels() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.24
 */
gboolean
mm_modem_cell_broadcast_set_channels_sync (MMModemCellBroadcast *self,
                                           const MMCellBroadcastChannels *channels,
                                           guint n_channels,
                                           GCancellable *cancellable,
                                           GError **error)
{
    GVariant *channels_variant;

    g_return_val_if_fail (MM_IS_MODEM_CELL_BROADCAST (self), FALSE);

    channels_variant = mm_common_cell_broadcast_channels_array_to_variant (channels, n_channels);
    return mm_gdbus_modem_cell_broadcast_call_set_channels_sync (MM_GDBUS_MODEM_CELL_BROADCAST (self),
                                                                 channels_variant,
                                                                 cancellable,
                                                                 error);
}

/*****************************************************************************/

static void
mm_modem_cell_broadcast_init (MMModemCellBroadcast *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_MODEM_CELL_BROADCAST, MMModemCellBroadcastPrivate);
    g_mutex_init (&self->priv->mutex);

    PROPERTY_INITIALIZE (channels, "channels")
}

static void
finalize (GObject *object)
{
    MMModemCellBroadcast *self = MM_MODEM_CELL_BROADCAST (object);

    g_mutex_clear (&self->priv->mutex);

    PROPERTY_ARRAY_FINALIZE (channels)

    G_OBJECT_CLASS (mm_modem_cell_broadcast_parent_class)->finalize (object);
}

static void
mm_modem_cell_broadcast_class_init (MMModemCellBroadcastClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModemCellBroadcastPrivate));

    object_class->finalize = finalize;
}
