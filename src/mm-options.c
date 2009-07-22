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
 * Copyright (C) 2008 Novell, Inc.
 */

#include <stdlib.h>
#include <glib.h>
#include "mm-options.h"

static gboolean debug = FALSE;

void
mm_options_parse (int argc, char *argv[])
{
    GOptionContext *opt_ctx;
    GError *error = NULL;
    GOptionEntry entries[] = {
		{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Output to console rather than syslog", NULL },
		{ NULL }
	};

	opt_ctx = g_option_context_new (NULL);
	g_option_context_set_summary (opt_ctx, "DBus system service to communicate with modems.");
	g_option_context_add_main_entries (opt_ctx, entries, NULL);

	if (!g_option_context_parse (opt_ctx, &argc, &argv, &error)) {
		g_warning ("%s\n", error->message);
		g_error_free (error);
		exit (1);
	}

	g_option_context_free (opt_ctx);
}

void
mm_options_set_debug (gboolean enabled)
{
    debug = enabled;
}

gboolean
mm_options_debug (void)
{
    return debug;
}
