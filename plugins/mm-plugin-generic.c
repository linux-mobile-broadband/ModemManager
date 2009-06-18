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
 * Copyright (C) 2008 - 2009 Dan Williams <dcbw@redhat.com>
 */

#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>

#include <gmodule.h>
#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin-generic.h"
#include "mm-generic-gsm.h"
#include "mm-generic-cdma.h"
#include "mm-serial-port.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginGeneric, mm_plugin_generic, G_TYPE_OBJECT,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

#define MM_PLUGIN_GENERIC_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_GENERIC, MMPluginGenericPrivate))

typedef struct {
    GUdevClient *client;
    GHashTable *modems;
} MMPluginGenericPrivate;


G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_GENERIC, NULL));
}

/*****************************************************************************/

static char *
get_driver_name (GUdevDevice *device)
{
    GUdevDevice *parent = NULL;
    const char *driver;
    char *ret;

    driver = g_udev_device_get_driver (device);
    if (!driver) {
        parent = g_udev_device_get_parent (device);
        if (parent)
            driver = g_udev_device_get_driver (parent);
    }

    if (driver)
        ret = g_strdup (driver);
    if (parent)
        g_object_unref (parent);

    return ret;
}

static GUdevDevice *
find_physical_device (GUdevDevice *child)
{
    GUdevDevice *iter, *old = NULL;
    const char *bus, *type;

    g_return_val_if_fail (child != NULL, NULL);

    bus = g_udev_device_get_property (child, "ID_BUS");
    if (!bus)
        return NULL;

    if (!strcmp (bus, "usb")) {
        /* Walk the parents to find the 'usb_device' for this device. */
        iter = g_object_ref (child);
        while (iter) {
            type = g_udev_device_get_devtype (iter);
            if (type && !strcmp (type, "usb_device"))
                return iter;

            old = iter;
            iter = g_udev_device_get_parent (old);
            g_object_unref (old);
        }
        g_object_unref (child);
    } else if (!strcmp (bus, "pci")) {
        return g_udev_device_get_parent (child);
    }

    // FIXME: pci and pcmcia/cardbus? (like Sierra 850/860)
    return NULL;
}

#define PROP_GSM   "ID_MM_MODEM_GSM"
#define PROP_CDMA  "ID_MM_MODEM_IS707_A"
#define PROP_EVDO1 "ID_MM_MODEM_IS856"
#define PROP_EVDOA "ID_MM_MODEM_IS856_A"

static GUdevDevice *
get_device_type (MMPlugin *plugin,
                 const char *subsys,
                 const char *name,
                 gboolean *gsm,
                 gboolean *cdma)
{
    MMPluginGenericPrivate *priv = MM_PLUGIN_GENERIC_GET_PRIVATE (plugin);
    GUdevDevice *device;

    g_return_val_if_fail (plugin != NULL, NULL);
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), NULL);
    g_return_val_if_fail (subsys != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    device = g_udev_client_query_by_subsystem_and_name (priv->client, subsys, name);
    if (!device)
        return NULL;

    if (g_udev_device_get_property_as_boolean (device, PROP_GSM))
        *gsm = TRUE;
    if (   g_udev_device_get_property_as_boolean (device, PROP_CDMA)
        || g_udev_device_get_property_as_boolean (device, PROP_EVDO1)
        || g_udev_device_get_property_as_boolean (device, PROP_EVDOA))
        *cdma = TRUE;

    return device;
}

static guint32
supports_port (MMPlugin *plugin,
               const char *subsys,
               const char *name)
{
    GUdevDevice *device, *physdev = NULL;
    gboolean gsm = FALSE, cdma = FALSE;
    guint32 level = 0;

    g_return_val_if_fail (plugin != NULL, 0);
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), 0);
    g_return_val_if_fail (subsys != NULL, 0);
    g_return_val_if_fail (name != NULL, 0);

    /* Can't do anything with non-serial ports */
    if (strcmp (subsys, "tty"))
        return 0;

    device = get_device_type (plugin, subsys, name, &gsm, &cdma);
    if (!device)
        return 0;

    if (!gsm && !cdma)
        goto out;

    physdev = find_physical_device (device);
    if (!physdev)
        goto out;
    g_object_unref (physdev);
    level = 5;

