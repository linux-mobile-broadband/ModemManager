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

#include "mm-log-object.h"

#include "mm-port-serial-xmmrpc-xmm7360.h"
#include "mm-intel-enums-types.h"

G_DEFINE_TYPE (MMPortSerialXmmrpcXmm7360, mm_port_serial_xmmrpc_xmm7360, MM_TYPE_PORT_SERIAL)

struct _MMPortSerialXmmrpcXmm7360Private {
    GSList *unsolicited_msg_handlers;
    guint unsolicited_msg_handlers_i;
};

static void
xmm7360_rpc_msg_arg_free (Xmm7360RpcMsgArg *arg)
{
    if (arg->type == XMM7360_RPC_MSG_ARG_TYPE_STRING) {
        g_free ((gchar *) arg->value.string);
    }
    g_free (arg);
}

void
xmm7360_rpc_response_free (Xmm7360RpcResponse *response)
{
    if (response) {
        g_byte_array_free (response->body, TRUE);
        g_ptr_array_free (response->content, TRUE);
        g_free (response);
    }
}

static gint
xmm7360_byte_array_read_asn_int (GByteArray *buf, gsize *offset, Xmm7360RpcMsgArg *arg)
{
    gint size;
    gint bytes_read;
    gint val;

    g_assert (buf->len > *offset + 2);

    /* check ASN start byte and read int size */
    g_assert (buf->data[(*offset)++] == 0x02);
    size = buf->data[(*offset)++];

    /* read actual value byte by byte */
    g_assert (buf->len >= *offset + size);
    val = 0;
    for (bytes_read = 0; bytes_read < size; bytes_read++) {
        val <<= 8;
        val |= buf->data[(*offset)++];
    }

    if (arg != NULL) {
        if (size == 0x01) {
            arg->type = XMM7360_RPC_MSG_ARG_TYPE_BYTE;
            arg->value.b = (gint8) val;
        } else if (size == 0x02) {
            arg->type = XMM7360_RPC_MSG_ARG_TYPE_SHORT;
            arg->value.s = (gint16) val;
        } else {
            arg->type = XMM7360_RPC_MSG_ARG_TYPE_LONG;
            arg->value.l = (gint32) val;
        }
    }

    return val;
}

static gchar *
xmm7360_byte_array_read_string (GByteArray *buf, gsize *offset, gsize *string_len)
{
    guchar        string_type;
    gsize         string_len_padded;
    gsize         pad_len;
    gchar        *result;

    g_assert (buf->len > *offset + 2);

    string_type = buf->data[(*offset)++];
    g_assert (string_type == 0x55 || string_type == 0x56 || string_type == 0x57);

    *string_len = buf->data[(*offset)++];
    if (*string_len & 0x80) {
        guchar bytelen;
        guchar i;

        bytelen = *string_len & 0x0f;
        *string_len = 0;
        g_assert (bytelen <= 4);
        g_assert (buf->len > *offset + bytelen);
        for (i = 0; i < bytelen; i++) {
            *string_len |= buf->data[(*offset)++] << (i * 8);
        }
    }

    if (string_type == 0x56) {
        /* 0x56 contains 2-byte chars */
        *string_len <<= 1;
    } else if (string_type == 0x57) {
        /* 0x57 contains 4-byte chars */
        *string_len <<= 2;
    }

    string_len_padded = (gsize) xmm7360_byte_array_read_asn_int (buf, offset, NULL);
    pad_len = (gsize) xmm7360_byte_array_read_asn_int (buf, offset, NULL);

    if (string_len_padded > 0) {
        g_assert (string_len_padded == *string_len + pad_len);
    } else {
        string_len_padded = *string_len + pad_len;
    }

    /* make sure the buffer is large enough */
    g_assert (buf->len >= *offset + string_len_padded);

    /* copy the result */
    result = g_malloc (*string_len);
    memcpy (result, &(buf->data[*offset]), *string_len);

    /* move pointer to the end of the string data */
    *offset += (*string_len + pad_len);

    return result;
}


