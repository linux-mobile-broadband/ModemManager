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

/* Default time to defer probing checks */
#define SUPPORTS_DEFER_TIMEOUT_SECS 3

static void initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMPluginManager, mm_plugin_manager, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                               initable_iface_init));

struct _MMPluginManagerPrivate {
    /* The list of plugins. It is loaded once when the program starts, and the
     * list is NOT expected to change after that. */
    GSList *plugins;

    /* Hash table to keep track of support tasks, using physical path of the
     * device as key (which means that more than one tasks may be associated
     * to the same key if the modem happens to show more than one port).
     * The data in each HT item will be SupportsInfoList (not a GSList directly,
     * as we want to be able to modify the list without replacing items with
     * the HT API, which also replaces keys). */
    GHashTable *supports;
};

/* List of support infos associated to the same physical device */
typedef struct {
    GSList *info_list;
} SupportsInfoList;

/* Context of the task looking for best port support */
typedef struct {
    MMPluginManager *self;
    GSimpleAsyncResult *result;
    /* Input context */
    gchar *subsys;
    gchar *name;
    gchar *physdev_path;
    MMModem *existing;
    /* Current context */
    MMPlugin *suggested_plugin;
    GSList *current;
    guint source_id;
    /* Output context */
    MMPlugin *best_plugin;
} SupportsInfo;

static gboolean find_port_support_idle (SupportsInfo *info);

static void
supports_info_free (SupportsInfo *info)
{
    if (!info)
        return;

    /* There shouldn't be any ongoing supports operation */
    g_assert (info->current == NULL);

    /* There shouldn't be any scheduled source */
    g_assert (info->source_id == 0);

    if (info->existing)
        g_object_unref (info->existing);

    g_object_unref (info->result);
    g_free (info->subsys);
    g_free (info->name);
    g_free (info->physdev_path);
    g_free (info);
}

static void
supports_info_list_free (SupportsInfoList *list)
{
    g_slist_foreach (list->info_list,
                     (GFunc)supports_info_free,
                     NULL);
    g_slist_free (list->info_list);
    g_free (list);
}

static void
add_supports_info (MMPluginManager *self,
                   SupportsInfo *info)
{
    SupportsInfoList *list;

    list = g_hash_table_lookup (self->priv->supports,
                                info->physdev_path);
    if (!list) {
        list = g_malloc0 (sizeof (SupportsInfoList));
        g_hash_table_insert (self->priv->supports,
                             g_strdup (info->physdev_path),
                             list);
    }

    list->info_list = g_slist_append (list->info_list, info);
}

static void
remove_supports_info (MMPluginManager *self,
                      SupportsInfo *info)
{
    SupportsInfoList *list;

    list = g_hash_table_lookup (self->priv->supports,
                                info->physdev_path);
    g_assert (list != NULL);
    g_assert (list->info_list != NULL);

    list->info_list = g_slist_remove (list->info_list, info);

    /* If it was the last info for the given physical path,
     * also remove it */
    if (!list->info_list)
        g_hash_table_remove (self->priv->supports,
                             info->physdev_path);

    /* Note that we just remove it from the list, we don't free it */
}

static void
suggest_supports_info_result (MMPluginManager *self,
                              const gchar *physdev_path,
                              MMPlugin *suggested_plugin)
{
    SupportsInfoList *list;
    GSList *l;

    list = g_hash_table_lookup (self->priv->supports,
                                physdev_path);

    if (!list)
        return;

    /* Look for support infos on the same physical path */
    for (l = list->info_list;
         l;
         l = g_slist_next (l)) {
        SupportsInfo *info = l->data;

        if (!info->best_plugin &&
            !info->suggested_plugin) {
            /* TODO: Cancel probing in the port if the plugin being
             * checked right now is not the one being suggested.
             */
            mm_dbg ("(%s): (%s) suggested plugin for port",
                    mm_plugin_get_name (suggested_plugin),
                    info->name);
            info->suggested_plugin = suggested_plugin;
        }
    }
}

