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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mm-port.h"
#include "mm-port-enums-types.h"
#include "mm-log-object.h"

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMPort, mm_port, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

enum {
    PROP_0,
    PROP_DEVICE,
    PROP_SUBSYS,
    PROP_TYPE,
    PROP_CONNECTED,
    PROP_KERNEL_DEVICE,
    LAST_PROP
};

struct _MMPortPrivate {
    gchar *device;
    MMPortSubsys subsys;
    MMPortType ptype;
    gboolean connected;
    MMKernelDevice *kernel_device;
};

/*****************************************************************************/

const char *
mm_port_get_device (MMPort *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_PORT (self), NULL);

    return self->priv->device;
}

MMPortSubsys
mm_port_get_subsys (MMPort *self)
{
    g_return_val_if_fail (self != NULL, MM_PORT_SUBSYS_UNKNOWN);
    g_return_val_if_fail (MM_IS_PORT (self), MM_PORT_SUBSYS_UNKNOWN);

    return self->priv->subsys;
}

MMPortType
mm_port_get_port_type (MMPort *self)
{
    g_return_val_if_fail (self != NULL, MM_PORT_TYPE_UNKNOWN);
    g_return_val_if_fail (MM_IS_PORT (self), MM_PORT_TYPE_UNKNOWN);

    return self->priv->ptype;
}

gboolean
mm_port_get_connected (MMPort *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PORT (self), FALSE);

    return self->priv->connected;
}

void
mm_port_set_connected (MMPort *self, gboolean connected)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_PORT (self));

    if (self->priv->connected != connected) {
        self->priv->connected = connected;
        g_object_notify (G_OBJECT (self), MM_PORT_CONNECTED);
        mm_obj_dbg (self, "port now %s", connected ? "connected" : "disconnected");
    }
}

MMKernelDevice *
mm_port_peek_kernel_device (MMPort *self)
{
    g_return_val_if_fail (MM_IS_PORT (self), NULL);

    return self->priv->kernel_device;
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMPort *self;

    self = MM_PORT (_self);
    return g_strdup_printf ("%s/%s",
                            mm_port_get_device (self),
                            mm_port_type_get_string (mm_port_get_port_type (self)));
}

/*****************************************************************************/

static void
mm_port_init (MMPort *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_PORT, MMPortPrivate);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMPort *self = MM_PORT (object);

    switch (prop_id) {
    case PROP_DEVICE:
        /* Construct only */
        self->priv->device = g_value_dup_string (value);
        break;
    case PROP_SUBSYS:
        /* Construct only */
        self->priv->subsys = g_value_get_uint (value);
        break;
    case PROP_TYPE:
        /* Construct only */
        self->priv->ptype = g_value_get_uint (value);
        break;
    case PROP_CONNECTED:
        self->priv->connected = g_value_get_boolean (value);
        break;
    case PROP_KERNEL_DEVICE:
        /* Not construct only, but only set once */
        g_assert (!self->priv->kernel_device);
        self->priv->kernel_device = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMPort *self = MM_PORT (object);

    switch (prop_id) {
    case PROP_DEVICE:
        g_value_set_string (value, self->priv->device);
        break;
    case PROP_SUBSYS:
        g_value_set_uint (value, self->priv->subsys);
        break;
    case PROP_TYPE:
        g_value_set_uint (value, self->priv->ptype);
        break;
    case PROP_CONNECTED:
        g_value_set_boolean (value, self->priv->connected);
        break;
    case PROP_KERNEL_DEVICE:
        g_value_set_object (value, self->priv->kernel_device);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMPort *self = MM_PORT (object);

    g_free (self->priv->device);

    G_OBJECT_CLASS (mm_port_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMPort *self = MM_PORT (object);

    g_clear_object (&self->priv->kernel_device);

    G_OBJECT_CLASS (mm_port_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_port_class_init (MMPortClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    g_object_class_install_property
        (object_class, PROP_DEVICE,
         g_param_spec_string (MM_PORT_DEVICE,
                              "Device",
                              "Device",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_SUBSYS,
         g_param_spec_uint (MM_PORT_SUBSYS,
                            "Subsystem",
                            "Subsystem",
                            MM_PORT_SUBSYS_UNKNOWN,
                            MM_PORT_SUBSYS_LAST,
                            MM_PORT_SUBSYS_UNKNOWN,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_TYPE,
         g_param_spec_uint (MM_PORT_TYPE,
                            "Type",
                            "Type",
                            MM_PORT_TYPE_UNKNOWN,
                            MM_PORT_TYPE_LAST,
                            MM_PORT_TYPE_UNKNOWN,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_CONNECTED,
         g_param_spec_boolean (MM_PORT_CONNECTED,
                               "Connected",
                               "Is connected for data and not usable for control",
                               FALSE,
                               G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_KERNEL_DEVICE,
         g_param_spec_object (MM_PORT_KERNEL_DEVICE,
                              "Kernel device",
                              "kernel device object",
                              MM_TYPE_KERNEL_DEVICE,
                              G_PARAM_READWRITE));
}
