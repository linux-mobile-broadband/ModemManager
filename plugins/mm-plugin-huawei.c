/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <gmodule.h>
#include "mm-plugin-huawei.h"
#include "mm-modem-huawei.h"

static void plugin_init (MMPlugin *plugin_class);

G_DEFINE_TYPE_EXTENDED (MMPluginHuawei, mm_plugin_huawei, G_TYPE_OBJECT,
                        0, G_IMPLEMENT_INTERFACE (MM_TYPE_PLUGIN, plugin_init))

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_HUAWEI, NULL));
}

/*****************************************************************************/

static const char *
get_name (MMPlugin *plugin)
{
    return "Huawei";
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

static gboolean
supports_udi (MMPlugin *plugin, LibHalContext *hal_ctx, const char *udi)
{
    char **capabilities;
    char **iter;
    gboolean supported = FALSE;

    capabilities = libhal_device_get_property_strlist (hal_ctx, udi, "modem.command_sets", NULL);
	for (iter = capabilities; iter && *iter && !supported; iter++) {
		if (!strcmp (*iter, "GSM-07.07")) {
            char *parent_udi;

            parent_udi = libhal_device_get_property_string (hal_ctx, udi, "info.parent", NULL);
            if (parent_udi) {
                int vendor;
                int product;

                vendor = libhal_device_get_property_int (hal_ctx, parent_udi, "usb.vendor_id", NULL);
                product = libhal_device_get_property_int (hal_ctx, parent_udi, "usb.product_id", NULL);

                if (vendor == 0x12d1 && (product == 0x1001 || product == 0x1003 || product == 0x1004))
                    supported = TRUE;

                libhal_free_string (parent_udi);
            }
		}
    }
	g_strfreev (capabilities);

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

static char *
find_second_port (LibHalContext *ctx, const char *parent)
{
    char **children;
    char *second_port = NULL;
    int num_children = 0;
    int i;

    children = libhal_manager_find_device_string_match (ctx, "info.parent", parent, &num_children, NULL);
    for (i = 0; i < num_children && second_port == NULL; i++) {
        const char *child = children[i];

        if (libhal_device_property_exists (ctx, child, "serial.port", NULL) &&
            libhal_device_get_property_int (ctx, child, "serial.port", NULL) == 1)

            second_port = libhal_device_get_property_string (ctx, child, "serial.device", NULL);
    }

    libhal_free_string_array (children);

    return second_port;
}

static char *
get_monitor_device (LibHalContext *ctx, const char *udi)
{
    char *parent;
    char *grand_parent;
    char **uncles;
    char *monitor_device = NULL;
    int num_uncles = 0;
    int i;

    parent = libhal_device_get_property_string (ctx, udi, "info.parent", NULL);
    grand_parent = libhal_device_get_property_string (ctx, parent, "info.parent", NULL);

    /* Find "uncles" */
    uncles = libhal_manager_find_device_string_match (ctx, "info.parent", grand_parent, &num_uncles, NULL);
    for (i = 0; i < num_uncles && monitor_device == NULL; i++) {
        const char *uncle = uncles[i];

        /* Ignore "dad" */
        if (strcmp (uncle, parent))
            monitor_device = find_second_port (ctx, uncle);
    }

    libhal_free_string_array (uncles);
    libhal_free_string (parent);
    libhal_free_string (grand_parent);

    return monitor_device;
}

static MMModem *
create_modem (MMPlugin *plugin, LibHalContext *hal_ctx, const char *udi)
{
    char *data_device;
    char *monitor_device;
    char *driver;
    MMModem *modem;

    data_device = libhal_device_get_property_string (hal_ctx, udi, "serial.device", NULL);
    g_return_val_if_fail (data_device != NULL, NULL);

    driver = get_driver_name (hal_ctx, udi);
    g_return_val_if_fail (driver != NULL, NULL);

    monitor_device = get_monitor_device (hal_ctx, udi);
    g_debug ("Got monitor device: %s", monitor_device);

    modem = MM_MODEM (mm_modem_huawei_new (data_device, monitor_device, driver));

    libhal_free_string (data_device);
    libhal_free_string (monitor_device);
    libhal_free_string (driver);

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
mm_plugin_huawei_init (MMPluginHuawei *self)
{
}

static void
mm_plugin_huawei_class_init (MMPluginHuaweiClass *klass)
{
}
