/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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
 * Copyright (C) 2012 Google, Inc.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

#include "mmcli.h"
#include "mmcli-common.h"
#include "mmcli-output.h"

/* Context */
typedef struct {
    GDBusConnection *connection;
    MMManager *manager;
    GCancellable *cancellable;
    MMObject *object;
    MMModemMessaging *modem_messaging;
} Context;
static Context *ctx;

/* Options */
static gboolean status_flag;
static gboolean list_flag;
static gchar *create_str;
static gchar *create_with_data_str;
static gchar *delete_str;

static GOptionEntry entries[] = {
    { "messaging-status", 0, 0, G_OPTION_ARG_NONE, &status_flag,
      "Show status of messaging support.",
      NULL
    },
    { "messaging-list-sms", 0, 0, G_OPTION_ARG_NONE, &list_flag,
      "List SMS messages available in a given modem",
      NULL
    },
    { "messaging-create-sms", 0, 0, G_OPTION_ARG_STRING, &create_str,
      "Create a new SMS in a given modem",
      "[\"key=value,...\"]"
    },
    { "messaging-create-sms-with-data", 0, 0, G_OPTION_ARG_FILENAME, &create_with_data_str,
      "Pass the given file as data contents when creating a new SMS",
      "[File path]"
    },
    { "messaging-delete-sms", 0, 0, G_OPTION_ARG_STRING, &delete_str,
      "Delete a SMS from a given modem",
      "[PATH|INDEX]"
    },
    { NULL }
};

GOptionGroup *
mmcli_modem_messaging_get_option_group (void)
{
    GOptionGroup *group;

    group = g_option_group_new ("messaging",
                                "Messaging options:",
                                "Show Messaging options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_modem_messaging_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (status_flag +
                 list_flag +
                 !!create_str +
                 !!delete_str);

    if (n_actions > 1) {
        g_printerr ("error: too many Messaging actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (create_with_data_str && !create_str) {
        g_printerr ("error: `--messaging-create-with-data' must be given along "
                    "with `--messaging-create-sms'\n");
        exit (EXIT_FAILURE);
    }

    if (status_flag)
        mmcli_force_sync_operation ();

    checked = TRUE;
    return !!n_actions;
}

static void
context_free (void)
{
    if (!ctx)
        return;

    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->modem_messaging)
        g_object_unref (ctx->modem_messaging);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->connection)
        g_object_unref (ctx->connection);
    g_free (ctx);
}

static void
ensure_modem_messaging (void)
{
    if (mm_modem_get_state (mm_object_peek_modem (ctx->object)) < MM_MODEM_STATE_ENABLED) {
        g_printerr ("error: modem not enabled yet\n");
        exit (EXIT_FAILURE);
    }

    if (!ctx->modem_messaging) {
        g_printerr ("error: modem has no messaging capabilities\n");
        exit (EXIT_FAILURE);
    }

    /* Success */
}

void
mmcli_modem_messaging_shutdown (void)
{
    context_free ();
}

static MMSmsProperties *
build_sms_properties_from_input (const gchar *properties_string,
                                 const gchar *data_file)
{
    GError *error = NULL;
    MMSmsProperties *properties;

    properties = mm_sms_properties_new_from_string (properties_string, &error);
    if (!properties) {
        g_printerr ("error: cannot parse properties string: '%s'\n", error->message);
        exit (EXIT_FAILURE);
    }

    if (data_file) {
        gchar *path;
        GFile *file;
        gchar *contents;
        gsize contents_size;

        g_debug ("Reading data from file '%s'", data_file);

        file = g_file_new_for_commandline_arg (data_file);
        path = g_file_get_path (file);
        if (!g_file_get_contents (path,
                                  &contents,
                                  &contents_size,
                                  &error)) {
            g_printerr ("error: cannot read from file '%s': '%s'\n",
                        data_file, error->message);
            exit (EXIT_FAILURE);
        }
        g_free (path);
        g_object_unref (file);

        mm_sms_properties_set_data (properties, (guint8 *)contents, contents_size);
        g_free (contents);
    }

    return properties;
}

static void
print_messaging_status (void)
{
    MMSmsStorage *supported = NULL;
    guint         supported_len = 0;
    gchar        *supported_str = NULL;

    mm_modem_messaging_get_supported_storages (ctx->modem_messaging, &supported, &supported_len);
    if (supported)
        supported_str = mm_common_build_sms_storages_string (supported, supported_len);
    g_free (supported);

    mmcli_output_string_take (MMC_F_MESSAGING_SUPPORTED_STORAGES, supported_str);
    mmcli_output_string      (MMC_F_MESSAGING_DEFAULT_STORAGES,   mm_sms_storage_get_string (
                                                                    mm_modem_messaging_get_default_storage (ctx->modem_messaging)));
    mmcli_output_dump ();
}

static void
output_sms_info (MMSms *sms)
{
    gchar *extra;

    extra = g_strdup_printf ("(%s)", mm_sms_state_get_string (mm_sms_get_state (sms)));
    mmcli_output_listitem (MMC_F_SMS_LIST_DBUS_PATH,
                           "    ",
                           mm_sms_get_path (sms),
                           extra);
    g_free (extra);
}