static void
supports_port_ready_cb (MMPlugin *plugin,
                        GAsyncResult *result,
                        SupportsInfo *info)
{
    MMPluginSupportsResult support_result;
    GError *error = NULL;

    /* Get supports check results */
    support_result = mm_plugin_supports_port_finish (plugin,
                                                     result,
                                                     &error);
    if (error) {
        mm_warn ("(%s): (%s) error when checking support: '%s'",
                 mm_plugin_get_name (plugin),
                 info->name,
                 error->message);
        g_error_free (error);
    }

    switch (support_result) {
    case MM_PLUGIN_SUPPORTS_PORT_SUPPORTED:
        /* Found a best plugin */
        info->best_plugin = plugin;

        if (info->suggested_plugin &&
            info->suggested_plugin != plugin) {
            /* The last plugin we tried said it supported this port, but it
             * doesn't correspond with the one we're being suggested. */
            g_warn_if_reached ();
        }

        mm_dbg ("(%s): (%s) found best plugin for port",
                mm_plugin_get_name (info->best_plugin),
                info->name);
        info->current = NULL;

        /* Schedule checking support, which will end the operation */
        info->source_id = g_idle_add ((GSourceFunc)find_port_support_idle,
                                      info);
        break;

    case MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED:
        if (info->suggested_plugin) {
            if (info->suggested_plugin == plugin) {
                /* If the plugin that just completed the support check claims
                 * not to support this port, but this plugin is clearly the
                 * right plugin since it claimed this port's physical modem,
                 * just drop the port.
                 */
                mm_dbg ("(%s/%s): ignoring port unsupported by physical modem's plugin",
                        info->subsys,
                        info->name);
                info->best_plugin = NULL;
                info->current = NULL;
            } else {
                /* The last plugin we tried is NOT the one we got suggested, so
                 * directly check support with the suggested plugin. If we
                 * already checked its support, it won't be checked again. */
                info->current = g_slist_find (info->current,
                                              info->suggested_plugin);
            }
        } else {
            /* If the plugin knows it doesn't support the modem, just keep on
             * checking the next plugin.
             */
            info->current = g_slist_next (info->current);
        }
        info->source_id = g_idle_add ((GSourceFunc)find_port_support_idle,
                                      info);
        break;

    case MM_PLUGIN_SUPPORTS_PORT_DEFER:
        /* Try with the suggested one after being deferred */
        if (info->suggested_plugin) {
            mm_dbg ("(%s): (%s) deferring support check, suggested: %s",
                    mm_plugin_get_name (MM_PLUGIN (info->current->data)),
                    info->name,
                    mm_plugin_get_name (MM_PLUGIN (info->suggested_plugin)));
            info->current = g_slist_find (info->current,
                                          info->suggested_plugin);
        } else {
            mm_dbg ("(%s): (%s) deferring support check",
                    mm_plugin_get_name (MM_PLUGIN (info->current->data)),
                    info->name);
        }
        /* Schedule checking support */
        info->source_id = g_timeout_add_seconds (SUPPORTS_DEFER_TIMEOUT_SECS,
                                                 (GSourceFunc)find_port_support_idle,
                                                 info);
        break;
    }
}

static gboolean
find_port_support_idle (SupportsInfo *info)
{
    info->source_id = 0;

    /* Already checked all plugins? */
    if (!info->current) {
        /* Report best plugin in asynchronous result (could be none)
         * Note: plugins are not expected to be removed while these
         * operations are ongoing, so no issue if we don't ref/unref
         * them. */
        g_simple_async_result_set_op_res_gpointer (
            info->result,
            info->best_plugin,
            NULL);

        /* We are only giving the plugin as result, so we can now safely remove
         * the supports info from the manager. Always untrack the supports info
         * before completing the operation. */
        remove_supports_info (info->self, info);

        /* We are reporting a best plugin found to a port. We can now
         * 'suggest' this same plugin to other ports of the same device. */
        if (info->best_plugin) {
            suggest_supports_info_result (info->self,
                                          info->physdev_path,
                                          info->best_plugin);
        }

        /* The asynchronous operation is always completed here */
        g_simple_async_result_complete (info->result);

        supports_info_free (info);
        return FALSE;
    }

    /* Ask the current plugin to check support of this port */
    mm_plugin_supports_port (MM_PLUGIN (info->current->data),
                             info->subsys,
                             info->name,
                             info->physdev_path,
                             info->existing,
                             (GAsyncReadyCallback)supports_port_ready_cb,
                             info);
    return FALSE;
}

