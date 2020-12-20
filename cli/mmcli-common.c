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

/******************************************************************************/
/* Manager */

MMManager *
mmcli_get_manager_finish (GAsyncResult *res)
{
    return g_task_propagate_pointer (G_TASK (res), NULL);
}

static void
manager_new_ready (GDBusConnection *connection,
                   GAsyncResult    *res,
                   GTask           *task)
{
    MMManager *manager;
    gchar     *name_owner;
    GError    *error = NULL;

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

    g_task_return_pointer (task, manager, g_object_unref);
    g_object_unref (task);
}

void
mmcli_get_manager (GDBusConnection     *connection,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask *task;

    task = g_task_new (connection, cancellable, callback, user_data);

    mm_manager_new (connection,
                    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                    cancellable,
                    (GAsyncReadyCallback)manager_new_ready,
                    task);
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

/******************************************************************************/
/* Common to all objects */

#define ANY_OBJECT_STR "any"

static void
get_object_lookup_info (const gchar  *str,
                        const gchar  *object_type,
                        const gchar  *object_prefix,
                        gchar       **object_path,
                        gchar       **modem_uid,
                        gboolean     *find_any)
{
    gboolean all_numeric;
    guint    i;

    /* Empty string not allowed */
    if (!str || !str[0]) {
        g_printerr ("error: no %s was specified\n", object_type);
        exit (EXIT_FAILURE);
    }

    /* User string may come in four ways:
     *   a) full DBus path
     *   b) object index
     *   c) modem UID (for modem or SIM lookup only)
     *   d) "any" string (for modem or SIM lookup only)
     */

    *object_path = NULL;
    if (modem_uid)
        *modem_uid  = NULL;
    if (find_any)
        *find_any = FALSE;

    /* If match the DBus prefix, we have a DBus object path */
    if (g_str_has_prefix (str, object_prefix)) {
        g_debug ("Assuming '%s' is the full %s path", str, object_type);
        *object_path = g_strdup (str);
        return;
    }

    /* If all numeric, we have the object index */
    all_numeric = TRUE;
    for (i = 0; str[i]; i++) {
        if (!g_ascii_isdigit (str[i])) {
            all_numeric = FALSE;
            break;
        }
    }
    if (all_numeric) {
        g_debug ("Assuming '%s' is the %s index", str, object_type);
        *object_path = g_strdup_printf ("%s/%s", object_prefix, str);
        return;
    }

    /* If it matches the lookup keyword or any of its substrings, we have
     * to look for the first available object */
    if ((find_any) && (g_ascii_strncasecmp (str, ANY_OBJECT_STR, strlen (str)) == 0)) {
        g_debug ("Will look for first available %s", object_type);
        *find_any = TRUE;
        return;
    }

    /* Otherwise we have the UID */
    if (modem_uid) {
        g_debug ("Assuming '%s' is the modem UID", str);
        *modem_uid = g_strdup (str);
        return;
    }

    /* If UID is not a valid input for the object type, error out */
    g_printerr ("error: invalid %s string specified: '%s'\n", object_type, str);
    exit (EXIT_FAILURE);
}

/******************************************************************************/
/* Modem */

static MMObject *
find_modem (MMManager   *manager,
            const gchar *modem_path,
            const gchar *modem_uid,
            gboolean     modem_any)
{
    GList *modems;
    GList *l;
    MMObject *found = NULL;

    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    for (l = modems; l; l = g_list_next (l)) {
        MMObject *obj;
        MMModem  *modem;

        obj   = MM_OBJECT (l->data);
        modem = mm_object_get_modem (obj);
        if (!modem)
             continue;

        if (modem_any ||
            (modem_path && g_str_equal (mm_object_get_path (obj), modem_path)) ||
            (modem_uid && g_str_equal (mm_modem_get_device (modem), modem_uid))) {
            found = g_object_ref (obj);
            break;
        }
    }
    g_list_free_full (modems, g_object_unref);

    if (!found) {
        g_printerr ("error: couldn't find modem\n");
        exit (EXIT_FAILURE);
    }

    g_debug ("Modem found at '%s'\n", modem_path);

    return found;
}

typedef struct {
    gchar    *modem_path;
    gchar    *modem_uid;
    gboolean  modem_any;
} GetModemContext;

typedef struct {
    MMManager *manager;
    MMObject  *object;
} GetModemResults;

static void
get_modem_results_free (GetModemResults *results)
{
    g_object_unref (results->manager);
    g_object_unref (results->object);
    g_free (results);
}

static void
get_modem_context_free (GetModemContext *ctx)
{
    g_free (ctx->modem_path);
    g_free (ctx->modem_uid);
    g_free (ctx);
}

MMObject *
mmcli_get_modem_finish (GAsyncResult  *res,
                        MMManager    **o_manager)
{
    GetModemResults *results;
    MMObject        *obj;

    results = g_task_propagate_pointer (G_TASK (res), NULL);
    g_assert (results);
    if (o_manager)
        *o_manager = g_object_ref (results->manager);
    obj = g_object_ref (results->object);
    get_modem_results_free (results);
    return obj;
}

static void
get_manager_ready (GDBusConnection *connection,
                   GAsyncResult    *res,
                   GTask           *task)
{
    GetModemResults *results;
    GetModemContext *ctx;

    ctx = g_task_get_task_data (task);

    results = g_new (GetModemResults, 1);
    results->manager = mmcli_get_manager_finish (res);
    results->object = find_modem (results->manager, ctx->modem_path, ctx->modem_uid, ctx->modem_any);
    g_task_return_pointer (task, results, (GDestroyNotify)get_modem_results_free);
    g_object_unref (task);
}

void
mmcli_get_modem (GDBusConnection     *connection,
                 const gchar         *str,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    GTask           *task;
    GetModemContext *ctx;

    task = g_task_new (connection, cancellable, callback, user_data);

    ctx = g_new0 (GetModemContext, 1);
    get_object_lookup_info (str, "modem", MM_DBUS_MODEM_PREFIX,
                            &ctx->modem_path, &ctx->modem_uid, &ctx->modem_any);
    g_assert (!!ctx->modem_path + !!ctx->modem_uid + ctx->modem_any == 1);
    g_task_set_task_data (task, ctx, (GDestroyNotify) get_modem_context_free);

    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_manager_ready,
                       task);
}

