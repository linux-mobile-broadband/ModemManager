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

#ifndef MM_PORT_SERIAL_AT_H
#define MM_PORT_SERIAL_AT_H

#include <glib.h>
#include <glib-object.h>

#include "mm-port-serial.h"

#define MM_TYPE_PORT_SERIAL_AT            (mm_port_serial_at_get_type ())
#define MM_PORT_SERIAL_AT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_SERIAL_AT, MMPortSerialAt))
#define MM_PORT_SERIAL_AT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_SERIAL_AT, MMPortSerialAtClass))
#define MM_IS_PORT_SERIAL_AT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_SERIAL_AT))
#define MM_IS_PORT_SERIAL_AT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_SERIAL_AT))
#define MM_PORT_SERIAL_AT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_SERIAL_AT, MMPortSerialAtClass))

typedef struct _MMPortSerialAt MMPortSerialAt;
typedef struct _MMPortSerialAtClass MMPortSerialAtClass;
typedef struct _MMPortSerialAtPrivate MMPortSerialAtPrivate;

/* AT port flags; for example consider a device with two AT ports (ACM0 and ACM1)
 * which could have the following layouts:
 *
 * ACM0(PRIMARY | PPP), ACM1(SECONDARY): port 0 is used for command and status
 *    and for PPP data; while connected port 1 is used for command and status
 * ACM0(PPP), ACM1(PRIMARY): port 1 is always used for command and status, and
 *    only when connecting is port 0 opened for dialing (ATD) and PPP
 */
typedef enum { /*< underscore_name=mm_port_serial_at_flag >*/
    MM_PORT_SERIAL_AT_FLAG_NONE            = 0,
    /* This port is preferred for command and status */
    MM_PORT_SERIAL_AT_FLAG_PRIMARY         = 1 << 0,
    /* Use port for command and status if the primary port is connected */
    MM_PORT_SERIAL_AT_FLAG_SECONDARY       = 1 << 1,
    /* This port should be used for PPP */
    MM_PORT_SERIAL_AT_FLAG_PPP             = 1 << 2,
    /* This port should be used for GPS control */
    MM_PORT_SERIAL_AT_FLAG_GPS_CONTROL     = 1 << 3,
    /* Helper flag to allow plugins specify that generic tags shouldn't be
     * applied */
    MM_PORT_SERIAL_AT_FLAG_NONE_NO_GENERIC = 1 << 4,
} MMPortSerialAtFlag;

typedef gboolean (*MMPortSerialAtResponseParserFn) (gpointer   user_data,
                                                    GString   *response,
                                                    gpointer   log_object,
                                                    GError   **error);

typedef void (*MMPortSerialAtUnsolicitedMsgFn) (MMPortSerialAt *port,
                                                GMatchInfo *match_info,
                                                gpointer user_data);

#define MM_PORT_SERIAL_AT_REMOVE_ECHO           "remove-echo"
#define MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED "init-sequence-enabled"
#define MM_PORT_SERIAL_AT_INIT_SEQUENCE         "init-sequence"
#define MM_PORT_SERIAL_AT_SEND_LF               "send-lf"

struct _MMPortSerialAt {
    MMPortSerial parent;
    MMPortSerialAtPrivate *priv;
};

struct _MMPortSerialAtClass {
    MMPortSerialClass parent;
};

GType mm_port_serial_at_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortSerialAt, g_object_unref)

MMPortSerialAt *mm_port_serial_at_new (const char *name,
                                       MMPortSubsys subsys);

void     mm_port_serial_at_add_unsolicited_msg_handler (MMPortSerialAt *self,
                                                        GRegex *regex,
                                                        MMPortSerialAtUnsolicitedMsgFn callback,
                                                        gpointer user_data,
                                                        GDestroyNotify notify);

void     mm_port_serial_at_enable_unsolicited_msg_handler (MMPortSerialAt *self,
                                                           GRegex *regex,
                                                           gboolean enable);

void     mm_port_serial_at_set_response_parser (MMPortSerialAt *self,
                                                MMPortSerialAtResponseParserFn fn,
                                                gpointer user_data,
                                                GDestroyNotify notify);

void         mm_port_serial_at_command        (MMPortSerialAt *self,
                                               const char *command,
                                               guint32 timeout_seconds,
                                               gboolean is_raw,
                                               gboolean allow_cached,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
const gchar *mm_port_serial_at_command_finish (MMPortSerialAt *self,
                                               GAsyncResult *res,
                                               GError **error);

/*
 * Convert a string into a quoted and escaped string. Returns a new
 * allocated string. Follows ITU V.250 5.4.2.2 "String constants".
 */
gchar   *mm_port_serial_at_quote_string (const char *string);

/* Just for unit tests */
void     mm_port_serial_at_remove_echo (GByteArray *response);

void     mm_port_serial_at_set_flags (MMPortSerialAt *self,
                                      MMPortSerialAtFlag flags);

MMPortSerialAtFlag mm_port_serial_at_get_flags (MMPortSerialAt *self);

/* Tell the port to run its init sequence, if any, right away */
void mm_port_serial_at_run_init_sequence (MMPortSerialAt *self);

#endif /* MM_PORT_SERIAL_AT_H */
