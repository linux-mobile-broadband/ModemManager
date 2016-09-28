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
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2011-2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#if WITH_UDEV
# include <gudev/gudev.h>
#endif

#define _LIBMM_INSIDE_MMCLI
#include "libmm-glib.h"

#include "mmcli.h"
#include "mmcli-common.h"

/* Context */
typedef struct {
    MMManager *manager;
    GCancellable *cancellable;
#if WITH_UDEV
    GUdevClient *udev;
#endif
} Context;
static Context *ctx;

/* Options */
static gboolean list_modems_flag;
static gboolean monitor_modems_flag;
static gboolean scan_modems_flag;
static gchar *set_logging_str;
static gchar *report_kernel_event_str;

#if WITH_UDEV
static gboolean report_kernel_event_auto_scan;
#endif

static GOptionEntry entries[] = {
    { "set-logging", 'G', 0, G_OPTION_ARG_STRING, &set_logging_str,
      "Set logging level in the ModemManager daemon",
      "[ERR,WARN,INFO,DEBUG]",
    },
    { "list-modems", 'L', 0, G_OPTION_ARG_NONE, &list_modems_flag,
      "List available modems",
      NULL
    },
    { "monitor-modems", 'M', 0, G_OPTION_ARG_NONE, &monitor_modems_flag,
      "List available modems and monitor additions and removals",
      NULL
    },
    { "scan-modems", 'S', 0, G_OPTION_ARG_NONE, &scan_modems_flag,
      "Request to re-scan looking for modems",
      NULL
    },
    { "report-kernel-event", 'K', 0, G_OPTION_ARG_STRING, &report_kernel_event_str,
      "Report kernel event",
      "[\"key=value,...\"]"
    },
#if WITH_UDEV
    { "report-kernel-event-auto-scan", 0, 0, G_OPTION_ARG_NONE, &report_kernel_event_auto_scan,
      "Automatically report kernel events based on udev notifications",
      NULL
    },
#endif
    { NULL }
};

