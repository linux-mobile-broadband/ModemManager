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
 * Copyright (C) 2012-2024 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-device.h"
#include "mm-plugin.h"
#include "mm-log-object.h"
#include "mm-daemon-enums-types.h"
#include "mm-bind.h"

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMDevice, mm_device, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

enum {
    PROP_0,
    PROP_UID,
    PROP_PHYSDEV,
    PROP_OBJECT_MANAGER,
    PROP_PLUGIN,
    PROP_MODEM,
    PROP_HOTPLUGGED,
    PROP_VIRTUAL,
    PROP_INHIBITED,
    PROP_LAST
};

enum {
    SIGNAL_PORT_RELEASED,
    SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

struct _MMDevicePrivate {
    /* Whether the device is real or virtual */
    gboolean virtual;

    /* Unique id */
    gchar *uid;

    /* Physdev path */
    gchar *physdev;

    /* The object manager */
    GDBusObjectManagerServer *object_manager;

    /* If USB, device vid/pid */
    guint16 vendor;
    guint16 product;
    /* Subsystem vendor ID for PCI devices */
    guint16 subsystem_vendor;
    /* Subsystem device ID for PCI devices */
    guint16 subsystem_device;

    /* Kernel drivers managing this device */
    gchar **drivers;

    /* Best plugin to manage this device, only if the device probing
     * has finished successfully. */
    MMPlugin *plugin;

    /* Lists of port probes in the device */
    GList *port_probes;
    GList *ignored_port_probes;

    /* The Modem object for this device */
    MMBaseModem *modem;
    gulong       modem_valid_id;

    /* Whether the device was hot-plugged. */
    gboolean hotplugged;

    /* Whether the device is inhibited. */
    gboolean inhibited;

    /* Virtual ports */
    gchar **virtual_ports;

