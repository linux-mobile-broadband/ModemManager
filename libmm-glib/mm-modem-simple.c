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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-modem-simple.h"

/**
 * mm_modem_simple_get_path:
 * @self: A #MMModemSimple.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_simple_get_path (MMModemSimple *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_simple_dup_path:
 * @self: A #MMModemSimple.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_simple_dup_path (MMModemSimple *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_slice_free (ConnectContext, ctx);
}

static GVariant *
connect_build_properties (const gchar *first_property_name,
                          va_list var_args)
{
    const gchar *key;
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    key = first_property_name;
    while (key) {
        if (g_str_equal (key, MM_SIMPLE_PROPERTY_PIN) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_OPERATOR_ID) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_APN) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_IP_TYPE) ||
            g_str_equal (key, MM_SIMPLE_PROPERTY_NUMBER)) {
            const gchar *value;

            /* If a key with NULL value is given, just ignore it. */
            value = va_arg (var_args, gchar *);
            if (value)
                g_variant_builder_add (&builder, "{sv}", key, g_variant_new_string (value));
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_ALLOW_ROAMING)) {
            gboolean value;

            value = va_arg (var_args, gboolean);
            g_variant_builder_add (&builder, "{sv}", key, g_variant_new_boolean (value));
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_ALLOWED_BANDS)) {
            guint64 value;

            value = va_arg (var_args, guint64);
            g_variant_builder_add (&builder, "{sv}", key, g_variant_new_uint64 (value));
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_ALLOWED_MODES) ||
                   g_str_equal (key, MM_SIMPLE_PROPERTY_PREFERRED_MODE)) {
            guint32 value;

            value = va_arg (var_args, guint32);
            g_variant_builder_add (&builder, "{sv}", key, g_variant_new_uint32 (value));
        } else {
            g_warning ("Unexpected key '%s'", key);
            /* ignore value */
            va_arg (var_args, gpointer);
        }

        key = va_arg (var_args, gchar *);
    }

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

MMBearer *
mm_modem_simple_connect_finish (MMModemSimple *self,
                                GAsyncResult *res,
                                GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_SIMPLE (self), NULL);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void
new_bearer_ready (GDBusConnection *connection,
                  GAsyncResult *res,
                  ConnectContext *ctx)
{
    GError *error = NULL;
    MMBearer *bearer;

    bearer = mm_gdbus_bearer_proxy_new_finish (res, &error);
    if (error)
        g_simple_async_result_take_error (ctx->result, error);
    else
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);

    connect_context_complete_and_free (ctx);
}

static void
simple_connect_ready (MMModemSimple *self,
                      GAsyncResult *res,
                      ConnectContext *ctx)
{
    GError *error = NULL;
    gchar *bearer_path = NULL;

    if (!mm_gdbus_modem_simple_call_connect_finish (self,
                                                    &bearer_path,
                                                    res,
                                                    &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    mm_gdbus_bearer_proxy_new (
        g_dbus_proxy_get_connection (
            G_DBUS_PROXY (self)),
        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        MM_DBUS_SERVICE,
        bearer_path,
        ctx->cancellable,
        (GAsyncReadyCallback)new_bearer_ready,
        ctx);
}

void
mm_modem_simple_connect (MMModemSimple *self,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data,
                         const gchar *first_property_name,
                         ...)
{
    ConnectContext *ctx;
    va_list va_args;
    GVariant *properties;

    g_return_if_fail (MM_GDBUS_IS_MODEM_SIMPLE (self));

    ctx = g_slice_new0 (ConnectContext);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_modem_simple_connect);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);


    va_start (va_args, first_property_name);
    properties = connect_build_properties (first_property_name, va_args);
    mm_gdbus_modem_simple_call_connect (
        self,
        properties,
        cancellable,
        (GAsyncReadyCallback)simple_connect_ready,
        ctx);

    g_variant_unref (properties);
    va_end (va_args);
}

MMBearer *
mm_modem_simple_connect_sync (MMModemSimple *self,
                              GCancellable *cancellable,
                              GError **error,
                              const gchar *first_property_name,
                              ...)
{
    MMBearer *bearer = NULL;
    gchar *bearer_path = NULL;
    va_list va_args;
    GVariant *properties;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    va_start (va_args, first_property_name);
    properties = connect_build_properties (first_property_name, va_args);

    mm_gdbus_modem_simple_call_connect_sync (self,
                                             properties,
                                             &bearer_path,
                                             cancellable,
                                             error);
    if (bearer_path) {
        bearer = mm_gdbus_bearer_proxy_new_sync (
            g_dbus_proxy_get_connection (
                G_DBUS_PROXY (self)),
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
            MM_DBUS_SERVICE,
            bearer_path,
            cancellable,
            error);
        g_free (bearer_path);
    }

    g_variant_unref (properties);
    va_end (va_args);

    return bearer;
}
