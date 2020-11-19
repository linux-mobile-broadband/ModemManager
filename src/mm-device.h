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

#include "mm-kernel-device.h"
#include "mm-base-modem.h"

#define MM_TYPE_DEVICE            (mm_device_get_type ())
#define MM_DEVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_DEVICE, MMDevice))
#define MM_DEVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_DEVICE, MMDeviceClass))
#define MM_IS_DEVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_DEVICE))
#define MM_IS_DEVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_DEVICE))
#define MM_DEVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_DEVICE, MMDeviceClass))

typedef struct _MMDevice MMDevice;
typedef struct _MMDeviceClass MMDeviceClass;
typedef struct _MMDevicePrivate MMDevicePrivate;

#define MM_DEVICE_UID            "uid"
#define MM_DEVICE_PLUGIN         "plugin"
#define MM_DEVICE_MODEM          "modem"
#define MM_DEVICE_HOTPLUGGED     "hotplugged"
#define MM_DEVICE_VIRTUAL        "virtual"
#define MM_DEVICE_INHIBITED      "inhibited"
#define MM_DEVICE_OBJECT_MANAGER "object-manager"

#define MM_DEVICE_PORT_GRABBED  "port-grabbed"
#define MM_DEVICE_PORT_RELEASED "port-released"

struct _MMDevice {
    GObject parent;
    MMDevicePrivate *priv;
};

struct _MMDeviceClass {
    GObjectClass parent;

    /* signals */
    void (* port_grabbed)  (MMDevice       *self,
                            MMKernelDevice *port);
    void (* port_released) (MMDevice       *self,
                            MMKernelDevice *port);
};

GType mm_device_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMDevice, g_object_unref)

MMDevice *mm_device_new (const gchar              *uid,
                         gboolean                  hotplugged,
                         gboolean                  virtual,
                         GDBusObjectManagerServer *object_manager);

void     mm_device_grab_port   (MMDevice       *self,
                                MMKernelDevice *kernel_port);
gboolean mm_device_owns_port   (MMDevice       *self,
                                MMKernelDevice *kernel_port);
void     mm_device_ignore_port (MMDevice       *self,
                                MMKernelDevice *kernel_port);

gboolean mm_device_owns_port_name    (MMDevice       *self,
                                      const gchar    *subsystem,
                                      const gchar    *name);
void     mm_device_release_port_name (MMDevice       *self,
                                      const gchar    *subsystem,
                                      const gchar    *name);

gboolean mm_device_create_modem (MMDevice  *self,
                                 GError   **error);
void     mm_device_remove_modem (MMDevice  *self);

void     mm_device_inhibit        (MMDevice                  *self,
                                   GAsyncReadyCallback        callback,
                                   gpointer                   user_data);
gboolean mm_device_inhibit_finish (MMDevice                  *self,
                                   GAsyncResult              *res,
                                   GError                   **error);
gboolean mm_device_uninhibit      (MMDevice                  *self,
                                   GError                   **error);


const gchar     *mm_device_get_uid              (MMDevice       *self);
const gchar    **mm_device_get_drivers          (MMDevice       *self);
guint16          mm_device_get_vendor           (MMDevice       *self);
guint16          mm_device_get_product          (MMDevice       *self);
void             mm_device_set_plugin           (MMDevice       *self,
                                                 GObject        *plugin);
GObject         *mm_device_peek_plugin          (MMDevice       *self);
GObject         *mm_device_get_plugin           (MMDevice       *self);
MMBaseModem     *mm_device_peek_modem           (MMDevice       *self);
MMBaseModem     *mm_device_get_modem            (MMDevice       *self);
GObject         *mm_device_peek_port_probe      (MMDevice       *self,
                                                 MMKernelDevice *kernel_port);
GObject         *mm_device_get_port_probe       (MMDevice       *self,
                                                 MMKernelDevice *kernel_port);
GList           *mm_device_peek_port_probe_list (MMDevice       *self);
GList           *mm_device_get_port_probe_list  (MMDevice       *self);
gboolean         mm_device_get_hotplugged       (MMDevice       *self);
gboolean         mm_device_get_inhibited        (MMDevice       *self);

/* For testing purposes */
void          mm_device_virtual_grab_ports (MMDevice     *self,
                                            const gchar **ports);
const gchar **mm_device_virtual_peek_ports (MMDevice     *self);
gboolean      mm_device_is_virtual         (MMDevice     *self);

#endif /* MM_DEVICE_H */
