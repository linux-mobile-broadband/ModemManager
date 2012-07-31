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
 * Copyright (C) 2011 Google, Inc.
 *
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include "ModemManager.h"
#include <mm-gdbus-manager.h>
#include "mm-manager.h"

static void initable_iface_init       (GInitableIface      *iface);
static void async_initable_iface_init (GAsyncInitableIface *iface);

static GInitableIface      *initable_parent_iface;
static GAsyncInitableIface *async_initable_parent_iface;

G_DEFINE_TYPE_EXTENDED (MMManager, mm_manager, MM_GDBUS_TYPE_OBJECT_MANAGER_CLIENT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init));

struct _MMManagerPrivate {
  /* The proxy for the Manager interface */
  MmGdbusOrgFreedesktopModemManager1 *manager_iface_proxy;
};

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
 * See mm_manager_new_sync() for the synchronous, blocking version of this constructor.
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
                                "get-proxy-type-func", mm_gdbus_object_manager_client_get_proxy_type,
                                NULL);
}

/**
 * mm_manager_new_finish:
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_manager_new().
 * @error: Return location for error or %NULL
 *
 * Finishes an operation started with mm_manager_new().
 *
 * Returns: (transfer full) (type MMManager): The constructed object manager client or %NULL if @error is set.
 */
MMManager *
mm_manager_new_finish (GAsyncResult  *res,
                       GError       **error)
{
    return MM_MANAGER (mm_gdbus_object_manager_client_new_finish (res, error));
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
 * Returns: (transfer full) (type MMManager): The constructed object manager client or %NULL if @error is set.
 */
MMManager *
mm_manager_new_sync (GDBusConnection                *connection,
                     GDBusObjectManagerClientFlags   flags,
                     GCancellable                   *cancellable,
                     GError                        **error)
{
    GInitable *ret;

    ret = g_initable_new (MM_TYPE_MANAGER,
                          cancellable,
                          error,
                          "name", MM_DBUS_SERVICE,
                          "object-path", MM_DBUS_PATH,
                          "flags", flags,
                          "connection", connection,
                          "get-proxy-type-func", mm_gdbus_object_manager_client_get_proxy_type,
                          NULL);

    return (ret ? MM_MANAGER (ret) : NULL);
}

/**
 * mm_manager_peek_proxy:
 * @manager: A #MMManager.
 *
 * Gets the #GDBusProxy interface of the %manager.
 *
 * Returns: (transfer none): The #GDBusProxy interface of %manager. Do not free the returned object, it is owned by @manager.
 */
GDBusProxy *
mm_manager_peek_proxy (MMManager *manager)
{
    return G_DBUS_PROXY (manager->priv->manager_iface_proxy);
}

/**
 * mm_manager_get_proxy:
 * @manager: A #MMManager.
 *
 * Gets the #GDBusProxy interface of the %manager.
 *
 * Returns: (transfer full): The #GDBusProxy interface of %manager, which must be freed with g_object_unref().
 */
GDBusProxy *
mm_manager_get_proxy (MMManager *manager)
{
    return G_DBUS_PROXY (g_object_ref (manager->priv->manager_iface_proxy));
}

static void
set_logging_ready (MmGdbusOrgFreedesktopModemManager1 *manager_iface_proxy,
                   GAsyncResult                       *res,
                   GSimpleAsyncResult                 *simple)
{
    GError *error = NULL;

    if (mm_gdbus_org_freedesktop_modem_manager1_call_set_logging_finish (
            manager_iface_proxy,
            res,
            &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_manager_set_logging (MMManager           *manager,
                        const gchar         *level,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (manager),
                                        callback,
                                        user_data,
                                        mm_manager_set_logging);

    mm_gdbus_org_freedesktop_modem_manager1_call_set_logging (
        manager->priv->manager_iface_proxy,
        level,
        cancellable,
        (GAsyncReadyCallback)set_logging_ready,
        result);
}

gboolean
mm_manager_set_logging_finish (MMManager     *manager,
                               GAsyncResult  *res,
                               GError       **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res),
                                               error))
        return FALSE;

    return TRUE;
}

