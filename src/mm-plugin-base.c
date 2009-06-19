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

#include "mm-plugin-base.h"

G_DEFINE_TYPE (MMPluginBase, mm_plugin_base, G_TYPE_OBJECT)

#define MM_PLUGIN_BASE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_BASE, MMPluginBasePrivate))

typedef struct {
    GHashTable *modems;
} MMPluginBasePrivate;


typedef struct {
    char *key;
    gpointer modem;
} FindInfo;

static void
find_modem (gpointer key, gpointer data, gpointer user_data)
{
    FindInfo *info = user_data;

    if (!info->key && data == info->modem)
        info->key = g_strdup ((const char *) key);
}

static void
modem_destroyed (gpointer data, GObject *modem)
{
    MMPluginBase *self = MM_PLUGIN_BASE (data);
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    FindInfo info = { NULL, modem };

    g_hash_table_foreach (priv->modems, find_modem, &info);
    if (info.key)
        g_hash_table_remove (priv->modems, info.key);
    g_free (info.key);
}

gboolean
mm_plugin_base_add_modem (MMPluginBase *self,
                          MMModem *modem)
{
    MMPluginBasePrivate *priv;
    const char *device;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), FALSE);
    g_return_val_if_fail (modem != NULL, FALSE);
    g_return_val_if_fail (MM_IS_MODEM (modem), FALSE);

    priv = MM_PLUGIN_BASE_GET_PRIVATE (self);

    device = mm_modem_get_device (modem);
    if (g_hash_table_lookup (priv->modems, device))
        return FALSE;

    g_object_weak_ref (G_OBJECT (modem), modem_destroyed, self);
    g_hash_table_insert (priv->modems, g_strdup (device), modem);
    return TRUE;
}

MMModem *
mm_plugin_base_find_modem (MMPluginBase *self,
                           const char *master_device)
{
    MMPluginBasePrivate *priv;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), NULL);
    g_return_val_if_fail (master_device != NULL, NULL);
    g_return_val_if_fail (strlen (master_device) > 0, NULL);

    priv = MM_PLUGIN_BASE_GET_PRIVATE (self);
    return g_hash_table_lookup (priv->modems, master_device);
}

/*****************************************************************************/

static void
mm_plugin_base_init (MMPluginBase *self)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (self);

    priv->modems = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
finalize (GObject *object)
{
    MMPluginBasePrivate *priv = MM_PLUGIN_BASE_GET_PRIVATE (object);

    g_hash_table_destroy (priv->modems);

    G_OBJECT_CLASS (mm_plugin_base_parent_class)->finalize (object);
}

static void
mm_plugin_base_class_init (MMPluginBaseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginBasePrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}
