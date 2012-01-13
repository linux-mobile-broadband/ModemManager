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
 * Copyright (C) 2012 - Google, Inc.
 */

#include "mm-private-boxed-types.h"
#include "string.h"

static guint16 *
uint16_array_copy (guint16 *array)
{
    guint16 *dup;
    guint i;

    if (!array)
        return NULL;

    /* Get 0-terminated array size */
    for (i = 0; array[i]; i++);

    dup = g_new (guint16, i + 1);
    memcpy (dup, array, i * sizeof (guint16));
    dup[i] = 0;
    return dup;
}

GType
mm_uint16_array_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile)) {
        GType g_define_type_id =
            g_boxed_type_register_static (g_intern_static_string ("MMUint16Array"),
                                          (GBoxedCopyFunc) uint16_array_copy,
                                          (GBoxedFreeFunc) g_free);

        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}