gboolean
mm_manager_set_logging_sync (MMManager     *manager,
                             const gchar   *level,
                             GCancellable  *cancellable,
                             GError       **error)
{
    return (mm_gdbus_org_freedesktop_modem_manager1_call_set_logging_sync (
                manager->priv->manager_iface_proxy,
                level,
                cancellable,
                error));
}

static void
scan_devices_ready (MmGdbusOrgFreedesktopModemManager1 *manager_iface_proxy,
                    GAsyncResult                       *res,
                    GSimpleAsyncResult                 *simple)
{
    GError *error = NULL;

    if (mm_gdbus_org_freedesktop_modem_manager1_call_scan_devices_finish (
            manager_iface_proxy,
            res,
            &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

/**
 * mm_manager_scan_devices:
 * @manager: A #MMManager.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Requests to scan looking for devices.
 *
 * Asynchronously invokes the <link linkend="gdbus-method-org-freedesktop-ModemManager1.ScanDevices">ScanDevices()</link>
 * D-Bus method on @manager. When the operation is finished, @callback will be
 * invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_manager_scan_devices_finish() to get the result of the operation.
 *
 * See mm_manager_scan_devices_sync() for the synchronous, blocking version of this method.
 */
void
mm_manager_scan_devices (MMManager           *manager,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (manager),
                                        callback,
                                        user_data,
                                        mm_manager_scan_devices);

    mm_gdbus_org_freedesktop_modem_manager1_call_scan_devices (
        manager->priv->manager_iface_proxy,
        cancellable,
        (GAsyncReadyCallback)scan_devices_ready,
        result);
}

/**
 * mm_manager_scan_devices_finish:
 * @manager: A #MMManager.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_manager_scan_devices().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_manager_scan_devices().
 *
 * Returns: (skip): %TRUE if the call succeded, %FALSE if @error is set.
 */
gboolean
mm_manager_scan_devices_finish (MMManager     *manager,
                                GAsyncResult  *res,
                                GError       **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res),
                                               error))
        return FALSE;

    return TRUE;
}

/**
 * mm_manager_scan_devices_sync:
 * @manager: A #MMManager.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Requests to scan looking for devices.
 *
 * Synchronously invokes the <link linkend="gdbus-method-org-freedesktop-ModemManager1.ScanDevices">ScanDevices()</link>
 * D-Bus method on @manager. The calling thread is blocked until a reply is received.
 *
 * See mm_gdbus_org_freedesktop_modem_manager1_call_scan_devices() for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the call succeded, %FALSE if @error is set.
 */
gboolean
mm_manager_scan_devices_sync (MMManager     *manager,
                              GCancellable  *cancellable,
                              GError       **error)
{
    return (mm_gdbus_org_freedesktop_modem_manager1_call_scan_devices_sync (
                manager->priv->manager_iface_proxy,
                cancellable,
                error));
}

static gboolean
initable_init_sync (GInitable     *initable,
                    GCancellable  *cancellable,
                    GError       **error)
{
    MMManagerPrivate *priv = MM_MANAGER (initable)->priv;
	GError *inner_error = NULL;
    gchar *name = NULL;
    gchar *object_path = NULL;
    GDBusObjectManagerClientFlags flags = G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE;
    GDBusConnection *connection = NULL;

	/* Chain up parent's initable callback before calling child's one */
	if (!initable_parent_iface->init (initable, cancellable, &inner_error)) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

    /* Get the Manager proxy created synchronously now */
    g_object_get (initable,
                  "name", &name,
                  "object-path", &object_path,
                  "flags", &flags,
                  "connection", &connection,
                  NULL);
    priv->manager_iface_proxy =
        mm_gdbus_org_freedesktop_modem_manager1_proxy_new_sync (connection,
                                                                flags,
                                                                name,
                                                                object_path,
                                                                cancellable,
                                                                error);
    g_object_unref (connection);
    g_free (object_path);
    g_free (name);

    if (!priv->manager_iface_proxy) {
		g_propagate_error (error, inner_error);
		return FALSE;
    }

    /* All good */
	return TRUE;
}

