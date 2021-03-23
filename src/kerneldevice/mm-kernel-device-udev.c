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
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <string.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <ModemManager-tags.h>

#include "mm-kernel-device-udev.h"
#include "mm-log-object.h"

static void initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMKernelDeviceUdev, mm_kernel_device_udev,  MM_TYPE_KERNEL_DEVICE, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

enum {
    PROP_0,
    PROP_UDEV_DEVICE,
    PROP_PROPERTIES,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMKernelDeviceUdevPrivate {
    GUdevDevice *device;

    GUdevDevice *interface;
    GUdevDevice *physdev;
    guint16      vendor;
    guint16      product;
    guint16      revision;
    gchar       *driver;

    MMKernelEventProperties *properties;
};

/*****************************************************************************/

static guint
udev_device_get_sysfs_attr_as_hex (GUdevDevice *device,
                                   const gchar *attribute)
{
    const gchar *attr;
    guint        val = 0;

    attr = g_udev_device_get_sysfs_attr (device, attribute);
    if (attr)
        mm_get_uint_from_hex_str (attr, &val);
    return val;
}

/*****************************************************************************/

static void
preload_contents_other (MMKernelDeviceUdev *self)
{
    /* For any other kind of bus (or the absence of one, as in virtual devices),
     * assume this is a single port device and don't try to match multiple ports
     * together. Also, obviously, no vendor, product, revision or interface. */
    self->priv->driver = g_strdup (g_udev_device_get_driver (self->priv->device));
}

static void
preload_contents_platform (MMKernelDeviceUdev *self,
                           const gchar        *platform)
{
    GUdevDevice *iter;

    iter = g_object_ref (self->priv->device);
    while (iter) {
        GUdevDevice *parent;

        /* Store the first driver found */
        if (!self->priv->driver)
            self->priv->driver = g_strdup (g_udev_device_get_driver (iter));

        /* Take first parent with the given platform subsystem as physical device */
        if (!self->priv->physdev && (g_strcmp0 (g_udev_device_get_subsystem (iter), platform) == 0)) {
            self->priv->physdev = g_object_ref (iter);
            /* stop traversing as soon as the physical device is found */
            break;
        }

        parent = g_udev_device_get_parent (iter);
        g_clear_object (&iter);
        iter = parent;
    }

    g_clear_object (&iter);
}

static void
preload_contents_pcmcia (MMKernelDeviceUdev *self)
{
    GUdevDevice *iter;
    gboolean     pcmcia_subsystem_found = FALSE;

    iter = g_object_ref (self->priv->device);
    while (iter) {
        GUdevDevice *parent;

        /* Store the first driver found */
        if (!self->priv->driver)
            self->priv->driver = g_strdup (g_udev_device_get_driver (iter));

        if (g_strcmp0 (g_udev_device_get_subsystem (iter), "pcmcia") == 0)
            pcmcia_subsystem_found = TRUE;

        /* If the parent of this PCMCIA device is no longer part of
         * the PCMCIA subsystem, we want to stop since we're looking
         * for the base PCMCIA device, not the PCMCIA controller which
         * is usually PCI or some other bus type.
         */
        parent = g_udev_device_get_parent (iter);

        if (pcmcia_subsystem_found && parent && (g_strcmp0 (g_udev_device_get_subsystem (parent), "pcmcia") != 0)) {
            self->priv->vendor = udev_device_get_sysfs_attr_as_hex (iter, "manf_id");
            self->priv->product = udev_device_get_sysfs_attr_as_hex (iter, "card_id");
            self->priv->physdev = g_object_ref (iter);
            /* stop traversing as soon as the physical device is found */
            g_clear_object (&parent);
            break;
        }

        g_clear_object (&iter);
        iter = parent;
    }

    g_clear_object (&iter);
}

static void
preload_contents_pci (MMKernelDeviceUdev *self)
{
    GUdevDevice *iter;

    iter = g_object_ref (self->priv->device);
    while (iter) {
        GUdevDevice *parent;

        /* Store the first driver found */
        if (!self->priv->driver)
            self->priv->driver = g_strdup (g_udev_device_get_driver (iter));

        /* the PCI channel specific devices have their own drivers and
         * subsystems, we can rely on the physical device being the first
         * one that reports the 'pci' subsystem */
        if (!self->priv->physdev && (g_strcmp0 (g_udev_device_get_subsystem (iter), "pci") == 0)) {
            self->priv->vendor = udev_device_get_sysfs_attr_as_hex (iter, "vendor");
            self->priv->product = udev_device_get_sysfs_attr_as_hex (iter, "device");
            self->priv->revision = udev_device_get_sysfs_attr_as_hex (iter, "revision");
            self->priv->physdev = g_object_ref (iter);
            /* stop traversing as soon as the physical device is found */
            break;
        }

        parent = g_udev_device_get_parent (iter);
        g_clear_object (&iter);
        iter = parent;
    }

    g_clear_object (&iter);
}

static void
preload_contents_usb (MMKernelDeviceUdev *self)
{
    GUdevDevice *iter;

    iter = g_object_ref (self->priv->device);
    while (iter) {
        GUdevDevice *parent;
        const gchar *devtype;

        devtype = g_udev_device_get_devtype (iter);

        /* is this the USB interface? */
        if (!self->priv->interface && (g_strcmp0 (devtype, "usb_interface") == 0)) {
            self->priv->interface = g_object_ref (iter);
            self->priv->driver = g_strdup (g_udev_device_get_driver (iter));
        }

        /* is this the USB physdev? */
        if (!self->priv->physdev && (g_strcmp0 (devtype, "usb_device") == 0)) {
            self->priv->vendor = udev_device_get_sysfs_attr_as_hex (iter, "idVendor");
            self->priv->product = udev_device_get_sysfs_attr_as_hex (iter, "idProduct");
            self->priv->revision = udev_device_get_sysfs_attr_as_hex (iter, "bcdDevice");
            self->priv->physdev = g_object_ref (iter);
            /* stop traversing as soon as the physical device is found */
            break;
        }

        parent = g_udev_device_get_parent (iter);
        g_clear_object (&iter);
        iter = parent;
    }

    g_clear_object (&iter);
}

static gchar *
find_device_bus_subsystem (MMKernelDeviceUdev *self)
{
    GUdevDevice *iter;

    iter = g_object_ref (self->priv->device);
    while (iter) {
        const gchar *subsys;
        GUdevDevice *parent;

        /* stop search as soon as we find a parent object
         * of one of the supported bus subsystems */
        subsys = g_udev_device_get_subsystem (iter);
        if ((g_strcmp0 (subsys, "usb") == 0)      ||
            (g_strcmp0 (subsys, "pcmcia") == 0)   ||
            (g_strcmp0 (subsys, "pci") == 0)      ||
            (g_strcmp0 (subsys, "platform") == 0) ||
            (g_strcmp0 (subsys, "pnp") == 0)      ||
            (g_strcmp0 (subsys, "sdio") == 0)) {
            g_clear_object (&iter);
            return g_strdup (subsys);
        }

        parent = g_udev_device_get_parent (iter);
        g_clear_object (&iter);
        iter = parent;
    }

    /* no more parents to check */
    g_clear_object (&iter);
    return NULL;
}

static void
preload_contents (MMKernelDeviceUdev *self)
{
    g_autofree gchar *bus_subsys = NULL;

    bus_subsys = find_device_bus_subsystem (self);
    if (g_strcmp0 (bus_subsys, "usb") == 0)
        preload_contents_usb (self);
    else if (g_strcmp0 (bus_subsys, "pcmcia") == 0)
        preload_contents_pcmcia (self);
    else if (g_strcmp0 (bus_subsys, "pci") == 0)
        preload_contents_pci (self);
    else if ((g_strcmp0 (bus_subsys, "platform") == 0) ||
             (g_strcmp0 (bus_subsys, "pnp") == 0)      ||
             (g_strcmp0 (bus_subsys, "sdio") == 0))
        preload_contents_platform (self, bus_subsys);
    else
        preload_contents_other (self);

    if (!bus_subsys)
        return;

    mm_obj_dbg (self, "port contents loaded:");
    mm_obj_dbg (self, "  bus: %s", bus_subsys ? bus_subsys : "n/a");
    if (self->priv->interface)
        mm_obj_dbg (self, "  interface: %s", g_udev_device_get_sysfs_path (self->priv->interface));
    if (self->priv->physdev)
        mm_obj_dbg (self, "  device: %s", g_udev_device_get_sysfs_path (self->priv->physdev));
    if (self->priv->driver)
        mm_obj_dbg (self, "  driver: %s", self->priv->driver);
    if (self->priv->vendor)
        mm_obj_dbg (self, "  vendor: %04x", self->priv->vendor);
    if (self->priv->product)
        mm_obj_dbg (self, "  product: %04x", self->priv->product);
    if (self->priv->revision)
        mm_obj_dbg (self, "  revision: %04x", self->priv->revision);
}

/*****************************************************************************/

static const gchar *
kernel_device_get_subsystem (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);

