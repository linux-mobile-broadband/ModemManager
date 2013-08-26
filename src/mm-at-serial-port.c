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

#define _GNU_SOURCE  /* for strcasestr() */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mm-at-serial-port.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMAtSerialPort, mm_at_serial_port, MM_TYPE_SERIAL_PORT)

#define MM_AT_SERIAL_PORT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_AT_SERIAL_PORT, MMAtSerialPortPrivate))

enum {
    PROP_0,
    PROP_REMOVE_ECHO,
    PROP_INIT_SEQUENCE_ENABLED,
    PROP_INIT_SEQUENCE,
    PROP_SEND_LF,
    LAST_PROP
};

typedef struct {
    /* Response parser data */
    MMAtSerialResponseParserFn response_parser_fn;
    gpointer response_parser_user_data;
    GDestroyNotify response_parser_notify;

    GSList *unsolicited_msg_handlers;

    MMAtPortFlag flags;

    /* Properties */
    gboolean remove_echo;
    guint init_sequence_enabled;
    gchar **init_sequence;
    gboolean send_lf;
} MMAtSerialPortPrivate;

/*****************************************************************************/

gchar *
mm_at_serial_port_quote_string (const char *string)
{
    int len, i;
    gchar *quoted, *pos;

    if (string == NULL)
        len = 0;
    else
        len = strlen (string);
    quoted = g_malloc (3 + 3 * len); /* worst case */

    pos = quoted;
    *pos++ = '"';
    for (i = 0 ; i < len; i++) {
        if (string[i] < 0x20 || string[i] == '"' || string[i] == '\\')
            pos += sprintf (pos, "\\%02X", string[i]);
        else
            *pos++ = string[i];
    }
    *pos++ = '"';
    *pos++ = '\0';

    return quoted;
}

void
mm_at_serial_port_set_response_parser (MMAtSerialPort *self,
                                       MMAtSerialResponseParserFn fn,
                                       gpointer user_data,
                                       GDestroyNotify notify)
{
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);

    g_return_if_fail (MM_IS_AT_SERIAL_PORT (self));

    if (priv->response_parser_notify)
        priv->response_parser_notify (priv->response_parser_user_data);

    priv->response_parser_fn = fn;
    priv->response_parser_user_data = user_data;
    priv->response_parser_notify = notify;
}

void
mm_at_serial_port_remove_echo (GByteArray *response)
{
    guint i;

    if (response->len <= 2)
        return;

    for (i = 0; i < (response->len - 1); i++) {
        /* If there is any content before the first
         * <CR><LF>, assume it's echo or garbage, and skip it */
        if (response->data[i] == '\r' && response->data[i + 1] == '\n') {
            if (i > 0)
                g_byte_array_remove_range (response, 0, i);
            /* else, good, we're already started with <CR><LF> */
            break;
        }
    }
}

static gboolean
parse_response (MMSerialPort *port, GByteArray *response, GError **error)
{
    MMAtSerialPort *self = MM_AT_SERIAL_PORT (port);
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);
    gboolean found;
    GString *string;

    g_return_val_if_fail (priv->response_parser_fn != NULL, FALSE);

    /* Remove echo */
    if (priv->remove_echo)
        mm_at_serial_port_remove_echo (response);

    /* Construct the string that AT-parsing functions expect */
    string = g_string_sized_new (response->len + 1);
    g_string_append_len (string, (const char *) response->data, response->len);

    /* Parse it */
    found = priv->response_parser_fn (priv->response_parser_user_data, string, error);

    /* And copy it back into the response array after the parser has removed
     * matches and cleaned it up.
     */
    if (response->len)
        g_byte_array_remove_range (response, 0, response->len);
    g_byte_array_append (response, (const guint8 *) string->str, string->len);
    g_string_free (string, TRUE);
    return found;
}

static gsize
handle_response (MMSerialPort *port,
                 GByteArray *response,
                 GError *error,
                 GCallback callback,
                 gpointer callback_data)
{
    MMAtSerialPort *self = MM_AT_SERIAL_PORT (port);
    MMAtSerialResponseFn response_callback = (MMAtSerialResponseFn) callback;
    GString *string;

    /* Convert to a string and call the callback */
    string = g_string_sized_new (response->len + 1);
    g_string_append_len (string, (const char *) response->data, response->len);
    response_callback (self, string, error, callback_data);
    g_string_free (string, TRUE);

    return response->len;
}

