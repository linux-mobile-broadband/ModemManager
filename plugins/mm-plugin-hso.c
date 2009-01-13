/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <gmodule.h>
#include "mm-plugin-hso.h"
#include "mm-modem-hso.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginHso, mm_plugin_hso, G_TYPE_OBJECT,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_HSO, NULL));
}

/*****************************************************************************/

static const char *
get_name (MMPlugin *plugin)
{
    return "HSO";
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
get_driver_name (LibHalContext *ctx, const char *udi)
{
    char *parent_udi;
    char *driver = NULL;

    parent_udi = libhal_device_get_property_string (ctx, udi, "info.parent", NULL);
	if (parent_udi) {
		driver = libhal_device_get_property_string (ctx, parent_udi, "info.linux.driver", NULL);
		libhal_free_string (parent_udi);
	}

    return driver;
}

static gboolean
supports_udi (MMPlugin *plugin, LibHalContext *hal_ctx, const char *udi)
{
    char *driver_name;
    gboolean supported = FALSE;

    driver_name = get_driver_name (hal_ctx, udi);
    if (driver_name && !strcmp (driver_name, "hso")) {
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

static char *
get_netdev (LibHalContext *ctx, const char *udi)
{
	char *serial_parent, *netdev = NULL;
	char **netdevs;
	int num, i;

	/* Get the serial interface's originating device UDI, used to find the
	 * originating device's netdev.
	 */
	serial_parent = libhal_device_get_property_string (ctx, udi, "serial.originating_device", NULL);
	if (!serial_parent)
		serial_parent = libhal_device_get_property_string (ctx, udi, "info.parent", NULL);
	if (!serial_parent)
		return NULL;

	/* Look for the originating device's netdev */
	netdevs = libhal_find_device_by_capability (ctx, "net", &num, NULL);
	for (i = 0; netdevs && !netdev && (i < num); i++) {
		char *netdev_parent, *tmp;

		netdev_parent = libhal_device_get_property_string (ctx, netdevs[i], "net.originating_device", NULL);
		if (!netdev_parent)
			netdev_parent = libhal_device_get_property_string (ctx, netdevs[i], "net.physical_device", NULL);
		if (!netdev_parent)
			continue;

		if (!strcmp (netdev_parent, serial_parent)) {
			/* We found it */
			tmp = libhal_device_get_property_string (ctx, netdevs[i], "net.interface", NULL);
			if (tmp) {
				netdev = g_strdup (tmp);
				libhal_free_string (tmp);
			}
		}

		libhal_free_string (netdev_parent);
	}

    if (!netdev) {
        /* Didn't find from netdev's parents. Try again with "grandparents" */
        char *serial_grandparent;

        serial_grandparent = libhal_device_get_property_string (ctx, serial_parent, "info.parent", NULL);
        if (!serial_grandparent)
            goto cleanup;

        for (i = 0; netdevs && !netdev && (i < num); i++) {
            char *netdev_parent, *tmp;

            tmp = libhal_device_get_property_string (ctx, netdevs[i], "net.originating_device", NULL);
            if (!tmp)
                tmp = libhal_device_get_property_string (ctx, netdevs[i], "net.physical_device", NULL);
            if (!tmp)
                tmp = libhal_device_get_property_string (ctx, netdevs[i], "info.parent", NULL);
            if (!tmp)
                continue;

            netdev_parent = libhal_device_get_property_string (ctx, tmp, "info.parent", NULL);
            libhal_free_string (tmp);

            if (netdev_parent) {
                if (!strcmp (netdev_parent, serial_grandparent)) {
                    /* We found it */
                    tmp = libhal_device_get_property_string (ctx, netdevs[i], "net.interface", NULL);
                    if (tmp) {
                        netdev = g_strdup (tmp);
                        libhal_free_string (tmp);
                    }
                }

                libhal_free_string (netdev_parent);
            }
        }
    }

 cleanup:
	libhal_free_string_array (netdevs);
	libhal_free_string (serial_parent);

	return netdev;
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

    driver = get_driver_name (hal_ctx, udi);
    g_return_val_if_fail (driver != NULL, NULL);

    net_device = get_netdev (hal_ctx, udi);
    g_return_val_if_fail (net_device != NULL, NULL);

    modem = MM_MODEM (mm_modem_hso_new (serial_device, net_device, driver));

    g_free (serial_device);
    g_free (net_device);
    g_free (driver);

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
mm_plugin_hso_init (MMPluginHso *self)
{
}

static void
mm_plugin_hso_class_init (MMPluginHsoClass *klass)
{
}
