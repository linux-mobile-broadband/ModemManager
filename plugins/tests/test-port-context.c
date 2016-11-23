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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <string.h>

#include "test-port-context.h"

#define BUFFER_SIZE 1024

struct _TestPortContext {
    gchar *name;
    GThread *thread;
    gboolean ready;
    GCond ready_cond;
    GMutex ready_mutex;
    GMainLoop *loop;
    GMainContext *context;
    GSocket *socket;
    GSocketService *socket_service;
    GList *clients;
    GHashTable *commands;
};

/*****************************************************************************/

void
test_port_context_set_command (TestPortContext *self,
                               const gchar *command,
                               const gchar *response)
{
    if (G_UNLIKELY (!self->commands))
        self->commands = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_replace (self->commands, g_strdup (command), g_strcompress (response));
}

void
test_port_context_load_commands (TestPortContext *self,
                                 const gchar *file)
{
    GError *error = NULL;
    gchar *contents;
    gchar *current;

    if (!g_file_get_contents (file, &contents, NULL, &error))
        g_error ("Couldn't load commands file '%s': %s",
                 g_filename_display_name (file),
                 error->message);

    current = contents;
    while (current) {
        gchar *next;

        next = strchr (current, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        g_strstrip (current);
        if (current[0] != '\0' && current[0] != '#') {
            gchar *response;

            response = current;
            while (*response != ' ')
                response++;
            g_assert (*response == ' ');
            *response = '\0';
            response++;
            while (*response == ' ')
                response++;
            g_assert (*response != '\0');

            test_port_context_set_command (self, current, response);
        }
        current = next;
    }

    g_free (contents);
}

static const gchar *
process_next_command (TestPortContext *ctx,
                      GByteArray *buffer)
{
    gsize i = 0;
    gchar *command;
    const gchar *response;
    static const gchar *error_response = "\r\nERROR\r\n";

    /* Find command end */
    while (i < buffer->len && buffer->data[i] != '\r' && buffer->data[i] != '\n')
        i++;
    if (i ==  buffer->len)
        /* no command */
        return NULL;

    while (i < buffer->len && (buffer->data[i] == '\r' || buffer->data[i] == '\n'))
        buffer->data[i++] = '\0';

    /* Setup command and lookup response */
    command = g_strndup ((gchar *)buffer->data, i);
    response = g_hash_table_lookup (ctx->commands, command);
    g_free (command);

    /* Remove command from buffer */
    g_byte_array_remove_range (buffer, 0, i);

    return response ? response : error_response;
}

/*****************************************************************************/

typedef struct {
    TestPortContext *ctx;
    GSocketConnection *connection;
    GSource *connection_readable_source;
    GByteArray *buffer;
} Client;

static void
client_free (Client *client)
{
    g_source_destroy (client->connection_readable_source);
    g_source_unref (client->connection_readable_source);
    g_output_stream_close (g_io_stream_get_output_stream (G_IO_STREAM (client->connection)), NULL, NULL);
    if (client->buffer)
        g_byte_array_unref (client->buffer);
    g_object_unref (client->connection);
    g_slice_free (Client, client);
}

static void
connection_close (Client *client)
{
    client->ctx->clients = g_list_remove (client->ctx->clients, client);
    client_free (client);
}

static void
client_parse_request (Client *client)
{
    const gchar *response;

    do {
        response = process_next_command (client->ctx, client->buffer);
        if (response) {
            GError *error = NULL;

            if (!g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (client->connection)),
                                            response,
                                            strlen (response),
                                            NULL, /* bytes_written */
                                            NULL, /* cancellable */
                                            &error)) {
                g_warning ("Cannot send response to client: %s", error->message);
                g_error_free (error);
            }
        }

    } while (response);
}

static gboolean
connection_readable_cb (GSocket *socket,
                        GIOCondition condition,
                        Client *client)
{
    guint8 buffer[BUFFER_SIZE];
    GError *error = NULL;
    gssize r;

    if (condition & G_IO_HUP || condition & G_IO_ERR) {
        g_debug ("client connection closed");
        connection_close (client);
        return FALSE;
    }

    if (!(condition & G_IO_IN || condition & G_IO_PRI))
        return TRUE;

    r = g_input_stream_read (g_io_stream_get_input_stream (G_IO_STREAM (client->connection)),
                             buffer,
                             BUFFER_SIZE,
                             NULL,
                             &error);

    if (r < 0) {
        g_warning ("Error reading from istream: %s", error ? error->message : "unknown");
        if (error)
            g_error_free (error);
        /* Close the device */
        connection_close (client);
        return FALSE;
    }

    if (r == 0)
        return TRUE;

    /* else, r > 0 */
    if (!G_UNLIKELY (client->buffer))
        client->buffer = g_byte_array_sized_new (r);
    g_byte_array_append (client->buffer, buffer, r);

    /* Try to parse input messages */
    client_parse_request (client);

    return TRUE;
}

static Client *
client_new (TestPortContext *self,
            GSocketConnection *connection)
{
    Client *client;

    client = g_slice_new0 (Client);
    client->ctx = self;
    client->connection = g_object_ref (connection);
    client->connection_readable_source = g_socket_create_source (g_socket_connection_get_socket (client->connection),
                                                                 G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                                                                 NULL);
    g_source_set_callback (client->connection_readable_source,
                           (GSourceFunc)connection_readable_cb,
                           client,
                           NULL);
    g_source_attach (client->connection_readable_source, self->context);

    return client;
}

