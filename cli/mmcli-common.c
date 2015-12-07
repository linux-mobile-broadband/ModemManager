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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include <stdlib.h>
#include <string.h>

#define _LIBMM_INSIDE_MMCLI
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
                    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
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
                                   G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
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
    g_list_free_full (modems, (GDestroyNotify) g_object_unref);

    if (!found) {
        g_printerr ("error: couldn't find modem at '%s'\n", modem_path);
        exit (EXIT_FAILURE);
    }

    g_debug ("Modem found at '%s'\n", modem_path);

    return found;
}

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar *modem_path;
} GetModemContext;

typedef struct {
    MMManager *manager;
    MMObject *object;
} GetModemResults;

static void
get_modem_results_free (GetModemResults *results)
{
    g_object_unref (results->manager);
    g_object_unref (results->object);
    g_free (results);
}

static void
get_modem_context_complete_and_free (GetModemContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx->modem_path);
    g_free (ctx);
}

MMObject *
mmcli_get_modem_finish (GAsyncResult *res,
                        MMManager **o_manager)
{
    GetModemResults *results;

    results = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    if (o_manager)
        *o_manager = g_object_ref (results->manager);

    return g_object_ref (results->object);
}

static void
get_manager_ready (GDBusConnection *connection,
                   GAsyncResult *res,
                   GetModemContext *ctx)
{
    GetModemResults *results;

    results = g_new (GetModemResults, 1);
    results->manager = mmcli_get_manager_finish (res);
    results->object = find_modem (results->manager, ctx->modem_path);

    /* Set operation results */
    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        results,
        (GDestroyNotify)get_modem_results_free);

    get_modem_context_complete_and_free (ctx);
}

static gchar *
get_modem_path (const gchar *path_or_index)
{
    gchar *modem_path;

    /* We must have a given modem specified */
    if (!path_or_index) {
        g_printerr ("error: no modem was specified\n");
        exit (EXIT_FAILURE);
    }

    /* Modem path may come in two ways: full DBus path or just modem index.
     * If it is a modem index, we'll need to generate the DBus path ourselves */
    if (g_str_has_prefix (path_or_index, MM_DBUS_MODEM_PREFIX)) {
        g_debug ("Assuming '%s' is the full modem path", path_or_index);
        modem_path = g_strdup (path_or_index);
    } else if (g_ascii_isdigit (path_or_index[0])) {
        g_debug ("Assuming '%s' is the modem index", path_or_index);
        modem_path = g_strdup_printf (MM_DBUS_MODEM_PREFIX "/%s", path_or_index);
    } else {
        g_printerr ("error: invalid path or index string specified: '%s'\n",
                    path_or_index);
        exit (EXIT_FAILURE);
    }

    return modem_path;
}

void
mmcli_get_modem (GDBusConnection *connection,
                 const gchar *path_or_index,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    GetModemContext *ctx;

    ctx = g_new0 (GetModemContext, 1);
    ctx->modem_path = get_modem_path (path_or_index);
    ctx->result = g_simple_async_result_new (G_OBJECT (connection),
                                             callback,
                                             user_data,
                                             mmcli_get_modem);

    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_manager_ready,
                       ctx);
}

MMObject *
mmcli_get_modem_sync (GDBusConnection *connection,
                      const gchar *modem_str,
                      MMManager **o_manager)
{
    MMManager *manager;
    MMObject *found;
    gchar *modem_path;

    manager = mmcli_get_manager_sync (connection);
    modem_path = get_modem_path (modem_str);
    found = find_modem (manager, modem_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);
    g_free (modem_path);

    return found;
}

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar *bearer_path;
    MMManager *manager;
    GList *modems;
    MMObject *current;
    MMBearer *bearer;
} GetBearerContext;

static void
get_bearer_context_free (GetBearerContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->bearer)
        g_object_unref (ctx->bearer);
    g_list_free_full (ctx->modems, (GDestroyNotify) g_object_unref);
    g_free (ctx->bearer_path);
    g_free (ctx);
}

static void
get_bearer_context_complete (GetBearerContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    ctx->result = NULL;
}

