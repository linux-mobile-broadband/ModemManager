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
 * Copyright (C) 2011 Red Hat, Inc.
 */

#ifndef MM_LOG_H
#define MM_LOG_H

#include <glib.h>

/* Log levels */
enum {
    LOGL_ERR   = 0x00000001,
    LOGL_WARN  = 0x00000002,
    LOGL_INFO  = 0x00000004,
    LOGL_DEBUG = 0x00000008
};

#define mm_err(...) \
    _mm_log (G_STRLOC, G_STRFUNC, LOGL_ERR, ## __VA_ARGS__ )

#define mm_warn(...) \
    _mm_log (G_STRLOC, G_STRFUNC, LOGL_WARN, ## __VA_ARGS__ )

#define mm_info(...) \
    _mm_log (G_STRLOC, G_STRFUNC, LOGL_INFO, ## __VA_ARGS__ )

#define mm_dbg(...) \
    _mm_log (G_STRLOC, G_STRFUNC, LOGL_DEBUG, ## __VA_ARGS__ )

#define mm_log(level, ...) \
    _mm_log (G_STRLOC, G_STRFUNC, level, ## __VA_ARGS__ )

void _mm_log (const char *loc,
              const char *func,
              guint32 level,
              const char *fmt,
              ...)  __attribute__((__format__ (__printf__, 4, 5)));

gboolean mm_log_set_level (const char *level, GError **error);

gboolean mm_log_setup (const char *level,
                       const char *log_file,
                       gboolean show_ts,
                       gboolean rel_ts,
                       gboolean debug_func_loc,
                       GError **error);

void mm_log_shutdown (void);

#endif  /* MM_LOG_H */
