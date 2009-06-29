/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2008 Ericsson AB
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <string.h>
#include <gmodule.h>
#include "mm-plugin-mbm.h"
#include "mm-modem-mbm.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginMbm, mm_plugin_mbm, G_TYPE_OBJECT,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_MBM, NULL));
}

/*****************************************************************************/

static const char *
get_name (MMPlugin *plugin)
{
    return "MBM";
}

static char **
list_supported_udis (MMPlugin *plugin, LibHalContext *hal_ctx)
{
    char **supported = NULL;
    char **devices;
    int num_devices;
    int i;

    devices = libhal_find_device_by_capability (hal_ctx, "modem", &num_devices, NULL);
    if (devices) {
        GPtrArray *array;

        array = g_ptr_array_new ();

        for (i = 0; i < num_devices; i++) {
            char *udi = devices[i];

            if (mm_plugin_supports_udi (plugin, hal_ctx, udi))
                g_ptr_array_add (array, g_strdup (udi));
        }

        if (array->len > 0) {
            g_ptr_array_add (array, NULL);
            supported = (char **) g_ptr_array_free (array, FALSE);
        } else
            g_ptr_array_free (array, TRUE);
    }

    g_strfreev (devices);

    return supported;
}

static char *
get_netdev (LibHalContext *ctx, const char *udi)
{
    char *serial_parent, *serial_parent_parent, *netdev = NULL;
    char **netdevs;
    int num, i;

    /* Get the origin udi, which is parent of our parent */
    serial_parent = libhal_device_get_property_string (ctx, udi, "info.parent", NULL);
    if (!serial_parent)
        return NULL;
    /* Just attach to first cdc-acm interface */
    if (strncmp (serial_parent + strlen (serial_parent) - 4, "_if1", 4))
        return NULL;
    serial_parent_parent = libhal_device_get_property_string (ctx, serial_parent, "info.parent", NULL);
    if (!serial_parent_parent)
        return NULL;

    /* Look for the originating device's netdev */
    netdevs = libhal_find_device_by_capability (ctx, "net", &num, NULL);
    for (i = 0; netdevs && !netdev && (i < num); i++) {
        char *netdev_parent, *netdev_parent_parent, *tmp;

        /* Get the origin udi, which also is parent of our parent */
        netdev_parent = libhal_device_get_property_string (ctx, netdevs[i], "info.parent", NULL);
        if (!netdev_parent)
            continue;
        netdev_parent_parent = libhal_device_get_property_string (ctx, netdev_parent, "info.parent", NULL);
        if (!netdev_parent_parent)
            continue;

        if (!strcmp (netdev_parent_parent, serial_parent_parent)) {
            /* We found it */
            tmp = libhal_device_get_property_string (ctx, netdevs[i], "net.interface", NULL);
            if (tmp) {
                netdev = g_strdup (tmp);
                libhal_free_string (tmp);
            }
        }

        libhal_free_string (netdev_parent);
        libhal_free_string (netdev_parent_parent);
    }
    libhal_free_string_array (netdevs);
    libhal_free_string (serial_parent);
    libhal_free_string (serial_parent_parent);

    return netdev;
}

static char *
get_driver (LibHalContext *ctx, const char *udi)
{
    char *serial_parent, *serial_parent_parent, *driver = NULL;
    char **netdevs;
    int num, i;

    /* Get the origin udi, which is parent of our parent */
    serial_parent = libhal_device_get_property_string (ctx, udi, "info.parent", NULL);
    if (!serial_parent)
        return NULL;
    serial_parent_parent = libhal_device_get_property_string (ctx, serial_parent, "info.parent", NULL);
    if (!serial_parent_parent)
        return NULL;

    /* Look for the originating device's netdev */
    netdevs = libhal_find_device_by_capability (ctx, "net", &num, NULL);
    for (i = 0; netdevs && !driver && (i < num); i++) {
        char *netdev_parent, *netdev_parent_parent, *tmp;

        /* Get the origin udi, which also is parent of our parent */
        netdev_parent = libhal_device_get_property_string (ctx, netdevs[i], "info.parent", NULL);
        if (!netdev_parent)
            continue;
        netdev_parent_parent = libhal_device_get_property_string (ctx, netdev_parent, "info.parent", NULL);
        if (!netdev_parent_parent)
            continue;

        if (!strcmp (netdev_parent_parent, serial_parent_parent)) {
            /* We found it */
            tmp = libhal_device_get_property_string (ctx,
                                                     netdev_parent, "info.linux.driver", NULL);
            if (tmp) {
                driver = g_strdup (tmp);
                libhal_free_string (tmp);
            }
        }

        libhal_free_string (netdev_parent);
        libhal_free_string (netdev_parent_parent);
    }
    libhal_free_string_array (netdevs);
    libhal_free_string (serial_parent);
    libhal_free_string (serial_parent_parent);

    return driver;
}

static gboolean
supports_udi (MMPlugin *plugin, LibHalContext *hal_ctx, const char *udi)
{
    char *driver_name;
    gboolean supported = FALSE;

    driver_name = get_driver (hal_ctx, udi);
    if (driver_name && (!strcmp (driver_name, "cdc_ether") || !strcmp (driver_name, "mbm"))) {
        char **capabilities;
        char **iter;

        capabilities = libhal_device_get_property_strlist (hal_ctx, udi, "modem.command_sets", NULL);
        for (iter = capabilities; iter && *iter && !supported; iter++) {
            if (!strcmp (*iter, "GSM-07.07") || !strcmp (*iter, "GSM-07.05")) {
                supported = TRUE;
                break;
            }
        }

        libhal_free_string_array (capabilities);
    }

    libhal_free_string (driver_name);

    return supported;
}

static MMModem *
create_modem (MMPlugin *plugin, LibHalContext *hal_ctx, const char *udi)
{
    char *serial_device;
    char *net_device;
    char *driver;
    MMModem *modem;

    serial_device = libhal_device_get_property_string (hal_ctx, udi, "serial.device", NULL);
    g_return_val_if_fail (serial_device != NULL, NULL);

    net_device = get_netdev (hal_ctx, udi);
    g_return_val_if_fail (net_device != NULL, NULL);

    driver = get_driver (hal_ctx, udi);
    g_return_val_if_fail (driver != NULL, NULL);

    modem = MM_MODEM (mm_modem_mbm_new (serial_device, net_device, driver));

    g_free (serial_device);
    g_free (net_device);

    return modem;
}

/*****************************************************************************/

static void
plugin_init (MMPlugin *plugin_class)
{
    /* interface implementation */
    plugin_class->get_name = get_name;
    plugin_class->list_supported_udis = list_supported_udis;
    plugin_class->supports_udi = supports_udi;
    plugin_class->create_modem = create_modem;
}

static void
mm_plugin_mbm_init (MMPluginMbm *self)
{
}

static void
mm_plugin_mbm_class_init (MMPluginMbmClass *klass)
{
}
