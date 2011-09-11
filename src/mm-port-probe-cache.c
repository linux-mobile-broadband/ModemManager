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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <glib.h>

#include "mm-port-probe.h"
#include "mm-port-probe-cache.h"

/* Cache of port probing objects */
static GHashTable *cache;

static gchar *
get_key (GUdevDevice *port)
{
    return g_strdup_printf ("%s%s",
                            g_udev_device_get_subsystem (port),
                            g_udev_device_get_name (port));
}

MMPortProbe *
mm_port_probe_cache_get (GUdevDevice *port,
                         const gchar *physdev_path,
                         const gchar *driver)
{
    MMPortProbe *probe = NULL;
    gchar *key;

    key = get_key (port);

    if (G_UNLIKELY (!cache)) {
        cache = g_hash_table_new_full (g_str_hash,
                                       g_str_equal,
                                       g_free,
                                       g_object_unref);
    } else {
        probe = g_hash_table_lookup (cache, key);
    }

    if (!probe) {
        probe = mm_port_probe_new (port, physdev_path, driver);
        g_hash_table_insert (cache, key, probe);
        key = NULL;
    }

    g_free (key);
    return g_object_ref (probe);
}

void
mm_port_probe_cache_remove (GUdevDevice *port)
{
    MMPortProbe *probe = NULL;
    gchar *key;

    if (G_UNLIKELY (!cache))
        return;

    key = get_key (port);
    probe = g_hash_table_lookup (cache, key);
    if (probe) {
        mm_port_probe_run_cancel (probe);
        g_object_unref (probe);
    }
    g_hash_table_remove (cache, key);
    g_free (key);
}


