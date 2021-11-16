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
 * Copyright (C) 2011 - 2012 Google, Inc.
 * Copyright (C) 2011 - 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <ModemManager.h>

#include "mm-helpers.h"
#include "mm-common-helpers.h"
#include "mm-errors-types.h"
#include "mm-gdbus-manager.h"
#include "mm-manager.h"
#include "mm-object.h"

/**
 * SECTION: mm-manager
 * @title: MMManager
 * @short_description: The Manager object
 *
 * The #MMManager is the object allowing access to the Manager interface.
 *
 * This object is also a #GDBusObjectManagerClient, and therefore it allows to
 * use the standard ObjectManager interface to list and handle the managed
 * modem objects.
 */

G_DEFINE_TYPE (MMManager, mm_manager, MM_GDBUS_TYPE_OBJECT_MANAGER_CLIENT)

struct _MMManagerPrivate {
  /* The proxy for the Manager interface */
  MmGdbusOrgFreedesktopModemManager1 *manager_iface_proxy;
};

/*****************************************************************************/

static GType
get_proxy_type (GDBusObjectManagerClient *manager,
                const gchar *object_path,
                const gchar *interface_name,
                gpointer user_data)
{
    static gsize once_init_value = 0;
    static GHashTable *lookup_hash;
    GType ret;

    if (interface_name == NULL)
        return MM_TYPE_OBJECT;

    if (g_once_init_enter (&once_init_value)) {
        lookup_hash = g_hash_table_new (g_str_hash, g_str_equal);
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem",                          GSIZE_TO_POINTER (MM_TYPE_MODEM));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Messaging",                GSIZE_TO_POINTER (MM_TYPE_MODEM_MESSAGING));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Voice",                    GSIZE_TO_POINTER (MM_TYPE_MODEM_VOICE));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Location",                 GSIZE_TO_POINTER (MM_TYPE_MODEM_LOCATION));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Time",                     GSIZE_TO_POINTER (MM_TYPE_MODEM_TIME));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Sar",                      GSIZE_TO_POINTER (MM_TYPE_MODEM_SAR));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Signal",                   GSIZE_TO_POINTER (MM_TYPE_MODEM_SIGNAL));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Firmware",                 GSIZE_TO_POINTER (MM_TYPE_MODEM_FIRMWARE));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Oma",                      GSIZE_TO_POINTER (MM_TYPE_MODEM_OMA));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.ModemCdma",                GSIZE_TO_POINTER (MM_TYPE_MODEM_CDMA));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Modem3gpp",                GSIZE_TO_POINTER (MM_TYPE_MODEM_3GPP));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Modem3gpp.ProfileManager", GSIZE_TO_POINTER (MM_TYPE_MODEM_3GPP_PROFILE_MANAGER));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd",           GSIZE_TO_POINTER (MM_TYPE_MODEM_3GPP_USSD));
        g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.ModemManager1.Modem.Simple",                   GSIZE_TO_POINTER (MM_TYPE_MODEM_SIMPLE));
        g_once_init_leave (&once_init_value, 1);
    }

    ret = (GType) GPOINTER_TO_SIZE (g_hash_table_lookup (lookup_hash, interface_name));
    if (ret == (GType) 0)
        ret = G_TYPE_DBUS_PROXY;
    return ret;
}

/*****************************************************************************/

static void
cleanup_modem_manager1_proxy (MMManager *self)
{
    if (self->priv->manager_iface_proxy) {
        g_signal_handlers_disconnect_by_func (self, cleanup_modem_manager1_proxy, NULL);
        g_clear_object (&self->priv->manager_iface_proxy);
    }
}

