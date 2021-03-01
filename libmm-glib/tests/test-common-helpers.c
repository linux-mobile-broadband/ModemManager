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

#include <string.h>
#include <glib-object.h>

#include <libmm-glib.h>

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

/********************* BAND ARRAY TESTS *********************/

static void
common_band_array_cmp_test (gboolean equal,
                            const MMModemBand *bands_a,
                            guint n_bands_a,
                            const MMModemBand *bands_b,
                            guint n_bands_b)
{
    GArray *a;
    GArray *b;

    a = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), n_bands_a);
    g_array_append_vals (a, bands_a, n_bands_a);

    b = g_array_sized_new (FALSE, FALSE, sizeof (MMModemBand), n_bands_b);
    g_array_append_vals (b, bands_b, n_bands_b);

    g_assert_cmpuint (equal, ==, mm_common_bands_garray_cmp (a, b));
    g_assert_cmpuint (equal, ==, mm_common_bands_garray_cmp (b, a));

    g_array_unref (a);
    g_array_unref (b);
}

static void
band_array_cmp_test_equal_empty (void)
{
    const MMModemBand a[] = { };
    const MMModemBand b[] = { };

    common_band_array_cmp_test (TRUE, a, G_N_ELEMENTS (a), b, G_N_ELEMENTS (b));
}

static void
band_array_cmp_test_equal_one (void)
{
    const MMModemBand a[] = { MM_MODEM_BAND_EGSM };
    const MMModemBand b[] = { MM_MODEM_BAND_EGSM };

    common_band_array_cmp_test (TRUE, a, G_N_ELEMENTS (a), b, G_N_ELEMENTS (b));
}

static void
band_array_cmp_test_equal_multiple_same_order (void)
{
    const MMModemBand a[] = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS };
    const MMModemBand b[] = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS };

    common_band_array_cmp_test (TRUE, a, G_N_ELEMENTS (a), b, G_N_ELEMENTS (b));
}

static void
band_array_cmp_test_equal_multiple_different_order (void)
{
    const MMModemBand a[] = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS, MM_MODEM_BAND_PCS  };
    const MMModemBand b[] = { MM_MODEM_BAND_DCS,  MM_MODEM_BAND_PCS, MM_MODEM_BAND_EGSM };

    common_band_array_cmp_test (TRUE, a, G_N_ELEMENTS (a), b, G_N_ELEMENTS (b));
}

static void
band_array_cmp_test_different_one (void)
{
    const MMModemBand a[] = { MM_MODEM_BAND_EGSM };
    const MMModemBand b[] = { MM_MODEM_BAND_DCS  };

    common_band_array_cmp_test (FALSE, a, G_N_ELEMENTS (a), b, G_N_ELEMENTS (b));
}

static void
band_array_cmp_test_different_none (void)
{
    const MMModemBand a[] = { };
    const MMModemBand b[] = { MM_MODEM_BAND_EGSM };

    common_band_array_cmp_test (FALSE, a, G_N_ELEMENTS (a), b, G_N_ELEMENTS (b));
}

static void
band_array_cmp_test_different_multiple_1 (void)
{
    const MMModemBand a[] = { MM_MODEM_BAND_EGSM };
    const MMModemBand b[] = { MM_MODEM_BAND_EGSM, MM_MODEM_BAND_DCS };

    common_band_array_cmp_test (FALSE, a, G_N_ELEMENTS (a), b, G_N_ELEMENTS (b));
}

static void
band_array_cmp_test_different_multiple_2 (void)
{
    const MMModemBand a[] = { MM_MODEM_BAND_EGSM };
    const MMModemBand b[] = { MM_MODEM_BAND_DCS, MM_MODEM_BAND_EGSM };

    common_band_array_cmp_test (FALSE, a, G_N_ELEMENTS (a), b, G_N_ELEMENTS (b));
}

/********************* FIELD PARSERS TESTS *********************/

