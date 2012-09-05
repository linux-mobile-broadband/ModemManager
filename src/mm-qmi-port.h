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

#ifndef MM_QMI_PORT_H
#define MM_QMI_PORT_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <libqmi-glib.h>

#include "mm-port.h"

#define MM_TYPE_QMI_PORT            (mm_qmi_port_get_type ())
#define MM_QMI_PORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_QMI_PORT, MMQmiPort))
#define MM_QMI_PORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_QMI_PORT, MMQmiPortClass))
#define MM_IS_QMI_PORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_QMI_PORT))
#define MM_IS_QMI_PORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_QMI_PORT))
#define MM_QMI_PORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_QMI_PORT, MMQmiPortClass))

typedef struct _MMQmiPort MMQmiPort;
typedef struct _MMQmiPortClass MMQmiPortClass;
typedef struct _MMQmiPortPrivate MMQmiPortPrivate;

struct _MMQmiPort {
    MMPort parent;
    MMQmiPortPrivate *priv;
};

struct _MMQmiPortClass {
    MMPortClass parent;
};

GType mm_qmi_port_get_type (void);

MMQmiPort *mm_qmi_port_new (const gchar *name);

void     mm_qmi_port_open        (MMQmiPort *self,
                                  gboolean set_data_format,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
gboolean mm_qmi_port_open_finish (MMQmiPort *self,
                                  GAsyncResult *res,
                                  GError **error);
gboolean mm_qmi_port_is_open     (MMQmiPort *self);
void     mm_qmi_port_close       (MMQmiPort *self);

typedef enum {
    MM_QMI_PORT_FLAG_DEFAULT  = 0,
    MM_QMI_PORT_FLAG_WDS_IPV4 = 100,
    MM_QMI_PORT_FLAG_WDS_IPV6 = 101
} MMQmiPortFlag;

void     mm_qmi_port_allocate_client        (MMQmiPort *self,
                                             QmiService service,
                                             MMQmiPortFlag flag,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data);
gboolean mm_qmi_port_allocate_client_finish (MMQmiPort *self,
                                             GAsyncResult *res,
                                             GError **error);

QmiClient *mm_qmi_port_peek_client (MMQmiPort *self,
                                    QmiService service,
                                    MMQmiPortFlag flag);
QmiClient *mm_qmi_port_get_client  (MMQmiPort *self,
                                    QmiService service,
                                    MMQmiPortFlag flag);

#endif /* MM_QMI_PORT_H */
