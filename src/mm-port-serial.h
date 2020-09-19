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

#ifndef MM_PORT_SERIAL_H
#define MM_PORT_SERIAL_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "mm-modem-helpers.h"
#include "mm-port.h"

#define MM_TYPE_PORT_SERIAL            (mm_port_serial_get_type ())
#define MM_PORT_SERIAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_SERIAL, MMPortSerial))
#define MM_PORT_SERIAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_SERIAL, MMPortSerialClass))
#define MM_IS_PORT_SERIAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_SERIAL))
#define MM_IS_PORT_SERIAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_SERIAL))
#define MM_PORT_SERIAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_SERIAL, MMPortSerialClass))

#define MM_PORT_SERIAL_BAUD         "baud"
#define MM_PORT_SERIAL_BITS         "bits"
#define MM_PORT_SERIAL_PARITY       "parity"
#define MM_PORT_SERIAL_STOPBITS     "stopbits"
#define MM_PORT_SERIAL_FLOW_CONTROL "flowcontrol"
#define MM_PORT_SERIAL_SEND_DELAY   "send-delay"
#define MM_PORT_SERIAL_FD           "fd" /* Construct-only */
#define MM_PORT_SERIAL_SPEW_CONTROL "spew-control" /* Construct-only */
#define MM_PORT_SERIAL_FLASH_OK     "flash-ok" /* Construct-only */

typedef enum {
    MM_PORT_SERIAL_RESPONSE_NONE,
    MM_PORT_SERIAL_RESPONSE_BUFFER,
    MM_PORT_SERIAL_RESPONSE_ERROR,
} MMPortSerialResponseType;

typedef struct _MMPortSerial MMPortSerial;
typedef struct _MMPortSerialClass MMPortSerialClass;
typedef struct _MMPortSerialPrivate MMPortSerialPrivate;

struct _MMPortSerial {
    MMPort parent;
    MMPortSerialPrivate *priv;
};

struct _MMPortSerialClass {
    MMPortClass parent;

    /* Called for subclasses to parse unsolicited responses.  If any recognized
     * unsolicited response is found, it should be removed from the 'response'
     * byte array before returning.
     */
    void     (*parse_unsolicited) (MMPortSerial *self, GByteArray *response);

    /*
     * Called to parse the device's response to a command or determine if the
     * response was an error response.
     *
     * If the response indicates an error, @MM_PORT_SERIAL_RESPONSE_ERROR will
     * be returned and an appropriate GError set in @error.
     *
     * If the response indicates a valid response, @MM_PORT_SERIAL_RESPONSE_BUFFER
     * will be returned, and a newly allocated GByteArray set in @parsed_response.
     *
     * If there is no response, @MM_PORT_SERIAL_RESPONSE_NONE will be returned,
     * and neither @error nor @parsed_response will be set.
     *
     * The implementation is allowed to cleanup the @response byte array, e.g. to
     * just remove 1 single response if more than one found.
     */
    MMPortSerialResponseType (*parse_response) (MMPortSerial *self,
                                                GByteArray *response,
                                                GByteArray **parsed_response,
                                                GError **error);

    /* Called to configure the serial port fd after it's opened.  On error, should
     * return FALSE and set 'error' as appropriate.
     */
    gboolean (*config_fd)         (MMPortSerial *self, int fd, GError **error);

    /* Called to configure the serial port after it's opened. Errors, if any,
     * should get ignored. */
    void (*config)                (MMPortSerial *self);

    void (*debug_log)             (MMPortSerial *self,
                                   const gchar  *prefix,
                                   const gchar  *buf,
                                   gsize         len);

    /* Signals */
    void (*buffer_full)           (MMPortSerial *port, const GByteArray *buffer);
    void (*timed_out)             (MMPortSerial *port, guint n_consecutive_replies);
    void (*forced_close)          (MMPortSerial *port);
};

GType mm_port_serial_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortSerial, g_object_unref)

MMPortSerial *mm_port_serial_new (const char *name, MMPortType ptype);

/* Keep in mind that port open/close is refcounted, so ensure that
 * open/close calls are properly balanced.
 */

gboolean mm_port_serial_is_open           (MMPortSerial *self);

gboolean mm_port_serial_open              (MMPortSerial *self,
                                           GError  **error);

void     mm_port_serial_close             (MMPortSerial *self);

/* Reopen(), async */
void     mm_port_serial_reopen            (MMPortSerial *self,
                                           guint32 reopen_time,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_port_serial_reopen_finish     (MMPortSerial *port,
                                           GAsyncResult *res,
                                           GError **error);

void     mm_port_serial_flash             (MMPortSerial *self,
                                           guint32 flash_time,
                                           gboolean ignore_errors,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_port_serial_flash_finish      (MMPortSerial *self,
                                           GAsyncResult *res,
                                           GError **error);
void     mm_port_serial_flash_cancel      (MMPortSerial *self);

void        mm_port_serial_command        (MMPortSerial *self,
                                           GByteArray *command,
                                           guint32 timeout_seconds,
                                           gboolean allow_cached,
                                           gboolean run_next,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
GByteArray *mm_port_serial_command_finish (MMPortSerial *self,
                                           GAsyncResult *res,
                                           GError **error);

gboolean mm_port_serial_set_flow_control (MMPortSerial   *self,
                                          MMFlowControl   flow_control,
                                          GError        **error);

MMFlowControl mm_port_serial_get_flow_control (MMPortSerial *self);
#endif /* MM_PORT_SERIAL_H */
