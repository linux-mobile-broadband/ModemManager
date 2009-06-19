/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string.h>
#include <gmodule.h>
#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "mm-manager.h"
#include "mm-errors.h"
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

#define DBUS_PATH_TAG "dbus-path"

typedef struct {
    DBusGConnection *connection;
    GUdevClient *udev;
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
mm_manager_new (DBusGConnection *bus)
{
    MMManager *manager;

    g_return_val_if_fail (bus != NULL, NULL);

    manager = (MMManager *) g_object_new (MM_TYPE_MANAGER, NULL);
    if (manager) {
        MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);

        priv->connection = dbus_g_connection_ref (bus);
        dbus_g_connection_register_g_object (priv->connection,
                                             MM_DBUS_PATH,
                                             G_OBJECT (manager));
    }

    return manager;
}

static void
remove_modem (MMManager *manager, MMModem *modem)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    char *device;

    device = mm_modem_get_device (modem);
    g_assert (device);
    g_debug ("Removed modem %s", device);

    g_signal_emit (manager, signals[DEVICE_REMOVED], 0, modem);
    g_hash_table_remove (priv->modems, device);
    g_free (device);
}

static void
modem_valid (MMModem *modem, GParamSpec *pspec, gpointer user_data)
{
    MMManager *manager = MM_MANAGER (user_data);
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    static guint32 id = 0;
    char *path, *device;

    if (mm_modem_get_valid (modem)) {
        path = g_strdup_printf (MM_DBUS_PATH"/Modems/%d", id++);
        dbus_g_connection_register_g_object (priv->connection, path, G_OBJECT (modem));
        g_object_set_data_full (G_OBJECT (modem), DBUS_PATH_TAG, path, (GDestroyNotify) g_free);

        device = mm_modem_get_device (modem);
        g_assert (device);
        g_debug ("Exported modem %s", device);
        g_free (device);

        g_signal_emit (manager, signals[DEVICE_ADDED], 0, modem);
    } else
        remove_modem (manager, modem);
}

static void
add_modem (MMManager *manager, MMModem *modem)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    char *device;
    gboolean valid = FALSE;

    device = mm_modem_get_device (modem);
    g_assert (device);
    if (!g_hash_table_lookup (priv->modems, device)) {
        g_hash_table_insert (priv->modems, g_strdup (device), modem);
        g_debug ("Added modem %s", device);
        g_signal_connect (modem, "notify::valid", G_CALLBACK (modem_valid), manager);
        g_object_get (modem, MM_MODEM_VALID, &valid, NULL);
        if (valid)
            modem_valid (modem, NULL, manager);
    }
    g_free (device);
}

static void
enumerate_devices_cb (gpointer key, gpointer val, gpointer user_data)
{
    MMModem *modem = MM_MODEM (val);
    GPtrArray **devices = (GPtrArray **) user_data;
    const char *path;
    gboolean valid = FALSE;

    g_object_get (G_OBJECT (modem), MM_MODEM_VALID, &valid, NULL);
    if (valid) {
        path = g_object_get_data (G_OBJECT (modem), DBUS_PATH_TAG);
        g_return_if_fail (path != NULL);
        g_ptr_array_add (*devices, g_strdup (path));
    }
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

typedef struct {
    MMModem *modem;
    const char *subsys;
    const char *name;
} FindPortInfo;

static void
find_port (gpointer key, gpointer data, gpointer user_data)
{
    FindPortInfo *info = user_data;
    MMModem *modem = MM_MODEM (data);

    if (!info->modem && mm_modem_owns_port (modem, info->subsys, info->name))
        info->modem = modem;
}

static MMModem *
find_modem_for_port (MMManager *manager, const char *subsys, const char *name)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    FindPortInfo info = { NULL, subsys, name };

    g_hash_table_foreach (priv->modems, find_port, &info);
    return info.modem;
}

