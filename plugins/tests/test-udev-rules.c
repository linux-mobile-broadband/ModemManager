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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-kernel-device-generic-rules.h"
#include "mm-log.h"

/************************************************************/

static void
common_test (const gchar *plugindir)
{
    GArray *rules;
    GError *error = NULL;

    rules = mm_kernel_device_generic_rules_load (plugindir, &error);
    g_assert_no_error (error);
    g_assert (rules);
    g_assert (rules->len > 0);

    g_array_unref (rules);
}

/************************************************************/

static void
test_huawei (void)
{
    common_test (TESTUDEVRULESDIR_HUAWEI);
}

static void
test_mbm (void)
{
    common_test (TESTUDEVRULESDIR_MBM);
}

static void
test_nokia (void)
{
    common_test (TESTUDEVRULESDIR_NOKIA);
}

static void
test_zte (void)
{
    common_test (TESTUDEVRULESDIR_ZTE);
}

static void
test_longcheer (void)
{
    common_test (TESTUDEVRULESDIR_LONGCHEER);
}

static void
test_simtech (void)
{
    common_test (TESTUDEVRULESDIR_SIMTECH);
}

static void
test_x22x (void)
{
    common_test (TESTUDEVRULESDIR_X22X);
}

static void
test_cinterion (void)
{
    common_test (TESTUDEVRULESDIR_CINTERION);
}

static void
test_dell (void)
{
    common_test (TESTUDEVRULESDIR_DELL);
}

static void
test_telit (void)
{
    common_test (TESTUDEVRULESDIR_TELIT);
}

static void
test_mtk (void)
{
    common_test (TESTUDEVRULESDIR_MTK);
}

static void
test_haier (void)
{
    common_test (TESTUDEVRULESDIR_HAIER);
}

static void
test_fibocom (void)
{
    common_test (TESTUDEVRULESDIR_FIBOCOM);
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

    g_test_add_func ("/MM/test-udev-rules/huawei",    test_huawei);
    g_test_add_func ("/MM/test-udev-rules/mbm",       test_mbm);
    g_test_add_func ("/MM/test-udev-rules/nokia",     test_nokia);
    g_test_add_func ("/MM/test-udev-rules/zte",       test_zte);
    g_test_add_func ("/MM/test-udev-rules/longcheer", test_longcheer);
    g_test_add_func ("/MM/test-udev-rules/simtech",   test_simtech);
    g_test_add_func ("/MM/test-udev-rules/x22x",      test_x22x);
    g_test_add_func ("/MM/test-udev-rules/cinterion", test_cinterion);
    g_test_add_func ("/MM/test-udev-rules/dell",      test_dell);
    g_test_add_func ("/MM/test-udev-rules/telit",     test_telit);
    g_test_add_func ("/MM/test-udev-rules/mtk",       test_mtk);
    g_test_add_func ("/MM/test-udev-rules/haier",     test_haier);
    g_test_add_func ("/MM/test-udev-rules/fibocom",   test_fibocom);

    return g_test_run ();
}
