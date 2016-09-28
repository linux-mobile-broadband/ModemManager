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
 * Copyright (C) 2016 Velocloud, Inc.
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-kernel-device-generic.h"
#include "mm-log.h"

static void initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMKernelDeviceGeneric, mm_kernel_device_generic,  MM_TYPE_KERNEL_DEVICE, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

enum {
    PROP_0,
    PROP_PROPERTIES,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMKernelDeviceGenericPrivate {
    /* Input properties */
    MMKernelEventProperties *properties;
};

/*****************************************************************************/

static const gchar *
kernel_device_get_subsystem (MMKernelDevice *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), NULL);

    return mm_kernel_event_properties_get_subsystem (MM_KERNEL_DEVICE_GENERIC (self)->priv->properties);
}

static const gchar *
kernel_device_get_name (MMKernelDevice *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), NULL);

    return mm_kernel_event_properties_get_name (MM_KERNEL_DEVICE_GENERIC (self)->priv->properties);
}

static const gchar *
kernel_device_get_sysfs_path (MMKernelDevice *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), NULL);

    return NULL;
}

static const gchar *
kernel_device_get_parent_sysfs_path (MMKernelDevice *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), NULL);

    return NULL;
}

static const gchar *
kernel_device_get_physdev_uid (MMKernelDevice *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), NULL);

    /* Prefer the one coming in the properties, if any */
    return mm_kernel_event_properties_get_uid (MM_KERNEL_DEVICE_GENERIC (self)->priv->properties);
}

static const gchar *
kernel_device_get_driver (MMKernelDevice *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), NULL);

    return NULL;
}

static guint16
kernel_device_get_physdev_vid (MMKernelDevice *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), 0);

    return 0;
}

static guint16
kernel_device_get_physdev_pid (MMKernelDevice *self)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), 0);

    return 0;
}

static gboolean
kernel_device_is_candidate (MMKernelDevice *_self,
                            gboolean        manual_scan)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (_self), FALSE);

    return TRUE;
}

static gboolean
kernel_device_cmp (MMKernelDevice *a,
                   MMKernelDevice *b)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (a), FALSE);
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (b), FALSE);

    return (!g_strcmp0 (mm_kernel_device_get_subsystem (a), mm_kernel_device_get_subsystem (b)) &&
            !g_strcmp0 (mm_kernel_device_get_name      (a), mm_kernel_device_get_name      (b)));
}

static gboolean
kernel_device_has_property (MMKernelDevice *self,
                            const gchar    *property)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), FALSE);

    return FALSE;
}

static const gchar *
kernel_device_get_property (MMKernelDevice *self,
                            const gchar    *property)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), NULL);

    return NULL;
}

static gboolean
kernel_device_get_property_as_boolean (MMKernelDevice *self,
                                       const gchar    *property)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), FALSE);

    return FALSE;
}

static gint
kernel_device_get_property_as_int (MMKernelDevice *self,
                                   const gchar    *property)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE_GENERIC (self), -1);

    return 0;
}

/*****************************************************************************/

MMKernelDevice *
mm_kernel_device_generic_new (MMKernelEventProperties  *properties,
                              GError                  **error)
{
    g_return_val_if_fail (MM_IS_KERNEL_EVENT_PROPERTIES (properties), NULL);

    return MM_KERNEL_DEVICE (g_initable_new (MM_TYPE_KERNEL_DEVICE_GENERIC,
                                             NULL,
                                             error,
                                             "properties", properties,
                                             NULL));
}

/*****************************************************************************/

static void
mm_kernel_device_generic_init (MMKernelDeviceGeneric *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_KERNEL_DEVICE_GENERIC, MMKernelDeviceGenericPrivate);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMKernelDeviceGeneric *self = MM_KERNEL_DEVICE_GENERIC (object);

    switch (prop_id) {
    case PROP_PROPERTIES:
        g_assert (!self->priv->properties);
        self->priv->properties = g_value_dup_object (value);
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
    MMKernelDeviceGeneric *self = MM_KERNEL_DEVICE_GENERIC (object);

    switch (prop_id) {
    case PROP_PROPERTIES:
        g_value_set_object (value, self->priv->properties);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
    MMKernelDeviceGeneric *self = MM_KERNEL_DEVICE_GENERIC (initable);
    const gchar *subsystem;

    subsystem = mm_kernel_device_get_subsystem (MM_KERNEL_DEVICE (self));
    if (!subsystem) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "subsystem is mandatory in kernel device");
        return FALSE;
    }

    if (!g_str_equal (subsystem, "virtual")) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "only virtual subsystem supported");
        return FALSE;
    }

    if (!mm_kernel_device_get_name (MM_KERNEL_DEVICE (self))) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "name is mandatory in kernel device");
        return FALSE;
    }

    return TRUE;
}

static void
dispose (GObject *object)
{
    MMKernelDeviceGeneric *self = MM_KERNEL_DEVICE_GENERIC (object);

    g_clear_object (&self->priv->properties);

    G_OBJECT_CLASS (mm_kernel_device_generic_parent_class)->dispose (object);
}

static void
initable_iface_init (GInitableIface *iface)
{
    iface->init = initable_init;
}

static void
mm_kernel_device_generic_class_init (MMKernelDeviceGenericClass *klass)
{
    GObjectClass        *object_class        = G_OBJECT_CLASS (klass);
    MMKernelDeviceClass *kernel_device_class = MM_KERNEL_DEVICE_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMKernelDeviceGenericPrivate));

    object_class->dispose      = dispose;
    object_class->get_property = get_property;
    object_class->set_property = set_property;

    kernel_device_class->get_subsystem           = kernel_device_get_subsystem;
    kernel_device_class->get_name                = kernel_device_get_name;
    kernel_device_class->get_driver              = kernel_device_get_driver;
    kernel_device_class->get_sysfs_path          = kernel_device_get_sysfs_path;
    kernel_device_class->get_physdev_uid         = kernel_device_get_physdev_uid;
    kernel_device_class->get_physdev_vid         = kernel_device_get_physdev_vid;
    kernel_device_class->get_physdev_pid         = kernel_device_get_physdev_pid;
    kernel_device_class->get_parent_sysfs_path   = kernel_device_get_parent_sysfs_path;
    kernel_device_class->is_candidate            = kernel_device_is_candidate;
    kernel_device_class->cmp                     = kernel_device_cmp;
    kernel_device_class->has_property            = kernel_device_has_property;
    kernel_device_class->get_property            = kernel_device_get_property;
    kernel_device_class->get_property_as_boolean = kernel_device_get_property_as_boolean;
    kernel_device_class->get_property_as_int     = kernel_device_get_property_as_int;

    properties[PROP_PROPERTIES] =
        g_param_spec_object ("properties",
                             "Properties",
                             "Generic kernel event properties",
                             MM_TYPE_KERNEL_EVENT_PROPERTIES,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PROPERTIES, properties[PROP_PROPERTIES]);
}
