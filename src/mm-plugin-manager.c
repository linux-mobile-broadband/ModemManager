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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <string.h>
#include <ctype.h>

#include <gmodule.h>
#include <gio/gio.h>

#include "mm-plugin-manager.h"
#include "mm-plugin.h"
#include "mm-errors.h"
#include "mm-log.h"

static void initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMPluginManager, mm_plugin_manager, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                               initable_iface_init));

struct _MMPluginManagerPrivate {
    /* The list of plugins. It is loaded once when the program starts, and the
     * list is NOT expected to change after that. */
    GSList *plugins;
};

static MMPlugin *
load_plugin (const gchar *path)
{
    MMPlugin *plugin = NULL;
    GModule *module;
    MMPluginCreateFunc plugin_create_func;
    gint *major_plugin_version;
    gint *minor_plugin_version;
    gchar *path_display;

    /* Get printable UTF-8 string of the path */
    path_display = g_filename_display_name (path);

    module = g_module_open (path, G_MODULE_BIND_LAZY);
    if (!module) {
        g_warning ("Could not load plugin '%s': %s", path_display, g_module_error ());
        goto out;
    }

    if (!g_module_symbol (module, "mm_plugin_major_version", (gpointer *) &major_plugin_version)) {
        g_warning ("Could not load plugin '%s': Missing major version info", path_display);
        goto out;
    }

    if (*major_plugin_version != MM_PLUGIN_MAJOR_VERSION) {
        g_warning ("Could not load plugin '%s': Plugin major version %d, %d is required",
                   path_display, *major_plugin_version, MM_PLUGIN_MAJOR_VERSION);
        goto out;
    }

    if (!g_module_symbol (module, "mm_plugin_minor_version", (gpointer *) &minor_plugin_version)) {
        g_warning ("Could not load plugin '%s': Missing minor version info", path_display);
        goto out;
    }

    if (*minor_plugin_version != MM_PLUGIN_MINOR_VERSION) {
        g_warning ("Could not load plugin '%s': Plugin minor version %d, %d is required",
                   path_display, *minor_plugin_version, MM_PLUGIN_MINOR_VERSION);
        goto out;
    }

    if (!g_module_symbol (module, "mm_plugin_create", (gpointer *) &plugin_create_func)) {
        g_warning ("Could not load plugin '%s': %s", path_display, g_module_error ());
        goto out;
    }

    plugin = (*plugin_create_func) ();
    if (plugin) {
        g_object_weak_ref (G_OBJECT (plugin), (GWeakNotify) g_module_close, module);
    } else
        mm_warn ("Could not load plugin '%s': initialization failed", path_display);

out:
    if (module && !plugin)
        g_module_close (module);

    g_free (path_display);

    return plugin;
}

static gint
compare_plugins (const MMPlugin *plugin_a,
                 const MMPlugin *plugin_b)
{
    /* The order of the plugins in the list is the same order used to check
     * whether the plugin can manage a given modem:
     *  - First, modems that will check vendor ID from udev.
     *  - Then, modems that report to be sorted last (those which will check
     *    vendor ID also from the probed ones..
     */
    if (mm_plugin_get_sort_last (plugin_a) &&
        !mm_plugin_get_sort_last (plugin_b))
        return 1;
    if (!mm_plugin_get_sort_last (plugin_a) &&
        mm_plugin_get_sort_last (plugin_b))
        return -1;
    return 0;
}

static void
found_plugin (MMPlugin *plugin)
{
    mm_info ("Loaded plugin '%s'", mm_plugin_get_name (plugin));
}

static gboolean
load_plugins (MMPluginManager *self,
              GError **error)
{
    GDir *dir = NULL;
    const gchar *fname;
    MMPlugin *generic_plugin = NULL;
    gchar *plugindir_display = NULL;

	if (!g_module_supported ()) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "GModules are not supported on your platform!");
        goto out;
	}

    /* Get printable UTF-8 string of the path */
    plugindir_display = g_filename_display_name (PLUGINDIR);

    mm_dbg ("Looking for plugins in '%s'", plugindir_display);
    dir = g_dir_open (PLUGINDIR, 0, NULL);
    if (!dir) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NO_PLUGINS,
                     "Plugin directory '%s' not found",
                     plugindir_display);
        goto out;
    }

    while ((fname = g_dir_read_name (dir)) != NULL) {
        gchar *path;
        MMPlugin *plugin;

        if (!g_str_has_suffix (fname, G_MODULE_SUFFIX))
            continue;

        path = g_module_build_path (PLUGINDIR, fname);
        plugin = load_plugin (path);
        g_free (path);

        if (plugin) {
            if (g_str_equal (mm_plugin_get_name (plugin),
                             MM_PLUGIN_GENERIC_NAME))
                generic_plugin = plugin;
            else
                self->priv->plugins = g_slist_append (self->priv->plugins,
                                                      plugin);
        }
    }

    /* Sort last plugins that request it */
    self->priv->plugins = g_slist_sort (self->priv->plugins,
                                        (GCompareFunc)compare_plugins);

    /* Make sure the generic plugin is last */
    if (generic_plugin)
        self->priv->plugins = g_slist_append (self->priv->plugins,
                                              generic_plugin);

    /* Treat as error if we don't find any plugin */
    if (!self->priv->plugins) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NO_PLUGINS,
                     "No plugins found in plugin directory '%s'",
                     plugindir_display);
        goto out;
    }

    /* Now report about all the found plugins, in the same order they will be
     * used while checking support */
    g_slist_foreach (self->priv->plugins, (GFunc)found_plugin, NULL);

    mm_info ("Successfully loaded %u plugins",
             g_slist_length (self->priv->plugins));

out:
    if (dir)
        g_dir_close (dir);
    g_free (plugindir_display);

    return !!self->priv->plugins;
}

MMPluginManager *
mm_plugin_manager_new (GError **error)
{
    return g_initable_new (MM_TYPE_PLUGIN_MANAGER,
                           NULL,
                           error,
                           NULL);
}

static void
mm_plugin_manager_init (MMPluginManager *manager)
{
    /* Initialize opaque pointer to private data */
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
                                                 MM_TYPE_PLUGIN_MANAGER,
                                                 MMPluginManagerPrivate);
}

static gboolean
initable_init (GInitable *initable,
               GCancellable *cancellable,
               GError **error)
{
    /* Load the list of plugins */
    return load_plugins (MM_PLUGIN_MANAGER (initable), error);
}

static void
finalize (GObject *object)
{
    MMPluginManager *self = MM_PLUGIN_MANAGER (object);

    /* Cleanup list of plugins */
    g_slist_foreach (self->priv->plugins, (GFunc)g_object_unref, NULL);
    g_slist_free (self->priv->plugins);

    G_OBJECT_CLASS (mm_plugin_manager_parent_class)->finalize (object);
}

static void
initable_iface_init (GInitableIface *iface)
{
	iface->init = initable_init;
}

static void
mm_plugin_manager_class_init (MMPluginManagerClass *manager_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

    g_type_class_add_private (object_class, sizeof (MMPluginManagerPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}

