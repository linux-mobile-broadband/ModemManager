/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <gmodule.h>
#include <libhal.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "mm-manager.h"
#include "mm-errors.h"
#include "mm-generic-gsm.h"
#include "mm-generic-cdma.h"
#include "mm-plugin.h"

static gboolean impl_manager_enumerate_devices (MMManager *manager,
                                                GPtrArray **devices,
                                                GError **err);

#include "mm-manager-glue.h"

G_DEFINE_TYPE (MMManager, mm_manager, G_TYPE_OBJECT)

enum {
    DEVICE_ADDED,
    DEVICE_REMOVED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define MM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_MANAGER, MMManagerPrivate))

typedef struct {
    DBusGConnection *connection;
    LibHalContext *hal_ctx;
    GSList *plugins;
    GHashTable *modems;
} MMManagerPrivate;

static MMPlugin *
load_plugin (const char *path)
{
    MMPlugin *plugin = NULL;
    GModule *module;
    MMPluginCreateFunc plugin_create_func;
    int *major_plugin_version, *minor_plugin_version;

    module = g_module_open (path, G_MODULE_BIND_LAZY);
    if (!module) {
        g_warning ("Could not load plugin %s: %s", path, g_module_error ());
        return NULL;
    }

    if (!g_module_symbol (module, "mm_plugin_major_version", (gpointer *) &major_plugin_version)) {
        g_warning ("Could not load plugin %s: Missing major version info", path);
        goto out;
    }

    if (*major_plugin_version != MM_PLUGIN_MAJOR_VERSION) {
        g_warning ("Could not load plugin %s: Plugin major version %d, %d is required",
                   path, *major_plugin_version, MM_PLUGIN_MAJOR_VERSION);
        goto out;
    }

    if (!g_module_symbol (module, "mm_plugin_minor_version", (gpointer *) &minor_plugin_version)) {
        g_warning ("Could not load plugin %s: Missing minor version info", path);
        goto out;
    }

    if (*minor_plugin_version != MM_PLUGIN_MINOR_VERSION) {
        g_warning ("Could not load plugin %s: Plugin minor version %d, %d is required",
                   path, *minor_plugin_version, MM_PLUGIN_MINOR_VERSION);
        goto out;
    }

    if (!g_module_symbol (module, "mm_plugin_create", (gpointer *) &plugin_create_func)) {
        g_warning ("Could not load plugin %s: %s", path, g_module_error ());
        goto out;
    }

    plugin = (*plugin_create_func) ();
    if (plugin) {
        g_object_weak_ref (G_OBJECT (plugin), (GWeakNotify) g_module_close, module);
        g_message ("Loaded plugin %s", mm_plugin_get_name (plugin));
    } else
        g_warning ("Could not load plugin %s: initialization failed", path);

 out:
    if (!plugin)
        g_module_close (module);

    return plugin;
}

static void
load_plugins (MMManager *manager)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    GDir *dir;
    const char *fname;

	if (!g_module_supported ()) {
		g_warning ("GModules are not supported on your platform!");
		return;
	}

    dir = g_dir_open (PLUGINDIR, 0, NULL);
    if (!dir) {
        g_warning ("No plugins found");
        return;
    }

    while ((fname = g_dir_read_name (dir)) != NULL) {
        char *path;
        MMPlugin *plugin;

        if (!g_str_has_suffix (fname, G_MODULE_SUFFIX))
            continue;

        path = g_module_build_path (PLUGINDIR, fname);
        plugin = load_plugin (path);
        g_free (path);

        if (plugin)
            priv->plugins = g_slist_append (priv->plugins, plugin);
    }

    g_dir_close (dir);
}