static gboolean
ensure_modem_manager1_proxy (MMManager  *self,
                             GError    **error)
{
    gchar *name = NULL;
    gchar *object_path = NULL;
    GDBusObjectManagerClientFlags obj_manager_flags = G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE;
    GDBusProxyFlags proxy_flags = G_DBUS_PROXY_FLAGS_NONE;
    GDBusConnection *connection = NULL;

    if (self->priv->manager_iface_proxy)
        return TRUE;

    /* Get the Manager proxy created synchronously now */
    g_object_get (self,
                  "name",        &name,
                  "object-path", &object_path,
                  "flags",       &obj_manager_flags,
                  "connection",  &connection,
                  NULL);

    if (obj_manager_flags & G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START)
        proxy_flags |= G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;

    self->priv->manager_iface_proxy =
        mm_gdbus_org_freedesktop_modem_manager1_proxy_new_sync (connection,
                                                                proxy_flags,
                                                                name,
                                                                object_path,
                                                                NULL,
                                                                error);
    g_object_unref (connection);
    g_free (object_path);
    g_free (name);

    if (self->priv->manager_iface_proxy)
        g_signal_connect (self,
                          "notify::name-owner",
                          G_CALLBACK (cleanup_modem_manager1_proxy),
                          NULL);

    return !!self->priv->manager_iface_proxy;
}

/*****************************************************************************/

/**
 * mm_manager_new_finish:
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_manager_new().
 * @error: Return location for error or %NULL
 *
 * Finishes an operation started with mm_manager_new().
 *
 * Returns: (transfer full) (type MMManager): The constructed object manager
 * client or %NULL if @error is set.
 *
 * Since: 1.0
 */
MMManager *
mm_manager_new_finish (GAsyncResult  *res,
                       GError       **error)
{
    GObject *ret;
    GObject *source_object;

    source_object = g_async_result_get_source_object (res);
    ret = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, error);
    g_object_unref (source_object);
    return MM_MANAGER (ret);
}

/**
 * mm_manager_new:
 * @connection: A #GDBusConnection.
 * @flags: Flags from the #GDBusObjectManagerClientFlags enumeration.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously creates a #MMManager.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from.
 *
 * You can then call mm_manager_new_finish() to get the result of the operation.
 *
 * See mm_manager_new_sync() for the synchronous, blocking version of this
 * constructor.
 *
 * Since: 1.0
 */
void
mm_manager_new (GDBusConnection               *connection,
                GDBusObjectManagerClientFlags  flags,
                GCancellable                  *cancellable,
                GAsyncReadyCallback            callback,
                gpointer                       user_data)
{
    g_async_initable_new_async (MM_TYPE_MANAGER,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                callback,
                                user_data,
                                "name", MM_DBUS_SERVICE,
                                "object-path", MM_DBUS_PATH,
                                "flags", flags,
                                "connection", connection,
                                "get-proxy-type-func", get_proxy_type,
                                NULL);
}

/**
 * mm_manager_new_sync:
 * @connection: A #GDBusConnection.
 * @flags: Flags from the #GDBusObjectManagerClientFlags enumeration.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL
 *
 * Synchronously creates a #MMManager.
 *
 * The calling thread is blocked until a reply is received.
 *
 * See mm_manager_new() for the asynchronous version of this constructor.
 *
 * Returns: (transfer full) (type MMManager): The constructed object manager
 * client or %NULL if @error is set.
 *
 * Since: 1.0
 */
MMManager *
mm_manager_new_sync (GDBusConnection                *connection,
                     GDBusObjectManagerClientFlags   flags,
                     GCancellable                   *cancellable,
                     GError                        **error)
{
    return MM_MANAGER (g_initable_new (MM_TYPE_MANAGER,
                                       cancellable,
                                       error,
                                       "name", MM_DBUS_SERVICE,
                                       "object-path", MM_DBUS_PATH,
                                       "flags", flags,
                                       "connection", connection,
                                       "get-proxy-type-func", get_proxy_type,
                                       NULL));
}

/*****************************************************************************/

/**
 * mm_manager_peek_proxy:
 * @manager: A #MMManager.
 *
 * Gets the #GDBusProxy interface of the @manager.
 *
 * Returns: (transfer none): The #GDBusProxy interface of @manager, or #NULL if
 * none. Do not free the returned object, it is owned by @manager.
 *
 * Since: 1.0
 */
