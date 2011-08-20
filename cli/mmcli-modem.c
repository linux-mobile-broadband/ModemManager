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
    gboolean monitor_state_flag;
    gboolean enable_flag;
    gboolean disable_flag;
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
    { "monitor-state", 'f', 0, G_OPTION_ARG_NONE, &ctxt.monitor_state_flag,
      "Monitor state of a given modem",
      NULL
    },
    { "enable", 'e', 0, G_OPTION_ARG_NONE, &ctxt.enable_flag,
      "Enable a given modem",
      NULL
    },
    { "disable", 'd', 0, G_OPTION_ARG_NONE, &ctxt.disable_flag,
      "Disable a given modem",
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

    n_actions = (ctxt.info_flag +
                 ctxt.monitor_state_flag +
                 ctxt.enable_flag +
                 ctxt.disable_flag);

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

static gchar *
prefix_newlines (const gchar *prefix,
                 const gchar *str)
{
    GString *prefixed_string = NULL;
    const gchar *line_start = str;
    const gchar *line_end;

    while ((line_end = strchr (line_start, '\n'))) {
        gssize line_length;

        line_length = line_end - line_start;
        if (line_start[line_length - 1] == '\r')
            line_length--;

        if (line_length > 0) {
            if (prefixed_string) {
                /* If not the first line, add the prefix */
                g_string_append_printf (prefixed_string,
                                        "\n%s", prefix);
            } else {
                prefixed_string = g_string_new ("");
            }

            g_string_append_len (prefixed_string,
                                 line_start,
                                 line_length);
        }

        line_start = line_end + 1;
    }

    return (prefixed_string ?
            g_string_free (prefixed_string, FALSE) :
            NULL);
}

static const gchar *
get_ip_method_string (MMModemIpMethod ip_method)
{
    switch (ip_method) {
    case MM_MODEM_IP_METHOD_PPP:
        return "PPP";
    case MM_MODEM_IP_METHOD_STATIC:
        return "Static";
    case MM_MODEM_IP_METHOD_DHCP:
        return "DHCP";
    }

    g_warn_if_reached ();
    return NULL;
}

static const gchar *
get_modem_type_string (MMModemType type)
{
    switch (type) {
    case MM_MODEM_TYPE_UNKNOWN:
        return "Unknown";
    case MM_MODEM_TYPE_GSM:
        return "GSM";
    case MM_MODEM_TYPE_CDMA:
        return "CDMA";
    }

    g_warn_if_reached ();
    return NULL;
}

static const gchar *
get_state_string (MMModemState state)
{
    switch (state) {
    case MM_MODEM_STATE_UNKNOWN:
        return "Unknown";
    case MM_MODEM_STATE_DISABLED:
        return "Disabled";
    case MM_MODEM_STATE_DISABLING:
        return "Disabling";
    case MM_MODEM_STATE_ENABLING:
        return "Enabling";
    case MM_MODEM_STATE_ENABLED:
        return "Enabled";
    case MM_MODEM_STATE_SEARCHING:
        return "Searching";
    case MM_MODEM_STATE_REGISTERED:
        return "Registered";
    case MM_MODEM_STATE_DISCONNECTING:
        return "Disconnecting";
    case MM_MODEM_STATE_CONNECTING:
        return "Connecting";
    case MM_MODEM_STATE_CONNECTED:
        return "Connected";
    }

    g_warn_if_reached ();
    return NULL;
}

static const gchar *
get_state_reason_string (MMModemStateReason reason)
{
    switch (reason) {
    case MM_MODEM_STATE_REASON_NONE:
        return "None or unknown";
    case MM_MODEM_STATE_REASON_USER_REQUESTED:
        return "User request";
    case MM_MODEM_STATE_REASON_SUSPEND:
        return "Suspend";
    }

    g_warn_if_reached ();
    return NULL;
}

static void
get_info_process_reply (gboolean      result,
                        const GError *error,
                        const gchar  *manufacturer,
                        const gchar  *model,
                        const gchar  *revision)
{
    gchar *prefixed_revision;
    gchar *master_device;
    gchar *device;
    gchar *device_id;
    gchar *equipment_id;
    gchar *driver;
    gchar *plugin;
    MMModemType type;
    gboolean enabled;
    gchar *unlock_required;
    guint32 unlock_retries;
    gchar *unlock;
    MMModemIpMethod ip_method;
    MMModemState state;

    if (!result) {
        g_printerr ("couldn't get info from modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    /* Get additional info from properties */
    master_device = mm_modem_get_master_device (ctxt.modem);
    device = mm_modem_get_device (ctxt.modem);
    device_id = mm_modem_get_device_identifier (ctxt.modem);
    equipment_id = mm_modem_get_equipment_identifier (ctxt.modem);
    driver = mm_modem_get_driver (ctxt.modem);
    plugin = mm_modem_get_plugin (ctxt.modem);
    type = mm_modem_get_modem_type (ctxt.modem);
    enabled = mm_modem_get_enabled (ctxt.modem);
    unlock_required = mm_modem_get_unlock_required (ctxt.modem);
    unlock_retries = mm_modem_get_unlock_retries (ctxt.modem);
    ip_method = mm_modem_get_ip_method (ctxt.modem);
    state = mm_modem_get_state (ctxt.modem);

    /* Strings with mixed properties */
    unlock = (unlock_required ?
              g_strdup_printf ("%s (%u retries)",
                               unlock_required,
                               unlock_retries) :
              g_strdup ("not required"));

    /* Rework possible multiline strings */
    prefixed_revision = prefix_newlines ("           |                 ",
                                         revision);

    g_print ("\n"
             "%s\n"
             "  -------------------------\n"
             "  Hardware |  manufacturer: '%s'\n"
             "           |         model: '%s'\n"
             "           |      revision: '%s'\n"
             "           |          type: '%s'\n"
             "  -------------------------\n"
             "  System   | master device: '%s'\n"
             "           |        device: '%s'\n"
             "           |     device id: '%s'\n"
             "           |  equipment id: '%s'\n"
             "           |        driver: '%s'\n"
             "           |        plugin: '%s'\n"
             "  -------------------------\n"
             "  Status   |       enabled: '%s'\n"
             "           |        unlock: '%s'\n"
             "           |     IP method: '%s'\n"
             "           |         state: '%s'\n"
             "\n",
             ctxt.modem_str,
             manufacturer,
             model,
             prefixed_revision ? prefixed_revision : revision,
             get_modem_type_string (type),
             master_device,
             device,
             device_id,
             equipment_id,
             driver,
             plugin,
             enabled ? "yes" : "no",
             unlock,
             get_ip_method_string (ip_method),
             get_state_string (state));

    g_free (prefixed_revision);
    g_free (master_device);
    g_free (device);
    g_free (device_id);
    g_free (equipment_id);
    g_free (driver);
    g_free (plugin);
    g_free (unlock_required);
    g_free (unlock);
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

static void
enable_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't enable the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully enabled the modem\n");
}

static void
enable_ready (MMModem      *modem,
              GAsyncResult *result,
              gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_enable_finish (modem,
                                               result,
                                               &error);
    enable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
disable_process_reply (gboolean      result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't disable the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully disabled the modem\n");
}

static void
disable_ready (MMModem      *modem,
               GAsyncResult *result,
               gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_disable_finish (modem,
                                                result,
                                                &error);
    disable_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
state_changed (MMModem            *modem,
               MMModemState        old_state,
               MMModemState        new_state,
               MMModemStateReason  reason)
{
    g_print ("State changed: '%s' --> '%s' (Reason: %s)\n",
             get_state_string (old_state),
             get_state_string (new_state),
             get_state_reason_string (reason));
    fflush (stdout);
}

gboolean
mmcli_modem_run_asynchronous (GDBusConnection *connection,
                              GCancellable    *cancellable)
{
    /* Initialize context */
    init (connection);

    /* Request to get info from modem? */
    if (ctxt.info_flag) {
        mm_modem_get_info_async (ctxt.modem,
                                 cancellable,
                                 (GAsyncReadyCallback)get_info_ready,
                                 NULL);
        return FALSE;
    }

    /* Request to monitor modems? */
    if (ctxt.monitor_state_flag) {
        MMModemState current;

        g_signal_connect (ctxt.modem,
                          "state-changed",
                          G_CALLBACK (state_changed),
                          NULL);

        current = mm_modem_get_state (ctxt.modem);
        g_print ("Initial state: '%s'\n", get_state_string (current));

        /* We need to keep the loop */
        return TRUE;
    }

    /* Request to enable the modem? */
    if (ctxt.enable_flag) {
        mm_modem_enable_async (ctxt.modem,
                               cancellable,
                               (GAsyncReadyCallback)enable_ready,
                               NULL);
        return FALSE;
    }

    /* Request to disable the modem? */
    if (ctxt.disable_flag) {
        mm_modem_disable_async (ctxt.modem,
                                cancellable,
                                (GAsyncReadyCallback)disable_ready,
                                NULL);
        return FALSE;
    }

    g_warn_if_reached ();
    return FALSE;
}

void
mmcli_modem_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    if (ctxt.monitor_state_flag) {
        g_printerr ("error: monitoring state cannot be done synchronously\n");
        exit (EXIT_FAILURE);
    }

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

    /* Request to enable the modem? */
    if (ctxt.enable_flag) {
        gboolean result;

        result = mm_modem_enable (ctxt.modem, &error);
        enable_process_reply (result, error);
        return;
    }

    /* Request to disable the modem? */
    if (ctxt.disable_flag) {
        gboolean result;

        result = mm_modem_disable (ctxt.modem, &error);
        disable_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