static void
field_parser_int (void)
{
    gint num;
    gchar *str;

    /* Failures */

    g_assert (mm_get_int_from_str (NULL, &num) == FALSE);

    g_assert (mm_get_int_from_str ("", &num) == FALSE);

    g_assert (mm_get_int_from_str ("a", &num) == FALSE);

    g_assert (mm_get_int_from_str ("a100", &num) == FALSE);

    g_assert (mm_get_int_from_str ("100a", &num) == FALSE);

    g_assert (mm_get_int_from_str ("\r\n", &num) == FALSE);

    str = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64)G_MAXINT + 1);
    g_assert (mm_get_int_from_str (str, &num) == FALSE);
    g_free (str);

    str = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64)(G_MININT) - 1);
    g_assert (mm_get_int_from_str (str, &num) == FALSE);
    g_free (str);

    /* Successes */

    g_assert (mm_get_int_from_str ("0", &num) == TRUE);
    g_assert_cmpint (num, ==, 0);

    g_assert (mm_get_int_from_str ("-100", &num) == TRUE);
    g_assert_cmpint (num, ==, -100);

    g_assert (mm_get_int_from_str ("100", &num) == TRUE);
    g_assert_cmpint (num, ==, 100);

    g_assert (mm_get_int_from_str ("-256\r\n", &num) == TRUE);
    g_assert_cmpint (num, ==, -256);

    str = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64)G_MAXINT);
    g_assert (mm_get_int_from_str (str, &num) == TRUE);
    g_assert_cmpint (num, ==, G_MAXINT);
    g_free (str);

    str = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64)G_MININT);
    g_assert (mm_get_int_from_str (str, &num) == TRUE);
    g_assert_cmpint (num, ==, G_MININT);
    g_free (str);
}

static void
field_parser_uint (void)
{
    gchar *str;
    guint num;

    /* Failures */

    g_assert (mm_get_uint_from_str (NULL, &num) == FALSE);

    g_assert (mm_get_uint_from_str ("", &num) == FALSE);

    g_assert (mm_get_uint_from_str ("a", &num) == FALSE);

    g_assert (mm_get_uint_from_str ("a100", &num) == FALSE);

    g_assert (mm_get_uint_from_str ("100a", &num) == FALSE);

    g_assert (mm_get_uint_from_str ("-100", &num) == FALSE);

    g_assert (mm_get_uint_from_str ("\r\n", &num) == FALSE);

    str = g_strdup_printf ("%" G_GUINT64_FORMAT, (guint64)(G_MAXUINT) + 1);
    g_assert (mm_get_uint_from_str (str, &num) == FALSE);
    g_free (str);

    /* Successes */

    g_assert (mm_get_uint_from_str ("0", &num) == TRUE);
    g_assert_cmpuint (num, ==, 0);

    g_assert (mm_get_uint_from_str ("100", &num) == TRUE);
    g_assert_cmpuint (num, ==, 100);

    g_assert (mm_get_uint_from_str ("256\r\n", &num) == TRUE);
    g_assert_cmpuint (num, ==, 256);

    str = g_strdup_printf ("%" G_GUINT64_FORMAT, (guint64)G_MAXUINT);
    g_assert (mm_get_uint_from_str (str, &num) == TRUE);
    g_assert_cmpuint (num, ==, G_MAXUINT);
    g_free (str);
}

static void
field_parser_double (void)
{
    gchar *str;
    gdouble num;

    /* Failures */

    g_assert (mm_get_double_from_str (NULL, &num) == FALSE);

    g_assert (mm_get_double_from_str ("", &num) == FALSE);

    g_assert (mm_get_double_from_str ("a", &num) == FALSE);

    g_assert (mm_get_double_from_str ("a100", &num) == FALSE);

    g_assert (mm_get_double_from_str ("100a", &num) == FALSE);

    g_assert (mm_get_double_from_str ("\r\n", &num) == FALSE);

    /* Successes */

    g_assert (mm_get_double_from_str ("-100", &num) == TRUE);
    g_assert (num - (-100.0) < 0000000.1);

    g_assert (mm_get_double_from_str ("-100.7567", &num) == TRUE);
    g_assert (num - (-100.7567) < 0000000.1);

    g_assert (mm_get_double_from_str ("0", &num) == TRUE);
    g_assert (num < 0000000.1);

    g_assert (mm_get_double_from_str ("-0.0", &num) == TRUE);
    g_assert (num < 0000000.1);

    g_assert (mm_get_double_from_str ("0.0", &num) == TRUE);
    g_assert (num < 0000000.1);

    g_assert (mm_get_double_from_str ("100", &num) == TRUE);
    g_assert (num - (100.0) < 0000000.1);

    g_assert (mm_get_double_from_str ("100.7567", &num) == TRUE);
    g_assert (num - (100.7567) < 0000000.1);

    g_assert (mm_get_double_from_str ("100.7567\r\n", &num) == TRUE);
    g_assert (num - (100.7567) < 0000000.1);

    str = g_strdup_printf ("%lf", (gdouble)G_MINDOUBLE);
    g_assert (mm_get_double_from_str (str, &num) == TRUE);
    g_assert (num - G_MINDOUBLE < 0000000.1);
    g_free (str);

    str = g_strdup_printf ("%lf", (gdouble)G_MAXDOUBLE);
    g_assert (mm_get_double_from_str (str, &num) == TRUE);
    g_assert (num - G_MAXDOUBLE < 0000000.1);
    g_free (str);
}

