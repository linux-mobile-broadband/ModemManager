/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-daemon-enums-types.h"
#include "mm-iface-modem.h"
#include "mm-bearer.h"
#include "mm-base-modem-at.h"
#include "mm-base-modem.h"
#include "mm-utils.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

/* We require up to 20s to get a proper IP when using PPP */
#define MM_BEARER_IP_TIMEOUT_DEFAULT 20

G_DEFINE_TYPE (MMBearer, mm_bearer, MM_GDBUS_TYPE_BEARER_SKELETON);

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_MODEM,
    PROP_STATUS,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBearerPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    /* The modem which owns this BEARER */
    MMBaseModem *modem;
    /* The path where the BEARER object is exported */
    gchar *path;
    /* Status of this bearer */
    MMBearerStatus status;

    /* Cancellable for connect() */
    GCancellable *connect_cancellable;
    /* handler id for the disconnect + cancel connect request */
    gulong disconnect_signal_handler;
};

/*****************************************************************************/

void
mm_bearer_export (MMBearer *self)
{
    static guint id = 0;
    gchar *path;

    path = g_strdup_printf (MM_DBUS_BEARER_PREFIX "/%d", id++);
    g_object_set (self,
                  MM_BEARER_PATH, path,
                  NULL);
    g_free (path);
}

/*****************************************************************************/
/* CONNECT */

gboolean
mm_bearer_connect_finish (MMBearer *self,
                          GAsyncResult *res,
                          GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disconnect_after_cancel_ready (MMBearer *self,
                               GAsyncResult *res)
{
    GError *error = NULL;

    if (!MM_BEARER_GET_CLASS (self)->disconnect_finish (self, res, &error)) {
        mm_warn ("Error disconnecting bearer '%s': '%s'. "
                 "Will assume disconnected anyway.",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
    else
        mm_dbg ("Disconnected bearer '%s'", self->priv->path);

    self->priv->status = MM_BEARER_STATUS_DISCONNECTED;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
}

static void
connect_ready (MMBearer *self,
               GAsyncResult *res,
               GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    gboolean launch_disconnect = FALSE;
    MMPort *data = NULL;
    MMBearerIpConfig *ipv4_config = NULL;
    MMBearerIpConfig *ipv6_config = NULL;

    /* NOTE: connect() implementations *MUST* handle cancellations themselves */
    if (!MM_BEARER_GET_CLASS (self)->connect_finish (self,
                                                     res,
                                                     &data,
                                                     &ipv4_config,
                                                     &ipv6_config,
                                                     &error)) {
        mm_dbg ("Couldn't connect bearer '%s': '%s'",
                self->priv->path,
                error->message);
        if (g_error_matches (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_CANCELLED)) {
            /* Will launch disconnection */
            launch_disconnect = TRUE;
        } else {
            self->priv->status = MM_BEARER_STATUS_DISCONNECTED;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
        }
        g_simple_async_result_take_error (simple, error);
    }
    /* Handle cancellations detected after successful connection */
    else if (g_cancellable_is_cancelled (self->priv->connect_cancellable)) {
        mm_dbg ("Connected bearer '%s', but need to disconnect", self->priv->path);

        g_clear_object (&data);
        g_clear_object (&ipv4_config);
        g_clear_object (&ipv6_config);

        g_simple_async_result_set_error (
            simple,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CANCELLED,
            "Bearer got connected, but had to disconnect after cancellation request");
            launch_disconnect = TRUE;
    }
    else {
        mm_dbg ("Connected bearer '%s'", self->priv->path);

        self->priv->status = MM_BEARER_STATUS_CONNECTED;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);

        /* Update the interface state */
        mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (self), TRUE);
        mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (self), mm_port_get_device (data));
        mm_gdbus_bearer_set_ip4_config (
            MM_GDBUS_BEARER (self),
            mm_bearer_ip_config_get_dictionary (ipv4_config));
        mm_gdbus_bearer_set_ip6_config (
            MM_GDBUS_BEARER (self),
            mm_bearer_ip_config_get_dictionary (ipv6_config));

        g_clear_object (&data);
        g_clear_object (&ipv4_config);
        g_clear_object (&ipv6_config);

        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    if (launch_disconnect) {
        self->priv->status = MM_BEARER_STATUS_DISCONNECTING;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
        MM_BEARER_GET_CLASS (self)->disconnect (
            self,
            (GAsyncReadyCallback)disconnect_after_cancel_ready,
            NULL);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_bearer_connect (MMBearer *self,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_assert (MM_BEARER_GET_CLASS (self)->connect != NULL);
    g_assert (MM_BEARER_GET_CLASS (self)->connect_finish != NULL);

    /* If already connecting, return error, don't allow a second request. */
    if (self->priv->status == MM_BEARER_STATUS_CONNECTING) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_IN_PROGRESS,
            "Bearer already being connected");
        return;
    }

    /* If currently disconnecting, return error, previous operation should
     * finish before allowing to connect again. */
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTING) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Bearer currently being disconnected");
        return;
    }

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_bearer_connect);

    /* If already connected, done */
    if (self->priv->status == MM_BEARER_STATUS_CONNECTED) {
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Connecting! */
    mm_dbg ("Connecting bearer '%s'", self->priv->path);
    self->priv->connect_cancellable = g_cancellable_new ();
    self->priv->status = MM_BEARER_STATUS_CONNECTING;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
    MM_BEARER_GET_CLASS (self)->connect (
        self,
        self->priv->connect_cancellable,
        (GAsyncReadyCallback)connect_ready,
        result);
}

