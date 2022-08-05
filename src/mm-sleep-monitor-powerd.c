/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This program is free software; you can redistribute it and/or modify
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
 * (C) Copyright 2022 Google, Inc.
 * Author: Rukun Mao <rmao@google.com>
 * Original code from ./mm-sleep-monitor-systemd.c
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "mm-log-object.h"
#include "mm-utils.h"
#include "mm-sleep-monitor.h"

#define PD_NAME              "org.chromium.PowerManager"
#define PD_PATH              "/org/chromium/PowerManager"
#define PD_INTERFACE         "org.chromium.PowerManager"

struct _MMSleepMonitor {
    GObject parent_instance;

    GDBusProxy *pd_proxy;
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

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMSleepMonitor, mm_sleep_monitor, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("sleep-monitor-powerd");
}

/********************************************************************/

static void
signal_cb (GDBusProxy  *proxy,
           const gchar *sendername,
           const gchar *signalname,
           GVariant    *args,
           gpointer     data)
{
    MMSleepMonitor *self = data;

    if (proxy == self->pd_proxy) {
        if (strcmp (signalname, "SuspendImminent") == 0) {
            mm_obj_msg (self, "system suspend signal from powerd");
            g_signal_emit (self, signals[SLEEPING], 0);
        } else if (strcmp (signalname, "SuspendDone") == 0) {
            mm_obj_msg (self, "system resume signal from powerd");
            g_signal_emit (self, signals[RESUMING], 0);
        }
    }
}

static void
on_pd_proxy_acquired (GObject *object,
                      GAsyncResult *res,
                      MMSleepMonitor *self)
{
    GError *error = NULL;

    self->pd_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
    if (!self->pd_proxy) {
        mm_obj_warn (self, "failed to acquire powerd proxy: %s", error->message);
        g_clear_error (&error);
        return;
    }

    g_signal_connect (self->pd_proxy, "g-signal", G_CALLBACK (signal_cb), self);
}

static void
mm_sleep_monitor_init (MMSleepMonitor *self)
{
    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                              G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                              G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                              NULL,
                              PD_NAME, PD_PATH, PD_INTERFACE,
                              NULL,
                              (GAsyncReadyCallback) on_pd_proxy_acquired, self);
}

static void
finalize (GObject *object)
{
    MMSleepMonitor *self = MM_SLEEP_MONITOR (object);

    if (self->pd_proxy)
        g_object_unref (self->pd_proxy);

    if (G_OBJECT_CLASS (mm_sleep_monitor_parent_class)->finalize != NULL)
        G_OBJECT_CLASS (mm_sleep_monitor_parent_class)->finalize (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
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

/* ---------------------------------------------------------------------------------------------------- */
