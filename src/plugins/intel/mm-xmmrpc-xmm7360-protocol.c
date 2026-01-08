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
 * Copyright (C) 2019 James Wah
 * Copyright (C) 2020 Marinus Enzinger <marinus@enzingerm.de>
 * Copyright (C) 2023 Shane Parslow
 * Copyright (C) 2024 Thomas Vogt
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-xmmrpc-xmm7360-protocol.h"
#include "mm-intel-enums-types.h"

static void
xmm7360_rpc_msg_arg_free (Xmm7360RpcMsgArg *arg)
{
    if (arg->type == XMM7360_RPC_MSG_ARG_TYPE_STRING) {
        g_free ((gchar *) arg->value.string);
    }
    g_free (arg);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Xmm7360RpcMsgArg, xmm7360_rpc_msg_arg_free)

void
xmm7360_rpc_response_free (Xmm7360RpcResponse *response)
{
    if (response) {
        g_byte_array_free (response->body, TRUE);
        g_ptr_array_free (response->content, TRUE);
        g_free (response);
    }
}

#define PARSE_ERROR(fmt, ...) { \
    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, fmt, __VA_ARGS__); \
}

gint
xmm7360_byte_array_read_asn_int (GByteArray        *buf,
                                 gsize              offset,
                                 gint              *out_val,
                                 Xmm7360RpcMsgArg  *out_arg,
                                 GError           **error)
{
    const gsize orig_offset = offset;
    gint        size;
    gint        bytes_read;
    gint        val;

    if (buf->len <= offset + 2) {
        PARSE_ERROR ("initial buffer size %u too small (need %zu)",
                     buf->len, offset + 2 + 1);
        return -1;
    }

    /* check ASN start byte and read int size */
    if (buf->data[offset] != 0x02) {
        PARSE_ERROR ("unexpected ASN start byte 0x%02X (expected 0x02)", buf->data[offset]);
        return -1;
    }
    offset++;
    size = buf->data[offset++];
    if (size > 4 || size == 3) {
        PARSE_ERROR ("unhandled int size %d", size);
        return -1;
    }

    /* read actual value byte by byte */
    if (buf->len < offset + size) {
        PARSE_ERROR ("buffer size %u too small (need %zu)", buf->len, offset + size);
        return -1;
    }
    val = 0;
    for (bytes_read = 0; bytes_read < size; bytes_read++) {
        val <<= 8;
        val |= buf->data[offset++];
    }

    if (out_arg) {
        if (size == 0x00) {
            out_arg->type = XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN;
        } else if (size == 0x01) {
            out_arg->type = XMM7360_RPC_MSG_ARG_TYPE_BYTE;
            out_arg->value.b = (gint8) val;
        } else if (size == 0x02) {
            out_arg->type = XMM7360_RPC_MSG_ARG_TYPE_SHORT;
            out_arg->value.s = (gint16) val;
        } else if (size == 0x04) {
            out_arg->type = XMM7360_RPC_MSG_ARG_TYPE_LONG;
            out_arg->value.l = (gint32) val;
        } else {
            /* size is validated above */
            g_assert_not_reached ();
        }
    }

    if (out_val)
        *out_val = val;

    /* return bytes consumed by this function */
    return offset - orig_offset;
}