typedef struct {
    MMBearer *self;
    MMBaseModem *modem;
    GDBusMethodInvocation *invocation;
} HandleConnectContext;

static void
handle_connect_context_free (HandleConnectContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_connect_ready (MMBearer *self,
                      GAsyncResult *res,
                      HandleConnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_bearer_connect_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_bearer_complete_connect (MM_GDBUS_BEARER (self), ctx->invocation);

    handle_connect_context_free (ctx);
}

static void
handle_connect_auth_ready (MMBaseModem *modem,
                           GAsyncResult *res,
                           HandleConnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_connect_context_free (ctx);
        return;
    }

    mm_bearer_connect (ctx->self,
                       (GAsyncReadyCallback)handle_connect_ready,
                       ctx);
}

static gboolean
handle_connect (MMBearer *self,
                GDBusMethodInvocation *invocation)
{
    HandleConnectContext *ctx;

    ctx = g_new0 (HandleConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    g_object_get (self,
                  MM_BEARER_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_connect_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* DISCONNECT */

gboolean
mm_bearer_disconnect_finish (MMBearer *self,
                             GAsyncResult *res,
                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disconnect_ready (MMBearer *self,
                  GAsyncResult *res,
                  GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_BEARER_GET_CLASS (self)->disconnect_finish (self, res, &error)) {
        mm_dbg ("Couldn't disconnect bearer '%s'", self->priv->path);
        self->priv->status = MM_BEARER_STATUS_CONNECTED;
        g_simple_async_result_take_error (simple, error);
    }
    else {
        mm_dbg ("Disconnected bearer '%s'", self->priv->path);
        self->priv->status = MM_BEARER_STATUS_DISCONNECTED;

        /* Update the interface state */
        mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (self), FALSE);
        mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (self), NULL);
        mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (self), NULL);
        mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (self), NULL);

        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
status_changed_complete_disconnect (MMBearer *self,
                                    GParamSpec *pspec,
                                    GSimpleAsyncResult *simple)
{
    /* We may get other states here before DISCONNECTED, like DISCONNECTING or
     * even CONNECTED. */
    if (self->priv->status != MM_BEARER_STATUS_DISCONNECTED)
        return;

    mm_dbg ("Disconnected bearer '%s' after cancelling previous connect request",
            self->priv->path);
    g_signal_handler_disconnect (self,
                                 self->priv->disconnect_signal_handler);
    self->priv->disconnect_signal_handler = 0;

    /* Update the interface state */
    mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (self), NULL);

    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_bearer_disconnect (MMBearer *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    GSimpleAsyncResult *simple;

    g_assert (MM_BEARER_GET_CLASS (self)->disconnect != NULL);
    g_assert (MM_BEARER_GET_CLASS (self)->disconnect_finish != NULL);

    simple = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_bearer_disconnect);

    /* If already disconnected, done */
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTED) {
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    /* If already disconnecting, return error, don't allow a second request. */
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTING) {
        g_simple_async_result_set_error (
            simple,
            MM_CORE_ERROR,
            MM_CORE_ERROR_IN_PROGRESS,
            "Bearer already being disconnected");
        g_simple_async_result_complete_in_idle (simple);
        g_object_unref (simple);
        return;
    }

    mm_dbg ("Disconnecting bearer '%s'", self->priv->path);

    /* If currently connecting, try to cancel that operation, and wait to get
     * disconnected. */
    if (self->priv->status == MM_BEARER_STATUS_CONNECTING) {
        /* We MUST ensure that we get to DISCONNECTED */
        g_cancellable_cancel (self->priv->connect_cancellable);
        /* Note that we only allow to remove disconnected bearers, so should
         * be safe to assume that we'll get the signal handler called properly
         */
        self->priv->disconnect_signal_handler =
            g_signal_connect (self,
                              "notify::" MM_BEARER_STATUS,
                              (GCallback)status_changed_complete_disconnect,
                              simple); /* takes ownership */

        return;
    }

    /* Disconnecting! */
    self->priv->status = MM_BEARER_STATUS_DISCONNECTING;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
    MM_BEARER_GET_CLASS (self)->disconnect (
        self,
        (GAsyncReadyCallback)disconnect_ready,
        simple); /* takes ownership */
}