MMObject *
mmcli_get_modem_sync (GDBusConnection  *connection,
                      const gchar      *str,
                      MMManager       **o_manager)
{
    MMManager *manager;
    MMObject *found;
    gchar *modem_path = NULL;
    gchar *modem_uid = NULL;
    gboolean modem_any = FALSE;

    manager = mmcli_get_manager_sync (connection);
    get_object_lookup_info (str, "modem", MM_DBUS_MODEM_PREFIX,
                            &modem_path, &modem_uid, &modem_any);
    g_assert (!!modem_path + !!modem_uid + modem_any == 1);
    found = find_modem (manager, modem_path, modem_uid, modem_any);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);
    g_free (modem_path);
    g_free (modem_uid);

    return found;
}

/******************************************************************************/
/* Bearer */

typedef struct {
    gchar     *bearer_path;
    MMManager *manager;
    GList     *modems;
    MMObject  *current;
} GetBearerContext;

typedef struct {
    MMManager *manager;
    MMObject  *object;
    MMBearer  *bearer;
} GetBearerResults;

static void
get_bearer_results_free (GetBearerResults *results)
{
    g_object_unref (results->manager);
    g_object_unref (results->object);
    g_object_unref (results->bearer);
    g_free (results);
}

static void
get_bearer_context_free (GetBearerContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_list_free_full (ctx->modems, g_object_unref);
    g_free (ctx->bearer_path);
    g_free (ctx);
}

MMBearer *
mmcli_get_bearer_finish (GAsyncResult  *res,
                         MMManager    **o_manager,
                         MMObject     **o_object)
{
    GetBearerResults *results;
    MMBearer         *obj;

    results = g_task_propagate_pointer (G_TASK (res), NULL);
    g_assert (results);
    if (o_manager)
        *o_manager = g_object_ref (results->manager);
    if (o_object)
        *o_object = g_object_ref (results->object);
    obj = g_object_ref (results->bearer);
    get_bearer_results_free (results);
    return obj;
}

static void look_for_bearer_in_modem (GTask *task);

