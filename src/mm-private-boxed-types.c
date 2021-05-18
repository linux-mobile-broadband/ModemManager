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
    static gsize g_define_type_id_initialized = 0;

    if (g_once_init_enter (&g_define_type_id_initialized)) {
        GType g_define_type_id =
            g_boxed_type_register_static (g_intern_static_string ("MMUint16Array"),
                                          (GBoxedCopyFunc) uint16_array_copy,
                                          (GBoxedFreeFunc) g_free);

        g_once_init_leave (&g_define_type_id_initialized, g_define_type_id);
    }

    return g_define_type_id_initialized;
}

static mm_uint16_pair *
uint16_pair_array_copy (mm_uint16_pair *array)
{
    mm_uint16_pair *dup;
    guint i;

    if (!array)
        return NULL;

    /* Get 0-terminated array size */
    for (i = 0; array[i].l; i++);

    dup = g_new (mm_uint16_pair, i + 1);
    memcpy (dup, array, i * sizeof (mm_uint16_pair));
    dup[i].l = 0; dup[i].r = 0;
    return dup;
}

GType
mm_uint16_pair_array_get_type (void)
{
    static gsize g_define_type_id_initialized = 0;

    if (g_once_init_enter (&g_define_type_id_initialized)) {
        GType g_define_type_id =
            g_boxed_type_register_static (g_intern_static_string ("MMUint16PairArray"),
                                          (GBoxedCopyFunc) uint16_pair_array_copy,
                                          (GBoxedFreeFunc) g_free);

        g_once_init_leave (&g_define_type_id_initialized, g_define_type_id);
    }

    return g_define_type_id_initialized;
}

static void
str_pair_array_free (mm_str_pair *array)
{
    guint i;

    for (i = 0; array[i].l; i++) {
        g_free (array[i].l);
        g_free (array[i].r);
    }
    g_free (array);
}

static mm_str_pair *
str_pair_array_copy (mm_str_pair *array)
{
    mm_str_pair *dup;
    guint i;

    if (!array)
        return NULL;

    /* Get NULL-terminated array size */
    for (i = 0; array[i].l; i++);

    dup = g_new (mm_str_pair, i + 1);
    for (i = 0; array[i].l; i++) {
        dup[i].l = g_strdup (array[i].l);
        dup[i].r = g_strdup (array[i].r);
    }
    dup[i].l = NULL; dup[i].r = NULL;
    return dup;
}

GType
mm_str_pair_array_get_type (void)
{
    static gsize g_define_type_id_initialized = 0;

    if (g_once_init_enter (&g_define_type_id_initialized)) {
        GType g_define_type_id =
            g_boxed_type_register_static (g_intern_static_string ("MMStrPairArray"),
                                          (GBoxedCopyFunc) str_pair_array_copy,
                                          (GBoxedFreeFunc) str_pair_array_free);

        g_once_init_leave (&g_define_type_id_initialized, g_define_type_id);
    }

    return g_define_type_id_initialized;
}

static gpointer *
pointer_array_copy (gpointer *array)
{
    gpointer *dup;
    guint i;

    if (!array)
        return NULL;

    /* Get NULL-terminated array size */
    for (i = 0; array[i]; i++);

    dup = g_new (gpointer, i + 1);
    memcpy (dup, array, i * sizeof (gpointer));
    dup[i] = NULL;
    return dup;
}

GType
mm_pointer_array_get_type (void)
{
    static gsize g_define_type_id_initialized = 0;

    if (g_once_init_enter (&g_define_type_id_initialized)) {
        GType g_define_type_id =
            g_boxed_type_register_static (g_intern_static_string ("MMPointerArray"),
                                          (GBoxedCopyFunc) pointer_array_copy,
                                          (GBoxedFreeFunc) g_free);

        g_once_init_leave (&g_define_type_id_initialized, g_define_type_id);
    }

    return g_define_type_id_initialized;
}

static GPtrArray *
object_array_copy (GPtrArray *object_array)
{
    return g_ptr_array_ref (object_array);
}

static void
object_array_free (GPtrArray *object_array)
{
    g_ptr_array_unref (object_array);
}

GType
mm_object_array_get_type (void)
{
    static gsize g_define_type_id_initialized = 0;

    if (g_once_init_enter (&g_define_type_id_initialized)) {
        GType g_define_type_id =
            g_boxed_type_register_static (g_intern_static_string ("MMObjectArray"),
                                          (GBoxedCopyFunc) object_array_copy,
                                          (GBoxedFreeFunc) object_array_free);

        g_once_init_leave (&g_define_type_id_initialized, g_define_type_id);
    }

    return g_define_type_id_initialized;
}

static void
async_method_free (MMAsyncMethod *method)
{
    g_slice_free (MMAsyncMethod, method);
}

static MMAsyncMethod *
async_method_copy (MMAsyncMethod *original)
{
    MMAsyncMethod *copy;

    if (!original)
        return NULL;

    copy = g_slice_new (MMAsyncMethod);
    copy->async = original->async;
    copy->finish = original->finish;
    return copy;
}

GType
mm_async_method_get_type (void)
{
    static gsize g_define_type_id_initialized = 0;

    if (g_once_init_enter (&g_define_type_id_initialized)) {
        GType g_define_type_id =
            g_boxed_type_register_static (g_intern_static_string ("MMAsyncMethod"),
                                          (GBoxedCopyFunc) async_method_copy,
                                          (GBoxedFreeFunc) async_method_free);

        g_once_init_leave (&g_define_type_id_initialized, g_define_type_id);
    }

    return g_define_type_id_initialized;
}