    if (self->priv->device)
        return g_udev_device_get_subsystem (self->priv->device);

    g_assert (self->priv->properties);
    return mm_kernel_event_properties_get_subsystem (self->priv->properties);
}

static const gchar *
kernel_device_get_name (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);

    if (self->priv->device)
        return g_udev_device_get_name (self->priv->device);

    g_assert (self->priv->properties);
    return mm_kernel_event_properties_get_name (self->priv->properties);
}

static const gchar *
kernel_device_get_driver (MMKernelDevice *self)
{
    return MM_KERNEL_DEVICE_UDEV (self)->priv->driver;
}

static const gchar *
kernel_device_get_sysfs_path (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->device ?
            g_udev_device_get_sysfs_path (self->priv->device) :
            NULL);
}

static const gchar *
kernel_device_get_physdev_uid (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;
    const gchar        *uid = NULL;

    self = MM_KERNEL_DEVICE_UDEV (_self);

    /* Prefer the one coming in the properties, if any */
    if (self->priv->properties) {
        if ((uid = mm_kernel_event_properties_get_uid (self->priv->properties)) != NULL)
            return uid;
    }

    /* Try to load from properties set on the physical device */
    if ((uid = mm_kernel_device_get_global_property (_self, ID_MM_PHYSDEV_UID)) != NULL)
        return uid;

    /* Use physical device sysfs path, if any */
    if (self->priv->physdev && (uid = g_udev_device_get_sysfs_path (self->priv->physdev)) != NULL)
        return uid;

    /* If there is no physical device sysfs path, use the device sysfs itself */
    g_assert (self->priv->device);
    return g_udev_device_get_sysfs_path (self->priv->device);
}

