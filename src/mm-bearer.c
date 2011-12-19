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
    PROP_CONNECTION_ALLOWED,
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
    /* Flag to specify whether the bearer can be connected */
    gboolean connection_allowed;
};

/*****************************************************************************/
/* CONNECT */

static void
handle_connect_ready (MMBearer *self,
                      GAsyncResult *res,
                      GDBusMethodInvocation *invocation)
{
    GError *error = NULL;

    if (!MM_BEARER_GET_CLASS (self)->connect_finish (self, res, &error))
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
    if (!self->priv->connection_allowed) {
        g_dbus_method_invocation_return_error (
            invocation,
            MM_CORE_ERROR,
            MM_CORE_ERROR_WRONG_STATE,
            "Not allowed to connect bearer");
        return TRUE;
    }

    if (MM_BEARER_GET_CLASS (self)->connect != NULL &&
        MM_BEARER_GET_CLASS (self)->connect_finish != NULL) {
        MM_BEARER_GET_CLASS (self)->connect (
            self,
            number,
            (GAsyncReadyCallback)handle_connect_ready,
            g_object_ref (invocation));
        return TRUE;
    }

    return FALSE;
}

/*****************************************************************************/
/* DISCONNECT */

static void
handle_disconnect_ready (MMBearer *self,
                         GAsyncResult *res,
                         GDBusMethodInvocation *invocation)
{
    GError *error = NULL;

    if (!MM_BEARER_GET_CLASS (self)->disconnect_finish (self, res, &error))
        g_dbus_method_invocation_take_error (invocation, error);
    else
        mm_gdbus_bearer_complete_disconnect (MM_GDBUS_BEARER (self), invocation);

    g_object_unref (invocation);
}

static gboolean
handle_disconnect (MMBearer *self,
                   GDBusMethodInvocation *invocation)
{
    if (MM_BEARER_GET_CLASS (self)->disconnect != NULL &&
        MM_BEARER_GET_CLASS (self)->disconnect_finish != NULL) {
        MM_BEARER_GET_CLASS (self)->disconnect (
            self,
            (GAsyncReadyCallback)handle_disconnect_ready,
            g_object_ref (invocation));
        return TRUE;
    }

    return FALSE;
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

const gchar *
mm_bearer_get_path (MMBearer *self)
{
    return self->priv->path;
}

void
mm_bearer_set_connection_allowed (MMBearer *self)
{
    if (self->priv->connection_allowed)
        return;

    mm_dbg ("Connection in bearer '%s' is allowed", self->priv->path);
    self->priv->connection_allowed = TRUE;
}

void
mm_bearer_set_connection_forbidden (MMBearer *self)
{
    if (!self->priv->connection_allowed)
        return;

    mm_dbg ("Connection in bearer '%s' is forbidden", self->priv->path);
    self->priv->connection_allowed = FALSE;
    /* TODO: possibly, force disconnection */
}

void
mm_bearer_expose_properties (MMBearer *bearer,
                             const gchar *first_property_name,
                             ...)
{
    va_list va_args;
    const gchar *key;
    GVariantBuilder builder;

    va_start (va_args, first_property_name);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
    key = first_property_name;
    while (key) {
        const gchar *value;

        /* If a key with NULL value is given, just ignore it. */
        value = va_arg (va_args, gchar *);
        if (value)
            g_variant_builder_add (&builder, "{ss}", key, value);

        key = va_arg (va_args, gchar *);
    }
    va_end (va_args);

    /* Keep the whole list of properties in the interface */
    mm_gdbus_bearer_set_properties (MM_GDBUS_BEARER (bearer),
                                    g_variant_builder_end (&builder));
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
        if (self->priv->connection)
            g_object_unref (self->priv->connection);
        self->priv->connection = g_value_dup_object (value);

        /* Export when we get a DBus connection */
        if (self->priv->connection)
            mm_bearer_export (self);
        else
            mm_bearer_unexport (self);
        break;
    case PROP_MODEM:
        if (self->priv->modem)
            g_object_unref (self->priv->modem);
        self->priv->modem = g_value_dup_object (value);
        if (self->priv->modem)
            /* Bind the modem's connection (which is set when it is exported,
             * and unset when unexported) to the BEARER's connection */
            g_object_bind_property (self->priv->modem, MM_BASE_MODEM_CONNECTION,
                                    self, MM_BEARER_CONNECTION,
                                    G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        break;
    case PROP_CONNECTION_ALLOWED:
        self->priv->connection_allowed = g_value_get_boolean (value);
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
    case PROP_CONNECTION_ALLOWED:
        g_value_set_boolean (value, self->priv->connection_allowed);
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

    properties[PROP_CONNECTION_ALLOWED] =
        g_param_spec_boolean (MM_BEARER_CONNECTION_ALLOWED,
                              "Connection allowed",
                              "Flag to specify whether the bearer is allowed to get connected",
                              FALSE,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION_ALLOWED, properties[PROP_CONNECTION_ALLOWED]);
}