MMBearer *
mmcli_get_bearer_finish (GAsyncResult *res,
                         MMManager **o_manager,
                         MMObject **o_object)
{
    GetBearerContext *ctx;

    ctx = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    if (o_manager)
        *o_manager = g_object_ref (ctx->manager);
    if (o_object)
        *o_object = g_object_ref (ctx->current);
    return g_object_ref (ctx->bearer);
}

static void look_for_bearer_in_modem (GetBearerContext *ctx);

static MMBearer *
find_bearer_in_list (GList *list,
                     const gchar *bearer_path)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)) {
        MMBearer *bearer = MM_BEARER (l->data);

        if (g_str_equal (mm_bearer_get_path (bearer), bearer_path)) {
            g_debug ("Bearer found at '%s'\n", bearer_path);
            return g_object_ref (bearer);
        }
    }

    return NULL;
}

static void
list_bearers_ready (MMModem *modem,
                    GAsyncResult *res,
                    GetBearerContext *ctx)
{
    GList *bearers;
    GError *error = NULL;

    bearers = mm_modem_list_bearers_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't list bearers at '%s': '%s'\n",
                    mm_modem_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    ctx->bearer = find_bearer_in_list (bearers, ctx->bearer_path);
    g_list_free_full (bearers, (GDestroyNotify) g_object_unref);

    /* Found! */
    if (ctx->bearer) {
        g_simple_async_result_set_op_res_gpointer (
            ctx->result,
            ctx,
            (GDestroyNotify)get_bearer_context_free);
        get_bearer_context_complete (ctx);
        return;
    }

    /* Not found, try with next modem */
    look_for_bearer_in_modem (ctx);
}

static void
look_for_bearer_in_modem (GetBearerContext *ctx)
{
    MMModem *modem;

    if (!ctx->modems) {
        g_printerr ("error: couldn't find bearer at '%s': 'not found in any modem'\n",
                    ctx->bearer_path);
        exit (EXIT_FAILURE);
    }

    /* Loop looking for the bearer in each modem found */
    ctx->current = MM_OBJECT (ctx->modems->data);
    ctx->modems = g_list_delete_link (ctx->modems, ctx->modems);

    modem = mm_object_get_modem (ctx->current);

    /* Don't look for bearers in modems which are not fully initialized */
    if (mm_modem_get_state (modem) < MM_MODEM_STATE_DISABLED) {
        g_debug ("Skipping modem '%s' when looking for bearers "
                 "(not fully initialized)",
                 mm_object_get_path (ctx->current));
        g_object_unref (modem);
        look_for_bearer_in_modem (ctx);
        return;
    }

    g_debug ("Looking for bearer '%s' in modem '%s'...",
             ctx->bearer_path,
             mm_object_get_path (ctx->current));

    mm_modem_list_bearers (modem,
                           ctx->cancellable,
                           (GAsyncReadyCallback)list_bearers_ready,
                           ctx);
    g_object_unref (modem);
}

static void
get_bearer_manager_ready (GDBusConnection *connection,
                          GAsyncResult *res,
                          GetBearerContext *ctx)
{
    ctx->manager = mmcli_get_manager_finish (res);
    ctx->modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));
    if (!ctx->modems) {
        g_printerr ("error: couldn't find bearer at '%s': 'no modems found'\n",
                    ctx->bearer_path);
        exit (EXIT_FAILURE);
    }

    look_for_bearer_in_modem (ctx);
}

static gchar *
get_bearer_path (const gchar *path_or_index)
{
    gchar *bearer_path;

    /* We must have a given bearer specified */
    if (!path_or_index) {
        g_printerr ("error: no bearer was specified\n");
        exit (EXIT_FAILURE);
    }

    /* Bearer path may come in two ways: full DBus path or just bearer index.
     * If it is a bearer index, we'll need to generate the DBus path ourselves */
    if (g_str_has_prefix (path_or_index, MM_DBUS_BEARER_PREFIX)) {
        g_debug ("Assuming '%s' is the full bearer path", path_or_index);
        bearer_path = g_strdup (path_or_index);
    } else if (g_ascii_isdigit (path_or_index[0])) {
        g_debug ("Assuming '%s' is the bearer index", path_or_index);
        bearer_path = g_strdup_printf (MM_DBUS_BEARER_PREFIX "/%s", path_or_index);
    } else {
        g_printerr ("error: invalid path or index string specified: '%s'\n",
                    path_or_index);
        exit (EXIT_FAILURE);
    }

    return bearer_path;
}

