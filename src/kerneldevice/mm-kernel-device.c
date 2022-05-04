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

#include <config.h>
#include <string.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-kernel-device.h"
#include "mm-log-object.h"

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MMKernelDevice, mm_kernel_device, G_TYPE_OBJECT,
                                  G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

enum {
    PROP_0,
    PROP_LOWER_DEVICE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMKernelDevicePrivate {
    MMKernelDevice *lower_device;
};

/*****************************************************************************/

const gchar *
mm_kernel_device_get_subsystem (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_subsystem ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_subsystem (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_name (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_name ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_name (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_driver (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_driver ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_driver (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_sysfs_path (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_sysfs_path ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_sysfs_path (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_wwandev_sysfs_path (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_wwandev_sysfs_path ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_wwandev_sysfs_path (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_physdev_uid (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_uid (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_uid ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_uid (self) :
            NULL);
}

guint16
mm_kernel_device_get_physdev_vid (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_vid (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_vid ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_vid (self) :
            0);
}

guint16
mm_kernel_device_get_physdev_subsystem_vid (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_subsystem_vid (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_subsystem_vid ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_subsystem_vid (self) :
            0);
}

guint16
mm_kernel_device_get_physdev_pid (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_pid (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_pid ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_pid (self) :
            0);
}

guint16
mm_kernel_device_get_physdev_revision (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_revision (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_revision ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_revision (self) :
            0);
}

const gchar *
mm_kernel_device_get_physdev_subsystem (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_subsystem (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_subsystem ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_subsystem (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_physdev_sysfs_path (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_sysfs_path (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_sysfs_path ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_sysfs_path (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_physdev_manufacturer (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_manufacturer (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_manufacturer ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_manufacturer (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_physdev_product (MMKernelDevice *self)
{
    /* when a lower device is available, physdev info taken from it */
    if (self->priv->lower_device)
        return mm_kernel_device_get_physdev_product (self->priv->lower_device);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_product ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_physdev_product (self) :
            NULL);
}

MMKernelDevice *
mm_kernel_device_peek_lower_device (MMKernelDevice *self)
{
    return self->priv->lower_device;
}

gint
mm_kernel_device_get_interface_number (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_number ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_number (self) :
            -1);
}

gint
mm_kernel_device_get_interface_class (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_class ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_class (self) :
            -1);
}

gint
mm_kernel_device_get_interface_subclass (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_subclass ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_subclass (self) :
            -1);
}

gint
mm_kernel_device_get_interface_protocol (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_protocol ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_protocol (self) :
            -1);
}

const gchar *
mm_kernel_device_get_interface_sysfs_path (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_sysfs_path ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_sysfs_path (self) :
            NULL);
}

const gchar *
mm_kernel_device_get_interface_description (MMKernelDevice *self)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_description ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_interface_description (self) :
            NULL);
}

gboolean
mm_kernel_device_cmp (MMKernelDevice *a,
                      MMKernelDevice *b)
{
    if (G_OBJECT_TYPE (a) != G_OBJECT_TYPE (b))
        return FALSE;

    return (MM_KERNEL_DEVICE_GET_CLASS (a)->cmp ?
            MM_KERNEL_DEVICE_GET_CLASS (a)->cmp (a, b) :
            FALSE);
}

gboolean
mm_kernel_device_has_property (MMKernelDevice *self,
                               const gchar    *property)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->has_property ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->has_property (self, property) :
            FALSE);
}

const gchar *
mm_kernel_device_get_property (MMKernelDevice *self,
                               const gchar    *property)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_property ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_property (self, property) :
            NULL);
}

gboolean
mm_kernel_device_get_property_as_boolean (MMKernelDevice *self,
                                          const gchar    *property)
{
    const gchar *value;

    value = mm_kernel_device_get_property (self, property);
    return (value && mm_common_get_boolean_from_string (value, NULL));
}

gint
mm_kernel_device_get_property_as_int (MMKernelDevice *self,
                                      const gchar    *property)
{
    const gchar *value;
    gint         aux;

    value = mm_kernel_device_get_property (self, property);
    return ((value && mm_get_int_from_str (value, &aux)) ? aux : 0);
}

guint
mm_kernel_device_get_property_as_int_hex (MMKernelDevice *self,
                                          const gchar    *property)
{
    const gchar *value;
    guint        aux;

    value = mm_kernel_device_get_property (self, property);
    return ((value && mm_get_uint_from_hex_str (value, &aux)) ? aux : 0);
}

gboolean
mm_kernel_device_has_global_property (MMKernelDevice *self,
                                      const gchar    *property)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->has_global_property ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->has_global_property (self, property) :
            FALSE);
}

const gchar *
mm_kernel_device_get_global_property (MMKernelDevice *self,
                                      const gchar    *property)
{
    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_global_property ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_global_property (self, property) :
            NULL);
}

gboolean
mm_kernel_device_get_global_property_as_boolean (MMKernelDevice *self,
                                                 const gchar    *property)
{
    const gchar *value;

    value = mm_kernel_device_get_global_property (self, property);
    return (value && mm_common_get_boolean_from_string (value, NULL));
}

gint
mm_kernel_device_get_global_property_as_int (MMKernelDevice *self,
                                             const gchar    *property)
{
    const gchar *value;
    gint         aux;

    value = mm_kernel_device_get_global_property (self, property);
    return ((value && mm_get_int_from_str (value, &aux)) ? aux : 0);
}

guint
mm_kernel_device_get_global_property_as_int_hex (MMKernelDevice *self,
                                                 const gchar    *property)
{
    const gchar *value;
    guint        aux;

    value = mm_kernel_device_get_global_property (self, property);
    return ((value && mm_get_uint_from_hex_str (value, &aux)) ? aux : 0);
}

gboolean
mm_kernel_device_has_attribute (MMKernelDevice *self,
                                const gchar    *attribute)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE (self), FALSE);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->has_attribute ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->has_attribute (self, attribute) :
            FALSE);
}

const gchar *
mm_kernel_device_get_attribute (MMKernelDevice *self,
                                const gchar    *attribute)
{
    g_return_val_if_fail (MM_IS_KERNEL_DEVICE (self), NULL);

    return (MM_KERNEL_DEVICE_GET_CLASS (self)->get_attribute ?
            MM_KERNEL_DEVICE_GET_CLASS (self)->get_attribute (self, attribute) :
            NULL);
}

gboolean
mm_kernel_device_get_attribute_as_boolean (MMKernelDevice *self,
                                           const gchar    *attribute)
{
    const gchar *value;

    value = mm_kernel_device_get_attribute (self, attribute);
    return (value && mm_common_get_boolean_from_string (value, NULL));
}

gint
mm_kernel_device_get_attribute_as_int (MMKernelDevice *self,
                                       const gchar    *attribute)
{
    const gchar *value;
    gint         aux;

    value = mm_kernel_device_get_attribute (self, attribute);
    return ((value && mm_get_int_from_str (value, &aux)) ? aux : 0);
}

guint
mm_kernel_device_get_attribute_as_int_hex (MMKernelDevice *self,
                                           const gchar    *attribute)
{
    const gchar *value;
    guint        aux;

    value = mm_kernel_device_get_attribute (self, attribute);
    return ((value && mm_get_uint_from_hex_str (value, &aux)) ? aux : 0);
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMKernelDevice *self;

    self = MM_KERNEL_DEVICE (_self);
    return g_strdup (mm_kernel_device_get_name (self));
}

/*****************************************************************************/

static void
mm_kernel_device_init (MMKernelDevice *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_KERNEL_DEVICE, MMKernelDevicePrivate);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMKernelDevice *self = MM_KERNEL_DEVICE (object);

    switch (prop_id) {
    case PROP_LOWER_DEVICE:
        g_clear_object (&self->priv->lower_device);
        self->priv->lower_device = g_value_dup_object (value);
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
    MMKernelDevice *self = MM_KERNEL_DEVICE (object);

    switch (prop_id) {
    case PROP_LOWER_DEVICE:
        g_value_set_object (value, self->priv->lower_device);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
dispose (GObject *object)
{
    MMKernelDevice *self = MM_KERNEL_DEVICE (object);

    g_clear_object (&self->priv->lower_device);

    G_OBJECT_CLASS (mm_kernel_device_parent_class)->dispose (object);
}

static void
mm_kernel_device_class_init (MMKernelDeviceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMKernelDevicePrivate));

    object_class->dispose      = dispose;
    object_class->get_property = get_property;
    object_class->set_property = set_property;

    properties[PROP_LOWER_DEVICE] =
        g_param_spec_object ("lower-device",
                             "lower device",
                             "Lower real device, when this is a virtual one",
                             MM_TYPE_KERNEL_DEVICE,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_LOWER_DEVICE, properties[PROP_LOWER_DEVICE]);
}