typedef struct {
    MMBearer *self;
    MMBaseModem *modem;
    GDBusMethodInvocation *invocation;
} HandleDisconnectContext;

static void
handle_disconnect_context_free (HandleDisconnectContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_disconnect_ready (MMBearer *self,
                         GAsyncResult *res,
                         HandleDisconnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_bearer_disconnect_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_bearer_complete_disconnect (MM_GDBUS_BEARER (self), ctx->invocation);

    handle_disconnect_context_free (ctx);
}

static void
handle_disconnect_auth_ready (MMBaseModem *modem,
                              GAsyncResult *res,
                              HandleDisconnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (modem, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_disconnect_context_free (ctx);
        return;
    }

    mm_bearer_disconnect (ctx->self,
                          (GAsyncReadyCallback)handle_disconnect_ready,
                          ctx);
}

static gboolean
handle_disconnect (MMBearer *self,
                   GDBusMethodInvocation *invocation)
{
    HandleDisconnectContext *ctx;

    ctx = g_new0 (HandleDisconnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->invocation = g_object_ref (invocation);
    g_object_get (self,
                  MM_BEARER_MODEM, &ctx->modem,
                  NULL);

    mm_base_modem_authorize (ctx->modem,
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_disconnect_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static void
mm_bearer_dbus_export (MMBearer *self)
{
    GError *error = NULL;

    /* Handle method invocations */
    g_signal_connect (self,
                      "handle-connect",
                      G_CALLBACK (handle_connect),
                      NULL);
    g_signal_connect (self,
                      "handle-disconnect",
                      G_CALLBACK (handle_disconnect),
                      NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                           self->priv->connection,
                                           self->priv->path,
                                           &error)) {
        mm_warn ("couldn't export BEARER at '%s': '%s'",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
}

static void
mm_bearer_dbus_unexport (MMBearer *self)
{
    const gchar *path;

    path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self));
    /* Only unexport if currently exported */
    if (path) {
        mm_dbg ("Removing from DBus bearer at '%s'", path);
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));
    }
}

/*****************************************************************************/

MMBearerStatus
mm_bearer_get_status (MMBearer *self)
{
    return self->priv->status;
}

const gchar *
mm_bearer_get_path (MMBearer *self)
{
    return self->priv->path;
}

/*****************************************************************************/

static void
disconnect_force_ready (MMBearer *self,
                        GAsyncResult *res)
{
    GError *error = NULL;

    if (!MM_BEARER_GET_CLASS (self)->disconnect_finish (self, res, &error)) {
        mm_warn ("Error disconnecting bearer '%s': '%s'. "
                 "Will assume disconnected anyway.",
                 self->priv->path,
                 error->message);
        g_error_free (error);
    }
    else
        mm_dbg ("Disconnected bearer '%s'", self->priv->path);

    self->priv->status = MM_BEARER_STATUS_DISCONNECTED;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
}

void
mm_bearer_disconnect_force (MMBearer *self)
{
    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTING ||
        self->priv->status == MM_BEARER_STATUS_DISCONNECTED)
        return;

    mm_dbg ("Forcing disconnection of bearer '%s'", self->priv->path);

    /* If currently connecting, try to cancel that operation. */
    if (self->priv->status == MM_BEARER_STATUS_CONNECTING) {
        g_cancellable_cancel (self->priv->connect_cancellable);
        return;
    }

    /* Disconnecting! */
    self->priv->status = MM_BEARER_STATUS_DISCONNECTING;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
    MM_BEARER_GET_CLASS (self)->disconnect (
        self,
        (GAsyncReadyCallback)disconnect_force_ready,
        NULL);
}

