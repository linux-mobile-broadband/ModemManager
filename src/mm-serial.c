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

#include "mm-serial.h"
#include "mm-errors.h"
#include "mm-options.h"

#define TYPE_TAG "type"

G_DEFINE_TYPE (MMSerial, mm_serial, G_TYPE_OBJECT)

enum {
    PROP_0,

    LAST_PROP
};

#define MM_SERIAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_SERIAL, MMSerialPrivate))

typedef struct {
    GHashTable *ports;
} MMSerialPrivate;

MMSerialPort *
mm_serial_get_port (MMSerial *self, const char *name)
{
    return g_hash_table_lookup (MM_SERIAL_GET_PRIVATE (self)->ports, name);
}

static void
find_primary (gpointer key, gpointer data, gpointer user_data)
{
    MMSerialPort **found = user_data;
    MMSerialPort *port = MM_SERIAL_PORT (data);
    MMSerialPortType ptype;

    if (*found)
        return;

    ptype = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (port), TYPE_TAG));
    if (ptype == MM_SERIAL_PORT_TYPE_PRIMARY)
        *found = port;
}

MMSerialPort *
mm_serial_add_port (MMSerial *self,
                    const char *name,
                    MMSerialPortType ptype)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    MMSerialPort *port = NULL;

    g_return_val_if_fail (MM_IS_SERIAL (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (ptype != MM_SERIAL_PORT_TYPE_UNKNOWN, FALSE);

    g_return_val_if_fail (g_hash_table_lookup (priv->ports, name) == NULL, FALSE);

    if (ptype == MM_SERIAL_PORT_TYPE_PRIMARY) {
        g_hash_table_foreach (priv->ports, find_primary, &port);
        g_return_val_if_fail (port == NULL, FALSE);
    }

    port = mm_serial_port_new (name);
    if (!port)
        return NULL;

    g_object_set_data (G_OBJECT (port), TYPE_TAG, GUINT_TO_POINTER (ptype));
    g_hash_table_insert (priv->ports, g_strdup (name), port);
    return port;
}

gboolean
mm_serial_remove_port (MMSerial *self, MMSerialPort *port)
{
    g_return_val_if_fail (MM_IS_SERIAL (self), FALSE);
    g_return_val_if_fail (port != NULL, FALSE);

    return g_hash_table_remove (MM_SERIAL_GET_PRIVATE (self)->ports, port);
}

/*****************************************************************************/

static void
mm_serial_init (MMSerial *self)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    priv->ports = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMSerial *self = MM_SERIAL (object);
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    g_hash_table_destroy (priv->ports);

    G_OBJECT_CLASS (mm_serial_parent_class)->finalize (object);
}

static void
mm_serial_class_init (MMSerialClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSerialPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;
}
