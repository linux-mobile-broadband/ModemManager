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

typedef struct {
    DBusGConnection *connection;
    GUdevClient *udev;
    GSList *plugins;
    GHashTable *modems;

    GHashTable *supports;
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
    MMPlugin *generic_plugin = NULL;

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

        if (plugin) {
            if (!strcmp (mm_plugin_get_name (plugin), MM_PLUGIN_GENERIC_NAME))
                generic_plugin = plugin;
            else
                priv->plugins = g_slist_append (priv->plugins, plugin);
        }
    }

    /* Make sure the generic plugin is last */
    if (generic_plugin)
        priv->plugins = g_slist_append (priv->plugins, generic_plugin);

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
        g_debug ("Exported modem %s as %s", device, path);
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

static MMModem *
find_modem_for_device (MMManager *manager, const char *device)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, priv->modems);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMModem *modem = MM_MODEM (value);

        if (!strcmp (device, mm_modem_get_device (modem)))
            return modem;
    }
    return NULL;
}


static MMModem *
find_modem_for_port (MMManager *manager, const char *subsys, const char *name)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, priv->modems);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMModem *modem = MM_MODEM (value);

        if (mm_modem_owns_port (modem, subsys, name))
            return modem;
    }
    return NULL;
}

typedef struct {
    MMManager *manager;
    char *subsys;
    char *name;
    GSList *plugins;
    GSList *cur_plugin;
    guint defer_id;
    guint done_id;

    guint32 best_level;
    MMPlugin *best_plugin;
} SupportsInfo;

static SupportsInfo *
supports_info_new (MMManager *self, const char *subsys, const char *name)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (self);
    SupportsInfo *info;

    info = g_malloc0 (sizeof (SupportsInfo));
    info->manager = self;
    info->subsys = g_strdup (subsys);
    info->name = g_strdup (name);
    info->plugins = g_slist_copy (priv->plugins);
    info->cur_plugin = info->plugins;
    return info;
}

static void
supports_info_free (SupportsInfo *info)
{
    /* Cancel any in-process operation on the first plugin */
    if (info->cur_plugin)
        mm_plugin_cancel_supports_port (MM_PLUGIN (info->cur_plugin->data), info->subsys, info->name);

    if (info->defer_id)
        g_source_remove (info->defer_id);

    if (info->done_id)
        g_source_remove (info->done_id);

    g_free (info->subsys);
    g_free (info->name);
    g_slist_free (info->plugins);
    memset (info, 0, sizeof (SupportsInfo));
    g_free (info);
}

static char *
get_key (const char *subsys, const char *name)
{
    return g_strdup_printf ("%s%s", subsys, name);
}


static void supports_callback (MMPlugin *plugin,
                               const char *subsys,
                               const char *name,
                               guint32 level,
                               gpointer user_data);

static void try_supports_port (MMManager *manager,
                               MMPlugin *plugin,
                               const char *subsys,
                               const char *name,
                               SupportsInfo *info);

static gboolean
supports_defer_timeout (gpointer user_data)
{
    SupportsInfo *info = user_data;

    g_debug ("(%s): re-checking support...", info->name);
    try_supports_port (info->manager,
                       MM_PLUGIN (info->cur_plugin->data),
                       info->subsys,
                       info->name,
                       info);
    return FALSE;
}

static void
try_supports_port (MMManager *manager,
                   MMPlugin *plugin,
                   const char *subsys,
                   const char *name,
                   SupportsInfo *info)
{
    MMPluginSupportsResult result;

    result = mm_plugin_supports_port (plugin, subsys, name, supports_callback, info);

    switch (result) {
    case MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED:
        /* If the plugin knows it doesn't support the modem, just call the
         * callback and indicate 0 support.
         */
        supports_callback (plugin, subsys, name, 0, info);
        break;
    case MM_PLUGIN_SUPPORTS_PORT_DEFER:
        g_debug ("(%s): (%s) deferring support check", mm_plugin_get_name (plugin), name);
        if (info->defer_id)
            g_source_remove (info->defer_id);

        /* defer port detection for a bit as requested by the plugin */
        info->defer_id = g_timeout_add (3000, supports_defer_timeout, info);
        break;
    case MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS:
    default:
        break;
    }
}

static gboolean
do_grab_port (gpointer user_data)
{
    SupportsInfo *info = user_data;
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (info->manager);
    MMModem *modem;
    GError *error = NULL;
    char *key;
    GSList *iter;

    /* No more plugins to try */
    if (info->best_plugin) {
        /* Create the modem */
        modem = mm_plugin_grab_port (info->best_plugin, info->subsys, info->name, &error);
        if (modem) {
            guint32 modem_type = MM_MODEM_TYPE_UNKNOWN;
            const char *type_name = "UNKNOWN";

            g_object_get (G_OBJECT (modem), MM_MODEM_TYPE, &modem_type, NULL);
            if (modem_type == MM_MODEM_TYPE_GSM)
                type_name = "GSM";
            else if (modem_type == MM_MODEM_TYPE_CDMA)
                type_name = "CDMA";

            g_message ("(%s): %s modem %s claimed port %s",
                        mm_plugin_get_name (info->best_plugin),
                        type_name,
                        mm_modem_get_device (modem),
                        info->name);

            add_modem (info->manager, modem);
        } else {
            g_warning ("%s: plugin '%s' claimed to support %s/%s but couldn't: (%d) %s",
                        __func__,
                        mm_plugin_get_name (info->best_plugin),
                        info->subsys,
                        info->name,
                        error ? error->code : -1,
                        (error && error->message) ? error->message : "(unknown)");
        }
    }

    /* Tell each plugin to clean up any outstanding supports task */
    for (iter = info->plugins; iter; iter = g_slist_next (iter))
        mm_plugin_cancel_supports_port (MM_PLUGIN (iter->data), info->subsys, info->name);
    g_slist_free (info->plugins);
    info->cur_plugin = info->plugins = NULL;

    key = get_key (info->subsys, info->name);
    g_hash_table_remove (priv->supports, key);
    g_free (key);

    return FALSE;
}

