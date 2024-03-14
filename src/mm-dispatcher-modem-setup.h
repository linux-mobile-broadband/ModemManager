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
 * Copyright (C) 2023-2024 Nero Sinaro <xu.zhang@fibocom.com>
 */

#ifndef MM_DISPATCHER_MODEM_SETUP_H
#define MM_DISPATCHER_MODEM_SETUP_H

#include <config.h>
#include <gio/gio.h>

#include "mm-dispatcher.h"

#define MM_TYPE_DISPATCHER_MODEM_SETUP            (mm_dispatcher_modem_setup_get_type ())
#define MM_DISPATCHER_MODEM_SETUP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_DISPATCHER_MODEM_SETUP, MMDispatcherModemSetup))
#define MM_DISPATCHER_MODEM_SETUP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_DISPATCHER_MODEM_SETUP, MMDispatcherModemSetupClass))
#define MM_IS_DISPATCHER_MODEM_SETUP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_DISPATCHER_MODEM_SETUP))
#define MM_IS_DISPATCHER_MODEM_SETUP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_DISPATCHER_MODEM_SETUP))
#define MM_DISPATCHER_MODEM_SETUP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_DISPATCHER_MODEM_SETUP, MMDispatcherModemSetupClass))

typedef struct _MMDispatcherModemSetup        MMDispatcherModemSetup;
typedef struct _MMDispatcherModemSetupClass   MMDispatcherModemSetupClass;
typedef struct _MMDispatcherModemSetupPrivate MMDispatcherModemSetupPrivate;

GType                   mm_dispatcher_modem_setup_get_type (void);
MMDispatcherModemSetup *mm_dispatcher_modem_setup_get (void);
void                    mm_dispatcher_modem_setup_run (MMDispatcherModemSetup   *self,
                                                       guint                     vid,
                                                       guint                     pid,
                                                       const gchar              *modem_dbus_path,
                                                       const GStrv               ports,
                                                       GCancellable             *cancellable,
                                                       GAsyncReadyCallback       callback,
                                                       gpointer                  user_data);

gboolean                mm_dispatcher_modem_setup_run_finish (MMDispatcherModemSetup  *self,
                                                              GAsyncResult            *res,
                                                              GError                 **error);

#endif /* MM_DISPATCHER_MODEM_SETUP_H */
