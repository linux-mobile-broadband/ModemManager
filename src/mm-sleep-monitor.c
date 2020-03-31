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
 * (C) Copyright 2012 Red Hat, Inc.
 * Author: Matthias Clasen <mclasen@redhat.com>
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "mm-log-object.h"
#include "mm-utils.h"
#include "mm-sleep-monitor.h"

#define SD_NAME              "org.freedesktop.login1"
#define SD_PATH              "/org/freedesktop/login1"
#define SD_INTERFACE         "org.freedesktop.login1.Manager"


struct _MMSleepMonitor {
    GObject parent_instance;

    GDBusProxy *sd_proxy;
    gint inhibit_fd;
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
    return g_strdup ("sleep-monitor");
}

/********************************************************************/

static gboolean
drop_inhibitor (MMSleepMonitor *self)
{
    if (self->inhibit_fd >= 0) {
        mm_obj_dbg (self, "dropping systemd sleep inhibitor");
        close (self->inhibit_fd);
        self->inhibit_fd = -1;
        return TRUE;
    }
    return FALSE;
}

static void
inhibit_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
    GDBusProxy *sd_proxy = G_DBUS_PROXY (source);
    MMSleepMonitor *self = user_data;
    GError *error = NULL;
    GVariant *res;
    GUnixFDList *fd_list;

    res = g_dbus_proxy_call_with_unix_fd_list_finish (sd_proxy, &fd_list, result, &error);
    if (!res) {
        mm_obj_warn (self, "inhibit failed: %s", error->message);
        g_error_free (error);
    } else {
        if (!fd_list || g_unix_fd_list_get_length (fd_list) != 1)
            mm_obj_warn (self, "didn't get a single fd back");

        self->inhibit_fd = g_unix_fd_list_get (fd_list, 0, NULL);

        mm_obj_dbg (self, "inhibitor fd is %d", self->inhibit_fd);
        g_object_unref (fd_list);
        g_variant_unref (res);
    }
}

static void
take_inhibitor (MMSleepMonitor *self)
{
    g_assert (self->inhibit_fd == -1);

    mm_obj_dbg (self, "taking systemd sleep inhibitor");
    g_dbus_proxy_call_with_unix_fd_list (self->sd_proxy,
                                         "Inhibit",
                                         g_variant_new ("(ssss)",
                                                        "sleep",
                                                        "ModemManager",
                                                        _("ModemManager needs to reset devices"),
                                                        "delay"),
                                         0,
                                         G_MAXINT,
                                         NULL,
                                         NULL,
                                         inhibit_done,
                                         self);
}

static void
signal_cb (GDBusProxy  *proxy,
           const gchar *sendername,
           const gchar *signalname,
           GVariant    *args,
           gpointer     data)
{
    MMSleepMonitor *self = data;
    gboolean is_about_to_suspend;

    if (strcmp (signalname, "PrepareForSleep") != 0)
        return;

    g_variant_get (args, "(b)", &is_about_to_suspend);

    if (is_about_to_suspend) {
        mm_obj_info (self, "system is about to suspend");
        g_signal_emit (self, signals[SLEEPING], 0);
        drop_inhibitor (self);
    } else {
        mm_obj_info (self, "system is resuming");
        take_inhibitor (self);
        g_signal_emit (self, signals[RESUMING], 0);
    }
}

static void
name_owner_cb (GObject    *object,
               GParamSpec *pspec,
               gpointer    user_data)
{
    GDBusProxy *proxy = G_DBUS_PROXY (object);
    MMSleepMonitor *self = MM_SLEEP_MONITOR (user_data);
    char *owner;

    g_assert (proxy == self->sd_proxy);

    owner = g_dbus_proxy_get_name_owner (proxy);
    if (owner)
        take_inhibitor (self);
    else
        drop_inhibitor (self);
    g_free (owner);
}

static void
on_proxy_acquired (GObject *object,
                   GAsyncResult *res,
                   MMSleepMonitor *self)
{
    GError *error = NULL;
    char *owner;

    self->sd_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
    if (!self->sd_proxy) {
        mm_obj_warn (self, "failed to acquire logind proxy: %s", error->message);
        g_clear_error (&error);
        return;
    }

    g_signal_connect (self->sd_proxy, "notify::g-name-owner", G_CALLBACK (name_owner_cb), self);
    g_signal_connect (self->sd_proxy, "g-signal", G_CALLBACK (signal_cb), self);

    owner = g_dbus_proxy_get_name_owner (self->sd_proxy);
    if (owner)
        take_inhibitor (self);
    g_free (owner);
}

static void
mm_sleep_monitor_init (MMSleepMonitor *self)
{
    self->inhibit_fd = -1;
    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                              G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                              G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                              NULL,
                              SD_NAME, SD_PATH, SD_INTERFACE,
                              NULL,
                              (GAsyncReadyCallback) on_proxy_acquired, self);
}

static void
finalize (GObject *object)
{
    MMSleepMonitor *self = MM_SLEEP_MONITOR (object);

    drop_inhibitor (self);
    if (self->sd_proxy)
        g_object_unref (self->sd_proxy);

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
