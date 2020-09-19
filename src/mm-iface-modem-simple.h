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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_SIMPLE_H
#define MM_IFACE_MODEM_SIMPLE_H

#include <glib-object.h>
#include <gio/gio.h>

#define MM_TYPE_IFACE_MODEM_SIMPLE               (mm_iface_modem_simple_get_type ())
#define MM_IFACE_MODEM_SIMPLE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM_SIMPLE, MMIfaceModemSimple))
#define MM_IS_IFACE_MODEM_SIMPLE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM_SIMPLE))
#define MM_IFACE_MODEM_SIMPLE_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM_SIMPLE, MMIfaceModemSimple))

#define MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON "iface-modem-simple-dbus-skeleton"
#define MM_IFACE_MODEM_SIMPLE_STATUS        "iface-modem-simple-status"

typedef struct _MMIfaceModemSimple MMIfaceModemSimple;

struct _MMIfaceModemSimple {
    GTypeInterface g_iface;
};

GType mm_iface_modem_simple_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMIfaceModemSimple, g_object_unref)

/* Initialize Modem Simple interface */
void mm_iface_modem_simple_initialize (MMIfaceModemSimple *self);

/* Shutdown Modem Simple interface */
void mm_iface_modem_simple_shutdown (MMIfaceModemSimple *self);

#endif /* MM_IFACE_MODEM_SIMPLE_H */
