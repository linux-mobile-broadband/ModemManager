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
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef MM_DEVICE_H
#define MM_DEVICE_H

#include <glib.h>
#include <glib-object.h>

#include <gudev/gudev.h>

#include "mm-plugin.h"

#define MM_TYPE_DEVICE            (mm_device_get_type ())
#define MM_DEVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_DEVICE, MMDevice))
#define MM_DEVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_DEVICE, MMDeviceClass))
#define MM_IS_DEVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_DEVICE))
#define MM_IS_DEVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_DEVICE))
#define MM_DEVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_DEVICE, MMDeviceClass))

typedef struct _MMDevice MMDevice;
typedef struct _MMDeviceClass MMDeviceClass;
typedef struct _MMDevicePrivate MMDevicePrivate;

#define MM_DEVICE_UDEV_DEVICE "udev-device"
#define MM_DEVICE_PLUGIN      "plugin"
#define MM_DEVICE_MODEM       "modem"

struct _MMDevice {
    GObject parent;
    MMDevicePrivate *priv;
};

struct _MMDeviceClass {
    GObjectClass parent;
};

GType mm_device_get_type (void);

MMDevice *mm_device_new (GUdevDevice *udev_device);

void     mm_device_grab_port    (MMDevice    *self,
                                 GUdevDevice *udev_port);
void     mm_device_release_port (MMDevice    *self,
                                 GUdevDevice *udev_port);
gboolean mm_device_owns_port    (MMDevice    *self,
                                 GUdevDevice *udev_port);

gboolean mm_device_create_modem (MMDevice                  *self,
                                 GDBusObjectManagerServer  *object_manager,
                                 GError                   **error);
void     mm_device_remove_modem (MMDevice  *self);

const gchar *mm_device_get_path         (MMDevice *self);
const gchar *mm_device_get_driver       (MMDevice *self);
GUdevDevice *mm_device_peek_udev_device (MMDevice *self);
GUdevDevice *mm_device_get_udev_device  (MMDevice *self);
void         mm_device_set_plugin       (MMDevice *self,
                                         MMPlugin *plugin);
MMPlugin    *mm_device_peek_plugin      (MMDevice *self);
MMPlugin    *mm_device_get_plugin       (MMDevice *self);
MMBaseModem *mm_device_peek_modem       (MMDevice *self);
MMBaseModem *mm_device_get_modem        (MMDevice *self);

#endif /* MM_DEVICE_H */