static MMBearer *
find_bearer_in_list (GList       *list,
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
list_bearers_ready (MMModem      *modem,
                    GAsyncResult *res,
                    GTask        *task)
{
    GetBearerContext *ctx;
    GetBearerResults *results;
    MMBearer         *found;
    GList            *bearers;
    GError           *error = NULL;

    ctx = g_task_get_task_data (task);

    bearers = mm_modem_list_bearers_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't list bearers at '%s': '%s'\n",
                    mm_modem_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    found = find_bearer_in_list (bearers, ctx->bearer_path);
    g_list_free_full (bearers, g_object_unref);

    if (!found) {
        /* Not found, try with next modem */
        look_for_bearer_in_modem (task);
        return;
    }

    /* Found! */
    results = g_new (GetBearerResults, 1);
    results->manager = g_object_ref (ctx->manager);
    results->object  = g_object_ref (ctx->current);
    results->bearer  = found;
    g_task_return_pointer (task, results, (GDestroyNotify) get_bearer_results_free);
    g_object_unref (task);
}

static void
look_for_bearer_in_modem_bearer_list (GTask *task)
{
    GetBearerContext *ctx;
    MMModem          *modem;

    ctx = g_task_get_task_data (task);

    g_assert (ctx->current);
    modem = mm_object_get_modem (ctx->current);
    mm_modem_list_bearers (modem,
                           g_task_get_cancellable (task),
                           (GAsyncReadyCallback)list_bearers_ready,
                           task);
    g_object_unref (modem);
}

static void
get_initial_eps_bearer_ready (MMModem3gpp  *modem3gpp,
                              GAsyncResult *res,
                              GTask        *task)
{
    GetBearerContext *ctx;
    MMBearer         *bearer;
    GetBearerResults *results;

    ctx = g_task_get_task_data (task);

    bearer = mm_modem_3gpp_get_initial_eps_bearer_finish (modem3gpp, res, NULL);
    if (!bearer) {
        look_for_bearer_in_modem_bearer_list (task);
        return;
    }

    /* Found! */
    results = g_new (GetBearerResults, 1);
    results->manager = g_object_ref (ctx->manager);
    results->object  = g_object_ref (ctx->current);
    results->bearer  = bearer;
    g_task_return_pointer (task, results, (GDestroyNotify) get_bearer_results_free);
    g_object_unref (task);
}

static void
look_for_bearer_in_modem_3gpp_eps_initial_bearer (GTask *task)
{
    GetBearerContext *ctx;
    MMModem3gpp      *modem3gpp;

    ctx = g_task_get_task_data (task);

    g_assert (ctx->current);
    modem3gpp = mm_object_get_modem_3gpp (ctx->current);
    if (!modem3gpp) {
        look_for_bearer_in_modem_bearer_list (task);
        return;
    }

    if (!g_strcmp0 (mm_modem_3gpp_get_initial_eps_bearer_path (modem3gpp), ctx->bearer_path))
        mm_modem_3gpp_get_initial_eps_bearer (modem3gpp,
                                              g_task_get_cancellable (task),
                                              (GAsyncReadyCallback)get_initial_eps_bearer_ready,
                                              task);
    else
        look_for_bearer_in_modem_bearer_list (task);
    g_object_unref (modem3gpp);
}

static void
look_for_bearer_in_modem (GTask *task)
{
    GetBearerContext *ctx;
    MMModem          *modem;

    ctx = g_task_get_task_data (task);

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
        look_for_bearer_in_modem (task);
    } else {
        g_debug ("Looking for bearer '%s' in modem '%s'...",
                 ctx->bearer_path,
                 mm_object_get_path (ctx->current));
        look_for_bearer_in_modem_3gpp_eps_initial_bearer (task);
    }

    g_object_unref (modem);
}

static void
get_bearer_manager_ready (GDBusConnection *connection,
                          GAsyncResult    *res,
                          GTask           *task)
{
    GetBearerContext *ctx;

    ctx = g_task_get_task_data (task);

    ctx->manager = mmcli_get_manager_finish (res);
    ctx->modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));
    if (!ctx->modems) {
        g_printerr ("error: couldn't find bearer at '%s': 'no modems found'\n",
                    ctx->bearer_path);
        exit (EXIT_FAILURE);
    }

    look_for_bearer_in_modem (task);
}

void
mmcli_get_bearer (GDBusConnection     *connection,
                  const gchar         *str,
                  GCancellable        *cancellable,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    GTask            *task;
    GetBearerContext *ctx;

    task = g_task_new (connection, cancellable, callback, user_data);

    ctx = g_new0 (GetBearerContext, 1);
    get_object_lookup_info (str, "bearer", MM_DBUS_BEARER_PREFIX,
                            &ctx->bearer_path, NULL, NULL);
    g_assert (ctx->bearer_path);
    g_task_set_task_data (task, ctx, (GDestroyNotify) get_bearer_context_free);

    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_bearer_manager_ready,
                       task);
}

MMBearer *
mmcli_get_bearer_sync (GDBusConnection  *connection,
                       const gchar      *str,
                       MMManager       **o_manager,
                       MMObject        **o_object)
{
    MMManager *manager;
    GList *modems;
    GList *l;
    MMBearer *found = NULL;
    gchar *bearer_path = NULL;

    get_object_lookup_info (str, "bearer", MM_DBUS_BEARER_PREFIX,
                            &bearer_path, NULL, NULL);
    g_assert (bearer_path);

    manager = mmcli_get_manager_sync (connection);
    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!modems) {
        g_printerr ("error: couldn't find bearer at '%s': 'no modems found'\n",
                    bearer_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; !found && l; l = g_list_next (l)) {
        GError      *error = NULL;
        MMObject    *object;
        MMModem     *modem;
        MMModem3gpp *modem3gpp;

        object = MM_OBJECT (l->data);
        modem = mm_object_get_modem (object);
        modem3gpp = mm_object_get_modem_3gpp (object);

        /* Don't look for bearers in modems which are not fully initialized */
        if (mm_modem_get_state (modem) < MM_MODEM_STATE_DISABLED) {
            g_debug ("Skipping modem '%s' when looking for bearers "
                     "(not fully initialized)",
                     mm_object_get_path (object));
            goto next;
        }

        if (modem3gpp && !g_strcmp0 (mm_modem_3gpp_get_initial_eps_bearer_path (modem3gpp), bearer_path)) {
            found = mm_modem_3gpp_get_initial_eps_bearer_sync (modem3gpp, NULL, &error);
            if (!found) {
                g_printerr ("error: couldn't get initial EPS bearer object at '%s': '%s'\n",
                            mm_modem_get_path (modem),
                            error->message);
                exit (EXIT_FAILURE);
            }
        } else {
            GList *bearers;

            bearers = mm_modem_list_bearers_sync (modem, NULL, &error);
            if (error) {
                g_printerr ("error: couldn't list bearers at '%s': '%s'\n",
                            mm_modem_get_path (modem),
                            error->message);
                exit (EXIT_FAILURE);
            }

            found = find_bearer_in_list (bearers, bearer_path);
            g_list_free_full (bearers, g_object_unref);
        }

        if (found && o_object)
            *o_object = g_object_ref (object);

    next:
        g_clear_object (&modem);
        g_clear_object (&modem3gpp);
    }

    if (!found) {
        g_printerr ("error: couldn't find bearer at '%s': 'not found in any modem'\n",
                    bearer_path);
        exit (EXIT_FAILURE);
    }

    g_list_free_full (modems, g_object_unref);
    g_free (bearer_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);

    return found;
}

