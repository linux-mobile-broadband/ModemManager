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
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"

/************************************************************/

static void
common_test (const gchar *keyfile_path)
{
    GKeyFile *keyfile;
    GError   *error = NULL;
    gboolean  ret;

    keyfile = g_key_file_new ();
    ret = g_key_file_load_from_file (keyfile, keyfile_path, G_KEY_FILE_NONE, &error);
    g_assert_no_error (error);
    g_assert (ret);
    g_key_file_unref (keyfile);
}

/************************************************************/

static void
test_dell_dw5821e (void)
{
    common_test (TESTKEYFILE_DELL_DW5821E);
}

/************************************************************/

void
_mm_log (const char *loc,
         const char *func,
         guint32 level,
         const char *fmt,
         ...)
{
    va_list args;
    gchar *msg;

    if (!g_test_verbose ())
        return;

    va_start (args, fmt);
    msg = g_strdup_vprintf (fmt, args);
    va_end (args);
    g_print ("%s\n", msg);
    g_free (msg);
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/test-keyfiles/dell/dw5821e", test_dell_dw5821e);

    return g_test_run ();
}
