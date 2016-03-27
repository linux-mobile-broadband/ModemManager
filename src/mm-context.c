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

/*****************************************************************************/
/* Application context */

static gboolean version_flag;
static gboolean debug;
static const gchar *log_level;
static const gchar *log_file;
static gboolean show_ts;
static gboolean rel_ts;

static const GOptionEntry entries[] = {
    { "version", 'V', 0, G_OPTION_ARG_NONE, &version_flag, "Print version", NULL },
    { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Run with extended debugging capabilities", NULL },
    { "log-level", 0, 0, G_OPTION_ARG_STRING, &log_level, "Log level: one of ERR, WARN, INFO, DEBUG", "[LEVEL]" },
    { "log-file", 0, 0, G_OPTION_ARG_FILENAME, &log_file, "Path to log file", "[PATH]" },
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

/*****************************************************************************/
/* Test context */

static gboolean test_session;
static gboolean test_no_auto_scan;
static gboolean test_enable;
static gchar *test_plugin_dir;

static const GOptionEntry test_entries[] = {
    { "test-session", 0, 0, G_OPTION_ARG_NONE, &test_session, "Run in session DBus", NULL },
    { "test-no-auto-scan", 0, 0, G_OPTION_ARG_NONE, &test_no_auto_scan, "Don't auto-scan looking for devices", NULL },
    { "test-enable", 0, 0, G_OPTION_ARG_NONE, &test_enable, "Enable the Test interface in the daemon", NULL },
    { "test-plugin-dir", 0, 0, G_OPTION_ARG_FILENAME, &test_plugin_dir, "Path to look for plugins", "[PATH]" },
    { NULL }
};

static GOptionGroup *
test_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("test",
                                "Test options",
                                "Show Test options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, test_entries);
    return group;
}

gboolean
mm_context_get_test_session (void)
{
    return test_session;
}

gboolean
mm_context_get_test_no_auto_scan (void)
{
    return test_no_auto_scan;
}

gboolean
mm_context_get_test_enable (void)
{
    return test_enable;
}

const gchar *
mm_context_get_test_plugin_dir (void)
{
    return test_plugin_dir ? test_plugin_dir : PLUGINDIR;
}

/*****************************************************************************/

static void
print_version (void)
{
    g_print ("\n"
             "ModemManager " MM_DIST_VERSION "\n"
             "Copyright (2008 - 2014) The ModemManager authors\n"
             "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>\n"
             "This is free software: you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by law.\n"
             "\n");
    exit (EXIT_SUCCESS);
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
    g_option_context_add_group (ctx, test_get_option_group ());

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

    /* If just version requested, print and exit */
    if (version_flag)
        print_version ();
}