/******************************************************************************/
/* SIM */

typedef struct {
    gchar     *sim_path;
    gchar     *modem_uid;
    gboolean   sim_any;
    MMManager *manager;
    MMObject  *current;
} GetSimContext;

typedef struct {
    MMManager *manager;
    MMObject  *object;
    MMSim     *sim;
} GetSimResults;

static void
get_sim_results_free (GetSimResults *results)
{
    g_object_unref (results->manager);
    g_object_unref (results->object);
    g_object_unref (results->sim);
    g_free (results);
}

static void
get_sim_context_free (GetSimContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx->modem_uid);
    g_free (ctx->sim_path);
    g_free (ctx);
}

MMSim *
mmcli_get_sim_finish (GAsyncResult  *res,
                      MMManager    **o_manager,
                      MMObject     **o_object)
{
    GetSimResults *results;
    MMSim         *obj;

    results = g_task_propagate_pointer (G_TASK (res), NULL);
    g_assert (results);
    if (o_manager)
        *o_manager = g_object_ref (results->manager);
    if (o_object)
        *o_object = g_object_ref (results->object);
    obj = g_object_ref (results->sim);
    get_sim_results_free (results);
    return obj;
}

static void
list_sim_slots_ready (MMModem      *modem,
                      GAsyncResult *res,
                      GTask        *task)
{
    g_autoptr(GPtrArray)  sim_slots = NULL;
    GetSimContext        *ctx;
    GetSimResults        *results = NULL;
    guint                 i;
    GError               *error = NULL;

    ctx = g_task_get_task_data (task);

    sim_slots = mm_modem_list_sim_slots_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't list SIM slots at '%s': '%s'\n",
                    mm_modem_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    for (i = 0; i < sim_slots->len; i++) {
        MMSim *sim;

        sim = MM_SIM (g_ptr_array_index (sim_slots, i));
        if (sim && g_str_equal (mm_sim_get_path (sim), ctx->sim_path)) {
            /* Found! */
            results = g_new (GetSimResults, 1);
            results->manager = g_object_ref (ctx->manager);
            results->object  = g_object_ref (ctx->current);
            results->sim     = g_object_ref (sim);
            break;
        }
    }

    if (results) {
        g_task_return_pointer (task, results, (GDestroyNotify) get_sim_results_free);
        g_object_unref (task);
        return;
    }

    g_printerr ("error: couldn't get additional SIM '%s' at '%s'\n",
                ctx->sim_path,
                mm_modem_get_path (modem));
    exit (EXIT_FAILURE);
}

static void
get_sim_ready (MMModem      *modem,
               GAsyncResult *res,
               GTask        *task)
{
    GetSimContext *ctx;
    GetSimResults *results;
    MMSim         *sim;
    GError        *error = NULL;

    ctx = g_task_get_task_data (task);

    sim = mm_modem_get_sim_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't get SIM '%s' at '%s': '%s'\n",
                    ctx->sim_path,
                    mm_modem_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    /* Found! */
    results = g_new (GetSimResults, 1);
    results->manager = g_object_ref (ctx->manager);
    results->object  = g_object_ref (ctx->current);
    results->sim     = sim;
    g_task_return_pointer (task, results, (GDestroyNotify) get_sim_results_free);
    g_object_unref (task);
}

