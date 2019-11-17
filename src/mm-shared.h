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
 */

#ifndef MM_SHARED_H
#define MM_SHARED_H

#include <glib.h>
#include <glib-object.h>

#define MM_SHARED_MAJOR_VERSION 1
#define MM_SHARED_MINOR_VERSION 0

#if defined (G_HAVE_GNUC_VISIBILITY)
#define VISIBILITY __attribute__((visibility("protected")))
#else
#define VISIBILITY
#endif

#define MM_SHARED_DEFINE_MAJOR_VERSION VISIBILITY int mm_shared_major_version = MM_SHARED_MAJOR_VERSION;
#define MM_SHARED_DEFINE_MINOR_VERSION VISIBILITY int mm_shared_minor_version = MM_SHARED_MINOR_VERSION;
#define MM_SHARED_DEFINE_NAME(NAME)    VISIBILITY const char *mm_shared_name = #NAME;

#endif /* MM_SHARED_H */
