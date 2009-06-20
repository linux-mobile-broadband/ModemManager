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

#include "mm-plugin-huawei.h"
#include "mm-generic-gsm.h"
#include "mm-modem-huawei.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginHuawei, mm_plugin_huawei, MM_TYPE_PLUGIN_BASE,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

#define MM_PLUGIN_HUAWEI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PLUGIN_HUAWEI, MMPluginHuaweiPrivate))

typedef struct {
    GUdevClient *client;
} MMPluginHuaweiPrivate;


G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_HUAWEI, NULL));
}

/*****************************************************************************/

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

static gboolean
get_ids (GUdevDevice *device, guint32 *vendor, guint32 *product)
{
    const char *vid, *pid;

    g_return_val_if_fail (device != NULL, FALSE);

    vid = g_udev_device_get_property (device, "ID_VENDOR_ID");
    if (!vid || (strlen (vid) != 4))
        return FALSE;

    if (vendor) {
        *vendor = (guint32) (hex2byte (vid + 2) & 0xFF);
        *vendor |= (guint32) ((hex2byte (vid) & 0xFF) << 8);
    }

    pid = g_udev_device_get_property (device, "ID_MODEL_ID");
    if (!pid || (strlen (pid) != 4))
        return FALSE;

    if (product) {
        *product = (guint32) (hex2byte (pid + 2) & 0xFF);
        *product |= (guint32) ((hex2byte (pid) & 0xFF) << 8);
    }

    return TRUE;
}

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

    if (strcmp (bus, "usb"))
        return NULL;

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

    return NULL;
}

#define PROP_GSM   "ID_MM_MODEM_GSM"

static GUdevDevice *
get_device (GUdevClient *client,
            const char *subsys,
            const char *name,
            GUdevDevice **physdev)
{
    GUdevDevice *device;
    const char *tmp;

    /* Can't do anything with non-serial ports */
    if (strcmp (subsys, "tty"))
        return NULL;

    device = g_udev_client_query_by_subsystem_and_name (client, subsys, name);
    if (!device)
        return NULL;

    tmp = g_udev_device_get_property (device, "ID_BUS");
    if (!tmp || strcmp (tmp, "usb"))
        goto error;

    if (!g_udev_device_get_property_as_boolean (device, PROP_GSM))
        goto error;

    *physdev = find_physical_device (device);
    if (*physdev)
        return device;

error:
    g_object_unref (device);
    return NULL;
}

static guint32
supports_port (MMPlugin *plugin,
               const char *subsys,
               const char *name)
{
    MMPluginHuaweiPrivate *priv = MM_PLUGIN_HUAWEI_GET_PRIVATE (plugin);
    GUdevDevice *device, *physdev = NULL;
    guint32 level = 0;
    guint32 vendor = 0, product = 0;

    device = get_device (priv->client, subsys, name, &physdev);
    if (!device)
        goto out;
    g_object_unref (physdev);

    if (!get_ids (device, &vendor, &product))
        goto out;

    if (vendor != 0x12d1)
        goto out;

    if (product == 0x1001 || product == 0x1003 || product == 0x1004)
        level = 10;

out:
    if (device)
        g_object_unref (device);
    return level;
}

#if 0
static char *
find_second_port (LibHalContext *ctx, const char *parent)
{
    char **children;
    char *second_port = NULL;
    int num_children = 0;
    int i;

    if (!libhal_device_property_exists (ctx, parent, "usb.interface.number", NULL) ||
        libhal_device_get_property_int (ctx, parent, "usb.interface.number", NULL) != 1)
        return NULL;

    children = libhal_manager_find_device_string_match (ctx, "info.parent", parent, &num_children, NULL);
    for (i = 0; i < num_children && second_port == NULL; i++)
        second_port = libhal_device_get_property_string (ctx, children[i], "serial.device", NULL);

    libhal_free_string_array (children);

    return second_port;
}
#endif

static MMModem *
grab_port (MMPlugin *plugin,
           const char *subsys,
           const char *name,
           GError **error)
{
    MMPluginHuawei *self = MM_PLUGIN_HUAWEI (plugin);
    MMPluginHuaweiPrivate *priv = MM_PLUGIN_HUAWEI_GET_PRIVATE (plugin);
    GUdevDevice *device = NULL, *physdev = NULL;
    const char *devfile, *sysfs_path;
    char *driver = NULL;
    MMModem *modem = NULL;
    guint32 product = 0;

    g_return_val_if_fail (subsys != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    device = get_device (priv->client, subsys, name, &physdev);
    if (!device)
        goto out;

    devfile = g_udev_device_get_device_file (device);
    if (!devfile) {
        g_set_error (error, 0, 0, "Could not get port's sysfs file.");
        goto out;
    }

    driver = get_driver_name (device);
    if (!driver) {
        g_set_error (error, 0, 0, "Could not get port's driver name.");
        goto out;
    }

    sysfs_path = g_udev_device_get_sysfs_path (physdev);
    if (!devfile) {
        g_set_error (error, 0, 0, "Could not get port's physical device sysfs path.");
        goto out;
    }

    if (!get_ids (device, NULL, &product)) {
        g_set_error (error, 0, 0, "Could not get modem product ID.");
        goto out;
    }

    modem = mm_plugin_base_find_modem (MM_PLUGIN_BASE (self), sysfs_path);
    if (!modem) {
        if (product == 0x1001) {
            /* This modem is handled by generic GSM driver */
            modem = mm_generic_gsm_new (sysfs_path,
                                        driver,
                                        mm_plugin_get_name (plugin));
        } else {
            modem = mm_modem_huawei_new (sysfs_path,
                                         driver,
                                         mm_plugin_get_name (plugin));
        }

        if (modem) {
            if (!mm_modem_grab_port (modem, subsys, name, error)) {
g_message ("%s: couldn't grab port", __func__);
                g_object_unref (modem);
                modem = NULL;
                goto out;
            }
            mm_plugin_base_add_modem (MM_PLUGIN_BASE (self), modem);
        }
    } else {
        if (!mm_modem_grab_port (modem, subsys, name, error))
            modem = NULL;
    }

out:
    g_free (driver);
    g_object_unref (device);
    g_object_unref (physdev);
g_message ("%s: port %s / %s modem %p", __func__, subsys, name, modem);
    return modem;
}

static const char *
get_name (MMPlugin *plugin)
{
    return "Huawei";
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
mm_plugin_huawei_init (MMPluginHuawei *self)
{
    MMPluginHuaweiPrivate *priv = MM_PLUGIN_HUAWEI_GET_PRIVATE (self);
    const char *subsys[2] = { "tty", NULL };

    priv->client = g_udev_client_new (subsys);
}

static void
dispose (GObject *object)
{
    MMPluginHuaweiPrivate *priv = MM_PLUGIN_HUAWEI_GET_PRIVATE (object);

    g_object_unref (priv->client);
}

static void
mm_plugin_huawei_class_init (MMPluginHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPluginHuaweiPrivate));

    object_class->dispose = dispose;
}
