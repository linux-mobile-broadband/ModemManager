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

#include "mm-log-object.h"
#include "mm-kernel-device-helpers.h"

/******************************************************************************/

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

/******************************************************************************/

static gchar *
build_string_match_pattern (const gchar *str)
{
    GString          *regex_pattern;
    const gchar      *str_start;
    gsize             len;
    g_autofree gchar *aux = NULL;
    gboolean          prefix_match = FALSE;
    gboolean          suffix_match = FALSE;

    /* We allow prefix and suffix matches given as input, by means of the
     * single '*' character given either at the beginning or the end of the
     * string. If given in another place, it will assumed to be explicitly
     * the '*' character, not a catch-all indication. */

    regex_pattern = g_string_new (NULL);

    /* suffix match? */
    if (str[0] == '*') {
        str_start = &str[1];
        suffix_match = TRUE;
    } else
        str_start = str;

    /* prefix match? */
    len = strlen (str_start);
    if (len > 0 && str_start[len - 1] == '*') {
        len--;
        prefix_match = TRUE;
    }

    /* match start of string */
    g_string_append (regex_pattern, "^");

    if (suffix_match)
        g_string_append (regex_pattern, ".*");

    aux = g_regex_escape_string (str_start, len);
    g_string_append (regex_pattern, aux);

    if (prefix_match)
        g_string_append (regex_pattern, ".*");

    /* match end of string */
    g_string_append (regex_pattern, "$");

    return g_string_free (regex_pattern, FALSE);
}

gboolean
mm_kernel_device_generic_string_match (const gchar *str,
                                       const gchar *pattern,
                                       gpointer     log_object)
{
    g_autoptr(GError)      inner_error = NULL;
    g_autoptr(GRegex)      regex = NULL;
    g_autoptr(GMatchInfo)  match_info = NULL;
    g_autofree gchar      *regex_pattern = NULL;

    regex_pattern = build_string_match_pattern (pattern);

    regex = g_regex_new (regex_pattern, G_REGEX_UNGREEDY, 0, &inner_error);
    if (!regex) {
        mm_obj_warn (log_object, "invalid pattern in rule '%s': %s", regex_pattern, inner_error->message);
        return FALSE;
    }
    g_regex_match_full (regex, str, -1, 0, 0, &match_info, &inner_error);
    if (inner_error) {
        mm_obj_warn (log_object, "couldn't apply pattern match in rule '%s': %s", regex_pattern, inner_error->message);
        return FALSE;
    }

    if (!g_match_info_matches (match_info))
        return FALSE;

    mm_obj_dbg (log_object, "pattern '%s' matched: '%s'", regex_pattern, str);
    return TRUE;
}
