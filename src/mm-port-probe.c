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

struct _MMPortProbePrivate {
    /* Port and properties */
    GUdevDevice *port;
    gchar *subsys;
    gchar *name;
    gchar *physdev_path;
    gchar *driver;
};

GUdevDevice *
mm_port_probe_get_port (MMPortProbe *self)
{
    g_return_val_if_fail (MM_IS_PORT_PROBE (self), NULL);

    return self->priv->port;
}

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