void
mmcli_get_bearer (GDBusConnection *connection,
                  const gchar *path_or_index,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GetBearerContext *ctx;

    ctx = g_new0 (GetBearerContext, 1);
    ctx->bearer_path = get_bearer_path (path_or_index);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (connection),
                                             callback,
                                             user_data,
                                             mmcli_get_bearer);
    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_bearer_manager_ready,
                       ctx);
}

MMBearer *
mmcli_get_bearer_sync (GDBusConnection *connection,
                       const gchar *path_or_index,
                       MMManager **o_manager,
                       MMObject **o_object)
{
    MMManager *manager;
    GList *modems;
    GList *l;
    MMBearer *found = NULL;
    gchar *bearer_path;

    bearer_path = get_bearer_path (path_or_index);

    manager = mmcli_get_manager_sync (connection);
    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!modems) {
        g_printerr ("error: couldn't find bearer at '%s': 'no modems found'\n",
                    bearer_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; !found && l; l = g_list_next (l)) {
        GError *error = NULL;
        MMObject *object;
        MMModem *modem;
        GList *bearers;

        object = MM_OBJECT (l->data);
        modem = mm_object_get_modem (object);

        /* Don't look for bearers in modems which are not fully initialized */
        if (mm_modem_get_state (modem) < MM_MODEM_STATE_DISABLED) {
            g_debug ("Skipping modem '%s' when looking for bearers "
                     "(not fully initialized)",
                     mm_object_get_path (object));
            g_object_unref (modem);
            continue;
        }

        bearers = mm_modem_list_bearers_sync (modem, NULL, &error);
        if (error) {
            g_printerr ("error: couldn't list bearers at '%s': '%s'\n",
                        mm_modem_get_path (modem),
                        error->message);
            exit (EXIT_FAILURE);
        }

        found = find_bearer_in_list (bearers, bearer_path);
        g_list_free_full (bearers, (GDestroyNotify) g_object_unref);

        if (found && o_object)
            *o_object = g_object_ref (object);

        g_object_unref (modem);
    }

    if (!found) {
        g_printerr ("error: couldn't find bearer at '%s': 'not found in any modem'\n",
                    bearer_path);
        exit (EXIT_FAILURE);
    }

    g_list_free_full (modems, (GDestroyNotify) g_object_unref);
    g_free (bearer_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);

    return found;
}

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar *sim_path;
    MMManager *manager;
    MMObject *modem;
    MMSim *sim;
} GetSimContext;

static void
get_sim_context_free (GetSimContext *ctx)
{
    if (ctx->modem)
        g_object_unref (ctx->modem);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->sim)
        g_object_unref (ctx->sim);
    g_free (ctx->sim_path);
    g_free (ctx);
}

static void
get_sim_context_complete (GetSimContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    ctx->result = NULL;
}

MMSim *
mmcli_get_sim_finish (GAsyncResult *res,
                      MMManager **o_manager,
                      MMObject **o_object)
{
    GetSimContext *ctx;

    ctx = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    if (o_manager)
        *o_manager = g_object_ref (ctx->manager);
    if (o_object)
        *o_object = g_object_ref (ctx->modem);
    return g_object_ref (ctx->sim);
}

static void
get_sim_ready (MMModem *modem,
               GAsyncResult *res,
               GetSimContext *ctx)
{
    GError *error = NULL;

    ctx->sim = mm_modem_get_sim_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't get sim '%s' at '%s': '%s'\n",
                    ctx->sim_path,
                    mm_modem_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        ctx,
        (GDestroyNotify)get_sim_context_free);
    get_sim_context_complete (ctx);
}

