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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mm-port-serial-gps.h"
#include "mm-log-object.h"

G_DEFINE_TYPE (MMPortSerialGps, mm_port_serial_gps, MM_TYPE_PORT_SERIAL)

struct _MMPortSerialGpsPrivate {
    /* Trace handler data */
    MMPortSerialGpsTraceFn callback;
    gpointer user_data;
    GDestroyNotify notify;

    /* Regex for all known traces */
    GRegex *known_traces_regex;
};

/*****************************************************************************/

void
mm_port_serial_gps_add_trace_handler (MMPortSerialGps *self,
                                      MMPortSerialGpsTraceFn callback,
                                      gpointer user_data,
                                      GDestroyNotify notify)
{
    g_return_if_fail (MM_IS_PORT_SERIAL_GPS (self));

    if (self->priv->notify)
        self->priv->notify (self->priv->user_data);

    self->priv->callback = callback;
    self->priv->user_data = user_data;
    self->priv->notify = notify;
}

/*****************************************************************************/

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

static MMPortSerialResponseType
parse_response (MMPortSerial *port,
                GByteArray *response,
                GByteArray **parsed_response,
                GError **error)
{
    MMPortSerialGps *self = MM_PORT_SERIAL_GPS (port);
    gboolean matches;
    GMatchInfo *match_info;
    gchar *str;
    gint result_len;
    guint i;

    for (i = 0; i < response->len; i++) {
        /* If there is any content before the first $,
         * assume it's garbage, and skip it */
        if (response->data[i] == '$') {
            if (i > 0)
                g_byte_array_remove_range (response, 0, i);
            /* else, good, we're already started with $ */
            break;
        }
    }

    matches = g_regex_match_full (self->priv->known_traces_regex,
                                  (const gchar *) response->data,
                                  response->len,
                                  0, 0, &match_info, NULL);

    if (self->priv->callback) {
        while (g_match_info_matches (match_info)) {
            gchar *trace;

            trace = g_match_info_fetch (match_info, 0);
            if (trace) {
                self->priv->callback (self, trace, self->priv->user_data);
                g_free (trace);
            }
            g_match_info_next (match_info, NULL);
        }
    }

    g_match_info_free (match_info);

    if (!matches)
        return MM_PORT_SERIAL_RESPONSE_NONE;

    /* Remove matches */
    result_len = response->len;
    str = g_regex_replace_eval (self->priv->known_traces_regex,
                                (const char *) response->data,
                                response->len,
                                0, 0,
                                remove_eval_cb, &result_len, NULL);

    /* Cleanup response buffer */
    g_byte_array_remove_range (response, 0, response->len);

    /* Build parsed response */
    *parsed_response = g_byte_array_new_take ((guint8 *)str, result_len);

    return TRUE;
}

/*****************************************************************************/

static void
debug_log (MMPortSerial *self,
           const gchar  *prefix,
           const gchar  *buf,
           gsize         len)
{
    static GString *debug = NULL;
    const gchar    *s;

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

/*****************************************************************************/

MMPortSerialGps *
mm_port_serial_gps_new (const char *name)
{
    return MM_PORT_SERIAL_GPS (g_object_new (MM_TYPE_PORT_SERIAL_GPS,
                                             MM_PORT_DEVICE, name,
                                             MM_PORT_SUBSYS, MM_PORT_SUBSYS_TTY,
                                             MM_PORT_TYPE, MM_PORT_TYPE_GPS,
                                             NULL));
}

static void
mm_port_serial_gps_init (MMPortSerialGps *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_PORT_SERIAL_GPS,
                                              MMPortSerialGpsPrivate);

    /* We'll assume that all traces start with the dollar sign and end with \r\n */
    self->priv->known_traces_regex =
        g_regex_new ("\\$.*\\r\\n",
                     G_REGEX_RAW | G_REGEX_OPTIMIZE,
                     0,
                     NULL);
}

static void
finalize (GObject *object)
{
    MMPortSerialGps *self = MM_PORT_SERIAL_GPS (object);

    if (self->priv->notify)
        self->priv->notify (self->priv->user_data);

    g_regex_unref (self->priv->known_traces_regex);

    G_OBJECT_CLASS (mm_port_serial_gps_parent_class)->finalize (object);
}

static void
mm_port_serial_gps_class_init (MMPortSerialGpsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMPortSerialClass *serial_class = MM_PORT_SERIAL_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortSerialGpsPrivate));

    /* Virtual methods */
    object_class->finalize = finalize;

    serial_class->parse_response = parse_response;
    serial_class->debug_log = debug_log;
}