/* /\*****************************************************************************\/ */

static void
incoming_cb (GSocketService *service,
             GSocketConnection *connection,
             GObject *unused,
             TestPortContext *self)
{
    Client *client;

    client = client_new (self, connection);
    self->clients = g_list_append (self->clients, client);
}

static void
create_socket_service (TestPortContext *self)
{
    GError *error = NULL;
    GSocketService *service;
    GSocketAddress *address;
    GSocket *socket;

    g_assert (self->socket_service == NULL);

    /* Create socket */
    socket = g_socket_new (G_SOCKET_FAMILY_UNIX,
                           G_SOCKET_TYPE_STREAM,
                           G_SOCKET_PROTOCOL_DEFAULT,
                           &error);
    if (!socket)
        g_error ("Cannot create socket: %s", error->message);

    /* Bind to address */
    address = (g_unix_socket_address_new_with_type (
                   self->name,
                   -1,
                   (g_str_has_prefix (self->name, "abstract:") ?
                    G_UNIX_SOCKET_ADDRESS_ABSTRACT :
                    G_UNIX_SOCKET_ADDRESS_PATH)));
    if (!g_socket_bind (socket, address, TRUE, &error))
        g_error ("Cannot bind socket: %s", error->message);
    g_object_unref (address);

    /* Listen */
    if (!g_socket_listen (socket, &error))
        g_error ("Cannot listen in socket: %s", error->message);

    /* Create socket service */
    service = g_socket_service_new ();
    g_signal_connect (service, "incoming", G_CALLBACK (incoming_cb), self);
    if (!g_socket_listener_add_socket (G_SOCKET_LISTENER (service),
                                       socket,
                                       NULL, /* don't pass an object, will take a reference */
                                       &error))
        g_error ("Cannot add listener to socket: %s", error->message);

    /* Start it */
    g_socket_service_start (service);

    /* And store both the service and the socket.
     * Since GLib 2.42 the socket may not be explicitly closed when the
     * listener is diposed, so we'll do it ourselves. */
    self->socket_service = service;
    self->socket = socket;

    /* Signal that the thread is ready */
    g_mutex_lock (&self->ready_mutex);
    self->ready = TRUE;
    g_cond_signal (&self->ready_cond);
    g_mutex_unlock (&self->ready_mutex);
}

/*****************************************************************************/

static gboolean
cancel_loop_cb (TestPortContext *self)
{
    g_main_loop_quit (self->loop);
    return FALSE;
}

void
test_port_context_stop (TestPortContext *self)
{
    g_assert (self->thread != NULL);
    g_assert (self->loop != NULL);
    g_assert (self->context != NULL);

    /* Cancel main loop of the port context thread, by scheduling an idle task
     * in the thread-owned main context */
    g_main_context_invoke (self->context, (GSourceFunc) cancel_loop_cb, self);

    g_thread_join (self->thread);
    self->thread = NULL;
}

static gpointer
port_context_thread_func (TestPortContext *self)
{
    g_assert (self->loop == NULL);
    g_assert (self->context == NULL);

    /* Define main context and loop for the thread */
    self->context = g_main_context_new ();
    self->loop    = g_main_loop_new (self->context, FALSE);
    g_main_context_push_thread_default (self->context);

    /* Once the thread default context is setup, launch service */
    create_socket_service (self);

    g_main_loop_run (self->loop);

    g_main_loop_unref (self->loop);
    self->loop = NULL;
    g_main_context_unref (self->context);
    self->context = NULL;
    return NULL;
}

void
test_port_context_start (TestPortContext *self)
{
    g_assert (self->thread == NULL);
    self->thread = g_thread_new (self->name,
                                 (GThreadFunc)port_context_thread_func,
                                 self);

    /* Now wait until the thread has finished its initialization and is
     * ready to serve connections */
    g_mutex_lock (&self->ready_mutex);
    while (!self->ready)
        g_cond_wait (&self->ready_cond, &self->ready_mutex);
    g_mutex_unlock (&self->ready_mutex);
}

/*****************************************************************************/

void
test_port_context_free (TestPortContext *self)
{
    g_assert (self->thread == NULL);
    g_assert (self->loop == NULL);

    g_cond_clear (&self->ready_cond);
    g_mutex_clear (&self->ready_mutex);

    if (self->commands)
        g_hash_table_unref (self->commands);
    g_list_free_full (self->clients, (GDestroyNotify)client_free);
    if (self->socket) {
        GError *error = NULL;

        if (!g_socket_close (self->socket, &error)) {
            g_debug ("Couldn't close socket: %s", error->message);
            g_error_free (error);
        }
        g_object_unref (self->socket);
    }
    if (self->socket_service) {
        if (g_socket_service_is_active (self->socket_service))
            g_socket_service_stop (self->socket_service);
        g_object_unref (self->socket_service);
    }
    g_free (self->name);
    g_slice_free (TestPortContext, self);
}

TestPortContext *
test_port_context_new (const gchar *name)
{
    TestPortContext *self;

    self = g_slice_new0 (TestPortContext);
    self->name = g_strdup (name);
    g_cond_init (&self->ready_cond);
    g_mutex_init (&self->ready_mutex);
    return self;
}
