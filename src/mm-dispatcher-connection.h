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
 * Copyright (C) 2022 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_DISPATCHER_CONNECTION_H
#define MM_DISPATCHER_CONNECTION_H

#include <config.h>
#include <gio/gio.h>

#include "mm-dispatcher.h"

#define MM_TYPE_DISPATCHER_CONNECTION            (mm_dispatcher_connection_get_type ())
#define MM_DISPATCHER_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_DISPATCHER_CONNECTION, MMDispatcherConnection))
#define MM_DISPATCHER_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_DISPATCHER_CONNECTION, MMDispatcherConnectionClass))
#define MM_IS_DISPATCHER_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_DISPATCHER_CONNECTION))
#define MM_IS_DISPATCHER_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_DISPATCHER_CONNECTION))
#define MM_DISPATCHER_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_DISPATCHER_CONNECTION, MMDispatcherConnectionClass))

typedef struct _MMDispatcherConnection        MMDispatcherConnection;
typedef struct _MMDispatcherConnectionClass   MMDispatcherConnectionClass;
typedef struct _MMDispatcherConnectionPrivate MMDispatcherConnectionPrivate;

GType                   mm_dispatcher_connection_get_type   (void);
MMDispatcherConnection *mm_dispatcher_connection_get        (void);
void                    mm_dispatcher_connection_run        (MMDispatcherConnection *self,
                                                             const gchar            *modem_dbus_path,
                                                             const gchar            *bearer_dbus_path,
                                                             const gchar            *data_port,
                                                             gboolean                connected,
                                                             GCancellable           *cancellable,
                                                             GAsyncReadyCallback     callback,
                                                             gpointer                user_data);
gboolean                mm_dispatcher_connection_run_finish (MMDispatcherConnection  *self,
                                                             GAsyncResult           *res,
                                                             GError                **error);

#endif /* MM_DISPATCHER_CONNECTION_H */
