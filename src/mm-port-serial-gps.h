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
 * Copyright (C) 2012 - Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_PORT_SERIAL_GPS_H
#define MM_PORT_SERIAL_GPS_H

#include <glib.h>
#include <glib-object.h>

#include "mm-port-serial.h"

#define MM_TYPE_PORT_SERIAL_GPS            (mm_port_serial_gps_get_type ())
#define MM_PORT_SERIAL_GPS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_SERIAL_GPS, MMPortSerialGps))
#define MM_PORT_SERIAL_GPS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_SERIAL_GPS, MMPortSerialGpsClass))
#define MM_IS_PORT_SERIAL_GPS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_SERIAL_GPS))
#define MM_IS_PORT_SERIAL_GPS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_SERIAL_GPS))
#define MM_PORT_SERIAL_GPS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_SERIAL_GPS, MMPortSerialGpsClass))

typedef struct _MMPortSerialGps MMPortSerialGps;
typedef struct _MMPortSerialGpsClass MMPortSerialGpsClass;
typedef struct _MMPortSerialGpsPrivate MMPortSerialGpsPrivate;

typedef void (*MMPortSerialGpsTraceFn) (MMPortSerialGps *port,
                                        const gchar *trace,
                                        gpointer user_data);

struct _MMPortSerialGps {
    MMPortSerial parent;
    MMPortSerialGpsPrivate *priv;
};

struct _MMPortSerialGpsClass {
    MMPortSerialClass parent;
};

GType mm_port_serial_gps_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortSerialGps, g_object_unref)

MMPortSerialGps *mm_port_serial_gps_new (const char *name);

void mm_port_serial_gps_add_trace_handler (MMPortSerialGps *self,
                                           MMPortSerialGpsTraceFn callback,
                                           gpointer user_data,
                                           GDestroyNotify notify);

#endif /* MM_PORT_SERIAL_GPS_H */
