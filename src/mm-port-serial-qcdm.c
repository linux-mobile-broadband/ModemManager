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

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-port-serial-qcdm.h"
#include "libqcdm/src/com.h"
#include "libqcdm/src/utils.h"
#include "libqcdm/src/errors.h"
#include "libqcdm/src/dm-commands.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMPortSerialQcdm, mm_port_serial_qcdm, MM_TYPE_PORT_SERIAL)

struct _MMPortSerialQcdmPrivate {
    GSList *unsolicited_msg_handlers;
};

/*****************************************************************************/

static gboolean
find_qcdm_start (GByteArray *response, gsize *start)
{
    guint i;
    gint  last = -1;

    /* Look for 3 bytes and a QCDM frame marker, ie enough data for a valid
     * frame.  There will usually be three cases here; (1) a QCDM frame
     * starting with data and terminated by 0x7E, and (2) a QCDM frame starting
     * with 0x7E and ending with 0x7E, and (3) a non-QCDM frame that still
     * uses HDLC framing (like Sierra CnS) that starts and ends with 0x7E.
     */
    for (i = 0; i < response->len; i++) {
        /* Marker found */
        if (response->data[i] == 0x7E) {
            /* If we didn't get an initial marker, count at least 3 bytes since
             * origin; if we did get an initial marker, count at least 3 bytes
             * since the marker.
             */
            if (((last == -1) && (i >= 3)) || ((last >= 0) && (i > (guint)(last + 3)))) {
                /* Got a full QCDM frame; 3 non-0x7E bytes and a terminator */
                if (start)
                    *start = last + 1;
                return TRUE;
            }

            /* Save position of the last QCDM frame marker */
            last = i;
        }
    }
    return FALSE;
}

static MMPortSerialResponseType
parse_qcdm (GByteArray *response,
            gboolean want_log,
            GByteArray **parsed_response,
            GError **error)
{
    gsize start = 0;
    gsize used = 0;
    gsize unescaped_len = 0;
    guint8 *unescaped_buffer;
    qcdmbool more = FALSE;

    /* Get the offset into the buffer of where the QCDM frame starts */
    if (!find_qcdm_start (response, &start)) {
        /* Discard the unparsable data right away, we do need a QCDM
         * start, and anything that comes before it is unknown data
         * that we'll never use. */
        return MM_PORT_SERIAL_RESPONSE_NONE;
    }

    /* If there is anything before the start marker, remove it */
    g_byte_array_remove_range (response, 0, start);
    if (response->len == 0)
        return MM_PORT_SERIAL_RESPONSE_NONE;

    /* Try to decapsulate the response into a buffer */
    unescaped_buffer = g_malloc (1024);
    if (!dm_decapsulate_buffer ((const char *)(response->data),
                                response->len,
                                (char *)unescaped_buffer,
                                1024,
                                &unescaped_len,
                                &used,
                                &more)) {
        /* Report an error right away. Not being able to decapsulate a QCDM
         * packet once we got message start marker likely means that this
         * data that we got is not a QCDM message. */
        g_set_error (error,
                     MM_SERIAL_ERROR,
                     MM_SERIAL_ERROR_PARSE_FAILED,
                     "Failed to unescape QCDM packet");
        g_free (unescaped_buffer);
        return MM_PORT_SERIAL_RESPONSE_ERROR;
    }

    if (more) {
        /* Need more data, we leave the original byte array untouched so that
         * we can retry later when more data arrives. */
        g_free (unescaped_buffer);
        return MM_PORT_SERIAL_RESPONSE_NONE;
    }

    if (want_log && unescaped_buffer[0] != DIAG_CMD_LOG) {
        /* If we only want log items and this isn't one, don't remove this
         * DM packet from the buffer.
         */
        g_free (unescaped_buffer);
        return MM_PORT_SERIAL_RESPONSE_NONE;
    }

    /* Successfully decapsulated the DM command. We'll build a new byte array
     * with the response, and leave the input buffer cleaned up. */
    g_assert (unescaped_len <= 1024);
    unescaped_buffer = g_realloc (unescaped_buffer, unescaped_len);
    *parsed_response = g_byte_array_new_take (unescaped_buffer, unescaped_len);

    /* Remove the data we used from the input buffer, leaving out any
     * additional data that may already been received (e.g. from the following
     * message). */
    g_byte_array_remove_range (response, 0, used);
    return MM_PORT_SERIAL_RESPONSE_BUFFER;
}