    /* Scheduled reprobe */
    guint reprobe_id;
};

/*****************************************************************************/

static MMPortProbe *
probe_list_lookup_by_device (GList          *port_probes,
                             MMKernelDevice *kernel_port)
{
    GList *l;

    for (l = port_probes; l; l = g_list_next (l)) {
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (mm_kernel_device_cmp (mm_port_probe_peek_port (probe), kernel_port))
            return probe;
    }
    return NULL;
}

static MMPortProbe *
probe_list_lookup_by_name (GList       *port_probes,
                           const gchar *subsystem,
                           const gchar *name)
{
    GList *l;

    for (l = port_probes; l; l = g_list_next (l)) {
        MMPortProbe    *probe = MM_PORT_PROBE (l->data);
        MMKernelDevice *probe_device;

        probe_device = mm_port_probe_peek_port (probe);
        if ((g_strcmp0 (subsystem, mm_kernel_device_get_subsystem (probe_device)) == 0) &&
            (g_strcmp0 (name,      mm_kernel_device_get_name      (probe_device)) == 0))
            return probe;
    }
    return NULL;
}

static MMPortProbe *
device_find_probe_with_device (MMDevice       *self,
                               MMKernelDevice *kernel_port,
                               gboolean        lookup_ignored)
{
    MMPortProbe *probe;

    probe = probe_list_lookup_by_device (self->priv->port_probes, kernel_port);
    if (probe)
        return probe;

    if (!lookup_ignored)
        return NULL;

    return probe_list_lookup_by_device (self->priv->ignored_port_probes, kernel_port);
}

gboolean
mm_device_owns_port (MMDevice       *self,
                     MMKernelDevice *kernel_port)
{
    return !!device_find_probe_with_device (self, kernel_port, TRUE);
}

static MMPortProbe *
device_find_probe_with_name (MMDevice    *self,
                             const gchar *subsystem,
                             const gchar *name)
{
    MMPortProbe *probe;

    probe = probe_list_lookup_by_name (self->priv->port_probes, subsystem, name);
    if (probe)
        return probe;

    return probe_list_lookup_by_name (self->priv->ignored_port_probes, subsystem, name);
}

gboolean
mm_device_owns_port_name (MMDevice    *self,
                          const gchar *subsystem,
                          const gchar *name)
{
    return !!device_find_probe_with_name (self, subsystem, name);
}

static void
add_port_driver (MMDevice       *self,
                 MMKernelDevice *kernel_port)
{
    const gchar *driver;
    guint n_items;
    guint i;

    driver = mm_kernel_device_get_driver (kernel_port);
    if (!driver)
        return;

    n_items = (self->priv->drivers ? g_strv_length (self->priv->drivers) : 0);
    if (n_items > 0) {
        /* Add driver to our list of drivers, if not already there */
        for (i = 0; self->priv->drivers[i]; i++) {
            if (g_str_equal (self->priv->drivers[i], driver)) {
                driver = NULL;
                break;
            }
        }
    }

    if (!driver)
        return;

    self->priv->drivers = g_realloc (self->priv->drivers, (n_items + 2) * sizeof (gchar *));
    self->priv->drivers[n_items] = g_strdup (driver);
    self->priv->drivers[n_items + 1] = NULL;
}

gboolean
mm_device_grab_port (MMDevice       *self,
                     MMKernelDevice *kernel_port)
{
    MMPortProbe    *probe;
    MMKernelDevice *lower_port;

    if (mm_device_owns_port (self, kernel_port))
        return TRUE;

    lower_port = mm_kernel_device_peek_lower_device (kernel_port);
    if (lower_port) {
        g_autoptr(GError) error = NULL;

        /* No port probing done, at this point this is not something we require
         * as all the virtual instantiated ports are net devices. */
        if (self->priv->modem && !mm_base_modem_grab_link_port (self->priv->modem, kernel_port, &error))
            mm_obj_dbg (self, "fully ignoring link port %s from now on: %s",
                        mm_kernel_device_get_name (kernel_port),
                        error->message);
        return FALSE;
    }
    if (!g_strcmp0 ("net", mm_kernel_device_get_subsystem (kernel_port)) &&
        mm_kernel_device_get_wwandev_sysfs_path (kernel_port)) {
        /* This is a wwan netdevice, possibly a multiplexed one.
         * Multiplexed wwan netdevices do not have a lower device, so they won't fall in the
         * previous check verified for virtual ports, but require the same management.
         * However, we need to make sure that the arrived netdevice is not the default one that
         * instead requires the standard flow: for doing this we check that the name of the
         * arrived netdevice is not the default one, found in the wwandev_sysfs_path */
        if (!g_strstr_len (mm_kernel_device_get_wwandev_sysfs_path (kernel_port),
                           -1,
                           mm_kernel_device_get_name (kernel_port))) {
            g_autoptr(GError) error = NULL;

            mm_obj_dbg (self, "grabbing wwan multiplexed device %s", mm_kernel_device_get_name (kernel_port));
            if (self->priv->modem && !mm_base_modem_grab_link_port (self->priv->modem, kernel_port, &error))
                mm_obj_dbg (self, "fully ignoring link port %s from now on: %s",
                            mm_kernel_device_get_name (kernel_port),
                            error->message);
            return FALSE;
        }
    }

    /* Get the vendor/product IDs out of the first one that gives us
     * some valid value (it seems we may get NULL reported for VID in QMI
     * ports, e.g. Huawei E367) */
    if (!self->priv->vendor && !self->priv->product) {
        self->priv->vendor  = mm_kernel_device_get_physdev_vid (kernel_port);
        self->priv->product = mm_kernel_device_get_physdev_pid (kernel_port);
    }

    if (!self->priv->subsystem_vendor)
        self->priv->subsystem_vendor  = mm_kernel_device_get_physdev_subsystem_vid (kernel_port);

    if (!self->priv->subsystem_device)
        self->priv->subsystem_device  = mm_kernel_device_get_physdev_subsystem_pid (kernel_port);

    /* Add new port driver */
    add_port_driver (self, kernel_port);

    /* Create and store new port probe */
    probe = mm_port_probe_new (self, kernel_port);
    self->priv->port_probes = g_list_prepend (self->priv->port_probes, probe);

    return TRUE;
}

void
mm_device_release_port_name (MMDevice    *self,
                             const gchar *subsystem,
                             const gchar *name)
{
    MMPortProbe *probe;

    /* If modem exists, try to remove it as a link port. We also avoid emitting
     * the PORT_RELEASED signal in this case, as the link ports are not associated
     * to the port probe list */
    if (self->priv->modem && mm_base_modem_release_link_port (self->priv->modem, subsystem, name, NULL))
        return;

    probe = device_find_probe_with_name (self, subsystem, name);
    if (probe) {
        /* Found, remove from lists and destroy probe */
        if (g_list_find (self->priv->port_probes, probe))
            self->priv->port_probes = g_list_remove (self->priv->port_probes, probe);
        else if (g_list_find (self->priv->ignored_port_probes, probe))
            self->priv->ignored_port_probes = g_list_remove (self->priv->ignored_port_probes, probe);
        else
            g_assert_not_reached ();
        g_signal_emit (self, signals[SIGNAL_PORT_RELEASED], 0, mm_port_probe_peek_port (probe));
        g_object_unref (probe);
    }
}

void
mm_device_ignore_port  (MMDevice       *self,
                        MMKernelDevice *kernel_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, kernel_port, FALSE);
    if (probe) {
        /* Found, remove from list and add to the ignored list */
        mm_obj_dbg (self, "fully ignoring port %s from now on",
                    mm_kernel_device_get_name (kernel_port));
        self->priv->port_probes = g_list_remove (self->priv->port_probes, probe);
        self->priv->ignored_port_probes = g_list_prepend (self->priv->ignored_port_probes, probe);
    }
}

