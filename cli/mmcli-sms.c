/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control sms status & access information from the command line
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
    MMManager *manager;
    MMObject *object;
    GCancellable *cancellable;
    MMSms *sms;
} Context;
static Context *ctx;

/* Options */
static gboolean info_flag; /* set when no action found */
static gboolean send_flag;
static gboolean store_flag;
static gchar *store_in_storage_str;
static gchar *create_file_with_data_str;

static GOptionEntry entries[] = {
    { "send", 0, 0, G_OPTION_ARG_NONE, &send_flag,
      "Send SMS.",
      NULL,
    },
    { "store", 0, 0, G_OPTION_ARG_NONE, &store_flag,
      "Store the SMS in the device, at the default storage",
      NULL,
    },
    { "store-in-storage", 0, 0, G_OPTION_ARG_STRING, &store_in_storage_str,
      "Store the SMS in the device, at the specified storage",
      "[Storage]",
    },
    { "create-file-with-data", 0, 0, G_OPTION_ARG_STRING, &create_file_with_data_str,
      "Create a file with the data contents of the SMS.",
      "[File path]",
    },
    { NULL }
};

GOptionGroup *
mmcli_sms_get_option_group (void)
{
    GOptionGroup *group;

    /* Status options */
    group = g_option_group_new ("sms",
                                "SMS options:",
                                "Show SMS options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_sms_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (send_flag +
                 store_flag +
                 !!store_in_storage_str +
                 !!create_file_with_data_str);

    if (n_actions == 0 && mmcli_get_common_sms_string ()) {
        /* default to info */
        info_flag = TRUE;
        n_actions++;
    }

    if (n_actions > 1) {
        g_printerr ("error: too many SMS actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (info_flag)
        mmcli_force_sync_operation ();

    if (create_file_with_data_str)
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
    if (ctx->sms)
        g_object_unref (ctx->sms);
    if (ctx->object)
        g_object_unref (ctx->object);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx);
}

void
mmcli_sms_shutdown (void)
{
    context_free ();
}

static void
print_sms_info (MMSms *sms)
{
    MMSmsPduType  pdu_type;
    gchar        *data = NULL;
    const guint8 *databin;
    gsize         databin_size;
    gchar        *validity = NULL;
    gchar        *class = NULL;
    const gchar  *delivery_report = NULL;
    gchar        *message_reference = NULL;
    const gchar  *delivery_state = NULL;

    databin = mm_sms_get_data (sms, &databin_size);
    if (databin)
        data = mm_utils_bin2hexstr (databin, databin_size);
    if (mm_sms_get_validity_type (sms) == MM_SMS_VALIDITY_TYPE_RELATIVE)
        validity = g_strdup_printf ("%u", mm_sms_get_validity_relative (sms));
    if (mm_sms_get_class (sms) >= 0)
        class = g_strdup_printf ("%d", mm_sms_get_class (sms));
    pdu_type = mm_sms_get_pdu_type (sms);
    if (pdu_type == MM_SMS_PDU_TYPE_SUBMIT)
        delivery_report = mm_sms_get_delivery_report_request (sms) ? "requested" : "not requested";
    if (mm_sms_get_message_reference (sms) != 0)
        message_reference = g_strdup_printf ("%u", mm_sms_get_message_reference (sms));
    if (mm_sms_get_delivery_state (sms) != MM_SMS_DELIVERY_STATE_UNKNOWN)
        delivery_state = mm_sms_delivery_state_get_string_extended (mm_sms_get_delivery_state (sms));

    mmcli_output_string           (MMC_F_SMS_GENERAL_DBUS_PATH,           mm_sms_get_path (sms));
    mmcli_output_string           (MMC_F_SMS_CONTENT_NUMBER,              mm_sms_get_number (sms));
    mmcli_output_string (MMC_F_SMS_CONTENT_TEXT,                mm_sms_get_text (sms));
    mmcli_output_string_take      (MMC_F_SMS_CONTENT_DATA,                data);
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_PDU_TYPE,         mm_sms_pdu_type_get_string (pdu_type));
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_STATE,            mm_sms_state_get_string (mm_sms_get_state (sms)));
    mmcli_output_string_take      (MMC_F_SMS_PROPERTIES_VALIDITY,         validity);
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_STORAGE,          mm_sms_storage_get_string (mm_sms_get_storage (sms)));
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_SMSC,             mm_sms_get_smsc (sms));
    mmcli_output_string_take      (MMC_F_SMS_PROPERTIES_CLASS,            class);
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_TELESERVICE_ID,   mm_sms_cdma_teleservice_id_get_string (mm_sms_get_teleservice_id (sms)));
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_SERVICE_CATEGORY, mm_sms_cdma_service_category_get_string (mm_sms_get_service_category (sms)));
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_DELIVERY_REPORT,  delivery_report);
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_MSG_REFERENCE,    message_reference);
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_TIMESTAMP,        mm_sms_get_timestamp (sms));
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_DELIVERY_STATE,   delivery_state);
    mmcli_output_string           (MMC_F_SMS_PROPERTIES_DISCH_TIMESTAMP,  mm_sms_get_discharge_timestamp (sms));
    mmcli_output_dump ();
}

