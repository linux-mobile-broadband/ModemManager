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
 * Copyright (C) 2012 Google, Inc.
 */

#include <glib.h>

#include <libmm-common.h>

/********************* KEY VALUE PARSER TESTS *********************/

typedef struct {
    const gchar *key;
    const gchar *value;
} KeyValueEntry;

/* ---- Expected cases ---- */

typedef struct {
    const KeyValueEntry *entries;
    guint n_entries;
    guint i;
} CommonKeyValueTestContext;

static gboolean
common_key_value_test_foreach (const gchar *key,
                               const gchar *value,
                               CommonKeyValueTestContext *ctx)
{
    g_assert_cmpuint (ctx->i, <, ctx->n_entries);

    g_assert_cmpstr (key, ==, ctx->entries[ctx->i].key);
    g_assert_cmpstr (value, ==, ctx->entries[ctx->i].value);
    ctx->i++;

    return TRUE;
}

static void
common_key_value_test (const gchar *str,
                       const KeyValueEntry *entries,
                       guint n_entries)
{
    GError *error = NULL;
    CommonKeyValueTestContext ctx;

    ctx.entries = entries;
    ctx.n_entries = n_entries;
    ctx.i = 0;

    mm_common_parse_key_value_string (str,
                                      &error,
                                      (MMParseKeyValueForeachFn)common_key_value_test_foreach,
                                      &ctx);
    g_assert_no_error (error);
    g_assert_cmpuint (ctx.i, ==, ctx.n_entries);
}

static void
key_value_test_standard (void)
{
    const gchar *str =
        "key1=value1,"
        "key2=value2,"
        "key3=value3";
    const KeyValueEntry entries[] = {
        { "key1", "value1" },
        { "key2", "value2" },
        { "key3", "value3" }
    };

    common_key_value_test (str, entries, G_N_ELEMENTS (entries));
}

static void
key_value_test_spaces (void)
{
    const gchar *str =
        "  key1 =    value1    ,    "
        "\t\tkey2\t=\tvalue2\t,\t"
        "\n\nkey3\n=\nvalue3\n";
    const KeyValueEntry entries[] = {
        { "key1", "value1" },
        { "key2", "value2" },
        { "key3", "value3" }
    };

    common_key_value_test (str, entries, G_N_ELEMENTS (entries));
}

static void
key_value_test_double_quotes (void)
{
    const gchar *str =
        "key1=\"this is a string\","
        "key2=\"and so is this\"";
    const KeyValueEntry entries[] = {
        { "key1", "this is a string" },
        { "key2", "and so is this" }
    };

    common_key_value_test (str, entries, G_N_ELEMENTS (entries));
}

static void
key_value_test_single_quotes (void)
{
    const gchar *str =
        "key1='this is a string',"
        "key2='and so is this'";
    const KeyValueEntry entries[] = {
        { "key1", "this is a string" },
        { "key2", "and so is this" }
    };

    common_key_value_test (str, entries, G_N_ELEMENTS (entries));
}

static void
key_value_test_empty_value (void)
{
    const gchar *str =
        "key1=,"
        "key2=\"\"";
    const KeyValueEntry entries[] = {
        { "key1", "" },
        { "key2", "" }
    };

    common_key_value_test (str, entries, G_N_ELEMENTS (entries));
}

static void
key_value_test_empty_string (void)
{
    const gchar *str = "";
    const KeyValueEntry entries[] = { };

    common_key_value_test (str, entries, G_N_ELEMENTS (entries));
}

/* ---- Unexpected cases ---- */

static gboolean
common_key_value_error_test_foreach (const gchar *key,
                                     const gchar *value,
                                     gpointer none)
{
    /* no op */
    return TRUE;
}

static void
common_key_value_error_test (const gchar *str)
{
    GError *error = NULL;

    mm_common_parse_key_value_string (str,
                                      &error,
                                      (MMParseKeyValueForeachFn)common_key_value_error_test_foreach,
                                      NULL);

    /* We don't really care about the specific error type */
    g_assert (error != NULL);
    g_error_free (error);
}

static void
key_value_error_test_no_first_key (void)
{
    common_key_value_error_test ("=value1");
}

static void
key_value_error_test_no_key (void)
{
    common_key_value_error_test ("key1=value1,"
                                 "=value2");
}

static void
key_value_error_test_missing_double_quotes_0 (void)
{
    common_key_value_error_test ("key1=\"value1");
}

static void
key_value_error_test_missing_double_quotes_1 (void)
{
    common_key_value_error_test ("key1=\"value1,"
                                 "key2=\"value2\"");
}

static void
key_value_error_test_missing_double_quotes_2 (void)
{
    common_key_value_error_test ("key1=\"value1\","
                                 "key2=\"value2");
}

static void
key_value_error_test_missing_single_quotes_0 (void)
{
    common_key_value_error_test ("key1='value1");
}

static void
key_value_error_test_missing_single_quotes_1 (void)
{
    common_key_value_error_test ("key1='value1,"
                                 "key2='value2'");
}

static void
key_value_error_test_missing_single_quotes_2 (void)
{
    common_key_value_error_test ("key1='value1',"
                                 "key2='value2");
}

static void
key_value_error_test_missing_comma_0 (void)
{
    common_key_value_error_test ("key1=value1 "
                                 "key2=value2");
}

static void
key_value_error_test_missing_comma_1 (void)
{
    common_key_value_error_test ("key1=\"value1\" "
                                 "key2=\"value2\"");
}

static void
key_value_error_test_missing_comma_2 (void)
{
    common_key_value_error_test ("key1='value1' "
                                 "key2='value2'");
}

int main (int argc, char **argv)
{
    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/Common/KeyValue/standard", key_value_test_standard);
    g_test_add_func ("/MM/Common/KeyValue/spaces", key_value_test_spaces);
    g_test_add_func ("/MM/Common/KeyValue/double-quotes", key_value_test_double_quotes);
    g_test_add_func ("/MM/Common/KeyValue/single-quotes", key_value_test_single_quotes);
    g_test_add_func ("/MM/Common/KeyValue/empty-value", key_value_test_empty_value);
    g_test_add_func ("/MM/Common/KeyValue/empty-string", key_value_test_empty_string);

    g_test_add_func ("/MM/Common/KeyValue/Error/no-first-key", key_value_error_test_no_first_key);
    g_test_add_func ("/MM/Common/KeyValue/Error/no-key", key_value_error_test_no_key);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-double-quotes-0", key_value_error_test_missing_double_quotes_0);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-double-quotes-1", key_value_error_test_missing_double_quotes_1);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-double-quotes-2", key_value_error_test_missing_double_quotes_2);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-single-quotes-0", key_value_error_test_missing_single_quotes_0);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-single-quotes-1", key_value_error_test_missing_single_quotes_1);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-single-quotes-2", key_value_error_test_missing_single_quotes_2);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-comma-0", key_value_error_test_missing_comma_0);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-comma-1", key_value_error_test_missing_comma_1);
    g_test_add_func ("/MM/Common/KeyValue/Error/missing-comma-2", key_value_error_test_missing_comma_2);

    return g_test_run ();
}