/*****************************************************************************/

static void
unexport_modem (MMDevice *self)
{
    gchar *path;

    g_assert (MM_IS_BASE_MODEM (self->priv->modem));
    g_assert (G_IS_DBUS_OBJECT_MANAGER (self->priv->object_manager));

    path = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (self->priv->modem)));
    if (path != NULL) {
        g_dbus_object_manager_server_unexport (self->priv->object_manager, path);
        g_object_set (self->priv->modem,
                      MM_BINDABLE_CONNECTION, NULL,
                      NULL);
        mm_obj_dbg (self, "unexported modem from path '%s'", path);
        g_free (path);
    }
}

/*****************************************************************************/

static void
export_modem (MMDevice *self)
{
    g_autoptr(GDBusConnection) connection = NULL;
    g_autofree gchar *existing_path = NULL;
    g_autofree gchar *path = NULL;

    g_assert (MM_IS_BASE_MODEM (self->priv->modem));
    g_assert (G_IS_DBUS_OBJECT_MANAGER (self->priv->object_manager));

    /* If modem not yet valid (not fully initialized), don't export it */
    if (!mm_base_modem_get_valid (self->priv->modem)) {
        mm_obj_dbg (self, "modem not yet fully initialized");
        return;
    }

    /* Don't export if we've aborted initialization */
    g_object_get (self->priv->object_manager,
                  "connection", &connection,
                  NULL);
    if (!connection) {
        mm_obj_dbg (self, "exporting aborted as there is no bus connection");
        return;
    }

    /* Don't export already exported modems */
    g_object_get (self->priv->modem,
                  "g-object-path", &existing_path,
                  NULL);
    if (existing_path) {
        mm_obj_dbg (self, "modem already exported");
        return;
    }

    /* No outstanding port tasks, so if the modem is valid we can export it */

    path = g_strdup_printf (MM_DBUS_MODEM_PREFIX "/%d", mm_base_modem_get_dbus_id (self->priv->modem));
    g_object_set (self->priv->modem,
                  "g-object-path", path,
                  MM_BINDABLE_CONNECTION, connection,
                  NULL);

    g_dbus_object_manager_server_export (self->priv->object_manager,
                                         G_DBUS_OBJECT_SKELETON (self->priv->modem));

    mm_obj_dbg (self, " exported modem at path '%s'", path);
}

/*****************************************************************************/

static void
initialize_ready (MMBaseModem   *modem,
                  GAsyncResult  *res,
                  MMDevice      *_self) /* full reference */
{
    g_autoptr(MMDevice) self = _self;
    g_autoptr(GError)   error = NULL;

    if (!mm_base_modem_initialize_finish (modem, res, &error)) {
        if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED)) {
            /* FATAL error, won't even be exported in DBus */
            mm_obj_err (self, "fatal error initializing: %s", error->message);
        } else {
            /* non-fatal error */
            mm_obj_warn (self, "error initializing: %s", error->message);
            mm_base_modem_set_valid (modem, TRUE);
        }
    } else {
        mm_obj_dbg (self, "modem initialized");
        mm_base_modem_set_valid (modem, TRUE);
    }
}

void
mm_device_initialize_modem (MMDevice *self)
{
    MMBaseModem *modem;

    modem = mm_device_peek_modem (self);
    if (!modem) {
        mm_obj_warn (self, "cannot initialize modem: not found");
        return;
    }

    mm_obj_dbg (self, "modem initializing...");
    mm_base_modem_initialize (modem,
                              MM_OPERATION_LOCK_REQUIRED,
                              (GAsyncReadyCallback)initialize_ready,
                              g_object_ref (self));
}

