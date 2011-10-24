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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#ifndef MM_QCDM_SERIAL_PORT_H
#define MM_QCDM_SERIAL_PORT_H

#include <glib.h>
#include <glib-object.h>

#include "mm-serial-port.h"

#define MM_TYPE_QCDM_SERIAL_PORT            (mm_qcdm_serial_port_get_type ())
#define MM_QCDM_SERIAL_PORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_QCDM_SERIAL_PORT, MMQcdmSerialPort))
#define MM_QCDM_SERIAL_PORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_QCDM_SERIAL_PORT, MMQcdmSerialPortClass))
#define MM_IS_QCDM_SERIAL_PORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_QCDM_SERIAL_PORT))
#define MM_IS_QCDM_SERIAL_PORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_QCDM_SERIAL_PORT))
#define MM_QCDM_SERIAL_PORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_QCDM_SERIAL_PORT, MMQcdmSerialPortClass))

typedef struct _MMQcdmSerialPort MMQcdmSerialPort;
typedef struct _MMQcdmSerialPortClass MMQcdmSerialPortClass;

typedef void (*MMQcdmSerialResponseFn)     (MMQcdmSerialPort *port,
                                            GByteArray *response,
                                            GError *error,
                                            gpointer user_data);

struct _MMQcdmSerialPort {
    MMSerialPort parent;
};

struct _MMQcdmSerialPortClass {
    MMSerialPortClass parent;
};

GType mm_qcdm_serial_port_get_type (void);

MMQcdmSerialPort *mm_qcdm_serial_port_new (const char *name, MMPortType ptype);

MMQcdmSerialPort *mm_qcdm_serial_port_new_fd (int fd, MMPortType ptype);

void     mm_qcdm_serial_port_queue_command     (MMQcdmSerialPort *self,
                                                GByteArray *command,
                                                guint32 timeout_seconds,
                                                MMQcdmSerialResponseFn callback,
                                                gpointer user_data);

void     mm_qcdm_serial_port_queue_command_cached (MMQcdmSerialPort *self,
                                                   GByteArray *command,
                                                   guint32 timeout_seconds,
                                                   MMQcdmSerialResponseFn callback,
                                                   gpointer user_data);

#endif /* MM_QCDM_SERIAL_PORT_H */

