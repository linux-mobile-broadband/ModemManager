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
 * GNU General Public License for more details.
 *
 * (C) Copyright 2012 Red Hat, Inc.
 * Author: Matthias Clasen <mclasen@redhat.com>
 * Original code imported from NetworkManager.
 */

#ifndef __MM_SLEEP_MONITOR_H__
#define __MM_SLEEP_MONITOR_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define MM_TYPE_SLEEP_MONITOR         (mm_sleep_monitor_get_type ())
#define MM_SLEEP_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MM_TYPE_SLEEP_MONITOR, MMSleepMonitor))
#define MM_SLEEP_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), MM_TYPE_SLEEP_MONITOR, MMSleepMonitorClass))
#define MM_SLEEP_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MM_TYPE_SLEEP_MONITOR, MMSleepMonitorClass))
#define MM_IS_SLEEP_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MM_TYPE_SLEEP_MONITOR))
#define MM_IS_SLEEP_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MM_TYPE_SLEEP_MONITOR))

#define MM_SLEEP_MONITOR_SLEEPING "sleeping"
#define MM_SLEEP_MONITOR_RESUMING "resuming"

typedef struct _MMSleepMonitor         MMSleepMonitor;
typedef struct _MMSleepMonitorClass    MMSleepMonitorClass;

GType           mm_sleep_monitor_get_type     (void) G_GNUC_CONST;
MMSleepMonitor *mm_sleep_monitor_get          (void);

G_END_DECLS

#endif /* __MM_SLEEP_MONITOR_H__ */