/*****************************************************************************/

static void
clear_modem (MMDevice *self)
{
    if (self->priv->modem_valid_id) {
        g_signal_handler_disconnect (self->priv->modem, self->priv->modem_valid_id);
        self->priv->modem_valid_id = 0;
    }

    if (self->priv->modem) {
        /* Run dispose before unref-ing, in order to cleanup the SIM object,
         * if any (which also holds a reference to the modem object) */
        g_object_run_dispose (G_OBJECT (self->priv->modem));
        g_clear_object (&(self->priv->modem));
    }
}

gboolean
mm_device_remove_modem_finish (MMDevice        *self,
                               GAsyncResult    *res,
                               GError         **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
teardown_ports_ready (MMBaseModem  *modem,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMDevice *self;
    GError   *error = NULL;
    gboolean  success;

    self = g_task_get_source_object (task);

    success = mm_base_modem_teardown_ports_finish (modem, res, &error);
    clear_modem (self);

    if (!success)
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_device_remove_modem (MMDevice            *self,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);
    if (!self->priv->modem) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    unexport_modem (self);
    g_cancellable_cancel (mm_base_modem_peek_cancellable (self->priv->modem));
    mm_base_modem_teardown_ports (self->priv->modem,
                                  (GAsyncReadyCallback)teardown_ports_ready,
                                  task);
}

/* Should only be used from testing code or when the hardware is no longer
 * present and cannot be gracefully cleaned up.
 */
void
mm_device_remove_modem_quick (MMDevice *self)
{
    if (self->priv->modem) {
        unexport_modem (self);
        clear_modem (self);
    }
}

/*****************************************************************************/

#define REPROBE_SECS 2

static gboolean
reprobe (MMDevice *self)
{
    g_autoptr (GError) error = NULL;

    self->priv->reprobe_id = 0;

    mm_obj_dbg (self, "Reprobing modem...");
    if (!mm_device_create_modem (self, &error)) {
        mm_obj_warn (self, "could not recreate modem: %s", error->message);
    } else {
        mm_obj_dbg (self, "modem recreated");
        mm_device_initialize_modem (self);
    }

    return G_SOURCE_REMOVE;
}

static void
modem_valid_remove_ready (MMDevice     *self,
                          GAsyncResult *res,
                          MMBaseModem  *modem)
{
    g_autoptr(GError) error = NULL;

    if (!mm_device_remove_modem_finish (self, res, &error))
        mm_obj_warn (self, "removing modem failed: %s", error->message);

    if (mm_base_modem_get_reprobe (modem)) {
        if (self->priv->reprobe_id)
            g_source_remove (self->priv->reprobe_id);
        self->priv->reprobe_id = g_timeout_add_seconds (REPROBE_SECS, (GSourceFunc)reprobe, self);
    }

    g_object_unref (modem);
}

static void
modem_valid (MMBaseModem *modem,
             GParamSpec  *pspec,
             MMDevice    *self)
{
    if (!mm_base_modem_get_valid (modem)) {
        /* Modem no longer valid */
        mm_device_remove_modem (self,
                                (GAsyncReadyCallback)modem_valid_remove_ready,
                                g_object_ref (modem));
    } else {
        /* Modem now valid, export it, but only if we really have it around.
         * It may happen that the initialization sequence fails because the
         * modem gets disconnected, and in that case we don't really need
         * to export it */
        if (self->priv->modem)
            export_modem (self);
        else
            mm_obj_dbg (self, "not exporting modem; no longer available");
    }
}

gboolean
mm_device_create_modem (MMDevice  *self,
                        GError   **error)
{
    g_assert (self->priv->modem == NULL);

    if (self->priv->inhibited) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Device is inhibited");
        return FALSE;
    }

    if (!self->priv->virtual) {
        g_autofree gchar *device_id_info = NULL;

        if (!self->priv->port_probes) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Not creating a device without ports");
            return FALSE;
        }

        /* PCI devices will have all subsystem vendor, vendor and product */
        if (self->priv->subsystem_vendor) {
            device_id_info = g_strdup_printf ("(%04x:%04x:%04x)",
                                              self->priv->subsystem_vendor,
                                              self->priv->vendor,
                                              self->priv->product);
        }
        /* USB devices will have all vendor and product */
        else if (self->priv->vendor || self->priv->product) {
            device_id_info = g_strdup_printf ("(%04x:%04x)",
                                              self->priv->vendor,
                                              self->priv->product);
        }
        /* else, serial devices will not have any */

        mm_obj_msg (self, "creating modem with plugin '%s' and '%u' ports %s",
                    mm_plugin_get_name (self->priv->plugin),
                    g_list_length (self->priv->port_probes),
                    device_id_info ? device_id_info : "");

    } else {
        if (!self->priv->virtual_ports) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_FAILED,
                         "Not creating a virtual device without ports");
            return FALSE;
        }

        mm_obj_msg (self, "creating virtual modem with plugin '%s' and '%u' ports",
                    mm_plugin_get_name (self->priv->plugin),
                    g_strv_length (self->priv->virtual_ports));
    }

    self->priv->modem = mm_plugin_create_modem (self->priv->plugin, self, error);
    if (self->priv->modem)
        /* We want to get notified when the modem becomes valid/invalid */
        self->priv->modem_valid_id = g_signal_connect (self->priv->modem,
                                                       "notify::" MM_BASE_MODEM_VALID,
                                                       G_CALLBACK (modem_valid),
                                                       self);

    return !!self->priv->modem;
}

