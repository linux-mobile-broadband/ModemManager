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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_PORT_NET_H
#define MM_PORT_NET_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "mm-port.h"

/* Default MTU expected in a wwan interface */
#define MM_PORT_NET_MTU_DEFAULT 1500

#define MM_TYPE_PORT_NET            (mm_port_net_get_type ())
#define MM_PORT_NET(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_PORT_NET, MMPortNet))
#define MM_PORT_NET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_PORT_NET, MMPortNetClass))
#define MM_IS_PORT_NET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_PORT_NET))
#define MM_IS_PORT_NET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_PORT_NET))
#define MM_PORT_NET_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_PORT_NET, MMPortNetClass))

typedef struct _MMPortNet MMPortNet;
typedef struct _MMPortNetClass MMPortNetClass;
typedef struct _MMPortNetPrivate MMPortNetPrivate;

struct _MMPortNet {
    MMPort parent;
    MMPortNetPrivate *priv;
};

struct _MMPortNetClass {
    MMPortClass parent;
};

GType mm_port_net_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMPortNet, g_object_unref)

MMPortNet *mm_port_net_new (const gchar *name);

void     mm_port_net_link_setup        (MMPortNet            *self,
                                        gboolean              up,
                                        guint                 mtu,
                                        GCancellable         *cancellable,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data);
gboolean mm_port_net_link_setup_finish (MMPortNet            *self,
                                        GAsyncResult         *res,
                                        GError              **error);

#endif /* MM_PORT_NET_H */
