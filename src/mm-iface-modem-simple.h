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

#include "mm-iface-modem.h"

#define MM_TYPE_IFACE_MODEM_SIMPLE mm_iface_modem_simple_get_type ()
G_DECLARE_INTERFACE (MMIfaceModemSimple, mm_iface_modem_simple, MM, IFACE_MODEM_SIMPLE, MMIfaceModem)

#define MM_IFACE_MODEM_SIMPLE_DBUS_SKELETON "iface-modem-simple-dbus-skeleton"
#define MM_IFACE_MODEM_SIMPLE_STATUS        "iface-modem-simple-status"

struct _MMIfaceModemSimpleInterface {
    GTypeInterface g_iface;
};

/* Initialize Modem Simple interface */
void mm_iface_modem_simple_initialize (MMIfaceModemSimple *self);

/* Abort ongoing operations in Modem Simple interface */
void mm_iface_modem_simple_abort_ongoing (MMIfaceModemSimple *self);

/* Shutdown Modem Simple interface */
void mm_iface_modem_simple_shutdown (MMIfaceModemSimple *self);

#endif /* MM_IFACE_MODEM_SIMPLE_H */
