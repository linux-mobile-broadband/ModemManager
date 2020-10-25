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

#ifndef MM_PORT_QMI_H
#define MM_PORT_QMI_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <libqmi-glib.h>

#include "mm-port.h"

#define MM_TYPE_PORT_QMI            (mm_port_qmi_get_type ())
#define MM_PORT_QMI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_QMI, MMPortQmi))
#define MM_PORT_QMI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_QMI, MMPortQmiClass))
#define MM_IS_PORT_QMI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_QMI))
#define MM_IS_PORT_QMI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_QMI))
#define MM_PORT_QMI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_QMI, MMPortQmiClass))

typedef struct _MMPortQmi MMPortQmi;
typedef struct _MMPortQmiClass MMPortQmiClass;
typedef struct _MMPortQmiPrivate MMPortQmiPrivate;

struct _MMPortQmi {
    MMPort parent;
    MMPortQmiPrivate *priv;
};

struct _MMPortQmiClass {
    MMPortClass parent;
};

GType mm_port_qmi_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortQmi, g_object_unref)

MMPortQmi *mm_port_qmi_new          (const gchar          *name,
                                     MMPortSubsys          subsys);
void       mm_port_qmi_open         (MMPortQmi            *self,
                                     gboolean              set_data_format,
                                     GCancellable         *cancellable,
                                     GAsyncReadyCallback   callback,
                                     gpointer              user_data);
gboolean   mm_port_qmi_open_finish  (MMPortQmi            *self,
                                     GAsyncResult         *res,
                                     GError              **error);
gboolean   mm_port_qmi_is_open      (MMPortQmi            *self);
void       mm_port_qmi_close        (MMPortQmi            *self,
                                     GAsyncReadyCallback   callback,
                                     gpointer              user_data);
gboolean   mm_port_qmi_close_finish (MMPortQmi            *self,
                                     GAsyncResult         *res,
                                     GError              **error);

typedef enum {
    MM_PORT_QMI_FLAG_DEFAULT  = 0,
    MM_PORT_QMI_FLAG_WDS_IPV4 = 100,
    MM_PORT_QMI_FLAG_WDS_IPV6 = 101
} MMPortQmiFlag;

void     mm_port_qmi_allocate_client        (MMPortQmi *self,
                                             QmiService service,
                                             MMPortQmiFlag flag,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
gboolean mm_port_qmi_allocate_client_finish (MMPortQmi *self,
                                             GAsyncResult *res,
                                             GError **error);

void     mm_port_qmi_release_client         (MMPortQmi     *self,
                                             QmiService     service,
                                             MMPortQmiFlag  flag);

QmiClient *mm_port_qmi_peek_client (MMPortQmi *self,
                                    QmiService service,
                                    MMPortQmiFlag flag);
QmiClient *mm_port_qmi_get_client  (MMPortQmi *self,
                                    QmiService service,
                                    MMPortQmiFlag flag);

QmiDevice *mm_port_qmi_peek_device (MMPortQmi *self);

gboolean mm_port_qmi_llp_is_raw_ip (MMPortQmi *self);

#endif /* MM_PORT_QMI_H */
