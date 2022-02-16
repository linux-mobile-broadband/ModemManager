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
 * Copyright (C) 2021-2022 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MM_DISPATCHER_H
#define MM_DISPATCHER_H

#include <config.h>
#include <gio/gio.h>

#define MM_TYPE_DISPATCHER            (mm_dispatcher_get_type ())
#define MM_DISPATCHER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_DISPATCHER, MMDispatcher))
#define MM_DISPATCHER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_DISPATCHER, MMDispatcherClass))
#define MM_IS_DISPATCHER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_DISPATCHER))
#define MM_IS_DISPATCHER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_DISPATCHER))
#define MM_DISPATCHER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_DISPATCHER, MMDispatcherClass))

typedef struct _MMDispatcher        MMDispatcher;
typedef struct _MMDispatcherClass   MMDispatcherClass;
typedef struct _MMDispatcherPrivate MMDispatcherPrivate;

#define MM_DISPATCHER_OPERATION_DESCRIPTION "operation-description"

struct _MMDispatcher {
    GObject parent;
    MMDispatcherPrivate *priv;
};

struct _MMDispatcherClass {
    GObjectClass parent;
};

GType mm_dispatcher_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMDispatcher, g_object_unref)

void     mm_dispatcher_run        (MMDispatcher         *self,
                                   const GStrv           argv,
                                   guint                 timeout_secs,
                                   GCancellable         *cancellable,
                                   GAsyncReadyCallback   callback,
                                   gpointer              user_data);
gboolean mm_dispatcher_run_finish (MMDispatcher         *self,
                                   GAsyncResult         *res,
                                   GError              **error);

#endif /* MM_DISPATCHER_H */
