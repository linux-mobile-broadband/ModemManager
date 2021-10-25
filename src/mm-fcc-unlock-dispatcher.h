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

#ifndef MM_FCC_UNLOCK_DISPATCHER_H
#define MM_FCC_UNLOCK_DISPATCHER_H

#include <config.h>
#include <gio/gio.h>

#define MM_TYPE_FCC_UNLOCK_DISPATCHER            (mm_fcc_unlock_dispatcher_get_type ())
#define MM_FCC_UNLOCK_DISPATCHER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_FCC_UNLOCK_DISPATCHER, MMFccUnlockDispatcher))
#define MM_FCC_UNLOCK_DISPATCHER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_FCC_UNLOCK_DISPATCHER, MMFccUnlockDispatcherClass))
#define MM_IS_FCC_UNLOCK_DISPATCHER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_FCC_UNLOCK_DISPATCHER))
#define MM_IS_FCC_UNLOCK_DISPATCHER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_FCC_UNLOCK_DISPATCHER))
#define MM_FCC_UNLOCK_DISPATCHER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_FCC_UNLOCK_DISPATCHER, MMFccUnlockDispatcherClass))

typedef struct _MMFccUnlockDispatcher        MMFccUnlockDispatcher;
typedef struct _MMFccUnlockDispatcherClass   MMFccUnlockDispatcherClass;
typedef struct _MMFccUnlockDispatcherPrivate MMFccUnlockDispatcherPrivate;

GType                  mm_fcc_unlock_dispatcher_get_type   (void);
MMFccUnlockDispatcher *mm_fcc_unlock_dispatcher_get        (void);
void                   mm_fcc_unlock_dispatcher_run        (MMFccUnlockDispatcher  *self,
                                                            guint                   vid,
                                                            guint                   pid,
                                                            const gchar            *modem_dbus_path,
                                                            const GStrv             ports,
                                                            GCancellable           *cancellable,
                                                            GAsyncReadyCallback     callback,
                                                            gpointer                user_data);
gboolean               mm_fcc_unlock_dispatcher_run_finish (MMFccUnlockDispatcher  *self,
                                                            GAsyncResult           *res,
                                                            GError                **error);

#endif /* MM_FCC_UNLOCK_DISPATCHER_H */