static void
get_sim_manager_ready (GDBusConnection *connection,
                       GAsyncResult    *res,
                       GTask           *task)
{
    GetSimContext *ctx;
    GList         *l;
    GList         *modems;

    ctx = g_task_get_task_data (task);

    ctx->manager = mmcli_get_manager_finish (res);

    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));
    if (!modems) {
        g_printerr ("error: couldn't find SIM at '%s': 'no modems found'\n",
                    ctx->sim_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; l && !ctx->current; l = g_list_next (l)) {
        MMObject           *object;
        MMModem            *modem;
        const gchar *const *sim_slot_paths;

        object = MM_OBJECT (l->data);
        modem = mm_object_get_modem (object);
        sim_slot_paths = mm_modem_get_sim_slot_paths (modem);

        /* check if we can match the first object found */
        if (ctx->sim_any) {
            g_assert (!ctx->sim_path);
            ctx->sim_path = g_strdup (mm_modem_get_sim_path (modem));
        }
        /* check if modem UID matches */
        else if (ctx->modem_uid) {
            if (g_str_equal (ctx->modem_uid, mm_modem_get_device (modem))) {
                g_assert (!ctx->sim_path);
                ctx->sim_path = g_strdup (mm_modem_get_sim_path (modem));
            } else {
                g_object_unref (modem);
                continue;
            }
        }

        if (g_str_equal (ctx->sim_path, mm_modem_get_sim_path (modem))) {
            ctx->current = g_object_ref (object);
            mm_modem_get_sim (modem,
                              g_task_get_cancellable (task),
                              (GAsyncReadyCallback)get_sim_ready,
                              task);
        } else if (sim_slot_paths) {
            guint i;

            for (i = 0; sim_slot_paths[i]; i++) {
                if (g_str_equal (ctx->sim_path, sim_slot_paths[i])) {
                    ctx->current = g_object_ref (object);
                    mm_modem_list_sim_slots (modem,
                                             g_task_get_cancellable (task),
                                             (GAsyncReadyCallback)list_sim_slots_ready,
                                             task);
                    break;
                }
            }
        }
        g_object_unref (modem);
    }
    g_list_free_full (modems, g_object_unref);

    if (!ctx->current) {
        g_printerr ("error: couldn't find SIM\n");
        exit (EXIT_FAILURE);
    }
}

void
mmcli_get_sim (GDBusConnection     *connection,
               const gchar         *str,
               GCancellable        *cancellable,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    GTask         *task;
    GetSimContext *ctx;

    task = g_task_new (connection, cancellable, callback, user_data);

    ctx = g_new0 (GetSimContext, 1);
    get_object_lookup_info (str, "SIM", MM_DBUS_SIM_PREFIX,
                            &ctx->sim_path, &ctx->modem_uid, &ctx->sim_any);
    g_assert (!!ctx->sim_path + !!ctx->modem_uid + ctx->sim_any == 1);
    g_task_set_task_data (task, ctx, (GDestroyNotify) get_sim_context_free);

    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_sim_manager_ready,
                       task);
}

MMSim *
mmcli_get_sim_sync (GDBusConnection  *connection,
                    const gchar      *str,
                    MMManager       **o_manager,
                    MMObject        **o_object)
{
    MMManager *manager;
    GList *modems;
    GList *l;
    MMSim *found = NULL;
    gchar *sim_path = NULL;
    gchar *modem_uid = NULL;
    gboolean sim_any = FALSE;

    get_object_lookup_info (str, "SIM", MM_DBUS_SIM_PREFIX,
                            &sim_path, &modem_uid, &sim_any);
    g_assert (!!sim_path + !!modem_uid + sim_any == 1);

    manager = mmcli_get_manager_sync (connection);
    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!modems) {
        g_printerr ("error: couldn't find SIM at '%s': 'no modems found'\n",
                    sim_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; !found && l; l = g_list_next (l)) {
        GError             *error = NULL;
        MMObject           *object;
        MMModem            *modem;
        const gchar *const *sim_slot_paths;

        object = MM_OBJECT (l->data);
        modem = mm_object_get_modem (object);
        sim_slot_paths = mm_modem_get_sim_slot_paths (modem);

        /* check if we can match the first object found */
        if (sim_any) {
            g_assert (!sim_path);
            sim_path = g_strdup (mm_modem_get_sim_path (modem));
        }
        /* check if modem UID matches */
        else if (modem_uid) {
            if (g_str_equal (modem_uid, mm_modem_get_device (modem))) {
                g_assert (!sim_path);
                sim_path = g_strdup (mm_modem_get_sim_path (modem));
            } else {
                g_object_unref (modem);
                continue;
            }
        }

        if (g_str_equal (sim_path, mm_modem_get_sim_path (modem))) {
            found = mm_modem_get_sim_sync (modem, NULL, &error);
            if (error) {
                g_printerr ("error: couldn't get SIM '%s' in modem '%s': '%s'\n",
                            sim_path,
                            mm_modem_get_path (modem),
                            error->message);
                exit (EXIT_FAILURE);
            }

            if (found && o_object)
                *o_object = g_object_ref (object);
        } else if (sim_slot_paths) {
            guint i;

            for (i = 0; !found && sim_slot_paths[i]; i++) {
                if (g_str_equal (sim_path, sim_slot_paths[i])) {
                    g_autoptr(GPtrArray) sim_slots = NULL;
                    guint                j;

                    sim_slots = mm_modem_list_sim_slots_sync (modem, NULL, &error);
                    if (error) {
                        g_printerr ("error: couldn't get SIM slots in modem '%s': '%s'\n",
                                    mm_modem_get_path (modem),
                                    error->message);
                        exit (EXIT_FAILURE);
                    }

                    for (j = 0; j < sim_slots->len; j++) {
                        MMSim *sim;

                        sim = MM_SIM (g_ptr_array_index (sim_slots, j));
                        if (sim && g_str_equal (sim_path, mm_sim_get_path (sim))) {
                            found = g_object_ref (sim);
                            if (o_object)
                                *o_object = g_object_ref (object);
                        }
                    }
                }
            }
        }

        g_object_unref (modem);
    }

    if (!found) {
        g_printerr ("error: couldn't find SIM\n");
        exit (EXIT_FAILURE);
    }

    g_list_free_full (modems, g_object_unref);
    g_free (sim_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);

    return found;
}