static void
get_sim_manager_ready (GDBusConnection *connection,
                       GAsyncResult *res,
                       GetSimContext *ctx)
{
    GList *l;
    GList *modems;

    ctx->manager = mmcli_get_manager_finish (res);

    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));
    if (!modems) {
        g_printerr ("error: couldn't find sim at '%s': 'no modems found'\n",
                    ctx->sim_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; l; l = g_list_next (l)) {
        MMObject *object;
        MMModem *modem;

        object = MM_OBJECT (l->data);
        modem = mm_object_get_modem (object);
        if (g_str_equal (ctx->sim_path, mm_modem_get_sim_path (modem))) {
            ctx->modem  = g_object_ref (object);
            mm_modem_get_sim (modem,
                              ctx->cancellable,
                              (GAsyncReadyCallback)get_sim_ready,
                              ctx);
            break;
        }
        g_object_unref (modem);
    }

    if (!ctx->modem) {
        g_printerr ("error: couldn't find sim at '%s'\n",
                    ctx->sim_path);
        exit (EXIT_FAILURE);
    }

    g_list_free_full (modems, (GDestroyNotify) g_object_unref);
}

static gchar *
get_sim_path (const gchar *path_or_index)
{
    gchar *sim_path;

    /* We must have a given sim specified */
    if (!path_or_index) {
        g_printerr ("error: no sim was specified\n");
        exit (EXIT_FAILURE);
    }

    /* Sim path may come in two ways: full DBus path or just sim index.
     * If it is a sim index, we'll need to generate the DBus path ourselves */
    if (g_str_has_prefix (path_or_index, MM_DBUS_SIM_PREFIX)) {
        g_debug ("Assuming '%s' is the full SIM path", path_or_index);
        sim_path = g_strdup (path_or_index);
    } else if (g_ascii_isdigit (path_or_index[0])) {
        g_debug ("Assuming '%s' is the SIM index", path_or_index);
        sim_path = g_strdup_printf (MM_DBUS_SIM_PREFIX "/%s", path_or_index);
    } else {
        g_printerr ("error: invalid index string specified: '%s'\n",
                    path_or_index);
        exit (EXIT_FAILURE);
    }

    return sim_path;
}

void
mmcli_get_sim (GDBusConnection *connection,
                  const gchar *path_or_index,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GetSimContext *ctx;

    ctx = g_new0 (GetSimContext, 1);
    ctx->sim_path = get_sim_path (path_or_index);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (connection),
                                             callback,
                                             user_data,
                                             mmcli_get_sim);
    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_sim_manager_ready,
                       ctx);
}

MMSim *
mmcli_get_sim_sync (GDBusConnection *connection,
                    const gchar *path_or_index,
                    MMManager **o_manager,
                    MMObject **o_object)
{
    MMManager *manager;
    GList *modems;
    GList *l;
    MMSim *found = NULL;
    gchar *sim_path;

    sim_path = get_sim_path (path_or_index);

    manager = mmcli_get_manager_sync (connection);
    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!modems) {
        g_printerr ("error: couldn't find sim at '%s': 'no modems found'\n",
                    sim_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; !found && l; l = g_list_next (l)) {
        GError *error = NULL;
        MMObject *object;
        MMModem *modem;

        object = MM_OBJECT (l->data);
        modem = mm_object_get_modem (object);
        if (g_str_equal (sim_path, mm_modem_get_sim_path (modem))) {
            found = mm_modem_get_sim_sync (modem, NULL, &error);
            if (error) {
                g_printerr ("error: couldn't get sim '%s' in modem '%s': '%s'\n",
                            sim_path,
                            mm_modem_get_path (modem),
                            error->message);
                exit (EXIT_FAILURE);
            }

            if (found && o_object)
                *o_object = g_object_ref (object);
        }

        g_object_unref (modem);
    }

    if (!found) {
        g_printerr ("error: couldn't find sim at '%s'\n", sim_path);
        exit (EXIT_FAILURE);
    }

    g_list_free_full (modems, (GDestroyNotify) g_object_unref);
    g_free (sim_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);

    return found;
}

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar *sms_path;
    MMManager *manager;
    GList *modems;
    MMObject *current;
    MMSms *sms;
} GetSmsContext;

static void
get_sms_context_free (GetSmsContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->sms)
        g_object_unref (ctx->sms);
    g_list_free_full (ctx->modems, (GDestroyNotify) g_object_unref);
    g_free (ctx->sms_path);
    g_free (ctx);
}

static void
get_sms_context_complete (GetSmsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    ctx->result = NULL;
}

