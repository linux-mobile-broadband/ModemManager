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
 * Copyright (C) 2022 Google Inc.
 */

#ifndef MM_BUILTIN_PLUGINS_H
#define MM_BUILTIN_PLUGINS_H

#include <config.h>
#include <glib.h>

#if !defined WITH_BUILTIN_PLUGINS
# error Build with builtin plugins was not enabled
#endif

GList *mm_builtin_plugins_load (void);

#endif /* MM_BUILTIN_PLUGINS_H */
