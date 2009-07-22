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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_MANAGER_H
#define MM_MANAGER_H

#include <glib/gtypes.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include "mm-modem.h"

#define MM_TYPE_MANAGER            (mm_manager_get_type ())
#define MM_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MANAGER, MMManager))
#define MM_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MM_TYPE_MANAGER, MMManagerClass))
#define MM_IS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MANAGER))
#define MM_IS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MM_TYPE_MANAGER))
#define MM_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MM_TYPE_MANAGER, MMManagerClass))

#define MM_DBUS_SERVICE "org.freedesktop.ModemManager"
#define MM_DBUS_PATH "/org/freedesktop/ModemManager"

typedef struct {
    GObject parent;
} MMManager;

typedef struct {
    GObjectClass parent;

    /* Signals */
    void (*device_added) (MMManager *manager, MMModem *device);
    void (*device_removed) (MMManager *manager, MMModem *device);
} MMManagerClass;

GType mm_manager_get_type (void);

MMManager       *mm_manager_new         (DBusGConnection *bus);

void             mm_manager_start       (MMManager *manager);

#endif /* MM_MANAGER_H */