MMPlugin *
mm_plugin_manager_find_port_support_finish (MMPluginManager *self,
                                            GAsyncResult *result,
                                            GError **error)
{
    g_return_val_if_fail (MM_IS_PLUGIN_MANAGER (self), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

    /* Propagate error, if any */
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return NULL;

    /* Return the plugin found, if any */
    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
}

void
mm_plugin_manager_find_port_support (MMPluginManager *self,
                                     const gchar *subsys,
                                     const gchar *name,
                                     const gchar *physdev_path,
                                     MMPlugin *suggested_plugin,
                                     MMModem *existing,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    SupportsInfo *info;

    g_return_if_fail (MM_IS_PLUGIN_MANAGER (self));

    /* Setup supports info */
    info = g_malloc0 (sizeof (SupportsInfo));
    info->self = self; /* SupportsInfo lives as long as self lives */
    info->subsys = g_strdup (subsys);
    info->name = g_strdup (name);
    info->physdev_path = g_strdup (physdev_path);
    info->suggested_plugin = suggested_plugin;
    if (existing)
        info->existing = g_object_ref (existing);
    info->result = g_simple_async_result_new (G_OBJECT (self),
                                              callback,
                                              user_data,
                                              NULL);

    /* Set first plugin to check */
    info->current = self->priv->plugins;

    /* If we got one suggested, it will be the first one */
    if (info->suggested_plugin) {
        info->current = g_slist_find (info->current,
                                      info->suggested_plugin);
    }

    /* We will keep track of the supports info internally.
     * Ownership of the supports info will belong to the manager now. */
    add_supports_info (self, info);

    /* Schedule the processing of the supports task in an idle */
    info->source_id = g_idle_add ((GSourceFunc)find_port_support_idle,
                                  info);
}

gboolean
mm_plugin_manager_is_finding_device_support (MMPluginManager *self,
                                             const gchar *physdev_path,
                                             const gchar **subsys,
                                             const gchar **name)
{
    SupportsInfoList *list;

    list = g_hash_table_lookup (self->priv->supports,
                                physdev_path);
    if (list) {
        if (subsys)
            *subsys = ((SupportsInfo *)list->info_list->data)->subsys;
        if (name)
            *name = ((SupportsInfo *)list->info_list->data)->name;

        return TRUE;
    }
    return FALSE;
}

gboolean
mm_plugin_manager_is_finding_port_support (MMPluginManager *self,
                                           const gchar *subsys,
                                           const gchar *name,
                                           const gchar *physdev_path)
{
    SupportsInfoList *list;

    list = g_hash_table_lookup (self->priv->supports,
                                physdev_path);
    if (list) {
        GSList *l;

        for (l = list->info_list;
             l;
             l = g_slist_next (l)) {
            SupportsInfo *info = l->data;

            if (g_str_equal (subsys, info->subsys) &&
                g_str_equal (name, info->name)) {
                /* Support check task already exists */
                return TRUE;
            }
        }
    }

    return FALSE;
}

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

    manager->priv->supports = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        (GDestroyNotify)supports_info_list_free);
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

	/* The Plugin Manager will only be finalized when all support tasks have
     * been finished (as the GSimpleAsyncResult takes a reference to the object.
     * Therefore, the hash table of support tasks should always be empty.
     */
    g_assert (g_hash_table_size (self->priv->supports) == 0);
    g_hash_table_destroy (self->priv->supports);

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

