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

#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"

/* Context */
typedef struct {
    MMManager *manager;
    GCancellable *cancellable;
    MMObject *object;
    MMModemSimple *modem_simple;
} Context;
static Context *ctx;

/* Options */
static gchar *connect_str;

static GOptionEntry entries[] = {
    { "simple-connect", 0, 0, G_OPTION_ARG_STRING, &connect_str,
      "Run full connection sequence.",
      "[\"key=value,...\"]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_simple_get_option_group (void)
{
	GOptionGroup *group;

	group = g_option_group_new ("simple",
	                            "Simple options",
	                            "Show Simple options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

gboolean
mmcli_modem_simple_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (!!connect_str);

    if (n_actions > 1) {
        g_printerr ("error: too many Simple actions requested\n");
        exit (EXIT_FAILURE);
    }

    /* Simple connection may take really a long time, so we do it asynchronously
     * always to avoid DBus timeouts */
    if (connect_str)
        mmcli_force_async_operation ();

    checked = TRUE;
    return !!n_actions;
}

static void
context_free (Context *ctx)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->modem_simple)
        g_object_unref (ctx->modem_simple);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

void
mmcli_modem_simple_shutdown (void)
{
    context_free (ctx);
}

static void
connect_process_reply (MMBearer *result,
                       const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't connect the modem: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully connected the modem\n");
    g_object_unref (result);
}

static void
connect_ready (MMModemSimple  *modem_simple,
               GAsyncResult *result,
               gpointer      nothing)
{
    MMBearer *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_simple_connect_finish (modem_simple, result, &error);
    connect_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

typedef struct {
    gchar       *pin;
    gchar       *operator_id;
    MMModemBand  allowed_bands;
    MMModemMode  allowed_modes;
    MMModemMode  preferred_mode;
    gchar       *apn;
    gchar       *ip_type;
    gboolean     allow_roaming;
    gchar       *number;
} SimpleConnectProperties;

static gboolean
string_get_boolean (const gchar *value)
{
    if (!g_ascii_strcasecmp (value, "true") || g_str_equal (value, "1"))
        return TRUE;

    if (g_ascii_strcasecmp (value, "false") && g_str_equal (value, "0"))
        g_printerr ("error: value '%s' is not boolean", value);

    return FALSE;
}

static void
simple_connect_properties_shutdown (SimpleConnectProperties *properties)
{
    g_free (properties->pin);
    g_free (properties->operator_id);
    g_free (properties->apn);
    g_free (properties->ip_type);
    g_free (properties->number);
}

static void
simple_connect_properties_init (const gchar  *input,
                                SimpleConnectProperties *properties)
{
    gchar **words;
    gchar *key;
    gchar *value;
    guint i;

    /* Some defaults... */
    memset (properties, 0, sizeof (*properties));
    properties->allow_roaming = TRUE;
    properties->allowed_bands = MM_MODEM_BAND_ANY;
    properties->allowed_modes = MM_MODEM_MODE_ANY;
    properties->preferred_mode = MM_MODEM_MODE_NONE;

    /* Expecting input as:
     *   key1=string,key2=true,key3=false...
     * */

    words = g_strsplit_set (input, ",= ", -1);
    if (!words)
        return;

    i = 0;
    key = words[i];
    while (key) {
        value = words[++i];
        if (!value) {
            g_printerr ("error: invalid properties string, no value for key '%s'\n", key);
            exit (EXIT_FAILURE);
        }

        if (g_str_equal (key, MM_SIMPLE_PROPERTY_PIN)) {
            g_debug ("PIN: %s", value);
            properties->pin = value;
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_OPERATOR_ID)) {
            g_debug ("Operator ID: %s", value);
            properties->operator_id = value;
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_APN)) {
            g_debug ("APN: %s", value);
            properties->apn = value;
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_IP_TYPE)) {
            g_debug ("IP type: %s", value);
            properties->ip_type = value;
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_ALLOW_ROAMING)) {
            g_debug ("Allow Roaming: %s", value);
            properties->allow_roaming = string_get_boolean (value);
            g_free (value);
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_NUMBER)) {
            g_debug ("Number: %s", value);
            properties->number = value;
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_ALLOWED_BANDS)) {
            g_warning ("Allowed bands: Not supported in the CLI yet");
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_ALLOWED_MODES)) {
            g_warning ("Allowed modes: Not supported in the CLI yet");
        } else if (g_str_equal (key, MM_SIMPLE_PROPERTY_PREFERRED_MODE)) {
            g_warning ("Preferred mode: Not supported in the CLI yet");
        } else {
            g_printerr ("error: invalid key '%s' in properties string\n", key);
            g_free (value);
        }

        g_free (key);
        key = words[++i];
    }

    g_free (words);
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_simple = mm_object_get_modem_simple (ctx->object);

    /* Request to connect the modem? */
    if (connect_str) {
        SimpleConnectProperties properties;

        simple_connect_properties_init (connect_str, &properties);
        g_debug ("Asynchronously connecting the modem...");
        mm_modem_simple_connect (ctx->modem_simple,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)connect_ready,
                                 NULL,
                                 MM_SIMPLE_PROPERTY_PIN,            properties.pin,
                                 MM_SIMPLE_PROPERTY_OPERATOR_ID,    properties.operator_id,
                                 MM_SIMPLE_PROPERTY_ALLOWED_BANDS,  properties.allowed_bands,
                                 MM_SIMPLE_PROPERTY_ALLOWED_MODES,  properties.allowed_modes,
                                 MM_SIMPLE_PROPERTY_PREFERRED_MODE, properties.preferred_mode,
                                 MM_SIMPLE_PROPERTY_APN,            properties.apn,
                                 MM_SIMPLE_PROPERTY_IP_TYPE,        properties.ip_type,
                                 MM_SIMPLE_PROPERTY_NUMBER,         properties.number,
                                 MM_SIMPLE_PROPERTY_ALLOW_ROAMING,  properties.allow_roaming,
                                 NULL);
        simple_connect_properties_shutdown (&properties);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_simple_run_asynchronous (GDBusConnection *connection,
                                     GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper modem */
    mmcli_get_modem (connection,
                     mmcli_get_common_modem_string (),
                     cancellable,
                     (GAsyncReadyCallback)get_modem_ready,
                     NULL);
}

void
mmcli_modem_simple_run_synchronous (GDBusConnection *connection)
{
    /* GError *error = NULL; */

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_simple = mm_object_get_modem_simple (ctx->object);

    if (connect_str)
        g_assert_not_reached ();

    g_warn_if_reached ();
}
