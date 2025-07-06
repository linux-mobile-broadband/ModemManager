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
 * Copyright 2025 Dan Williams <dan@ioncontrol.co>
 */

#include <gio/gio.h>

#include "mm-utils.h"

#include "mm-sleep-context.h"

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMSleepContext, mm_sleep_context, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

struct _MMSleepContextPrivate {
    GTask *task;
    guint  timeout_id;
    gint64 timeout_start;
};

enum {
    DONE,
    LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup_printf ("sleep-context");
}

/*****************************************************************************/

static void
timeout_cleanup (MMSleepContext *self)
{
    if (self->priv->timeout_id)
        g_source_remove (self->priv->timeout_id);
    self->priv->timeout_id = 0;
    self->priv->timeout_start = 0;
}

void
mm_sleep_context_complete (MMSleepContext *self,
                           GError         *error)
{
    timeout_cleanup (self);

    if (error) {
        mm_obj_dbg (self, "completing with error '%s'", error->message);
        g_task_return_error (self->priv->task, error);
    } else {
        mm_obj_dbg (self, "completing with success");
        g_task_return_boolean (self->priv->task, TRUE);
    }
    g_clear_object (&self->priv->task);
}

static gboolean
operation_timeout (MMSleepContext *self)
{
    mm_obj_dbg (self, "timeout");
    mm_sleep_context_complete (self,
                               g_error_new_literal (G_IO_ERROR,
                                                    G_IO_ERROR_TIMED_OUT,
                                                    "timeout waiting for sleep preparation to complete"));
    return G_SOURCE_REMOVE;
}

static void
operation_ready (MMSleepContext *self,
                 GAsyncResult   *res)
{
    g_autoptr(GError) error = NULL;
    gboolean          success;

    mm_obj_dbg (self, "notifying listeners task is done");

    success = g_task_propagate_boolean (G_TASK (res), &error);
    g_assert (success || error);
    g_signal_emit (self, signals[DONE], 0, error);
}

static void
timeout_add (MMSleepContext *self,
             guint           interval)
{
    timeout_cleanup (self);
    self->priv->timeout_start = g_get_monotonic_time ();
    self->priv->timeout_id = g_timeout_add_seconds (interval,
                                                    (GSourceFunc)operation_timeout,
                                                    self);
}

void
mm_sleep_context_timeout_backoff (MMSleepContext *self,
                                  guint           more_seconds)
{
    gint64 cur_time;
    gint64 new_interval;

    new_interval = more_seconds * G_USEC_PER_SEC;
    if (self->priv->timeout_start) {
        cur_time = g_get_monotonic_time ();
        new_interval += (cur_time - self->priv->timeout_start);
    }

    timeout_add (self, new_interval / G_USEC_PER_SEC);
}

/*****************************************************************************/

MMSleepContext *
mm_sleep_context_new (guint timeout_seconds)
{
    MMSleepContext *self;

    self = MM_SLEEP_CONTEXT (g_object_new (MM_TYPE_SLEEP_CONTEXT, NULL));
    self->priv->task = g_task_new (self,
                                   NULL,
                                   (GAsyncReadyCallback)operation_ready,
                                   NULL);
    timeout_add (self, timeout_seconds);
    mm_obj_dbg (self, "new context with %d second timeout", timeout_seconds);

    return self;
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_sleep_context_init (MMSleepContext *self)
{
    /* Initialize opaque pointer to private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_SLEEP_CONTEXT,
                                              MMSleepContextPrivate);
}

static void
dispose (GObject *object)
{
    MMSleepContext *self = MM_SLEEP_CONTEXT (object);

    timeout_cleanup (self);
    /* Task must always be completed and cleared before disposal */
    g_assert (self->priv->task == NULL);

    G_OBJECT_CLASS (mm_sleep_context_parent_class)->dispose (object);
}

static void
mm_sleep_context_class_init (MMSleepContextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSleepContextPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;

    signals[DONE] = g_signal_new (MM_SLEEP_CONTEXT_DONE,
                                  G_OBJECT_CLASS_TYPE (object_class),
                                  G_SIGNAL_RUN_FIRST,
                                  G_STRUCT_OFFSET (MMSleepContextClass, done),
                                  NULL, /* accumulator      */
                                  NULL, /* accumulator data */
                                  g_cclosure_marshal_generic,
                                  G_TYPE_NONE,
                                  1,
                                  G_TYPE_ERROR);
}