static void
device_added (MMManager *manager, GUdevDevice *device)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    GSList *iter;
    MMModem *modem = NULL;
    const char *subsys, *name;
    MMPlugin *best_plugin = NULL;
    guint32 best_level = 0;
    GError *error = NULL;

    g_return_if_fail (device != NULL);

    subsys = g_udev_device_get_subsystem (device);
    name = g_udev_device_get_name (device);

    if (find_modem_for_port (manager, subsys, name))
        return;

    /* Build up the list of plugins that support this port */
    for (iter = priv->plugins; iter; iter = iter->next) {
        MMPlugin *plugin = MM_PLUGIN (iter->data);
        guint32 level;

        level = mm_plugin_supports_port (plugin, subsys, name);
        if (level > best_level) {
            best_plugin = plugin;
            best_level = level;
        }
    }

    /* Let the best plugin handle this port */
    if (!best_plugin)
        return;

    modem = mm_plugin_grab_port (best_plugin, subsys, name, &error);
    if (modem) {
        guint32 modem_type = MM_MODEM_TYPE_UNKNOWN;
        const char *type_name = "UNKNOWN";

        g_object_get (G_OBJECT (modem), MM_MODEM_TYPE, &modem_type, NULL);
        if (modem_type == MM_MODEM_TYPE_GSM)
            type_name = "GSM";
        else if (modem_type == MM_MODEM_TYPE_CDMA)
            type_name = "CDMA";

        g_message ("(%s): %s modem %s claimed port %s",
                   mm_plugin_get_name (best_plugin),
                   type_name,
                   mm_modem_get_device (modem),
                   name);
    } else {
        g_warning ("%s: plugin '%s' claimed to support %s/%s but couldn't: (%d) %s",
                   __func__, mm_plugin_get_name (best_plugin), subsys, name,
                   error ? error->code : -1,
                   (error && error->message) ? error->message : "(unknown)");
        return;
    }

    add_modem (manager, modem);
}

static void
device_removed (MMManager *manager, GUdevDevice *device)
{
    MMModem *modem;
    const char *subsys, *name;

    g_return_if_fail (device != NULL);

    subsys = g_udev_device_get_subsystem (device);
    name = g_udev_device_get_name (device);
    modem = find_modem_for_port (manager, subsys, name);
    if (modem)
        mm_modem_release_port (modem, subsys, name);
}

static void
handle_uevent (GUdevClient *client,
               const char *action,
               GUdevDevice *device,
               gpointer user_data)
{
    MMManager *self = MM_MANAGER (user_data);
	const char *subsys;

	g_return_if_fail (action != NULL);

	/* A bit paranoid */
	subsys = g_udev_device_get_subsystem (device);
	g_return_if_fail (subsys != NULL);

	g_return_if_fail (!strcmp (subsys, "tty") || !strcmp (subsys, "net"));

	if (!strcmp (action, "add"))
		device_added (self, device);
	else if (!strcmp (action, "remove"))
		device_removed (self, device);
}

void
mm_manager_start (MMManager *manager)
{
    MMManagerPrivate *priv;
    GList *devices, *iter;

    g_return_if_fail (manager != NULL);
    g_return_if_fail (MM_IS_MANAGER (manager));

    priv = MM_MANAGER_GET_PRIVATE (manager);

    devices = g_udev_client_query_by_subsystem (priv->udev, "tty");
    for (iter = devices; iter; iter = g_list_next (iter))
        device_added (manager, G_UDEV_DEVICE (iter->data));

    devices = g_udev_client_query_by_subsystem (priv->udev, "net");
    for (iter = devices; iter; iter = g_list_next (iter))
        device_added (manager, G_UDEV_DEVICE (iter->data));
}

static void
mm_manager_init (MMManager *manager)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    const char *subsys[3] = { "tty", "net", NULL };

    priv->modems = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    load_plugins (manager);

    priv->udev = g_udev_client_new (subsys);
    g_assert (priv->udev);
    g_signal_connect (priv->udev, "uevent", G_CALLBACK (handle_uevent), manager);
}

static void
finalize (GObject *object)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (object);

    g_hash_table_destroy (priv->modems);

    g_slist_foreach (priv->plugins, (GFunc) g_object_unref, NULL);
    g_slist_free (priv->plugins);

    if (priv->udev)
        g_object_unref (priv->udev);

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
