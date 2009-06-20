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

#include <string.h>
#include <gmodule.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include "mm-plugin-hso.h"
#include "mm-modem-hso.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginHso, mm_plugin_hso, MM_TYPE_PLUGIN_BASE,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

#define MM_PLUGIN_HSO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_HSO, MMPluginHsoPrivate))

typedef struct {
    GUdevClient *client;
} MMPluginHsoPrivate;


G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_HSO, NULL));
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
    const char *type;

    g_return_val_if_fail (child != NULL, NULL);

    /* Walk the parents to find the first 'usb_device' for this device. */
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

    return NULL;
}

static GUdevDevice *
get_device (GUdevClient *client,
            const char *subsys,
            const char *name,
            GUdevDevice **physdev,
            char **driver,
            GError **error)
{
    GUdevDevice *device = NULL;

    if (strcmp (subsys, "tty") && strcmp (subsys, "net")) {
        g_set_error (error, 0, 0, "Unsupported subsystem.");
        return NULL;
    }

    device = g_udev_client_query_by_subsystem_and_name (client, subsys, name);
    if (!device) {
        g_set_error (error, 0, 0, "Coud not get port's udev device.");
        return NULL;
    }

    *driver = get_driver_name (device);
    if (!*driver || strcmp (*driver, "hso")) {
        g_set_error (error, 0, 0, "Unsupported driver (not 'hso').");
        g_object_unref (device);
        device = NULL;
        goto out;
    }

    *physdev = find_physical_device (device);
    if (!*physdev) {
        g_set_error (error, 0, 0, "Could not get port's physical udev device.");
        g_object_unref (device);
        device = NULL;
    }

out:
    return device;
}

static guint32
supports_port (MMPlugin *plugin,
               const char *subsys,
               const char *name)
{
    MMPluginHsoPrivate *priv = MM_PLUGIN_HSO_GET_PRIVATE (plugin);
    GUdevDevice *device, *physdev = NULL;
    guint32 level = 0;
    char *driver = NULL;

    g_return_val_if_fail (plugin != NULL, 0);
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), 0);
    g_return_val_if_fail (subsys != NULL, 0);
    g_return_val_if_fail (name != NULL, 0);

    device = get_device (priv->client, subsys, name, &physdev, &driver, NULL);
    if (device)
        level = 10;

    g_free (driver);
    if (physdev)
        g_object_unref (physdev);
    if (device)
        g_object_unref (device);
    return level;
}

static MMModem *
grab_port (MMPlugin *plugin,
           const char *subsys,
           const char *name,
           GError **error)
{
    MMPluginHso *self = MM_PLUGIN_HSO (plugin);
    MMPluginHsoPrivate *priv = MM_PLUGIN_HSO_GET_PRIVATE (plugin);
    GUdevDevice *device = NULL, *physdev = NULL;
    const char *sysfs_path = NULL;
    char *driver = NULL, *devfile = NULL;
    MMModem *modem = NULL;

    g_return_val_if_fail (subsys != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    device = get_device (priv->client, subsys, name, &physdev, &driver, error);
    if (!device) {
        g_set_error (error, 0, 0, "Could not get port's udev device.");
        return NULL;
    }

    devfile = g_strdup (g_udev_device_get_device_file (device));
    if (!devfile) {
        if (!strcmp (subsys, "net")) {
            /* Apparently 'hso' doesn't set up the right links for the netdevice,
             * and thus libgudev can't get the sysfs file path for it.
             */
            devfile = g_strdup_printf ("/sys/class/net/%s", name);
            if (!g_file_test (devfile, G_FILE_TEST_EXISTS)) {
                g_free (devfile);
                devfile = NULL;
            }
        }

        if (!devfile) {
            g_set_error (error, 0, 0, "Could not get port's sysfs file.");
            goto out;
        }
    }

    sysfs_path = g_udev_device_get_sysfs_path (physdev);
    if (!sysfs_path) {
        g_set_error (error, 0, 0, "Could not get port's physical device sysfs path.");
        goto out;
    }

    modem = mm_plugin_base_find_modem (MM_PLUGIN_BASE (self), sysfs_path);
    if (!modem) {
        modem = mm_modem_hso_new (sysfs_path,
                                  driver,
                                  mm_plugin_get_name (plugin));

        if (modem) {
            if (!mm_modem_grab_port (modem, subsys, name, error)) {
                g_object_unref (modem);
                modem = NULL;
            }
        }

        if (modem)
            mm_plugin_base_add_modem (MM_PLUGIN_BASE (self), modem);
    } else {
        if (!mm_modem_grab_port (modem, subsys, name, error))
            modem = NULL;
    }

out:
    g_free (devfile);
    g_free (driver);
    g_object_unref (device);
    g_object_unref (physdev);
    return modem;
}

static const char *
get_name (MMPlugin *plugin)
{
    return "HSO";
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
mm_plugin_hso_init (MMPluginHso *self)
{
    MMPluginHsoPrivate *priv = MM_PLUGIN_HSO_GET_PRIVATE (self);
    const char *subsys[] = { "tty", "net", NULL };

    priv->client = g_udev_client_new (subsys);
}

static void
dispose (GObject *object)
{
    MMPluginHsoPrivate *priv = MM_PLUGIN_HSO_GET_PRIVATE (object);

    g_object_unref (priv->client);
}

static void
mm_plugin_hso_class_init (MMPluginHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginHsoPrivate));

    object_class->dispose = dispose;
}
