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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_LOG_TEST_H
#define MM_LOG_TEST_H

#include <glib.h>
#include "mm-log.h"

/* This is a common logging method to be used by all test applications */

void
_mm_log (gpointer     obj,
         const gchar *module,
         const gchar *loc,
         const gchar *func,
         guint32      level,
         const gchar *fmt,
         ...)
{
    va_list  args;
    gchar   *msg;

    if (!g_test_verbose ())
        return;

    va_start (args, fmt);
    msg = g_strdup_vprintf (fmt, args);
    va_end (args);
    g_print ("%s\n", msg);
    g_free (msg);
}

#endif /* MM_LOG_TEST_H */
