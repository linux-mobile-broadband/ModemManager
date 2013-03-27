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

/* AT port flags; for example consider a device with two AT ports (ACM0 and ACM1)
 * which could have the following layouts:
 *
 * ACM0(PRIMARY | PPP), ACM1(SECONDARY): port 0 is used for command and status
 *    and for PPP data; while connected port 1 is used for command and status
 * ACM0(PPP), ACM1(PRIMARY): port 1 is always used for command and status, and
 *    only when connecting is port 0 opened for dialing (ATD) and PPP
 */
typedef enum { /*< underscore_name=mm_at_port_flag >*/
    MM_AT_PORT_FLAG_NONE        = 0,
    /* This port is preferred for command and status */
    MM_AT_PORT_FLAG_PRIMARY     = 1 << 0,
    /* Use port for command and status if the primary port is connected */
    MM_AT_PORT_FLAG_SECONDARY   = 1 << 1,
    /* This port should be used for PPP */
    MM_AT_PORT_FLAG_PPP         = 1 << 2,
    /* This port should be used for GPS control */
    MM_AT_PORT_FLAG_GPS_CONTROL = 1 << 3,
} MMAtPortFlag;

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

#define MM_AT_SERIAL_PORT_REMOVE_ECHO           "remove-echo"
#define MM_AT_SERIAL_PORT_INIT_SEQUENCE_ENABLED "init-sequence-enabled"
#define MM_AT_SERIAL_PORT_INIT_SEQUENCE         "init-sequence"

#define MM_AT_SERIAL_PORT_SEND_LF "send-lf"

struct _MMAtSerialPort {
    MMSerialPort parent;
};

struct _MMAtSerialPortClass {
    MMSerialPortClass parent;
};

GType mm_at_serial_port_get_type (void);

MMAtSerialPort *mm_at_serial_port_new (const char *name);

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
                                              gboolean is_raw,
                                              GCancellable *cancellable,
                                              MMAtSerialResponseFn callback,
                                              gpointer user_data);

void     mm_at_serial_port_queue_command_cached (MMAtSerialPort *self,
                                                 const char *command,
                                                 guint32 timeout_seconds,
                                                 gboolean is_raw,
                                                 GCancellable *cancellable,
                                                 MMAtSerialResponseFn callback,
                                                 gpointer user_data);

/*
 * Convert a string into a quoted and escaped string. Returns a new
 * allocated string. Follows ITU V.250 5.4.2.2 "String constants".
 */
gchar   *mm_at_serial_port_quote_string (const char *string);

/* Just for unit tests */
void mm_at_serial_port_remove_echo (GByteArray *response);

void     mm_at_serial_port_set_flags (MMAtSerialPort *self,
                                      MMAtPortFlag flags);

MMAtPortFlag mm_at_serial_port_get_flags (MMAtSerialPort *self);

/* Tell the port to run its init sequence, if any, right away */
void mm_at_serial_port_run_init_sequence (MMAtSerialPort *self);

#endif /* MM_AT_SERIAL_PORT_H */
