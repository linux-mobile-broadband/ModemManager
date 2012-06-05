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
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include "mm-plugin.h"

const char *
mm_plugin_get_name (MMPlugin *plugin)
{
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), NULL);

    return MM_PLUGIN_GET_INTERFACE (plugin)->get_name (plugin);
}

gboolean
mm_plugin_get_sort_last (const MMPlugin *plugin)
{
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), FALSE);

    return MM_PLUGIN_GET_INTERFACE (plugin)->get_sort_last (plugin);
}

void
mm_plugin_supports_port (MMPlugin *self,
                         const gchar *subsys,
                         const gchar *name,
                         const gchar *physdev_path,
                         MMBaseModem *existing,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    g_return_if_fail (MM_IS_PLUGIN (self));
    g_return_if_fail (subsys != NULL);
    g_return_if_fail (name != NULL);
    g_return_if_fail (physdev_path != NULL);
    g_return_if_fail (callback != NULL);

    MM_PLUGIN_GET_INTERFACE (self)->supports_port (self,
                                                   subsys,
                                                   name,
                                                   physdev_path,
                                                   existing,
                                                   callback,
                                                   user_data);
}

MMPluginSupportsResult
mm_plugin_supports_port_finish (MMPlugin *self,
                                GAsyncResult *result,
                                GError **error)
{
    g_return_val_if_fail (MM_IS_PLUGIN (self),
                          MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result),
                          MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED);

    return MM_PLUGIN_GET_INTERFACE (self)->supports_port_finish (self,
                                                                 result,
                                                                 error);
}

void
mm_plugin_supports_port_cancel (MMPlugin *plugin,
                                const char *subsys,
                                const char *name)
{
    g_return_if_fail (MM_IS_PLUGIN (plugin));
    g_return_if_fail (subsys != NULL);
    g_return_if_fail (name != NULL);

    MM_PLUGIN_GET_INTERFACE (plugin)->supports_port_cancel (plugin, subsys, name);
}

MMBaseModem *
mm_plugin_create_modem (MMPlugin *plugin,
                        GList *ports,
                        GError **error)
{
    if (MM_PLUGIN_GET_INTERFACE (plugin)->create_modem)
        return MM_PLUGIN_GET_INTERFACE (plugin)->create_modem (plugin, ports, error);

    return NULL;
}

/*****************************************************************************/

static void
mm_plugin_init (gpointer g_iface)
{
}

GType
mm_plugin_get_type (void)
{
    static GType plugin_type = 0;

    if (!G_UNLIKELY (plugin_type)) {
        const GTypeInfo plugin_info = {
            sizeof (MMPlugin), /* class_size */
            mm_plugin_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        plugin_type = g_type_register_static (G_TYPE_INTERFACE,
                                              "MMPlugin",
                                              &plugin_info, 0);

        g_type_interface_add_prerequisite (plugin_type, G_TYPE_OBJECT);
    }

    return plugin_type;
}
