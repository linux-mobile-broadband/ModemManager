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
 * Copyright (C) 2011 - Google, Inc.
 */

#include <gio/gio.h>

#include "mm-enums-types.h"
#include "mm-common-helpers.h"

gchar *
mm_common_get_capabilities_string (MMModemCapability caps)
{
	GFlagsClass *flags_class;
    GString *str;
    MMModemCapability it;
    gboolean first = TRUE;

    str = g_string_new ("");
    flags_class = G_FLAGS_CLASS (g_type_class_ref (MM_TYPE_MODEM_CAPABILITY));

    for (it = MM_MODEM_CAPABILITY_POTS; /* first */
         it <= MM_MODEM_CAPABILITY_LTE_ADVANCED; /* last */
         it = it << 1) {
        if (caps & it) {
            GFlagsValue *value;

            value = g_flags_get_first_value (flags_class, it);
            g_string_append_printf (str, "%s%s",
                                    first ? "" : ", ",
                                    value->value_nick);

            if (first)
                first = FALSE;
        }
    }
    g_type_class_unref (flags_class);

    return g_string_free (str, FALSE);
}
