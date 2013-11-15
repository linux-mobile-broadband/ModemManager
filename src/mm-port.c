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
#include "mm-log.h"

G_DEFINE_TYPE (MMPort, mm_port, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_DEVICE,
    PROP_SUBSYS,
    PROP_TYPE,
    PROP_CONNECTED,

    LAST_PROP
};

#define MM_PORT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_PORT, MMPortPrivate))

typedef struct {
    char *device;
    MMPortSubsys subsys;
    MMPortType ptype;
    gboolean connected;
} MMPortPrivate;

/*****************************************************************************/

static GObject*
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
    GObject *object;
    MMPortPrivate *priv;

    object = G_OBJECT_CLASS (mm_port_parent_class)->constructor (type,
                                                                 n_construct_params,
                                                                 construct_params);
    if (!object)
        return NULL;

    priv = MM_PORT_GET_PRIVATE (object);

    if (!priv->device) {
        g_warning ("MMPort: no device provided");
        g_object_unref (object);
        return NULL;
    }

    if (priv->subsys == MM_PORT_SUBSYS_UNKNOWN) {
        g_warning ("MMPort: invalid port subsystem");
        g_object_unref (object);
        return NULL;
    }

    /* Can't have a TTY subsystem port that's unknown */
    if (   priv->subsys != MM_PORT_SUBSYS_NET
        && priv->ptype == MM_PORT_TYPE_UNKNOWN) {
        g_warning ("MMPort: invalid port type");
        g_object_unref (object);
        return NULL;
    }

    return object;
}


const char *
mm_port_get_device (MMPort *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_PORT (self), NULL);

    return MM_PORT_GET_PRIVATE (self)->device;
}

MMPortSubsys
mm_port_get_subsys (MMPort *self)
{
    g_return_val_if_fail (self != NULL, MM_PORT_SUBSYS_UNKNOWN);
    g_return_val_if_fail (MM_IS_PORT (self), MM_PORT_SUBSYS_UNKNOWN);

    return MM_PORT_GET_PRIVATE (self)->subsys;
}

MMPortType
mm_port_get_port_type (MMPort *self)
{
    g_return_val_if_fail (self != NULL, MM_PORT_TYPE_UNKNOWN);
    g_return_val_if_fail (MM_IS_PORT (self), MM_PORT_TYPE_UNKNOWN);

    return MM_PORT_GET_PRIVATE (self)->ptype;
}

gboolean
mm_port_get_connected (MMPort *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_PORT (self), FALSE);

    return MM_PORT_GET_PRIVATE (self)->connected;
}

void
mm_port_set_connected (MMPort *self, gboolean connected)
{
    MMPortPrivate *priv;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_PORT (self));

    priv = MM_PORT_GET_PRIVATE (self);
    if (priv->connected != connected) {
        priv->connected = connected;
        g_object_notify (G_OBJECT (self), MM_PORT_CONNECTED);

        mm_dbg ("(%s): port now %s",
                priv->device,
                connected ? "connected" : "disconnected");
    }
}

/*****************************************************************************/

static void
mm_port_init (MMPort *self)
{
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMPortPrivate *priv = MM_PORT_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_DEVICE:
        /* Construct only */
        priv->device = g_value_dup_string (value);
        break;
    case PROP_SUBSYS:
        /* Construct only */
        priv->subsys = g_value_get_uint (value);
        break;
    case PROP_TYPE:
        /* Construct only */
        priv->ptype = g_value_get_uint (value);
        break;
    case PROP_CONNECTED:
        priv->connected = g_value_get_boolean (value);
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
    MMPortPrivate *priv = MM_PORT_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_DEVICE:
        g_value_set_string (value, priv->device);
        break;
    case PROP_SUBSYS:
        g_value_set_uint (value, priv->subsys);
        break;
    case PROP_TYPE:
        g_value_set_uint (value, priv->ptype);
        break;
    case PROP_CONNECTED:
        g_value_set_boolean (value, priv->connected);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMPortPrivate *priv = MM_PORT_GET_PRIVATE (object);

    g_free (priv->device);

    G_OBJECT_CLASS (mm_port_parent_class)->finalize (object);
}

static void
mm_port_class_init (MMPortClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMPortPrivate));

    /* Virtual methods */
    object_class->constructor = constructor;
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

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
}
