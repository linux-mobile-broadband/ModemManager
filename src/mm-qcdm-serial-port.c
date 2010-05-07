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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mm-qcdm-serial-port.h"
#include "mm-errors.h"
#include "mm-options.h"
#include "libqcdm/src/com.h"
#include "libqcdm/src/utils.h"

G_DEFINE_TYPE (MMQcdmSerialPort, mm_qcdm_serial_port, MM_TYPE_SERIAL_PORT)

#define MM_QCDM_SERIAL_PORT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_QCDM_SERIAL_PORT, MMQcdmSerialPortPrivate))

typedef struct {
    gboolean foo;
} MMQcdmSerialPortPrivate;


/*****************************************************************************/

static gboolean
parse_response (MMSerialPort *port, GByteArray *response, GError **error)
{
    int i;

    /* Look for the QCDM packet termination character; if we found it, treat
     * the buffer as a qcdm command.
     */
    for (i = 0; i < response->len; i++) {
        if (response->data[i] == 0x7E)
            return TRUE;
    }

    /* Otherwise, need more data from the device */
    return FALSE;
}

static gsize
handle_response (MMSerialPort *port,
                 GByteArray *response,
                 GError *error,
                 GCallback callback,
                 gpointer callback_data)
{
    MMQcdmSerialResponseFn response_callback = (MMQcdmSerialResponseFn) callback;
    GByteArray *unescaped = NULL;
    GError *dm_error = NULL;
    gsize used = 0;

    /* Ignore empty frames */
    if (response->len > 0 && response->data[0] == 0x7E)
        return 1;

    if (!error) {
        gboolean more = FALSE, success;
        gsize unescaped_len = 0;

        /* FIXME: don't munge around with byte array internals */
        unescaped = g_byte_array_sized_new (1024);
        success = dm_decapsulate_buffer ((const char *) response->data,
                                         response->len,
                                         (char *) unescaped->data,
                                         1024,
                                         &unescaped_len,
                                         &used,
                                         &more);
        if (!success) {
            g_set_error_literal (&dm_error,
                                 MM_MODEM_ERROR, MM_MODEM_ERROR_GENERAL,
                                 "Failed to unescape QCDM packet.");
            g_byte_array_free (unescaped, TRUE);
            unescaped = NULL;
        } else if (more) {
            /* Need more data; we shouldn't have gotten here since the parse
             * function checks for the end-of-frame marker, but whatever.
             */
            return 0;
        } else {
            /* Successfully decapsulated the DM command */
            unescaped->len = (guint) unescaped_len;
        }
    }

    response_callback (MM_QCDM_SERIAL_PORT (port),
                       unescaped,
                       dm_error ? dm_error : error,
                       callback_data);

    if (unescaped)
        g_byte_array_free (unescaped, TRUE);

    return used;
}

/*****************************************************************************/

void
mm_qcdm_serial_port_queue_command (MMQcdmSerialPort *self,
                                   GByteArray *command,
                                   guint32 timeout_seconds,
                                   MMQcdmSerialResponseFn callback,
                                   gpointer user_data)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_QCDM_SERIAL_PORT (self));
    g_return_if_fail (command != NULL);

    /* 'command' is expected to be already CRC-ed and escaped */
    mm_serial_port_queue_command (MM_SERIAL_PORT (self),
                                  command,
                                  TRUE,
                                  timeout_seconds,
                                  (MMSerialResponseFn) callback,
                                  user_data);
}

void
mm_qcdm_serial_port_queue_command_cached (MMQcdmSerialPort *self,
                                          GByteArray *command,
                                          guint32 timeout_seconds,
                                          MMQcdmSerialResponseFn callback,
                                          gpointer user_data)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_QCDM_SERIAL_PORT (self));
    g_return_if_fail (command != NULL);

    /* 'command' is expected to be already CRC-ed and escaped */
    mm_serial_port_queue_command_cached (MM_SERIAL_PORT (self),
                                         command,
                                         TRUE,
                                         timeout_seconds,
                                         (MMSerialResponseFn) callback,
                                         user_data);
}

static void
debug_log (MMSerialPort *port, const char *prefix, const char *buf, gsize len)
{
    static GString *debug = NULL;
    const char *s = buf;
    GTimeVal tv;

    if (!debug)
        debug = g_string_sized_new (512);

    g_string_append (debug, prefix);

    while (len--)
        g_string_append_printf (debug, " %02x", (guint8) (*s++ & 0xFF));

    g_get_current_time (&tv);
    g_debug ("<%ld.%ld> (%s): %s",
             tv.tv_sec,
             tv.tv_usec,
             mm_port_get_device (MM_PORT (port)),
             debug->str);
    g_string_truncate (debug, 0);
}

/*****************************************************************************/

static gboolean
config_fd (MMSerialPort *port, int fd, GError **error)
{
    return qcdm_port_setup (fd, error);
}

/*****************************************************************************/

MMQcdmSerialPort *
mm_qcdm_serial_port_new (const char *name, MMPortType ptype)
{
    return MM_QCDM_SERIAL_PORT (g_object_new (MM_TYPE_QCDM_SERIAL_PORT,
                                              MM_PORT_DEVICE, name,
                                              MM_PORT_SUBSYS, MM_PORT_SUBSYS_TTY,
                                              MM_PORT_TYPE, ptype,
                                              NULL));
}

static void
mm_qcdm_serial_port_init (MMQcdmSerialPort *self)
{
}

static void
finalize (GObject *object)
{
    G_OBJECT_CLASS (mm_qcdm_serial_port_parent_class)->finalize (object);
}

static void
mm_qcdm_serial_port_class_init (MMQcdmSerialPortClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMSerialPortClass *port_class = MM_SERIAL_PORT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMQcdmSerialPortPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;

    port_class->parse_response = parse_response;
    port_class->handle_response = handle_response;
    port_class->config_fd = config_fd;
    port_class->debug_log = debug_log;
}