gint
xmm7360_byte_array_read_string (GByteArray        *buf,
                                gsize              offset,
                                Xmm7360RpcMsgArg  *out_arg,
                                GError           **error)
{
    const gsize  orig_offset = offset;
    guchar       string_type;
    gint         string_len_padded;
    gint         pad_len;
    gint         string_len;
    gint         consumed;

    if (buf->len < offset + 2) {
        PARSE_ERROR ("initial buffer size %u too small (need %zu)",
                     buf->len, offset + 2);
        return -1;
    }

    string_type = buf->data[offset++];
    if (string_type != 0x55 && string_type != 0x56 && string_type != 0x57) {
        PARSE_ERROR ("unhandled string type 0x%02X", string_type);
        return -1;
    }

    string_len = buf->data[offset++];
    if (string_len & 0x80) {
        guchar bytelen;
        guint8 i;

        bytelen = string_len & 0x0f;
        if (bytelen > 4) {
            PARSE_ERROR ("unhandled string length size 0x%02X", bytelen);
            return -1;
        } else if (buf->len <= offset + bytelen) {
            PARSE_ERROR ("string len buffer size %u too small (need %zu)",
                         buf->len, offset + bytelen);
            return -1;
        }

        string_len = 0;
        for (i = 0; i < bytelen; i++)
            string_len |= buf->data[offset++] << (i * 8);
    }

    if (string_type == 0x56) {
        /* 0x56 contains 2-byte chars */
        string_len <<= 1;
    } else if (string_type == 0x57) {
        /* 0x57 contains 4-byte chars */
        string_len <<= 2;
    }

    consumed = xmm7360_byte_array_read_asn_int (buf, offset, &string_len_padded, NULL, error);
    if (consumed < 0)
        return -1;
    offset += consumed;

    consumed = xmm7360_byte_array_read_asn_int (buf, offset, &pad_len, NULL, error);
    if (consumed < 0)
        return -1;
    offset += consumed;

    if (string_len_padded > 0) {
        if (string_len_padded != string_len + pad_len) {
            PARSE_ERROR ("padded string len %u mismatch (expected %u)",
                         string_len_padded, string_len + pad_len);
            return -1;
        }
    } else {
        string_len_padded = string_len + pad_len;
    }

    /* make sure the buffer is large enough */
    if (buf->len < offset + string_len_padded) {
        PARSE_ERROR ("padded string len buffer size %u too small (need %zu)",
                     buf->len, offset + string_len_padded);
        return -1;
    }

    /* copy the result */
    if (out_arg) {
        out_arg->type = XMM7360_RPC_MSG_ARG_TYPE_STRING;
        /* Copy the bare string without padding */
        out_arg->value.string = g_memdup (buf->data + offset, string_len);
        out_arg->size = string_len;
    }

    /* return bytes consumed by this function */
    return offset + string_len_padded - orig_offset;
}


void
xmm7360_byte_array_append_asn_int4 (GByteArray *buf, gint32 value)
{
    gint32 value_be = GINT32_TO_BE (value);
    g_byte_array_append (buf, (const guint8 *) "\x02\x04", 2);
    g_byte_array_append (buf, (const guint8 *) &value_be, 4);
}

void
xmm7360_byte_array_append_uint8 (GByteArray *buf, gulong val)
{
    guint8 _val = (guint8) val;
    g_byte_array_append (buf, &_val, 1);
}

void
xmm7360_byte_array_append_string (GByteArray   *buf,
                                  const guint8 *data,
                                  gsize         data_len,
                                  guint         data_len_padded)
{
    gsize  i;
    gsize  pad_len;

    g_assert (data_len_padded >= data_len);
    pad_len = data_len_padded - data_len;

    /* only support 1-byte element size */
    xmm7360_byte_array_append_uint8 (buf, 0x55);

    if (data_len < 0x80) {
        xmm7360_byte_array_append_uint8 (buf, data_len);
    } else {
        guchar bytelen = 0x80;
        /* write dummy first byte (updated later) */
        xmm7360_byte_array_append_uint8 (buf, bytelen);
        for (i = data_len; i > 0; i >>= 8) {
            bytelen++;
            xmm7360_byte_array_append_uint8 (buf, i & 0xff);
        }
        /* update first byte */
        buf->data[buf->len - 1 - (bytelen & 0x7f)] = bytelen;
    }

    xmm7360_byte_array_append_asn_int4 (buf, data_len_padded);
    xmm7360_byte_array_append_asn_int4 (buf, pad_len);
    g_byte_array_append (buf, data, data_len);
    for (i = 0; i < pad_len; i++) {
        xmm7360_byte_array_append_uint8 (buf, 0);
    }
}

GString *
xmm7360_byte_array_hexlify (GByteArray *buf)
{
    GString *string;
    gsize i;

    string = g_string_sized_new (2 * buf->len + 1);
    for (i = 0; i < buf->len; i++) {
        g_string_append_printf (string, "%02x", buf->data[i]);
    }
    g_string_append (string, "");

    /* truncate very long strings to avoid overly verbose logs */
    if (string->len > 64) {
        gsize string_len;
        string_len = string->len;
        g_string_truncate (string, 48);
        g_string_append_printf (string, "... (%" G_GSIZE_FORMAT " chars)", string_len);
    }

    return string;
}

