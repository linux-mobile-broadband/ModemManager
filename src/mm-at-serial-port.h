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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#ifndef MM_AT_SERIAL_PORT_H
#define MM_AT_SERIAL_PORT_H

#include <glib.h>
#include <glib-object.h>

#include "mm-serial-port.h"

#define MM_TYPE_AT_SERIAL_PORT            (mm_at_serial_port_get_type ())
#define MM_AT_SERIAL_PORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_AT_SERIAL_PORT, MMAtSerialPort))
#define MM_AT_SERIAL_PORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_AT_SERIAL_PORT, MMAtSerialPortClass))
#define MM_IS_AT_SERIAL_PORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_AT_SERIAL_PORT))
#define MM_IS_AT_SERIAL_PORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_AT_SERIAL_PORT))
#define MM_AT_SERIAL_PORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_AT_SERIAL_PORT, MMAtSerialPortClass))

typedef struct _MMAtSerialPort MMAtSerialPort;
typedef struct _MMAtSerialPortClass MMAtSerialPortClass;

typedef gboolean (*MMAtSerialResponseParserFn) (gpointer user_data,
                                                GString *response,
                                                GError **error);

typedef void (*MMAtSerialUnsolicitedMsgFn) (MMAtSerialPort *port,
                                            GMatchInfo *match_info,
                                            gpointer user_data);

typedef void (*MMAtSerialResponseFn)     (MMAtSerialPort *port,
                                          GString *response,
                                          GError *error,
                                          gpointer user_data);

#define MM_AT_SERIAL_PORT_REMOVE_ECHO "remove-echo"

struct _MMAtSerialPort {
    MMSerialPort parent;
};

struct _MMAtSerialPortClass {
    MMSerialPortClass parent;
};

GType mm_at_serial_port_get_type (void);

MMAtSerialPort *mm_at_serial_port_new (const char *name, MMPortType ptype);

void     mm_at_serial_port_add_unsolicited_msg_handler (MMAtSerialPort *self,
                                                        GRegex *regex,
                                                        MMAtSerialUnsolicitedMsgFn callback,
                                                        gpointer user_data,
                                                        GDestroyNotify notify);

void     mm_at_serial_port_set_response_parser (MMAtSerialPort *self,
                                                MMAtSerialResponseParserFn fn,
                                                gpointer user_data,
                                                GDestroyNotify notify);

void     mm_at_serial_port_queue_command     (MMAtSerialPort *self,
                                              const char *command,
                                              guint32 timeout_seconds,
                                              MMAtSerialResponseFn callback,
                                              gpointer user_data);

void     mm_at_serial_port_queue_command_cached (MMAtSerialPort *self,
                                                 const char *command,
                                                 guint32 timeout_seconds,
                                                 MMAtSerialResponseFn callback,
                                                 gpointer user_data);

/* Just for unit tests */
void mm_at_serial_port_remove_echo (GByteArray *response);

#endif /* MM_AT_SERIAL_PORT_H */