static MMPortSerialResponseType
parse_response (MMPortSerial *port,
                GByteArray *response,
                GByteArray **parsed_response,
                GError **error)
{
    return parse_qcdm (response, FALSE, parsed_response, error);
}

/*****************************************************************************/

GByteArray *
mm_port_serial_qcdm_command_finish (MMPortSerialQcdm *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
serial_command_ready (MMPortSerial *port,
                      GAsyncResult *res,
                      GTask *task)
{
    GByteArray *response;
    GError *error = NULL;

    response = mm_port_serial_command_finish (port, res, &error);
    if (!response)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, response, (GDestroyNotify)g_byte_array_unref);

    g_object_unref (task);
}

void
mm_port_serial_qcdm_command (MMPortSerialQcdm *self,
                             GByteArray *command,
                             guint32 timeout_seconds,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    GTask *task;

    g_return_if_fail (MM_IS_PORT_SERIAL_QCDM (self));
    g_return_if_fail (command != NULL);

    task = g_task_new (self, cancellable, callback, user_data);

    /* 'command' is expected to be already CRC-ed and escaped */
    mm_port_serial_command (MM_PORT_SERIAL (self),
                            command,
                            timeout_seconds,
                            FALSE, /* never cached */
                            FALSE, /* always queued last */
                            cancellable,
                            (GAsyncReadyCallback)serial_command_ready,
                            task);
}

static void
debug_log (MMPortSerial *self,
           const gchar  *prefix,
           const gchar  *buf,
           gsize         len)
{
    static GString *debug = NULL;
    const gchar    *s = buf;

    if (!debug)
        debug = g_string_sized_new (512);

    g_string_append (debug, prefix);

    while (len--)
        g_string_append_printf (debug, " %02x", (guint8) (*s++ & 0xFF));

    mm_obj_dbg (self, "%s", debug->str);
    g_string_truncate (debug, 0);
}

/*****************************************************************************/

typedef struct {
    guint log_code;
    MMPortSerialQcdmUnsolicitedMsgFn callback;
    gboolean enable;
    gpointer user_data;
    GDestroyNotify notify;
} MMQcdmUnsolicitedMsgHandler;

static gint
unsolicited_msg_handler_cmp (MMQcdmUnsolicitedMsgHandler *handler,
                             gpointer log_code)
{
    return handler->log_code - GPOINTER_TO_UINT (log_code);
}

void
mm_port_serial_qcdm_add_unsolicited_msg_handler (MMPortSerialQcdm *self,
                                                 guint log_code,
                                                 MMPortSerialQcdmUnsolicitedMsgFn callback,
                                                 gpointer user_data,
                                                 GDestroyNotify notify)
{
    GSList *existing;
    MMQcdmUnsolicitedMsgHandler *handler;

    g_return_if_fail (MM_IS_PORT_SERIAL_QCDM (self));
    g_return_if_fail (log_code > 0 && log_code <= G_MAXUINT16);

    existing = g_slist_find_custom (self->priv->unsolicited_msg_handlers,
                                    GUINT_TO_POINTER (log_code),
                                    (GCompareFunc)unsolicited_msg_handler_cmp);
    if (existing) {
        handler = existing->data;
        /* We OVERWRITE any existing one, so if any context data existing, free it */
        if (handler->notify)
            handler->notify (handler->user_data);
    } else {
        handler = g_slice_new (MMQcdmUnsolicitedMsgHandler);
        self->priv->unsolicited_msg_handlers = g_slist_append (self->priv->unsolicited_msg_handlers, handler);
        handler->log_code = log_code;
    }

    handler->callback = callback;
    handler->enable = TRUE;
    handler->user_data = user_data;
    handler->notify = notify;
}

void
mm_port_serial_qcdm_enable_unsolicited_msg_handler (MMPortSerialQcdm *self,
                                                    guint log_code,
                                                    gboolean enable)
{
    GSList *existing;
    MMQcdmUnsolicitedMsgHandler *handler;

    g_return_if_fail (MM_IS_PORT_SERIAL_QCDM (self));
    g_return_if_fail (log_code > 0 && log_code <= G_MAXUINT16);

    existing = g_slist_find_custom (self->priv->unsolicited_msg_handlers,
                                    GUINT_TO_POINTER (log_code),
                                    (GCompareFunc)unsolicited_msg_handler_cmp);
    if (existing) {
        handler = existing->data;
        handler->enable = enable;
    }
}

