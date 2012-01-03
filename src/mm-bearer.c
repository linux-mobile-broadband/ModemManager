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

#include "mm-private-enums-types.h"
#include "mm-iface-modem.h"
#include "mm-bearer.h"
#include "mm-base-modem-at.h"
#include "mm-base-modem.h"
#include "mm-utils.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMBearer, mm_bearer, MM_GDBUS_TYPE_BEARER_SKELETON);

enum {
    PROP_0,
    PROP_PATH,
    PROP_CONNECTION,
    PROP_MODEM,
    PROP_CONNECTION_FORBIDDEN_REASON,
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
    /* Reason for not allowing connection */
    MMBearerConnectionForbiddenReason connection_forbidden_reason;
    /* Status of this bearer */
    MMBearerStatus status;

    /* Cancellable for connect() */
    GCancellable *connect_cancellable;
    /* handler id for the disconnect + cancel connect request */
    gulong disconnect_signal_handler;
};

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

    /* NOTE: connect() implementations *MUST* handle cancellations themselves */
    if (!MM_BEARER_GET_CLASS (self)->connect_finish (self, res, &error)) {
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
    else if (g_cancellable_is_cancelled (self->priv->connect_cancellable)) {
        mm_dbg ("Connected bearer '%s', but need to disconnect", self->priv->path);
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
                   const gchar *number,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_assert (MM_BEARER_GET_CLASS (self)->connect != NULL);
    g_assert (MM_BEARER_GET_CLASS (self)->connect_finish != NULL);

    /* Bearer may not be allowed to connect yet */
    if (self->priv->connection_forbidden_reason != MM_BEARER_CONNECTION_FORBIDDEN_REASON_NONE) {
        GEnumClass *enum_class;
        GEnumValue *value;

        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_BEARER_CONNECTION_FORBIDDEN_REASON));
        value = g_enum_get_value (enum_class, self->priv->connection_forbidden_reason);

        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_UNAUTHORIZED,
            "Not allowed to connect bearer: %s",
            value->value_nick);

        g_type_class_unref (enum_class);
        return;
    }

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
        number,
        self->priv->connect_cancellable,
        (GAsyncReadyCallback)connect_ready,
        result);
}

static void
handle_connect_ready (MMBearer *self,
                      GAsyncResult *res,
                      GDBusMethodInvocation *invocation)
{
    GError *error = NULL;

    if (!mm_bearer_connect_finish (self, res, &error))
        g_dbus_method_invocation_take_error (invocation, error);
    else
        mm_gdbus_bearer_complete_connect (MM_GDBUS_BEARER (self), invocation);

    g_object_unref (invocation);
}

static gboolean
handle_connect (MMBearer *self,
                GDBusMethodInvocation *invocation,
                const gchar *number)
{
    mm_bearer_connect (self,
                       number,
                       (GAsyncReadyCallback)handle_connect_ready,
                       g_object_ref (invocation));
    return TRUE;
}

/*****************************************************************************/
/* DISCONNECT */

static void
handle_disconnect_ready (MMBearer *self,
                         GAsyncResult *res,
                         GDBusMethodInvocation *invocation)
{
    GError *error = NULL;

    if (!mm_bearer_disconnect_finish (self, res, &error))
        g_dbus_method_invocation_take_error (invocation, error);
    else
        mm_gdbus_bearer_complete_disconnect (MM_GDBUS_BEARER (self), invocation);
    g_object_unref (invocation);
}

static gboolean
handle_disconnect (MMBearer *self,
                   GDBusMethodInvocation *invocation)
{
    mm_bearer_disconnect (self,
                          (GAsyncReadyCallback)handle_disconnect_ready,
                          g_object_ref (invocation));
    return TRUE;
}

/*****************************************************************************/

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

/*****************************************************************************/

static void
mm_bearer_export (MMBearer *self)
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
mm_bearer_unexport (MMBearer *self)
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

void
mm_bearer_set_connection_allowed (MMBearer *self)
{
    if (self->priv->connection_forbidden_reason == MM_BEARER_CONNECTION_FORBIDDEN_REASON_NONE)
        return;

    mm_dbg ("Connection in bearer '%s' is allowed", self->priv->path);
    self->priv->connection_forbidden_reason = MM_BEARER_CONNECTION_FORBIDDEN_REASON_NONE;
}

static void
disconnect_after_forbidden_ready (MMBearer *self,
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
mm_bearer_set_connection_forbidden (MMBearer *self,
                                    MMBearerConnectionForbiddenReason reason)
{
    GEnumClass *enum_class;
    GEnumValue *value;

    g_assert (reason != MM_BEARER_CONNECTION_FORBIDDEN_REASON_NONE);

    self->priv->connection_forbidden_reason = reason;
    enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_BEARER_CONNECTION_FORBIDDEN_REASON));
    value = g_enum_get_value (enum_class, self->priv->connection_forbidden_reason);
    mm_dbg ("Connection in bearer '%s' is forbidden: '%s'",
            self->priv->path,
            value->value_nick);
    g_type_class_unref (enum_class);

    if (self->priv->status == MM_BEARER_STATUS_DISCONNECTING ||
        self->priv->status == MM_BEARER_STATUS_DISCONNECTED) {
        return;
    }

    mm_dbg ("Disconnecting bearer '%s'", self->priv->path);

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
        (GAsyncReadyCallback)disconnect_after_forbidden_ready,
        NULL);
}

void
mm_bearer_expose_properties (MMBearer *bearer,
                             MMCommonBearerProperties *properties)
{
    GVariant *dictionary;

    /* Keep the whole list of properties in the interface */
    dictionary = mm_common_bearer_properties_get_dictionary (properties);
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
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection */
        if (self->priv->connection)
            mm_bearer_export (self);
        else
            mm_bearer_unexport (self);
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
    case PROP_CONNECTION_FORBIDDEN_REASON:
        self->priv->connection_forbidden_reason = g_value_get_enum (value);
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
    case PROP_CONNECTION_FORBIDDEN_REASON:
        g_value_set_enum (value, self->priv->connection_forbidden_reason);
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
    self->priv->connection_forbidden_reason = MM_BEARER_CONNECTION_FORBIDDEN_REASON_UNREGISTERED;

    /* Set defaults */
    mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_suspended (MM_GDBUS_BEARER (self), FALSE);
    mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (self), NULL);
    mm_gdbus_bearer_set_properties (MM_GDBUS_BEARER (self), NULL);
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
        mm_bearer_unexport (self);
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

    properties[PROP_CONNECTION_FORBIDDEN_REASON] =
        g_param_spec_enum (MM_BEARER_CONNECTION_FORBIDDEN_REASON,
                           "Connection forbidden reason",
                           "Reason to specify why the connection in the bearer is forbidden",
                           MM_TYPE_BEARER_CONNECTION_FORBIDDEN_REASON,
                           MM_BEARER_CONNECTION_FORBIDDEN_REASON_UNREGISTERED,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION_FORBIDDEN_REASON, properties[PROP_CONNECTION_FORBIDDEN_REASON]);

    properties[PROP_STATUS] =
        g_param_spec_enum (MM_BEARER_STATUS,
                           "Bearer status",
                           "Status of the bearer",
                           MM_TYPE_BEARER_STATUS,
                           MM_BEARER_STATUS_DISCONNECTED,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_STATUS, properties[PROP_STATUS]);
}