/**************************************************************/
/* hexstr2bin & bin2hexstr */

static void
common_hexstr2bin_test_failure (const gchar *input_hex)
{
    g_autoptr(GError)  error = NULL;
    g_autofree guint8 *bin = NULL;
    gsize              bin_len = 0;

    g_assert (mm_utils_ishexstr (input_hex) == FALSE);

    bin = mm_utils_hexstr2bin (input_hex, -1, &bin_len, &error);
    g_assert_null (bin);
    g_assert_nonnull (error);
}

static void
common_hexstr2bin_test_success_len (const gchar *input_hex,
                                    gssize       input_hex_len)
{
    g_autoptr(GError)  error = NULL;
    g_autofree guint8 *bin = NULL;
    gsize              bin_len = 0;
    g_autofree gchar  *hex = NULL;

    bin = mm_utils_hexstr2bin (input_hex, input_hex_len, &bin_len, &error);
    g_assert_nonnull (bin);
    g_assert_no_error (error);

    hex = mm_utils_bin2hexstr (bin, bin_len);
    g_assert_nonnull (hex);

    if (input_hex_len == -1)
        g_assert (g_ascii_strcasecmp (input_hex, hex) == 0);
    else
        g_assert (g_ascii_strncasecmp (input_hex, hex, (gsize)input_hex_len) == 0);
}

static void
common_hexstr2bin_test_success (const gchar *input_hex)
{
    gsize  input_hex_len;
    gssize i;

    g_assert (mm_utils_ishexstr (input_hex) == TRUE);

    common_hexstr2bin_test_success_len (input_hex, -1);

    input_hex_len = strlen (input_hex);
    for (i = input_hex_len; i >= 2; i-=2)
        common_hexstr2bin_test_success_len (input_hex, i);
}

static void
hexstr_lower_case (void)
{
    common_hexstr2bin_test_success ("000123456789abcdefff");
}

static void
hexstr_upper_case (void)
{
    common_hexstr2bin_test_success ("000123456789ABCDEFFF");
}

static void
hexstr_mixed_case (void)
{
    common_hexstr2bin_test_success ("000123456789AbcDefFf");
}

static void
hexstr_empty (void)
{
    common_hexstr2bin_test_failure ("");
}

static void
hexstr_missing_digits (void)
{
    common_hexstr2bin_test_failure ("012");
}

static void
hexstr_wrong_digits_all (void)
{
    common_hexstr2bin_test_failure ("helloworld");
}

static void
hexstr_wrong_digits_some (void)
{
    common_hexstr2bin_test_failure ("012345k7");
}

/**************************************************************/

int main (int argc, char **argv)
{
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

    g_test_add_func ("/MM/Common/BandArray/Cmp/equal-empty", band_array_cmp_test_equal_empty);
    g_test_add_func ("/MM/Common/BandArray/Cmp/equal-one", band_array_cmp_test_equal_one);
    g_test_add_func ("/MM/Common/BandArray/Cmp/equal-multiple-same-order", band_array_cmp_test_equal_multiple_same_order);
    g_test_add_func ("/MM/Common/BandArray/Cmp/equal-multiple-different-order", band_array_cmp_test_equal_multiple_different_order);
    g_test_add_func ("/MM/Common/BandArray/Cmp/different-one", band_array_cmp_test_different_one);
    g_test_add_func ("/MM/Common/BandArray/Cmp/different-none", band_array_cmp_test_different_none);
    g_test_add_func ("/MM/Common/BandArray/Cmp/different-multiple-1", band_array_cmp_test_different_multiple_1);
    g_test_add_func ("/MM/Common/BandArray/Cmp/different-multiple-2", band_array_cmp_test_different_multiple_2);

    g_test_add_func ("/MM/Common/FieldParsers/Int", field_parser_int);
    g_test_add_func ("/MM/Common/FieldParsers/Uint", field_parser_uint);
    g_test_add_func ("/MM/Common/FieldParsers/Double", field_parser_double);

    g_test_add_func ("/MM/Common/HexStr/lower-case",        hexstr_lower_case);
    g_test_add_func ("/MM/Common/HexStr/upper-case",        hexstr_upper_case);
    g_test_add_func ("/MM/Common/HexStr/mixed-case",        hexstr_mixed_case);
    g_test_add_func ("/MM/Common/HexStr/missing-empty",     hexstr_empty);
    g_test_add_func ("/MM/Common/HexStr/missing-digits",    hexstr_missing_digits);
    g_test_add_func ("/MM/Common/HexStr/wrong-digits-all",  hexstr_wrong_digits_all);
    g_test_add_func ("/MM/Common/HexStr/wrong-digits-some", hexstr_wrong_digits_some);

    return g_test_run ();
}