static void
parse_unsolicited (MMPortSerial *port, GByteArray *response)
{
    MMPortSerialQcdm *self = MM_PORT_SERIAL_QCDM (port);
    GByteArray *log_buffer = NULL;
    GSList *iter;

    if (parse_qcdm (response,
                    TRUE,
                    &log_buffer,
                    NULL) != MM_PORT_SERIAL_RESPONSE_BUFFER) {
        return;
    }

    /* These should be guaranteed by parse_qcdm() */
    g_return_if_fail (log_buffer);
    g_return_if_fail (log_buffer->len > 0);
    g_return_if_fail (log_buffer->data[0] == DIAG_CMD_LOG);

    if (log_buffer->len < sizeof (DMCmdLog))
        return;

    for (iter = self->priv->unsolicited_msg_handlers; iter; iter = iter->next) {
        MMQcdmUnsolicitedMsgHandler *handler = (MMQcdmUnsolicitedMsgHandler *) iter->data;
        DMCmdLog *log_cmd = (DMCmdLog *) log_buffer->data;

        if (!handler->enable)
            continue;
        if (handler->log_code != le16toh (log_cmd->log_code))
            continue;
        if (handler->callback)
            handler->callback (self, log_buffer, handler->user_data);
    }
}

/*****************************************************************************/

static gboolean
config_fd (MMPortSerial *port, int fd, GError **error)
{
    int err;

    err = qcdm_port_setup (fd);
    if (err != QCDM_SUCCESS) {
        g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_ERROR_OPEN_FAILED,
                     "Failed to open QCDM port: %d", err);
        return FALSE;
    }
    return TRUE;
}

/*****************************************************************************/

MMPortSerialQcdm *
mm_port_serial_qcdm_new (const char *name,
                         MMPortSubsys subsys)
{
    return MM_PORT_SERIAL_QCDM (g_object_new (MM_TYPE_PORT_SERIAL_QCDM,
                                              MM_PORT_DEVICE, name,
                                              MM_PORT_SUBSYS, subsys,
                                              MM_PORT_TYPE, MM_PORT_TYPE_QCDM,
                                              MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                                              NULL));
}

MMPortSerialQcdm *
mm_port_serial_qcdm_new_fd (int fd)
{
    MMPortSerialQcdm *port;
    char *name;

    name = g_strdup_printf ("port%d", fd);
    port = MM_PORT_SERIAL_QCDM (g_object_new (MM_TYPE_PORT_SERIAL_QCDM,
                                              MM_PORT_DEVICE, name,
                                              MM_PORT_SUBSYS, MM_PORT_SUBSYS_TTY,
                                              MM_PORT_TYPE, MM_PORT_TYPE_QCDM,
                                              MM_PORT_SERIAL_FD, fd,
                                              MM_PORT_SERIAL_SEND_DELAY, (guint64) 0,
                                              NULL));
    g_free (name);
    return port;
}

static void
mm_port_serial_qcdm_init (MMPortSerialQcdm *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT_SERIAL_QCDM, MMPortSerialQcdmPrivate);
}

static void
finalize (GObject *object)
{
    MMPortSerialQcdm *self = MM_PORT_SERIAL_QCDM (object);

    while (self->priv->unsolicited_msg_handlers) {
        MMQcdmUnsolicitedMsgHandler *handler = (MMQcdmUnsolicitedMsgHandler *) self->priv->unsolicited_msg_handlers->data;

        if (handler->notify)
            handler->notify (handler->user_data);

        g_slice_free (MMQcdmUnsolicitedMsgHandler, handler);
        self->priv->unsolicited_msg_handlers = g_slist_delete_link (self->priv->unsolicited_msg_handlers,
                                                                    self->priv->unsolicited_msg_handlers);
    }

    G_OBJECT_CLASS (mm_port_serial_qcdm_parent_class)->finalize (object);
}

static void
mm_port_serial_qcdm_class_init (MMPortSerialQcdmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMPortSerialClass *port_class = MM_PORT_SERIAL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortSerialQcdmPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
    port_class->parse_unsolicited = parse_unsolicited;
    port_class->parse_response = parse_response;
    port_class->config_fd = config_fd;
    port_class->debug_log = debug_log;
}