/*****************************************************************************/

const gchar *
mm_device_get_uid (MMDevice *self)
{
    return self->priv->uid;
}

const gchar *
mm_device_get_physdev (MMDevice *self)
{
    return self->priv->physdev;
}

const gchar **
mm_device_get_drivers (MMDevice *self)
{
    return (const gchar **)self->priv->drivers;
}

guint16
mm_device_get_vendor (MMDevice *self)
{
    return self->priv->vendor;
}

guint16
mm_device_get_product (MMDevice *self)
{
    return self->priv->product;
}

guint16
mm_device_get_subsystem_vendor (MMDevice *self)
{
    return self->priv->subsystem_vendor;
}

guint16
mm_device_get_subsystem_device (MMDevice *self)
{
    return self->priv->subsystem_device;
}


void
mm_device_set_plugin (MMDevice *self,
                      GObject  *plugin)
{
    g_object_set (self,
                  MM_DEVICE_PLUGIN, plugin,
                  NULL);
}

GObject *
mm_device_peek_plugin (MMDevice *self)
{
    return (self->priv->plugin ?
            G_OBJECT (self->priv->plugin) :
            NULL);
}

GObject *
mm_device_get_plugin (MMDevice *self)
{
    return (self->priv->plugin ?
            G_OBJECT (g_object_ref (self->priv->plugin)) :
            NULL);
}

MMBaseModem *
mm_device_peek_modem (MMDevice *self)
{
    return (self->priv->modem ?
            MM_BASE_MODEM (self->priv->modem) :
            NULL);
}

MMBaseModem *
mm_device_get_modem (MMDevice *self)
{
    return (self->priv->modem ?
            MM_BASE_MODEM (g_object_ref (self->priv->modem)) :
            NULL);
}

GObject *
mm_device_peek_port_probe (MMDevice       *self,
                           MMKernelDevice *kernel_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, kernel_port, FALSE);
    return (probe ? G_OBJECT (probe) : NULL);
}

GObject *
mm_device_get_port_probe (MMDevice       *self,
                          MMKernelDevice *kernel_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, kernel_port, FALSE);
    return (probe ? G_OBJECT (g_object_ref (probe)) : NULL);
}

GList *
mm_device_peek_port_probe_list (MMDevice *self)
{
    return self->priv->port_probes;
}

void
mm_device_reset_port_probe_list (MMDevice *self)
{
    GList *l;

    mm_obj_dbg (self, "port probe list reset...");
    for (l = self->priv->port_probes; l; l = g_list_next (l)) {
        mm_port_probe_reset (MM_PORT_PROBE (l->data));
    }
}

gboolean
mm_device_get_hotplugged (MMDevice *self)
{
    return self->priv->hotplugged;
}

void
mm_device_reset_hotplugged (MMDevice *self)
{
    self->priv->hotplugged = FALSE;
}

gboolean
mm_device_get_inhibited (MMDevice *self)
{
    return self->priv->inhibited;
}

/*****************************************************************************/

