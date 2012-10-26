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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#include <string.h>
#include <ctype.h>

#include <gmodule.h>
#include <gio/gio.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-plugin-manager.h"
#include "mm-plugin.h"
#include "mm-log.h"

/* Default time to defer probing checks */
#define DEFER_TIMEOUT_SECS 3

/* Time to wait for other ports to appear once the first port is exposed */
#define MIN_PROBING_TIME_SECS 2

static void initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMPluginManager, mm_plugin_manager, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                               initable_iface_init));

struct _MMPluginManagerPrivate {
    /* This list contains all plugins except for the generic one, order is not
     * important. It is loaded once when the program starts, and the list is NOT
     * expected to change after that.*/
    GList *plugins;
    /* Last, the generic plugin. */
    MMPlugin *generic;
};

/*****************************************************************************/
/* Find device support */

typedef struct {
    MMPluginManager *self;
    MMDevice *device;
    GSimpleAsyncResult *result;
    guint timeout_id;
    gulong grabbed_id;
    gulong released_id;

    GList *running_probes;
} FindDeviceSupportContext;

typedef struct {
    FindDeviceSupportContext *parent_ctx;
    GUdevDevice *port;

    GList *plugins;
    GList *current;
    MMPlugin *best_plugin;
    MMPlugin *suggested_plugin;
    guint defer_id;
    gboolean defer_until_suggested;
} PortProbeContext;

static void port_probe_context_step (PortProbeContext *port_probe_ctx);
static void suggest_port_probe_result (FindDeviceSupportContext *ctx,
                                       PortProbeContext *origin,
                                       MMPlugin *suggested_plugin);

static void
port_probe_context_free (PortProbeContext *ctx)
{
    g_assert (ctx->defer_id == 0);

    if (ctx->best_plugin)
        g_object_unref (ctx->best_plugin);
    if (ctx->suggested_plugin)
        g_object_unref (ctx->suggested_plugin);
    if (ctx->plugins)
        g_list_free_full (ctx->plugins, (GDestroyNotify)g_object_unref);
    g_object_unref (ctx->port);
    g_slice_free (PortProbeContext, ctx);
}

static void
find_device_support_context_complete_and_free (FindDeviceSupportContext *ctx)
{
    g_assert (ctx->timeout_id == 0);

    /* Set async operation result */
    if (!mm_device_peek_plugin (ctx->device)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_UNSUPPORTED,
                                         "not supported by any plugin");
    } else {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    g_simple_async_result_complete (ctx->result);

    g_signal_handler_disconnect (ctx->device, ctx->grabbed_id);
    g_signal_handler_disconnect (ctx->device, ctx->released_id);

    g_warn_if_fail (ctx->running_probes == NULL);

    g_object_unref (ctx->result);
    g_object_unref (ctx->device);
    g_object_unref (ctx->self);
    g_slice_free (FindDeviceSupportContext, ctx);
}

