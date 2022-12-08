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

#ifndef MM_PLUGIN_COMMON_H
#define MM_PLUGIN_COMMON_H

#if !defined MM_MODULE_NAME
# error MM_MODULE_NAME must be defined
#endif

#include <config.h>
#include <glib.h>
#include <glib-object.h>

#include "mm-plugin.h"

#if defined (G_HAVE_GNUC_VISIBILITY)
# define MM_VISIBILITY __attribute__((visibility("protected")))
#else
# define MM_VISIBILITY
#endif

#if defined WITH_BUILTIN_PLUGINS
# define MM_PLUGIN_VERSION
# define MM_PLUGIN_NAMED_CREATOR_SCOPE
# define MM_PLUGIN_CREATOR(unused)
#else
# define MM_PLUGIN_VERSION                                               \
    MM_VISIBILITY int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION; \
    MM_VISIBILITY int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;
# define MM_PLUGIN_NAMED_CREATOR_SCOPE static
# define MM_PLUGIN_CREATOR(my_plugin)                  \
    G_MODULE_EXPORT MMPlugin *mm_plugin_create (void); \
    G_MODULE_EXPORT MMPlugin *                         \
    mm_plugin_create (void)                            \
    {                                                  \
        return mm_plugin_create_##my_plugin ();        \
    }
#endif

#define MM_DEFINE_PLUGIN(MY_PLUGIN, my_plugin, MyPlugin)                                              \
    G_DECLARE_FINAL_TYPE(MMPlugin##MyPlugin, mm_plugin_##my_plugin, MM, PLUGIN_##MY_PLUGIN, MMPlugin) \
    struct _MMPlugin##MyPlugin {                                                                      \
        MMPlugin parent;                                                                              \
    };                                                                                                \
    G_DEFINE_TYPE (MMPlugin##MyPlugin, mm_plugin_##my_plugin, MM_TYPE_PLUGIN)                         \
                                                                                                      \
    MM_PLUGIN_VERSION                                                                                 \
                                                                                                      \
    MM_PLUGIN_NAMED_CREATOR_SCOPE MMPlugin *mm_plugin_create_##my_plugin (void);                      \
    MM_PLUGIN_CREATOR(my_plugin)

#endif /* MM_PLUGIN_COMMON_H */
