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

#ifndef MM_SERIAL_H
#define MM_SERIAL_H

#include <glib.h>
#include <glib/gtypes.h>
#include <glib-object.h>

#include "mm-serial-port.h"

#define MM_TYPE_SERIAL            (mm_serial_get_type ())
#define MM_SERIAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SERIAL, MMSerial))
#define MM_SERIAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SERIAL, MMSerialClass))
#define MM_IS_SERIAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SERIAL))
#define MM_IS_SERIAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SERIAL))
#define MM_SERIAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SERIAL, MMSerialClass))

typedef struct _MMSerial MMSerial;
typedef struct _MMSerialClass MMSerialClass;

struct _MMSerial {
    GObject parent;
};

struct _MMSerialClass {
    GObjectClass parent;
};

GType mm_serial_get_type (void);

MMSerialPort *mm_serial_get_port         (MMSerial *self,
                                          const char *name);

MMSerialPort *mm_serial_add_port         (MMSerial *self,
                                          const char *name,
                                          MMSerialPortType ptype);

gboolean mm_serial_remove_port           (MMSerial *self,
                                          MMSerialPort *port);

#endif /* MM_SERIAL_H */

