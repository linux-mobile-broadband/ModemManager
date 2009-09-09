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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_SERIAL_PORT_H
#define MM_SERIAL_PORT_H

#include <glib.h>
#include <glib/gtypes.h>
#include <glib-object.h>

#include "mm-port.h"

#define MM_TYPE_SERIAL_PORT            (mm_serial_port_get_type ())
#define MM_SERIAL_PORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SERIAL_PORT, MMSerialPort))
#define MM_SERIAL_PORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SERIAL_PORT, MMSerialPortClass))
#define MM_IS_SERIAL_PORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SERIAL_PORT))
#define MM_IS_SERIAL_PORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SERIAL_PORT))
#define MM_SERIAL_PORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SERIAL_PORT, MMSerialPortClass))

#define MM_SERIAL_PORT_BAUD       "baud"
#define MM_SERIAL_PORT_BITS       "bits"
#define MM_SERIAL_PORT_PARITY     "parity"
#define MM_SERIAL_PORT_STOPBITS   "stopbits"
#define MM_SERIAL_PORT_SEND_DELAY "send-delay"

typedef struct _MMSerialPort MMSerialPort;
typedef struct _MMSerialPortClass MMSerialPortClass;

typedef gboolean (*MMSerialResponseParserFn) (gpointer user_data,
                                              GString *response,
                                              GError **error);

typedef void (*MMSerialUnsolicitedMsgFn) (MMSerialPort *port,
                                          GMatchInfo *match_info,
                                          gpointer user_data);

typedef void (*MMSerialResponseFn)     (MMSerialPort *port,
                                        GString *response,
                                        GError *error,
                                        gpointer user_data);

typedef void (*MMSerialFlashFn)        (MMSerialPort *port,
                                        GError *error,
                                        gpointer user_data);

struct _MMSerialPort {
    MMPort parent;
};

struct _MMSerialPortClass {
    MMPortClass parent;
};

GType mm_serial_port_get_type (void);

MMSerialPort *mm_serial_port_new (const char *name, MMPortType ptype);

void     mm_serial_port_add_unsolicited_msg_handler (MMSerialPort *self,
                                                     GRegex *regex,
                                                     MMSerialUnsolicitedMsgFn callback,
                                                     gpointer user_data,
                                                     GDestroyNotify notify);

void     mm_serial_port_set_response_parser (MMSerialPort *self,
                                             MMSerialResponseParserFn fn,
                                             gpointer user_data,
                                             GDestroyNotify notify);

gboolean mm_serial_port_open              (MMSerialPort *self,
                                           GError  **error);

void     mm_serial_port_close             (MMSerialPort *self);
void     mm_serial_port_queue_command     (MMSerialPort *self,
                                           const char *command,
                                           guint32 timeout_seconds,
                                           MMSerialResponseFn callback,
                                           gpointer user_data);

void     mm_serial_port_queue_command_cached (MMSerialPort *self,
                                              const char *command,
                                              guint32 timeout_seconds,
                                              MMSerialResponseFn callback,
                                              gpointer user_data);

gboolean mm_serial_port_flash             (MMSerialPort *self,
                                           guint32 flash_time,
                                           MMSerialFlashFn callback,
                                           gpointer user_data);
void     mm_serial_port_flash_cancel      (MMSerialPort *self);

#endif /* MM_SERIAL_PORT_H */

