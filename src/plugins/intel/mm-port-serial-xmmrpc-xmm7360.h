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

#ifndef MM_PORT_SERIAL_XMMRPC_XMM7360_H
#define MM_PORT_SERIAL_XMMRPC_XMM7360_H

#include <glib.h>
#include <glib-object.h>

#include "mm-port-serial.h"
#include "mm-xmmrpc-xmm7360-protocol.h"

#define MM_TYPE_PORT_SERIAL_XMMRPC_XMM7360            (mm_port_serial_xmmrpc_xmm7360_get_type ())
#define MM_PORT_SERIAL_XMMRPC_XMM7360(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_SERIAL_XMMRPC_XMM7360, MMPortSerialXmmrpcXmm7360))
#define MM_PORT_SERIAL_XMMRPC_XMM7360_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_SERIAL_XMMRPC_XMM7360, MMPortSerialXmmrpcXmm7360Class))
#define MM_IS_PORT_SERIAL_XMMRPC_XMM7360(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_SERIAL_XMMRPC_XMM7360))
#define MM_IS_PORT_SERIAL_XMMRPC_XMM7360_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_SERIAL_XMMRPC_XMM7360))
#define MM_PORT_SERIAL_XMMRPC_XMM7360_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_SERIAL_XMMRPC_XMM7360, MMPortSerialXmmrpcXmm7360Class))

typedef struct _MMPortSerialXmmrpcXmm7360 MMPortSerialXmmrpcXmm7360;
typedef struct _MMPortSerialXmmrpcXmm7360Class MMPortSerialXmmrpcXmm7360Class;
typedef struct _MMPortSerialXmmrpcXmm7360Private MMPortSerialXmmrpcXmm7360Private;

typedef gboolean (*MMPortSerialXmmrpcXmm7360UnsolicitedMsgFn) (MMPortSerialXmmrpcXmm7360 *port,
                                                               Xmm7360RpcResponse *response,
                                                               gpointer user_data);

struct _MMPortSerialXmmrpcXmm7360 {
    MMPortSerial parent;
    MMPortSerialXmmrpcXmm7360Private *priv;
};

struct _MMPortSerialXmmrpcXmm7360Class {
    MMPortSerialClass parent;
};

GType mm_port_serial_xmmrpc_xmm7360_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortSerialXmmrpcXmm7360, g_object_unref)

MMPortSerialXmmrpcXmm7360 *mm_port_serial_xmmrpc_xmm7360_new (const char *name);

void mm_port_serial_xmmrpc_xmm7360_command (MMPortSerialXmmrpcXmm7360 *self,
                                            Xmm7360RpcCallId callid,
                                            gboolean is_async,
                                            GByteArray *body,
                                            guint32 timeout_seconds,
                                            gboolean allow_cached,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);

Xmm7360RpcResponse *mm_port_serial_xmmrpc_xmm7360_command_finish (MMPortSerialXmmrpcXmm7360  *self,
                                                                  GAsyncResult               *res,
                                                                  GError                    **error);

guint mm_port_serial_xmmrpc_xmm7360_add_unsolicited_msg_handler (MMPortSerialXmmrpcXmm7360 *self,
                                                                   MMPortSerialXmmrpcXmm7360UnsolicitedMsgFn callback,
                                                                   gpointer user_data,
                                                                   GDestroyNotify notify);

void mm_port_serial_xmmrpc_xmm7360_enable_unsolicited_msg_handler (MMPortSerialXmmrpcXmm7360 *self,
                                                                   guint handler_id,
                                                                   gboolean enable);

#endif /* MM_PORT_SERIAL_XMMRPC_XMM7360_H */