gboolean
mm_plugin_manager_find_device_support_finish (MMPluginManager *self,
                                              GAsyncResult *result,
                                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void
port_probe_context_finished (PortProbeContext *port_probe_ctx)
{
    FindDeviceSupportContext *ctx = port_probe_ctx->parent_ctx;

    if (!port_probe_ctx->best_plugin) {
        gboolean cancel_remaining;
        GList *l;

        mm_dbg ("(Plugin Manager) [%s] not supported by any plugin",
                g_udev_device_get_name (port_probe_ctx->port));

        /* Tell the device to ignore this port */
        mm_device_ignore_port (ctx->device, port_probe_ctx->port);

        /* If this is the last valid probe which was running (i.e. the last one
         * not being deferred-until-suggested), cancel all remaining ones. */
        cancel_remaining = TRUE;
        for (l = ctx->running_probes; l; l = g_list_next (l)) {
            PortProbeContext *other = l->data;

            /* Do not cancel anything if we find at least one probe which is not
             * waiting for the suggested plugin */
            if (other != port_probe_ctx && !other->defer_until_suggested) {
                cancel_remaining = FALSE;
                break;
            }
        }

        if (cancel_remaining)
            /* Set a NULL suggested plugin, will cancel the probes */
            suggest_port_probe_result (ctx, port_probe_ctx, NULL);

    } else {
        MMPlugin *device_plugin;

        device_plugin = (MMPlugin *)mm_device_peek_plugin (ctx->device);

        /* Notify the plugin to the device, if this is the first port probing
         * result we got.
         * Also, if the previously suggested plugin was the GENERIC one and now
         * we're reporting a more specific one, use the new one.
         */
        if (!device_plugin ||
            (g_str_equal (mm_plugin_get_name (device_plugin), MM_PLUGIN_GENERIC_NAME) &&
             device_plugin != port_probe_ctx->best_plugin)) {
            /* Only log best plugin if it's not the generic one */
            if (!g_str_equal (mm_plugin_get_name (port_probe_ctx->best_plugin), MM_PLUGIN_GENERIC_NAME))
                mm_dbg ("(Plugin Manager) (%s) [%s]: found best plugin for device (%s)",
                        mm_plugin_get_name (port_probe_ctx->best_plugin),
                        g_udev_device_get_name (port_probe_ctx->port),
                        mm_device_get_path (ctx->device));

            mm_device_set_plugin (ctx->device, G_OBJECT (port_probe_ctx->best_plugin));

            /* Suggest this plugin also to other port probes */
            suggest_port_probe_result (ctx, port_probe_ctx, port_probe_ctx->best_plugin);
        }
        /* Warn if the best plugin found for this port differs from the
         * best plugin found for the the first probed port */
        else if (!g_str_equal (mm_plugin_get_name (device_plugin),
                               mm_plugin_get_name (port_probe_ctx->best_plugin)))
            mm_warn ("(Plugin Manager) (%s): plugin mismatch error (expected: '%s', got: '%s')",
                     g_udev_device_get_name (port_probe_ctx->port),
                     mm_plugin_get_name (MM_PLUGIN (mm_device_peek_plugin (ctx->device))),
                     mm_plugin_get_name (port_probe_ctx->best_plugin));
    }

    /* Remove us from the list of running probes */
    g_assert (g_list_find (ctx->running_probes, port_probe_ctx) != NULL);
    ctx->running_probes = g_list_remove (ctx->running_probes, port_probe_ctx);
    port_probe_context_free (port_probe_ctx);

    /* If there are running probes around, wait for them to finish */
    if (ctx->running_probes != NULL)
        return;

    /* If we didn't use the minimum probing time, wait for it to finish */
    if (ctx->timeout_id > 0)
        return;

    /* If we just finished the last running probe, we can now finish the device
     * support check */
    find_device_support_context_complete_and_free (ctx);
}

static gboolean
deferred_support_check_idle (PortProbeContext *port_probe_ctx)
{
    port_probe_ctx->defer_id = 0;
    port_probe_context_step (port_probe_ctx);
    return FALSE;
}

static void
suggest_port_probe_result (FindDeviceSupportContext *ctx,
                           PortProbeContext *origin,
                           MMPlugin *suggested_plugin)
{
    GList *l;

    for (l = ctx->running_probes; l; l = g_list_next (l)) {
        PortProbeContext *port_probe_ctx = l->data;

        /* Plugin suggestions serve two different purposes here:
         *  1) Finish all the probes which were deferred until suggested.
         *  2) Suggest to other probes which plugin to test next.
         *
         * The exception here is when we suggest the GENERIC plugin.
         * In this case, only purpose (1) is applied, this is, only
         * the deferred until suggested probes get finished.
         */

        if (port_probe_ctx != origin &&
            !port_probe_ctx->best_plugin &&
            !port_probe_ctx->suggested_plugin) {
            /* If we got a task deferred until a suggestion comes,
             * complete it */
            if (port_probe_ctx->defer_until_suggested) {
                if (suggested_plugin) {
                    mm_dbg ("(Plugin Manager) (%s) [%s] deferred task completed, got suggested plugin",
                            mm_plugin_get_name (suggested_plugin),
                            g_udev_device_get_name (port_probe_ctx->port));
                    /* Advance to the suggested plugin and re-check support there */
                    port_probe_ctx->suggested_plugin = g_object_ref (suggested_plugin);
                    port_probe_ctx->current = g_list_find (port_probe_ctx->current,
                                                           port_probe_ctx->suggested_plugin);
                } else {
                    mm_dbg ("(Plugin Manager) [%s] deferred task cancelled, no suggested plugin",
                            g_udev_device_get_name (port_probe_ctx->port));
                    port_probe_ctx->best_plugin = NULL;
                    port_probe_ctx->current = NULL;
                }

                /* Schedule checking support, which will end the operation */
                g_assert (port_probe_ctx->defer_id == 0);
                port_probe_ctx->defer_id = g_idle_add ((GSourceFunc)deferred_support_check_idle,
                                                       port_probe_ctx);
            }
            /* TODO: Cancel probing in the port if the plugin being
             * checked right now is not the one being suggested.
             */
            else if (suggested_plugin &&
                     /* The GENERIC plugin is NEVER suggested to others */
                     !g_str_equal (mm_plugin_get_name (suggested_plugin),
                                   MM_PLUGIN_GENERIC_NAME)) {
                mm_dbg ("(Plugin Manager) (%s) [%s] suggested plugin for port",
                        mm_plugin_get_name (suggested_plugin),
                        g_udev_device_get_name (port_probe_ctx->port));
                port_probe_ctx->suggested_plugin = g_object_ref (suggested_plugin);
            }
        }
    }
}

static void
plugin_supports_port_ready (MMPlugin *plugin,
                            GAsyncResult *result,
                            PortProbeContext *port_probe_ctx)
{
    MMPluginSupportsResult support_result;
    GError *error = NULL;

    /* Get supports check results */
    support_result = mm_plugin_supports_port_finish (plugin, result, &error);

    if (error) {
        mm_warn ("(Plugin Manager) (%s) [%s] error when checking support: '%s'",
                 mm_plugin_get_name (plugin),
                 g_udev_device_get_name (port_probe_ctx->port),
                 error->message);
        g_error_free (error);
    }

    switch (support_result) {
    case MM_PLUGIN_SUPPORTS_PORT_SUPPORTED:
        /* Found a best plugin */
        port_probe_ctx->best_plugin = g_object_ref (plugin);

        if (port_probe_ctx->suggested_plugin &&
            port_probe_ctx->suggested_plugin != plugin) {
            /* The last plugin we tried said it supported this port, but it
             * doesn't correspond with the one we're being suggested. */
            g_warn_if_reached ();
        }

        mm_dbg ("(Plugin Manager) (%s) [%s] found best plugin for port",
                mm_plugin_get_name (port_probe_ctx->best_plugin),
                g_udev_device_get_name (port_probe_ctx->port));
        port_probe_ctx->current = NULL;

        /* Step, which will end the port probe operation */
        port_probe_context_step (port_probe_ctx);
        return;


    case MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED:
        if (port_probe_ctx->suggested_plugin) {
            if (port_probe_ctx->suggested_plugin == plugin) {
                /* If the plugin that just completed the support check claims
                 * not to support this port, but this plugin is clearly the
                 * right plugin since it claimed this port's physical modem,
                 * just drop the port.
                 */
                mm_dbg ("(Plugin Manager) [%s] ignoring port unsupported by physical modem's plugin",
                        g_udev_device_get_name (port_probe_ctx->port));
                port_probe_ctx->best_plugin = NULL;
                port_probe_ctx->current = NULL;
            } else {
                /* The last plugin we tried is NOT the one we got suggested, so
                 * directly check support with the suggested plugin. If we
                 * already checked its support, it won't be checked again. */
                port_probe_ctx->current = g_list_find (port_probe_ctx->current,
                                                       port_probe_ctx->suggested_plugin);
            }
        } else {
            /* If the plugin knows it doesn't support the modem, just keep on
             * checking the next plugin.
             */
            port_probe_ctx->current = g_list_next (port_probe_ctx->current);
        }

        /* Step */
        port_probe_context_step (port_probe_ctx);
        return;


    case MM_PLUGIN_SUPPORTS_PORT_DEFER:
        /* Try with the suggested one after being deferred */
        if (port_probe_ctx->suggested_plugin) {
            mm_dbg ("(Plugin Manager) (%s) [%s] deferring support check, suggested: %s",
                    mm_plugin_get_name (MM_PLUGIN (port_probe_ctx->current->data)),
                    g_udev_device_get_name (port_probe_ctx->port),
                    mm_plugin_get_name (MM_PLUGIN (port_probe_ctx->suggested_plugin)));
            port_probe_ctx->current = g_list_find (port_probe_ctx->current,
                                                   port_probe_ctx->suggested_plugin);
        } else {
            mm_dbg ("(Plugin Manager) (%s) [%s] deferring support check",
                    mm_plugin_get_name (MM_PLUGIN (port_probe_ctx->current->data)),
                    g_udev_device_get_name (port_probe_ctx->port));
        }

        /* Schedule checking support */
        port_probe_ctx->defer_id = g_timeout_add_seconds (DEFER_TIMEOUT_SECS,
                                                          (GSourceFunc)deferred_support_check_idle,
                                                          port_probe_ctx);
        return;


    case MM_PLUGIN_SUPPORTS_PORT_DEFER_UNTIL_SUGGESTED:
        /* If we arrived here and we already have a plugin suggested, use it */
        if (port_probe_ctx->suggested_plugin) {
            if (port_probe_ctx->suggested_plugin == plugin) {
                mm_dbg ("(Plugin Manager) (%s) [%s] task completed, got suggested plugin",
                        mm_plugin_get_name (port_probe_ctx->suggested_plugin),
                        g_udev_device_get_name (port_probe_ctx->port));
                port_probe_ctx->best_plugin = g_object_ref (port_probe_ctx->suggested_plugin);
                port_probe_ctx->current = NULL;
            } else {
                mm_dbg ("(Plugin Manager) (%s) [%s] re-checking support on deferred task, got suggested plugin",
                        mm_plugin_get_name (port_probe_ctx->suggested_plugin),
                        g_udev_device_get_name (port_probe_ctx->port));
                port_probe_ctx->current = g_list_find (port_probe_ctx->current,
                                                       port_probe_ctx->suggested_plugin);
            }

            /* Schedule checking support, which will end the operation */
            port_probe_context_step (port_probe_ctx);
            return;
        }

        /* We are deferred until a suggested plugin is given. If last supports task
         * of a given device is finished without finding a best plugin, this task
         * will get finished reporting unsupported. */
        mm_dbg ("(Plugin Manager) [%s] deferring support check until result suggested",
                g_udev_device_get_name (port_probe_ctx->port));
        port_probe_ctx->defer_until_suggested = TRUE;
        return;
    }
}

static void
port_probe_context_step (PortProbeContext *port_probe_ctx)
{
    FindDeviceSupportContext *ctx = port_probe_ctx->parent_ctx;

    /* Already checked all plugins? */
    if (!port_probe_ctx->current) {
        port_probe_context_finished (port_probe_ctx);
        return;
    }

    /* Ask the current plugin to check support of this port */
    mm_plugin_supports_port (MM_PLUGIN (port_probe_ctx->current->data),
                             ctx->device,
                             port_probe_ctx->port,
                             (GAsyncReadyCallback)plugin_supports_port_ready,
                             port_probe_ctx);
}

static GList *
build_plugins_list (MMPluginManager *self,
                    MMDevice *device,
                    GUdevDevice *port)
{
    GList *list = NULL;
    GList *l;
    gboolean supported_found = FALSE;

    for (l = self->priv->plugins; l && !supported_found; l = g_list_next (l)) {
        MMPluginSupportsHint hint;

        hint = mm_plugin_discard_port_early (MM_PLUGIN (l->data), device, port);
        switch (hint) {
        case MM_PLUGIN_SUPPORTS_HINT_UNSUPPORTED:
            /* Fully discard */
            break;
        case MM_PLUGIN_SUPPORTS_HINT_MAYBE:
            /* Maybe supported, add to tail of list */
            list = g_list_append (list, g_object_ref (l->data));
            break;
        case MM_PLUGIN_SUPPORTS_HINT_LIKELY:
            /* Likely supported, add to head of list */
            list = g_list_prepend (list, g_object_ref (l->data));
            break;
        case MM_PLUGIN_SUPPORTS_HINT_SUPPORTED:
            /* Really supported, clean existing list and add it alone */
            if (list) {
                g_list_free_full (list, (GDestroyNotify)g_object_unref);
                list = NULL;
            }
            list = g_list_prepend (list, g_object_ref (l->data));
            /* This will end the loop as well */
            supported_found = TRUE;
            break;
        default:
            g_assert_not_reached();
        }
    }

    /* Add the generic plugin at the end of the list */
    if (self->priv->generic)
        list = g_list_append (list, g_object_ref (self->priv->generic));

    mm_info ("(Plugin Manager) [%s] Found '%u' plugins to try...",
             g_udev_device_get_name (port),
             g_list_length (list));
    for (l = list; l; l = g_list_next (l)) {
        mm_info ("(Plugin Manager) [%s]   Will try with plugin '%s'",
                 g_udev_device_get_name (port),
                 mm_plugin_get_name (MM_PLUGIN (l->data)));
    }

    return list;
}

static void
device_port_grabbed_cb (MMDevice *device,
                        GUdevDevice *port,
                        FindDeviceSupportContext *ctx)
{
    PortProbeContext *port_probe_ctx;


    /* Launch probing task on this port with the first plugin of the list */
    port_probe_ctx = g_slice_new0 (PortProbeContext);
    port_probe_ctx->parent_ctx = ctx;
    port_probe_ctx->port = g_object_ref (port);

    /* Setup plugins to probe and first one to check */
    port_probe_ctx->plugins = build_plugins_list (ctx->self, device, port);
    port_probe_ctx->current = port_probe_ctx->plugins;

    /* If we got one suggested, it will be the first one */
    port_probe_ctx->suggested_plugin = (!!mm_device_peek_plugin (device) ?
                                        MM_PLUGIN (mm_device_get_plugin (device)) :
                                        NULL);
    if (port_probe_ctx->suggested_plugin)
        port_probe_ctx->current = g_list_find (port_probe_ctx->current,
                                               port_probe_ctx->suggested_plugin);

    /* Set as running */
    ctx->running_probes = g_list_prepend (ctx->running_probes, port_probe_ctx);

    /* Launch supports check in the Plugin Manager */
    port_probe_context_step (port_probe_ctx);
}

static void
device_port_released_cb (MMDevice *device,
                         GUdevDevice *port,
                         FindDeviceSupportContext *ctx)
{
    /* TODO: abort probing on that port */
}

static gboolean
min_probing_timeout_cb (FindDeviceSupportContext *ctx)
{
    ctx->timeout_id = 0;

    /* If there are no running probes around, we're free to finish */
    if (ctx->running_probes == NULL)
        find_device_support_context_complete_and_free (ctx);

    return FALSE;
}

void
mm_plugin_manager_find_device_support (MMPluginManager *self,
                                       MMDevice *device,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    FindDeviceSupportContext *ctx;

    ctx = g_slice_new0 (FindDeviceSupportContext);
    ctx->self = g_object_ref (self);
    ctx->device = g_object_ref (device);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_plugin_manager_find_device_support);

    /* Connect to device port grabbed/released notifications */
    ctx->grabbed_id = g_signal_connect (device,
                                        MM_DEVICE_PORT_GRABBED,
                                        G_CALLBACK (device_port_grabbed_cb),
                                        ctx);
    ctx->released_id = g_signal_connect (device,
                                         MM_DEVICE_PORT_RELEASED,
                                         G_CALLBACK (device_port_released_cb),
                                         ctx);

    /* Set the initial timeout of 2s. We force the probing time of the device to
     * be at least this amount of time, so that the kernel has enough time to
     * bring up ports. Given that we launch this only when the first port of the
     * device has been exposed in udev, this timeout effectively means that we
     * leave up to 2s to the remaining ports to appear. */
    ctx->timeout_id = g_timeout_add_seconds (MIN_PROBING_TIME_SECS,
                                             (GSourceFunc)min_probing_timeout_cb,
                                             ctx);
}

