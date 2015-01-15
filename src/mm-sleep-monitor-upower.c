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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2012 Red Hat, Inc.
 * Author: Matthias Clasen <mclasen@redhat.com>
 *
 * Port to GDBus:
 * (C) Copyright 2015 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <gio/gio.h>

#include "mm-log.h"
#include "mm-utils.h"
#include "mm-sleep-monitor.h"

#define UPOWER_NAME       "org.freedesktop.UPower"
#define UPOWER_PATH       "/org/freedesktop/UPower"
#define UPOWER_INTERFACE  "org.freedesktop.UPower"

struct _MMSleepMonitor {
    GObject parent_instance;

    GDBusProxy *upower_proxy;
};

struct _MMSleepMonitorClass {
    GObjectClass parent_class;

    void (*sleeping) (MMSleepMonitor *monitor);
    void (*resuming) (MMSleepMonitor *monitor);
};


enum {
    SLEEPING,
    RESUMING,
    LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (MMSleepMonitor, mm_sleep_monitor, G_TYPE_OBJECT);

/********************************************************************/

static void
signal_cb (GDBusProxy  *proxy,
           const gchar *sendername,
           const gchar *signalname,
           GVariant    *args,
           gpointer     data)
{
    MMSleepMonitor *self = data;

    if (strcmp (signalname, "Sleeping") == 0) {
        mm_dbg ("[sleep-monitor] received UPower sleeping signal");
        g_signal_emit (self, signals[SLEEPING], 0);
    } else if (strcmp (signalname, "Resuming") == 0) {
        mm_dbg ("[sleep-monitor] received UPower resuming signal");
        g_signal_emit (self, signals[RESUMING], 0);
    }
}

static void
on_proxy_acquired (GObject *object,
                   GAsyncResult *res,
                   MMSleepMonitor *self)
{
    GError *error = NULL;

    self->upower_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
    if (!self->upower_proxy) {
        mm_warn ("[sleep-monitor] failed to acquire UPower proxy: %s", error->message);
        g_clear_error (&error);
        return;
    }

    g_signal_connect (self->upower_proxy, "g-signal", G_CALLBACK (signal_cb), self);
}

static void
mm_sleep_monitor_init (MMSleepMonitor *self)
{
    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                              G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                              G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                              NULL,
                              UPOWER_NAME, UPOWER_PATH, UPOWER_INTERFACE,
                              NULL,
                              (GAsyncReadyCallback) on_proxy_acquired, self);
}

static void
finalize (GObject *object)
{
    MMSleepMonitor *self = MM_SLEEP_MONITOR (object);

    if (self->upower_proxy)
        g_object_unref (self->upower_proxy);

    if (G_OBJECT_CLASS (mm_sleep_monitor_parent_class)->finalize != NULL)
        G_OBJECT_CLASS (mm_sleep_monitor_parent_class)->finalize (object);
}

static void
mm_sleep_monitor_class_init (MMSleepMonitorClass *klass)
{
    GObjectClass *gobject_class;

    gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = finalize;

    signals[SLEEPING] = g_signal_new (MM_SLEEP_MONITOR_SLEEPING,
                                      MM_TYPE_SLEEP_MONITOR,
                                      G_SIGNAL_RUN_LAST,
                                      G_STRUCT_OFFSET (MMSleepMonitorClass, sleeping),
                                      NULL,                   /* accumulator      */
                                      NULL,                   /* accumulator data */
                                      g_cclosure_marshal_VOID__VOID,
                                      G_TYPE_NONE, 0);
    signals[RESUMING] = g_signal_new (MM_SLEEP_MONITOR_RESUMING,
                                      MM_TYPE_SLEEP_MONITOR,
                                      G_SIGNAL_RUN_LAST,
                                      G_STRUCT_OFFSET (MMSleepMonitorClass, resuming),
                                      NULL,                   /* accumulator      */
                                      NULL,                   /* accumulator data */
                                      g_cclosure_marshal_VOID__VOID,
                                      G_TYPE_NONE, 0);
}

MM_DEFINE_SINGLETON_GETTER (MMSleepMonitor, mm_sleep_monitor_get, MM_TYPE_SLEEP_MONITOR);
