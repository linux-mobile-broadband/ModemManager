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

static void
test_rule_from_line_valid (void)
{
    MMUdevRule       rule = { 0 };
    MMUdevRuleMatch *match;
    GError          *error = NULL;
    gboolean         ret;

    ret = mm_kernel_device_generic_rule_load_from_line (
              &rule,
              "SUBSYSTEM==\"tty\", ATTRS{vendor}==\"0x1234\", ACTION!=\"remove\", ENV{ID_MM_PORT_IGNORE}=\"1\"",
              &error);
    g_assert_no_error (error);
    g_assert_true (ret);
    g_assert_nonnull (rule.conditions);
    g_assert_cmpuint (rule.conditions->len, ==, 3);

    match = &g_array_index (rule.conditions, MMUdevRuleMatch, 0);
    g_assert_cmpint (match->type, ==, MM_UDEV_RULE_MATCH_TYPE_EQUAL);
    g_assert_cmpstr (match->parameter, ==, "SUBSYSTEM");
    g_assert_cmpstr (match->value, ==, "tty");

    match = &g_array_index (rule.conditions, MMUdevRuleMatch, 1);
    g_assert_cmpint (match->type, ==, MM_UDEV_RULE_MATCH_TYPE_EQUAL);
    g_assert_cmpstr (match->parameter, ==, "ATTRS{vendor}");
    g_assert_cmpstr (match->value, ==, "0x1234");

    match = &g_array_index (rule.conditions, MMUdevRuleMatch, 2);
    g_assert_cmpint (match->type, ==, MM_UDEV_RULE_MATCH_TYPE_NOT_EQUAL);
    g_assert_cmpstr (match->parameter, ==, "ACTION");
    g_assert_cmpstr (match->value, ==, "remove");

    g_assert_cmpint (rule.result.type, ==, MM_UDEV_RULE_RESULT_TYPE_PROPERTY);
    g_assert_cmpstr (rule.result.content.property.name, ==, "ID_MM_PORT_IGNORE");
    g_assert_cmpstr (rule.result.content.property.value, ==, "1");

    mm_kernel_device_generic_rule_clear (&rule);
}

static void
test_rule_from_line_invalid (void)
{
    static const gchar *invalid_lines[] = {
        /* Missing comma between conditions */
        "SUBSYSTEM==\"tty\" ATTRS{vendor}==\"0x1234\", ATTRS{device}==\"0x5678\", ENV{ID_MM_PORT_IGNORE}=\"1\"",
        /* Missing comma between condition and result */
        "SUBSYSTEM==\"tty\", ATTRS{vendor}==\"0x1234\" ENV{ID_MM_PORT_IGNORE}=\"1\"",
        /* Missing comma between '=' and '==' items */
        "ENV{.MM_USBIFNUM}=\"00\" SUBSYSTEM==\"tty\", ENV{ID_MM_PORT_IGNORE}=\"1\"",
        /* Unquoted right field */
        "SUBSYSTEM==tty, ENV{ID_MM_PORT_IGNORE}=\"1\"",
        "SUBSYSTEM==\"tty\", ENV{ID_MM_PORT_IGNORE}=1",
        /* Unterminated or mismatched quotes in right field */
        "SUBSYSTEM==\"tty, ENV{ID_MM_PORT_IGNORE}=\"1\"",
        "SUBSYSTEM==tty\", ENV{ID_MM_PORT_IGNORE}=\"1\"",
        "SUBSYSTEM==\"\", ENV{ID_MM_PORT_IGNORE}=\"1\"",
        /* Missing or invalid left field */
        "==\"tty\", ENV{ID_MM_PORT_IGNORE}=\"1\"",
        "SUBSYSTEM DRIVERS==\"tty\", ENV{ID_MM_PORT_IGNORE}=\"1\"",
        "SUBSYSTEM!==\"tty\", ENV{ID_MM_PORT_IGNORE}=\"1\"",
        /* Missing operator */
        "SUBSYSTEM, ENV{ID_MM_PORT_IGNORE}=\"1\"",
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (invalid_lines); i++) {
        MMUdevRule      rule = { 0 };
        g_autoptr(GError) error = NULL;
        gboolean        ret;

        ret = mm_kernel_device_generic_rule_load_from_line (&rule, invalid_lines[i], &error);
        g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED);
        g_assert_false (ret);
    }
}

/************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/test-udev-rules/load-cleanup-core", test_load_cleanup_core);
    g_test_add_func ("/MM/test-udev-rules/rule-from-line/valid", test_rule_from_line_valid);
    g_test_add_func ("/MM/test-udev-rules/rule-from-line/invalid", test_rule_from_line_invalid);

    return g_test_run ();
}