/******************************************************************************/
/* SMS */

typedef struct {
    gchar     *sms_path;
    MMManager *manager;
    GList     *modems;
    MMObject  *current;
} GetSmsContext;

typedef struct {
    MMManager *manager;
    MMObject  *object;
    MMSms     *sms;
} GetSmsResults;

static void
get_sms_results_free (GetSmsResults *results)
{
    g_object_unref (results->manager);
    g_object_unref (results->object);
    g_object_unref (results->sms);
    g_free (results);
}

static void
get_sms_context_free (GetSmsContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_list_free_full (ctx->modems, g_object_unref);
    g_free (ctx->sms_path);
    g_free (ctx);
}

MMSms *
mmcli_get_sms_finish (GAsyncResult  *res,
                      MMManager    **o_manager,
                      MMObject     **o_object)
{
    GetSmsResults *results;
    MMSms         *obj;

    results = g_task_propagate_pointer (G_TASK (res), NULL);
    g_assert (results);
    if (o_manager)
        *o_manager = g_object_ref (results->manager);
    if (o_object)
        *o_object = g_object_ref (results->object);
    obj = g_object_ref (results->sms);
    get_sms_results_free (results);
    return obj;
}

static void look_for_sms_in_modem (GTask *task);

static MMSms *
find_sms_in_list (GList       *list,
                  const gchar *sms_path)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)) {
        MMSms *sms = MM_SMS (l->data);

        if (g_str_equal (mm_sms_get_path (sms), sms_path)) {
            g_debug ("SMS found at '%s'\n", sms_path);
            return g_object_ref (sms);
        }
    }

    return NULL;
}

static void
list_sms_ready (MMModemMessaging *modem,
                GAsyncResult     *res,
                GTask            *task)
{
    GetSmsContext *ctx;
    GetSmsResults *results;
    MMSms         *found;
    GList         *sms_list;
    GError        *error = NULL;

    ctx = g_task_get_task_data (task);

    sms_list = mm_modem_messaging_list_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't list SMS at '%s': '%s'\n",
                    mm_modem_messaging_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    found = find_sms_in_list (sms_list, ctx->sms_path);
    g_list_free_full (sms_list, g_object_unref);

    if (!found) {
        /* Not found, try with next modem */
        look_for_sms_in_modem (task);
        return;
    }

    /* Found! */
    results = g_new (GetSmsResults, 1);
    results->manager = g_object_ref (ctx->manager);
    results->object  = g_object_ref (ctx->current);
    results->sms     = found;
    g_task_return_pointer (task, results, (GDestroyNotify) get_sms_results_free);
    g_object_unref (task);
}

static void
look_for_sms_in_modem (GTask *task)
{
    GetSmsContext    *ctx;
    MMModemMessaging *modem;

    ctx = g_task_get_task_data (task);

    if (!ctx->modems) {
        g_printerr ("error: couldn't find SMS at '%s': 'not found in any modem'\n",
                    ctx->sms_path);
        exit (EXIT_FAILURE);
    }

    /* Loop looking for the sms in each modem found */
    ctx->current = MM_OBJECT (ctx->modems->data);
    ctx->modems = g_list_delete_link (ctx->modems, ctx->modems);

    modem = mm_object_get_modem_messaging (ctx->current);
    if (!modem) {
        /* Current modem has no messaging capabilities, try with next modem */
        look_for_sms_in_modem (task);
        return;
    }

    g_debug ("Looking for sms '%s' in modem '%s'...",
             ctx->sms_path,
             mm_object_get_path (ctx->current));
    mm_modem_messaging_list (modem,
                             g_task_get_cancellable (task),
                             (GAsyncReadyCallback)list_sms_ready,
                             task);
    g_object_unref (modem);
}