void
xmm7360_byte_array_append_asn_int4 (GByteArray *buf, gint32 value)
{
    gint32 value_be = GINT32_TO_BE (value);
    g_byte_array_append (buf, (const guint8 *) "\x02\x04", 2);
    g_byte_array_append (buf, (const guint8 *) &value_be, 4);
}

static void
xmm7360_byte_array_append_uint8 (GByteArray *buf, gulong val)
{
    guint8 _val = (guint8) val;
    g_byte_array_append (buf, &_val, 1);
}

static void
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

static GString *
xmm7360_byte_array_hexlify (GByteArray *buf) {
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

static void
xmm7360_byte_array_log (gpointer obj, const gchar *prefix, GByteArray *buf)
{
    GString *hexlified;

    hexlified = xmm7360_byte_array_hexlify (buf);
    mm_obj_dbg (obj, "%sb'%s'", prefix ? prefix : "", hexlified->str);
    g_string_free (hexlified, TRUE);
}

static GString *
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

static void
xmm7360_rpc_msg_args_log (gpointer obj, const gchar *prefix, GPtrArray *args)
{
    GString *args_string;

    args_string = xmm7360_rpc_args_to_string (args);

    /* truncate very long strings to avoid overly verbose logs */
    if (args_string->len > 64) {
        g_string_truncate (args_string, 58);
        g_string_append_printf (args_string, "... (%d args)", args->len);
    }

    mm_obj_dbg (obj, "%s%s", prefix ? prefix : "", args_string->str);
    g_string_free (args_string, TRUE);
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

static GByteArray *
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

static int
xmm7360_rpc_msg_body_unpack (GByteArray *buf, GPtrArray *args, GError **error)
{
    GError           *inner_error = NULL;
    gsize             offset;
    gchar            *str_value;
    Xmm7360RpcMsgArg *arg;

    g_assert (buf != NULL && args != NULL);

    offset = 0;
    while (offset < buf->len) {
        arg = g_new0 (Xmm7360RpcMsgArg, 1);
        arg->size = 0;
        switch (buf->data[offset]) {
            case 0x02:
                xmm7360_byte_array_read_asn_int (buf, &offset, arg);
                break;
            case 0x55:
            case 0x56:
            case 0x57:
                arg->type = XMM7360_RPC_MSG_ARG_TYPE_STRING;
                str_value = xmm7360_byte_array_read_string (buf, &offset, &arg->size);
                if (str_value == NULL) {
                    inner_error = g_error_new (MM_CORE_ERROR,
                                               MM_CORE_ERROR_FAILED,
                                               "Parsing a string failed");
                    goto out;
                }
                arg->value.string = str_value;
                break;
            default:
                arg->type = XMM7360_RPC_MSG_ARG_TYPE_UNKNOWN;
                inner_error = g_error_new (MM_CORE_ERROR,
                                           MM_CORE_ERROR_FAILED,
                                           "Unknown type 0x%x",
                                           buf->data[offset]);
                goto out;
        }
        g_ptr_array_add (args, arg);
    }

out:
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return -1;
    }

    return 0;
}

static Xmm7360RpcResponse *
parse_response_xmm7360 (GByteArray *buf)
{
    GError             *error = NULL;
    Xmm7360RpcResponse *response = NULL;
    gsize               offset;
    Xmm7360RpcMsgArg   *arg;

    /* first 20 bytes are format header:
     * message size           (  int32, 4)
     * message size (again!)  (asn int, 6)
     * unsolicited message id (asn int, 6)
     * transmission id        (  int32, 4)
     */
    g_assert (buf->len > 20);

    offset = 4;
    if (*(gint32 *) buf->data != xmm7360_byte_array_read_asn_int (buf, &offset, NULL)) {
        mm_obj_dbg (NULL, "error parsing RPC message: length mismatch");
        goto err;
    }

    /* initialize all fields of the response */
    response = g_new0 (Xmm7360RpcResponse, 1);
    response->id = GINT32_FROM_BE (*(gint32 *) (buf->data + 16));
    response->type = XMM7360_RPC_RESPONSE_TYPE_UNKNOWN;
    response->unsol_id = xmm7360_byte_array_read_asn_int (buf, &offset, NULL);
    response->body = g_byte_array_new ();
    response->content = g_ptr_array_new_with_free_func ((GDestroyNotify)xmm7360_rpc_msg_arg_free);

    g_byte_array_append (response->body, &buf->data[20], buf->len - 20);
    if (xmm7360_rpc_msg_body_unpack (response->body, response->content, &error) != 0) {
        mm_obj_dbg (NULL, "error parsing RPC message: unpacking failed: %s", error->message);
        g_error_free (error);
        goto err;
    }

    if (response->id == 0x11000100) {
        response->type = XMM7360_RPC_RESPONSE_TYPE_RESPONSE;
    } else if ((response->id & 0xffffff00) == 0x11000100) {
        if (response->unsol_id >= 0x7d0) {
            response->type = XMM7360_RPC_RESPONSE_TYPE_ASYNC_ACK;
        } else {
            response->type = XMM7360_RPC_RESPONSE_TYPE_RESPONSE;

            /* discard first element in content (equals transmission id) */
            arg = (Xmm7360RpcMsgArg *) g_ptr_array_index (response->content, 0);
            if (arg->type != XMM7360_RPC_MSG_ARG_TYPE_LONG
                || (guint32) XMM7360_RPC_MSG_ARG_GET_INT (arg) != response->id) {
                mm_obj_dbg (NULL, "error parsing RPC message: invalid start of async response body");
                goto err;
            }
            g_ptr_array_remove_index (response->content, 0);
            g_byte_array_remove_range (response->body, 0, 6);
        }
    } else {
        response->type = XMM7360_RPC_RESPONSE_TYPE_UNSOLICITED;
    }

    return response;

err:
    if (response)
        xmm7360_rpc_response_free (response);
    return NULL;
}

static MMPortSerialResponseType
parse_response (MMPortSerial *port,
                GByteArray *response_buffer,
                GByteArray **parsed_response,
                GError **error)
{
    if (!response_buffer->len)
        return MM_PORT_SERIAL_RESPONSE_NONE;

    *parsed_response = g_byte_array_new ();
    g_byte_array_append (*parsed_response, response_buffer->data, response_buffer->len);

    /* Fully cleanup the response array, we'll consider the contents we got
     * as the full reply that the command may expect. */
    g_byte_array_remove_range (response_buffer, 0, response_buffer->len);

    return MM_PORT_SERIAL_RESPONSE_BUFFER;
}

static void
serial_command_ready (MMPortSerial *port,
                      GAsyncResult *res,
                      GTask *task)
{
    GByteArray *response_buffer;
    GError *error = NULL;
    Xmm7360RpcResponse *response;

    response_buffer = mm_port_serial_command_finish (port, res, &error);
    if (!response_buffer) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    response = parse_response_xmm7360 (response_buffer);
    if (response_buffer->len > 0)
        g_byte_array_remove_range (response_buffer, 0, response_buffer->len);
    g_byte_array_unref (response_buffer);

    if (response &&
        response->type == XMM7360_RPC_RESPONSE_TYPE_RESPONSE) {
        xmm7360_byte_array_log (port, "", response->body);
        xmm7360_rpc_msg_args_log (port, "", response->content);
    }

    g_task_return_pointer (task, response, (GDestroyNotify) xmm7360_rpc_response_free);
    g_object_unref (task);
}

Xmm7360RpcResponse *
mm_port_serial_xmmrpc_xmm7360_command_finish (MMPortSerialXmmrpcXmm7360  *self,
                                              GAsyncResult               *res,
                                              GError                    **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

void
mm_port_serial_xmmrpc_xmm7360_command (MMPortSerialXmmrpcXmm7360 *self,
                                       Xmm7360RpcCallId           callid,
                                       gboolean                   is_async,
                                       GByteArray                *body,
                                       guint32                    timeout_seconds,
                                       gboolean                   allow_cached,
                                       GCancellable              *cancellable,
                                       GAsyncReadyCallback        callback,
                                       gpointer                   user_data)
{
    g_autoptr(GByteArray) buf = NULL;
    GTask *task;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_PORT_SERIAL_XMMRPC_XMM7360 (self));

    buf = xmm7360_command_to_byte_array (callid, is_async, body);
    g_return_if_fail (buf != NULL);

    task = g_task_new (self, NULL, callback, user_data);

    /* Body logged in debug_log() */
    mm_obj_dbg (self, "--> %s%s", is_async ? "(async) " : "", xmm_7360_rpc_call_id_get_string (callid));

    mm_port_serial_command (MM_PORT_SERIAL (self),
                            buf,
                            timeout_seconds,
                            allow_cached,
                            TRUE, /* raw commands always run next, never queued last */
                            cancellable,
                            (GAsyncReadyCallback)serial_command_ready,
                            task);
}

/*****************************************************************************/

typedef struct {
    guint id;
    MMPortSerialXmmrpcXmm7360UnsolicitedMsgFn callback;
    gboolean enable;
    gpointer user_data;
    GDestroyNotify notify;
} MMRpcUnsolicitedMsgHandler;

static gint
unsolicited_msg_handler_cmp (MMRpcUnsolicitedMsgHandler *handler, guint *id)
{
    return handler->id == *id;
}

guint
mm_port_serial_xmmrpc_xmm7360_add_unsolicited_msg_handler (MMPortSerialXmmrpcXmm7360 *self,
                                                           MMPortSerialXmmrpcXmm7360UnsolicitedMsgFn callback,
                                                           gpointer user_data,
                                                           GDestroyNotify notify)
{
    MMRpcUnsolicitedMsgHandler *handler;

    g_return_val_if_fail (MM_IS_PORT_SERIAL_XMMRPC_XMM7360 (self), 0);

    handler = g_slice_new (MMRpcUnsolicitedMsgHandler);
    handler->id = ++(self->priv->unsolicited_msg_handlers_i);
    handler->callback = callback;
    handler->enable = TRUE;
    handler->user_data = user_data;
    handler->notify = notify;

    /* The new handler is always PREPENDED */
    self->priv->unsolicited_msg_handlers = g_slist_prepend (self->priv->unsolicited_msg_handlers, handler);

    return handler->id;
}

void
mm_port_serial_xmmrpc_xmm7360_enable_unsolicited_msg_handler (MMPortSerialXmmrpcXmm7360 *self,
                                                              guint handler_id,
                                                              gboolean enable)
{
    GSList *existing;
    MMRpcUnsolicitedMsgHandler *handler;

    g_return_if_fail (MM_IS_PORT_SERIAL_XMMRPC_XMM7360 (self));

    existing = g_slist_find_custom (self->priv->unsolicited_msg_handlers,
                                    &handler_id,
                                    (GCompareFunc)unsolicited_msg_handler_cmp);
    if (existing) {
        handler = existing->data;
        handler->enable = enable;
    }
}

static void
parse_unsolicited (MMPortSerial *port, GByteArray *response_buffer)
{
    MMPortSerialXmmrpcXmm7360 *self = MM_PORT_SERIAL_XMMRPC_XMM7360 (port);
    g_autoptr(Xmm7360RpcResponse) response = NULL;
    GSList *iter;

    response = parse_response_xmm7360 (response_buffer);

    if (!response) {
        return;
    }

    if (response->type == XMM7360_RPC_RESPONSE_TYPE_RESPONSE) {
        mm_obj_dbg (port, "<-- (response)");
        /* Body logged in serial_command_ready() */
        return;
    }

    if (response->type == XMM7360_RPC_RESPONSE_TYPE_ASYNC_ACK) {
        /* discard ASYNC_ACK message since the RESPONSE is expected to follow later
         * empty the buffer to show that the message is dealt with
         */
        mm_obj_dbg (port, "<-- (async-ack)");
        g_byte_array_remove_range (response_buffer, 0, response_buffer->len);
        return;
    }

    mm_obj_dbg (port, "<-- (unsolicited) %s",
                xmm_7360_rpc_unsol_id_get_string (response->unsol_id));
    xmm7360_byte_array_log (port, "", response->body);
    xmm7360_rpc_msg_args_log (port, "", response->content);

    for (iter = self->priv->unsolicited_msg_handlers; iter; iter = iter->next) {
        MMRpcUnsolicitedMsgHandler *handler = (MMRpcUnsolicitedMsgHandler *) iter->data;

        if (!handler->enable)
            continue;

        if (handler->callback (self, response, handler->user_data)) {
            /* if successful, empty the buffer to show that the message is dealt with */
            g_byte_array_remove_range (response_buffer, 0, response_buffer->len);
            return;
        }
    }

    /* unhandled unsolicited message is discarded */
    g_byte_array_remove_range (response_buffer, 0, response_buffer->len);
}

static void
debug_log (MMPortSerial *self,
           const gchar  *prefix,
           const gchar  *buf,
           gsize         len)
{
    static GString *debug = NULL;
    const  char    *s;

    if (g_strcmp0 (prefix, "<--") == 0) {
        /* Incoming data is already logged in parse functions */
        return;
    }

    if (!debug)
        debug = g_string_sized_new (256);

    g_string_append (debug, prefix);
    g_string_append (debug, " raw [");

    s = buf;
    while (len--) {
        g_string_append_printf (debug, "%02x", (guint8)*s);
        s++;
    }

    g_string_append_c (debug, ']');
    mm_obj_dbg (self, "%s", debug->str);
    g_string_truncate (debug, 0);
}

/*****************************************************************************/

MMPortSerialXmmrpcXmm7360 *
mm_port_serial_xmmrpc_xmm7360_new (const char   *name)
{
    return MM_PORT_SERIAL_XMMRPC_XMM7360 (g_object_new (MM_TYPE_PORT_SERIAL_XMMRPC_XMM7360,
                                                        MM_PORT_DEVICE, name,
                                                        MM_PORT_SUBSYS, MM_PORT_SUBSYS_WWAN,
                                                        MM_PORT_TYPE,   MM_PORT_TYPE_XMMRPC,
                                                        NULL));
}

static void
mm_port_serial_xmmrpc_xmm7360_init (MMPortSerialXmmrpcXmm7360 *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_PORT_SERIAL_XMMRPC_XMM7360,
                                              MMPortSerialXmmrpcXmm7360Private);

    self->priv->unsolicited_msg_handlers_i = 0;
    self->priv->unsolicited_msg_handlers = NULL;
}

static void
finalize (GObject *object)
{
    MMPortSerialXmmrpcXmm7360 *self = MM_PORT_SERIAL_XMMRPC_XMM7360 (object);

    while (self->priv->unsolicited_msg_handlers) {
        MMRpcUnsolicitedMsgHandler *handler = (MMRpcUnsolicitedMsgHandler *) self->priv->unsolicited_msg_handlers->data;

        if (handler->notify)
            handler->notify (handler->user_data);

        g_slice_free (MMRpcUnsolicitedMsgHandler, handler);
        self->priv->unsolicited_msg_handlers = g_slist_delete_link (self->priv->unsolicited_msg_handlers,
                                                                    self->priv->unsolicited_msg_handlers);
    }

    G_OBJECT_CLASS (mm_port_serial_xmmrpc_xmm7360_parent_class)->finalize (object);
}

static void
mm_port_serial_xmmrpc_xmm7360_class_init (MMPortSerialXmmrpcXmm7360Class *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMPortSerialClass *serial_class = MM_PORT_SERIAL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortSerialXmmrpcXmm7360Private));

    object_class->finalize = finalize;

    serial_class->parse_response = parse_response;
    serial_class->parse_unsolicited = parse_unsolicited;
    serial_class->debug_log = debug_log;
}
