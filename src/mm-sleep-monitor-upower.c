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
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <dbus/dbus-glib.h>
#include <gio/gio.h>

#include "mm-log.h"
#include "mm-utils.h"
#include "mm-sleep-monitor.h"

#define UPOWER_DBUS_SERVICE "org.freedesktop.UPower"

struct _MMSleepMonitor {
    GObject parent_instance;

    DBusGProxy *upower_proxy;
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
upower_sleeping_cb (DBusGProxy *proxy, gpointer user_data)
{
    mm_dbg ("[sleep-monitor] received UPower sleeping signal");
    g_signal_emit (user_data, signals[SLEEPING], 0);
}

static void
upower_resuming_cb (DBusGProxy *proxy, gpointer user_data)
{
    mm_dbg ("[sleep-monitor] received UPower resuming signal");
    g_signal_emit (user_data, signals[RESUMING], 0);
}

static void
mm_sleep_monitor_init (MMSleepMonitor *self)
{
    DBusGConnection *bus;

    bus = mm_dbus_manager_get_connection (mm_dbus_manager_get ());
    self->upower_proxy = dbus_g_proxy_new_for_name (bus,
                                                    UPOWER_DBUS_SERVICE,
                                                    "/org/freedesktop/UPower",
                                                    "org.freedesktop.UPower");
    if (self->upower_proxy) {
        dbus_g_proxy_add_signal (self->upower_proxy, "Sleeping", G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (self->upower_proxy, "Sleeping",
                                     G_CALLBACK (upower_sleeping_cb),
                                     self, NULL);

        dbus_g_proxy_add_signal (self->upower_proxy, "Resuming", G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (self->upower_proxy, "Resuming",
                                     G_CALLBACK (upower_resuming_cb),
                                     self, NULL);
    } else
        mm_warn ("could not initialize UPower D-Bus proxy");
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
