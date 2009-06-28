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
#include "mm-serial-port.h"
#include "mm-errors.h"
#include "mm-options.h"

G_DEFINE_TYPE (MMModemBase, mm_modem_base, G_TYPE_OBJECT)

#define MM_MODEM_BASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MODEM_BASE, MMModemBasePrivate))

typedef struct {
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

/*****************************************************************************/

static void
mm_modem_base_init (MMModemBase *self)
{
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);

    priv->ports = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
finalize (GObject *object)
{
    MMModemBase *self = MM_MODEM_BASE (object);
    MMModemBasePrivate *priv = MM_MODEM_BASE_GET_PRIVATE (self);

    g_hash_table_destroy (priv->ports);

    G_OBJECT_CLASS (mm_modem_base_parent_class)->finalize (object);
}

static void
mm_modem_base_class_init (MMModemBaseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMModemBasePrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}
