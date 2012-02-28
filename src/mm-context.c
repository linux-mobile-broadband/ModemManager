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

#include <stdlib.h>

#include "mm-context.h"

/* Application context */
static gboolean debug;
static const gchar *log_level;
static const gchar *log_file;
static gboolean show_ts;
static gboolean rel_ts;

static const GOptionEntry entries[] = {
    { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Run with extended debugging capabilities", NULL },
    { "log-level", 0, 0, G_OPTION_ARG_STRING, &log_level, "Log level: one of [ERR, WARN, INFO, DEBUG]", "INFO" },
    { "log-file", 0, 0, G_OPTION_ARG_STRING, &log_file, "Path to log file", NULL },
    { "timestamps", 0, 0, G_OPTION_ARG_NONE, &show_ts, "Show timestamps in log output", NULL },
    { "relative-timestamps", 0, 0, G_OPTION_ARG_NONE, &rel_ts, "Use relative timestamps (from MM start)", NULL },
    { NULL }
};

gboolean
mm_context_get_debug (void)
{
    return debug;
}

const gchar *
mm_context_get_log_level (void)
{
    return log_level;
}

const gchar *
mm_context_get_log_file (void)
{
    return log_file;
}

gboolean
mm_context_get_timestamps (void)
{
    return show_ts;
}

gboolean
mm_context_get_relative_timestamps (void)
{
    return rel_ts;
}

void
mm_context_init (gint argc,
                 gchar **argv)
{
    GError *error = NULL;
    GOptionContext *ctx;

	ctx = g_option_context_new (NULL);
	g_option_context_set_summary (ctx, "DBus system service to communicate with modems.");
	g_option_context_add_main_entries (ctx, entries, NULL);

    if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
		g_warning ("%s\n", error->message);
		g_error_free (error);
        exit (1);
	}

	g_option_context_free (ctx);

    /* Additional setup to be done on debug mode */
    if (debug) {
        log_level = "DEBUG";
        if (!show_ts && !rel_ts)
            show_ts = TRUE;
    }
}