static void
get_sms_manager_ready (GDBusConnection *connection,
                       GAsyncResult    *res,
                       GTask           *task)
{
    GetSmsContext *ctx;

    ctx = g_task_get_task_data (task);

    ctx->manager = mmcli_get_manager_finish (res);
    ctx->modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));
    if (!ctx->modems) {
        g_printerr ("error: couldn't find SMS at '%s': 'no modems found'\n",
                    ctx->sms_path);
        exit (EXIT_FAILURE);
    }

    look_for_sms_in_modem (task);
}

void
mmcli_get_sms (GDBusConnection     *connection,
               const gchar         *str,
               GCancellable        *cancellable,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
    GTask         *task;
    GetSmsContext *ctx;

    task = g_task_new (connection, cancellable, callback, user_data);

    ctx = g_new0 (GetSmsContext, 1);
    get_object_lookup_info (str, "SMS", MM_DBUS_SMS_PREFIX,
                            &ctx->sms_path, NULL, NULL);
    g_task_set_task_data (task, ctx, (GDestroyNotify) get_sms_context_free);

    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_sms_manager_ready,
                       task);
}

MMSms *
mmcli_get_sms_sync (GDBusConnection  *connection,
                    const gchar      *str,
                    MMManager       **o_manager,
                    MMObject        **o_object)
{
    MMManager *manager;
    GList *modems;
    GList *l;
    MMSms *found = NULL;
    gchar *sms_path = NULL;

    get_object_lookup_info (str, "SMS", MM_DBUS_SMS_PREFIX,
                            &sms_path, NULL, NULL);

    manager = mmcli_get_manager_sync (connection);
    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!modems) {
        g_printerr ("error: couldn't find SMS at '%s': 'no modems found'\n",
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
        g_list_free_full (sms_list, g_object_unref);

        if (found && o_object)
            *o_object = g_object_ref (object);

        g_object_unref (modem);
    }

    if (!found) {
        g_printerr ("error: couldn't find SMS at '%s': 'not found in any modem'\n",
                    sms_path);
        exit (EXIT_FAILURE);
    }

    g_list_free_full (modems, g_object_unref);
    g_free (sms_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);

    return found;
}

/******************************************************************************/
/* Call */

typedef struct {
    gchar     *call_path;
    MMManager *manager;
    GList     *modems;
    MMObject  *current;
} GetCallContext;

typedef struct {
    MMManager *manager;
    MMObject  *object;
    MMCall    *call;
} GetCallResults;

static void
get_call_results_free (GetCallResults *results)
{
    g_object_unref (results->manager);
    g_object_unref (results->object);
    g_object_unref (results->call);
    g_free (results);
}

static void
get_call_context_free (GetCallContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_list_free_full (ctx->modems, g_object_unref);
    g_free (ctx->call_path);
    g_free (ctx);
}

MMCall *
mmcli_get_call_finish (GAsyncResult  *res,
                       MMManager    **o_manager,
                       MMObject     **o_object)
{
    GetCallResults *results;
    MMCall         *obj;

    results = g_task_propagate_pointer (G_TASK (res), NULL);
    g_assert (results);
    if (o_manager)
        *o_manager = g_object_ref (results->manager);
    if (o_object)
        *o_object = g_object_ref (results->object);
    obj = g_object_ref (results->call);
    get_call_results_free (results);
    return obj;
}

static void look_for_call_in_modem (GTask *task);

static MMCall *
find_call_in_list (GList       *list,
                   const gchar *call_path)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)) {
        MMCall *call = MM_CALL (l->data);

        if (g_str_equal (mm_call_get_path (call), call_path)) {
            g_debug ("Call found at '%s'\n", call_path);
            return g_object_ref (call);
        }
    }

    return NULL;
}

static void
list_calls_ready (MMModemVoice *modem,
                  GAsyncResult *res,
                  GTask        *task)
{
    GetCallContext *ctx;
    GetCallResults *results;
    MMCall         *found;
    GList          *call_list;
    GError         *error = NULL;

    ctx = g_task_get_task_data (task);

    call_list = mm_modem_voice_list_calls_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't list call at '%s': '%s'\n",
                    mm_modem_voice_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    found = find_call_in_list (call_list, ctx->call_path);
    g_list_free_full (call_list, g_object_unref);

    if (!found) {
        /* Not found, try with next modem */
        look_for_call_in_modem (task);
        return;
    }

    /* Found! */
    results = g_new (GetCallResults, 1);
    results->manager = g_object_ref (ctx->manager);
    results->object  = g_object_ref (ctx->current);
    results->call    = found;
    g_task_return_pointer (task, results, (GDestroyNotify) get_call_results_free);
    g_object_unref (task);
}