static guint16
kernel_device_get_physdev_vid (MMKernelDevice *self)
{
    return MM_KERNEL_DEVICE_UDEV (self)->priv->vendor;
}

static guint16
kernel_device_get_physdev_pid (MMKernelDevice *self)
{
    return MM_KERNEL_DEVICE_UDEV (self)->priv->product;
}

static guint16
kernel_device_get_physdev_revision (MMKernelDevice *self)
{
    return MM_KERNEL_DEVICE_UDEV (self)->priv->revision;
}

static const gchar *
kernel_device_get_physdev_sysfs_path (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->physdev ? g_udev_device_get_sysfs_path (self->priv->physdev) : NULL);
}

static const gchar *
kernel_device_get_physdev_subsystem (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->physdev ? g_udev_device_get_subsystem (self->priv->physdev) : NULL);
}

static const gchar *
kernel_device_get_physdev_manufacturer (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->physdev ? g_udev_device_get_sysfs_attr (self->priv->physdev, "manufacturer") : NULL);
}

static const gchar *
kernel_device_get_physdev_product (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->physdev ? g_udev_device_get_sysfs_attr (self->priv->physdev, "product") : NULL);
}

static gint
kernel_device_get_interface_class (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->interface ? (gint) udev_device_get_sysfs_attr_as_hex (self->priv->interface, "bInterfaceClass") : -1);
}

static gint
kernel_device_get_interface_subclass (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->interface ? (gint) udev_device_get_sysfs_attr_as_hex (self->priv->interface, "bInterfaceSubClass") : -1);
}

static gint
kernel_device_get_interface_protocol (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->interface ? (gint) udev_device_get_sysfs_attr_as_hex (self->priv->interface, "bInterfaceProtocol") : -1);
}

static const gchar *
kernel_device_get_interface_sysfs_path (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->interface ? g_udev_device_get_sysfs_path (self->priv->interface) : NULL);
}

static const gchar *
kernel_device_get_interface_description (MMKernelDevice *_self)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->interface ? g_udev_device_get_sysfs_attr (self->priv->interface, "interface") : NULL);
}