static void
supports_callback (MMPlugin *plugin,
                   const char *subsys,
                   const char *name,
                   guint32 level,
                   gpointer user_data)
{
    SupportsInfo *info = user_data;
    MMPlugin *next_plugin = NULL;

    info->cur_plugin = info->cur_plugin->next;
    if (info->cur_plugin)
        next_plugin = MM_PLUGIN (info->cur_plugin->data);

    /* Is this plugin's result better than any one we've tried before? */
    if (level > info->best_level) {
        info->best_level = level;
        info->best_plugin = plugin;
    }

    /* Prevent the generic plugin from probing devices that are already supported
     * by other plugins.  For Huawei for example, secondary ports shouldn't
     * be probed, but the generic plugin would happily do so if allowed to.
     */
    if (   next_plugin
        && !strcmp (mm_plugin_get_name (next_plugin), MM_PLUGIN_GENERIC_NAME)
        && info->best_plugin)
        next_plugin = NULL;

    if (next_plugin) {
        /* Try the next plugin */
        try_supports_port (info->manager, next_plugin, info->subsys, info->name, info);
    } else {
        /* All done; let the best modem grab the port */
        info->done_id = g_idle_add (do_grab_port, info);
    }
}

static void
device_added (MMManager *manager, GUdevDevice *device)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    const char *subsys, *name;
    SupportsInfo *info;
    char *key;
    gboolean found;

    g_return_if_fail (device != NULL);

    if (!g_slist_length (priv->plugins))
        return;

    subsys = g_udev_device_get_subsystem (device);
    name = g_udev_device_get_name (device);

    if (find_modem_for_port (manager, subsys, name))
        return;

    key = get_key (subsys, name);
    found = !!g_hash_table_lookup (priv->supports, key);
    if (found) {
        g_free (key);
        return;
    }

    info = supports_info_new (manager, subsys, name);
    g_hash_table_insert (priv->supports, key, info);

    try_supports_port (manager, MM_PLUGIN (info->cur_plugin->data), subsys, name, info);
}

static void
device_removed (MMManager *manager, GUdevDevice *device)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (manager);
    MMModem *modem;
    const char *subsys, *name;
    char *key;
    SupportsInfo *info;

    g_return_if_fail (device != NULL);

    if (!g_slist_length (priv->plugins))
        return;

    subsys = g_udev_device_get_subsystem (device);
    name = g_udev_device_get_name (device);

    if (strcmp (subsys, "usb") != 0) {
        /* find_modem_for_port handles tty and net removal */
        modem = find_modem_for_port (manager, subsys, name);
        if (modem) {
            mm_modem_release_port (modem, subsys, name);
            return;
        }
    } else {
        /* This case is designed to handle the case where, at least with kernel 2.6.31, unplugging
         * an in-use ttyACMx device results in udev generating remove events for the usb, but the
         * ttyACMx device (subsystem tty) is not removed, since it was in-use.  So if we have not
         * found a modem for the port (above), we're going to look here to see if we have a modem
         * associated with the newly removed device.  If so, we'll remove the modem, since the
         * device has been removed.  That way, if the device is reinserted later, we'll go through
         * the process of exporting it.
         */
        const char *sysfs_path = g_udev_device_get_sysfs_path (device);

        // g_debug ("Looking for a modem for removed device %s", sysfs_path);
        modem = find_modem_for_device (manager, sysfs_path);
        if (modem) {
            g_debug ("Removing modem claimed by removed device %s", sysfs_path);
            remove_modem (manager, modem);
            return;
        }
    }

    /* Maybe a plugin is checking whether or not the port is supported */
    key = get_key (subsys, name);
    info = g_hash_table_lookup (priv->supports, key);

    if (info) {
        if (info->plugins)
            mm_plugin_cancel_supports_port (MM_PLUGIN (info->plugins->data), subsys, name);
        g_hash_table_remove (priv->supports, key);
    }

    g_free (key);
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

	g_return_if_fail (!strcmp (subsys, "tty") || !strcmp (subsys, "net") || !strcmp (subsys, "usb"));

	/* We only care about tty/net devices when adding modem ports,
	 * but for remove, also handle usb parent device remove events
	 */
	if ((!strcmp (action, "add") || !strcmp (action, "move")) && strcmp (subsys, "usb") !=0 )
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
    const char *subsys[4] = { "tty", "net", "usb", NULL };

    priv->modems = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    load_plugins (manager);

    priv->supports = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) supports_info_free);

    priv->udev = g_udev_client_new (subsys);
    g_assert (priv->udev);
    g_signal_connect (priv->udev, "uevent", G_CALLBACK (handle_uevent), manager);
}

static void
finalize (GObject *object)
{
    MMManagerPrivate *priv = MM_MANAGER_GET_PRIVATE (object);

    g_hash_table_destroy (priv->supports);
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

