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

static void
date_time_iso8601 (void)
{
    gchar *date = NULL;
    GError *error = NULL;

    date = mm_new_iso8601_time_from_unix_time (1634307342, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (date, ==, "2021-10-15T14:15:42Z");
    g_free (date);

    date = mm_new_iso8601_time (2021, 10, 15, 16, 15, 42, FALSE, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (date, ==, "2021-10-15T16:15:42Z");
    g_free (date);

    date = mm_new_iso8601_time (2021, 10, 15, 16, 15, 42, TRUE, 120, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (date, ==, "2021-10-15T16:15:42+02");
    g_free (date);

    /* Valid args:
     * - Year:[1-9999]
     * - Month:[1-12]
     * - Day:[1-28|29|30|31] according to year and month
     * - Hour: [0-23]
     * - Minute: [0-59]
     * - Seconds: [0.0-60.0)
     * */
    date = mm_new_iso8601_time (2021, 13, 15, 16, 15, 42, TRUE, 120, &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert_null (date);
    g_clear_error (&error);

    /* No February 29 in 2021 */
    date = mm_new_iso8601_time (2021, 2, 29, 16, 15, 42, TRUE, 120, &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert_null (date);
    g_clear_error (&error);

    /* Too far into the future */
    date = mm_new_iso8601_time_from_unix_time (G_MAXINT64, &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert_null (date);
    g_clear_error (&error);
}

/**************************************************************/
/* string helpers */

static void
bands_to_string (void)
{
    gchar *bands_str = NULL;
    MMModemBand bands[2] = {MM_MODEM_BAND_G480, MM_MODEM_BAND_CDMA_BC9};

    bands_str = mm_common_build_bands_string (NULL, 0);
    g_assert_cmpstr (bands_str, ==, "none");
    g_clear_pointer (&bands_str, g_free);

    bands_str = mm_common_build_bands_string (bands, 1);
    g_assert_cmpstr (bands_str, ==, "g480");
    g_clear_pointer (&bands_str, g_free);

    bands_str = mm_common_build_bands_string (bands, 2);
    g_assert_cmpstr (bands_str, ==, "g480, cdma-bc9");
    g_clear_pointer (&bands_str, g_free);
}

static void
capabilities_to_string (void)
{
    gchar *capabilities_str = NULL;
    MMModemCapability capabilities[2] = {MM_MODEM_CAPABILITY_CDMA_EVDO, MM_MODEM_CAPABILITY_TDS};

    capabilities_str = mm_common_build_capabilities_string (NULL, 0);
    g_assert_cmpstr (capabilities_str, ==, "none");
    g_clear_pointer (&capabilities_str, g_free);

    capabilities_str = mm_common_build_capabilities_string (capabilities, 1);
    g_assert_cmpstr (capabilities_str, ==, "cdma-evdo");
    g_clear_pointer (&capabilities_str, g_free);

    capabilities_str = mm_common_build_capabilities_string (capabilities, 2);
    g_assert_cmpstr (capabilities_str, ==, "cdma-evdo\ntds");
    g_clear_pointer (&capabilities_str, g_free);
}

static void
mode_combinations_to_string (void)
{
    gchar *mode_combinations_str = NULL;
    MMModemModeCombination mode_combinations[2] = {
        {
            .allowed = MM_MODEM_MODE_2G | MM_MODEM_MODE_3G,
            .preferred = MM_MODEM_MODE_3G
        },
        {
            .allowed = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G,
            .preferred = MM_MODEM_MODE_5G
        }
    };

    mode_combinations_str = mm_common_build_mode_combinations_string (NULL, 0);
    g_assert_cmpstr (mode_combinations_str, ==, "none");
    g_clear_pointer (&mode_combinations_str, g_free);

    mode_combinations_str = mm_common_build_mode_combinations_string (mode_combinations, 1);
    g_assert_cmpstr (mode_combinations_str, ==, "allowed: 2g, 3g; preferred: 3g");
    g_clear_pointer (&mode_combinations_str, g_free);

    mode_combinations_str = mm_common_build_mode_combinations_string (mode_combinations, 2);
    g_assert_cmpstr (mode_combinations_str, ==, "allowed: 2g, 3g; preferred: 3g\nallowed: 4g, 5g; preferred: 5g");
    g_clear_pointer (&mode_combinations_str, g_free);
}

static void
ports_to_string (void)
{
    gchar *ports_str = NULL;
    MMModemPortInfo ports[2] = {
        {
          .name = (gchar*)"port1",
          .type = MM_MODEM_PORT_TYPE_AT
        },
        {
          .name = (gchar*)"port2",
          .type = MM_MODEM_PORT_TYPE_QMI
        }
    };

    ports_str = mm_common_build_ports_string (NULL, 0);
    g_assert_cmpstr (ports_str, ==, "none");
    g_clear_pointer (&ports_str, g_free);

    ports_str = mm_common_build_ports_string (ports, 1);
    g_assert_cmpstr (ports_str, ==, "port1 (at)");
    g_clear_pointer (&ports_str, g_free);

    ports_str = mm_common_build_ports_string (ports, 2);
    g_assert_cmpstr (ports_str, ==, "port1 (at), port2 (qmi)");
    g_clear_pointer (&ports_str, g_free);
}

static void
sms_storages_to_string (void)
{
    gchar *sms_storages_str = NULL;
    MMSmsStorage sms_storages[2] = {MM_SMS_STORAGE_MT, MM_SMS_STORAGE_BM};

    sms_storages_str = mm_common_build_sms_storages_string (NULL, 0);
    g_assert_cmpstr (sms_storages_str, ==, "none");
    g_clear_pointer (&sms_storages_str, g_free);

    sms_storages_str = mm_common_build_sms_storages_string (sms_storages, 1);
    g_assert_cmpstr (sms_storages_str, ==, "mt");
    g_clear_pointer (&sms_storages_str, g_free);

    sms_storages_str = mm_common_build_sms_storages_string (sms_storages, 2);
    g_assert_cmpstr (sms_storages_str, ==, "mt, bm");
    g_clear_pointer (&sms_storages_str, g_free);
}

static void
capabilities_from_string (void)
{
    MMModemCapability capability = MM_MODEM_CAPABILITY_ANY;
    GError *error = NULL;

    capability = mm_common_get_capabilities_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (capability == MM_MODEM_CAPABILITY_NONE);
    g_clear_error (&error);

    capability = mm_common_get_capabilities_from_string ("gsm-umts", &error);
    g_assert_no_error (error);
    g_assert (capability == MM_MODEM_CAPABILITY_GSM_UMTS);

    capability = mm_common_get_capabilities_from_string ("gsm-umts|capa-unknown", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (capability == MM_MODEM_CAPABILITY_NONE);
    g_clear_error (&error);

    capability = mm_common_get_capabilities_from_string ("gsm-umts|lte", &error);
    g_assert_no_error (error);
    g_assert (capability == (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE));
}

static void
modes_from_string (void)
{
    MMModemMode mode = MM_MODEM_MODE_ANY;
    GError *error = NULL;

    mode = mm_common_get_modes_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (mode == MM_MODEM_MODE_NONE);
    g_clear_error (&error);

    mode = mm_common_get_modes_from_string ("3g", &error);
    g_assert_no_error (error);
    g_assert (mode == MM_MODEM_MODE_3G);

    mode = mm_common_get_modes_from_string ("3g|mode-unknown", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (mode == MM_MODEM_MODE_NONE);
    g_clear_error (&error);

    mode = mm_common_get_modes_from_string ("3g|4g", &error);
    g_assert_no_error (error);
    g_assert (mode == (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G));
}

static void
bands_from_string (void)
{
    MMModemBand *bands = NULL;
    guint n_bands = 0;
    gboolean ret = FALSE;
    GError *error = NULL;

    ret = mm_common_get_bands_from_string ("not found", &bands, &n_bands, &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert_false (ret);
    g_assert_cmpuint (n_bands, ==, 0);
    g_assert_null (bands);
    g_clear_error (&error);

    ret = mm_common_get_bands_from_string ("eutran-9", &bands, &n_bands, &error);
    g_assert_no_error (error);
    g_assert_true (ret);
    g_assert_cmpuint (n_bands, ==, 1);
    g_assert (bands[0] == MM_MODEM_BAND_EUTRAN_9);
    g_clear_pointer(&bands, g_free);

    ret = mm_common_get_bands_from_string ("eutran-9|band-unknown", &bands, &n_bands, &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert_false (ret);
    g_assert_cmpuint (n_bands, ==, 0);
    g_assert_null (bands);
    g_clear_error (&error);

    ret = mm_common_get_bands_from_string ("eutran-9|cdma-bc7", &bands, &n_bands, &error);
    g_assert_no_error (error);
    g_assert_true (ret);
    g_assert_cmpuint (n_bands, ==, 2);
    g_assert (bands[0] == MM_MODEM_BAND_EUTRAN_9);
    g_assert (bands[1] == MM_MODEM_BAND_CDMA_BC7);
    g_clear_pointer(&bands, g_free);
}

static void
boolean_from_string (void)
{
    gboolean ret = FALSE;
    GError *error = NULL;

    ret = mm_common_get_boolean_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert_false (ret);
    g_clear_error (&error);

    ret = mm_common_get_boolean_from_string ("true", &error);
    g_assert_no_error (error);
    g_assert_true (ret);

    ret = mm_common_get_boolean_from_string ("1", &error);
    g_assert_no_error (error);
    g_assert_true (ret);

    ret = mm_common_get_boolean_from_string ("yes", &error);
    g_assert_no_error (error);
    g_assert_true (ret);

    ret = mm_common_get_boolean_from_string ("false", &error);
    g_assert_no_error (error);
    g_assert_false (ret);

    ret = mm_common_get_boolean_from_string ("0", &error);
    g_assert_no_error (error);
    g_assert_false (ret);

    ret = mm_common_get_boolean_from_string ("no", &error);
    g_assert_no_error (error);
    g_assert_false (ret);
}

static void
rm_protocol_from_string (void)
{
    MMModemCdmaRmProtocol rm_protocol = MM_MODEM_CDMA_RM_PROTOCOL_STU_III;
    GError *error = NULL;

    rm_protocol = mm_common_get_rm_protocol_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (rm_protocol == MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN);
    g_clear_error (&error);

    rm_protocol = mm_common_get_rm_protocol_from_string ("packet-network-ppp", &error);
    g_assert_no_error (error);
    g_assert (rm_protocol == MM_MODEM_CDMA_RM_PROTOCOL_PACKET_NETWORK_PPP);
}

static void
ip_type_from_string (void)
{
    MMBearerIpFamily ip_type = MM_BEARER_IP_FAMILY_ANY;
    GError *error = NULL;

    ip_type = mm_common_get_ip_type_from_string("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (ip_type == MM_BEARER_IP_FAMILY_NONE);
    g_clear_error (&error);

    ip_type = mm_common_get_ip_type_from_string ("ipv4v6", &error);
    g_assert_no_error (error);
    g_assert (ip_type == MM_BEARER_IP_FAMILY_IPV4V6);

    ip_type = mm_common_get_ip_type_from_string ("ipv4v6|type-unknown", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (ip_type == MM_BEARER_IP_FAMILY_NONE);
    g_clear_error (&error);

    ip_type = mm_common_get_ip_type_from_string ("ipv4|ipv6", &error);
    g_assert_no_error (error);
    g_assert (ip_type == (MM_BEARER_IP_FAMILY_IPV4 | MM_BEARER_IP_FAMILY_IPV6));
}

static void
allowed_auth_from_string (void)
{
    MMBearerAllowedAuth allowed_auth = MM_BEARER_ALLOWED_AUTH_EAP;
    GError *error = NULL;

    allowed_auth = mm_common_get_allowed_auth_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (allowed_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN);
    g_clear_error (&error);

    allowed_auth = mm_common_get_allowed_auth_from_string ("pap", &error);
    g_assert_no_error (error);
    g_assert (allowed_auth == MM_BEARER_ALLOWED_AUTH_PAP);

    allowed_auth = mm_common_get_allowed_auth_from_string ("pap|auth-unknown", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (allowed_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN);
    g_clear_error (&error);

    allowed_auth = mm_common_get_allowed_auth_from_string ("pap|chap", &error);
    g_assert_no_error (error);
    g_assert (allowed_auth == (MM_BEARER_ALLOWED_AUTH_PAP | MM_BEARER_ALLOWED_AUTH_CHAP));
}

static void
sms_storage_from_string (void)
{
    MMSmsStorage sms_storage = MM_SMS_STORAGE_TA;
    GError *error = NULL;

    sms_storage = mm_common_get_sms_storage_from_string("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (sms_storage == MM_SMS_STORAGE_UNKNOWN);
    g_clear_error (&error);

    sms_storage = mm_common_get_sms_storage_from_string ("bm", &error);
    g_assert_no_error (error);
    g_assert (sms_storage == MM_SMS_STORAGE_BM);
}

static void
sms_cdma_teleservice_id_from_string (void)
{
    MMSmsCdmaTeleserviceId sms_cdma_teleservice_id = MM_SMS_CDMA_TELESERVICE_ID_CATPT;
    GError *error = NULL;

    sms_cdma_teleservice_id = mm_common_get_sms_cdma_teleservice_id_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (sms_cdma_teleservice_id == MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN);
    g_clear_error (&error);

    sms_cdma_teleservice_id = mm_common_get_sms_cdma_teleservice_id_from_string ("wemt", &error);
    g_assert_no_error (error);
    g_assert (sms_cdma_teleservice_id == MM_SMS_CDMA_TELESERVICE_ID_WEMT);
}

static void
sms_cdma_service_category_from_string (void)
{
    MMSmsCdmaServiceCategory sms_cdma_service_category = MM_SMS_CDMA_SERVICE_CATEGORY_LODGINGS;
    GError *error = NULL;

    sms_cdma_service_category = mm_common_get_sms_cdma_service_category_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (sms_cdma_service_category == MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN);
    g_clear_error (&error);

    sms_cdma_service_category = mm_common_get_sms_cdma_service_category_from_string ("lodgings", &error);
    g_assert_no_error (error);
    g_assert (sms_cdma_service_category == MM_SMS_CDMA_SERVICE_CATEGORY_LODGINGS);
}

static void
call_direction_from_string (void)
{
    MMCallDirection call_direction = MM_CALL_DIRECTION_OUTGOING;
    GError *error = NULL;

    call_direction = mm_common_get_call_direction_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (call_direction == MM_CALL_DIRECTION_UNKNOWN);
    g_clear_error (&error);

    call_direction = mm_common_get_call_direction_from_string ("incoming", &error);
    g_assert_no_error (error);
    g_assert (call_direction == MM_CALL_DIRECTION_INCOMING); 
}

static void
call_state_from_string (void)
{
    MMCallState call_state = MM_CALL_STATE_RINGING_IN;
    GError *error = NULL;

    call_state = mm_common_get_call_state_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (call_state == MM_CALL_STATE_UNKNOWN);
    g_clear_error (&error);

    call_state = mm_common_get_call_state_from_string ("waiting", &error);
    g_assert_no_error (error);
    g_assert (call_state == MM_CALL_STATE_WAITING);
}

static void
call_state_reason_from_string (void)
{
    MMCallStateReason call_state_reason = MM_CALL_STATE_REASON_TERMINATED;
    GError *error = NULL;

    call_state_reason = mm_common_get_call_state_reason_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (call_state_reason == MM_CALL_STATE_REASON_UNKNOWN);
    g_clear_error (&error);

    call_state_reason = mm_common_get_call_state_reason_from_string ("refused-or-busy", &error);
    g_assert_no_error (error);
    g_assert (call_state_reason == MM_CALL_STATE_REASON_REFUSED_OR_BUSY);
}

static void
oma_features_from_string (void)
{
    MMOmaFeature oma_features = MM_OMA_FEATURE_HANDS_FREE_ACTIVATION;
    GError *error = NULL;

    oma_features = mm_common_get_oma_features_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (oma_features == MM_OMA_FEATURE_NONE);
    g_clear_error (&error);

    oma_features = mm_common_get_oma_features_from_string ("device-provisioning", &error);
    g_assert_no_error (error);
    g_assert (oma_features == MM_OMA_FEATURE_DEVICE_PROVISIONING);

    oma_features = mm_common_get_oma_features_from_string ("device-provisioning|oma-unknown", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (oma_features == MM_OMA_FEATURE_NONE);
    g_clear_error (&error);

    oma_features = mm_common_get_oma_features_from_string ("device-provisioning|prl-update", &error);
    g_assert_no_error (error);
    g_assert (oma_features == (MM_OMA_FEATURE_DEVICE_PROVISIONING | MM_OMA_FEATURE_PRL_UPDATE));
}

static void
oma_session_type_from_string (void)
{
    MMOmaSessionType oma_session_type = MM_OMA_SESSION_TYPE_NETWORK_INITIATED_DEVICE_CONFIGURE;
    GError *error = NULL;

    oma_session_type = mm_common_get_oma_session_type_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (oma_session_type == MM_OMA_SESSION_TYPE_UNKNOWN);
    g_clear_error (&error);

    oma_session_type = mm_common_get_oma_session_type_from_string ("device-initiated-prl-update", &error);
    g_assert_no_error (error);
    g_assert (oma_session_type == MM_OMA_SESSION_TYPE_DEVICE_INITIATED_PRL_UPDATE);
}

static void
eps_ue_mode_operation_from_string (void)
{
    MMModem3gppEpsUeModeOperation eps_ue_mode_opearation = MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_CSPS_1;
    GError *error = NULL;

    eps_ue_mode_opearation = mm_common_get_eps_ue_mode_operation_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (eps_ue_mode_opearation == MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_UNKNOWN);
    g_clear_error (&error);

    eps_ue_mode_opearation = mm_common_get_eps_ue_mode_operation_from_string ("ps-2", &error);
    g_assert_no_error (error);
    g_assert (eps_ue_mode_opearation == MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_PS_2);
}

static void
access_technology_from_string (void)
{
    MMModemAccessTechnology access_technology = MM_MODEM_ACCESS_TECHNOLOGY_ANY;
    GError *error = NULL;

    access_technology = mm_common_get_access_technology_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (access_technology == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
    g_clear_error (&error);

    access_technology = mm_common_get_access_technology_from_string ("hsdpa", &error);
    g_assert_no_error (error);
    g_assert (access_technology == MM_MODEM_ACCESS_TECHNOLOGY_HSDPA);

    access_technology = mm_common_get_access_technology_from_string ("hsdpa|access-unknown", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (access_technology == MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
    g_clear_error (&error);

    access_technology = mm_common_get_access_technology_from_string ("hsdpa|hspa-plus", &error);
    g_assert_no_error (error);
    g_assert (access_technology == (MM_MODEM_ACCESS_TECHNOLOGY_HSDPA | MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS));
}

static void
multiplex_support_from_string (void)
{
    MMBearerMultiplexSupport multiplex_support = MM_BEARER_MULTIPLEX_SUPPORT_REQUIRED;
    GError *error = NULL;

    multiplex_support = mm_common_get_multiplex_support_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (multiplex_support == MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN);
    g_clear_error (&error);

    multiplex_support = mm_common_get_multiplex_support_from_string ("requested", &error);
    g_assert_no_error (error);
    g_assert (multiplex_support == MM_BEARER_MULTIPLEX_SUPPORT_REQUESTED);
}

static void
apn_type_from_string (void)
{
    MMBearerApnType apn_type = MM_BEARER_APN_TYPE_DEFAULT;
    GError *error = NULL;

    apn_type = mm_common_get_apn_type_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (apn_type == MM_BEARER_APN_TYPE_NONE);
    g_clear_error (&error);

    apn_type = mm_common_get_apn_type_from_string ("emergency", &error);
    g_assert_no_error (error);
    g_assert (apn_type == MM_BEARER_APN_TYPE_EMERGENCY);

    apn_type = mm_common_get_apn_type_from_string ("emergency|type-unknown", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (apn_type == MM_BEARER_APN_TYPE_NONE);
    g_clear_error (&error);

    apn_type = mm_common_get_apn_type_from_string ("emergency|local", &error);
    g_assert_no_error (error);
    g_assert (apn_type == (MM_BEARER_APN_TYPE_EMERGENCY | MM_BEARER_APN_TYPE_LOCAL));
}

static void
_3gpp_facility_from_string (void)
{
    MMModem3gppFacility facility = MM_MODEM_3GPP_FACILITY_CORP_PERS;
    GError *error = NULL;

    facility = mm_common_get_3gpp_facility_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (facility == MM_MODEM_3GPP_FACILITY_NONE);
    g_clear_error (&error);

    facility = mm_common_get_3gpp_facility_from_string ("ph-sim", &error);
    g_assert_no_error (error);
    g_assert (facility == MM_MODEM_3GPP_FACILITY_PH_SIM);

    facility = mm_common_get_3gpp_facility_from_string ("ph-sim|type-unknown", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (facility == MM_MODEM_3GPP_FACILITY_NONE);
    g_clear_error (&error);

    facility = mm_common_get_3gpp_facility_from_string ("ph-fsim|provider-pers", &error);
    g_assert_no_error (error);
    g_assert (facility == (MM_MODEM_3GPP_FACILITY_PH_FSIM | MM_MODEM_3GPP_FACILITY_PROVIDER_PERS));
}

static void
_3gpp_packet_service_state_from_string (void)
{
    MMModem3gppPacketServiceState packet_service_state = MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED;
    GError *error = NULL;

    packet_service_state = mm_common_get_3gpp_packet_service_state_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (packet_service_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN);
    g_clear_error (&error);

    packet_service_state = mm_common_get_3gpp_packet_service_state_from_string ("attached", &error);
    g_assert_no_error (error);
    g_assert (packet_service_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED);
}

static void
_3gpp_mico_mode_from_string (void)
{
    MMModem3gppMicoMode mico_mode = MM_MODEM_3GPP_MICO_MODE_UNSUPPORTED;
    GError *error = NULL;

    mico_mode = mm_common_get_3gpp_mico_mode_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (mico_mode == MM_MODEM_3GPP_MICO_MODE_UNKNOWN);
    g_clear_error (&error);

    mico_mode = mm_common_get_3gpp_mico_mode_from_string ("enabled", &error);
    g_assert_no_error (error);
    g_assert (mico_mode == MM_MODEM_3GPP_MICO_MODE_ENABLED);
}

static void
_3gpp_drx_cycle_from_string (void)
{
    MMModem3gppDrxCycle drx_cycle = MM_MODEM_3GPP_DRX_CYCLE_UNSUPPORTED;
    GError *error = NULL;

    drx_cycle = mm_common_get_3gpp_drx_cycle_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (drx_cycle == MM_MODEM_3GPP_DRX_CYCLE_UNKNOWN);
    g_clear_error (&error);

    drx_cycle = mm_common_get_3gpp_drx_cycle_from_string ("128", &error);
    g_assert_no_error (error);
    g_assert (drx_cycle == MM_MODEM_3GPP_DRX_CYCLE_128);
}

static void
access_type_preference_from_string (void)
{
    MMBearerAccessTypePreference access_type_preference = MM_BEARER_ACCESS_TYPE_PREFERENCE_NON_3GPP_ONLY;
    GError *error = NULL;

    access_type_preference = mm_common_get_access_type_preference_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (access_type_preference == MM_BEARER_ACCESS_TYPE_PREFERENCE_NONE);
    g_clear_error (&error);

    access_type_preference = mm_common_get_access_type_preference_from_string ("3gpp-preferred", &error);
    g_assert_no_error (error);
    g_assert (access_type_preference == MM_BEARER_ACCESS_TYPE_PREFERENCE_3GPP_PREFERRED);   
}

static void
profile_source_from_string (void)
{
    MMBearerProfileSource profile_source = MM_BEARER_PROFILE_SOURCE_OPERATOR;
    GError *error = NULL;

    profile_source = mm_common_get_profile_source_from_string ("not found", &error);
    g_assert_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS);
    g_assert (profile_source == MM_BEARER_PROFILE_SOURCE_UNKNOWN);
    g_clear_error (&error);

    profile_source = mm_common_get_profile_source_from_string ("modem", &error);
    g_assert_no_error (error);
    g_assert (profile_source == MM_BEARER_PROFILE_SOURCE_MODEM);
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

    g_test_add_func ("/MM/Common/DateTime/iso8601", date_time_iso8601);

    g_test_add_func ("/MM/Common/StrConvTo/bands",             bands_to_string);
    g_test_add_func ("/MM/Common/StrConvTo/capabilities",      capabilities_to_string);
    g_test_add_func ("/MM/Common/StrConvTo/mode-combinations", mode_combinations_to_string);
    g_test_add_func ("/MM/Common/StrConvTo/ports",             ports_to_string);
    g_test_add_func ("/MM/Common/StrConvTo/sms-storages",      sms_storages_to_string);

    g_test_add_func ("/MM/Common/StrConvFrom/capabilities",              capabilities_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/modes",                     modes_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/bands",                     bands_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/boolean",                   boolean_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/rm_protocol",               rm_protocol_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/ip_type",                   ip_type_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/allowed_auth",              allowed_auth_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/sms_storage",               sms_storage_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/sms_cdma_teleservice_id",   sms_cdma_teleservice_id_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/sms_cdma_service_category", sms_cdma_service_category_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/call_direction",            call_direction_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/call_state",                call_state_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/call_state_reason",         call_state_reason_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/oma_features",              oma_features_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/oma_session_type",          oma_session_type_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/eps_ue_mode_operation",     eps_ue_mode_operation_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/access_technology",         access_technology_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/multiplex_support",         multiplex_support_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/apn_type",                  apn_type_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/3gpp_facility",             _3gpp_facility_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/3gpp_packet_service_state", _3gpp_packet_service_state_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/3gpp_mico_mode",            _3gpp_mico_mode_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/3gpp_drx_cycle",            _3gpp_drx_cycle_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/access_type",               access_type_preference_from_string);
    g_test_add_func ("/MM/Common/StrConvFrom/profile_source",            profile_source_from_string);

    return g_test_run ();
}
