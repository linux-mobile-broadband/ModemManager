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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_CONTEXT_H
#define MM_CONTEXT_H

#include <config.h>
#include <glib.h>

#if !defined(MM_DIST_VERSION)
# define MM_DIST_VERSION VERSION
#endif

void mm_context_init (gint argc,
                      gchar **argv);

gboolean     mm_context_get_debug               (void);
const gchar *mm_context_get_log_level           (void);
const gchar *mm_context_get_log_file            (void);
gboolean     mm_context_get_timestamps          (void);
gboolean     mm_context_get_relative_timestamps (void);

/* Testing support */
gboolean     mm_context_get_test_session        (void);
gboolean     mm_context_get_test_no_auto_scan   (void);
gboolean     mm_context_get_test_enable         (void);
const gchar *mm_context_get_test_plugin_dir     (void);

#endif /* MM_CONTEXT_H */