/*****************************************************************************/

gboolean
mm_bearer_cmp_properties (MMBearer *self,
                          MMBearerProperties *properties)
{
    return MM_BEARER_GET_CLASS (self)->cmp_properties (self, properties);
}

/*****************************************************************************/

void
mm_bearer_expose_properties (MMBearer *bearer,
                             MMBearerProperties *properties)
{
    GVariant *dictionary;

    /* Keep the whole list of properties in the interface */
    dictionary = mm_bearer_properties_get_dictionary (properties);
    mm_gdbus_bearer_set_properties (MM_GDBUS_BEARER (bearer),
                                    dictionary);
    g_variant_unref (dictionary);
}

/*****************************************************************************/

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBearer *self = MM_BEARER (object);

    switch (prop_id) {
    case PROP_PATH:
        g_free (self->priv->path);
        self->priv->path = g_value_dup_string (value);

        /* Export when we get a DBus connection AND we have a path */
        if (self->priv->path &&
            self->priv->connection)
            mm_bearer_dbus_export (self);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection AND we have a path */
        if (!self->priv->connection)
            mm_bearer_dbus_unexport (self);
        else if (self->priv->path)
            mm_bearer_dbus_export (self);
        break;
    case PROP_MODEM:
        g_clear_object (&self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
        if (self->priv->modem)
            /* Bind the modem's connection (which is set when it is exported,
             * and unset when unexported) to the BEARER's connection */
            g_object_bind_property (self->priv->modem, MM_BASE_MODEM_CONNECTION,
                                    self, MM_BEARER_CONNECTION,
                                    G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        break;
    case PROP_STATUS:
        self->priv->status = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBearer *self = MM_BEARER (object);

    switch (prop_id) {
    case PROP_PATH:
        g_value_set_string (value, self->priv->path);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    case PROP_STATUS:
        g_value_set_enum (value, self->priv->status);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_bearer_init (MMBearer *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER,
                                              MMBearerPrivate);
    self->priv->status = MM_BEARER_STATUS_DISCONNECTED;

    /* Set defaults */
    mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_suspended (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_properties (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_ip_timeout (MM_GDBUS_BEARER (self), MM_BEARER_IP_TIMEOUT_DEFAULT);
    mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (self),
                                    mm_bearer_ip_config_get_dictionary (NULL));
    mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (self),
                                    mm_bearer_ip_config_get_dictionary (NULL));
}

static void
finalize (GObject *object)
{
    MMBearer *self = MM_BEARER (object);

    g_free (self->priv->path);

    G_OBJECT_CLASS (mm_bearer_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBearer *self = MM_BEARER (object);

    if (self->priv->connection) {
        mm_bearer_dbus_unexport (self);
        g_clear_object (&self->priv->connection);
    }

    if (self->priv->modem)
        g_clear_object (&self->priv->modem);

    G_OBJECT_CLASS (mm_bearer_parent_class)->dispose (object);
}

static void
mm_bearer_class_init (MMBearerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BEARER_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);

    properties[PROP_PATH] =
        g_param_spec_string (MM_BEARER_PATH,
                             "Path",
                             "DBus path of the Bearer",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PATH, properties[PROP_PATH]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_BEARER_MODEM,
                             "Modem",
                             "The Modem which owns this Bearer",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    properties[PROP_STATUS] =
        g_param_spec_enum (MM_BEARER_STATUS,
                           "Bearer status",
                           "Status of the bearer",
                           MM_TYPE_BEARER_STATUS,
                           MM_BEARER_STATUS_DISCONNECTED,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_STATUS, properties[PROP_STATUS]);
}
