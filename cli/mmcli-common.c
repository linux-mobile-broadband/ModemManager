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

#include <stdlib.h>

#include <libmm-glib.h>

#include "mmcli-common.h"

static void
manager_new_ready (GDBusConnection *connection,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    MMManager *manager;
    gchar *name_owner;
    GError *error = NULL;

    manager = mm_manager_new_finish (res, &error);
    if (!manager) {
        g_printerr ("error: couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (manager));
    if (!name_owner) {
        g_printerr ("error: couldn't find the ModemManager process in the bus\n");
        exit (EXIT_FAILURE);
    }

    g_debug ("ModemManager process found at '%s'", name_owner);
    g_free (name_owner);

    g_simple_async_result_set_op_res_gpointer (simple, manager, NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

MMManager *
mmcli_get_manager_finish (GAsyncResult *res)
{
    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

void
mmcli_get_manager (GDBusConnection *connection,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (connection),
                                        callback,
                                        user_data,
                                        mmcli_get_manager);
    mm_manager_new (connection,
                    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                    cancellable,
                    (GAsyncReadyCallback)manager_new_ready,
                    result);
}

MMManager *
mmcli_get_manager_sync (GDBusConnection *connection)
{
    MMManager *manager;
    gchar *name_owner;
    GError *error = NULL;

    manager = mm_manager_new_sync (connection,
                                   G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                   NULL,
                                   &error);
    if (!manager) {
        g_printerr ("error: couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (manager));
    if (!name_owner) {
        g_printerr ("error: couldn't find the ModemManager process in the bus\n");
        exit (EXIT_FAILURE);
    }

    g_debug ("ModemManager process found at '%s'", name_owner);
    g_free (name_owner);

    return manager;
}

#define MODEM_PATH_TAG "modem-path-tag"

static MMObject *
find_modem (MMManager *manager,
            const gchar *modem_path)
{
    GList *modems;
    GList *l;
    MMObject *found = NULL;

    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    for (l = modems; l; l = g_list_next (l)) {
        MMObject *modem = MM_OBJECT (l->data);

        if (g_str_equal (mm_object_get_path (modem), modem_path)) {
            found = g_object_ref (modem);
            break;
        }
    }
    g_list_foreach (modems, (GFunc)g_object_unref, NULL);
    g_list_free (modems);

    if (!found) {
        g_printerr ("error: couldn't find modem at '%s'\n", modem_path);
        exit (EXIT_FAILURE);
    }

    g_debug ("Modem found at '%s'\n", modem_path);

    return found;
}

static gchar *
get_modem_path (const gchar *modem_str)
{
    gchar *modem_path;

    /* We must have a given modem specified */
    if (!modem_str) {
        g_printerr ("error: no modem was specified\n");
        exit (EXIT_FAILURE);
    }

    /* Modem path may come in two ways: full DBus path or just modem index.
     * If it is a modem index, we'll need to generate the DBus path ourselves */
    if (modem_str[0] == '/')
        modem_path = g_strdup (modem_str);
    else {
        if (g_ascii_isdigit (modem_str[0]))
            modem_path = g_strdup_printf (MM_DBUS_PATH "/Modems/%s", modem_str);
        else {
            g_printerr ("error: invalid modem string specified: '%s'\n",
                        modem_str);
            exit (EXIT_FAILURE);
        }
    }

    return modem_path;
}

static void
get_manager_ready (GDBusConnection *connection,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    MMManager *manager;
    MMObject *found;
    const gchar *modem_path;

    manager = mmcli_get_manager_finish (res);
    modem_path = g_object_get_data (G_OBJECT (simple), MODEM_PATH_TAG);
    found = find_modem (manager, modem_path);
    g_object_unref (manager);

    g_simple_async_result_set_op_res_gpointer (simple, found, NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

MMObject *
mmcli_get_modem_finish (GAsyncResult *res)
{
    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

void
mmcli_get_modem (GDBusConnection *connection,
                 const gchar *modem_str,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    GSimpleAsyncResult *result;
    gchar *modem_path;

    modem_path = get_modem_path (modem_str);
    result = g_simple_async_result_new (G_OBJECT (connection),
                                        callback,
                                        user_data,
                                        mmcli_get_modem);
    g_object_set_data_full (G_OBJECT (result),
                            MODEM_PATH_TAG,
                            modem_path,
                            g_free);

    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_manager_ready,
                       result);
}

MMObject *
mmcli_get_modem_sync (GDBusConnection *connection,
                      const gchar *modem_str)
{
    MMManager *manager;
    MMObject *found;
    gchar *modem_path;

    manager = mmcli_get_manager_sync (connection);
    modem_path = get_modem_path (modem_str);

    found = find_modem (manager, modem_path);
    g_object_unref (manager);
    g_free (modem_path);

    return found;
}

const gchar *
mmcli_get_state_string (MMModemState state)
{
    static GEnumClass *enum_class = NULL;
    GEnumValue *value;

    if (!enum_class)
        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_STATE));

    value = g_enum_get_value (enum_class, state);
    return value->value_nick;
}

const gchar *
mmcli_get_state_reason_string (MMModemStateChangeReason reason)
{
    switch (reason) {
    case MM_MODEM_STATE_CHANGE_REASON_UNKNOWN:
        return "None or unknown";
    case MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED:
        return "User request";
    case MM_MODEM_STATE_CHANGE_REASON_SUSPEND:
        return "Suspend";
    }

    g_warn_if_reached ();
    return NULL;
}

const gchar *
mmcli_get_lock_string (MMModemLock lock)
{
    static GEnumClass *enum_class = NULL;
    GEnumValue *value;

    if (!enum_class)
        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_LOCK));

    value = g_enum_get_value (enum_class, lock);
    return value->value_nick;
}
