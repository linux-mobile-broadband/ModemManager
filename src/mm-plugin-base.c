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

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

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

/* From hostap, Copyright (c) 2002-2005, Jouni Malinen <jkmaline@cc.hut.fi> */

static int hex2num (char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex2byte (const char *hex)
{
	int a, b;
	a = hex2num(*hex++);
	if (a < 0)
		return -1;
	b = hex2num(*hex++);
	if (b < 0)
		return -1;
	return (a << 4) | b;
}

/* End from hostap */

gboolean
mm_plugin_base_get_device_ids (MMPluginBase *self,
                               const char *subsys,
                               const char *name,
                               guint16 *vendor,
                               guint16 *product)
{
    GUdevClient *client;
    GUdevDevice *device = NULL;
    const char *tmp[] = { subsys, NULL };
    const char *vid, *pid;
    gboolean success = FALSE;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PLUGIN_BASE (self), FALSE);
    g_return_val_if_fail (subsys != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    if (vendor)
        g_return_val_if_fail (*vendor == 0, FALSE);
    if (product)
        g_return_val_if_fail (*product == 0, FALSE);

    client = g_udev_client_new (tmp);
    if (!client)
        return FALSE;

    device = g_udev_client_query_by_subsystem_and_name (client, subsys, name);
    if (!device)
        goto out;

    vid = g_udev_device_get_property (device, "ID_VENDOR_ID");
    if (!vid || (strlen (vid) != 4))
        goto out;

    if (vendor) {
        *vendor = (guint16) (hex2byte (vid + 2) & 0xFF);
        *vendor |= (guint16) ((hex2byte (vid) & 0xFF) << 8);
    }

    pid = g_udev_device_get_property (device, "ID_MODEL_ID");
    if (!pid || (strlen (pid) != 4)) {
        *vendor = 0;
        goto out;
    }

    if (product) {
        *product = (guint16) (hex2byte (pid + 2) & 0xFF);
        *product |= (guint16) ((hex2byte (pid) & 0xFF) << 8);
    }

    success = TRUE;

out:
    if (device)
        g_object_unref (device);
    g_object_unref (client);
    return success;
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
