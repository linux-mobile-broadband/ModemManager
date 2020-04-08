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
 * Copyright (C) 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <mm-log-test.h>
#include <mm-port-serial.h>
#include <mm-port-serial-at.h>
#include <mm-serial-parsers.h>

#define PROGRAM_NAME    "mmtty"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Globals */
static MMPortSerialAt *port;
static GMainLoop      *loop;
static GIOChannel     *input;
static guint           input_watch_id;

/* Context */
static gchar    *device_str;
static gboolean  no_flash_flag;
static gboolean  no_echo_removal_flag;
static gint64    send_delay = -1;
static gboolean  verbose_flag;
static gboolean  version_flag;

static GOptionEntry main_entries[] = {
    { "device", 'd', 0, G_OPTION_ARG_STRING, &device_str,
      "Specify device path",
      "[PATH]"
    },
    { "no-flash", 0, 0, G_OPTION_ARG_NONE, &no_flash_flag,
      "Avoid flashing the port while opening",
      NULL
    },
    { "no-echo-removal", 0, 0, G_OPTION_ARG_NONE, &no_echo_removal_flag,
      "Avoid logic to remove echo",
      NULL
    },
    { "send-delay", 0, 0, G_OPTION_ARG_INT64, &send_delay,
      "Send delay for each byte in microseconds (default=1000)",
      "[DELAY]"
    },
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose_flag,
      "Run action with verbose logs",
      NULL
    },
    { "version", 'V', 0, G_OPTION_ARG_NONE, &version_flag,
      "Print version",
      NULL
    },
    { NULL }
};

static void
signals_handler (int signum)
{
    if (loop && g_main_loop_is_running (loop)) {
        g_printerr ("%s\n",
                    "cancelling the main loop...\n");
        g_main_loop_quit (loop);
    }
}

static void
print_version_and_exit (void)
{
    g_print ("\n"
             PROGRAM_NAME " " PROGRAM_VERSION "\n"
             "Copyright (2015) Aleksander Morgado\n"
             "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>\n"
             "This is free software: you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by law.\n"
             "\n");
    exit (EXIT_SUCCESS);
}

static void
at_command_ready (MMPortSerialAt *serial_at,
                  GAsyncResult   *res)
{
    const gchar *response;
    GError      *error = NULL;

    response = mm_port_serial_at_command_finish (serial_at, res, &error);
    if (response)
        g_print ("%s\n", response);
    if (error) {
        g_printerr ("%s\n", error->message);
        g_error_free (error);
    }

    g_print ("> ");
}

static gboolean
input_callback (GIOChannel   *channel,
                GIOCondition  condition)
{
    GError    *error = NULL;
    GIOStatus  status;
    gchar     *line = NULL;

    status = g_io_channel_read_line (channel, &line, NULL, NULL, &error);

    switch (status) {
    case G_IO_STATUS_NORMAL:
        mm_port_serial_at_command (port, line, 60, FALSE, FALSE, NULL,
                                   (GAsyncReadyCallback) at_command_ready, NULL);
        g_free (line);
        return TRUE;

    case G_IO_STATUS_ERROR:
        g_printerr ("error: %s\n", error->message);
        g_error_free (error);
        return FALSE;

    case G_IO_STATUS_EOF:
        g_warning ("error: No input data available");
        return TRUE;

    case G_IO_STATUS_AGAIN:
        return TRUE;

    default:
        g_assert_not_reached ();
    }

    return FALSE;
}

static void
flash_ready (MMPortSerial *serial,
             GAsyncResult *res)
{
    GError *error = NULL;

    if (!mm_port_serial_flash_finish (serial, res, &error)) {
        g_printerr ("error: cannot flash serial port: %s\n", error->message);
        exit (EXIT_FAILURE);
    }

    g_print ("ready\n");
    g_print ("> ");

    /* Setup input reading */
    input = g_io_channel_unix_new (STDIN_FILENO);
    input_watch_id = g_io_add_watch (input, G_IO_IN, (GIOFunc) input_callback, NULL);
}

static void
serial_buffer_full (void)
{
    g_printerr ("error: serial buffer full\n");
}

static gboolean
start_cb (void)
{
    GError *error = NULL;
    const gchar *device_name;

    device_name = device_str;
    if (g_str_has_prefix (device_name, "/dev/"))
        device_name += strlen ("/dev/");

    g_print ("creating AT capable serial port for device '%s'...\n", device_name);
    port = mm_port_serial_at_new (device_name, MM_PORT_SUBSYS_TTY);

    /* Setup send delay */
    if (send_delay >= 0) {
        if (send_delay > 0)
            g_print ("updating send delay to %" G_GINT64_FORMAT "us...\n", send_delay);
        else
            g_print ("disabling send delay...\n");
        g_object_set (port, MM_PORT_SERIAL_SEND_DELAY, send_delay, NULL);
    }

    /* Setup echo removal */
    if (no_echo_removal_flag) {
        g_print ("disabling echo removal...\n");
        g_object_set (port, MM_PORT_SERIAL_AT_REMOVE_ECHO, FALSE, NULL);
    }

    /* Set common response parser */
    mm_port_serial_at_set_response_parser (MM_PORT_SERIAL_AT (port),
                                           mm_serial_parser_v1_parse,
                                           mm_serial_parser_v1_new (),
                                           mm_serial_parser_v1_destroy);

    /* Try to open the port... */
    g_print ("opening serial port...\n");
    if (!mm_port_serial_open (MM_PORT_SERIAL (port), &error)) {
        g_printerr ("error: cannot open serial port: %s\n", error->message);
        exit (EXIT_FAILURE);
    }

    /* Warn on full buffer by default */
    g_signal_connect (port, "buffer-full", G_CALLBACK (serial_buffer_full), NULL);

    /* If we set FLASH_OK to FALSE, the flashing operation does nothing */
    if (no_flash_flag) {
        g_print ("disabling serial port flash...\n");
        g_object_set (port, MM_PORT_SERIAL_FLASH_OK, FALSE, NULL);
    } else
        g_print ("flashing serial port...\n");
    mm_port_serial_flash (MM_PORT_SERIAL (port),
                          100,
                          FALSE,
                          (GAsyncReadyCallback) flash_ready,
                          NULL);
    return G_SOURCE_REMOVE;
}

int main (int argc, char **argv)
{
    GOptionContext *context;

    setlocale (LC_ALL, "");

    /* Setup option context, process it and destroy it */
    context = g_option_context_new ("- ModemManager TTY testing");
    g_option_context_add_main_entries (context, main_entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (version_flag)
        print_version_and_exit ();

    /* No device path given? */
    if (!device_str) {
        g_printerr ("error: no device path specified\n");
        exit (EXIT_FAILURE);
    }

    /* Setup signals */
    signal (SIGINT, signals_handler);
    signal (SIGHUP, signals_handler);
    signal (SIGTERM, signals_handler);

    /* Setup main loop and shedule start in idle */
    loop = g_main_loop_new (NULL, FALSE);
    g_idle_add ((GSourceFunc)start_cb, NULL);
    g_main_loop_run (loop);

    /* Cleanup */
    g_main_loop_unref (loop);
    if (port) {
        if (mm_port_serial_is_open (MM_PORT_SERIAL (port)))
            mm_port_serial_close (MM_PORT_SERIAL (port));
        g_object_unref (port);
    }
    if (input)
        g_io_channel_unref (input);
    return 0;
}
