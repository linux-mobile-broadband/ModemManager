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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_MBIM_PORT_H
#define MM_MBIM_PORT_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <libmbim-glib.h>

#include "mm-port.h"

#define MM_TYPE_MBIM_PORT            (mm_mbim_port_get_type ())
#define MM_MBIM_PORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MBIM_PORT, MMMbimPort))
#define MM_MBIM_PORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MBIM_PORT, MMMbimPortClass))
#define MM_IS_MBIM_PORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MBIM_PORT))
#define MM_IS_MBIM_PORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MBIM_PORT))
#define MM_MBIM_PORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MBIM_PORT, MMMbimPortClass))

typedef struct _MMMbimPort MMMbimPort;
typedef struct _MMMbimPortClass MMMbimPortClass;
typedef struct _MMMbimPortPrivate MMMbimPortPrivate;

struct _MMMbimPort {
    MMPort parent;
    MMMbimPortPrivate *priv;
};

struct _MMMbimPortClass {
    MMPortClass parent;
};

GType mm_mbim_port_get_type (void);

MMMbimPort *mm_mbim_port_new (const gchar *name);

void     mm_mbim_port_open         (MMMbimPort *self,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean mm_mbim_port_open_finish  (MMMbimPort *self,
                                    GAsyncResult *res,
                                    GError **error);
gboolean mm_mbim_port_is_open      (MMMbimPort *self);
void     mm_mbim_port_close        (MMMbimPort *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
gboolean mm_mbim_port_close_finish (MMMbimPort *self,
                                    GAsyncResult *res,
                                    GError **error);

MbimDevice *mm_mbim_port_peek_device (MMMbimPort *self);

#endif /* MM_MBIM_PORT_H */
