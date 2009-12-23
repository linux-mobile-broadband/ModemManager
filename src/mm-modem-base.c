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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mm-modem-base.h"
#include "mm-modem.h"
#include "mm-serial-port.h"
#include "mm-errors.h"
#include "mm-options.h"
#include "mm-properties-changed-signal.h"

static void modem_init (MMModem *modem_class);

G_DEFINE_TYPE_EXTENDED (MMModemBase, mm_modem_base,
                        G_TYPE_OBJECT,
                        G_TYPE_FLAG_VALUE_ABSTRACT,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_MODEM, modem_init))

#define MM_MODEM_BASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_BASE, MMModemBasePrivate))

typedef struct {
    char *driver;
    char *plugin;
    char *device;
    guint32 ip_method;
    gboolean valid;
    MMModemState state;

    GHashTable *ports;
} MMModemBasePrivate;


static char *
get_hash_key (const char *subsys, const char *name)
{
    return g_strdup_printf ("%s%s", subsys, name);
}

MMPort *
mm_modem_base_get_port (MMModemBase *self,
                        const char *subsys,
                        const char *name)
{
    MMPort *port;
    char *key;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (subsys != NULL, NULL);

    g_return_val_if_fail (!strcmp (subsys, "net") || !strcmp (subsys, "tty"), NULL);

    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (MM_MODEM_BASE_GET_PRIVATE (self)->ports, key);
    g_free (key);
    return port;
}

static void
find_primary (gpointer key, gpointer data, gpointer user_data)
{
    MMPort **found = user_data;
    MMPort *port = MM_PORT (data);

    if (!*found && (mm_port_get_port_type (port) == MM_PORT_TYPE_PRIMARY))
        *found = port;
}

MMPort *
mm_modem_base_add_port (MMModemBase *self,
                        const char *subsys,
                        const char *name,
                        MMPortType ptype)
{
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);
    MMPort *port = NULL;
    char *key;

    g_return_val_if_fail (MM_IS_MODEM_BASE (self), NULL);
    g_return_val_if_fail (subsys != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (ptype != MM_PORT_TYPE_UNKNOWN, NULL);

    g_return_val_if_fail (!strcmp (subsys, "net") || !strcmp (subsys, "tty"), NULL);

    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (priv->ports, key);
    g_free (key);
    g_return_val_if_fail (port == NULL, NULL);

    if (ptype == MM_PORT_TYPE_PRIMARY) {
        g_hash_table_foreach (priv->ports, find_primary, &port);
        g_return_val_if_fail (port == NULL, FALSE);
    }

    if (!strcmp (subsys, "tty"))
        port = MM_PORT (mm_serial_port_new (name, ptype));
    else if (!strcmp (subsys, "net")) {
        port = MM_PORT (g_object_new (MM_TYPE_PORT,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_NET,
                                      MM_PORT_TYPE, ptype,
                                      NULL));
    }

    if (!port)
        return NULL;

    key = get_hash_key (subsys, name);
    g_hash_table_insert (priv->ports, key, port);
    return port;
}

gboolean
mm_modem_base_remove_port (MMModemBase *self, MMPort *port)
{
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), FALSE);
    g_return_val_if_fail (port != NULL, FALSE);

    return g_hash_table_remove (MM_MODEM_BASE_GET_PRIVATE (self)->ports, port);
}

void
mm_modem_base_set_valid (MMModemBase *self, gboolean new_valid)
{
    MMModemBasePrivate *priv;

    g_return_if_fail (MM_IS_MODEM_BASE (self));

    priv = MM_MODEM_BASE_GET_PRIVATE (self);

    if (priv->valid != new_valid) {
        priv->valid = new_valid;

        /* Modem starts off in disabled state, and jumps to disabled when
         * it's no longer valid.
         */
        mm_modem_set_state (MM_MODEM (self),
                            MM_MODEM_STATE_DISABLED,
                            MM_MODEM_STATE_REASON_NONE);

        g_object_notify (G_OBJECT (self), MM_MODEM_VALID);
    }
}