MMManager *
mm_manager_new (void)
{
    return g_object_new (MM_TYPE_MANAGER, NULL);
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

static MMModem *
create_generic_modem (MMManager *manager, const char *udi)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    MMModem *modem;
    char **capabilities;
    char **iter;
    char *serial_device;
    char *driver;
    gboolean type_gsm = FALSE;
    gboolean type_cdma = FALSE;

    capabilities = libhal_device_get_property_strlist (priv->hal_ctx, udi, "modem.command_sets", NULL);
	for (iter = capabilities; iter && *iter; iter++) {
		if (!strcmp (*iter, "GSM-07.07")) {
			type_gsm = TRUE;
			break;
		}
		if (!strcmp (*iter, "IS-707-A")) {
			type_cdma = TRUE;
			break;
		}
	}
	g_strfreev (capabilities);

    if (!type_gsm && !type_cdma)
        return NULL;

    serial_device = libhal_device_get_property_string (priv->hal_ctx, udi, "serial.device", NULL);
    g_return_val_if_fail (serial_device != NULL, NULL);

    driver = get_driver_name (priv->hal_ctx, udi);
    g_return_val_if_fail (driver != NULL, NULL);

    if (type_gsm)
        modem = mm_generic_gsm_new (serial_device, driver);
    else
        modem = mm_generic_cdma_new (serial_device, driver);

    g_free (serial_device);
    g_free (driver);

    if (modem)
        g_debug ("Created new generic modem (%s)", udi);
    else
        g_warning ("Failed to create generic modem (%s)", udi);

    return modem;
}

static void
add_modem (MMManager *manager, const char *udi, MMModem *modem)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);

    g_hash_table_insert (priv->modems, g_strdup (udi), modem);
    dbus_g_connection_register_g_object (priv->connection, udi, G_OBJECT (modem));

    g_signal_emit (manager, signals[DEVICE_ADDED], 0, modem);
}

static MMModem *
modem_exists (MMManager *manager, const char *udi)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);

    return (MMModem *) g_hash_table_lookup (priv->modems, udi);
}

static void
create_initial_modems_from_plugins (MMManager *manager)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    GSList *iter;

    for (iter = priv->plugins; iter; iter = iter->next) {
        MMPlugin *plugin = MM_PLUGIN (iter->data);
        char **udis;
        int i;

        udis = mm_plugin_list_supported_udis (plugin, priv->hal_ctx);
        if (udis) {
            for (i = 0; udis[i]; i++) {
                char *udi = udis[i];
                MMModem *modem;

                if (modem_exists (manager, udi)) {
                    g_warning ("Modem for UDI %s already exists, ignoring", udi);
                    continue;
                }

                modem = mm_plugin_create_modem (plugin, priv->hal_ctx, udi);
                if (modem)
                    add_modem (manager, udi, modem);
            }

            g_strfreev (udis);
        }
    }
}

static void
create_initial_modems_generic (MMManager *manager)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    char **devices;
    int num_devices;
    int i;
    DBusError err;

    dbus_error_init (&err);
    devices = libhal_find_device_by_capability (priv->hal_ctx, "modem", &num_devices, &err);
    if (dbus_error_is_set (&err)) {
        g_warning ("Could not list HAL devices: %s", err.message);
        dbus_error_free (&err);
    }

    if (devices) {
        for (i = 0; i < num_devices; i++) {
            char *udi = devices[i];
            MMModem *modem;

            if (modem_exists (manager, udi))
                /* Already exists, most likely handled by a plugin */
                continue;

            modem = create_generic_modem (manager, udi);
            if (modem)
                add_modem (manager, g_strdup (udi), modem);
        }
    }

    g_strfreev (devices);
}

static void
create_initial_modems (MMManager *manager)
{
    create_initial_modems_from_plugins (manager);
    create_initial_modems_generic (manager);
}

static void
enumerate_devices_cb (gpointer key, gpointer val, gpointer user_data)
{
    GPtrArray **devices = (GPtrArray **) user_data;

    g_ptr_array_add (*devices, g_strdup ((char *) key));
}

static gboolean
impl_manager_enumerate_devices (MMManager *manager,
                                GPtrArray **devices,
                                GError **err)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);

    *devices = g_ptr_array_sized_new (g_hash_table_size (priv->modems));
    g_hash_table_foreach (priv->modems, enumerate_devices_cb, devices);

    return TRUE;
}

