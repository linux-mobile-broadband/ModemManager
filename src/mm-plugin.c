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

MMPluginSupportsResult
mm_plugin_supports_port (MMPlugin *plugin,
                         const char *subsys,
                         const char *name,
                         const char *physdev_path,
                         MMModem *existing,
                         MMSupportsPortResultFunc callback,
                         gpointer user_data)
{
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), FALSE);
    g_return_val_if_fail (subsys != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (physdev_path != NULL, FALSE);
    g_return_val_if_fail (callback != NULL, FALSE);

    return MM_PLUGIN_GET_INTERFACE (plugin)->supports_port (plugin,
                                                            subsys,
                                                            name,
                                                            physdev_path,
                                                            existing,
                                                            callback,
                                                            user_data);
}

void
mm_plugin_cancel_supports_port (MMPlugin *plugin,
                                const char *subsys,
                                const char *name)
{
    g_return_if_fail (MM_IS_PLUGIN (plugin));
    g_return_if_fail (subsys != NULL);
    g_return_if_fail (name != NULL);

    MM_PLUGIN_GET_INTERFACE (plugin)->cancel_supports_port (plugin, subsys, name);
}

MMModem *
mm_plugin_grab_port (MMPlugin *plugin,
                     const char *subsys,
                     const char *name,
                     MMModem *existing,
                     GError **error)
{
    g_return_val_if_fail (MM_IS_PLUGIN (plugin), FALSE);
    g_return_val_if_fail (subsys != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return MM_PLUGIN_GET_INTERFACE (plugin)->grab_port (plugin, subsys, name, existing, error);
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
