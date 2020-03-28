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
 * Copyright (C) 2011-2020 Red Hat, Inc.
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_LOG_H
#define MM_LOG_H

#include <glib.h>

/* Log levels */
typedef enum {
    MM_LOG_LEVEL_ERR   = 0x00000001,
    MM_LOG_LEVEL_WARN  = 0x00000002,
    MM_LOG_LEVEL_INFO  = 0x00000004,
    MM_LOG_LEVEL_DEBUG = 0x00000008
} MMLogLevel;

#define mm_obj_err(obj, ...)  _mm_log (obj, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_ERR,   ## __VA_ARGS__ )
#define mm_obj_warn(obj, ...) _mm_log (obj, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_WARN,  ## __VA_ARGS__ )
#define mm_obj_info(obj, ...) _mm_log (obj, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_INFO,  ## __VA_ARGS__ )
#define mm_obj_dbg(obj, ...)  _mm_log (obj, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_DEBUG, ## __VA_ARGS__ )

#define mm_err(...)  mm_obj_err  (NULL, ## __VA_ARGS__ )
#define mm_warn(...) mm_obj_warn (NULL, ## __VA_ARGS__ )
#define mm_info(...) mm_obj_info (NULL, ## __VA_ARGS__ )
#define mm_dbg(...)  mm_obj_dbg  (NULL, ## __VA_ARGS__ )

void _mm_log (gpointer    obj,
              const char *loc,
              const char *func,
              MMLogLevel level,
              const char *fmt,
              ...)  __attribute__((__format__ (__printf__, 5, 6)));

gboolean mm_log_set_level (const char *level, GError **error);

gboolean mm_log_setup (const char *level,
                       const char *log_file,
                       gboolean log_journal,
                       gboolean show_ts,
                       gboolean rel_ts,
                       GError **error);

void mm_log_shutdown (void);

#endif  /* MM_LOG_H */
