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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <config.h>
#include <sys/stat.h>

#include <ModemManager.h>
#include "mm-errors-types.h"
#include "mm-utils.h"
#include "mm-log-object.h"
#include "mm-fcc-unlock-dispatcher.h"

#if !defined FCCUNLOCKDIRPACKAGE
# error FCCUNLOCKDIRPACKAGE must be defined at build time
#endif

#if !defined FCCUNLOCKDIRUSER
# error FCCUNLOCKDIRUSER must be defined at build time
#endif

/* Maximum time a FCC unlock command is allowed to run before
 * us killing it */
#define MAX_FCC_UNLOCK_EXEC_TIME_SECS 5

struct _MMFccUnlockDispatcher {
    GObject              parent;
    GSubprocessLauncher *launcher;
};

struct _MMFccUnlockDispatcherClass {
    GObjectClass parent;
};

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMFccUnlockDispatcher, mm_fcc_unlock_dispatcher, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("fcc-unlock-dispatcher");
}

/*****************************************************************************/

typedef struct {
    gchar       *filename;
    GSubprocess *subprocess;
    guint        timeout_id;
} RunContext;

static void
run_context_free (RunContext *ctx)
{
    g_assert (!ctx->timeout_id);
    g_clear_object (&ctx->subprocess);
    g_free (ctx->filename);
    g_slice_free (RunContext, ctx);
}

gboolean
mm_fcc_unlock_dispatcher_run_finish (MMFccUnlockDispatcher  *self,
                                     GAsyncResult           *res,
                                     GError                **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
subprocess_wait_timed_out (GTask *task)
{
    MMFccUnlockDispatcher *self;
    RunContext            *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_obj_warn (self, "forcing exit on %s FCC unlock operation", ctx->filename);
    g_subprocess_force_exit (ctx->subprocess);

    ctx->timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static void
subprocess_wait_ready (GSubprocess  *subprocess,
                       GAsyncResult *res,
                       GTask        *task)
{
    GError     *error = NULL;
    RunContext *ctx;

    /* cleanup timeout before any return */
    ctx = g_task_get_task_data (task);
    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }

    if (!g_subprocess_wait_finish (subprocess, res, &error)) {
        g_prefix_error (&error, "FCC unlock operation wait failed: ");
        g_task_return_error (task, error);
    } else if (!g_subprocess_get_successful (subprocess)) {
        if (g_subprocess_get_if_signaled (subprocess))
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "FCC unlock operation aborted with signal %d",
                                     g_subprocess_get_term_sig (subprocess));
        else if (g_subprocess_get_if_exited (subprocess))
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "FCC unlock operation finished with status %d",
                                     g_subprocess_get_exit_status (subprocess));
        else
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "FCC unlock operation failed");
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static gboolean
validate_file (const gchar  *path,
               GError      **error)
{
    g_autoptr(GFile)     file = NULL;
    g_autoptr(GFileInfo) file_info = NULL;
    guint32              file_mode;
    guint32              file_uid;

    file = g_file_new_for_path (path);
    file_info = g_file_query_info (file,
                                   (G_FILE_ATTRIBUTE_STANDARD_SIZE           ","
                                    G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET ","
                                    G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK     ","
                                    G_FILE_ATTRIBUTE_UNIX_MODE               ","
                                    G_FILE_ATTRIBUTE_UNIX_UID),
                                   G_FILE_QUERY_INFO_NONE,
                                   NULL,
                                   error);
    if (!file_info)
        return FALSE;

    if (g_file_info_get_is_symlink (file_info)) {
        const gchar *link_target;

        link_target = g_file_info_get_symlink_target (file_info);
        if (g_strcmp0 (link_target, "/dev/null") == 0) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                         "Link '%s' to /dev/null is not executable", path);
            return FALSE;
        }
    }

    if (g_file_info_get_size (file_info) == 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                     "File '%s' is empty", path);
        return FALSE;
    }

    file_uid = g_file_info_get_attribute_uint32 (file_info, G_FILE_ATTRIBUTE_UNIX_UID);
    if (file_uid != 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                     "File '%s' not owned by root", path);
        return FALSE;
    }

    file_mode = g_file_info_get_attribute_uint32 (file_info, G_FILE_ATTRIBUTE_UNIX_MODE);
    if (!S_ISREG (file_mode)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                     "File '%s' is not regular", path);
        return FALSE;
    }
    if (file_mode & (S_IWGRP | S_IWOTH)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                     "File '%s' is writable by group or other", path);
        return FALSE;
    }
    if (file_mode & S_ISUID) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                     "File '%s' is set-UID", path);
        return FALSE;
    }
    if (!(file_mode & S_IXUSR)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                     "File '%s' is not executable by the owner", path);
        return FALSE;
    }

    return TRUE;
}

