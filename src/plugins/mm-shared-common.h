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
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2022 Google, Inc.
 */

#ifndef MM_SHARED_COMMON_H
#define MM_SHARED_COMMON_H

#if !defined MM_MODULE_NAME
# error MM_MODULE_NAME must be defined
#endif

#include <config.h>
#include <glib.h>
#include <glib-object.h>

#include "mm-plugin.h"

#if defined (G_HAVE_GNUC_VISIBILITY)
#define MM_VISIBILITY __attribute__((visibility("protected")))
#else
#define MM_VISIBILITY
#endif

#if defined WITH_BUILTIN_PLUGINS
# define MM_DEFINE_SHARED(unused)
#else
# define MM_DEFINE_SHARED(MyShared)                                      \
    MM_VISIBILITY int mm_shared_major_version = MM_SHARED_MAJOR_VERSION; \
    MM_VISIBILITY int mm_shared_minor_version = MM_SHARED_MINOR_VERSION; \
    MM_VISIBILITY const char *mm_shared_name = #MyShared;
#endif

#endif /* MM_SHARED_COMMON_H */
