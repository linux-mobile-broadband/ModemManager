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
#include "mm-sleep-context.h"

#define PD_NAME              "org.chromium.PowerManager"
#define PD_PATH              "/org/chromium/PowerManager"
#define PD_INTERFACE         "org.chromium.PowerManager"

struct _MMSleepMonitor {
    GObject parent_instance;

    GDBusProxy     *pd_proxy;
    MMSleepContext *sleep_ctx;
    guint           sleep_done_id;
};

struct _MMSleepMonitorClass {
    GObjectClass parent_class;

    void (*sleeping) (MMSleepMonitor *monitor, MMSleepContext *ctx);
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
cleanup_sleep_context (MMSleepMonitor *self)
{
    if (self->sleep_ctx && self->sleep_done_id)
        g_signal_handler_disconnect (self->sleep_ctx, self->sleep_done_id);
    self->sleep_done_id = 0;
    g_clear_object (&self->sleep_ctx);
}

static void
sleep_context_done (MMSleepContext *sleep_ctx,
                    GError         *error,
                    MMSleepMonitor *self)
{
    if (error)
        mm_obj_warn (self, "sleep context failed: %s", error->message);
    mm_obj_msg (self, "ready to sleep");
    cleanup_sleep_context (self);
}

static void
signal_cb (GDBusProxy  *proxy,
           const gchar *sendername,
           const gchar *signalname,
           GVariant    *args,
           gpointer     data)
{
    MMSleepMonitor *self = data;

    if (proxy != self->pd_proxy)
        return;

    if (strcmp (signalname, "SuspendImminent") == 0) {
        if (self->sleep_ctx || self->sleep_done_id) {
            mm_obj_warn (self, "clearing unfinished sleep context...");
            cleanup_sleep_context (self);
        }
        self->sleep_ctx = mm_sleep_context_new (5);
        self->sleep_done_id = g_signal_connect (self->sleep_ctx,
                                                MM_SLEEP_CONTEXT_DONE,
                                                (GCallback)sleep_context_done,
                                                self);

        g_signal_emit (self, signals[SLEEPING], 0, self->sleep_ctx);
    } else if (strcmp (signalname, "SuspendDone") == 0) {
        mm_obj_msg (self, "system resume signal from powerd");
        g_signal_emit (self, signals[RESUMING], 0);
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
dispose (GObject *object)
{
    MMSleepMonitor *self = MM_SLEEP_MONITOR (object);

    cleanup_sleep_context (self);
    g_clear_object (&self->pd_proxy);

    G_OBJECT_CLASS (mm_sleep_monitor_parent_class)->dispose (object);
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

    gobject_class->dispose = dispose;

    signals[SLEEPING] = g_signal_new (MM_SLEEP_MONITOR_SLEEPING,
                                      MM_TYPE_SLEEP_MONITOR,
                                      G_SIGNAL_RUN_LAST,
                                      G_STRUCT_OFFSET (MMSleepMonitorClass, sleeping),
                                      NULL,                   /* accumulator      */
                                      NULL,                   /* accumulator data */
                                      g_cclosure_marshal_VOID__OBJECT,
                                      G_TYPE_NONE, 1, G_TYPE_CANCELLABLE);
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