static gboolean
kernel_device_cmp (MMKernelDevice *_a,
                   MMKernelDevice *_b)
{
    MMKernelDeviceUdev *a;
    MMKernelDeviceUdev *b;

    a = MM_KERNEL_DEVICE_UDEV (_a);
    b = MM_KERNEL_DEVICE_UDEV (_b);

    if (a->priv->device && b->priv->device) {
        if (g_udev_device_has_property (a->priv->device, "DEVPATH_OLD") &&
            g_str_has_suffix (g_udev_device_get_sysfs_path (b->priv->device),
                              g_udev_device_get_property   (a->priv->device, "DEVPATH_OLD")))
            return TRUE;

        if (g_udev_device_has_property (b->priv->device, "DEVPATH_OLD") &&
            g_str_has_suffix (g_udev_device_get_sysfs_path (a->priv->device),
                              g_udev_device_get_property   (b->priv->device, "DEVPATH_OLD")))
            return TRUE;

        return !g_strcmp0 (g_udev_device_get_sysfs_path (a->priv->device), g_udev_device_get_sysfs_path (b->priv->device));
    }

    return (!g_strcmp0 (mm_kernel_device_get_subsystem (_a), mm_kernel_device_get_subsystem (_b)) &&
            !g_strcmp0 (mm_kernel_device_get_name      (_a), mm_kernel_device_get_name      (_b)));
}

static gboolean
kernel_device_has_property (MMKernelDevice *_self,
                            const gchar    *property)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->device ? g_udev_device_has_property (self->priv->device, property) : FALSE);
}

static const gchar *
kernel_device_get_property (MMKernelDevice *_self,
                            const gchar    *property)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    return (self->priv->device ? g_udev_device_get_property (self->priv->device, property) : NULL);
}

static gboolean
kernel_device_has_global_property (MMKernelDevice *_self,
                                   const gchar    *property)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    if (self->priv->physdev && g_udev_device_has_property (self->priv->physdev, property))
        return TRUE;

    return kernel_device_has_property (_self, property);
}

static const gchar *
kernel_device_get_global_property (MMKernelDevice *_self,
                                   const gchar    *property)
{
    MMKernelDeviceUdev *self;
    const gchar        *str;

    self = MM_KERNEL_DEVICE_UDEV (_self);

    if (self->priv->physdev &&
        g_udev_device_has_property (self->priv->physdev, property) &&
        (str = g_udev_device_get_property (self->priv->physdev, property)) != NULL)
        return str;

    return kernel_device_get_property (_self, property);
}

/*****************************************************************************/

static gboolean
kernel_device_has_attribute (MMKernelDevice *_self,
                             const gchar    *attribute)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    if (!self->priv->device)
        return FALSE;

    return g_udev_device_has_sysfs_attr (self->priv->device, attribute);
}

static const gchar *
kernel_device_get_attribute (MMKernelDevice *_self,
                             const gchar    *attribute)
{
    MMKernelDeviceUdev *self;

    self = MM_KERNEL_DEVICE_UDEV (_self);
    if (!self->priv->device)
        return NULL;

    return g_udev_device_get_sysfs_attr (self->priv->device, attribute);
}

/*****************************************************************************/

MMKernelDevice *
mm_kernel_device_udev_new (GUdevDevice *udev_device)
{
    GError         *error = NULL;
    MMKernelDevice *self;


    self = MM_KERNEL_DEVICE (g_initable_new (MM_TYPE_KERNEL_DEVICE_UDEV,
                                             NULL,
                                             &error,
                                             "udev-device", udev_device,
                                             NULL));
    g_assert_no_error (error);
    return self;
}

/*****************************************************************************/

MMKernelDevice *
mm_kernel_device_udev_new_from_properties (MMKernelEventProperties  *props,
                                           GError                  **error)
{
    return MM_KERNEL_DEVICE (g_initable_new (MM_TYPE_KERNEL_DEVICE_UDEV,
                                             NULL,
                                             error,
                                             "properties", props,
                                             NULL));
}

/*****************************************************************************/

static void
mm_kernel_device_udev_init (MMKernelDeviceUdev *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_KERNEL_DEVICE_UDEV, MMKernelDeviceUdevPrivate);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMKernelDeviceUdev *self = MM_KERNEL_DEVICE_UDEV (object);

    switch (prop_id) {
    case PROP_UDEV_DEVICE:
        g_assert (!self->priv->device);
        self->priv->device = g_value_dup_object (value);
        break;
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
    MMKernelDeviceUdev *self = MM_KERNEL_DEVICE_UDEV (object);

    switch (prop_id) {
    case PROP_UDEV_DEVICE:
        g_value_set_object (value, self->priv->device);
        break;
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
    MMKernelDeviceUdev *self = MM_KERNEL_DEVICE_UDEV (initable);
    const gchar        *subsystem;
    const gchar        *name;

    /* When created from a GUdevDevice, we're done */
    if (self->priv->device) {
        preload_contents (self);
        return TRUE;
    }

    /* Otherwise, we do need properties with subsystem and name */
    if (!self->priv->properties) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "missing properties in kernel device");
        return FALSE;
    }

    subsystem = mm_kernel_event_properties_get_subsystem (self->priv->properties);
    if (!subsystem) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "subsystem is mandatory in kernel device");
        return FALSE;
    }

    name = mm_kernel_event_properties_get_name (self->priv->properties);
    if (!mm_kernel_device_get_name (MM_KERNEL_DEVICE (self))) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "name is mandatory in kernel device");
        return FALSE;
    }

    /* On remove events, we don't look for the GUdevDevice */
    if (g_strcmp0 (mm_kernel_event_properties_get_action (self->priv->properties), "remove")) {
        GUdevClient *client;
        GUdevDevice *device;

        client = g_udev_client_new (NULL);
        device = g_udev_client_query_by_subsystem_and_name (client, subsystem, name);
        if (!device) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                         "device %s/%s not found",
                         subsystem,
                         name);
            g_object_unref (client);
            return FALSE;
        }

        /* Store device */
        self->priv->device = device;
        g_object_unref (client);
    }

    if (self->priv->device)
        preload_contents (self);
    return TRUE;
}