/*****************************************************************************/

typedef struct {
    GRegex *regex;
    MMAtSerialUnsolicitedMsgFn callback;
    gboolean enable;
    gpointer user_data;
    GDestroyNotify notify;
} MMAtUnsolicitedMsgHandler;

static gint
unsolicited_msg_handler_cmp (MMAtUnsolicitedMsgHandler *handler,
                             GRegex *regex)
{
    return g_strcmp0 (g_regex_get_pattern (handler->regex),
                      g_regex_get_pattern (regex));
}

void
mm_at_serial_port_add_unsolicited_msg_handler (MMAtSerialPort *self,
                                               GRegex *regex,
                                               MMAtSerialUnsolicitedMsgFn callback,
                                               gpointer user_data,
                                               GDestroyNotify notify)
{
    GSList *existing;
    MMAtUnsolicitedMsgHandler *handler;
    MMAtSerialPortPrivate *priv;

    g_return_if_fail (MM_IS_AT_SERIAL_PORT (self));
    g_return_if_fail (regex != NULL);

    priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);

    existing = g_slist_find_custom (priv->unsolicited_msg_handlers,
                                    regex,
                                    (GCompareFunc)unsolicited_msg_handler_cmp);
    if (existing) {
        handler = existing->data;
        /* We OVERWRITE any existing one, so if any context data existing, free it */
        if (handler->notify)
            handler->notify (handler->user_data);
    } else {
        handler = g_slice_new (MMAtUnsolicitedMsgHandler);
        priv->unsolicited_msg_handlers = g_slist_append (priv->unsolicited_msg_handlers, handler);
        handler->regex = g_regex_ref (regex);
    }

    handler->callback = callback;
    handler->enable = TRUE;
    handler->user_data = user_data;
    handler->notify = notify;
}

void
mm_at_serial_port_enable_unsolicited_msg_handler (MMAtSerialPort *self,
                                                  GRegex *regex,
                                                  gboolean enable)
{
    GSList *existing;
    MMAtUnsolicitedMsgHandler *handler;
    MMAtSerialPortPrivate *priv;

    g_return_if_fail (MM_IS_AT_SERIAL_PORT (self));
    g_return_if_fail (regex != NULL);

    priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);

    existing = g_slist_find_custom (priv->unsolicited_msg_handlers,
                                    regex,
                                    (GCompareFunc)unsolicited_msg_handler_cmp);
    if (existing) {
        handler = existing->data;
        handler->enable = enable;
    }
}

static gboolean
remove_eval_cb (const GMatchInfo *match_info,
                GString *result,
                gpointer user_data)
{
    int *result_len = (int *) user_data;
    int start;
    int end;

    if (g_match_info_fetch_pos  (match_info, 0, &start, &end))
        *result_len -= (end - start);

    return FALSE;
}

static void
parse_unsolicited (MMSerialPort *port, GByteArray *response)
{
    MMAtSerialPort *self = MM_AT_SERIAL_PORT (port);
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);
    GSList *iter;

    /* Remove echo */
    if (priv->remove_echo)
        mm_at_serial_port_remove_echo (response);

    for (iter = priv->unsolicited_msg_handlers; iter; iter = iter->next) {
        MMAtUnsolicitedMsgHandler *handler = (MMAtUnsolicitedMsgHandler *) iter->data;
        GMatchInfo *match_info;
        gboolean matches;

        if (!handler->enable)
            continue;

        matches = g_regex_match_full (handler->regex,
                                      (const char *) response->data,
                                      response->len,
                                      0, 0, &match_info, NULL);
        if (handler->callback) {
            while (g_match_info_matches (match_info)) {
                handler->callback (self, match_info, handler->user_data);
                g_match_info_next (match_info, NULL);
            }
        }

        g_match_info_free (match_info);

        if (matches) {
            /* Remove matches */
            char *str;
            int result_len = response->len;

            str = g_regex_replace_eval (handler->regex,
                                        (const char *) response->data,
                                        response->len,
                                        0, 0,
                                        remove_eval_cb, &result_len, NULL);

            g_byte_array_remove_range (response, 0, response->len);
            g_byte_array_append (response, (const guint8 *) str, result_len);
            g_free (str);
        }
    }
}

/*****************************************************************************/

