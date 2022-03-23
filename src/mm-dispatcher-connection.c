/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2022 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <sys/stat.h>

#include <ModemManager.h>
#include "mm-errors-types.h"
#include "mm-utils.h"
#include "mm-log-object.h"
#include "mm-dispatcher-connection.h"

#if !defined CONNECTIONDIRPACKAGE
# error CONNECTIONDIRPACKAGE must be defined at build time
#endif

#if !defined CONNECTIONDIRUSER
# error CONNECTIONDIRUSER must be defined at build time
#endif

#define OPERATION_DESCRIPTION "connection status report"
#define CONNECTED_STRING      "connected"
#define DISCONNECTED_STRING   "disconnected"

/* Maximum time a connection dispatcher command is allowed to run before
 * us killing it */
#define MAX_CONNECTION_EXEC_TIME_SECS 5

struct _MMDispatcherConnection {
    MMDispatcher parent;
};

struct _MMDispatcherConnectionClass {
    MMDispatcherClass parent;
};

G_DEFINE_TYPE (MMDispatcherConnection, mm_dispatcher_connection, MM_TYPE_DISPATCHER)

/*****************************************************************************/

typedef struct {
    gchar    *modem_dbus_path;
    gchar    *bearer_dbus_path;
    gchar    *data_port;
    gboolean  connected;
    GList    *dispatcher_scripts;
    GFile    *current;
    guint     n_failures;
} ConnectionRunContext;

static void
connection_run_context_free (ConnectionRunContext *ctx)
{
    g_assert (!ctx->current);
    g_free (ctx->modem_dbus_path);
    g_free (ctx->bearer_dbus_path);
    g_free (ctx->data_port);
    g_list_free_full (ctx->dispatcher_scripts, (GDestroyNotify)g_object_unref);
    g_slice_free (ConnectionRunContext, ctx);
}

gboolean
mm_dispatcher_connection_run_finish (MMDispatcherConnection  *self,
                                     GAsyncResult           *res,
                                     GError                **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void connection_run_next (GTask *task);

static void
dispatcher_run_ready (MMDispatcher *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    ConnectionRunContext *ctx;
    g_autoptr(GError)     error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_dispatcher_run_finish (self, res, &error)) {
        ctx->n_failures++;
        mm_obj_warn (self, "Cannot run " OPERATION_DESCRIPTION " operation from %s: %s",
                     g_file_peek_path (ctx->current), error->message);
    } else
        mm_obj_dbg (self, OPERATION_DESCRIPTION " operation successfully from %s",
                    g_file_peek_path (ctx->current));

    g_clear_object (&ctx->current);
    connection_run_next (task);
}

static void
connection_run_next (GTask *task)
{
    MMDispatcherConnection *self;
    ConnectionRunContext   *ctx;
    g_autofree gchar       *path = NULL;
    GPtrArray              *aux;
    g_auto(GStrv)           argv = NULL;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!ctx->dispatcher_scripts) {
        if (ctx->n_failures)
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "Failed %u " OPERATION_DESCRIPTION " operations",
                                     ctx->n_failures);
        else
            g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* store current file reference in context */
    ctx->current = ctx->dispatcher_scripts->data;
    ctx->dispatcher_scripts = g_list_delete_link (ctx->dispatcher_scripts, ctx->dispatcher_scripts);
    path = g_file_get_path (ctx->current);

    /* build argv */
    aux = g_ptr_array_new ();
    g_ptr_array_add (aux, g_steal_pointer (&path));
    g_ptr_array_add (aux, g_strdup (ctx->modem_dbus_path));
    g_ptr_array_add (aux, g_strdup (ctx->bearer_dbus_path));
    g_ptr_array_add (aux, g_strdup (ctx->data_port));
    g_ptr_array_add (aux, g_strdup (ctx->connected ? CONNECTED_STRING : DISCONNECTED_STRING));
    g_ptr_array_add (aux, NULL);
    argv = (GStrv) g_ptr_array_free (aux, FALSE);

    /* run */
    mm_dispatcher_run (MM_DISPATCHER (self),
                       argv,
                       MAX_CONNECTION_EXEC_TIME_SECS,
                       g_task_get_cancellable (task),
                       (GAsyncReadyCallback) dispatcher_run_ready,
                       task);
}

static gint
dispatcher_script_cmp (GFile *a,
                       GFile *b)
{
    g_autofree gchar *a_name = NULL;
    g_autofree gchar *b_name = NULL;

    a_name = g_file_get_basename (a);
    b_name = g_file_get_basename (b);

    return g_strcmp0 (a_name, b_name);
}

void
mm_dispatcher_connection_run (MMDispatcherConnection *self,
                              const gchar            *modem_dbus_path,
                              const gchar            *bearer_dbus_path,
                              const gchar            *data_port,
                              gboolean                connected,
                              GCancellable           *cancellable,
                              GAsyncReadyCallback     callback,
                              gpointer                user_data)
{
    GTask                *task;
    ConnectionRunContext *ctx;
    guint                 i;
    const gchar          *enabled_dirs[] = {
        CONNECTIONDIRUSER,    /* sysconfdir */
        CONNECTIONDIRPACKAGE, /* libdir */
    };

    task = g_task_new (self, cancellable, callback, user_data);

    ctx = g_slice_new0 (ConnectionRunContext);
    ctx->modem_dbus_path = g_strdup (modem_dbus_path);
    ctx->bearer_dbus_path = g_strdup (bearer_dbus_path);
    ctx->data_port = g_strdup (data_port);
    ctx->connected = connected;
    g_task_set_task_data (task, ctx, (GDestroyNotify)connection_run_context_free);

    /* Iterate over all enabled dirs and collect all dispatcher script paths */
    for (i = 0; i < G_N_ELEMENTS (enabled_dirs); i++) {
        g_autoptr(GFile)            dir_file = NULL;
        g_autoptr(GFileEnumerator)  enumerator = NULL;
        GFile                      *child;

        dir_file = g_file_new_for_path (enabled_dirs[i]);
        enumerator = g_file_enumerate_children (dir_file,
                                                G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                G_FILE_QUERY_INFO_NONE,
                                                cancellable,
                                                NULL);
        if (!enumerator)
            continue;

        while (g_file_enumerator_iterate (enumerator, NULL, &child, cancellable, NULL) && child)
            ctx->dispatcher_scripts = g_list_prepend (ctx->dispatcher_scripts, g_object_ref (child));
    }

    /* Sort all by filename, regardless of the directory where they're in */
    ctx->dispatcher_scripts = g_list_sort (ctx->dispatcher_scripts, (GCompareFunc)dispatcher_script_cmp);

    connection_run_next (task);
}

/*****************************************************************************/

static void
mm_dispatcher_connection_init (MMDispatcherConnection *self)
{
}

static void
mm_dispatcher_connection_class_init (MMDispatcherConnectionClass *class)
{
}

MM_DEFINE_SINGLETON_GETTER (MMDispatcherConnection, mm_dispatcher_connection_get, MM_TYPE_DISPATCHER_CONNECTION,
                            MM_DISPATCHER_OPERATION_DESCRIPTION, OPERATION_DESCRIPTION)
