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

void
mm_log_bearer_properties (gpointer            log_object,
                          MMLogLevel          level,
                          const gchar        *prefix,
                          MMBearerProperties *properties)
{
    g_autoptr(GPtrArray) properties_print = NULL;
    guint                i;

    if (!mm_log_check_level_enabled (level))
      return;

    properties_print = mm_bearer_properties_print (properties, mm_log_get_show_personal_info ());
    mm_common_str_array_human_keys (properties_print);
    for (i = 0; i < properties_print->len; i++)
        mm_obj_log (log_object, level, "%s%s", prefix,
                    (const gchar *)g_ptr_array_index (properties_print, i));
}
