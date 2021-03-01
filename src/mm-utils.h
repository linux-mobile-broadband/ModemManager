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
 * Singleton support imported from NetworkManager.
 *     (C) Copyright 2014 Red Hat, Inc.
 *
 * GPtrArray lookup with GEqualFunc imported from GLib 2.48
 */

#ifndef MM_UTILS_H
#define MM_UTILS_H

#include <glib.h>
#include <glib-object.h>

#include "mm-log-object.h"

/*****************************************************************************/

#define MM_DEFINE_SINGLETON_INSTANCE(TYPE)      \
    static TYPE *singleton_instance;

#define MM_DEFINE_SINGLETON_WEAK_REF(TYPE)                              \
    static void                                                         \
    _singleton_instance_weak_ref_cb (gpointer data,                     \
                                     GObject *where_the_object_was)     \
    {                                                                   \
        mm_obj_dbg (singleton_instance, "singleton disposed");          \
        singleton_instance = NULL;                                      \
    }                                                                   \
    static inline void                                                  \
    mm_singleton_instance_weak_ref_register (void)                      \
    {                                                                   \
        g_object_weak_ref (G_OBJECT (singleton_instance), _singleton_instance_weak_ref_cb, NULL); \
    }

#define MM_DEFINE_SINGLETON_DESTRUCTOR(TYPE)                            \
    static void __attribute__((destructor))                             \
    _singleton_destructor (void)                                        \
    {                                                                   \
        if (singleton_instance) {                                       \
            if (G_OBJECT (singleton_instance)->ref_count > 1)           \
                mm_obj_dbg (singleton_instance, "singleton disowned");  \
            g_object_unref (singleton_instance);                        \
        }                                                               \
    }

/* By default, the getter will assert that the singleton will be created only once. You can
 * change this by redefining MM_DEFINE_SINGLETON_ALLOW_MULTIPLE. */
#ifndef MM_DEFINE_SINGLETON_ALLOW_MULTIPLE
#define MM_DEFINE_SINGLETON_ALLOW_MULTIPLE     FALSE
#endif

#define MM_DEFINE_SINGLETON_GETTER(TYPE, GETTER, GTYPE, ...)            \
    MM_DEFINE_SINGLETON_INSTANCE (TYPE)                                 \
    MM_DEFINE_SINGLETON_WEAK_REF (TYPE)                                 \
    TYPE *                                                              \
    GETTER (void)                                                       \
    {                                                                   \
        if (G_UNLIKELY (!singleton_instance)) {                         \
            static char _already_created = FALSE;                       \
                                                                        \
            g_assert (!_already_created || (MM_DEFINE_SINGLETON_ALLOW_MULTIPLE));        \
            _already_created = TRUE;                                                     \
            singleton_instance = (g_object_new (GTYPE, ##__VA_ARGS__, NULL));            \
            g_assert (singleton_instance);                              \
            mm_singleton_instance_weak_ref_register ();                 \
            mm_obj_dbg (singleton_instance, "singleton created");       \
        }                                                               \
        return singleton_instance;                                      \
    }                                                                   \
    MM_DEFINE_SINGLETON_DESTRUCTOR(TYPE)


#if !GLIB_CHECK_VERSION(2,54,0)

/* Pointer Array lookup with a GEqualFunc, imported from GLib 2.54 */
#define g_ptr_array_find_with_equal_func mm_ptr_array_find_with_equal_func
gboolean mm_ptr_array_find_with_equal_func (GPtrArray     *haystack,
                                            gconstpointer  needle,
                                            GEqualFunc     equal_func,
                                            guint         *index_);

#endif

#endif /* MM_UTILS_H */
