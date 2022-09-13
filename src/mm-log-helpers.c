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
 * Copyright (C) 2022 Google, Inc.
 */

#include "mm-log-helpers.h"

static void
common_log_print_array (gpointer     log_object,
                        MMLogLevel   level,
                        const gchar *prefix,
                        GPtrArray   *print_array)
{
    guint i;

    mm_common_str_array_human_keys (print_array);
    for (i = 0; i < print_array->len; i++) {
        mm_obj_log (log_object, level, "%s%s", prefix,
                    (const gchar *)g_ptr_array_index (print_array, i));
    }
}

void
mm_log_simple_connect_properties (gpointer                   log_object,
                                  MMLogLevel                 level,
                                  const gchar               *prefix,
                                  MMSimpleConnectProperties *value)
{
    g_autoptr(GPtrArray) print_array = NULL;

    if (!mm_log_check_level_enabled (level))
      return;

    print_array = mm_simple_connect_properties_print (value, mm_log_get_show_personal_info ());
    common_log_print_array (log_object, level, prefix, print_array);
}

void
mm_log_bearer_properties (gpointer            log_object,
                          MMLogLevel          level,
                          const gchar        *prefix,
                          MMBearerProperties *value)
{
    g_autoptr(GPtrArray) print_array = NULL;

    if (!mm_log_check_level_enabled (level))
      return;

    print_array = mm_bearer_properties_print (value, mm_log_get_show_personal_info ());
    common_log_print_array (log_object, level, prefix, print_array);
}

void
mm_log_3gpp_profile (gpointer       log_object,
                     MMLogLevel     level,
                     const gchar   *prefix,
                     MM3gppProfile *value)
{
    g_autoptr(GPtrArray) print_array = NULL;

    if (!mm_log_check_level_enabled (level))
      return;

    print_array = mm_3gpp_profile_print (value, mm_log_get_show_personal_info ());
    common_log_print_array (log_object, level, prefix, print_array);
}