GString *
xmm7360_rpc_args_to_string (GPtrArray *args)
{
    guint i;
    GString *result;
    Xmm7360RpcMsgArg *arg;
    gint16 arg_value_short;
    gint32 arg_value_long;

    result = g_string_new ("");

    for (i = 0; i < args->len; i++) {
        arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (args, i);
        if (i > 0) {
            g_string_append (result, " ");
        }
        switch (arg->type) {
        case XMM7360_RPC_MSG_ARG_TYPE_BYTE:
            g_string_append_printf (result, "B(0x%02x)", arg->value.b);
            break;
        case XMM7360_RPC_MSG_ARG_TYPE_SHORT:
            arg_value_short = GINT16_TO_BE (arg->value.s);
            g_string_append_printf (result, "S(0x%04x)", arg_value_short);
            break;
        case XMM7360_RPC_MSG_ARG_TYPE_LONG:
            arg_value_long = GINT32_TO_BE (arg->value.l);
            g_string_append_printf (result, "L(0x%08x)", arg_value_long);
            break;
        case XMM7360_RPC_MSG_ARG_TYPE_STRING:
            g_string_append_printf (result, "STR(size=%" G_GSIZE_FORMAT ")", arg->size);
            break;
        case XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN:
        default:
            g_assert_not_reached ();
        }
    }

    return result;
}

GByteArray *
xmm7360_rpc_args_to_byte_array (const Xmm7360RpcMsgArg *args)
{
    GByteArray *result;
    gint16 arg_value_short;
    gint32 arg_value_long;

    if (args == NULL)
        return NULL;

    result = g_byte_array_new ();
    for (; args->type != XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN; args++) {
        switch (args->type) {
        case XMM7360_RPC_MSG_ARG_TYPE_BYTE:
            g_byte_array_append (result, (guint8[]) { 0x02, 0x01 }, 2);
            g_byte_array_append (result, (guint8 *) &(args->value.b), 1);
            break;
        case XMM7360_RPC_MSG_ARG_TYPE_SHORT:
            arg_value_short = GINT16_TO_BE (args->value.s);
            g_byte_array_append (result, (guint8[]) { 0x02, 0x02 }, 2);
            g_byte_array_append (result, (guint8 *) &arg_value_short, 2);
            break;
        case XMM7360_RPC_MSG_ARG_TYPE_LONG:
            arg_value_long = GINT32_TO_BE (args->value.l);
            g_byte_array_append (result, (guint8[]) { 0x02, 0x04 }, 2);
            g_byte_array_append (result, (guint8 *) &arg_value_long, 4);
            break;
        case XMM7360_RPC_MSG_ARG_TYPE_STRING:
            xmm7360_byte_array_append_string (result,
                                              (const guint8 *) args->value.string,
                                              args->size,
                                              args->size + args->pad);
            break;
        case XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN:
        default:
            /* should be unreachable */
            return NULL;
        }
    }
    return result;
}

GByteArray *
xmm7360_command_to_byte_array (Xmm7360RpcCallId  callid,
                               gboolean          is_async,
                               GByteArray       *body)
{
    GByteArray *buf;
    gint32      tid;
    gint32      tid_word;
    gint32      tid_word_be;
    guint32     total_len;

    buf = g_byte_array_sized_new (22);

    if (body == NULL) {
        body = g_byte_array_new ();
        xmm7360_byte_array_append_asn_int4 (body, 0);
    } else {
        g_byte_array_ref (body);
    }

    if (is_async) {
        tid = 0x11000101;
    } else {
        tid = 0;
    }
    tid_word = 0x11000100 | tid;

    total_len = body->len + 16;
    if (tid) {
        total_len += 6;
    }
    g_byte_array_append (buf, (const guint8 *) &total_len, 4);
    xmm7360_byte_array_append_asn_int4 (buf, total_len);
    xmm7360_byte_array_append_asn_int4 (buf, callid);
    tid_word_be = GINT32_TO_BE (tid_word);
    g_byte_array_append (buf, (const guint8 *) &tid_word_be, 4);
    if (tid) {
        xmm7360_byte_array_append_asn_int4 (buf, tid);
    }

    g_assert (total_len == buf->len + body->len - 4);
    g_byte_array_append (buf, (const guint8 *) body->data, body->len);
    g_byte_array_unref (body);

    return buf;
}

