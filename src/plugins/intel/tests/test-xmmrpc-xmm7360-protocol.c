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
#include <locale.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-test.h"
#include "mm-modem-helpers.h"
#include "mm-xmmrpc-xmm7360-protocol.h"

#define XMM_BUFFER(...) \
    .buffer = { __VA_ARGS__ }, \
    .buffer_size = sizeof((guint8[]){ __VA_ARGS__ }),

#define XMM_STRING_BUFFER(str_type, str_len, ...) \
    .buffer = { str_type,                         \
                str_len,                          \
                0x02,    /* len_padded: ASN int start marker */  \
                0x01,    /* len_padded: ASN int size 1 */        \
                str_len * (str_type == 0x55 ? 1 : str_type == 0x56 ? 2 : str_type == 0x57 ? 4 : 0), /* len_padded: ASN int val <str_len> */ \
                0x02,    /* pad_len:    ASN int start marker */  \
                0x01,    /* pad_len:    ASN int size 1 */        \
                0x00,    /* pad_len:    ASN int val  0 */        \
                __VA_ARGS__ /* string data */                    \
    }, \
    .buffer_size = sizeof((guint8[]){ __VA_ARGS__ }) + 8,

#define XMM_EXPECTED_ARG_BUFFER(...) \
    .expected_arg_buf = { __VA_ARGS__ }, \
    .expected_arg_size = sizeof((guint8[]){ __VA_ARGS__ }),

/*****************************************************************************/

typedef struct {
    const gchar  *detail;
    const gsize   offset;
    const gint    expected_consumed;
    const gint    expected_error;
    const gchar  *expected_error_string;
    const guint8  buffer[255];
    const guint   buffer_size;
    const guint8  expected_arg_buf[255];
    const gsize   expected_arg_size;
} ReadStrTest;

static const ReadStrTest read_str_tests[] = {
    {
        .detail = "zero_buffer",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "initial buffer size 0 too small (need 2)",
    },
    {
        .detail = "1b_buffer",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "initial buffer size 1 too small (need 2)",
        XMM_BUFFER( 0x02 )
    },
    {
        .detail = "bad_string_type",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "unhandled string type 0x02",
        XMM_BUFFER( 0x02, 0x04 )
    },
    {
        .detail = "small_buffer_big_message",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "initial buffer size 3 too small (need 5)",
        XMM_BUFFER( 0x55, 0x04, 0x02 )
    },
    {
        .detail = "1b_string_2chars",
        .expected_consumed = 10,
        XMM_STRING_BUFFER( 0x55, 0x02, 'H', 'i' )
        XMM_EXPECTED_ARG_BUFFER( 'H', 'i' )
    },
    {
        .detail = "1b_string_5chars",
        .expected_consumed = 13,
        XMM_STRING_BUFFER( 0x55, 0x05, 'H', 'e', 'l', 'l', 'o' )
        XMM_EXPECTED_ARG_BUFFER( 'H', 'e', 'l', 'l', 'o' )
    },
    {
        .detail = "2b_string_2chars",
        .expected_consumed = 12,
        XMM_STRING_BUFFER( 0x56, 0x02, 0x00, 'H', 0x00, 'i' )
        XMM_EXPECTED_ARG_BUFFER( 0x00, 'H', 0x00, 'i' )
    },
    {
        .detail = "4b_string_2chars",
        .expected_consumed = 16,
        XMM_STRING_BUFFER( 0x57, 0x02, 0x00, 0x01, 0x02, 'H',  0x00, 0x01, 0x02, 'i' )
        XMM_EXPECTED_ARG_BUFFER( 0x00, 0x01, 0x02, 'H',  0x00, 0x01, 0x02, 'i' )
    },
    {
        .detail = "1b_string_zero_length",
        .expected_consumed = 8,
        XMM_STRING_BUFFER( 0x55, 0x00 )
    },
    {
        .detail = "2b_string_zero_length",
        .expected_consumed = 8,
        XMM_STRING_BUFFER( 0x56, 0x00 )
    },
    {
        .detail = "4b_string_zero_length",
        .expected_consumed = 8,
        XMM_STRING_BUFFER( 0x57, 0x00 )
    },
};

static void
test_read_str (gconstpointer data)
{
    const ReadStrTest    *test = data;
    g_autoptr(GError)     error = NULL;
    gint                  consumed;
    g_autoptr(GByteArray) buffer = NULL;
    Xmm7360RpcMsgArg      arg = { 0 };

    buffer = g_byte_array_sized_new (sizeof (test->buffer_size));
    if (sizeof (test->buffer))
        g_byte_array_append (buffer, test->buffer, test->buffer_size);

    consumed = xmm7360_byte_array_read_string (buffer, test->offset, &arg, &error);
    if (error) {
        g_assert_error (error, MM_CORE_ERROR, test->expected_error);
        g_assert_cmpstr (error->message, ==, test->expected_error_string);
        g_assert_cmpint (consumed, <, 0);
        g_assert_cmpint (test->expected_arg_size, ==, 0);
        g_assert_cmpint (arg.size, ==, 0);
    } else {
        g_assert_no_error (error);
        g_assert_cmpint (test->expected_error, ==, 0);
        g_assert_false (!!test->expected_error_string);
        g_assert_cmpint (consumed, ==, test->expected_consumed);
        g_assert_cmpint (arg.type, ==, XMM7360_RPC_MSG_ARG_TYPE_STRING);
        g_assert_cmpint (arg.size, ==, test->expected_arg_size);
        g_assert_cmpmem (arg.value.string, arg.size, test->expected_arg_buf, test->expected_arg_size);
    }
}

/*****************************************************************************/

