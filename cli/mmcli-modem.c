/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libmm.h>

#include "mmcli.h"

/* Context */
typedef struct {
    /* Input options */
    gchar *modem_str;
    gboolean info_flag;
    /* The modem proxy */
    MMModem *modem;
} Context;
static Context ctxt;

static GOptionEntry entries[] = {
    { "modem", 'm', 0, G_OPTION_ARG_STRING, &ctxt.modem_str,
      "Specify modem by path or index",
      NULL
    },
    { "info", 'i', 0, G_OPTION_ARG_NONE, &ctxt.info_flag,
      "Get information of a given modem",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_get_option_group (void)
{
	GOptionGroup *group;

	/* Status options */
	group = g_option_group_new ("modem",
	                            "Modem options",
	                            "Show modem options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

gboolean
mmcli_modem_options_enabled (void)
{
    guint n_actions;

    n_actions = (ctxt.info_flag);

    if (n_actions > 1) {
        g_printerr ("error: too many modem actions requested\n");
        exit (EXIT_FAILURE);
    }

    return !!n_actions;
}

static void
init (GDBusConnection *connection)
{
    GError *error = NULL;

    /* We must have a given modem specified */
    if (!ctxt.modem_str) {
        g_printerr ("error: no modem was specified\n");
        exit (EXIT_FAILURE);
    }

    /* Modem path may come in two ways: full DBus path or just modem index.
     * If it is a modem index, we'll need to generate the DBus path ourselves */
    if (ctxt.modem_str[0] != '/') {
        if (g_ascii_isdigit (ctxt.modem_str[0])) {
            gchar *tmp;

            tmp = g_strdup_printf (MM_DBUS_PATH "/Modems/%s", ctxt.modem_str);
            g_free (ctxt.modem_str);
            ctxt.modem_str = tmp;
        } else {
            g_printerr ("error: invalid modem string specified: '%s'\n",
                        ctxt.modem_str);
            exit (EXIT_FAILURE);
        }
    }

    /* Create new modem */
    ctxt.modem = mm_modem_new (ctxt.modem_str, connection, NULL, &error);
    if (!ctxt.modem) {
        g_printerr ("error: couldn't find modem '%s': %s\n",
                    ctxt.modem_str,
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }
}

void
mmcli_modem_shutdown (void)
{
    g_free (ctxt.modem_str);
    g_object_unref (ctxt.modem);
}

static void
get_info_process_reply (gboolean      result,
                        const GError *error,
                        const gchar  *manufacturer,
                        const gchar  *model,
                        const gchar  *revision)
{
    if (!result) {
        g_printerr ("couldn't get info from modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("\n"
             "%s\n"
             "  -------------------------\n"
             "  Hardware |  manufacturer: '%s'\n"
             "           |         model: '%s'\n"
             "           |      revision: '%s'\n"
             "\n",
             ctxt.modem_str,
             manufacturer,
             model,
             revision);
}

static void
get_info_ready (MMModem      *modem,
                GAsyncResult *result,
                gpointer      nothing)
{
    gboolean operation_result;
    gchar *manufacturer = NULL;
    gchar *model = NULL;
    gchar *revision = NULL;
    GError *error = NULL;

    operation_result = mm_modem_get_info_finish (modem,
                                                 result,
                                                 &manufacturer,
                                                 &model,
                                                 &revision,
                                                 &error);
    get_info_process_reply (operation_result,
                            error,
                            manufacturer,
                            model,
                            revision);

    g_free (manufacturer);
    g_free (model);
    g_free (revision);

    mmcli_async_operation_done ();
}

gboolean
mmcli_modem_run_asynchronous (GDBusConnection *connection,
                              GCancellable    *cancellable)
{
    gboolean keep_loop = FALSE;

    /* Initialize context */
    init (connection);

    /* Request to get info from modem? */
    if (ctxt.info_flag) {
        mm_modem_get_info_async (ctxt.modem,
                                 cancellable,
                                 (GAsyncReadyCallback)get_info_ready,
                                 NULL);
        return keep_loop;
    }

    g_warn_if_reached ();
    return FALSE;
}

void
mmcli_modem_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    init (connection);

    /* Request to get info from modem? */
    if (ctxt.info_flag) {
        gboolean result;
        gchar *manufacturer = NULL;
        gchar *model = NULL;
        gchar *revision = NULL;

        result = mm_modem_get_info (ctxt.modem,
                                    &manufacturer,
                                    &model,
                                    &revision,
                                    &error);
        get_info_process_reply (result,
                                error,
                                manufacturer,
                                model,
                                revision);

        g_free (manufacturer);
        g_free (model);
        g_free (revision);
        return;
    }

    g_warn_if_reached ();
}