MMSms *
mmcli_get_sms_finish (GAsyncResult *res,
                      MMManager **o_manager,
                      MMObject **o_object)
{
    GetSmsContext *ctx;

    ctx = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    if (o_manager)
        *o_manager = g_object_ref (ctx->manager);
    if (o_object)
        *o_object = g_object_ref (ctx->current);
    return g_object_ref (ctx->sms);
}

static void look_for_sms_in_modem (GetSmsContext *ctx);

static MMSms *
find_sms_in_list (GList *list,
                  const gchar *sms_path)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)) {
        MMSms *sms = MM_SMS (l->data);

        if (g_str_equal (mm_sms_get_path (sms), sms_path)) {
            g_debug ("Sms found at '%s'\n", sms_path);
            return g_object_ref (sms);
        }
    }

    return NULL;
}

static void
list_sms_ready (MMModemMessaging *modem,
                GAsyncResult *res,
                GetSmsContext *ctx)
{
    GList *sms_list;
    GError *error = NULL;

    sms_list = mm_modem_messaging_list_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't list SMS at '%s': '%s'\n",
                    mm_modem_messaging_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    ctx->sms = find_sms_in_list (sms_list, ctx->sms_path);
    g_list_free_full (sms_list, (GDestroyNotify) g_object_unref);

    /* Found! */
    if (ctx->sms) {
        g_simple_async_result_set_op_res_gpointer (
            ctx->result,
            ctx,
            (GDestroyNotify)get_sms_context_free);
        get_sms_context_complete (ctx);
        return;
    }

    /* Not found, try with next modem */
    look_for_sms_in_modem (ctx);
}

static void
look_for_sms_in_modem (GetSmsContext *ctx)
{
    MMModemMessaging *modem;

    if (!ctx->modems) {
        g_printerr ("error: couldn't find SMS at '%s': 'not found in any modem'\n",
                    ctx->sms_path);
        exit (EXIT_FAILURE);
    }

    /* Loop looking for the sms in each modem found */
    ctx->current = MM_OBJECT (ctx->modems->data);
    ctx->modems = g_list_delete_link (ctx->modems, ctx->modems);

    modem = mm_object_get_modem_messaging (ctx->current);
    if (modem) {
        g_debug ("Looking for sms '%s' in modem '%s'...",
                 ctx->sms_path,
                 mm_object_get_path (ctx->current));
        mm_modem_messaging_list (modem,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)list_sms_ready,
                                 ctx);
        g_object_unref (modem);
        return;
    }

    /* Current modem has no messaging capabilities, try with next modem */
    look_for_sms_in_modem (ctx);
}

static void
get_sms_manager_ready (GDBusConnection *connection,
                       GAsyncResult *res,
                       GetSmsContext *ctx)
{
    ctx->manager = mmcli_get_manager_finish (res);
    ctx->modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));
    if (!ctx->modems) {
        g_printerr ("error: couldn't find SMS at '%s': 'no modems found'\n",
                    ctx->sms_path);
        exit (EXIT_FAILURE);
    }

    look_for_sms_in_modem (ctx);
}

static gchar *
get_sms_path (const gchar *path_or_index)
{
    gchar *sms_path;

    /* We must have a given sms specified */
    if (!path_or_index) {
        g_printerr ("error: no SMS was specified\n");
        exit (EXIT_FAILURE);
    }

    /* Sms path may come in two ways: full DBus path or just sms index.
     * If it is a sms index, we'll need to generate the DBus path ourselves */
    if (g_str_has_prefix (path_or_index, MM_DBUS_SMS_PREFIX)) {
        g_debug ("Assuming '%s' is the full SMS path", path_or_index);
        sms_path = g_strdup (path_or_index);
    } else if (g_ascii_isdigit (path_or_index[0])) {
        g_debug ("Assuming '%s' is the SMS index", path_or_index);
        sms_path = g_strdup_printf (MM_DBUS_SMS_PREFIX "/%s", path_or_index);
    } else {
        g_printerr ("error: invalid path or index string specified: '%s'\n",
                    path_or_index);
        exit (EXIT_FAILURE);
    }

    return sms_path;
}

void
mmcli_get_sms (GDBusConnection *connection,
               const gchar *path_or_index,
               GCancellable *cancellable,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
    GetSmsContext *ctx;

    ctx = g_new0 (GetSmsContext, 1);
    ctx->sms_path = get_sms_path (path_or_index);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (connection),
                                             callback,
                                             user_data,
                                             mmcli_get_sms);
    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_sms_manager_ready,
                       ctx);
}