GDBusProxy *
mm_manager_peek_proxy (MMManager *manager)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), NULL);

    if (!ensure_modem_manager1_proxy (manager, NULL))
        return NULL;

    return G_DBUS_PROXY (manager->priv->manager_iface_proxy);
}

/**
 * mm_manager_get_proxy:
 * @manager: A #MMManager.
 *
 * Gets the #GDBusProxy interface of the @manager.
 *
 * Returns: (transfer full): The #GDBusProxy interface of @manager, or #NULL if
 * none. The returned object must be freed with g_object_unref().
 *
 * Since: 1.0
 */
GDBusProxy *
mm_manager_get_proxy (MMManager *manager)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), NULL);

    if (!ensure_modem_manager1_proxy (manager, NULL))
        return NULL;

    return G_DBUS_PROXY (g_object_ref (manager->priv->manager_iface_proxy));
}

/*****************************************************************************/

/**
 * mm_manager_get_version:
 * @manager: A #MMManager.
 *
 * Gets the ModemManager version, as reported by the daemon.
 *
 * It is safe to assume this value never changes during runtime.
 *
 * Returns: (transfer none): The version, or %NULL if none available. Do not
 * free the returned value, it belongs to @self.
 *
 * Since: 1.0
 */
const gchar *
mm_manager_get_version (MMManager *manager)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), NULL);

    if (!ensure_modem_manager1_proxy (manager, NULL))
        return NULL;

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_org_freedesktop_modem_manager1_get_version (manager->priv->manager_iface_proxy));
}

/*****************************************************************************/

/**
 * mm_manager_set_logging_finish:
 * @manager: A #MMManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_manager_set_logging().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_manager_set_logging().
 *
 * Returns: %TRUE if the call succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_manager_set_logging_finish (MMManager     *manager,
                               GAsyncResult  *res,
                               GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_logging_ready (MmGdbusOrgFreedesktopModemManager1 *manager_iface_proxy,
                   GAsyncResult                       *res,
                   GTask                              *task)
{
    GError *error = NULL;

    if (!mm_gdbus_org_freedesktop_modem_manager1_call_set_logging_finish (
            manager_iface_proxy,
            res,
            &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

/**
 * mm_manager_set_logging:
 * @manager: A #MMManager.
 * @level: the login level to set.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to set the specified logging level in the daemon.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_manager_set_logging_finish() to get the result of the operation.
 *
 * See mm_manager_set_logging_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_manager_set_logging (MMManager           *manager,
                        const gchar         *level,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    GTask *task;
    GError *inner_error = NULL;

    g_return_if_fail (MM_IS_MANAGER (manager));

    task = g_task_new (manager, cancellable, callback, user_data);

    if (!ensure_modem_manager1_proxy (manager, &inner_error)) {
        g_task_return_error (task, inner_error);
        g_object_unref (task);
        return;
    }

    mm_gdbus_org_freedesktop_modem_manager1_call_set_logging (
        manager->priv->manager_iface_proxy,
        level,
        cancellable,
        (GAsyncReadyCallback)set_logging_ready,
        task);
}

/**
 * mm_manager_set_logging_sync:
 * @manager: A #MMManager.
 * @level: the login level to set.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to set the specified logging level in the daemon.
 *
 * The calling thread is blocked until a reply is received.
 *
 * See mm_manager_set_logging() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the call succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_manager_set_logging_sync (MMManager     *manager,
                             const gchar   *level,
                             GCancellable  *cancellable,
                             GError       **error)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), FALSE);

    if (!ensure_modem_manager1_proxy (manager, error))
        return FALSE;

    return (mm_gdbus_org_freedesktop_modem_manager1_call_set_logging_sync (
                manager->priv->manager_iface_proxy,
                level,
                cancellable,
                error));
}

/*****************************************************************************/