static void
list_process_reply (GList        *result,
                    const GError *error)
{
    GList *l;

    if (error) {
        g_printerr ("error: couldn't list SMS: '%s'\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    for (l = result; l; l = g_list_next (l))
        output_sms_info (MM_SMS (l->data));
    mmcli_output_list_dump (MMC_F_SMS_LIST_DBUS_PATH);
}

static void
list_ready (MMModemMessaging *modem,
            GAsyncResult     *result,
            gpointer          nothing)
{
    GList *operation_result;
    GError *error = NULL;

    operation_result = mm_modem_messaging_list_finish (modem, result, &error);
    list_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
create_process_reply (MMSms        *sms,
                      const GError *error)
{
    if (!sms) {
        g_printerr ("error: couldn't create new SMS: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully created new SMS: %s\n", mm_sms_get_path (sms));
    g_object_unref (sms);
}

static void
create_ready (MMModemMessaging *modem,
              GAsyncResult     *result,
              gpointer          nothing)
{
    MMSms *sms;
    GError *error = NULL;

    sms = mm_modem_messaging_create_finish (modem, result, &error);
    create_process_reply (sms, error);

    mmcli_async_operation_done ();
}

static void
delete_process_reply (gboolean      result,
                      const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't delete SMS: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully deleted SMS from modem\n");
}

static void
delete_ready (MMModemMessaging *modem,
              GAsyncResult     *result,
              gpointer          nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_modem_messaging_delete_finish (modem, result, &error);
    delete_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_sms_to_delete_ready (GDBusConnection *connection,
                         GAsyncResult *res)
{
    MMSms *sms;
    MMObject *obj = NULL;

    sms = mmcli_get_sms_finish (res, NULL, &obj);
    if (!g_str_equal (mm_object_get_path (obj), mm_modem_messaging_get_path (ctx->modem_messaging))) {
        g_printerr ("error: SMS '%s' not owned by modem '%s'",
                    mm_sms_get_path (sms),
                    mm_modem_messaging_get_path (ctx->modem_messaging));
        exit (EXIT_FAILURE);
    }

    mm_modem_messaging_delete (ctx->modem_messaging,
                               mm_sms_get_path (sms),
                               ctx->cancellable,
                               (GAsyncReadyCallback)delete_ready,
                               NULL);
    g_object_unref (sms);
    g_object_unref (obj);
}

static void
get_modem_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      none)
{
    ctx->object = mmcli_get_modem_finish (result, &ctx->manager);
    ctx->modem_messaging = mm_object_get_modem_messaging (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_messaging)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_messaging));

    ensure_modem_messaging ();

    if (status_flag)
        g_assert_not_reached ();

    /* Request to list SMS? */
    if (list_flag) {
        g_debug ("Asynchronously listing SMS in modem...");
        mm_modem_messaging_list (ctx->modem_messaging,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)list_ready,
                                 NULL);
        return;
    }

    /* Request to create a new SMS? */
    if (create_str) {
        MMSmsProperties *properties;

        properties = build_sms_properties_from_input (create_str,
                                                      create_with_data_str);
        g_debug ("Asynchronously creating new SMS in modem...");
        mm_modem_messaging_create (ctx->modem_messaging,
                                   properties,
                                   ctx->cancellable,
                                   (GAsyncReadyCallback)create_ready,
                                   NULL);
        g_object_unref (properties);
        return;
    }

    /* Request to delete a given SMS? */
    if (delete_str) {
        mmcli_get_sms (ctx->connection,
                       delete_str,
                       ctx->cancellable,
                       (GAsyncReadyCallback)get_sms_to_delete_ready,
                       NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_modem_messaging_run_asynchronous (GDBusConnection *connection,
                                        GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);
    ctx->connection = g_object_ref (connection);

    /* Get proper modem */
    mmcli_get_modem (connection,
                     mmcli_get_common_modem_string (),
                     cancellable,
                     (GAsyncReadyCallback)get_modem_ready,
                     NULL);
}

void
mmcli_modem_messaging_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->object = mmcli_get_modem_sync (connection,
                                        mmcli_get_common_modem_string (),
                                        &ctx->manager);
    ctx->modem_messaging = mm_object_get_modem_messaging (ctx->object);

    /* Setup operation timeout */
    if (ctx->modem_messaging)
        mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->modem_messaging));

    ensure_modem_messaging ();

    /* Request to get messaging status? */
    if (status_flag) {
        g_debug ("Printing messaging status...");
        print_messaging_status ();
        return;
    }

    /* Request to list the SMS? */
    if (list_flag) {
        GList *result;

        g_debug ("Synchronously listing SMS messages...");
        result = mm_modem_messaging_list_sync (ctx->modem_messaging, NULL, &error);
        list_process_reply (result, error);
        return;
    }

    /* Request to create a new SMS? */
    if (create_str) {
        MMSms *sms;
        MMSmsProperties *properties;

        properties = build_sms_properties_from_input (create_str,
                                                      create_with_data_str);
        g_debug ("Synchronously creating new SMS in modem...");
        sms = mm_modem_messaging_create_sync (ctx->modem_messaging,
                                              properties,
                                              NULL,
                                              &error);
        g_object_unref (properties);

        create_process_reply (sms, error);
        return;
    }

    /* Request to delete a given SMS? */
    if (delete_str) {
        gboolean result;
        MMSms *sms;
        MMObject *obj = NULL;

        sms = mmcli_get_sms_sync (connection,
                                  delete_str,
                                  NULL,
                                  &obj);
        if (!g_str_equal (mm_object_get_path (obj), mm_modem_messaging_get_path (ctx->modem_messaging))) {
            g_printerr ("error: SMS '%s' not owned by modem '%s'",
                        mm_sms_get_path (sms),
                        mm_modem_messaging_get_path (ctx->modem_messaging));
            exit (EXIT_FAILURE);
        }

        result = mm_modem_messaging_delete_sync (ctx->modem_messaging,
                                                 mm_sms_get_path (sms),
                                                 NULL,
                                                 &error);
        g_object_unref (sms);
        g_object_unref (obj);

        delete_process_reply (result, error);
        return;
    }

    g_warn_if_reached ();
}