GOptionGroup *
mmcli_manager_get_option_group (void)
{
    GOptionGroup *group;

    /* Status options */
    group = g_option_group_new ("manager",
                                "Manager options",
                                "Show manager options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

gboolean
mmcli_manager_options_enabled (void)
{
    static guint n_actions = 0;
    static gboolean checked = FALSE;

    if (checked)
        return !!n_actions;

    n_actions = (list_modems_flag +
                 monitor_modems_flag +
                 scan_modems_flag +
                 !!set_logging_str +
                 !!report_kernel_event_str);

#if WITH_UDEV
    n_actions += report_kernel_event_auto_scan;
#endif

    if (n_actions > 1) {
        g_printerr ("error: too many manager actions requested\n");
        exit (EXIT_FAILURE);
    }

    if (monitor_modems_flag)
        mmcli_force_async_operation ();

#if WITH_UDEV
    if (report_kernel_event_auto_scan)
        mmcli_force_async_operation ();
#endif

    checked = TRUE;
    return !!n_actions;
}

static void
context_free (Context *ctx)
{
    if (!ctx)
        return;

#if WITH_UDEV
    if (ctx->udev)
        g_object_unref (ctx->udev);
#endif

    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx);
}

void
mmcli_manager_shutdown (void)
{
    context_free (ctx);
}

static void
report_kernel_event_process_reply (gboolean      result,
                                   const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't report kernel event: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully reported kernel event\n");
}

static void
report_kernel_event_ready (MMManager    *manager,
                           GAsyncResult *result)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_manager_report_kernel_event_finish (manager, result, &error);
    report_kernel_event_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static MMKernelEventProperties *
build_kernel_event_properties_from_input (const gchar *properties_string)
{
    GError *error = NULL;
    MMKernelEventProperties *properties;

    properties = mm_kernel_event_properties_new_from_string (properties_string, &error);
    if (!properties) {
        g_printerr ("error: cannot parse properties string: '%s'\n", error->message);
        exit (EXIT_FAILURE);
    }

    return properties;
}

static void
set_logging_process_reply (gboolean      result,
                           const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set logging level: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully set logging level\n");
}

static void
set_logging_ready (MMManager    *manager,
                   GAsyncResult *result,
                   gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_manager_set_logging_finish (manager,
                                                      result,
                                                      &error);
    set_logging_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
scan_devices_process_reply (gboolean      result,
                            const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't request to scan devices: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully requested to scan devices\n");
}

static void
scan_devices_ready (MMManager    *manager,
                    GAsyncResult *result,
                    gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_manager_scan_devices_finish (manager,
                                                       result,
                                                       &error);
    scan_devices_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
print_modem_short_info (MMObject *modem)
{
    const gchar *manufacturer, *model;

    manufacturer = mm_modem_get_manufacturer (mm_object_peek_modem (modem));
    model = mm_modem_get_model (mm_object_peek_modem (modem));

    g_print ("\t%s [%s] %s\n",
             mm_object_get_path (modem),
             manufacturer ? manufacturer : "unknown",
             model ? model : "unknown");
}

static void
device_added (MMManager *manager,
              MMObject  *modem)
{
    g_print ("Added modem:\n");
    print_modem_short_info (modem);
    fflush (stdout);
}

static void
device_removed (MMManager *manager,
                MMObject  *modem)
{
    g_print ("Removed modem:\n");
    print_modem_short_info (modem);
    fflush (stdout);
}

static void
list_current_modems (MMManager *manager)
{
    GList *modems;

    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));

    g_print ("\n");
    if (!modems)
        g_print ("No modems were found\n");
    else {
        GList *l;

        g_print ("Found %u modems:\n", g_list_length (modems));
        for (l = modems; l; l = g_list_next (l)) {
            print_modem_short_info (MM_OBJECT (l->data));
        }
        g_list_free_full (modems, (GDestroyNotify) g_object_unref);
    }
    g_print ("\n");
}

static void
cancelled (GCancellable *cancellable)
{
    mmcli_async_operation_done ();
}

#if WITH_UDEV

static void
handle_uevent (GUdevClient *client,
               const char  *action,
               GUdevDevice *device,
               gpointer     none)
{
    MMKernelEventProperties *properties;

    properties = mm_kernel_event_properties_new ();
    mm_kernel_event_properties_set_action (properties, action);
    mm_kernel_event_properties_set_subsystem (properties, g_udev_device_get_subsystem (device));
    mm_kernel_event_properties_set_name (properties, g_udev_device_get_name (device));
    mm_manager_report_kernel_event (ctx->manager, properties, NULL, NULL, NULL);
    g_object_unref (properties);
}

#endif

static void
get_manager_ready (GObject      *source,
                   GAsyncResult *result,
                   gpointer      none)
{
    ctx->manager = mmcli_get_manager_finish (result);

    /* Setup operation timeout */
    mmcli_force_operation_timeout (mm_manager_peek_proxy (ctx->manager));

    /* Request to set log level? */
    if (set_logging_str) {
        mm_manager_set_logging (ctx->manager,
                                set_logging_str,
                                ctx->cancellable,
                                (GAsyncReadyCallback)set_logging_ready,
                                NULL);
        return;
    }

    /* Request to scan modems? */
    if (scan_modems_flag) {
        mm_manager_scan_devices (ctx->manager,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)scan_devices_ready,
                                 NULL);
        return;
    }

    /* Request to report kernel event? */
    if (report_kernel_event_str) {
        MMKernelEventProperties *properties;

        properties = build_kernel_event_properties_from_input (report_kernel_event_str);
        mm_manager_report_kernel_event (ctx->manager,
                                        properties,
                                        ctx->cancellable,
                                        (GAsyncReadyCallback)report_kernel_event_ready,
                                        NULL);
        g_object_unref (properties);
        return;
    }

#if WITH_UDEV
    if (report_kernel_event_auto_scan) {
        const gchar *subsys[] = { "tty", "usbmisc", "net", NULL };
        guint i;

        ctx->udev = g_udev_client_new (subsys);
        g_signal_connect (ctx->udev, "uevent", G_CALLBACK (handle_uevent), NULL);

        for (i = 0; subsys[i]; i++) {
            GList *list, *iter;

            list = g_udev_client_query_by_subsystem (ctx->udev, subsys[i]);
            for (iter = list; iter; iter = g_list_next (iter)) {
                MMKernelEventProperties *properties;
                GUdevDevice *device;

                device = G_UDEV_DEVICE (iter->data);
                properties = mm_kernel_event_properties_new ();
                mm_kernel_event_properties_set_action (properties, "add");
                mm_kernel_event_properties_set_subsystem (properties, subsys[i]);
                mm_kernel_event_properties_set_name (properties, g_udev_device_get_name (device));
                mm_manager_report_kernel_event (ctx->manager, properties, NULL, NULL, NULL);
                g_object_unref (properties);
            }
            g_list_free_full (list, (GDestroyNotify) g_object_unref);
        }

        /* If we get cancelled, operation done */
        g_cancellable_connect (ctx->cancellable,
                               G_CALLBACK (cancelled),
                               NULL,
                               NULL);
        return;
    }
#endif

    /* Request to monitor modems? */
    if (monitor_modems_flag) {
        g_signal_connect (ctx->manager,
                          "object-added",
                          G_CALLBACK (device_added),
                          NULL);
        g_signal_connect (ctx->manager,
                          "object-removed",
                          G_CALLBACK (device_removed),
                          NULL);
        list_current_modems (ctx->manager);

        /* If we get cancelled, operation done */
        g_cancellable_connect (ctx->cancellable,
                               G_CALLBACK (cancelled),
                               NULL,
                               NULL);
        return;
    }

    /* Request to list modems? */
    if (list_modems_flag) {
        list_current_modems (ctx->manager);
        mmcli_async_operation_done ();
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_manager_run_asynchronous (GDBusConnection *connection,
                                GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Create a new Manager object asynchronously */
    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_manager_ready,
                       NULL);
}

void
mmcli_manager_run_synchronous (GDBusConnection *connection)
{
    GError *error = NULL;

    if (monitor_modems_flag) {
        g_printerr ("error: monitoring modems cannot be done synchronously\n");
        exit (EXIT_FAILURE);
    }

#if WITH_UDEV
    if (report_kernel_event_auto_scan) {
        g_printerr ("error: monitoring udev events cannot be done synchronously\n");
        exit (EXIT_FAILURE);
    }
#endif

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->manager = mmcli_get_manager_sync (connection);

    /* Setup operation timeout */
    mmcli_force_operation_timeout (mm_manager_peek_proxy (ctx->manager));

    /* Request to set log level? */
    if (set_logging_str) {
        gboolean result;

        result = mm_manager_set_logging_sync (ctx->manager,
                                              set_logging_str,
                                              NULL,
                                              &error);
        set_logging_process_reply (result, error);
        return;
    }

    /* Request to scan modems? */
    if (scan_modems_flag) {
        gboolean result;

        result = mm_manager_scan_devices_sync (ctx->manager,
                                               NULL,
                                               &error);
        scan_devices_process_reply (result, error);
        return;
    }

    /* Request to report kernel event? */
    if (report_kernel_event_str) {
        MMKernelEventProperties *properties;
        gboolean result;

        properties = build_kernel_event_properties_from_input (report_kernel_event_str);
        result = mm_manager_report_kernel_event_sync (ctx->manager,
                                                      properties,
                                                      NULL,
                                                      &error);
        report_kernel_event_process_reply (result, error);
        return;
    }

    /* Request to list modems? */
    if (list_modems_flag) {
        list_current_modems (ctx->manager);
        return;
    }

    g_warn_if_reached ();
}
