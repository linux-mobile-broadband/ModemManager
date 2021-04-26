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
#include <config.h>

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-test.h"

/************************************************************/

static void
common_test (const gchar *keyfile_path)
{
    GKeyFile *keyfile;
    GError   *error = NULL;
    gboolean  ret;

    if (!keyfile_path)
        return;

    keyfile = g_key_file_new ();
    ret = g_key_file_load_from_file (keyfile, keyfile_path, G_KEY_FILE_NONE, &error);
    g_assert_no_error (error);
    g_assert (ret);
    g_key_file_unref (keyfile);
}

/* Dummy test to avoid compiler warning about common_test() being unused
 * when none of the plugins enabled in build have custom key files. */
static void
test_dummy (void)
{
    common_test (NULL);
}

/************************************************************/

#if defined ENABLE_PLUGIN_FOXCONN
static void
test_foxconn (void)
{
    common_test (TESTKEYFILE_FOXCONN);
}
#endif

/************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/MM/test-keyfiles/dummy", test_dummy);

#if defined ENABLE_PLUGIN_FOXCONN
    g_test_add_func ("/MM/test-keyfiles/foxconn", test_foxconn);
#endif

    return g_test_run ();
}