static void
device_added (LibHalContext *ctx, const char *udi)
{
    MMManager *manager = MM_MANAGER (libhal_ctx_get_user_data (ctx));
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    GSList *iter;
    MMModem *modem = NULL;

    if (modem_exists (manager, udi))
        /* Shouldn't happen */
        return;

    for (iter = priv->plugins; iter && modem == NULL; iter = iter->next) {
        MMPlugin *plugin = MM_PLUGIN (iter->data);

        if (mm_plugin_supports_udi (plugin, ctx, udi)) {
            modem = mm_plugin_create_modem (plugin, ctx, udi);
            if (modem)
                break;
        }
    }

    if (!modem)
        /* None of the plugins supported the udi, try generic devices */
        modem = create_generic_modem (manager, udi);

    if (modem)
        add_modem (manager, udi, modem);
}

static void
device_removed (LibHalContext *ctx, const char *udi)
{
    MMManager *manager = MM_MANAGER (libhal_ctx_get_user_data (ctx));
    MMModem *modem;

    modem = modem_exists (manager, udi);
    if (modem) {
        g_debug ("Removed modem %s", udi);
        g_signal_emit (manager, signals[DEVICE_REMOVED], 0, modem);
        g_hash_table_remove (MM_MANAGER_GET_PRIVATE (manager)->modems, udi);
    }
}

static void
device_new_capability (LibHalContext *ctx, const char *udi, const char *capability)
{
    device_added (ctx, udi);
}

static void
mm_manager_init (MMManager *manager)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    GError *err = NULL;
	DBusError dbus_error;

    priv->modems = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &err);
    if (!priv->connection)
        g_error ("Could not connect to system bus.");

    dbus_g_connection_register_g_object (priv->connection,
	                                     MM_DBUS_PATH,
	                                     G_OBJECT (manager));

    priv->hal_ctx = libhal_ctx_new ();
	if (!priv->hal_ctx)
		g_error ("Could not get connection to the HAL service.");

	libhal_ctx_set_dbus_connection (priv->hal_ctx, dbus_g_connection_get_connection (priv->connection));

	dbus_error_init (&dbus_error);
	if (!libhal_ctx_init (priv->hal_ctx, &dbus_error))
		g_error ("libhal_ctx_init() failed: %s\n"
                 "Make sure the hal daemon is running?", 
                 dbus_error.message);

    load_plugins (manager);

    libhal_ctx_set_user_data (priv->hal_ctx, manager);
	libhal_ctx_set_device_added (priv->hal_ctx, device_added);
	libhal_ctx_set_device_removed (priv->hal_ctx, device_removed);
	libhal_ctx_set_device_new_capability (priv->hal_ctx, device_new_capability);

    create_initial_modems (manager);
}

static void
finalize (GObject *object)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (object);

    g_hash_table_destroy (priv->modems);

    g_slist_foreach (priv->plugins, (GFunc) g_object_unref, NULL);
    g_slist_free (priv->plugins);

    if (priv->hal_ctx) {
        libhal_ctx_shutdown (priv->hal_ctx, NULL);
        libhal_ctx_free (priv->hal_ctx);
    }

    if (priv->connection)
        dbus_g_connection_unref (priv->connection);

    G_OBJECT_CLASS (mm_manager_parent_class)->finalize (object);
}

static void
mm_manager_class_init (MMManagerClass *manager_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

    g_type_class_add_private (object_class, sizeof (MMManagerPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;

    /* Signals */
    signals[DEVICE_ADDED] =
        g_signal_new ("device-added",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMManagerClass, device_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
					  G_TYPE_NONE, 1,
					  G_TYPE_OBJECT);

    signals[DEVICE_REMOVED] =
        g_signal_new ("device-removed",
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMManagerClass, device_removed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1,
                      G_TYPE_OBJECT);

    dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (manager_class),
									 &dbus_glib_mm_manager_object_info);

    dbus_g_error_domain_register (MM_SERIAL_ERROR, "org.freedesktop.ModemManager.Modem", MM_TYPE_SERIAL_ERROR);
	dbus_g_error_domain_register (MM_MODEM_ERROR, "org.freedesktop.ModemManager.Modem", MM_TYPE_MODEM_ERROR);
    dbus_g_error_domain_register (MM_MODEM_CONNECT_ERROR, "org.freedesktop.ModemManager.Modem", MM_TYPE_MODEM_CONNECT_ERROR);
    dbus_g_error_domain_register (MM_MOBILE_ERROR, "org.freedesktop.ModemManager.Modem.Gsm", MM_TYPE_MOBILE_ERROR);
}
