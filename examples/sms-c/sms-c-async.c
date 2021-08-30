/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libmm-glib.h>

typedef struct {
    GMainLoop        *loop;
    GDBusConnection  *connection;
    MMManager        *manager;
    GList            *objects;
    MMSmsProperties  *properties;

    MMObject         *current_obj;
    MMModemMessaging *current_messaging;
    MMSms            *current_sms;
} Context;

static void send_sms_next (Context *context);

static void
sms_send_ready (MMSms        *sms,
                GAsyncResult *res,
                Context      *context)
{
    g_autoptr(GError) error = NULL;

    if (!mm_sms_send_finish (sms, res, &error))
        g_printerr ("error: couldn't send sms in modem %s: %s\n",
                    mm_object_get_path (context->current_obj),
                    error->message);
    else
        g_print ("successfully sent sms in modem %s\n",
                 mm_object_get_path (context->current_obj));

    send_sms_next (context);
}

static void
messaging_create_ready (MMModemMessaging *messaging,
                        GAsyncResult     *res,
                        Context          *context)
{
    g_autoptr(GError) error = NULL;

    context->current_sms = mm_modem_messaging_create_finish (messaging, res, &error);
    if (!context->current_sms) {
        g_printerr ("error: couldn't create sms in modem %s: %s\n",
                    mm_object_get_path (context->current_obj),
                    error->message);
        send_sms_next (context);
        return;
    }

    mm_sms_send (context->current_sms,
                 NULL,
                 (GAsyncReadyCallback) sms_send_ready,
                 context);
}

static void
send_sms_next (Context *context)
{
    g_clear_object (&context->current_sms);
    g_clear_object (&context->current_messaging);
    g_clear_object (&context->current_obj);

    if (!context->objects) {
        g_main_loop_quit (context->loop);
        return;
    }

    context->current_obj = context->objects->data;
    context->objects = g_list_delete_link (context->objects, context->objects);

    context->current_messaging = mm_object_get_modem_messaging (context->current_obj);
    if (!context->current_messaging) {
        g_printerr ("error: modem %s does not have messaging capabilities\n",
                    mm_object_get_path (context->current_obj));
        send_sms_next (context);
        return;
    }

    mm_modem_messaging_create (context->current_messaging,
                               context->properties,
                               NULL,
                               (GAsyncReadyCallback)messaging_create_ready,
                               context);
}

static void
manager_new_ready (GObject      *source,
                   GAsyncResult *res,
                   Context      *context)
{
    g_autofree gchar *name_owner = NULL;
    g_autoptr(GError) error = NULL;

    context->manager = mm_manager_new_finish (res, &error);
    if (!context->connection) {
        g_printerr ("error: couldn't get manager: %s\n", error->message);
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (context->manager));
    if (!name_owner) {
        g_printerr ("error: ModemManager not found in the system bus\n");
        exit (EXIT_FAILURE);
    }

    context->objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (context->manager));
    if (!context->objects) {
        g_printerr ("error: no modems found\n");
        exit (EXIT_FAILURE);
    }

    send_sms_next (context);
}

static void
bus_get_ready (GObject      *source,
               GAsyncResult *res,
               Context      *context)
{
    g_autoptr(GError) error = NULL;

    context->connection = g_bus_get_finish (res, &error);
    if (!context->connection) {
        g_printerr ("error: couldn't get bus: %s\n", error->message);
        exit (EXIT_FAILURE);
    }

    mm_manager_new (context->connection,
                    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                    NULL,
                    (GAsyncReadyCallback) manager_new_ready,
                    context);
}

int main (int argc, char **argv)
{
    Context context = { 0 };

    if (argc < 3) {
        g_printerr ("error: missing arguments\n");
        g_printerr ("usage: %s <NUMBER> <TEXT>\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    context.properties = mm_sms_properties_new ();
	mm_sms_properties_set_number (context.properties, argv[1]);
	mm_sms_properties_set_text   (context.properties, argv[2]);

    g_bus_get (G_BUS_TYPE_SYSTEM,
               NULL,
               (GAsyncReadyCallback) bus_get_ready,
               &context);

    context.loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (context.loop);

    g_assert (!context.current_obj);
    g_assert (!context.current_messaging);
    g_assert (!context.current_sms);

    g_main_loop_unref (context.loop);
    g_clear_object (&context.connection);
    g_clear_object (&context.manager);
    g_clear_object (&context.properties);
    g_list_free_full (g_steal_pointer (&context.objects), g_object_unref);

    return 0;
}