static GByteArray *
at_command_to_byte_array (const char *command, gboolean is_raw, gboolean send_lf)
{
    GByteArray *buf;
    int cmdlen;

    g_return_val_if_fail (command != NULL, NULL);

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
        if (send_lf) {
            /* Make sure there's a trailing line-feed */
            if ((cmdlen == 0) ||
                (command[cmdlen - 1] != '\n' && (cmdlen == 1 || command[cmdlen - 2] != '\n')))
                 g_byte_array_append (buf, (const guint8 *) "\n", 1);
        }
    }

    return buf;
}

void
mm_at_serial_port_queue_command (MMAtSerialPort *self,
                                 const char *command,
                                 guint32 timeout_seconds,
                                 gboolean is_raw,
                                 GCancellable *cancellable,
                                 MMAtSerialResponseFn callback,
                                 gpointer user_data)
{
    GByteArray *buf;
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_AT_SERIAL_PORT (self));
    g_return_if_fail (command != NULL);

    buf = at_command_to_byte_array (command, is_raw, priv->send_lf);
    g_return_if_fail (buf != NULL);

    mm_serial_port_queue_command (MM_SERIAL_PORT (self),
                                  buf,
                                  TRUE,
                                  timeout_seconds,
                                  cancellable,
                                  (MMSerialResponseFn) callback,
                                  user_data);
}

void
mm_at_serial_port_queue_command_cached (MMAtSerialPort *self,
                                        const char *command,
                                        guint32 timeout_seconds,
                                        gboolean is_raw,
                                        GCancellable *cancellable,
                                        MMAtSerialResponseFn callback,
                                        gpointer user_data)
{
    GByteArray *buf;
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_AT_SERIAL_PORT (self));
    g_return_if_fail (command != NULL);

    buf = at_command_to_byte_array (command, is_raw, priv->send_lf);
    g_return_if_fail (buf != NULL);

    mm_serial_port_queue_command_cached (MM_SERIAL_PORT (self),
                                         buf,
                                         TRUE,
                                         timeout_seconds,
                                         cancellable,
                                         (MMSerialResponseFn) callback,
                                         user_data);
}

static void
debug_log (MMSerialPort *port, const char *prefix, const char *buf, gsize len)
{
    static GString *debug = NULL;
    const char *s;

    if (!debug)
        debug = g_string_sized_new (256);

    g_string_append (debug, prefix);
    g_string_append (debug, " '");

    s = buf;
    while (len--) {
        if (g_ascii_isprint (*s))
            g_string_append_c (debug, *s);
        else if (*s == '\r')
            g_string_append (debug, "<CR>");
        else if (*s == '\n')
            g_string_append (debug, "<LF>");
        else
            g_string_append_printf (debug, "\\%u", (guint8) (*s & 0xFF));

        s++;
    }

    g_string_append_c (debug, '\'');
    mm_dbg ("(%s): %s", mm_port_get_device (MM_PORT (port)), debug->str);
    g_string_truncate (debug, 0);
}

void
mm_at_serial_port_set_flags (MMAtSerialPort *self, MMAtPortFlag flags)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_AT_SERIAL_PORT (self));
    g_return_if_fail (flags <= (MM_AT_PORT_FLAG_PRIMARY |
                                MM_AT_PORT_FLAG_SECONDARY |
                                MM_AT_PORT_FLAG_PPP |
                                MM_AT_PORT_FLAG_GPS_CONTROL));

    MM_AT_SERIAL_PORT_GET_PRIVATE (self)->flags = flags;
}

MMAtPortFlag
mm_at_serial_port_get_flags (MMAtSerialPort *self)
{
    g_return_val_if_fail (self != NULL, MM_AT_PORT_FLAG_NONE);
    g_return_val_if_fail (MM_IS_AT_SERIAL_PORT (self), MM_AT_PORT_FLAG_NONE);

    return MM_AT_SERIAL_PORT_GET_PRIVATE (self)->flags;
}

/*****************************************************************************/

void
mm_at_serial_port_run_init_sequence (MMAtSerialPort *self)
{
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);
    guint i;

    if (!priv->init_sequence)
        return;

    mm_dbg ("(%s): running init sequence...", mm_port_get_device (MM_PORT (self)));

    /* Just queue the init commands, don't wait for reply */
    for (i = 0; priv->init_sequence[i]; i++) {
        mm_at_serial_port_queue_command (self,
                                         priv->init_sequence[i],
                                         3,
                                         FALSE,
                                         NULL,
                                         NULL,
                                         NULL);
    }
}

