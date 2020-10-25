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

#include "mm-port-serial-at.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMPortSerialAt, mm_port_serial_at, MM_TYPE_PORT_SERIAL)

enum {
    PROP_0,
    PROP_REMOVE_ECHO,
    PROP_INIT_SEQUENCE_ENABLED,
    PROP_INIT_SEQUENCE,
    PROP_SEND_LF,
    LAST_PROP
};

struct _MMPortSerialAtPrivate {
    /* Response parser data */
    MMPortSerialAtResponseParserFn response_parser_fn;
    gpointer response_parser_user_data;
    GDestroyNotify response_parser_notify;

    GSList *unsolicited_msg_handlers;

    MMPortSerialAtFlag flags;

    /* Properties */
    gboolean remove_echo;
    guint init_sequence_enabled;
    gchar **init_sequence;
    gboolean send_lf;
};

/*****************************************************************************/

gchar *
mm_port_serial_at_quote_string (const char *string)
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
mm_port_serial_at_set_response_parser (MMPortSerialAt *self,
                                       MMPortSerialAtResponseParserFn fn,
                                       gpointer user_data,
                                       GDestroyNotify notify)
{
    g_return_if_fail (MM_IS_PORT_SERIAL_AT (self));

    if (self->priv->response_parser_notify)
        self->priv->response_parser_notify (self->priv->response_parser_user_data);

    self->priv->response_parser_fn = fn;
    self->priv->response_parser_user_data = user_data;
    self->priv->response_parser_notify = notify;
}

void
mm_port_serial_at_remove_echo (GByteArray *response)
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

static MMPortSerialResponseType
parse_response (MMPortSerial *port,
                GByteArray *response,
                GByteArray **parsed_response,
                GError **error)
{
    MMPortSerialAt *self = MM_PORT_SERIAL_AT (port);
    GString *string;
    gsize parsed_len;
    GError *inner_error = NULL;

    g_return_val_if_fail (self->priv->response_parser_fn != NULL, FALSE);

    /* Remove echo */
    if (self->priv->remove_echo)
        mm_port_serial_at_remove_echo (response);

    /* If there's no response to receive, we're done; e.g. if we only got
     * unsolicited messages */
    if (!response->len)
        return MM_PORT_SERIAL_RESPONSE_NONE;

    /* Construct the string that AT-parsing functions expect */
    string = g_string_sized_new (response->len + 1);
    g_string_append_len (string, (const char *) response->data, response->len);

    /* Fully cleanup the response array, we'll consider the contents we got
     * as the full reply that the command may expect. */
    g_byte_array_remove_range (response, 0, response->len);

    /* Parse it; returns FALSE if there is nothing we can do with this
     * response yet. */
    if (!self->priv->response_parser_fn (self->priv->response_parser_user_data, string, self, &inner_error)) {
        /* Copy what we got back in the response buffer. */
        g_byte_array_append (response, (const guint8 *) string->str, string->len);
        g_string_free (string, TRUE);
        return MM_PORT_SERIAL_RESPONSE_NONE;
    }

    /* If we got an error, propagate it without any further response string */
    if (inner_error) {
        g_string_free (string, TRUE);
        g_propagate_error (error, inner_error);
        return MM_PORT_SERIAL_RESPONSE_ERROR;
    }

    /* Otherwise, build a new GByteArray considered as parsed response */
    parsed_len = string->len;
    *parsed_response = g_byte_array_new_take ((guint8 *) g_string_free (string, FALSE), parsed_len);
    return MM_PORT_SERIAL_RESPONSE_BUFFER;
}

/*****************************************************************************/