out:
    return level;
}

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
    MMPluginGeneric *self = MM_PLUGIN_GENERIC (data);
    MMPluginGenericPrivate *priv = MM_PLUGIN_GENERIC_GET_PRIVATE (self);
    FindInfo info = { NULL, modem };

    g_hash_table_foreach (priv->modems, find_modem, &info);
    if (info.key)
        g_hash_table_remove (priv->modems, info.key);
    g_free (info.key);
}

static MMModem *
grab_port (MMPlugin *plugin,
           const char *subsys,
           const char *name,
           GError **error)
{
    MMPluginGeneric *self = MM_PLUGIN_GENERIC (plugin);
    MMPluginGenericPrivate *priv = MM_PLUGIN_GENERIC_GET_PRIVATE (plugin);
    GUdevDevice *device = NULL, *physdev = NULL;
    const char *devfile, *sysfs_path;
    char *driver = NULL;
    MMModem *modem = NULL;
    gboolean gsm = FALSE, cdma = FALSE;

    g_return_val_if_fail (subsys != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    /* Can't do anything with non-serial ports */
    if (strcmp (subsys, "tty"))
        return NULL;

    device = get_device_type (plugin, subsys, name, &gsm, &cdma);
    if (!device) {
        g_set_error (error, 0, 0, "Could not get port's udev device.");
        return NULL;
    }

    if (!gsm && !cdma) {
        g_set_error (error, 0, 0, "Modem unsupported (not GSM or CDMA).");
        goto out;
    }

    physdev = find_physical_device (device);
    if (!physdev) {
        g_set_error (error, 0, 0, "Could not get ports's physical device.");
        goto out;
    }

    devfile = g_udev_device_get_device_file (device);
    if (!devfile) {
        g_set_error (error, 0, 0, "Could not get port's sysfs file.");
        goto out;
    }

    driver = get_driver_name (device);
    if (!devfile) {
        g_set_error (error, 0, 0, "Could not get port's driver name.");
        goto out;
    }

    sysfs_path = g_udev_device_get_sysfs_path (physdev);
    if (!devfile) {
        g_set_error (error, 0, 0, "Could not get port's physical device sysfs path.");
        goto out;
    }

    modem = g_hash_table_lookup (priv->modems, sysfs_path);
    if (!modem) {
        if (gsm) {
            modem = mm_generic_gsm_new (sysfs_path,
                                        driver,
                                        mm_plugin_get_name (plugin));
        } else if (cdma) {
            modem = mm_generic_cdma_new (sysfs_path,
                                         driver,
                                         mm_plugin_get_name (plugin));
        }

        if (modem) {
            if (!mm_modem_grab_port (modem, subsys, name, error)) {
                g_object_unref (modem);
                modem = NULL;
            }
        }

        if (modem) {
            g_object_weak_ref (G_OBJECT (modem), modem_destroyed, self);
            g_hash_table_insert (priv->modems, g_strdup (sysfs_path), modem);
        }
    } else {
        if (gsm || cdma) {
            if (!mm_modem_grab_port (modem, subsys, name, error))
                modem = NULL;
        }
    }

out:
    g_free (driver);
    g_object_unref (device);
    g_object_unref (physdev);
    return modem;
}

static const char *
get_name (MMPlugin *plugin)
{
    return "Generic";
}

/*****************************************************************************/

static void
plugin_init (MMPlugin *plugin_class)
{
    /* interface implementation */
    plugin_class->get_name = get_name;
    plugin_class->supports_port = supports_port;
    plugin_class->grab_port = grab_port;
}

static void
mm_plugin_generic_init (MMPluginGeneric *self)
{
    MMPluginGenericPrivate *priv = MM_PLUGIN_GENERIC_GET_PRIVATE (self);
    const char *subsys[2] = { "tty", NULL };

    priv->modems = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    priv->client = g_udev_client_new (subsys);
}

static void
dispose (GObject *object)
{
    MMPluginGenericPrivate *priv = MM_PLUGIN_GENERIC_GET_PRIVATE (object);

    g_hash_table_destroy (priv->modems);
    g_object_unref (priv->client);
}

static void
mm_plugin_generic_class_init (MMPluginGenericClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginGenericPrivate));

    object_class->dispose = dispose;
}

