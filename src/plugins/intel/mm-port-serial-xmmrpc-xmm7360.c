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
xmm7360_byte_array_log (gpointer obj, const gchar *prefix, GByteArray *buf)
{
    GString *hexlified;

    hexlified = xmm7360_byte_array_hexlify (buf);
    mm_obj_dbg (obj, "%sb'%s'", prefix ? prefix : "", hexlified->str);
    g_string_free (hexlified, TRUE);
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

    response = xmm7360_parse_response (response_buffer, &error);
    if (!response) {
        mm_obj_dbg (port, "%s", error->message);
        g_clear_error (&error);
    }
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
    MMPortSerialXmmrpcXmm7360     *self = MM_PORT_SERIAL_XMMRPC_XMM7360 (port);
    g_autoptr(Xmm7360RpcResponse)  response = NULL;
    g_autoptr(GError)              error = NULL;
    GSList *iter;

    response = xmm7360_parse_response (response_buffer, &error);
    if (!response) {
        mm_obj_dbg (port, "%s", error->message);
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