void
mm_fcc_unlock_dispatcher_run (MMFccUnlockDispatcher  *self,
                              guint                   vid,
                              guint                   pid,
                              const gchar            *modem_dbus_path,
                              const GStrv             modem_ports,
                              GCancellable           *cancellable,
                              GAsyncReadyCallback     callback,
                              gpointer                user_data)
{
    GTask            *task;
    RunContext       *ctx;
    guint             i;
    const gchar      *enabled_dirs[] = {
        FCCUNLOCKDIRUSER,    /* sysconfdir */
        FCCUNLOCKDIRPACKAGE, /* libdir */
    };

    task = g_task_new (self, cancellable, callback, user_data);
    ctx = g_slice_new0 (RunContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) run_context_free);

    ctx->filename = g_strdup_printf ("%04x:%04x", vid, pid);

    for (i = 0; i < G_N_ELEMENTS (enabled_dirs); i++) {
        GPtrArray        *aux;
        g_auto(GStrv)     argv = NULL;
        g_autofree gchar *path = NULL;
        g_autoptr(GError) error = NULL;
        guint             j;

        path = g_build_path (G_DIR_SEPARATOR_S, enabled_dirs[i], ctx->filename, NULL);

        /* Validation checks to see if we should run it or not */
        if (!validate_file (path, &error)) {
            mm_obj_dbg (self, "Cannot run FCC unlock operation from %s: %s",
                        path, error->message);
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

        /* create and launch subprocess */
        ctx->subprocess = g_subprocess_launcher_spawnv (self->launcher,
                                                        (const gchar * const *)argv,
                                                        &error);
        if (!ctx->subprocess) {
            g_prefix_error (&error, "FCC unlock operation launch from %s failed: ", path);
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        /* setup timeout */
        ctx->timeout_id = g_timeout_add_seconds (MAX_FCC_UNLOCK_EXEC_TIME_SECS,
                                                 (GSourceFunc)subprocess_wait_timed_out,
                                                 task);

        /* wait for subprocess exit */
        g_subprocess_wait_async (ctx->subprocess,
                                 cancellable,
                                 (GAsyncReadyCallback)subprocess_wait_ready,
                                 task);
        return;
    }

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "FCC unlock operation launch aborted: no valid program found");
    g_object_unref (task);
}

/*****************************************************************************/

static void
mm_fcc_unlock_dispatcher_init (MMFccUnlockDispatcher *self)
{
    self->launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);

    /* inherit parent's environment */
    g_subprocess_launcher_set_environ (self->launcher, NULL);
}

static void
dispose (GObject *object)
{
    MMFccUnlockDispatcher *self = MM_FCC_UNLOCK_DISPATCHER (object);

    g_clear_object (&self->launcher);

    G_OBJECT_CLASS (mm_fcc_unlock_dispatcher_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_fcc_unlock_dispatcher_class_init (MMFccUnlockDispatcherClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);

    object_class->dispose = dispose;
}

MM_DEFINE_SINGLETON_GETTER (MMFccUnlockDispatcher, mm_fcc_unlock_dispatcher_get, MM_TYPE_FCC_UNLOCK_DISPATCHER)