static void
look_for_call_in_modem (GTask *task)
{
    GetCallContext *ctx;
    MMModemVoice   *modem;

    ctx = g_task_get_task_data (task);

    if (!ctx->modems) {
        g_printerr ("error: couldn't find call at '%s': 'not found in any modem'\n",
                    ctx->call_path);
        exit (EXIT_FAILURE);
    }

    /* Loop looking for the call in each modem found */
    ctx->current = MM_OBJECT (ctx->modems->data);
    ctx->modems = g_list_delete_link (ctx->modems, ctx->modems);

    modem = mm_object_get_modem_voice (ctx->current);
    if (!modem) {
        /* Current modem has no messaging capabilities, try with next modem */
        look_for_call_in_modem (task);
        return;
    }

    g_debug ("Looking for call '%s' in modem '%s'...",
             ctx->call_path,
             mm_object_get_path (ctx->current));
    mm_modem_voice_list_calls (modem,
                               g_task_get_cancellable (task),
                               (GAsyncReadyCallback)list_calls_ready,
                               task);
    g_object_unref (modem);
}

static void
get_call_manager_ready (GDBusConnection *connection,
                        GAsyncResult    *res,
                        GTask           *task)
{
    GetCallContext *ctx;

    ctx = g_task_get_task_data (task);

    ctx->manager = mmcli_get_manager_finish (res);
    ctx->modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));
    if (!ctx->modems) {
        g_printerr ("error: couldn't find call at '%s': 'no modems found'\n",
                    ctx->call_path);
        exit (EXIT_FAILURE);
    }

    look_for_call_in_modem (task);
}

void
mmcli_get_call (GDBusConnection     *connection,
                const gchar         *str,
                GCancellable        *cancellable,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    GTask          *task;
    GetCallContext *ctx;

    task = g_task_new (connection, cancellable, callback, user_data);

    ctx = g_new0 (GetCallContext, 1);
    get_object_lookup_info (str, "call", MM_DBUS_CALL_PREFIX,
                            &ctx->call_path, NULL, NULL);
    g_task_set_task_data (task, ctx, (GDestroyNotify) get_call_context_free);

    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_call_manager_ready,
                       task);
}

MMCall *
mmcli_get_call_sync (GDBusConnection  *connection,
                     const gchar      *str,
                     MMManager       **o_manager,
                     MMObject        **o_object)
{
    MMManager *manager;
    GList *modems;
    GList *l;
    MMCall *found = NULL;
    gchar *call_path = NULL;

    get_object_lookup_info (str, "call", MM_DBUS_CALL_PREFIX,
                            &call_path, NULL, NULL);

    manager = mmcli_get_manager_sync (connection);
    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!modems) {
        g_printerr ("error: couldn't find call at '%s': 'no modems found'\n",
                    call_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; !found && l; l = g_list_next (l)) {
        GError *error = NULL;
        MMObject *object;
        MMModemVoice *voice;
        GList *call_list;

        object = MM_OBJECT (l->data);
        voice = mm_object_get_modem_voice (object);

        /* If doesn't implement voice, continue to next one */
        if (!voice)
            continue;

        call_list = mm_modem_voice_list_calls_sync (voice, NULL, &error);
        if (error) {
            g_printerr ("error: couldn't list call at '%s': '%s'\n",
                        mm_modem_voice_get_path (voice),
                        error->message);
            exit (EXIT_FAILURE);
        }

        found = find_call_in_list (call_list, call_path);
        g_list_free_full (call_list, g_object_unref);

        if (found && o_object)
            *o_object = g_object_ref (object);

        g_object_unref (voice);
    }

    if (!found) {
        g_printerr ("error: couldn't find call at '%s': 'not found in any modem'\n",
                    call_path);
        exit (EXIT_FAILURE);
    }

    g_list_free_full (modems, g_object_unref);
    g_free (call_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);

    return found;
}

/******************************************************************************/

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
    default:
        g_assert_not_reached ();
    }
}

/* Common options */
static gchar *modem_str;
static gchar *bearer_str;
static gchar *sim_str;
static gchar *sms_str;
static gchar *call_str;

static GOptionEntry entries[] = {
    { "modem", 'm', 0, G_OPTION_ARG_STRING, &modem_str,
      "Specify modem by path, index, UID or 'any'. Shows modem information if no action specified.",
      "[PATH|INDEX|UID|any]"
    },
    { "bearer", 'b', 0, G_OPTION_ARG_STRING, &bearer_str,
      "Specify bearer by path or index. Shows bearer information if no action specified.",
      "[PATH|INDEX]"
    },
    { "sim", 'i', 0, G_OPTION_ARG_STRING, &sim_str,
      "Specify SIM card by path, index, UID or 'any'. Shows SIM card information if no action specified.",
      "[PATH|INDEX|UID|any]"
    },
    { "sms", 's', 0, G_OPTION_ARG_STRING, &sms_str,
      "Specify SMS by path or index. Shows SMS information if no action specified.",
      "[PATH|INDEX]"
    },
    { "call", 'o', 0, G_OPTION_ARG_STRING, &call_str,
      "Specify Call by path or index. Shows Call information if no action specified.",
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

const gchar *
mmcli_get_common_call_string (void)
{
    return call_str;
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