/**
 * mm_manager_scan_devices_finish:
 * @manager: A #MMManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_manager_scan_devices().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_manager_scan_devices().
 *
 * Returns: %TRUE if the call succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_manager_scan_devices_finish (MMManager     *manager,
                                GAsyncResult  *res,
                                GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
scan_devices_ready (MmGdbusOrgFreedesktopModemManager1 *manager_iface_proxy,
                    GAsyncResult                       *res,
                    GTask                              *task)
{
    GError *error = NULL;

    if (!mm_gdbus_org_freedesktop_modem_manager1_call_scan_devices_finish (
            manager_iface_proxy,
            res,
            &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

/**
 * mm_manager_scan_devices:
 * @manager: A #MMManager.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to scan looking for devices.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_manager_scan_devices_finish() to get the result of the operation.
 *
 * See mm_manager_scan_devices_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
 */
void
mm_manager_scan_devices (MMManager           *manager,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    GTask *task;
    GError *inner_error = NULL;

    g_return_if_fail (MM_IS_MANAGER (manager));

    task = g_task_new (manager, cancellable, callback, user_data);

    if (!ensure_modem_manager1_proxy (manager, &inner_error)) {
        g_task_return_error (task, inner_error);
        g_object_unref (task);
        return;
    }

    mm_gdbus_org_freedesktop_modem_manager1_call_scan_devices (
        manager->priv->manager_iface_proxy,
        cancellable,
        (GAsyncReadyCallback)scan_devices_ready,
        task);
}

/**
 * mm_manager_scan_devices_sync:
 * @manager: A #MMManager.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to scan looking for devices.
 *
 * The calling thread is blocked until a reply is received.
 *
 * See mm_manager_scan_devices() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the call succeeded, %FALSE if @error is set.
 *
 * Since: 1.0
 */
gboolean
mm_manager_scan_devices_sync (MMManager     *manager,
                              GCancellable  *cancellable,
                              GError       **error)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), FALSE);

    if (!ensure_modem_manager1_proxy (manager, error))
        return FALSE;

    return (mm_gdbus_org_freedesktop_modem_manager1_call_scan_devices_sync (
                manager->priv->manager_iface_proxy,
                cancellable,
                error));
}

/*****************************************************************************/

/**
 * mm_manager_report_kernel_event_finish:
 * @manager: A #MMManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_manager_report_kernel_event().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_manager_report_kernel_event().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.8
 */
