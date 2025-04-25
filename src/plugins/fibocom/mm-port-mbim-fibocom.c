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
 * Copyright (C) 2024 Google, Inc.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-serial-parsers.h"
#include "mm-iface-port-at.h"
#include "mm-port-mbim-fibocom.h"
#include "mm-log-object.h"

static void iface_port_at_init (MMIfacePortAtInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMPortMbimFibocom, mm_port_mbim_fibocom, MM_TYPE_PORT_MBIM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_PORT_AT, iface_port_at_init))

typedef enum {
    FEATURE_SUPPORT_UNKNOWN,
    FEATURE_NOT_SUPPORTED,
    FEATURE_SUPPORTED
} FeatureSupport;

struct _MMPortMbimFibocomPrivate {
    FeatureSupport  at_over_mbim;
    gpointer        parser;
};

/*****************************************************************************/

static gboolean
iface_port_at_check_support (MMIfacePortAt  *_self,
                             gboolean       *out_supported,
                             GError        **error)
{
    MMPortMbimFibocom *self = MM_PORT_MBIM_FIBOCOM (_self);

    g_assert (out_supported);

    if (self->priv->at_over_mbim == FEATURE_SUPPORT_UNKNOWN) {
        /* First time check */
        if (!mm_port_mbim_is_open (MM_PORT_MBIM (self))) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                         "Couldn't check AT support: MBIM port is closed");
            return FALSE;
        }

        if (!mm_port_mbim_supports_command (MM_PORT_MBIM (self), MBIM_SERVICE_FIBOCOM, MBIM_CID_FIBOCOM_AT_COMMAND)) {
            mm_obj_msg (self, "MBIM device is not AT capable");
            self->priv->at_over_mbim = FEATURE_NOT_SUPPORTED;
        } else if (mm_kernel_device_get_property_as_boolean (mm_port_peek_kernel_device (MM_PORT (self)),
                                                             "ID_MM_FIBOCOM_AT_OVER_MBIM_DISABLED")) {
            mm_obj_msg (self, "MBIM device is AT capable but support is disabled");
            self->priv->at_over_mbim = FEATURE_NOT_SUPPORTED;
        } else {
            mm_obj_msg (self, "MBIM device is AT capable");
            self->priv->at_over_mbim = FEATURE_SUPPORTED;
        }
    }

    *out_supported = (self->priv->at_over_mbim == FEATURE_SUPPORTED);
    return TRUE;
}

/*****************************************************************************/
/* AT command */