gboolean
xmm7360_rpc_msg_body_unpack (GByteArray *buf, GPtrArray *args, GError **error)
{
    gsize   offset;
    gint    consumed;

    g_assert (buf != NULL && args != NULL);

    offset = 0;
    while (offset < buf->len) {
        g_autoptr(Xmm7360RpcMsgArg) arg = NULL;

        arg = g_new0 (Xmm7360RpcMsgArg, 1);
        arg->size = 0;
        switch (buf->data[offset]) {
            case 0x02:
                consumed = xmm7360_byte_array_read_asn_int (buf,
                                                            offset,
                                                            NULL,
                                                            arg,
                                                            error);
                if (consumed < 0)
                    return FALSE;
                offset += consumed;
                break;
            case 0x55:
            case 0x56:
            case 0x57:
                arg->type = XMM7360_RPC_MSG_ARG_TYPE_STRING;
                consumed = xmm7360_byte_array_read_string (buf,
                                                           offset,
                                                           arg,
                                                           error);
                if (consumed < 0)
                    return FALSE;
                offset += consumed;
                break;
            default:
                arg->type = XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN;
                PARSE_ERROR ("Unknown type 0x%x", buf->data[offset]);
                return FALSE;
        }
        g_ptr_array_add (args, g_steal_pointer (&arg));
    }

    return TRUE;
}

Xmm7360RpcResponse *
xmm7360_parse_response (GByteArray *buf, GError **error)
{
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    gsize                         offset;
    gint                          consumed;
    gint                          asn_msg_len = 0;
    gint                          unsol_id = 0;

    /* first 20 bytes are format header:
     * message size           (  int32, 4)
     * message size (again!)  (asn int, 6)
     * unsolicited message id (asn int, 6)
     * transmission id        (  int32, 4)
     */
    if (buf->len <= 20) {
        PARSE_ERROR ("error parsing RPC message: message size %u too short", buf->len);
        return NULL;
    }

    /* Read and validate the ASN message size */
    offset = 4;
    consumed = xmm7360_byte_array_read_asn_int (buf, offset, &asn_msg_len, NULL, error);
    if (consumed < 0) {
        g_prefix_error (error, "error parsing RPC message: error reading ASN message size: ");
        return NULL;
    }
    offset += consumed;
    /* Make sure the ASN message size and message size agree */
    if (*(gint32 *) buf->data != asn_msg_len) {
        PARSE_ERROR ("error parsing RPC message: length mismatch (buf %d != asn %d)",
                     *(gint32 *) buf->data, asn_msg_len);
        return NULL;
    }

    consumed = xmm7360_byte_array_read_asn_int (buf, offset, &unsol_id, NULL, error);
    if (consumed < 0) {
        g_prefix_error (error, "error parsing RPC message: error reading unsolicited message id: ");
        return NULL;
    }
    offset += consumed;

    /* initialize all fields of the response */
    response = g_new0 (Xmm7360RpcResponse, 1);
    response->id = GINT32_FROM_BE (*(gint32 *) (buf->data + 16));
    response->type = XMM7360_RPC_RESPONSE_TYPE_UNKNOWN;
    response->unsol_id = unsol_id;
    response->body = g_byte_array_new ();
    response->content = g_ptr_array_new_with_free_func ((GDestroyNotify)xmm7360_rpc_msg_arg_free);

    g_byte_array_append (response->body, &buf->data[20], buf->len - 20);
    if (!xmm7360_rpc_msg_body_unpack (response->body, response->content, error))
        return NULL;

    if (response->id == 0x11000100) {
        response->type = XMM7360_RPC_RESPONSE_TYPE_RESPONSE;
    } else if ((response->id & 0xffffff00) == 0x11000100) {
        if (response->unsol_id >= 0x7d0) {
            response->type = XMM7360_RPC_RESPONSE_TYPE_ASYNC_ACK;
        } else {
            Xmm7360RpcMsgArg *arg;

            response->type = XMM7360_RPC_RESPONSE_TYPE_RESPONSE;

            /* discard first element in content (equals transmission id) */
            arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 0);
            if (arg->type != XMM7360_RPC_MSG_ARG_TYPE_LONG
                || (guint32) XMM7360_RPC_MSG_ARG_GET_INT (arg) != response->id) {
                PARSE_ERROR ("error parsing RPC message: invalid start of async response body (type %u not LONG or ID mismatch)",
                             arg->type);
                return NULL;
            }
            g_ptr_array_remove_index (response->content, 0);
            g_byte_array_remove_range (response->body, 0, 6);
        }
    } else {
        response->type = XMM7360_RPC_RESPONSE_TYPE_UNSOLICITED;
    }

    return g_steal_pointer (&response);
}
