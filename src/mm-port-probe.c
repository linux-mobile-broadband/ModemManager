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
 * Copyright (C) 2011 - Aleksander Morgado <aleksander@gnu.org>
 */

#include <stdio.h>
#include <stdlib.h>

#include "mm-port-probe.h"
#include "mm-errors.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMPortProbe, mm_port_probe, G_TYPE_OBJECT)

typedef struct {
    /* ---- Generic task context ---- */
    GSimpleAsyncResult *result;
} PortProbeRunTask;

struct _MMPortProbePrivate {
    /* Port and properties */
    GUdevDevice *port;
    gchar *subsys;
    gchar *name;
    gchar *physdev_path;
    gchar *driver;

    /* Current probing task. Only one can be available at a time */
    PortProbeRunTask *task;
};

static void
port_probe_run_task_free (PortProbeRunTask *task)
{
    g_object_unref (task->result);
    g_free (task);
}

static void
port_probe_run_task_complete (PortProbeRunTask *task,
                              gboolean complete_in_idle,
                              gboolean result,
                              GError *error)
{
    if (error)
        g_simple_async_result_take_error (task->result, error);
    else
        g_simple_async_result_set_op_res_gboolean (task->result, result);

    if (complete_in_idle)
        g_simple_async_result_complete_in_idle (task->result);
    else
        g_simple_async_result_complete (task->result);
}

gboolean
mm_port_probe_run_finish (MMPortProbe *self,
                          GAsyncResult *result,
                          GError **error)
{
    gboolean res;

    g_return_val_if_fail (MM_IS_PORT_PROBE (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

    /* Propagate error, if any */
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
        res = FALSE;
    else
        res = g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (result));

    /* Cleanup probing task */
    if (self->priv->task) {
        port_probe_run_task_free (self->priv->task);
        self->priv->task = NULL;
    }
    return res;
}

void
mm_port_probe_run (MMPortProbe *self,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    PortProbeRunTask *task;

    g_return_if_fail (MM_IS_PORT_PROBE (self));
    g_return_if_fail (callback != NULL);

    /* Shouldn't schedule more than one probing at a time */
    g_assert (self->priv->task == NULL);

    task = g_new0 (PortProbeRunTask, 1);
    task->result = g_simple_async_result_new (G_OBJECT (self),
                                              callback,
                                              user_data,
                                              mm_port_probe_run);

    /* Store as current task */
    self->priv->task = task;

    /* For now, just set successful */
    port_probe_run_task_complete (task, TRUE, TRUE, NULL);
}

GUdevDevice *
mm_port_probe_get_port (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->port;
};

const gchar *
mm_port_probe_get_port_name (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->name;
}

const gchar *
mm_port_probe_get_port_subsys (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->subsys;
}

const gchar *
mm_port_probe_get_port_physdev (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->physdev_path;
}

const gchar *
mm_port_probe_get_port_driver (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->driver;
}

MMPortProbe *
mm_port_probe_new (GUdevDevice *port,
                   const gchar *physdev_path,
                   const gchar *driver)
{
    MMPortProbe *self;

    self = MM_PORT_PROBE (g_object_new (MM_TYPE_PORT_PROBE, NULL));
    self->priv->port = g_object_ref (port);
    self->priv->subsys = g_strdup (g_udev_device_get_subsystem (port));
    self->priv->name = g_strdup (g_udev_device_get_name (port));
    self->priv->physdev_path = g_strdup (physdev_path);
    self->priv->driver = g_strdup (driver);

    return self;
}

static void
mm_port_probe_init (MMPortProbe *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),                  \
                                              MM_TYPE_PORT_PROBE,      \
                                              MMPortProbePrivate);
}

static void
finalize (GObject *object)
{
    MMPortProbe *self = MM_PORT_PROBE (object);

    /* We should never have a task here */
    g_assert (self->priv->task == NULL);

    g_free (self->priv->subsys);
    g_free (self->priv->name);
    g_free (self->priv->physdev_path);
    g_free (self->priv->driver);
    g_object_unref (self->priv->port);

    G_OBJECT_CLASS (mm_port_probe_parent_class)->finalize (object);
}

static void
mm_port_probe_class_init (MMPortProbeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortProbePrivate));

    /* Virtual methods */
    object_class->finalize = finalize;
}