static void
create_file_with_data (MMSms *sms,
                       const gchar *input_path_str)
{
    GError *error = NULL;
    gchar *path;
    GFile *file;
    const guint8 *data;
    gsize data_size;

    file = g_file_new_for_commandline_arg (input_path_str);
    path = g_file_get_path (file);

    data = mm_sms_get_data (sms, &data_size);
    if (!data) {
        g_printerr ("error: couldn't create file: SMS has no data\n");
        exit (EXIT_FAILURE);
    }

    if (!g_file_set_contents (path,
                              (const gchar *)data,
                              data_size,
                              &error)) {
        g_printerr ("error: cannot write to file '%s': '%s'\n",
                    input_path_str, error->message);
        exit (EXIT_FAILURE);
    }

    g_free (path);
    g_object_unref (file);
}

static void
send_process_reply (gboolean      result,
                    const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't send the SMS: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully sent the SMS\n");
}

static void
send_ready (MMSms        *sms,
            GAsyncResult *result,
            gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_sms_send_finish (sms, result, &error);
    send_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
store_process_reply (gboolean      result,
                     const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't store the SMS: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully stored the SMS\n");
}

static void
store_ready (MMSms        *sms,
             GAsyncResult *result,
             gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_sms_store_finish (sms, result, &error);
    store_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
get_sms_ready (GObject      *source,
               GAsyncResult *result,
               gpointer      none)
{
    ctx->sms = mmcli_get_sms_finish (result,
                                     &ctx->manager,
                                     &ctx->object);
    /* Setup operation timeout */
    mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->sms));

    if (info_flag)
        g_assert_not_reached ();

    if (create_file_with_data_str)
        g_assert_not_reached ();

    /* Requesting to send the SMS? */
    if (send_flag) {
        mm_sms_send (ctx->sms,
                     ctx->cancellable,
                     (GAsyncReadyCallback)send_ready,
                     NULL);
        return;
    }

    /* Requesting to store the SMS? */
    if (store_flag) {
        mm_sms_store (ctx->sms,
                      MM_SMS_STORAGE_UNKNOWN,
                      ctx->cancellable,
                      (GAsyncReadyCallback)store_ready,
                      NULL);
        return;
    }

    /* Requesting to store the SMS in a specific storage? */
    if (store_in_storage_str) {
        MMSmsStorage storage;
        GError *error = NULL;

        storage = mm_common_get_sms_storage_from_string (store_in_storage_str, &error);
        if (error) {
            g_printerr ("error: couldn't store the SMS: '%s'\n",
                        error->message);
            exit (EXIT_FAILURE);
        }

        mm_sms_store (ctx->sms,
                      storage,
                      ctx->cancellable,
                      (GAsyncReadyCallback)store_ready,
                      NULL);
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_sms_run_asynchronous (GDBusConnection *connection,
                            GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Get proper sms */
    mmcli_get_sms (connection,
                   mmcli_get_common_sms_string (),
                   cancellable,
                   (GAsyncReadyCallback)get_sms_ready,
                   NULL);
}

void
mmcli_sms_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->sms = mmcli_get_sms_sync (connection,
                                   mmcli_get_common_sms_string (),
                                   &ctx->manager,
                                   &ctx->object);

    /* Setup operation timeout */
    mmcli_force_operation_timeout (G_DBUS_PROXY (ctx->sms));

    /* Request to get info from SMS? */
    if (info_flag) {
        g_debug ("Printing SMS info...");
        print_sms_info (ctx->sms);
        return;
    }

    /* Request to create a new file with the data from the SMS? */
    if (create_file_with_data_str) {
        g_debug ("Creating file with SMS data...");
        create_file_with_data (ctx->sms, create_file_with_data_str);
        return;
    }

    /* Requesting to send the SMS? */
    if (send_flag) {
        gboolean operation_result;

        operation_result = mm_sms_send_sync (ctx->sms,
                                             NULL,
                                             &error);
        send_process_reply (operation_result, error);
        return;
    }

    /* Requesting to store the SMS? */
    if (store_flag) {
        gboolean operation_result;

        operation_result = mm_sms_store_sync (ctx->sms,
                                              MM_SMS_STORAGE_UNKNOWN,
                                              NULL,
                                              &error);
        store_process_reply (operation_result, error);
        return;
    }

    /* Requesting to store the SMS in a specific storage? */
    if (store_in_storage_str) {
        gboolean operation_result;
        MMSmsStorage storage;

        storage = mm_common_get_sms_storage_from_string (store_in_storage_str, &error);
        if (error) {
            g_printerr ("error: couldn't store the SMS: '%s'\n",
                        error->message);
            exit (EXIT_FAILURE);
        }

        operation_result = mm_sms_store_sync (ctx->sms,
                                              storage,
                                              NULL,
                                              &error);
        store_process_reply (operation_result, error);
        return;
    }

    g_warn_if_reached ();
}
