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
#include "mm-log-test.h"

/************************************************************/

static void
test_load_cleanup_core (void)
{
    GArray *rules;
    GError *error = NULL;

    rules = mm_kernel_device_generic_rules_load (TESTUDEVRULESDIR, &error);
    g_assert_no_error (error);
    g_assert (rules);
    g_assert (rules->len > 0);

    g_array_unref (rules);
}

/************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/test-udev-rules/load-cleanup-core", test_load_cleanup_core);

    return g_test_run ();
}