static void
config (MMSerialPort *self)
{
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);

    if (priv->init_sequence_enabled)
        mm_at_serial_port_run_init_sequence (MM_AT_SERIAL_PORT (self));
}

/*****************************************************************************/

MMAtSerialPort *
mm_at_serial_port_new (const char *name)
{
    return MM_AT_SERIAL_PORT (g_object_new (MM_TYPE_AT_SERIAL_PORT,
                                            MM_PORT_DEVICE, name,
                                            MM_PORT_SUBSYS, MM_PORT_SUBSYS_TTY,
                                            MM_PORT_TYPE, MM_PORT_TYPE_AT,
                                            NULL));
}

static void
mm_at_serial_port_init (MMAtSerialPort *self)
{
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);

    /* By default, remove echo */
    priv->remove_echo = TRUE;
    /* By default, run init sequence during first port opening */
    priv->init_sequence_enabled = TRUE;

    /* By default, don't send line feed */
    priv->send_lf = FALSE;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_REMOVE_ECHO:
        priv->remove_echo = g_value_get_boolean (value);
        break;
    case PROP_INIT_SEQUENCE_ENABLED:
        priv->init_sequence_enabled = g_value_get_boolean (value);
        break;
    case PROP_INIT_SEQUENCE:
        g_strfreev (priv->init_sequence);
        priv->init_sequence = g_value_dup_boxed (value);
        break;
    case PROP_SEND_LF:
        priv->send_lf = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_REMOVE_ECHO:
        g_value_set_boolean (value, priv->remove_echo);
        break;
    case PROP_INIT_SEQUENCE_ENABLED:
        g_value_set_boolean (value, priv->init_sequence_enabled);
        break;
    case PROP_INIT_SEQUENCE:
        g_value_set_boxed (value, priv->init_sequence);
        break;
    case PROP_SEND_LF:
        g_value_set_boolean (value, priv->send_lf);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMAtSerialPort *self = MM_AT_SERIAL_PORT (object);
    MMAtSerialPortPrivate *priv = MM_AT_SERIAL_PORT_GET_PRIVATE (self);

    while (priv->unsolicited_msg_handlers) {
        MMAtUnsolicitedMsgHandler *handler = (MMAtUnsolicitedMsgHandler *) priv->unsolicited_msg_handlers->data;

        if (handler->notify)
            handler->notify (handler->user_data);

        g_regex_unref (handler->regex);
        g_slice_free (MMAtUnsolicitedMsgHandler, handler);
        priv->unsolicited_msg_handlers = g_slist_delete_link (priv->unsolicited_msg_handlers,
                                                              priv->unsolicited_msg_handlers);
    }

    if (priv->response_parser_notify)
        priv->response_parser_notify (priv->response_parser_user_data);

    g_strfreev (priv->init_sequence);

    G_OBJECT_CLASS (mm_at_serial_port_parent_class)->finalize (object);
}

static void
mm_at_serial_port_class_init (MMAtSerialPortClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMSerialPortClass *port_class = MM_SERIAL_PORT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMAtSerialPortPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    port_class->parse_unsolicited = parse_unsolicited;
    port_class->parse_response = parse_response;
    port_class->handle_response = handle_response;
    port_class->debug_log = debug_log;
    port_class->config = config;

    g_object_class_install_property
        (object_class, PROP_REMOVE_ECHO,
         g_param_spec_boolean (MM_AT_SERIAL_PORT_REMOVE_ECHO,
                               "Remove echo",
                               "Built-in echo removal should be applied",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_INIT_SEQUENCE_ENABLED,
         g_param_spec_boolean (MM_AT_SERIAL_PORT_INIT_SEQUENCE_ENABLED,
                               "Init sequence enabled",
                               "Whether the initialization sequence should be run",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_INIT_SEQUENCE,
         g_param_spec_boxed (MM_AT_SERIAL_PORT_INIT_SEQUENCE,
                             "Init sequence",
                             "Initialization sequence",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_SEND_LF,
         g_param_spec_boolean (MM_AT_SERIAL_PORT_SEND_LF,
                               "Send LF",
                               "Send line-feed at the end of each AT command sent",
                               FALSE,
                               G_PARAM_READWRITE));
}
