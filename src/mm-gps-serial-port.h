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

#ifndef MM_GPS_SERIAL_PORT_H
#define MM_GPS_SERIAL_PORT_H

#include <glib.h>
#include <glib-object.h>

#include "mm-serial-port.h"

#define MM_TYPE_GPS_SERIAL_PORT            (mm_gps_serial_port_get_type ())
#define MM_GPS_SERIAL_PORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_GPS_SERIAL_PORT, MMGpsSerialPort))
#define MM_GPS_SERIAL_PORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_GPS_SERIAL_PORT, MMGpsSerialPortClass))
#define MM_IS_GPS_SERIAL_PORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_GPS_SERIAL_PORT))
#define MM_IS_GPS_SERIAL_PORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_GPS_SERIAL_PORT))
#define MM_GPS_SERIAL_PORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_GPS_SERIAL_PORT, MMGpsSerialPortClass))

typedef struct _MMGpsSerialPort MMGpsSerialPort;
typedef struct _MMGpsSerialPortClass MMGpsSerialPortClass;
typedef struct _MMGpsSerialPortPrivate MMGpsSerialPortPrivate;

typedef void (*MMGpsSerialTraceFn) (MMGpsSerialPort *port,
                                    const gchar *trace,
                                    gpointer user_data);

struct _MMGpsSerialPort {
    MMSerialPort parent;
    MMGpsSerialPortPrivate *priv;
};

struct _MMGpsSerialPortClass {
    MMSerialPortClass parent;
};

GType mm_gps_serial_port_get_type (void);

MMGpsSerialPort *mm_gps_serial_port_new (const char *name);

void mm_gps_serial_port_add_trace_handler (MMGpsSerialPort *self,
                                           MMGpsSerialTraceFn callback,
                                           gpointer user_data,
                                           GDestroyNotify notify);

#endif /* MM_GPS_SERIAL_PORT_H */