/*****************************************************************************/

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

static gboolean
load_plugins (MMPluginManager *self,
              GError **error)
{
    GDir *dir = NULL;
    const gchar *fname;
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

        if (!plugin)
            continue;

        mm_info ("Loaded plugin '%s'", mm_plugin_get_name (plugin));

        if (g_str_equal (mm_plugin_get_name (plugin), MM_PLUGIN_GENERIC_NAME))
            /* Generic plugin */
            self->priv->generic = plugin;
        else
            /* Vendor specific plugin */
            self->priv->plugins = g_list_append (self->priv->plugins, plugin);
    }

    /* Check the generic plugin once all looped */
    if (!self->priv->generic)
        mm_warn ("Generic plugin not loaded");

    /* Treat as error if we don't find any plugin */
    if (!self->priv->plugins && !self->priv->generic) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NO_PLUGINS,
                     "No plugins found in plugin directory '%s'",
                     plugindir_display);
        goto out;
    }

    mm_info ("Successfully loaded %u plugins",
             g_list_length (self->priv->plugins) + !!self->priv->generic);

out:
    if (dir)
        g_dir_close (dir);
    g_free (plugindir_display);

    /* Return TRUE if at least one plugin found */
    return (self->priv->plugins || self->priv->generic);
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
dispose (GObject *object)
{
    MMPluginManager *self = MM_PLUGIN_MANAGER (object);

    /* Cleanup list of plugins */
    if (self->priv->plugins) {
        g_list_free_full (self->priv->plugins, (GDestroyNotify)g_object_unref);
        self->priv->plugins = NULL;
    }
    g_clear_object (&self->priv->generic);

    G_OBJECT_CLASS (mm_plugin_manager_parent_class)->dispose (object);
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
    object_class->dispose = dispose;
}
