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

#include <mm-enums-types.h>
#include <mm-errors-types.h>
#include <mm-gdbus-bearer.h>

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
    PROP_CAPABILITY,
    PROP_CONNECTION_APN,
    PROP_CONNECTION_IP_TYPE,
    PROP_CONNECTION_USER,
    PROP_CONNECTION_PASSWORD,
    PROP_CONNECTION_NUMBER,
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
    /* Capability of this bearer */
    MMModemCapability capability;

    /* Input properties configured */
    gchar *connection_apn;
    gchar *connection_ip_type;
    gchar *connection_user;
    gchar *connection_password;
    gchar *connection_number;
};

/*****************************************************************************/
/* CONNECT */

static gboolean
handle_connect (MMBearer *self,
                GDBusMethodInvocation *invocation,
                const gchar *arg_number)
{
    return FALSE;
}

/*****************************************************************************/
/* DISCONNECT */

static gboolean
handle_disconnect (MMBearer *self,
                GDBusMethodInvocation *invocation,
                const gchar *arg_number)
{
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

/*****************************************************************************/

static gboolean
parse_input_properties (MMBearer *bearer,
                        MMModemCapability bearer_capability,
                        GVariant *properties,
                        GError **error)
{
    GVariantIter iter;
    gchar *key;
    gchar *value;

    g_variant_iter_init (&iter, properties);
    while (g_variant_iter_loop (&iter, "{ss}", &key, &value)) {
        gchar *previous = NULL;

        g_object_get (G_OBJECT (bearer),
                      key, &previous,
                      NULL);

        if (previous) {
            if (g_str_equal (previous, value)) {
                /* no big deal */
                g_free (previous);
                continue;
            }

            g_free (previous);
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_INVALID_ARGS,
                         "Invalid input properties: duplicated key '%s'",
                         key);
            return FALSE;
        }

        g_object_set (G_OBJECT (bearer),
                      key, value,
                      NULL);
    }

    /* Check mandatory properties for each capability */
#define CHECK_MANDATORY_PROPERTY(NAME, PROPERTY) do     \
    {                                                   \
        gchar *value;                                   \
                                                        \
        g_object_get (G_OBJECT (bearer),                \
                      PROPERTY, &value,                 \
                      NULL);                            \
        if (!value) {                                   \
            g_set_error (error,                         \
                         MM_CORE_ERROR,                                 \
                         MM_CORE_ERROR_INVALID_ARGS,                    \
                         "Invalid input properties: %s bearer requires '%s'", \
                         NAME,                                          \
                         PROPERTY);                                     \
            return FALSE;                                               \
        }                                                               \
        g_free (value);                                                 \
    } while (0)

    /* POTS bearer? */
    if (bearer_capability & MM_MODEM_CAPABILITY_POTS) {
        CHECK_MANDATORY_PROPERTY ("POTS", MM_BEARER_CONNECTION_NUMBER);
    }
    /* CDMA bearer? */
    else if (bearer_capability & MM_MODEM_CAPABILITY_CDMA_EVDO) {
        /* No mandatory properties here */
    }
    /* 3GPP bearer? */
    else {
        CHECK_MANDATORY_PROPERTY ("3GPP", MM_BEARER_CONNECTION_APN);
    }

#undef CHECK_MANDATORY_PROPERTY

    return TRUE;
}

