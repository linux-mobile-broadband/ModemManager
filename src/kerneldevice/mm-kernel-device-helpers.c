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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <stdlib.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "mm-kernel-device-helpers.h"

gchar *
mm_kernel_device_get_lower_device_name (const gchar *sysfs_path)
{
    g_autoptr(GFile)           dirfile = NULL;
    g_autoptr(GFileEnumerator) direnum = NULL;

    dirfile = g_file_new_for_path (sysfs_path);
    direnum = g_file_enumerate_children (dirfile,
                                         G_FILE_ATTRIBUTE_STANDARD_NAME,
                                         G_FILE_QUERY_INFO_NONE,
                                         NULL,
                                         NULL);
    if (!direnum)
        return NULL;

    while (TRUE) {
        GFileInfo        *info;
        g_autofree gchar *filename = NULL;
        g_autofree gchar *link_path = NULL;
        g_autofree gchar *real_path = NULL;

        if (!g_file_enumerator_iterate (direnum, &info, NULL, NULL, NULL) || !info)
            break;

        filename = g_file_info_get_attribute_as_string (info, G_FILE_ATTRIBUTE_STANDARD_NAME);
        if (!filename || !g_str_has_prefix (filename, "lower_"))
            continue;

        link_path = g_strdup_printf ("%s/%s", sysfs_path, filename);
        real_path = realpath (link_path, NULL);
        if (!real_path)
            continue;

        return g_path_get_basename (real_path);
    }

    return NULL;
}