typedef struct {
    const gchar            *detail;
    const gsize             offset;
    const gint              expected_consumed;
    const gint              expected_error;
    const gchar            *expected_error_string;
    const gint              expected_val;
    const guint8            buffer[255];
    const guint             buffer_size;
    const Xmm7360RpcMsgArg  expected_arg;
} ReadAsnIntTest;

static const ReadAsnIntTest read_asn_int_tests[] = {
    {
        .detail = "zero_buffer",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "initial buffer size 0 too small (need 3)",
    },
    {
        .detail = "1b_buffer",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "initial buffer size 1 too small (need 3)",
        XMM_BUFFER( 0x02 )
    },
    {
        .detail = "2b_buffer",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "initial buffer size 2 too small (need 3)",
        XMM_BUFFER( 0x02, 0x01 )
    },
    {
        .detail = "small_buffer_big_message",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "buffer size 3 too small (need 6)",
        XMM_BUFFER( 0x02, 0x04, 0x02 )
    },
    {
        .detail = "bad_start",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "unexpected ASN start byte 0x00 (expected 0x02)",
        XMM_BUFFER( 0x00, 0x01, 0x01 )
    },
    {
        .detail = "no_data",
        .expected_consumed = 2,
        .expected_val = 0,
        XMM_BUFFER( 0x02, 0x00, 0x00 )
        .expected_arg = {
            .type = XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN,
        },
    },
    {
        .detail = "u8",
        .expected_consumed = 3,
        .expected_val = 1,
        XMM_BUFFER( 0x02, 0x01, 0x01 )
        .expected_arg = {
            .type = XMM7360_RPC_MSG_ARG_TYPE_BYTE,
            .value.b = 0x01,
        },
    },
    {
        .detail = "u16",
        .expected_consumed = 4,
        .expected_val = 0x102,
        XMM_BUFFER( 0x02, 0x02, 0x01, 0x02 )
        .expected_arg = {
            .type = XMM7360_RPC_MSG_ARG_TYPE_SHORT,
            .value.s = 0x102,
        },
    },
    {
        .detail = "u24_is_bad",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "unhandled int size 3",
        XMM_BUFFER( 0x02, 0x03, 0x01, 0x02, 0x03 )
    },
    {
        .detail = "u32",
        .expected_consumed = 6,
        .expected_val = 0x1020304,
        XMM_BUFFER( 0x02, 0x04, 0x01, 0x02, 0x03, 0x04 )
        .expected_arg = {
            .type = XMM7360_RPC_MSG_ARG_TYPE_LONG,
            .value.l = 0x1020304,
        },
    },
    {
        .detail = "ubad",
        .expected_consumed = -1,
        .expected_error = MM_CORE_ERROR_FAILED,
        .expected_error_string = "unhandled int size 6",
        XMM_BUFFER( 0x02, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 )
    },
};

static void
test_read_asn_int (gconstpointer data)
{
    const ReadAsnIntTest *test = data;
    g_autoptr(GError)     error = NULL;
    gint                  consumed;
    g_autoptr(GByteArray) buffer = NULL;
    gint                  val = 0;
    Xmm7360RpcMsgArg      arg = { 0 };

    buffer = g_byte_array_sized_new (sizeof (test->buffer_size));
    if (sizeof (test->buffer))
        g_byte_array_append (buffer, test->buffer, test->buffer_size);

    consumed = xmm7360_byte_array_read_asn_int (buffer, test->offset, &val, &arg, &error);
    if (error) {
        g_assert_error (error, MM_CORE_ERROR, test->expected_error);
        g_assert_cmpstr (error->message, ==, test->expected_error_string);
        g_assert_cmpint (consumed, <, 0);
        g_assert_cmpint (test->expected_arg.type, ==, 0);
    } else {
        g_assert_no_error (error);
        g_assert_cmpint (test->expected_error, ==, 0);
        g_assert_false (!!test->expected_error_string);
        g_assert_cmpint (consumed, ==, test->expected_consumed);
        g_assert_cmpint (val, ==, test->expected_val);
        if (test->expected_arg.type != XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN) {
            g_assert_cmpint (arg.type, ==, test->expected_arg.type);
            switch (arg.type) {
            case XMM7360_RPC_MSG_ARG_TYPE_BYTE:
                g_assert_cmpint (arg.value.b, ==, test->expected_arg.value.b);
                break;
            case XMM7360_RPC_MSG_ARG_TYPE_SHORT:
                g_assert_cmpint (arg.value.s, ==, test->expected_arg.value.s);
                break;
            case XMM7360_RPC_MSG_ARG_TYPE_LONG:
                g_assert_cmpint (arg.value.l, ==, test->expected_arg.value.l);
                break;
            case XMM7360_RPC_MSG_ARG_TYPE_STRING:
                g_assert_cmpmem (arg.value.string, arg.size, test->expected_arg.value.string, test->expected_arg.size);
                break;
            case XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN:
            default:
                g_assert_not_reached ();
            }
        }
    }
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    guint i;

    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    for (i = 0; i < G_N_ELEMENTS (read_asn_int_tests); i++) {
        g_autofree gchar *path;

        path = g_strdup_printf ("/MM/xmmrpc/read_asn_int/%s", read_asn_int_tests[i].detail);
        g_test_add_data_func (path, &read_asn_int_tests[i], test_read_asn_int);
    }

    for (i = 0; i < G_N_ELEMENTS (read_str_tests); i++) {
        g_autofree gchar *path;

        path = g_strdup_printf ("/MM/xmmrpc/read_str/%s", read_str_tests[i].detail);
        g_test_add_data_func (path, &read_str_tests[i], test_read_str);
    }

    return g_test_run ();
}