gboolean
mm_modem_base_get_valid (MMModemBase *self)
{
    g_return_val_if_fail (MM_IS_MODEM_BASE (self), FALSE);

    return MM_MODEM_BASE_GET_PRIVATE (self)->valid;
}

/*****************************************************************************/

static void
mm_modem_base_init (MMModemBase *self)
{
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);

    priv->ports = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    mm_properties_changed_signal_register_property (G_OBJECT (self),
                                                    MM_MODEM_ENABLED,
                                                    MM_MODEM_DBUS_INTERFACE);
}

static void
modem_init (MMModem *modem_class)
{
}

static gboolean
is_enabled (MMModemState state)
{
    return (state >= MM_MODEM_STATE_ENABLED);
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (object);
    gboolean old_enabled;

    switch (prop_id) {
    case MM_MODEM_PROP_STATE:
        /* Ensure we update the 'enabled' property when the state changes */
        old_enabled = is_enabled (priv->state);
        priv->state = g_value_get_uint (value);
        if (old_enabled != is_enabled (priv->state))
            g_object_notify (object, MM_MODEM_ENABLED);
        break;
    case MM_MODEM_PROP_DRIVER:
        /* Construct only */
        priv->driver = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_PLUGIN:
        /* Construct only */
        priv->plugin = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_MASTER_DEVICE:
        /* Construct only */
        priv->device = g_value_dup_string (value);
        break;
    case MM_MODEM_PROP_IP_METHOD:
        priv->ip_method = g_value_get_uint (value);
        break;
    case MM_MODEM_PROP_VALID:
    case MM_MODEM_PROP_TYPE:
    case MM_MODEM_PROP_ENABLED:
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (object);

    switch (prop_id) {
    case MM_MODEM_PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
    case MM_MODEM_PROP_MASTER_DEVICE:
        g_value_set_string (value, priv->device);
        break;
    case MM_MODEM_PROP_DATA_DEVICE:
        g_value_set_string (value, NULL);
        break;
    case MM_MODEM_PROP_DRIVER:
        g_value_set_string (value, priv->driver);
        break;
    case MM_MODEM_PROP_PLUGIN:
        g_value_set_string (value, priv->plugin);
        break;
    case MM_MODEM_PROP_TYPE:
        g_value_set_uint (value, MM_MODEM_TYPE_UNKNOWN);
        break;
    case MM_MODEM_PROP_IP_METHOD:
        g_value_set_uint (value, priv->ip_method);
        break;
    case MM_MODEM_PROP_VALID:
        g_value_set_boolean (value, priv->valid);
        break;
    case MM_MODEM_PROP_ENABLED:
        g_value_set_boolean (value, is_enabled (priv->state));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMModemBase *self = MM_MODEM_BASE (object);
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);

    g_hash_table_destroy (priv->ports);
    g_free (priv->driver);
    g_free (priv->plugin);
    g_free (priv->device);

    G_OBJECT_CLASS (mm_modem_base_parent_class)->finalize (object);
}

static void
mm_modem_base_class_init (MMModemBaseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMModemBasePrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_STATE,
                                      MM_MODEM_STATE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_MASTER_DEVICE,
                                      MM_MODEM_MASTER_DEVICE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DATA_DEVICE,
                                      MM_MODEM_DATA_DEVICE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_DRIVER,
                                      MM_MODEM_DRIVER);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_PLUGIN,
                                      MM_MODEM_PLUGIN);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_TYPE,
                                      MM_MODEM_TYPE);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_IP_METHOD,
                                      MM_MODEM_IP_METHOD);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_VALID,
                                      MM_MODEM_VALID);

    g_object_class_override_property (object_class,
                                      MM_MODEM_PROP_ENABLED,
                                      MM_MODEM_ENABLED);

    mm_properties_changed_signal_new (object_class);
}