MMSms *
mmcli_get_sms_sync (GDBusConnection *connection,
                    const gchar *path_or_index,
                    MMManager **o_manager,
                    MMObject **o_object)
{
    MMManager *manager;
    GList *modems;
    GList *l;
    MMSms *found = NULL;
    gchar *sms_path;

    sms_path = get_sms_path (path_or_index);

    manager = mmcli_get_manager_sync (connection);
    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!modems) {
        g_printerr ("error: couldn't find sms at '%s': 'no modems found'\n",
                    sms_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; !found && l; l = g_list_next (l)) {
        GError *error = NULL;
        MMObject *object;
        MMModemMessaging *modem;
        GList *sms_list;

        object = MM_OBJECT (l->data);
        modem = mm_object_get_modem_messaging (object);

        /* If this modem doesn't implement messaging, continue to next one */
        if (!modem)
            continue;

        sms_list = mm_modem_messaging_list_sync (modem, NULL, &error);
        if (error) {
            g_printerr ("error: couldn't list SMS at '%s': '%s'\n",
                        mm_modem_messaging_get_path (modem),
                        error->message);
            exit (EXIT_FAILURE);
        }

        found = find_sms_in_list (sms_list, sms_path);
        g_list_free_full (sms_list, (GDestroyNotify) g_object_unref);

        if (found && o_object)
            *o_object = g_object_ref (object);

        g_object_unref (modem);
    }

    if (!found) {
        g_printerr ("error: couldn't find SMS at '%s': 'not found in any modem'\n",
                    sms_path);
        exit (EXIT_FAILURE);
    }

    g_list_free_full (modems, (GDestroyNotify) g_object_unref);
    g_free (sms_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);

    return found;
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
    case MM_MODEM_STATE_CHANGE_REASON_FAILURE:
        return "Failure";
    }

    g_warn_if_reached ();
    return NULL;
}

/* Common options */
static gchar *modem_str;
static gchar *bearer_str;
static gchar *sim_str;
static gchar *sms_str;

static GOptionEntry entries[] = {
    { "modem", 'm', 0, G_OPTION_ARG_STRING, &modem_str,
      "Specify modem by path or index. Shows modem information if no action specified.",
      "[PATH|INDEX]"
    },
    { "bearer", 'b', 0, G_OPTION_ARG_STRING, &bearer_str,
      "Specify bearer by path or index. Shows bearer information if no action specified.",
      "[PATH|INDEX]"
    },
    { "sim", 'i', 0, G_OPTION_ARG_STRING, &sim_str,
      "Specify SIM card by path or index. Shows SIM card information if no action specified.",
      "[PATH|INDEX]"
    },
    { "sms", 's', 0, G_OPTION_ARG_STRING, &sms_str,
      "Specify SMS by path or index. Shows SMS information if no action specified.",
      "[PATH|INDEX]"
    },
    { NULL }
};

GOptionGroup *
mmcli_get_common_option_group (void)
{
    GOptionGroup *group;

    /* Status options */
    group = g_option_group_new ("common",
                                "Common options",
                                "Show common options",
                                NULL,
                                NULL);
    g_option_group_add_entries (group, entries);

    return group;
}

const gchar *
mmcli_get_common_modem_string (void)
{
    return modem_str;
}

const gchar *
mmcli_get_common_bearer_string (void)
{
    return bearer_str;
}

const gchar *
mmcli_get_common_sim_string (void)
{
    return sim_str;
}

const gchar *
mmcli_get_common_sms_string (void)
{
    return sms_str;
}

gchar *
mmcli_prefix_newlines (const gchar *prefix,
                       const gchar *str)
{
    GString *prefixed_string = NULL;
    const gchar *line_start = str;
    const gchar *line_end;

    do {
        gssize line_length;

        line_end = strchr (line_start, '\n');
        if (line_end)
            line_length = line_end - line_start;
        else
            line_length = strlen (line_start);

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

        line_start = (line_end ? line_end + 1 : NULL);
    } while (line_start != NULL);

    return (prefixed_string ?
            g_string_free (prefixed_string, FALSE) :
            g_strdup (str));
}