typedef struct {
    GSimpleAsyncResult *result;
    int io_priority;
    GCancellable *cancellable;
    MMManager *manager;
} InitAsyncContext;

static void
init_async_context_free (InitAsyncContext *ctx)
{
    g_object_unref (ctx->manager);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_free (ctx);
}

static gboolean
initable_init_finish (GAsyncInitable  *initable,
                      GAsyncResult    *result,
                      GError         **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
        return FALSE;

    return TRUE;
}

static void
initable_init_async_manager_proxy_ready (GObject      *source,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
    GError *inner_error = NULL;
    InitAsyncContext *ctx = user_data;
    MMManagerPrivate *priv = ctx->manager->priv;

    priv->manager_iface_proxy =
        mm_gdbus_org_freedesktop_modem_manager1_proxy_new_finish (res,
                                                                  &inner_error);
    if (!priv->manager_iface_proxy)
        g_simple_async_result_take_error (ctx->result, inner_error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);

    g_simple_async_result_complete (ctx->result);
    init_async_context_free (ctx);
}

static void
initable_init_async_parent_ready (GObject      *source,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
    GError *inner_error = NULL;
    InitAsyncContext *ctx = user_data;
    gchar *name = NULL;
    gchar *object_path = NULL;
    GDBusObjectManagerClientFlags flags = G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE;
    GDBusConnection *connection = NULL;

    /* Parent init ready, check it */
    if (!async_initable_parent_iface->init_finish (G_ASYNC_INITABLE (source),
                                                   res,
                                                   &inner_error)) {
        g_simple_async_result_take_error (ctx->result, inner_error);
        g_simple_async_result_complete (ctx->result);
        init_async_context_free (ctx);
        return;
    }

    /* Get the Manager proxy created asynchronously now */
    g_object_get (ctx->manager,
                  "name", &name,
                  "object-path", &object_path,
                  "flags", &flags,
                  "connection", &connection,
                  NULL);
    mm_gdbus_org_freedesktop_modem_manager1_proxy_new (connection,
                                                       flags,
                                                       name,
                                                       object_path,
                                                       ctx->cancellable,
                                                       initable_init_async_manager_proxy_ready,
                                                       ctx);
    g_object_unref (connection);
    g_free (object_path);
    g_free (name);
}

static void
initable_init_async (GAsyncInitable      *initable,
                     int                  io_priority,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
    InitAsyncContext *ctx;

    ctx = g_new (InitAsyncContext, 1);
    ctx->manager = g_object_ref (initable);
    ctx->result = g_simple_async_result_new (G_OBJECT (initable),
                                             callback,
                                             user_data,
                                             initable_init_async);
    ctx->cancellable = (cancellable ?
                        g_object_ref (cancellable) :
                        NULL);
    ctx->io_priority = io_priority;

	/* Chain up parent's initable callback before calling child's one */
    async_initable_parent_iface->init_async (initable,
                                             io_priority,
                                             cancellable,
                                             initable_init_async_parent_ready,
                                             ctx);
}

static void
mm_manager_init (MMManager *manager)
{
    /* Setup private data */
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE ((manager),
                                                 MM_TYPE_MANAGER,
                                                 MMManagerPrivate);
}

static void
finalize (GObject *object)
{
    MMManagerPrivate *priv = MM_MANAGER (object)->priv;

    if (priv->manager_iface_proxy)
        g_object_unref (priv->manager_iface_proxy);

    G_OBJECT_CLASS (mm_manager_parent_class)->finalize (object);
}

static void
initable_iface_init (GInitableIface *iface)
{
    initable_parent_iface = g_type_interface_peek_parent (iface);
	iface->init = initable_init_sync;
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
    async_initable_parent_iface = g_type_interface_peek_parent (iface);
	iface->init_async = initable_init_async;
	iface->init_finish = initable_init_finish;
}

static void
mm_manager_class_init (MMManagerClass *manager_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

    g_type_class_add_private (object_class, sizeof (MMManagerPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}
