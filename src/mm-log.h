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
 * Copyright (C) 2020-2022 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2022 Google, Inc.
 */

#ifndef MM_LOG_H
#define MM_LOG_H

#include <glib.h>

/* Log levels */
typedef enum {
    MM_LOG_LEVEL_ERR   = 0x00000001,
    MM_LOG_LEVEL_WARN  = 0x00000002,
    MM_LOG_LEVEL_MSG   = 0x00000004,
    MM_LOG_LEVEL_INFO  = 0x00000008,
    MM_LOG_LEVEL_DEBUG = 0x00000010,
} MMLogLevel;

#if defined MM_MODULE_NAME
# define MM_LOG_MODULE_NAME MM_MODULE_NAME
#else
# define MM_LOG_MODULE_NAME (const gchar *)NULL
#endif

#define mm_obj_log(obj, level, ...) _mm_log (obj, MM_LOG_MODULE_NAME, G_STRLOC, G_STRFUNC, level,              ## __VA_ARGS__ )
#define mm_obj_err(obj, ...)        _mm_log (obj, MM_LOG_MODULE_NAME, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_ERR,   ## __VA_ARGS__ )
#define mm_obj_warn(obj, ...)       _mm_log (obj, MM_LOG_MODULE_NAME, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_WARN,  ## __VA_ARGS__ )
#define mm_obj_msg(obj, ...)        _mm_log (obj, MM_LOG_MODULE_NAME, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_MSG,   ## __VA_ARGS__ )
#define mm_obj_info(obj, ...)       _mm_log (obj, MM_LOG_MODULE_NAME, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_INFO,  ## __VA_ARGS__ )
#define mm_obj_dbg(obj, ...)        _mm_log (obj, MM_LOG_MODULE_NAME, G_STRLOC, G_STRFUNC, MM_LOG_LEVEL_DEBUG, ## __VA_ARGS__ )

/* only allow using non-object logging API if explicitly requested
 * (e.g. in the main daemon source) */
#if defined MM_LOG_NO_OBJECT
# define mm_err(...)  mm_obj_err  (NULL, ## __VA_ARGS__ )
# define mm_warn(...) mm_obj_warn (NULL, ## __VA_ARGS__ )
# define mm_msg(...)  mm_obj_msg  (NULL, ## __VA_ARGS__ )
# define mm_info(...) mm_obj_info (NULL, ## __VA_ARGS__ )
# define mm_dbg(...)  mm_obj_dbg  (NULL, ## __VA_ARGS__ )
#endif

#define mm_log_err_enabled()   mm_log_check_level_enabled (MM_LOG_LEVEL_ERR)
#define mm_log_warn_enabled()  mm_log_check_level_enabled (MM_LOG_LEVEL_WARN)
#define mm_log_msg_enabled()   mm_log_check_level_enabled (MM_LOG_LEVEL_MSG)
#define mm_log_info_enabled()  mm_log_check_level_enabled (MM_LOG_LEVEL_INFO)
#define mm_log_debug_enabled() mm_log_check_level_enabled (MM_LOG_LEVEL_DEBUG)

void _mm_log (gpointer     obj,
              const gchar *module,
              const gchar *loc,
              const gchar *func,
              MMLogLevel   level,
              const gchar *fmt,
              ...)  __attribute__((__format__ (__printf__, 6, 7)));

gboolean mm_log_set_level              (const gchar  *level,
                                        GError      **error);
gboolean mm_log_setup                  (const gchar  *level,
                                        const gchar  *log_file,
                                        gboolean      log_journal,
                                        gboolean      show_ts,
                                        gboolean      rel_ts,
                                        gboolean      show_personal_info,
                                        GError      **error);
gboolean mm_log_check_level_enabled    (MMLogLevel    level);
gboolean mm_log_get_show_personal_info (void);
void     mm_log_shutdown               (void);

/* Helper used when printing a string that may be personal
 * info. Depending on the settings, we may print it as-is,
 * or otherwise provide a fallback string. */
const gchar *mm_log_str_personal_info (const gchar *str);

#endif  /* MM_LOG_H */