static void
dispose (GObject *object)
{
    MMKernelDeviceUdev *self = MM_KERNEL_DEVICE_UDEV (object);

    g_clear_pointer (&self->priv->driver, g_free);
    g_clear_object  (&self->priv->physdev);
    g_clear_object  (&self->priv->interface);
    g_clear_object  (&self->priv->device);
    g_clear_object  (&self->priv->properties);

    G_OBJECT_CLASS (mm_kernel_device_udev_parent_class)->dispose (object);
}

static void
initable_iface_init (GInitableIface *iface)
{
    iface->init = initable_init;
}

static void
mm_kernel_device_udev_class_init (MMKernelDeviceUdevClass *klass)
{
    GObjectClass        *object_class        = G_OBJECT_CLASS (klass);
    MMKernelDeviceClass *kernel_device_class = MM_KERNEL_DEVICE_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMKernelDeviceUdevPrivate));

    object_class->dispose      = dispose;
    object_class->get_property = get_property;
    object_class->set_property = set_property;

    kernel_device_class->get_subsystem             = kernel_device_get_subsystem;
    kernel_device_class->get_name                  = kernel_device_get_name;
    kernel_device_class->get_driver                = kernel_device_get_driver;
    kernel_device_class->get_sysfs_path            = kernel_device_get_sysfs_path;
    kernel_device_class->get_physdev_uid           = kernel_device_get_physdev_uid;
    kernel_device_class->get_physdev_vid           = kernel_device_get_physdev_vid;
    kernel_device_class->get_physdev_pid           = kernel_device_get_physdev_pid;
    kernel_device_class->get_physdev_revision      = kernel_device_get_physdev_revision;
    kernel_device_class->get_physdev_sysfs_path    = kernel_device_get_physdev_sysfs_path;
    kernel_device_class->get_physdev_subsystem     = kernel_device_get_physdev_subsystem;
    kernel_device_class->get_physdev_manufacturer  = kernel_device_get_physdev_manufacturer;
    kernel_device_class->get_physdev_product       = kernel_device_get_physdev_product;
    kernel_device_class->get_interface_class       = kernel_device_get_interface_class;
    kernel_device_class->get_interface_subclass    = kernel_device_get_interface_subclass;
    kernel_device_class->get_interface_protocol    = kernel_device_get_interface_protocol;
    kernel_device_class->get_interface_sysfs_path  = kernel_device_get_interface_sysfs_path;
    kernel_device_class->get_interface_description = kernel_device_get_interface_description;
    kernel_device_class->cmp                       = kernel_device_cmp;
    kernel_device_class->has_property              = kernel_device_has_property;
    kernel_device_class->get_property              = kernel_device_get_property;
    kernel_device_class->has_global_property       = kernel_device_has_global_property;
    kernel_device_class->get_global_property       = kernel_device_get_global_property;
    kernel_device_class->has_attribute             = kernel_device_has_attribute;
    kernel_device_class->get_attribute             = kernel_device_get_attribute;

    properties[PROP_UDEV_DEVICE] =
        g_param_spec_object ("udev-device",
                             "udev device",
                             "Device object as reported by GUdev",
                             G_UDEV_TYPE_DEVICE,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_UDEV_DEVICE, properties[PROP_UDEV_DEVICE]);

    properties[PROP_PROPERTIES] =
        g_param_spec_object ("properties",
                             "Properties",
                             "Generic kernel event properties",
                             MM_TYPE_KERNEL_EVENT_PROPERTIES,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PROPERTIES, properties[PROP_PROPERTIES]);
}