gboolean
mm_device_inhibit_finish (MMDevice      *self,
                          GAsyncResult  *res,
                          GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
inhibit_disable_modem_remove_ready (MMDevice      *self,
                                    GAsyncResult  *res,
                                    GTask         *task)
{
    g_autoptr(GError) error = NULL;

    if (!mm_device_remove_modem_finish (self, res, &error))
        mm_obj_warn (self, "removing modem failed: %s", error->message);
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
inhibit_disable_ready (MMBaseModem  *modem,
                       GAsyncResult *res,
                       GTask        *task)
{
    MMDevice *self;
    GError   *error = NULL;

    self = g_task_get_source_object (task);

    if (!mm_base_modem_disable_finish (modem, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_device_remove_modem (self,
                            (GAsyncReadyCallback)inhibit_disable_modem_remove_ready,
                            task);
}

void
mm_device_inhibit (MMDevice            *self,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    /* We want to allow inhibiting only devices that are currently
     * exported in the bus, because otherwise we may be inhibiting
     * in the middle of port probing and that may lead to some ports
     * tracked inside the device object during inhibition and some
     * other ports tracked in the base manager. So, if the device
     * does not have a valid modem created and exposed, do not allow
     * the inhibition. */
    if (!self->priv->modem || !g_dbus_object_get_object_path (G_DBUS_OBJECT (self->priv->modem))) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                 "Modem not exported in the bus");
        g_object_unref (task);
        return;
    }

    /* Flag as inhibited right away */
    g_assert (!self->priv->inhibited);
    self->priv->inhibited = TRUE;

    /* Make sure modem is disabled while inhibited. This operation requests
     * an exclusive lock marked as override, so the modem object will not
     * allow any additional lock request any more. */
    mm_base_modem_disable (self->priv->modem,
                           MM_OPERATION_LOCK_REQUIRED,
                           MM_OPERATION_PRIORITY_OVERRIDE,
                           (GAsyncReadyCallback)inhibit_disable_ready,
                           task);
}

gboolean
mm_device_uninhibit (MMDevice  *self,
                     GError   **error)
{
    g_assert (self->priv->inhibited);
    self->priv->inhibited = FALSE;

    if (!mm_device_create_modem (self, error))
        return FALSE;

    mm_device_initialize_modem (self);
    return TRUE;
}

/*****************************************************************************/

void
mm_device_virtual_grab_ports (MMDevice *self,
                              const gchar **ports)
{
    g_return_if_fail (ports != NULL);
    g_return_if_fail (self->priv->virtual);

    /* Setup drivers array */
    self->priv->drivers = g_malloc (2 * sizeof (gchar *));
    self->priv->drivers[0] = g_strdup ("virtual");
    self->priv->drivers[1] = NULL;

    /* Keep virtual port names */
    self->priv->virtual_ports = g_strdupv ((gchar **)ports);
}

const gchar **
mm_device_virtual_peek_ports (MMDevice *self)
{
    g_return_val_if_fail (self->priv->virtual, NULL);

    return (const gchar **)self->priv->virtual_ports;
}

gboolean
mm_device_is_virtual (MMDevice *self)
{
    return self->priv->virtual;
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMDevice *self;

    self = MM_DEVICE (_self);
    return g_strdup_printf ("device %s", self->priv->uid);
}

/*****************************************************************************/

MMDevice *
mm_device_new (const gchar              *uid,
               const gchar              *physdev,
               gboolean                  hotplugged,
               gboolean                  virtual,
               GDBusObjectManagerServer *object_manager)
{
    g_return_val_if_fail (uid != NULL, NULL);

    return MM_DEVICE (g_object_new (MM_TYPE_DEVICE,
                                    MM_DEVICE_UID,            uid,
                                    MM_DEVICE_PHYSDEV,        physdev,
                                    MM_DEVICE_HOTPLUGGED,     hotplugged,
                                    MM_DEVICE_VIRTUAL,        virtual,
                                    MM_DEVICE_OBJECT_MANAGER, object_manager,
                                    NULL));
}

static void
mm_device_init (MMDevice *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_DEVICE, MMDevicePrivate);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMDevice *self = MM_DEVICE (object);

    switch (prop_id) {
    case PROP_UID:
        /* construct only */
        self->priv->uid = g_value_dup_string (value);
        break;
    case PROP_PHYSDEV:
        /* construct only */
        self->priv->physdev = g_value_dup_string (value);
        break;
    case PROP_OBJECT_MANAGER:
        /* construct only */
        self->priv->object_manager = g_value_dup_object (value);
        break;
    case PROP_PLUGIN:
        g_clear_object (&(self->priv->plugin));
        self->priv->plugin = g_value_dup_object (value);
        break;
    case PROP_MODEM:
        g_assert (self->priv->modem == NULL);
        self->priv->modem = g_value_dup_object (value);
        break;
    case PROP_HOTPLUGGED:
        self->priv->hotplugged = g_value_get_boolean (value);
        break;
    case PROP_VIRTUAL:
        self->priv->virtual = g_value_get_boolean (value);
        break;
    case PROP_INHIBITED:
        self->priv->inhibited = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMDevice *self = MM_DEVICE (object);

    switch (prop_id) {
    case PROP_UID:
        g_value_set_string (value, self->priv->uid);
        break;
    case PROP_PHYSDEV:
        g_value_set_object (value, self->priv->physdev);
        break;
    case PROP_OBJECT_MANAGER:
        g_value_set_object (value, self->priv->object_manager);
        break;
    case PROP_PLUGIN:
        g_value_set_object (value, self->priv->plugin);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    case PROP_HOTPLUGGED:
        g_value_set_boolean (value, self->priv->hotplugged);
        break;
    case PROP_VIRTUAL:
        g_value_set_boolean (value, self->priv->virtual);
        break;
    case PROP_INHIBITED:
        g_value_set_boolean (value, self->priv->inhibited);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
    MMDevice *self = MM_DEVICE (object);

    if (self->priv->reprobe_id) {
        g_source_remove (self->priv->reprobe_id);
        self->priv->reprobe_id = 0;
    }
    g_clear_object (&(self->priv->object_manager));
    g_clear_object (&(self->priv->plugin));
    g_list_free_full (self->priv->port_probes, g_object_unref);
    self->priv->port_probes = NULL;
    g_list_free_full (self->priv->ignored_port_probes, g_object_unref);
    self->priv->ignored_port_probes = NULL;

    clear_modem (self);

    G_OBJECT_CLASS (mm_device_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    MMDevice *self = MM_DEVICE (object);

    g_free (self->priv->physdev);
    g_free (self->priv->uid);
    g_strfreev (self->priv->drivers);
    g_strfreev (self->priv->virtual_ports);

    G_OBJECT_CLASS (mm_device_parent_class)->finalize (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_device_class_init (MMDeviceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMDevicePrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize     = finalize;
    object_class->dispose      = dispose;

    properties[PROP_UID] =
        g_param_spec_string (MM_DEVICE_UID,
                             "Unique ID",
                             "Unique device id, e.g. the physical device sysfs path",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_UID, properties[PROP_UID]);

    properties[PROP_PHYSDEV] =
        g_param_spec_string (MM_DEVICE_PHYSDEV,
                             "Physdev",
                             "Physical device path",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PHYSDEV, properties[PROP_PHYSDEV]);

    properties[PROP_OBJECT_MANAGER] =
        g_param_spec_object (MM_DEVICE_OBJECT_MANAGER,
                             "Object manager",
                             "GDBus object manager server",
                             G_TYPE_DBUS_OBJECT_MANAGER_SERVER,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_OBJECT_MANAGER, properties[PROP_OBJECT_MANAGER]);

    properties[PROP_PLUGIN] =
        g_param_spec_object (MM_DEVICE_PLUGIN,
                             "Plugin",
                             "Best plugin to manage this device",
                             MM_TYPE_PLUGIN,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PLUGIN, properties[PROP_PLUGIN]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_DEVICE_MODEM,
                             "Modem",
                             "The modem object",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    properties[PROP_HOTPLUGGED] =
        g_param_spec_boolean (MM_DEVICE_HOTPLUGGED,
                              "Hotplugged",
                              "Whether the modem was hotplugged",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_HOTPLUGGED, properties[PROP_HOTPLUGGED]);

    properties[PROP_VIRTUAL] =
        g_param_spec_boolean (MM_DEVICE_VIRTUAL,
                              "Virtual",
                              "Whether the device is virtual or real",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_VIRTUAL, properties[PROP_VIRTUAL]);

    properties[PROP_INHIBITED] =
        g_param_spec_boolean (MM_DEVICE_INHIBITED,
                              "Inhibited",
                              "Whether the modem is inhibited",
                              FALSE,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_INHIBITED, properties[PROP_INHIBITED]);

    signals[SIGNAL_PORT_RELEASED] =
        g_signal_new (MM_DEVICE_PORT_RELEASED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMDeviceClass, port_released),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, MM_TYPE_KERNEL_DEVICE);
}