gboolean
mm_manager_report_kernel_event_finish (MMManager     *manager,
                                       GAsyncResult  *res,
                                       GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
report_kernel_event_ready (MmGdbusOrgFreedesktopModemManager1 *manager_iface_proxy,
                           GAsyncResult                       *res,
                           GTask                              *task)
{
    GError *error = NULL;

    if (!mm_gdbus_org_freedesktop_modem_manager1_call_report_kernel_event_finish (
            manager_iface_proxy,
            res,
            &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

/**
 * mm_manager_report_kernel_event:
 * @manager: A #MMManager.
 * @properties: the properties of the kernel event.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously report kernel event.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_manager_report_kernel_event_finish() to get the result of the operation.
 *
 * See mm_manager_report_kernel_event_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.8
 */
void
mm_manager_report_kernel_event (MMManager                *manager,
                                MMKernelEventProperties  *properties,
                                GCancellable             *cancellable,
                                GAsyncReadyCallback       callback,
                                gpointer                  user_data)
{
    GTask    *task;
    GError   *inner_error = NULL;
    GVariant *dictionary;

    g_return_if_fail (MM_IS_MANAGER (manager));

    task = g_task_new (manager, cancellable, callback, user_data);

    if (!ensure_modem_manager1_proxy (manager, &inner_error)) {
        g_task_return_error (task, inner_error);
        g_object_unref (task);
        return;
    }

    dictionary = mm_kernel_event_properties_get_dictionary (properties);
    mm_gdbus_org_freedesktop_modem_manager1_call_report_kernel_event (
        manager->priv->manager_iface_proxy,
        dictionary,
        cancellable,
        (GAsyncReadyCallback)report_kernel_event_ready,
        task);
    g_variant_unref (dictionary);
}

/**
 * mm_manager_report_kernel_event_sync:
 * @manager: A #MMManager.
 * @properties: the properties of the kernel event.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously report kernel event.
 *
 * The calling thread is blocked until a reply is received.
 *
 * See mm_manager_report_kernel_event() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 1.8
 */
gboolean
mm_manager_report_kernel_event_sync (MMManager                *manager,
                                     MMKernelEventProperties  *properties,
                                     GCancellable             *cancellable,
                                     GError                  **error)
{
    GVariant *dictionary;
    gboolean  result;

    g_return_val_if_fail (MM_IS_MANAGER (manager), FALSE);

    if (!ensure_modem_manager1_proxy (manager, error))
        return FALSE;

    dictionary = mm_kernel_event_properties_get_dictionary (properties);
    result = (mm_gdbus_org_freedesktop_modem_manager1_call_report_kernel_event_sync (
                  manager->priv->manager_iface_proxy,
                  dictionary,
                  cancellable,
                  error));
    g_variant_unref (dictionary);
    return result;
}

/*****************************************************************************/

static gboolean
common_inhibit_device_finish (MMManager     *manager,
                              GAsyncResult  *res,
                              GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
inhibit_ready (MmGdbusOrgFreedesktopModemManager1 *manager_iface_proxy,
               GAsyncResult                       *res,
               GTask                              *task)
{
    GError *error = NULL;

    if (!mm_gdbus_org_freedesktop_modem_manager1_call_inhibit_device_finish (manager_iface_proxy, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
common_inhibit_device (MMManager           *manager,
                       const gchar         *uid,
                       gboolean             inhibit,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
    GTask  *task;
    GError *inner_error = NULL;

    task = g_task_new (manager, cancellable, callback, user_data);

    if (!ensure_modem_manager1_proxy (manager, &inner_error)) {
        g_task_return_error (task, inner_error);
        g_object_unref (task);
        return;
    }

    mm_gdbus_org_freedesktop_modem_manager1_call_inhibit_device (
        manager->priv->manager_iface_proxy,
        uid,
        inhibit,
        cancellable,
        (GAsyncReadyCallback)inhibit_ready,
        task);
}

static gboolean
common_inhibit_device_sync (MMManager     *manager,
                            const gchar   *uid,
                            gboolean       inhibit,
                            GCancellable  *cancellable,
                            GError       **error)
{
    if (!ensure_modem_manager1_proxy (manager, error))
        return FALSE;

    return (mm_gdbus_org_freedesktop_modem_manager1_call_inhibit_device_sync (
                manager->priv->manager_iface_proxy,
                uid,
                inhibit,
                cancellable,
                error));
}

/**
 * mm_manager_inhibit_device_finish:
 * @manager: A #MMManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_manager_inhibit_device().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_manager_inhibit_device().
 *
 * Returns: %TRUE if the call succeeded, %FALSE if @error is set.
 *
 * Since: 1.10
 */
gboolean
mm_manager_inhibit_device_finish (MMManager     *manager,
                                  GAsyncResult  *res,
                                  GError       **error)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), FALSE);
    return common_inhibit_device_finish (manager, res, error);
}

/**
 * mm_manager_inhibit_device:
 * @manager: A #MMManager.
 * @uid: the unique ID of the physical device.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to add an inhibition on the device identified by
 * @uid.
 *
 * The @uid must be the unique ID retrieved from an existing #MMModem using
 * mm_modem_get_device(). The caller should keep track of this @uid and use it
 * in the mm_manager_uninhibit_device() call when the inhibition is no longer
 * required.
 *
 * The inhibition added with this method may also be automatically removed when
 * the caller program disappears from the bus (e.g. if the program ends before
 * having called mm_manager_uninhibit_device() explicitly).
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_manager_inhibit_device_finish() to get the result of the operation.
 *
 * See mm_manager_inhibit_device_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.10
 */
void
mm_manager_inhibit_device (MMManager           *manager,
                           const gchar         *uid,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    g_return_if_fail (MM_IS_MANAGER (manager));
    common_inhibit_device (manager, uid, TRUE, cancellable, callback, user_data);
}

/**
 * mm_manager_inhibit_device_sync:
 * @manager: A #MMManager.
 * @uid: the unique ID of the physical device.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to add an inhibition on the device identified by @uid.
 *
 * The @uid must be the unique ID retrieved from an existing #MMModem using
 * mm_modem_get_device(). The caller should keep track of this @uid and use it
 * in the mm_manager_uninhibit_device_sync() call when the inhibition is no
 * longer required.
 *
 * The inhibition added with this method may also be automatically removed when
 * the caller program disappears from the bus (e.g. if the program ends before
 * having called mm_manager_uninhibit_device_sync() explicitly).
 *
 * See mm_manager_inhibit_device() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the call succeeded, %FALSE if @error is set.
 *
 * Since: 1.10
 */
gboolean
mm_manager_inhibit_device_sync (MMManager     *manager,
                                const gchar   *uid,
                                GCancellable  *cancellable,
                                GError       **error)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), FALSE);
    return common_inhibit_device_sync (manager, uid, TRUE, cancellable, error);
}

/**
 * mm_manager_uninhibit_device_finish:
 * @manager: A #MMManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_manager_uninhibit_device().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_manager_uninhibit_device().
 *
 * Returns: %TRUE if the call succeeded, %FALSE if @error is set.
 *
 * Since: 1.10
 */
gboolean
mm_manager_uninhibit_device_finish (MMManager     *manager,
                                    GAsyncResult  *res,
                                    GError       **error)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), FALSE);
    return common_inhibit_device_finish (manager, res, error);
}