typedef struct {
    GRegex *regex;
    MMPortSerialAtUnsolicitedMsgFn callback;
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
mm_port_serial_at_add_unsolicited_msg_handler (MMPortSerialAt *self,
                                               GRegex *regex,
                                               MMPortSerialAtUnsolicitedMsgFn callback,
                                               gpointer user_data,
                                               GDestroyNotify notify)
{
    GSList *existing;
    MMAtUnsolicitedMsgHandler *handler;

    g_return_if_fail (MM_IS_PORT_SERIAL_AT (self));
    g_return_if_fail (regex != NULL);

    existing = g_slist_find_custom (self->priv->unsolicited_msg_handlers,
                                    regex,
                                    (GCompareFunc)unsolicited_msg_handler_cmp);
    if (existing) {
        handler = existing->data;
        /* We OVERWRITE any existing one, so if any context data existing, free it */
        if (handler->notify)
            handler->notify (handler->user_data);
    } else {
        /* The new handler is always PREPENDED, so that e.g. plugins can provide
         * more specific matches for URCs that are also handled by the generic
         * plugin. */
        handler = g_slice_new (MMAtUnsolicitedMsgHandler);
        handler->regex = g_regex_ref (regex);
        self->priv->unsolicited_msg_handlers = g_slist_prepend (self->priv->unsolicited_msg_handlers, handler);
    }

    handler->callback = callback;
    handler->enable = TRUE;
    handler->user_data = user_data;
    handler->notify = notify;
}

void
mm_port_serial_at_enable_unsolicited_msg_handler (MMPortSerialAt *self,
                                                  GRegex *regex,
                                                  gboolean enable)
{
    GSList *existing;
    MMAtUnsolicitedMsgHandler *handler;

    g_return_if_fail (MM_IS_PORT_SERIAL_AT (self));
    g_return_if_fail (regex != NULL);

    existing = g_slist_find_custom (self->priv->unsolicited_msg_handlers,
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
parse_unsolicited (MMPortSerial *port, GByteArray *response)
{
    MMPortSerialAt *self = MM_PORT_SERIAL_AT (port);
    GSList *iter;

    /* Remove echo */
    if (self->priv->remove_echo)
        mm_port_serial_at_remove_echo (response);

    for (iter = self->priv->unsolicited_msg_handlers; iter; iter = iter->next) {
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

const gchar *
mm_port_serial_at_command_finish (MMPortSerialAt *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    GString *str;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    str = (GString *)g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    return str->str;
}

static void
string_free (GString *str)
{
    g_string_free (str, TRUE);
}

static void
serial_command_ready (MMPortSerial *port,
                      GAsyncResult *res,
                      GSimpleAsyncResult *simple)
{
    GByteArray *response_buffer;
    GError *error = NULL;
    GString *response;

    response_buffer = mm_port_serial_command_finish (port, res, &error);
    if (!response_buffer) {
        g_simple_async_result_take_error (simple, error);
        g_simple_async_result_complete (simple);
        g_object_unref (simple);
        return;
    }

    /* Build a GString just with the response we need, and clear the
     * processed range from the response buffer */
    response = g_string_new_len ((const gchar *)response_buffer->data, response_buffer->len);
    if (response_buffer->len > 0)
        g_byte_array_remove_range (response_buffer, 0, response_buffer->len);
    g_byte_array_unref (response_buffer);

    g_simple_async_result_set_op_res_gpointer (simple,
                                               response,
                                               (GDestroyNotify)string_free);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_port_serial_at_command (MMPortSerialAt *self,
                           const char *command,
                           guint32 timeout_seconds,
                           gboolean is_raw,
                           gboolean allow_cached,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *simple;
    GByteArray *buf;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_PORT_SERIAL_AT (self));
    g_return_if_fail (command != NULL);

    buf = at_command_to_byte_array (command,
                                    is_raw,
                                    (mm_port_get_subsys (MM_PORT (self)) == MM_PORT_SUBSYS_TTY ?
                                     self->priv->send_lf :
                                     TRUE));
    g_return_if_fail (buf != NULL);

    simple = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_port_serial_at_command);

    mm_port_serial_command (MM_PORT_SERIAL (self),
                            buf,
                            timeout_seconds,
                            allow_cached,
                            is_raw, /* raw commands always run next, never queued last */
                            cancellable,
                            (GAsyncReadyCallback)serial_command_ready,
                            simple);
    g_byte_array_unref (buf);
}

static void
debug_log (MMPortSerial *self,
           const gchar  *prefix,
           const gchar  *buf,
           gsize         len)
{
    static GString *debug = NULL;
    const  char    *s;

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
    mm_obj_dbg (self, "%s", debug->str);
    g_string_truncate (debug, 0);
}

void
mm_port_serial_at_set_flags (MMPortSerialAt *self, MMPortSerialAtFlag flags)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_PORT_SERIAL_AT (self));

    /* MM_PORT_SERIAL_AT_FLAG_NONE_NO_GENERIC is not expected */
    g_return_if_fail (flags <= (MM_PORT_SERIAL_AT_FLAG_PRIMARY |
                                MM_PORT_SERIAL_AT_FLAG_SECONDARY |
                                MM_PORT_SERIAL_AT_FLAG_PPP |
                                MM_PORT_SERIAL_AT_FLAG_GPS_CONTROL));

    self->priv->flags = flags;
}

MMPortSerialAtFlag
mm_port_serial_at_get_flags (MMPortSerialAt *self)
{
    g_return_val_if_fail (self != NULL, MM_PORT_SERIAL_AT_FLAG_NONE);
    g_return_val_if_fail (MM_IS_PORT_SERIAL_AT (self), MM_PORT_SERIAL_AT_FLAG_NONE);

    return self->priv->flags;
}

/*****************************************************************************/

void
mm_port_serial_at_run_init_sequence (MMPortSerialAt *self)
{
    guint i;

    if (!self->priv->init_sequence)
        return;

    mm_obj_dbg (self, "running init sequence...");

    /* Just queue the init commands, don't wait for reply */
    for (i = 0; self->priv->init_sequence[i]; i++) {
        mm_port_serial_at_command (self,
                                   self->priv->init_sequence[i],
                                   3,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   NULL,
                                   NULL);
    }
}

static void
config (MMPortSerial *_self)
{
    MMPortSerialAt *self = MM_PORT_SERIAL_AT (_self);

    if (self->priv->init_sequence_enabled)
        mm_port_serial_at_run_init_sequence (self);
}

/*****************************************************************************/

MMPortSerialAt *
mm_port_serial_at_new (const char   *name,
                       MMPortSubsys  subsys)
{
    return MM_PORT_SERIAL_AT (g_object_new (MM_TYPE_PORT_SERIAL_AT,
                                            MM_PORT_DEVICE, name,
                                            MM_PORT_SUBSYS, subsys,
                                            MM_PORT_TYPE,   MM_PORT_TYPE_AT,
                                            NULL));
}

static void
mm_port_serial_at_init (MMPortSerialAt *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT_SERIAL_AT, MMPortSerialAtPrivate);

    /* By default, remove echo */
    self->priv->remove_echo = TRUE;
    /* By default, run init sequence during first port opening */
    self->priv->init_sequence_enabled = TRUE;

    /* By default, don't send line feed */
    self->priv->send_lf = FALSE;
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMPortSerialAt *self = MM_PORT_SERIAL_AT (object);

    switch (prop_id) {
    case PROP_REMOVE_ECHO:
        self->priv->remove_echo = g_value_get_boolean (value);
        break;
    case PROP_INIT_SEQUENCE_ENABLED:
        self->priv->init_sequence_enabled = g_value_get_boolean (value);
        break;
    case PROP_INIT_SEQUENCE:
        g_strfreev (self->priv->init_sequence);
        self->priv->init_sequence = g_value_dup_boxed (value);
        break;
    case PROP_SEND_LF:
        self->priv->send_lf = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMPortSerialAt *self = MM_PORT_SERIAL_AT (object);

    switch (prop_id) {
    case PROP_REMOVE_ECHO:
        g_value_set_boolean (value, self->priv->remove_echo);
        break;
    case PROP_INIT_SEQUENCE_ENABLED:
        g_value_set_boolean (value, self->priv->init_sequence_enabled);
        break;
    case PROP_INIT_SEQUENCE:
        g_value_set_boxed (value, self->priv->init_sequence);
        break;
    case PROP_SEND_LF:
        g_value_set_boolean (value, self->priv->send_lf);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMPortSerialAt *self = MM_PORT_SERIAL_AT (object);

    while (self->priv->unsolicited_msg_handlers) {
        MMAtUnsolicitedMsgHandler *handler = (MMAtUnsolicitedMsgHandler *) self->priv->unsolicited_msg_handlers->data;

        if (handler->notify)
            handler->notify (handler->user_data);

        g_regex_unref (handler->regex);
        g_slice_free (MMAtUnsolicitedMsgHandler, handler);
        self->priv->unsolicited_msg_handlers = g_slist_delete_link (self->priv->unsolicited_msg_handlers,
                                                                    self->priv->unsolicited_msg_handlers);
    }

    if (self->priv->response_parser_notify)
        self->priv->response_parser_notify (self->priv->response_parser_user_data);

    g_strfreev (self->priv->init_sequence);

    G_OBJECT_CLASS (mm_port_serial_at_parent_class)->finalize (object);
}

static void
mm_port_serial_at_class_init (MMPortSerialAtClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMPortSerialClass *serial_class = MM_PORT_SERIAL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortSerialAtPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    serial_class->parse_unsolicited = parse_unsolicited;
    serial_class->parse_response = parse_response;
    serial_class->debug_log = debug_log;
    serial_class->config = config;

    g_object_class_install_property
        (object_class, PROP_REMOVE_ECHO,
         g_param_spec_boolean (MM_PORT_SERIAL_AT_REMOVE_ECHO,
                               "Remove echo",
                               "Built-in echo removal should be applied",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_INIT_SEQUENCE_ENABLED,
         g_param_spec_boolean (MM_PORT_SERIAL_AT_INIT_SEQUENCE_ENABLED,
                               "Init sequence enabled",
                               "Whether the initialization sequence should be run",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_INIT_SEQUENCE,
         g_param_spec_boxed (MM_PORT_SERIAL_AT_INIT_SEQUENCE,
                             "Init sequence",
                             "Initialization sequence",
                             G_TYPE_STRV,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_SEND_LF,
         g_param_spec_boolean (MM_PORT_SERIAL_AT_SEND_LF,
                               "Send LF",
                               "Send line-feed at the end of each AT command sent",
                               FALSE,
                               G_PARAM_READWRITE));
}
