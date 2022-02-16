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
#include "mm-dispatcher.h"

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMDispatcher, mm_dispatcher, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

enum {
    PROP_0,
    PROP_OPERATION_DESCRIPTION,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMDispatcherPrivate {
    gchar               *operation_description;
    GSubprocessLauncher *launcher;
};

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMDispatcher *self = MM_DISPATCHER (_self);

    return g_strdup_printf ("%s dispatcher", self->priv->operation_description);
}

/*****************************************************************************/

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

/*****************************************************************************/

typedef struct {
    GSubprocess *subprocess;
    guint        timeout_id;
} RunContext;

static void
run_context_free (RunContext *ctx)
{
    g_assert (!ctx->timeout_id);
    g_clear_object (&ctx->subprocess);
    g_slice_free (RunContext, ctx);
}

gboolean
mm_dispatcher_run_finish (MMDispatcher  *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean
subprocess_wait_timed_out (GTask *task)
{
    MMDispatcher *self;
    RunContext   *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_obj_warn (self, "forcing exit on %s operation", self->priv->operation_description);
    g_subprocess_force_exit (ctx->subprocess);

    ctx->timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static void
subprocess_wait_ready (GSubprocess  *subprocess,
                       GAsyncResult *res,
                       GTask        *task)
{
    GError       *error = NULL;
    MMDispatcher *self;
    RunContext   *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* cleanup timeout before any return */
    if (ctx->timeout_id) {
        g_source_remove (ctx->timeout_id);
        ctx->timeout_id = 0;
    }

    if (!g_subprocess_wait_finish (subprocess, res, &error)) {
        g_prefix_error (&error, "%s operation wait failed: ", self->priv->operation_description);
        g_task_return_error (task, error);
    } else if (!g_subprocess_get_successful (subprocess)) {
        if (g_subprocess_get_if_signaled (subprocess))
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "%s operation aborted with signal %d",
                                     self->priv->operation_description,
                                     g_subprocess_get_term_sig (subprocess));
        else if (g_subprocess_get_if_exited (subprocess))
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "%s operation finished with status %d",
                                     self->priv->operation_description,
                                     g_subprocess_get_exit_status (subprocess));
        else
            g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                     "%s operation failed", self->priv->operation_description);
    } else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_dispatcher_run (MMDispatcher        *self,
                   const GStrv          argv,
                   guint                timeout_secs,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask      *task;
    RunContext *ctx;
    GError     *error = NULL;

    g_assert (g_strv_length (argv) >= 1);

    task = g_task_new (self, cancellable, callback, user_data);
    ctx = g_slice_new0 (RunContext);
    g_task_set_task_data (task, ctx, (GDestroyNotify) run_context_free);

    /* Validation checks to see if we should run it or not */
    if (!validate_file (argv[0], &error)) {
        g_prefix_error (&error, "Cannot run %s operation from %s: ",
                        self->priv->operation_description, argv[0]);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* create and launch subprocess */
    ctx->subprocess = g_subprocess_launcher_spawnv (self->priv->launcher,
                                                    (const gchar * const *)argv,
                                                    &error);
    if (!ctx->subprocess) {
        g_prefix_error (&error, "%s operation launch from %s failed: ",
                        self->priv->operation_description, argv[0]);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* setup timeout */
    ctx->timeout_id = g_timeout_add_seconds (timeout_secs,
                                             (GSourceFunc)subprocess_wait_timed_out,
                                             task);

    /* wait for subprocess exit */
    g_subprocess_wait_async (ctx->subprocess,
                             cancellable,
                             (GAsyncReadyCallback)subprocess_wait_ready,
                             task);
}

/*****************************************************************************/

static void
mm_dispatcher_init (MMDispatcher *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_DISPATCHER, MMDispatcherPrivate);

    /* Create launcher and inherit parent's environment */
    self->priv->launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_subprocess_launcher_set_environ (self->priv->launcher, NULL);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMDispatcher *self = MM_DISPATCHER (object);

    switch (prop_id) {
    case PROP_OPERATION_DESCRIPTION:
        /* construct only */
        self->priv->operation_description = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MMDispatcher *self = MM_DISPATCHER (object);

    switch (prop_id) {
    case PROP_OPERATION_DESCRIPTION:
        g_value_set_string (value, self->priv->operation_description);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
    MMDispatcher *self = MM_DISPATCHER (object);

    g_clear_object (&self->priv->launcher);

    G_OBJECT_CLASS (mm_dispatcher_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    MMDispatcher *self = MM_DISPATCHER (object);

    g_free (self->priv->operation_description);

    G_OBJECT_CLASS (mm_dispatcher_parent_class)->finalize (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_dispatcher_class_init (MMDispatcherClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);

    g_type_class_add_private (object_class, sizeof (MMDispatcherPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->dispose = dispose;
    object_class->finalize = finalize;

    properties[PROP_OPERATION_DESCRIPTION] =
        g_param_spec_string (MM_DISPATCHER_OPERATION_DESCRIPTION,
                             "Operation description",
                             "String describing the operation, to be used in logging",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_OPERATION_DESCRIPTION, properties[PROP_OPERATION_DESCRIPTION]);
}