static gchar *
iface_port_at_command_finish (MMIfacePortAt  *self,
                              GAsyncResult   *res,
                              GError        **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
debug_log (MMPortMbimFibocom *self,
           const gchar       *prefix,
           const guint8      *buf,
           gsize              len)
{
    g_autoptr(GString)  debug = NULL;
    const guint8       *s;

    debug = g_string_new (prefix);
    g_string_append (debug, " '");

    s = buf;
    while (len--) {
        if (g_ascii_isprint ((gchar)*s))
            g_string_append_c (debug, (gchar)*s);
        else if (*s == '\r')
            g_string_append (debug, "<CR>");
        else if (*s == '\n')
            g_string_append (debug, "<LF>");
        else
            g_string_append_printf (debug, "\\%u", (guint8) (*s & 0xFF));
        s++;
    }

    g_string_append_c (debug, '\'');
    mm_obj_dbg (self, "%s", debug->str);
}

static void
at_command_ready (MbimDevice   *device,
                  GAsyncResult *res,
                  GTask        *task)
{
    GError                 *error = NULL;
    MMPortMbimFibocom      *self;
    guint32                 ret_size = 0;
    const guint8           *ret_str  = NULL;
    g_autoptr(MbimMessage)  response = NULL;
    GString                *string;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response ||
        !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) ||
        !mbim_message_fibocom_at_command_response_parse (
            response,
            &ret_size,
            &ret_str,
            &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    debug_log (self, "<--", ret_str, ret_size);

    if (G_UNLIKELY (!self->priv->parser))
        self->priv->parser = mm_serial_parser_v1_new ();

    /* Prepare the GString to pass to the parser. We add an explicit leading CRLF
     * sequence only because it is what the parser expects, so that we don't need
     * to do major changes to handle that case. */
    string = g_string_sized_new (ret_size + 3);
    g_string_append (string, "\r\n");
    g_string_append_len (string, (const gchar *) ret_str, ret_size);

    if (!mm_serial_parser_v1_parse (self->priv->parser, string, self, &error)) {
        if (error)
            g_task_return_error (task, error);
        else
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Incomplete response");
        g_string_free (string, TRUE);
        g_object_unref (task);
        return;
    }

    g_task_return_pointer (task, g_string_free (string, FALSE), g_free);
    g_object_unref (task);
}

static GByteArray *
at_command_to_byte_array (const gchar *command,
                          gboolean     is_raw)
{
    GByteArray *buf;
    int         cmdlen;

    cmdlen = strlen (command);
    buf = g_byte_array_sized_new (cmdlen + 4);

    if (!is_raw) {
        /* Make sure there's an AT in the front */
        if (!g_str_has_prefix (command, "AT"))
            g_byte_array_append (buf, (const guint8 *) "AT", 2);
    }

    g_byte_array_append (buf, (const guint8 *) command, cmdlen);

    if (!is_raw) {
        /* Make sure there's a trailing carriage return */
        if ((cmdlen == 0) ||
            (command[cmdlen - 1] != '\r' && (cmdlen == 1 || command[cmdlen - 2] != '\r')))
             g_byte_array_append (buf, (const guint8 *) "\r", 1);
        /* Make sure there's a trailing line-feed */
        if ((cmdlen == 0) ||
            (command[cmdlen - 1] != '\n' && (cmdlen == 1 || command[cmdlen - 2] != '\n')))
            g_byte_array_append (buf, (const guint8 *) "\n", 1);
    }

    return buf;
}

static void
iface_port_at_command (MMIfacePortAt        *self,
                       const gchar          *command,
                       guint32               timeout_seconds,
                       gboolean              is_raw,
                       gboolean              allow_cached, /* ignored */
                       GCancellable         *cancellable,
                       GAsyncReadyCallback   callback,
                       gpointer              user_data)
{
    g_autoptr(MbimMessage)  request = NULL;
    g_autoptr(GByteArray)   buffer = NULL;
    GTask                  *task;

    task = g_task_new (self, cancellable, callback, user_data);

    buffer = at_command_to_byte_array (command, is_raw);

    debug_log (MM_PORT_MBIM_FIBOCOM (self), "-->", buffer->data, buffer->len);

    request = mbim_message_fibocom_at_command_set_new (buffer->len, (const guint8 *)buffer->data, NULL);
    mbim_device_command (mm_port_mbim_peek_device (MM_PORT_MBIM (self)),
                         request,
                         timeout_seconds,
                         cancellable,
                         (GAsyncReadyCallback)at_command_ready,
                         task);
}

/*****************************************************************************/

MMPortMbimFibocom *
mm_port_mbim_fibocom_new (const gchar  *name,
                          MMPortSubsys  subsys)
{
    return MM_PORT_MBIM_FIBOCOM (g_object_new (MM_TYPE_PORT_MBIM_FIBOCOM,
                                               MM_PORT_DEVICE, name,
                                               MM_PORT_SUBSYS, subsys,
                                               MM_PORT_GROUP, MM_PORT_GROUP_USED,
                                               MM_PORT_TYPE, MM_PORT_TYPE_MBIM,
                                               NULL));
}

static void
mm_port_mbim_fibocom_init (MMPortMbimFibocom *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT_MBIM_FIBOCOM, MMPortMbimFibocomPrivate);
    self->priv->at_over_mbim = FEATURE_SUPPORT_UNKNOWN;
}

static void
finalize (GObject *object)
{
    MMPortMbimFibocom *self = MM_PORT_MBIM_FIBOCOM (object);

    if (self->priv->parser)
        mm_serial_parser_v1_destroy (self->priv->parser);

    G_OBJECT_CLASS (mm_port_mbim_fibocom_parent_class)->finalize (object);
}

static void
iface_port_at_init (MMIfacePortAtInterface *iface)
{
    iface->check_support = iface_port_at_check_support;
    iface->command = iface_port_at_command;
    iface->command_finish = iface_port_at_command_finish;
}

static void
mm_port_mbim_fibocom_class_init (MMPortMbimFibocomClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortMbimFibocomPrivate));

    object_class->finalize = finalize;
}
