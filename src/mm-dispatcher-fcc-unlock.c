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
 * Copyright (C) 2021-2022 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <sys/stat.h>

#include <ModemManager.h>
#include "mm-errors-types.h"
#include "mm-utils.h"
#include "mm-log-object.h"
#include "mm-dispatcher-fcc-unlock.h"

#if !defined FCCUNLOCKDIRPACKAGE
# error FCCUNLOCKDIRPACKAGE must be defined at build time
#endif

#if !defined FCCUNLOCKDIRUSER
# error FCCUNLOCKDIRUSER must be defined at build time
#endif

#define OPERATION_DESCRIPTION "fcc unlock"

/* Maximum time a FCC unlock command is allowed to run before
 * us killing it */
#define MAX_FCC_UNLOCK_EXEC_TIME_SECS 5

struct _MMDispatcherFccUnlock {
    MMDispatcher parent;
};

struct _MMDispatcherFccUnlockClass {
    MMDispatcherClass parent;
};

G_DEFINE_TYPE (MMDispatcherFccUnlock, mm_dispatcher_fcc_unlock, MM_TYPE_DISPATCHER)

/*****************************************************************************/

gboolean
mm_dispatcher_fcc_unlock_run_finish (MMDispatcherFccUnlock  *self,
                                     GAsyncResult           *res,
                                     GError                **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
dispatcher_run_ready (MMDispatcher *self,
                      GAsyncResult *res,
                      GTask        *task)
{
    GError *error = NULL;

    if (!mm_dispatcher_run_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_dispatcher_fcc_unlock_run (MMDispatcherFccUnlock  *self,
                              guint                   vid,
                              guint                   pid,
                              const gchar            *modem_dbus_path,
                              const GStrv             modem_ports,
                              GCancellable           *cancellable,
                              GAsyncReadyCallback     callback,
                              gpointer                user_data)
{
    GTask            *task;
    guint             i;
    g_autofree gchar *filename = NULL;
    const gchar      *enabled_dirs[] = {
        FCCUNLOCKDIRUSER,    /* sysconfdir */
        FCCUNLOCKDIRPACKAGE, /* libdir */
    };

    task = g_task_new (self, cancellable, callback, user_data);

    filename = g_strdup_printf ("%04x:%04x", vid, pid);

    for (i = 0; i < G_N_ELEMENTS (enabled_dirs); i++) {
        GPtrArray        *aux;
        g_auto(GStrv)     argv = NULL;
        g_autofree gchar *path = NULL;
        g_autoptr(GFile)  file = NULL;
        guint             j;

        path = g_build_path (G_DIR_SEPARATOR_S, enabled_dirs[i], filename, NULL);
        file = g_file_new_for_path (path);

        /* If file exists, we attempt to use it */
        if (!g_file_query_exists (file, cancellable)) {
            mm_obj_dbg (self, "Cannot run " OPERATION_DESCRIPTION " operation from %s: file doesn't exist", path);
            continue;
        }

        /* build argv */
        aux = g_ptr_array_new ();
        g_ptr_array_add (aux, g_steal_pointer (&path));
        g_ptr_array_add (aux, g_strdup (modem_dbus_path));
        for (j = 0; modem_ports && modem_ports[j]; j++)
            g_ptr_array_add (aux, g_strdup (modem_ports[j]));
        g_ptr_array_add (aux, NULL);
        argv = (GStrv) g_ptr_array_free (aux, FALSE);

        /* run */
        mm_dispatcher_run (MM_DISPATCHER (self),
                           argv,
                           MAX_FCC_UNLOCK_EXEC_TIME_SECS,
                           cancellable,
                           (GAsyncReadyCallback) dispatcher_run_ready,
                           task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             OPERATION_DESCRIPTION " operation launch aborted: no valid program found");
    g_object_unref (task);
}

/*****************************************************************************/

static void
mm_dispatcher_fcc_unlock_init (MMDispatcherFccUnlock *self)
{
}

static void
mm_dispatcher_fcc_unlock_class_init (MMDispatcherFccUnlockClass *class)
{
}

MM_DEFINE_SINGLETON_GETTER (MMDispatcherFccUnlock, mm_dispatcher_fcc_unlock_get, MM_TYPE_DISPATCHER_FCC_UNLOCK,
                            MM_DISPATCHER_OPERATION_DESCRIPTION, OPERATION_DESCRIPTION)