/**
 * mm_manager_uninhibit_device:
 * @manager: A #MMManager.
 * @uid: the unique ID of the physical device.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to remove an inhibition on the device identified by
 * @uid.
 *
 * The @uid must be the same unique ID that was sent in the inhibition request.
 *
 * Only the same program that placed an inhibition on a given device is able to
 * remove the inhibition.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_manager_uninhibit_device_finish() to get the result of the operation.
 *
 * See mm_manager_uninhibit_device_sync() for the synchronous, blocking version
 * of this method.
 *
 * Since: 1.10
 */
void
mm_manager_uninhibit_device (MMManager           *manager,
                             const gchar         *uid,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
    g_return_if_fail (MM_IS_MANAGER (manager));
    common_inhibit_device (manager, uid, FALSE, cancellable, callback, user_data);
}

/**
 * mm_manager_uninhibit_device_sync:
 * @manager: A #MMManager.
 * @uid: the unique ID of the physical device.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to remove an inhibition on the device identified by
 * @uid.
 *
 * The @uid must be the same unique ID that was sent in the inhibition request.
 *
 * Only the same program that placed an inhibition on a given device is able to
 * remove the inhibition.
 *
 * See mm_manager_uninhibit_device() for the asynchronous version of this
 * method.
 *
 * Returns: %TRUE if the call succeeded, %FALSE if @error is set.
 *
 * Since: 1.10
 */
gboolean
mm_manager_uninhibit_device_sync (MMManager     *manager,
                                  const gchar   *uid,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
    g_return_val_if_fail (MM_IS_MANAGER (manager), FALSE);
    return common_inhibit_device_sync (manager, uid, FALSE, cancellable, error);
}

/*****************************************************************************/

static void
mm_manager_init (MMManager *manager)
{
    mm_common_register_errors ();

    /* Setup private data */
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
                                                 MM_TYPE_MANAGER,
                                                 MMManagerPrivate);
}

static void
dispose (GObject *object)
{
    MMManager *self = MM_MANAGER (object);

    g_clear_object (&self->priv->manager_iface_proxy);

    G_OBJECT_CLASS (mm_manager_parent_class)->dispose (object);
}

static void
mm_manager_class_init (MMManagerClass *manager_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

    g_type_class_add_private (object_class, sizeof (MMManagerPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
}
