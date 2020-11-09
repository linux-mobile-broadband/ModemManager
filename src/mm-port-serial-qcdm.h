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

#ifndef MM_PORT_SERIAL_QCDM_H
#define MM_PORT_SERIAL_QCDM_H

#include <glib.h>
#include <glib-object.h>

#include "mm-port-serial.h"

#define MM_TYPE_PORT_SERIAL_QCDM            (mm_port_serial_qcdm_get_type ())
#define MM_PORT_SERIAL_QCDM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_SERIAL_QCDM, MMPortSerialQcdm))
#define MM_PORT_SERIAL_QCDM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_SERIAL_QCDM, MMPortSerialQcdmClass))
#define MM_IS_PORT_SERIAL_QCDM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_SERIAL_QCDM))
#define MM_IS_PORT_SERIAL_QCDM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_SERIAL_QCDM))
#define MM_PORT_SERIAL_QCDM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_SERIAL_QCDM, MMPortSerialQcdmClass))

typedef struct _MMPortSerialQcdm MMPortSerialQcdm;
typedef struct _MMPortSerialQcdmClass MMPortSerialQcdmClass;
typedef struct _MMPortSerialQcdmPrivate MMPortSerialQcdmPrivate;

struct _MMPortSerialQcdm {
    MMPortSerial parent;
    MMPortSerialQcdmPrivate *priv;
};

struct _MMPortSerialQcdmClass {
    MMPortSerialClass parent;
};

GType mm_port_serial_qcdm_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortSerialQcdm, g_object_unref)

MMPortSerialQcdm *mm_port_serial_qcdm_new    (const char *name,
                                              MMPortSubsys subsys);
MMPortSerialQcdm *mm_port_serial_qcdm_new_fd (int fd);

void        mm_port_serial_qcdm_command        (MMPortSerialQcdm *self,
                                                GByteArray *command,
                                                guint32 timeout_seconds,
                                                GCancellable *cancellable,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
GByteArray *mm_port_serial_qcdm_command_finish (MMPortSerialQcdm *self,
                                                GAsyncResult *res,
                                                GError **error);

typedef void (*MMPortSerialQcdmUnsolicitedMsgFn) (MMPortSerialQcdm *port,
                                                  GByteArray *log_buffer,
                                                  gpointer user_data);

void     mm_port_serial_qcdm_add_unsolicited_msg_handler (MMPortSerialQcdm *self,
                                                          guint log_code,
                                                          MMPortSerialQcdmUnsolicitedMsgFn callback,
                                                          gpointer user_data,
                                                          GDestroyNotify notify);

void     mm_port_serial_qcdm_enable_unsolicited_msg_handler (MMPortSerialQcdm *self,
                                                             guint log_code,
                                                             gboolean enable);

#endif /* MM_PORT_SERIAL_QCDM_H */