MMBearer *
mm_bearer_new (MMBaseModem *modem,
               GVariant *properties,
               MMModemCapability capability,
               GError **error)
{
    static guint32 id = 0;
    gchar *path;
    MMBearer *bearer;

    /* Ensure only one capability is set */
    g_assert_cmpuint (mm_count_bits_set (capability), ==, 1);

    /* Create the object */
    bearer = g_object_new  (MM_TYPE_BEARER,
                            MM_BEARER_CAPABILITY, capability,
                            NULL);

    /* Parse and set input properties */
    if (!parse_input_properties (bearer, capability, properties, error)) {
        g_object_unref (bearer);
        return NULL;
    }

    /* Set defaults */
    mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (bearer), NULL);
    mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (bearer), FALSE);
    mm_gdbus_bearer_set_suspended (MM_GDBUS_BEARER (bearer), FALSE);
    mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (bearer), NULL);
    mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (bearer), NULL);

    /* Set modem and path ONLY after having checked input properties, so that
     * we don't export invalid bearers. */
    path = g_strdup_printf (MM_DBUS_BEARER_PREFIX "%d", id++);
    g_object_set (bearer,
                  MM_BEARER_PATH,  path,
                  MM_BEARER_MODEM, modem,
                  NULL);
    g_free (path);

    return bearer;
}

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
    case PROP_CAPABILITY:
        self->priv->capability = g_value_get_flags (value);
        break;
    case PROP_CONNECTION_APN:
        g_free (self->priv->connection_apn);
        self->priv->connection_apn = g_value_dup_string (value);
        break;
    case PROP_CONNECTION_IP_TYPE:
        g_free (self->priv->connection_ip_type);
        self->priv->connection_ip_type = g_value_dup_string (value);
        break;
    case PROP_CONNECTION_USER:
        g_free (self->priv->connection_user);
        self->priv->connection_user = g_value_dup_string (value);
        break;
    case PROP_CONNECTION_PASSWORD:
        g_free (self->priv->connection_password);
        self->priv->connection_password = g_value_dup_string (value);
        break;
    case PROP_CONNECTION_NUMBER:
        g_free (self->priv->connection_number);
        self->priv->connection_number = g_value_dup_string (value);
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
    case PROP_CAPABILITY:
        g_value_set_flags (value, self->priv->capability);
        break;
    case PROP_CONNECTION_APN:
        g_value_set_string (value, self->priv->connection_apn);
        break;
    case PROP_CONNECTION_IP_TYPE:
        g_value_set_string (value, self->priv->connection_ip_type);
        break;
    case PROP_CONNECTION_USER:
        g_value_set_string (value, self->priv->connection_user);
        break;
    case PROP_CONNECTION_PASSWORD:
        g_value_set_string (value, self->priv->connection_password);
        break;
    case PROP_CONNECTION_NUMBER:
        g_value_set_string (value, self->priv->connection_number);
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
}

static void
finalize (GObject *object)
{
    MMBearer *self = MM_BEARER (object);

    g_free (self->priv->path);

    g_free (self->priv->connection_apn);
    g_free (self->priv->connection_ip_type);
    g_free (self->priv->connection_user);
    g_free (self->priv->connection_password);
    g_free (self->priv->connection_number);

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

    properties[PROP_CAPABILITY] =
        g_param_spec_flags (MM_BEARER_CAPABILITY,
                            "Capability",
                            "The Capability supported by this Bearer",
                            MM_TYPE_MODEM_CAPABILITY,
                            MM_MODEM_CAPABILITY_NONE,
                            G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CAPABILITY, properties[PROP_CAPABILITY]);

    properties[PROP_CONNECTION_APN] =
        g_param_spec_string (MM_BEARER_CONNECTION_APN,
                             "Connection APN",
                             "Access Point Name to use in the connection",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION_APN, properties[PROP_CONNECTION_APN]);

    properties[PROP_CONNECTION_IP_TYPE] =
        g_param_spec_string (MM_BEARER_CONNECTION_IP_TYPE,
                             "Connection IP type",
                             "IP setup to use in the connection",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION_IP_TYPE, properties[PROP_CONNECTION_IP_TYPE]);

    properties[PROP_CONNECTION_USER] =
        g_param_spec_string (MM_BEARER_CONNECTION_USER,
                             "Connection User",
                             "User to use in the connection",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION_USER, properties[PROP_CONNECTION_USER]);

    properties[PROP_CONNECTION_PASSWORD] =
        g_param_spec_string (MM_BEARER_CONNECTION_PASSWORD,
                             "Connection Password",
                             "Password to use in the connection",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION_PASSWORD, properties[PROP_CONNECTION_PASSWORD]);

    properties[PROP_CONNECTION_NUMBER] =
        g_param_spec_string (MM_BEARER_CONNECTION_NUMBER,
                             "Connection Number",
                             "Number to use in the connection",
                             NULL,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION_NUMBER, properties[PROP_CONNECTION_NUMBER]);
}
