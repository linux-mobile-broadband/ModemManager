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

#ifndef MM_DISPATCHER_FCC_UNLOCK_H
#define MM_DISPATCHER_FCC_UNLOCK_H

#include <config.h>
#include <gio/gio.h>

#include "mm-dispatcher.h"

#define MM_TYPE_DISPATCHER_FCC_UNLOCK            (mm_dispatcher_fcc_unlock_get_type ())
#define MM_DISPATCHER_FCC_UNLOCK(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_DISPATCHER_FCC_UNLOCK, MMDispatcherFccUnlock))
#define MM_DISPATCHER_FCC_UNLOCK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_DISPATCHER_FCC_UNLOCK, MMDispatcherFccUnlockClass))
#define MM_IS_DISPATCHER_FCC_UNLOCK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_DISPATCHER_FCC_UNLOCK))
#define MM_IS_DISPATCHER_FCC_UNLOCK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_DISPATCHER_FCC_UNLOCK))
#define MM_DISPATCHER_FCC_UNLOCK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_DISPATCHER_FCC_UNLOCK, MMDispatcherFccUnlockClass))

typedef struct _MMDispatcherFccUnlock        MMDispatcherFccUnlock;
typedef struct _MMDispatcherFccUnlockClass   MMDispatcherFccUnlockClass;
typedef struct _MMDispatcherFccUnlockPrivate MMDispatcherFccUnlockPrivate;

GType                  mm_dispatcher_fcc_unlock_get_type   (void);
MMDispatcherFccUnlock *mm_dispatcher_fcc_unlock_get        (void);
void                   mm_dispatcher_fcc_unlock_run        (MMDispatcherFccUnlock  *self,
                                                            guint                   vid,
                                                            guint                   pid,
                                                            const gchar            *modem_dbus_path,
                                                            const GStrv             ports,
                                                            GCancellable           *cancellable,
                                                            GAsyncReadyCallback     callback,
                                                            gpointer                user_data);
gboolean               mm_dispatcher_fcc_unlock_run_finish (MMDispatcherFccUnlock  *self,
                                                            GAsyncResult           *res,
                                                            GError                **error);

#endif /* MM_DISPATCHER_FCC_UNLOCK_H */
