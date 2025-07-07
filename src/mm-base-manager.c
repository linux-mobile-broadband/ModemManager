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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google, Inc
 * Copyright (C) 2016 Velocloud, Inc.
 * Copyright (C) 2011 - 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include "gio/gio.h"
#include "glib.h"
#include <config.h>

#include <string.h>
#include <ctype.h>

#include <gmodule.h>

#if defined WITH_QMI
# include <libqmi-glib.h>
#endif
#if defined WITH_QRTR
# include "mm-kernel-device-qrtr.h"
# include "mm-qrtr-bus-watcher.h"
#endif
#if defined WITH_UDEV
# include "mm-kernel-device-udev.h"
#endif
#include "mm-kernel-device-generic.h"

#include <ModemManager.h>
#include <ModemManager-tags.h>

#include <mm-errors-types.h>
#include "mm-error-helpers.h"

#include <mm-gdbus-manager.h>
#if defined WITH_TESTS
# include <mm-gdbus-test.h>
#endif

#include "mm-context.h"
#include "mm-base-manager.h"
#include "mm-daemon-enums-types.h"
#include "mm-device.h"
#include "mm-plugin-manager.h"
#include "mm-auth-provider.h"
#include "mm-plugin.h"
#include "mm-filter.h"
#include "mm-log-object.h"
#include "mm-base-modem.h"
#include "mm-iface-modem.h"

#include "mm-dispatcher-modem-setup.h"

static void initable_iface_init   (GInitableIface       *iface);
static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_TYPE_EXTENDED (MMBaseManager, mm_base_manager, MM_GDBUS_TYPE_ORG_FREEDESKTOP_MODEM_MANAGER1_SKELETON, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                        G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

enum {
    PROP_0,
    PROP_CONNECTION,
    PROP_AUTO_SCAN,
    PROP_FILTER_POLICY,
#if !defined WITH_BUILTIN_PLUGINS
    PROP_PLUGIN_DIR,
#endif
    PROP_INITIAL_KERNEL_EVENTS,
#if defined WITH_TESTS
    PROP_ENABLE_TEST,
#endif
    LAST_PROP
};

struct _MMBaseManagerPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    /* Whether auto-scanning is enabled */
    gboolean auto_scan;
    /* Filter policy (mask of enabled rules) */
    MMFilterRule filter_policy;
#if !defined WITH_BUILTIN_PLUGINS
    /* Path to look for plugins */
    gchar *plugin_dir;
#endif
    /* Path to the list of initial kernel events */
    gchar *initial_kernel_events;
    /* The authorization provider */
    MMAuthProvider *authp;
    GCancellable *authp_cancellable;
    /* The Plugin Manager object */
    MMPluginManager *plugin_manager;
    /* The port/device filter */
    MMFilter *filter;
    /* The container of devices being prepared */
    GHashTable *devices;
    /* The Object Manager server */
    GDBusObjectManagerServer *object_manager;
    /* The map of inhibited devices */
    GHashTable *inhibited_devices;

#if defined WITH_TESTS
    /* Whether the test interface is enabled */
    gboolean enable_test;
    /* The Test interface support */
    MmGdbusTest *test_skeleton;
#endif

#if defined WITH_UDEV
    /* The UDev client */
    GUdevClient *udev;
#endif
#if defined WITH_QRTR
    /* The Qrtr Bus Watcher */
    MMQrtrBusWatcher *qrtr_bus_watcher;
#endif
};

/*****************************************************************************/

static MMDevice *
find_device_by_port (MMBaseManager  *manager,
                     MMKernelDevice *port)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, manager->priv->devices);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMDevice *candidate = MM_DEVICE (value);

        if (mm_device_owns_port (candidate, port))
            return candidate;
    }
    return NULL;
}

static MMDevice *
find_device_by_port_name (MMBaseManager *manager,
                          const gchar   *subsystem,
                          const gchar   *name)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, manager->priv->devices);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMDevice *candidate = MM_DEVICE (value);

        if (mm_device_owns_port_name (candidate, subsystem, name))
            return candidate;
    }
    return NULL;
}

static MMDevice *
find_device_by_physdev_uid (MMBaseManager *self,
                            const gchar   *physdev_uid)
{
    return g_hash_table_lookup (self->priv->devices, physdev_uid);
}

/*****************************************************************************/

typedef struct {
    MMBaseManager *self;
    MMDevice      *device;
} FindDeviceSupportContext;

static void
find_device_support_context_free (FindDeviceSupportContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->device);
    g_slice_free (FindDeviceSupportContext, ctx);
}

static void
dispatcher_modem_setup_ready (MMDispatcherModemSetup    *dispatcher,
                              GAsyncResult              *res,
                              FindDeviceSupportContext  *ctx)
{
    g_autoptr(GError) error = NULL;

    if (!mm_dispatcher_modem_setup_run_finish (dispatcher, res, &error)) {
        if (!g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND))
            mm_obj_warn (ctx->self, "couldn't run setup for device '%s': %s",
                         mm_device_get_uid (ctx->device), error->message);
        else
            mm_obj_dbg (ctx->self, "no need to run setup for device '%s'",
                        mm_device_get_uid (ctx->device));
    } else
        mm_obj_dbg (ctx->self, "setup for device '%s' finished",
                    mm_device_get_uid (ctx->device));

    /* launch async modem initialization */
    mm_device_initialize_modem (ctx->device);

    find_device_support_context_free (ctx);
}

static void
modem_setup (FindDeviceSupportContext *ctx)
{
    guint                     n_port_infos = 0;
    g_auto(GStrv)             modem_ports = NULL;
    MMDispatcherModemSetup   *dispatcher;
    MMModemPortInfo          *port_infos;
    MMBaseModem              *modem = NULL;
    GPtrArray                *aux;
    guint                     i;

    dispatcher = mm_dispatcher_modem_setup_get ();

    modem = mm_device_peek_modem (ctx->device);

    aux = g_ptr_array_new ();
    port_infos = mm_base_modem_get_port_infos (modem, &n_port_infos);
    for (i = 0; i < n_port_infos; i++) {
        switch (port_infos[i].type) {
            case MM_MODEM_PORT_TYPE_AT:
            case MM_MODEM_PORT_TYPE_QMI:
            case MM_MODEM_PORT_TYPE_MBIM:
            case MM_MODEM_PORT_TYPE_XMMRPC:
                g_ptr_array_add (aux, g_strdup (port_infos[i].name));
                break;
            case MM_MODEM_PORT_TYPE_UNKNOWN:
            case MM_MODEM_PORT_TYPE_NET:
            case MM_MODEM_PORT_TYPE_QCDM:
            case MM_MODEM_PORT_TYPE_GPS:
            case MM_MODEM_PORT_TYPE_AUDIO:
            case MM_MODEM_PORT_TYPE_IGNORED:
            default:
                break;
        }
    }

    mm_modem_port_info_array_free (port_infos, n_port_infos);
    g_ptr_array_add (aux, NULL);
    modem_ports = (GStrv) g_ptr_array_free (aux, FALSE);

    mm_obj_msg (ctx->self, "running setup for device '%s'...",
                mm_device_get_uid (ctx->device));
    mm_dispatcher_modem_setup_run (dispatcher,
                                   mm_base_modem_get_vendor_id (modem),
                                   mm_base_modem_get_product_id (modem),
                                   mm_base_modem_get_device (modem),
                                   modem_ports,
                                   NULL,
                                   (GAsyncReadyCallback)dispatcher_modem_setup_ready,
                                   ctx);
}

static void
device_support_check_ready (MMPluginManager          *plugin_manager,
                            GAsyncResult             *res,
                            FindDeviceSupportContext *ctx)
{
    g_autoptr(GError)   error = NULL;
    g_autoptr(MMPlugin) plugin = NULL;

    /* If the device support check fails, either with an error, or afterwards
     * when trying to create a modem object, we must reset the port probe list
     * in the MMDevice, so that a manual scan request afterwards re-scans all
     * ports. */

    /* Receive plugin result from the plugin manager */
    plugin = mm_plugin_manager_device_support_check_finish (plugin_manager, res, &error);
    if (!plugin) {
        mm_obj_msg (ctx->self, "couldn't check support for device '%s': %s",
                    mm_device_get_uid (ctx->device), error->message);
        mm_device_reset_port_probe_list (ctx->device);
        find_device_support_context_free (ctx);
        return;
    }

    /* Set the plugin as the one expected in the device */
    mm_device_set_plugin (ctx->device, G_OBJECT (plugin));

    if (!mm_device_create_modem (ctx->device, &error)) {
        mm_obj_warn (ctx->self, "couldn't create modem for device '%s': %s",
                     mm_device_get_uid (ctx->device), error->message);
        mm_device_reset_port_probe_list (ctx->device);
        find_device_support_context_free (ctx);
        return;
    }

    /* Modem now created */
    mm_obj_msg (ctx->self, "modem for device '%s' successfully created",
                mm_device_get_uid (ctx->device));

    modem_setup (ctx);
}

static gboolean is_device_inhibited           (MMBaseManager  *self,
                                               const gchar    *physdev_uid);
static void     device_inhibited_track_port   (MMBaseManager  *self,
                                               const gchar    *physdev_uid,
                                               MMKernelDevice *port,
                                               gboolean        manual_scan);
static void     device_inhibited_untrack_port (MMBaseManager  *self,
                                               const gchar    *subsystem,
                                               const gchar    *name);

static void
device_removed (MMBaseManager *self,
                const gchar   *subsystem,
                const gchar   *name)
{
    g_autoptr(MMDevice) device = NULL;

    g_assert (subsystem);
    g_assert (name);
    device = find_device_by_port_name (self, subsystem, name);
    if (!device) {
        /* If the device was inhibited and the port is gone, untrack it.
         * This is only needed for ports that were tracked out of device objects.
         * In this case we don't rely on the physdev uid, as API-reported
         * remove kernel events may not include uid. */
        device_inhibited_untrack_port (self, subsystem, name);
        return;
    }

    /* The callbacks triggered when the port is released or device support is
     * cancelled may end up unreffing the device or removing it from the HT, and
     * so in order to make sure the reference is still valid when we call
     * support_check_cancel() and g_hash_table_remove(), we hold a full reference
     * ourselves. */
    g_object_ref (device);

    mm_obj_msg (self, "port %s released by device '%s'", name, mm_device_get_uid (device));
    mm_device_release_port_name (device, subsystem, name);

    /* If port probe list gets empty, remove the device object itself */
    if (!mm_device_peek_port_probe_list (device)) {
        mm_obj_dbg (self, "removing empty device '%s'", mm_device_get_uid (device));
        if (mm_plugin_manager_device_support_check_cancel (self->priv->plugin_manager, device))
            mm_obj_dbg (self, "device support check has been cancelled");

        /* The device may have already been removed from the tracking HT, we
         * just try to remove it and if it fails, we ignore it */
        mm_device_remove_modem_quick (device);
        g_hash_table_remove (self->priv->devices, mm_device_get_uid (device));
    }
}

static void
device_support_check_add_all_ports (MMBaseManager  *self,
                                    MMDevice       *device)
{
    FindDeviceSupportContext *ctx;
    GList                    *l;

    g_assert (!mm_plugin_manager_device_support_check_ongoing (self->priv->plugin_manager, device));

    /* There is no running device support context, so we launch a new one */
    ctx = g_slice_new (FindDeviceSupportContext);
    ctx->self = g_object_ref (self);
    ctx->device = g_object_ref (device);
    mm_plugin_manager_device_support_check (self->priv->plugin_manager,
                                            device,
                                            (GAsyncReadyCallback) device_support_check_ready,
                                            ctx);

    g_assert (mm_plugin_manager_device_support_check_ongoing (self->priv->plugin_manager, device));

    /* Iterate all known ports and notify them one by one */
    for (l = mm_device_peek_port_probe_list (device); l; l = g_list_next (l)) {
        mm_plugin_manager_device_support_check_add_port (self->priv->plugin_manager,
                                                         device,
                                                         mm_port_probe_peek_port (MM_PORT_PROBE (l->data)));
    }
}

static void
device_support_check_add_single_port (MMBaseManager  *self,
                                      MMDevice       *device,
                                      MMKernelDevice *port)
{
    FindDeviceSupportContext *ctx;
    gboolean                  added;

    /* Try to add the port to an already running device support context */
    if (mm_plugin_manager_device_support_check_add_port (self->priv->plugin_manager, device, port))
        return;

    /* There is no running device support context, so we launch a new one */
    ctx = g_slice_new (FindDeviceSupportContext);
    ctx->self = g_object_ref (self);
    ctx->device = g_object_ref (device);
    mm_plugin_manager_device_support_check (self->priv->plugin_manager,
                                            device,
                                            (GAsyncReadyCallback) device_support_check_ready,
                                            ctx);

    /* Retry now, which should never fail */
    added = mm_plugin_manager_device_support_check_add_port (self->priv->plugin_manager, device, port);
    g_assert (added);
}

static void
existing_port (MMBaseManager  *self,
               MMDevice       *device,
               MMKernelDevice *port)
{
    const gchar *name;
    const gchar *uid;

    name = mm_kernel_device_get_name (port);
    uid = mm_device_get_uid (device);

    mm_obj_dbg (self, "port %s already available in device '%s'", name, uid);

    /* If the port exists already in the device, and we already have a valid modem
     * object created, there is no point in processing this event, we can warn about
     * the situation and bail out. This may happen e.g. if a manual scan is requested
     * after having successfully finished the last probing operation. */
    if (mm_device_peek_plugin (device) && mm_device_peek_modem (device)) {
        mm_obj_dbg (self, "ignoring port %s as device '%s' already has a valid modem", name, uid);
        return;
    }

    /* The port exists in the device, but we don't have a valid modem associated with
     * it. This may happen if a manual scan is requested after having failed the last
     * device probing operation. We need to relaunch a new device probing task, unless
     * one is already running. */
    device_support_check_add_single_port (self, device, port);
}

static void
additional_port_modem_remove_ready (MMDevice      *device,
                                    GAsyncResult  *res,
                                    MMBaseManager *self)
{
    g_autoptr(GError) error = NULL;

    if (!mm_device_remove_modem_finish (device, res, &error))
        mm_obj_warn (device, "removing modem failed: %s", error->message);
    device_support_check_add_all_ports (self, device);
    g_object_unref (self);
}

static void
additional_port (MMBaseManager  *self,
                 MMDevice       *device,
                 MMKernelDevice *port)
{
    MMBaseModem *modem;
    const gchar *name;
    const gchar *uid;

    name = mm_kernel_device_get_name (port);
    uid = mm_device_get_uid (device);

    mm_obj_dbg (self, "additional port %s in device '%s'", name, uid);

    /* Do nothing if the device is ignoring the new port */
    if (!mm_device_grab_port (device, port))
        return;

    /* If there is an ongoing support check, we can add the single port right away */
    if (mm_plugin_manager_device_support_check_ongoing (self->priv->plugin_manager, device)) {
        device_support_check_add_single_port (self, device, port);
        return;
    }

    mm_obj_warn (self, "additional port %s in device '%s' added after device probing has already finished", name, uid);

    /* If there is no plugin assigned in the device, it means the device probing had failed earlier,
     * for example if none of the ports that were used for the device probing were usable control ports */
    if (!mm_device_peek_plugin (device)) {
        g_assert (!mm_device_peek_modem (device));
        mm_obj_info (self, "last device '%s' probing had failed, will retry", uid);
        device_support_check_add_all_ports (self, device);
        return;
    }

    /* If there is no modem object assigned to the device, it means the modem object creation had failed earlier,
     * for example if a required port was missing (e.g. missing net port in a QMI or MBIM modem) */
    modem = mm_device_peek_modem (device);
    if (!modem) {
        mm_obj_info (self, "last modem object creation in device '%s' had failed, will retry", uid);
        device_support_check_add_all_ports (self, device);
        return;
    }

    mm_obj_info (self, "last modem object creation in device '%s' succeeded, but we have a new port addition, will retry", uid);
    mm_device_remove_modem (device,
                            (GAsyncReadyCallback)additional_port_modem_remove_ready,
                            g_object_ref (self));
}

static void
first_port (MMBaseManager  *self,
            MMKernelDevice *port,
            gboolean        hotplugged)
{
    g_autoptr(MMDevice)  device = NULL;
    const gchar         *name;
    const gchar         *uid;
    const gchar         *physdev;

    name = mm_kernel_device_get_name (port);
    uid = mm_kernel_device_get_physdev_uid (port);

    mm_obj_dbg (self, "port %s is first in device '%s'", name, uid);

    physdev = mm_kernel_device_get_physdev_sysfs_path (port);
    device = mm_device_new (uid, physdev, hotplugged, FALSE, self->priv->object_manager);

    /* If the device is ignoring the new port, discard the new device as well */
    if (!mm_device_grab_port (device, port)) {
        mm_obj_dbg (self, "discarding device '%s' as the first port is not grabbed", uid);
        return;
    }

    /* Store the device */
    g_hash_table_insert (self->priv->devices, g_strdup (uid), g_object_ref (device));

    /* And start device support check */
    device_support_check_add_single_port (self, device, port);
}

static void
device_added (MMBaseManager  *self,
              MMKernelDevice *port,
              gboolean        hotplugged,
              gboolean        manual_scan)
{
    MMDevice    *device;
    const gchar *name;
    const gchar *uid;

    g_return_if_fail (port != NULL);

    name = mm_kernel_device_get_name (port);

    mm_obj_dbg (self, "adding port %s at sysfs path: %s",
                name, mm_kernel_device_get_sysfs_path (port));

    /* Ignore devices that aren't completely configured by udev yet.  If
     * ModemManager is started in parallel with udev, explicitly requesting
     * devices may return devices for which not all udev rules have yet been
     * applied (a bug in udev/gudev).  Since we often need those rules to match
     * the device to a specific ModemManager driver, we need to ensure that all
     * rules have been processed before handling a device.
     *
     * This udev tag applies to each port in a device. In other words, the flag
     * may be set in some ports, but not in others */
    if (!mm_kernel_device_get_property_as_boolean (port, ID_MM_CANDIDATE)) {
        /* This could mean that device changed, losing its candidate
         * flags (such as Bluetooth RFCOMM devices upon disconnect.
         * Try to forget it. */
        device_removed (self, mm_kernel_device_get_subsystem (port), name);
        mm_obj_dbg (self, "port %s not candidate", name);
        return;
    }

    /* Get the port's physical device's uid. All ports of the same physical
     * device will share the same uid. */
    uid = mm_kernel_device_get_physdev_uid (port);
    g_assert (uid);

    /* If the device is inhibited, do nothing else */
    if (is_device_inhibited (self, uid)) {
        /* Note: we will not report as hotplugged an inhibited device port
         * because we don't know what was done with the port out of our
         * context. */
        device_inhibited_track_port (self, uid, port, manual_scan);
        return;
    }

    /* Run port filter */
    if (!mm_filter_port (self->priv->filter, port, manual_scan))
        return;

    /* If the port is already added, don't add it again */
    device = find_device_by_port (self, port);
    if (device) {
        existing_port (self, device, port);
        return;
    }

    /* See if we already created an object to handle ports in this device */
    device = find_device_by_physdev_uid (self, uid);
    if (device) {
        additional_port (self, device, port);
        return;
    }

    /* This is the first port in a new device */
    first_port (self, port, hotplugged);
}

#if defined WITH_QRTR

static void
handle_qrtr_device_added (MMBaseManager    *self,
                          guint             node_id,
                          MMQrtrBusWatcher *bus_watcher)
{
    g_autoptr(MMKernelDevice)  kernel_device = NULL;
    QrtrNode                  *node;

    node = mm_qrtr_bus_watcher_peek_node (bus_watcher, node_id);

    kernel_device = mm_kernel_device_qrtr_new (node);

    device_added (self, kernel_device, TRUE, FALSE);
}

static void
handle_qrtr_device_removed (MMBaseManager    *self,
                            guint             node_id)
{
    g_autofree gchar *qrtr_device_name = NULL;

    qrtr_device_name = mm_kernel_device_qrtr_helper_build_name (node_id);
    device_removed (self, MM_KERNEL_DEVICE_QRTR_SUBSYSTEM, qrtr_device_name);
}
#endif

static gboolean
handle_kernel_event (MMBaseManager            *self,
                     MMKernelEventProperties  *properties,
                     GError                  **error)
{
    const gchar *action;
    const gchar *subsystem;
    const gchar *name;
    const gchar *uid;

    action = mm_kernel_event_properties_get_action (properties);
    if (!action) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Missing mandatory parameter 'action'");
        return FALSE;
    }
    if (g_strcmp0 (action, "add") != 0 && g_strcmp0 (action, "remove") != 0) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Invalid 'action' parameter given: '%s' (expected 'add' or 'remove')", action);
        return FALSE;
    }

    subsystem = mm_kernel_event_properties_get_subsystem (properties);
    if (!subsystem) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Missing mandatory parameter 'subsystem'");
        return FALSE;
    }

    if (!g_strv_contains (mm_plugin_manager_get_subsystems (self->priv->plugin_manager), subsystem)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Invalid 'subsystem' parameter given: '%s'", subsystem);
        return FALSE;
    }

    name = mm_kernel_event_properties_get_name (properties);
    if (!name) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Missing mandatory parameter 'name'");
        return FALSE;
    }

    uid = mm_kernel_event_properties_get_uid (properties);

    mm_obj_dbg (self, "kernel event reported:");
    mm_obj_dbg (self, "  action:    %s", action);
    mm_obj_dbg (self, "  subsystem: %s", subsystem);
    mm_obj_dbg (self, "  name:      %s", name);
    mm_obj_dbg (self, "  uid:       %s", uid ? uid : "n/a");

    if (g_strcmp0 (action, "add") == 0) {
        g_autoptr(MMKernelDevice) kernel_device = NULL;
#if defined WITH_UDEV
        if (!mm_context_get_test_no_udev ())
            kernel_device = mm_kernel_device_udev_new_from_properties (self->priv->udev, properties, error);
        else
#endif
            kernel_device = mm_kernel_device_generic_new (properties, error);
        if (!kernel_device)
            return FALSE;

        device_added (self, kernel_device, TRUE, TRUE);
        return TRUE;
    }

    if (g_strcmp0 (action, "remove") == 0) {
        device_removed (self, subsystem, name);
        return TRUE;
    }

    g_assert_not_reached ();
}

#if defined WITH_UDEV

static void
handle_uevent (MMBaseManager *self,
               const gchar   *action,
               GUdevDevice   *device)
{
    const gchar *subsystem;
    const gchar *name;

    subsystem = g_udev_device_get_subsystem (device);
    name      = g_udev_device_get_name      (device);

    /* Valid udev devices must have subsystem and name set; if they don't have
     * both things, we silently ignore them. */
    if (!subsystem || !name)
        return;

    if (g_str_equal (action, "add") || g_str_equal (action, "move") || g_str_equal (action, "change")) {
        g_autoptr(MMKernelDevice) kernel_device = NULL;

        kernel_device = mm_kernel_device_udev_new (self->priv->udev, device);
        device_added (self, kernel_device, TRUE, FALSE);
        return;
    }

    if (g_str_equal (action, "remove")) {
        device_removed (self, subsystem, name);
        return;
    }
}

typedef struct {
    MMBaseManager *self;
    GUdevDevice *device;
    gboolean manual_scan;
} StartDeviceAdded;

static gboolean
start_device_added_idle (StartDeviceAdded *ctx)
{
    const gchar *subsystem;
    const gchar *name;

    subsystem = g_udev_device_get_subsystem (ctx->device);
    name      = g_udev_device_get_name      (ctx->device);

    /* Valid udev devices must have subsystem and name set; if they don't have
     * both things, we silently ignore them. */
    if (subsystem && name) {
        g_autoptr(MMKernelDevice) kernel_device = NULL;

        kernel_device = mm_kernel_device_udev_new (ctx->self->priv->udev, ctx->device);
        device_added (ctx->self, kernel_device, FALSE, ctx->manual_scan);
    }

    g_object_unref (ctx->self);
    g_object_unref (ctx->device);
    g_slice_free (StartDeviceAdded, ctx);
    return G_SOURCE_REMOVE;
}

static void
start_device_added (MMBaseManager *self,
                    GUdevDevice *device,
                    gboolean manual_scan)
{
    StartDeviceAdded *ctx;

    ctx = g_slice_new (StartDeviceAdded);
    ctx->self = g_object_ref (self);
    ctx->device = g_object_ref (device);
    ctx->manual_scan = manual_scan;
    g_idle_add ((GSourceFunc)start_device_added_idle, ctx);
}

static void
process_scan (MMBaseManager *self,
              gboolean       manual_scan)
{
    const gchar **subsystems;
    guint         i;

    subsystems = mm_plugin_manager_get_subsystems (self->priv->plugin_manager);
    for (i = 0; subsystems[i]; i++) {
        GList *devices;
        GList *iter;

        devices = g_udev_client_query_by_subsystem (self->priv->udev, subsystems[i]);
        for (iter = devices; iter; iter = g_list_next (iter))
            start_device_added (self, G_UDEV_DEVICE (iter->data), manual_scan);
        g_list_free_full (devices, g_object_unref);
    }
}

#endif

static void
process_initial_kernel_events (MMBaseManager *self)
{
    gchar *contents = NULL;
    gchar *line;
    GError *error = NULL;

    if (!self->priv->initial_kernel_events)
        return;

    if (!g_file_get_contents (self->priv->initial_kernel_events, &contents, NULL, &error)) {
        mm_obj_warn (self, "couldn't load initial kernel events: %s", error->message);
        g_error_free (error);
        return;
    }

    line = contents;
    while (line) {
        gchar *next;

        next = strchr (line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        /* ignore empty lines */
        if (line[0] != '\0') {
            MMKernelEventProperties *properties;

            properties = mm_kernel_event_properties_new_from_string (line, &error);
            if (!properties) {
                mm_obj_warn (self, "couldn't parse line '%s' as initial kernel event %s", line, error->message);
                g_clear_error (&error);
            } else if (!handle_kernel_event (self, properties, &error)) {
                mm_obj_warn (self, "couldn't process line '%s' as initial kernel event %s", line, error->message);
                g_clear_error (&error);
            } else
                mm_obj_dbg (self, "processed initial kernel event:' %s'", line);
            g_clear_object (&properties);
        }

        line = next;
    }

    g_free (contents);
}

void
mm_base_manager_start (MMBaseManager *self,
                       gboolean       manual_scan)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_BASE_MANAGER (self));

    if (!self->priv->auto_scan && !manual_scan) {
        /* If we have a list of initial kernel events, process it now */
        process_initial_kernel_events (self);
        return;
    }

#if defined WITH_UDEV
    if (!mm_context_get_test_no_udev ()) {
        mm_obj_dbg (self, "starting %s device scan...", manual_scan ? "manual" : "automatic");
        process_scan (self, manual_scan);
        mm_obj_dbg (self, "finished device scan...");
    } else
#endif
        mm_obj_dbg (self, "unsupported %s device scan...", manual_scan ? "manual" : "automatic");
}

/*****************************************************************************/

typedef enum {
    DEVICE_CLEANUP_STEP_FIRST = 0,
    DEVICE_CLEANUP_STEP_DISABLE,
    DEVICE_CLEANUP_STEP_LOW_POWER,
    DEVICE_CLEANUP_STEP_REMOVE,
    DEVICE_CLEANUP_STEP_TERSE,
    DEVICE_CLEANUP_STEP_LAST,
} DeviceCleanupStep;

typedef struct {
    MMBaseModem               *modem;
    MMBaseManagerCleanupFlags  flags;
    DeviceCleanupStep          step;
} DeviceCleanupContext;

static void device_cleanup_step (GTask *task);

static DeviceCleanupContext *
device_cleanup_context_new (MMBaseModem *modem,
                            MMBaseManagerCleanupFlags  flags)
{
    DeviceCleanupContext *ctx;

    ctx = g_slice_new0 (DeviceCleanupContext);
    if (modem)
        ctx->modem = g_object_ref (modem);
    ctx->step = DEVICE_CLEANUP_STEP_FIRST;
    ctx->flags = flags;

    return ctx;
}

static void
device_cleanup_context_free (DeviceCleanupContext *ctx)
{
    g_clear_object (&ctx->modem);
    g_slice_free (DeviceCleanupContext, ctx);
}

static void
modem_remove_ready (MMDevice     *device,
                    GAsyncResult *res,
                    GTask        *task)
{
    DeviceCleanupContext *ctx;
    g_autoptr(GError)      error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_device_remove_modem_finish (device, res, &error))
        mm_obj_warn (device, "removing modem failed: %s", error->message);

    ctx->step++;
    device_cleanup_step (task);
}

static void
cleanup_low_power_ready (MMIfaceModem *modem,
                         GAsyncResult *res,
                         GTask        *task)
{
    DeviceCleanupContext *ctx;
    g_autoptr(GError)      error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_iface_modem_set_power_state_finish (modem, res, NULL, &error))
        mm_obj_info (modem, "changing to low power state failed: %s", error->message);

    ctx->step++;
    device_cleanup_step (task);
}

static void
cleanup_disable_ready (MMBaseModem  *modem,
                       GAsyncResult *res,
                       GTask        *task)
{
    DeviceCleanupContext *ctx;
    g_autoptr(GError)      error = NULL;

    ctx = g_task_get_task_data (task);

    if (mm_base_modem_disable_finish (modem, res, &error)) {
        ctx->step++;
    } else {
        mm_obj_info (modem, "disabling modem failed: %s", error->message);

        /* If disabling failed there's no point in trying to low-power the
         * modem; just remove it.
         */
        ctx->step = DEVICE_CLEANUP_STEP_REMOVE;
    }

    device_cleanup_step (task);
}

#if defined WITH_SUSPEND_RESUME
static void
cleanup_terse_ready (MMBaseModem  *modem,
                     GAsyncResult *res,
                     GTask        *task)
{
    DeviceCleanupContext *ctx;
    g_autoptr(GError)     error = NULL;

    ctx = g_task_get_task_data (task);
    if (!mm_base_modem_terse_finish (modem, res, &error))
        mm_obj_info (modem, "setting terse failed: %s", error->message);

    ctx->step++;

    device_cleanup_step (task);
}
#endif

static gboolean
has_cleanup_flag (MMBaseManagerCleanupFlags flags,
                  MMBaseManagerCleanupFlags expected)
{
    return flags & expected;
}

static void
device_cleanup_step (GTask *task)
{
    DeviceCleanupContext *ctx;
    MMDevice              *device;

    ctx = g_task_get_task_data (task);
    device = g_task_get_source_object (task);

    switch (ctx->step) {
    case DEVICE_CLEANUP_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DEVICE_CLEANUP_STEP_DISABLE:
        if (ctx->modem && has_cleanup_flag (ctx->flags, MM_BASE_MANAGER_CLEANUP_DISABLE)) {
            mm_obj_dbg (device,
                        "disabling modem%d",
                        mm_base_modem_get_dbus_id (ctx->modem));
            mm_base_modem_disable (ctx->modem,
                                   MM_OPERATION_LOCK_REQUIRED,
                                   MM_OPERATION_PRIORITY_OVERRIDE,
                                   (GAsyncReadyCallback)cleanup_disable_ready,
                                   task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DEVICE_CLEANUP_STEP_LOW_POWER:
        if (ctx->modem && has_cleanup_flag (ctx->flags, MM_BASE_MANAGER_CLEANUP_LOW_POWER)) {
            mm_obj_dbg (device,
                        "setting modem%d to low-power state",
                        mm_base_modem_get_dbus_id (ctx->modem));
            mm_iface_modem_set_power_state (MM_IFACE_MODEM (ctx->modem),
                                            MM_MODEM_POWER_STATE_LOW,
                                            (GAsyncReadyCallback)cleanup_low_power_ready,
                                            task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DEVICE_CLEANUP_STEP_REMOVE:
        if (ctx->modem && has_cleanup_flag (ctx->flags, MM_BASE_MANAGER_CLEANUP_REMOVE)) {
            mm_obj_dbg (device,
                        "removing modem%d",
                        mm_base_modem_get_dbus_id (ctx->modem));
            mm_device_remove_modem (device,
                                    (GAsyncReadyCallback)modem_remove_ready,
                                    task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DEVICE_CLEANUP_STEP_TERSE:

#if defined WITH_SUSPEND_RESUME
        if (ctx->modem && has_cleanup_flag (ctx->flags, MM_BASE_MANAGER_CLEANUP_TERSE)) {
            mm_obj_dbg(device,
                    "settting terse on modem%d",
                    mm_base_modem_get_dbus_id (ctx->modem));
            mm_base_modem_terse (ctx->modem,
                                 MM_OPERATION_LOCK_REQUIRED,
                                 (GAsyncReadyCallback)cleanup_terse_ready,
                                 task);
            return;
        }
#endif

        ctx->step++;
        /* fall through */

    case DEVICE_CLEANUP_STEP_LAST:
        mm_obj_dbg (device, "device cleanup complete");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

typedef struct {
    MMBaseManager  *self;
    GSList         *tasks;
    GSList         *devices_to_remove;
    MMSleepContext *sleep_ctx;
} ManagerCleanupContext;

static ManagerCleanupContext *
manager_cleanup_context_new (MMBaseManager  *self,
                             MMSleepContext *sleep_ctx)
{
    ManagerCleanupContext *ctx;

    ctx = g_slice_new0 (ManagerCleanupContext);
    ctx->self = g_object_ref (self);
    ctx->sleep_ctx = g_object_ref (sleep_ctx);
    return ctx;
}

static void
manager_cleanup_context_maybe_complete (ManagerCleanupContext *ctx,
                                        GTask                  *task,
                                        MMDevice               *device,
                                        gboolean                remove)
{
    if (task)
        ctx->tasks = g_slist_remove (ctx->tasks, task);

    if (remove) {
        g_assert (device);
        ctx->devices_to_remove = g_slist_append (ctx->devices_to_remove, device);
    }

    if (ctx->tasks == NULL) {
        GSList *iter;

        /* Remove devices at the end to avoid modifying the hash table
         * during iteration, in case a device has no modem and the steps
         * all fall through.
         */
        for (iter = ctx->devices_to_remove; iter; iter = iter->next)
            g_hash_table_remove (ctx->self->priv->devices, mm_device_get_uid (device));
        g_slist_free (ctx->devices_to_remove);

        mm_obj_dbg (ctx->self, "cleanup complete");
        mm_sleep_context_complete (ctx->sleep_ctx, NULL);

        g_object_unref (ctx->sleep_ctx);
        g_object_unref (ctx->self);
        g_slice_free (ManagerCleanupContext, ctx);
    }
}

static void
device_cleanup_ready (MMDevice               *device,
                      GAsyncResult           *res,
                      ManagerCleanupContext *ctx)
{
    GTask                 *task;
    DeviceCleanupContext *dctx;
    g_autoptr(GError)      error = NULL;
    gboolean               remove;

    task = G_TASK (res);

    if (!g_task_propagate_boolean (task, &error)) {
        mm_obj_info (ctx->self,
                     "failed to cleanup device %s: %s",
                     mm_device_get_uid (device),
                     error->message);
    }

    dctx = g_task_get_task_data (task);
    remove = has_cleanup_flag (dctx->flags, MM_BASE_MANAGER_CLEANUP_REMOVE);
    manager_cleanup_context_maybe_complete (ctx, task, device, remove);
}

void
mm_base_manager_cleanup (MMBaseManager             *self,
                         MMBaseManagerCleanupFlags  flags,
                         MMSleepContext            *sleep_ctx)
{
    ManagerCleanupContext *ctx;
    GHashTableIter          iter;
    MMDevice               *device;
    gboolean                disable, remove, low_power, terse;
    guint                   active_bearers = 0;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_BASE_MANAGER (self));

    /* Cancel all ongoing auth requests */
    g_cancellable_cancel (self->priv->authp_cancellable);

    disable = has_cleanup_flag (flags, MM_BASE_MANAGER_CLEANUP_DISABLE);
    low_power = has_cleanup_flag (flags, MM_BASE_MANAGER_CLEANUP_LOW_POWER);
    remove = has_cleanup_flag (flags, MM_BASE_MANAGER_CLEANUP_REMOVE);
    terse = has_cleanup_flag (flags, MM_BASE_MANAGER_CLEANUP_TERSE);
    mm_obj_dbg (self,
                "manager shutting down... (%s%s%s%s%s%s%s)",
                disable ? "disable" : "",
                disable && (low_power || remove || terse) ? "," : "",
                low_power ? "low-power" : "",
                low_power && (remove || terse) ? "," : "",
                remove ? "remove" : "",
                (remove && terse) ? "," : "",
                terse ? "terse" : "");

    ctx = manager_cleanup_context_new (self, sleep_ctx);

    g_hash_table_iter_init (&iter, self->priv->devices);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer) &device)) {
        DeviceCleanupContext *dctx;
        GTask                *task;
        MMBaseModem          *modem;

        modem = mm_device_peek_modem (device);
        if (modem) {
            guint count = 0;

            mm_iface_modem_count_bearers (MM_IFACE_MODEM (modem),
                                          MM_IFACE_MODEM_COUNT_BEARERS_FLAG_ACTIVE,
                                          &count,
                                          NULL,
                                          NULL);
            active_bearers += count;
        }

        task = g_task_new (device,
                           NULL,
                           (GAsyncReadyCallback)device_cleanup_ready,
                           ctx);
        ctx->tasks = g_slist_append (ctx->tasks, task);

        dctx = device_cleanup_context_new (modem, flags);
        g_task_set_task_data (task, dctx, (GDestroyNotify)device_cleanup_context_free);

        device_cleanup_step (task);
    }

    /* Increase cleanup timeout for each connected bearer; disconnecting a
     * bearer can take many seconds.
     */
    if (active_bearers)
        mm_sleep_context_timeout_backoff (sleep_ctx, active_bearers * 20);

    /* Complete cleanup if there were no devices */
    manager_cleanup_context_maybe_complete (ctx, NULL, NULL, FALSE);
}

guint32
mm_base_manager_num_modems (MMBaseManager *self)
{
    GHashTableIter iter;
    gpointer key, value;
    guint32 n;

    g_return_val_if_fail (self != NULL, 0);
    g_return_val_if_fail (MM_IS_BASE_MANAGER (self), 0);

    n = 0;
    g_hash_table_iter_init (&iter, self->priv->devices);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        n += !!mm_device_peek_modem (MM_DEVICE (value));
    }

    return n;
}

/*****************************************************************************/
/* Quick resume synchronization */

#if defined WITH_SUSPEND_RESUME

static void
base_modem_sync_ready (MMBaseModem  *self,
                       GAsyncResult *res,
                       gpointer      user_data)
{
    g_autoptr(GError) error = NULL;

    mm_base_modem_sync_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "synchronization failed: %s", error->message);
        return;
    }
    mm_obj_msg (self, "synchronization finished");
}

void
mm_base_manager_sync (MMBaseManager *self)
{
    GHashTableIter iter;
    gpointer       key, value;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_BASE_MANAGER (self));

    /* Refresh each device */
    g_hash_table_iter_init (&iter, self->priv->devices);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMBaseModem *modem;

        modem = mm_device_peek_modem (MM_DEVICE (value));

        /* We just want to start the synchronization, we don't need the result */
        if (modem) {
            mm_base_modem_sync (modem,
                                MM_OPERATION_LOCK_REQUIRED,
                                (GAsyncReadyCallback)base_modem_sync_ready,
                                NULL);
        }
    }
}

#endif

/*****************************************************************************/
/* Set logging */

typedef struct {
    MMBaseManager *self;
    GDBusMethodInvocation *invocation;
    gchar *level;
} SetLoggingContext;

static void
set_logging_context_free (SetLoggingContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->level);
    g_free (ctx);
}

static void
set_logging_auth_ready (MMAuthProvider    *authp,
                        GAsyncResult      *res,
                        SetLoggingContext *ctx)
{
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error))
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    else if (!mm_log_set_level (ctx->level, &error))
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_obj_msg (ctx->self, "logging: level '%s'", ctx->level);
        mm_gdbus_org_freedesktop_modem_manager1_complete_set_logging (
            MM_GDBUS_ORG_FREEDESKTOP_MODEM_MANAGER1 (ctx->self),
            ctx->invocation);
    }

    set_logging_context_free (ctx);
}

static gboolean
handle_set_logging (MmGdbusOrgFreedesktopModemManager1 *manager,
                    GDBusMethodInvocation *invocation,
                    const gchar *level)
{
    SetLoggingContext *ctx;

    ctx = g_new0 (SetLoggingContext, 1);
    ctx->self = MM_BASE_MANAGER (g_object_ref (manager));
    ctx->invocation = g_object_ref (invocation);
    ctx->level = g_strdup (level);

    mm_auth_provider_authorize (ctx->self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_MANAGER_CONTROL,
                                ctx->self->priv->authp_cancellable,
                                (GAsyncReadyCallback)set_logging_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Manual scan */

typedef struct {
    MMBaseManager *self;
    GDBusMethodInvocation *invocation;
} ScanDevicesContext;

static void
scan_devices_context_free (ScanDevicesContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
scan_devices_auth_ready (MMAuthProvider *authp,
                         GAsyncResult *res,
                         ScanDevicesContext *ctx)
{
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error))
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
#if defined WITH_UDEV
        if (!mm_context_get_test_no_udev ()) {
            mm_obj_info (ctx->self, "processing user request to launch device scan");
            mm_base_manager_start (MM_BASE_MANAGER (ctx->self), TRUE);
            mm_gdbus_org_freedesktop_modem_manager1_complete_scan_devices (
                MM_GDBUS_ORG_FREEDESKTOP_MODEM_MANAGER1 (ctx->self),
                ctx->invocation);
        } else
#endif
            mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                            "Cannot request manual scan of devices: unsupported");
    }

    scan_devices_context_free (ctx);
}

static gboolean
handle_scan_devices (MmGdbusOrgFreedesktopModemManager1 *manager,
                     GDBusMethodInvocation *invocation)
{
    ScanDevicesContext *ctx;

    ctx = g_new (ScanDevicesContext, 1);
    ctx->self = MM_BASE_MANAGER (g_object_ref (manager));
    ctx->invocation = g_object_ref (invocation);

    mm_auth_provider_authorize (ctx->self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_MANAGER_CONTROL,
                                ctx->self->priv->authp_cancellable,
                                (GAsyncReadyCallback)scan_devices_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MMBaseManager *self;
    GDBusMethodInvocation *invocation;
    GVariant *dictionary;
} ReportKernelEventContext;

static void
report_kernel_event_context_free (ReportKernelEventContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_variant_unref (ctx->dictionary);
    g_slice_free (ReportKernelEventContext, ctx);
}

static void
report_kernel_event_auth_ready (MMAuthProvider           *authp,
                                GAsyncResult             *res,
                                ReportKernelEventContext *ctx)
{
    GError                  *error = NULL;
    MMKernelEventProperties *properties = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error))
        goto out;

#if defined WITH_UDEV
    if (!mm_context_get_test_no_udev () && ctx->self->priv->auto_scan) {
        error = g_error_new_literal (MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                     "Cannot report kernel event: "
                                     "udev monitoring already in place");
        goto out;
    }
#endif

    properties = mm_kernel_event_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!properties)
        goto out;

    handle_kernel_event (ctx->self, properties, &error);

out:
    if (error) {
        mm_obj_warn (ctx->self, "couldn't handle kernel event: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    } else
        mm_gdbus_org_freedesktop_modem_manager1_complete_report_kernel_event (
            MM_GDBUS_ORG_FREEDESKTOP_MODEM_MANAGER1 (ctx->self),
            ctx->invocation);

    if (properties)
        g_object_unref (properties);
    report_kernel_event_context_free (ctx);
}

static gboolean
handle_report_kernel_event (MmGdbusOrgFreedesktopModemManager1 *manager,
                            GDBusMethodInvocation *invocation,
                            GVariant *dictionary)
{
    ReportKernelEventContext *ctx;

    ctx = g_slice_new0 (ReportKernelEventContext);
    ctx->self = MM_BASE_MANAGER (g_object_ref (manager));
    ctx->invocation = g_object_ref (invocation);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_auth_provider_authorize (ctx->self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_MANAGER_CONTROL,
                                ctx->self->priv->authp_cancellable,
                                (GAsyncReadyCallback)report_kernel_event_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Inhibit or uninhibit device */

typedef struct {
    MMKernelDevice *kernel_port;
    gboolean        manual_scan;
} InhibitedDevicePortInfo;

static void
inhibited_device_port_info_free (InhibitedDevicePortInfo *port_info)
{
    g_object_unref (port_info->kernel_port);
    g_slice_free (InhibitedDevicePortInfo, port_info);
}

typedef struct {
    gchar           *sender;
    guint            name_lost_id;
    GList           *port_infos;
} InhibitedDeviceInfo;

static void
inhibited_device_info_free (InhibitedDeviceInfo *info)
{
    g_list_free_full (info->port_infos, (GDestroyNotify)inhibited_device_port_info_free);
    g_bus_unwatch_name (info->name_lost_id);
    g_free (info->sender);
    g_slice_free (InhibitedDeviceInfo, info);
}

static InhibitedDeviceInfo *
find_inhibited_device_info_by_physdev_uid (MMBaseManager *self,
                                           const gchar   *physdev_uid)
{
    return (physdev_uid ? g_hash_table_lookup (self->priv->inhibited_devices, physdev_uid) : NULL);
}

static gboolean
is_device_inhibited (MMBaseManager *self,
                     const gchar   *physdev_uid)
{
    return !!find_inhibited_device_info_by_physdev_uid (self, physdev_uid);
}

static void
device_inhibited_untrack_port (MMBaseManager  *self,
                               const gchar    *subsystem,
                               const gchar    *name)
{
    GHashTableIter       iter;
    gchar               *uid;
    InhibitedDeviceInfo *info;

    g_hash_table_iter_init (&iter, self->priv->inhibited_devices);
    while (g_hash_table_iter_next (&iter, (gpointer)&uid, (gpointer)&info)) {
        GList *l;

        for (l = info->port_infos; l; l = g_list_next (l)) {
            InhibitedDevicePortInfo *port_info;

            port_info = (InhibitedDevicePortInfo *)(l->data);

            if ((g_strcmp0 (subsystem, mm_kernel_device_get_subsystem (port_info->kernel_port)) == 0) &&
                (g_strcmp0 (name, mm_kernel_device_get_name (port_info->kernel_port)) == 0)) {
                mm_obj_dbg (self, "released port %s while inhibited", name);
                inhibited_device_port_info_free (port_info);
                info->port_infos = g_list_delete_link (info->port_infos, l);
                return;
            }
        }
    }
}

static void
device_inhibited_track_port (MMBaseManager  *self,
                             const gchar    *physdev_uid,
                             MMKernelDevice *kernel_port,
                             gboolean        manual_scan)
{
    InhibitedDevicePortInfo *port_info;
    InhibitedDeviceInfo     *info;
    GList                   *l;

    info = find_inhibited_device_info_by_physdev_uid (self, physdev_uid);
    g_assert (info);

    for (l = info->port_infos; l; l = g_list_next (l)) {
        /* If device is already tracked, just overwrite the manual scan info */
        port_info = (InhibitedDevicePortInfo *)(l->data);
        if (mm_kernel_device_cmp (port_info->kernel_port, kernel_port)) {
            port_info->manual_scan = manual_scan;
            return;
        }
    }

    mm_obj_dbg (self, "added port %s while inhibited", mm_kernel_device_get_name (kernel_port));

    port_info = g_slice_new0 (InhibitedDevicePortInfo);
    port_info->kernel_port = g_object_ref (kernel_port);
    port_info->manual_scan = manual_scan;
    info->port_infos = g_list_append (info->port_infos, port_info);
}

typedef struct {
    MMBaseManager *self;
    gchar         *uid;
} InhibitSenderLostContext;

static void
inhibit_sender_lost_context_free (InhibitSenderLostContext *lost_ctx)
{
    g_free (lost_ctx->uid);
    g_slice_free (InhibitSenderLostContext, lost_ctx);
}

static void
remove_device_inhibition (MMBaseManager *self,
                          const gchar   *uid)
{
    InhibitedDeviceInfo *info;
    MMDevice            *device;
    GList               *port_infos;

    info = find_inhibited_device_info_by_physdev_uid (self, uid);
    g_assert (info);

    device = find_device_by_physdev_uid (self, uid);
    port_infos = info->port_infos;
    info->port_infos = NULL;
    g_hash_table_remove (self->priv->inhibited_devices, uid);

    /* If any port info exists, we require explicit port probing that will be
     * triggered via the artificial port notifications emitted with the
     * device_added() calls */
    if (port_infos) {
        GList *l;

        /* A device may exist at this point if e.g. not all ports were
         * removed during the inhibition (i.e. the MMDevice was never fully
         * removed) and new ports were then added while inhibited. In this
         * case, we must fake all ports going away so that the MMDevice gets
         * completely removed, otherwise the plugin manager won't start a new
         * device probing task and therefore no port probing tasks. */
        if (device) {
            GList *readded_port_infos = NULL;
            GList *leftover_ports;

            /* Create a new list of inhibited device port infos from the existing
             * port probes */
            leftover_ports = mm_device_peek_port_probe_list (device);
            for (l = leftover_ports; l; l = g_list_next (l)) {
                InhibitedDevicePortInfo *port_info;
                MMPortProbe             *port_probe;

                port_probe = MM_PORT_PROBE (l->data);

                port_info = g_slice_new0 (InhibitedDevicePortInfo);
                port_info->kernel_port = mm_port_probe_get_port (port_probe);
                port_info->manual_scan = TRUE;
                readded_port_infos = g_list_append (readded_port_infos, port_info);
            }

            /* Now, explicitly request to remove all ports, the device should go
             * away as well while doing so. */
            for (l = readded_port_infos; l; l = g_list_next (l)) {
                InhibitedDevicePortInfo *port_info;

                port_info = (InhibitedDevicePortInfo *)(l->data);
                mm_obj_msg (self, "fake releasing port %s/%s during uninhibition...",
                            mm_kernel_device_get_subsystem (port_info->kernel_port),
                            mm_kernel_device_get_name (port_info->kernel_port));
                device_removed (self,
                                mm_kernel_device_get_subsystem (port_info->kernel_port),
                                mm_kernel_device_get_name (port_info->kernel_port));
            }

            /* At this point, the device should have gone completely */
            g_assert (!find_device_by_physdev_uid (self, uid));

            /* Added the ports to re-add in the pending list */
            port_infos = g_list_concat (port_infos, readded_port_infos);
        }

        /* Report as added all port infos that we had tracked while the
         * device was inhibited. We can only report the added port after
         * having removed the entry from the inhibited devices tracking
         * table. */
        for (l = port_infos; l; l = g_list_next (l)) {
            InhibitedDevicePortInfo *port_info;

            port_info = (InhibitedDevicePortInfo *)(l->data);
            device_added (self, port_info->kernel_port, FALSE, port_info->manual_scan);
        }
        g_list_free_full (port_infos, (GDestroyNotify)inhibited_device_port_info_free);
        return;
    }

    /* The device may be totally gone from the system while we were
     * keeping the inhibition, so do not error out if not found. */
    if (device) {
        GError *error = NULL;

        /* Uninhibit device, which will create and expose the modem object */
        if (!mm_device_uninhibit (device, &error)) {
            mm_obj_warn (self, "couldn't uninhibit device: %s", error->message);
            g_error_free (error);
        }
    }
}

static void
inhibit_sender_lost (GDBusConnection          *connection,
                     const gchar              *sender_name,
                     InhibitSenderLostContext *lost_ctx)
{
    mm_obj_msg (lost_ctx->self, "device inhibition teardown for uid '%s' (owner disappeared from bus)", lost_ctx->uid);
    remove_device_inhibition (lost_ctx->self, lost_ctx->uid);
}

typedef struct {
    MMBaseManager         *self;
    GDBusMethodInvocation *invocation;
    gchar                 *uid;
    gboolean               inhibit;
} InhibitDeviceContext;

static void
inhibit_device_context_free (InhibitDeviceContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->uid);
    g_slice_free (InhibitDeviceContext, ctx);
}

static void
device_inhibit_ready (MMDevice             *device,
                      GAsyncResult         *res,
                      InhibitDeviceContext *ctx)
{
    InhibitSenderLostContext *lost_ctx;
    InhibitedDeviceInfo      *info;
    GError                   *error = NULL;

    if (!mm_device_inhibit_finish (device, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        inhibit_device_context_free (ctx);
        return;
    }

    info = g_slice_new0 (InhibitedDeviceInfo);
    info->sender = g_strdup (g_dbus_method_invocation_get_sender (ctx->invocation));

    /* This context will exist as long as the sender name watcher exists,
     * i.e. as long as the associated InhibitDeviceInfo exists. We don't need
     * an extra reference of self here because these contexts are stored within
     * self, and therefore bound to its lifetime. */
    lost_ctx = g_slice_new0 (InhibitSenderLostContext);
    lost_ctx->self = ctx->self;
    lost_ctx->uid = g_strdup (ctx->uid);
    info->name_lost_id = g_bus_watch_name_on_connection (g_dbus_method_invocation_get_connection (ctx->invocation),
                                                         info->sender,
                                                         G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                         NULL,
                                                         (GBusNameVanishedCallback)inhibit_sender_lost,
                                                         lost_ctx,
                                                         (GDestroyNotify)inhibit_sender_lost_context_free);

    g_hash_table_insert (ctx->self->priv->inhibited_devices, g_strdup (ctx->uid), info);

    mm_obj_msg (ctx->self, "device inhibition setup for uid '%s'", ctx->uid);

    mm_gdbus_org_freedesktop_modem_manager1_complete_inhibit_device (
        MM_GDBUS_ORG_FREEDESKTOP_MODEM_MANAGER1 (ctx->self),
        ctx->invocation);
    inhibit_device_context_free (ctx);
}

static void
base_manager_inhibit_device (InhibitDeviceContext *ctx)
{
    MMDevice *device;

    device = find_device_by_physdev_uid (ctx->self, ctx->uid);
    if (!device) {
        mm_dbus_method_invocation_return_error (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                                "No device found with uid '%s'", ctx->uid);
        inhibit_device_context_free (ctx);
        return;
    }

    if (mm_device_get_inhibited (device)) {
        mm_dbus_method_invocation_return_error (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS,
                                                "Device '%s' is already inhibited", ctx->uid);
        inhibit_device_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to inhibit uid '%s'", ctx->uid);
    mm_device_inhibit (device,
                       (GAsyncReadyCallback) device_inhibit_ready,
                       ctx);
}

static void
base_manager_uninhibit_device (InhibitDeviceContext *ctx)
{
    InhibitedDeviceInfo *info;
    const gchar         *sender;

    /* Validate uninhibit request */
    sender = g_dbus_method_invocation_get_sender (ctx->invocation);
    info = find_inhibited_device_info_by_physdev_uid (ctx->self, ctx->uid);
    if (!info || (g_strcmp0 (info->sender, sender) != 0)) {
        mm_dbus_method_invocation_return_error (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                                                "No inhibition found for uid '%s'", ctx->uid);
        inhibit_device_context_free (ctx);
        return;
    }

    mm_obj_info (ctx->self, "processing user request to uninhibit uid '%s'", ctx->uid);
    remove_device_inhibition (ctx->self, ctx->uid);
    mm_obj_msg (ctx->self, "device inhibition teardown for uid '%s'", ctx->uid);

    mm_gdbus_org_freedesktop_modem_manager1_complete_inhibit_device (
        MM_GDBUS_ORG_FREEDESKTOP_MODEM_MANAGER1 (ctx->self),
        ctx->invocation);
    inhibit_device_context_free (ctx);
}

static void
inhibit_device_auth_ready (MMAuthProvider       *authp,
                           GAsyncResult         *res,
                           InhibitDeviceContext *ctx)
{
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        inhibit_device_context_free (ctx);
        return;
    }

    if (ctx->inhibit)
        base_manager_inhibit_device (ctx);
    else
        base_manager_uninhibit_device (ctx);
}

static gboolean
handle_inhibit_device (MmGdbusOrgFreedesktopModemManager1 *manager,
                       GDBusMethodInvocation              *invocation,
                       const gchar                        *uid,
                       gboolean                            inhibit)
{
    InhibitDeviceContext *ctx;

    ctx = g_slice_new0 (InhibitDeviceContext);
    ctx->self = MM_BASE_MANAGER (g_object_ref (manager));
    ctx->invocation = g_object_ref (invocation);
    ctx->uid = g_strdup (uid);
    ctx->inhibit = inhibit;

    mm_auth_provider_authorize (ctx->self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_MANAGER_CONTROL,
                                ctx->self->priv->authp_cancellable,
                                (GAsyncReadyCallback)inhibit_device_auth_ready,
                                ctx);
    return TRUE;
}

/*****************************************************************************/
/* Test profile setup */

#if defined WITH_TESTS

static gboolean
handle_set_profile (MmGdbusTest *skeleton,
                    GDBusMethodInvocation *invocation,
                    const gchar *id,
                    const gchar *plugin_name,
                    const gchar *const *ports,
                    MMBaseManager *self)
{
    MMPlugin *plugin;
    MMDevice *device;
    gchar *physdev_uid;
    gchar *physdev = NULL;
    GError *error = NULL;

    mm_obj_msg (self, "test profile set to: '%s'", id);

    /* Create device and keep it listed in the Manager */
    physdev_uid = g_strdup_printf ("/virtual/%s", id);
    device = mm_device_new (physdev_uid, physdev, TRUE, TRUE, self->priv->object_manager);
    g_hash_table_insert (self->priv->devices, physdev_uid, device);

    /* Grab virtual ports */
    mm_device_virtual_grab_ports (device, (const gchar **)ports);

    /* Set plugin to use */
    plugin = mm_plugin_manager_peek_plugin (self->priv->plugin_manager, plugin_name);
    if (!plugin) {
        error = g_error_new (MM_CORE_ERROR,
                             MM_CORE_ERROR_NOT_FOUND,
                             "Requested plugin '%s' not found",
                             plugin_name);
        mm_obj_warn (self, "couldn't set plugin for virtual device '%s': %s",
                     mm_device_get_uid (device),
                     error->message);
        goto out;
    }
    mm_device_set_plugin (device, G_OBJECT (plugin));

    /* Create modem */
    if (!mm_device_create_modem (device, &error)) {
        mm_obj_warn (self, "couldn't create modem for virtual device '%s': %s",
                     mm_device_get_uid (device),
                     error->message);
        goto out;
    }

    mm_obj_msg (self, "modem for virtual device '%s' successfully created",
                mm_device_get_uid (device));

    /* launch async modem initialization */
    mm_device_initialize_modem (device);

out:

    if (error) {
        mm_device_remove_modem_quick (device);
        g_hash_table_remove (self->priv->devices, mm_device_get_uid (device));
        mm_dbus_method_invocation_return_gerror (invocation, error);
        g_error_free (error);
    } else
        mm_gdbus_test_complete_set_profile (skeleton, invocation);

    return TRUE;
}

#endif

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    return g_strdup ("base-manager");
}

/*****************************************************************************/

MMBaseManager *
mm_base_manager_new (GDBusConnection  *connection,
#if !defined WITH_BUILTIN_PLUGINS
                     const gchar      *plugin_dir,
#endif
                     gboolean          auto_scan,
                     MMFilterRule      filter_policy,
                     const gchar      *initial_kernel_events,
#if defined WITH_TESTS
                     gboolean          enable_test,
#endif
                     GError          **error)
{
    g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);

    return g_initable_new (MM_TYPE_BASE_MANAGER,
                           NULL, /* cancellable */
                           error,
                           MM_BASE_MANAGER_CONNECTION,            connection,
#if !defined WITH_BUILTIN_PLUGINS
                           MM_BASE_MANAGER_PLUGIN_DIR,            plugin_dir,
#endif
                           MM_BASE_MANAGER_AUTO_SCAN,             auto_scan,
                           MM_BASE_MANAGER_FILTER_POLICY,         filter_policy,
                           MM_BASE_MANAGER_INITIAL_KERNEL_EVENTS, initial_kernel_events,
                           "version",                             MM_DIST_VERSION,
#if defined WITH_TESTS
                           MM_BASE_MANAGER_ENABLE_TEST,           enable_test,
#endif
                           NULL);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MMBaseManager *self = MM_BASE_MANAGER (object);

    switch (prop_id) {
    case PROP_CONNECTION: {
        gboolean had_connection = FALSE;

        if (self->priv->connection) {
            had_connection = TRUE;
            g_object_unref (self->priv->connection);
        }
        self->priv->connection = g_value_dup_object (value);
        /* Propagate connection loss to subobjects */
        if (had_connection && !self->priv->connection) {
            if (self->priv->object_manager) {
                mm_obj_dbg (self, "stopping connection in object manager server");
                g_dbus_object_manager_server_set_connection (self->priv->object_manager, NULL);
            }
#if defined WITH_TESTS
            if (self->priv->test_skeleton &&
                g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (self->priv->test_skeleton))) {
                mm_obj_dbg (self, "stopping connection in test skeleton");
                g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->priv->test_skeleton));
            }
#endif
        }
        break;
    }
    case PROP_AUTO_SCAN:
        self->priv->auto_scan = g_value_get_boolean (value);
        break;
    case PROP_FILTER_POLICY:
        self->priv->filter_policy = g_value_get_flags (value);
        break;
#if !defined WITH_BUILTIN_PLUGINS
    case PROP_PLUGIN_DIR:
        g_free (self->priv->plugin_dir);
        self->priv->plugin_dir = g_value_dup_string (value);
        break;
#endif
    case PROP_INITIAL_KERNEL_EVENTS:
        g_free (self->priv->initial_kernel_events);
        self->priv->initial_kernel_events = g_value_dup_string (value);
        break;
#if defined WITH_TESTS
    case PROP_ENABLE_TEST:
        self->priv->enable_test = g_value_get_boolean (value);
        break;
#endif
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MMBaseManager *self = MM_BASE_MANAGER (object);

    switch (prop_id) {
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_AUTO_SCAN:
        g_value_set_boolean (value, self->priv->auto_scan);
        break;
    case PROP_FILTER_POLICY:
        g_value_set_flags (value, self->priv->filter_policy);
        break;
#if !defined WITH_BUILTIN_PLUGINS
    case PROP_PLUGIN_DIR:
        g_value_set_string (value, self->priv->plugin_dir);
        break;
#endif
    case PROP_INITIAL_KERNEL_EVENTS:
        g_value_set_string (value, self->priv->initial_kernel_events);
        break;
#if defined WITH_TESTS
    case PROP_ENABLE_TEST:
        g_value_set_boolean (value, self->priv->enable_test);
        break;
#endif
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_base_manager_init (MMBaseManager *self)
{
    /* Setup private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BASE_MANAGER, MMBaseManagerPrivate);

    /* Setup authorization provider */
    self->priv->authp = mm_auth_provider_get ();
    self->priv->authp_cancellable = g_cancellable_new ();

    /* Setup internal lists of device objects */
    self->priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    /* Setup internal list of inhibited devices */
    self->priv->inhibited_devices = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)inhibited_device_info_free);

    /* By default, enable autoscan */
    self->priv->auto_scan = TRUE;

#if defined WITH_TESTS
    /* By default, no test interface */
    self->priv->enable_test = FALSE;
#endif

    /* Setup Object Manager Server */
    self->priv->object_manager = g_dbus_object_manager_server_new (MM_DBUS_PATH);

    /* Enable processing of input DBus messages */
    g_object_connect (self,
                      "signal::handle-set-logging",         G_CALLBACK (handle_set_logging),         NULL,
                      "signal::handle-scan-devices",        G_CALLBACK (handle_scan_devices),        NULL,
                      "signal::handle-report-kernel-event", G_CALLBACK (handle_report_kernel_event), NULL,
                      "signal::handle-inhibit-device",      G_CALLBACK (handle_inhibit_device),      NULL,
                      NULL);
}

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
    MMBaseManager *self = MM_BASE_MANAGER (initable);

    /* Create filter */
    self->priv->filter = mm_filter_new (self->priv->filter_policy, error);
    if (!self->priv->filter)
        return FALSE;

    /* Create plugin manager */
    self->priv->plugin_manager = mm_plugin_manager_new (self->priv->filter,
#if !defined WITH_BUILTIN_PLUGINS
                                                        self->priv->plugin_dir,
#endif
                                                        error);
    if (!self->priv->plugin_manager)
        return FALSE;

#if defined WITH_UDEV
    /* Create udev client based on the subsystems requested by the plugins */
    self->priv->udev = g_udev_client_new (mm_plugin_manager_get_subsystems (self->priv->plugin_manager));

    /* If autoscan enabled, list for udev events */
    if (!mm_context_get_test_no_udev () && self->priv->auto_scan)
        g_signal_connect_swapped (self->priv->udev, "uevent", G_CALLBACK (handle_uevent), initable);
#endif

#if defined WITH_QRTR
    if (!mm_context_get_test_no_qrtr ()) {
        /* Create and setup the QrtrBusWatcher */
        self->priv->qrtr_bus_watcher = mm_qrtr_bus_watcher_new ();
        mm_qrtr_bus_watcher_start (self->priv->qrtr_bus_watcher, NULL, NULL);
        /* If autoscan enabled, list for QrtrBusWatcher events */
        if (self->priv->auto_scan) {
            g_object_connect (self->priv->qrtr_bus_watcher,
                              "swapped-signal::" MM_QRTR_BUS_WATCHER_DEVICE_ADDED,   G_CALLBACK (handle_qrtr_device_added),   self,
                              "swapped-signal::" MM_QRTR_BUS_WATCHER_DEVICE_REMOVED, G_CALLBACK (handle_qrtr_device_removed), self,
                              NULL);
        }
    }
#endif

    /* Export the manager interface */
    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (initable),
                                           self->priv->connection,
                                           MM_DBUS_PATH,
                                           error))
        return FALSE;

    /* Export the Object Manager interface */
    g_dbus_object_manager_server_set_connection (self->priv->object_manager,
                                                 self->priv->connection);

#if defined WITH_TESTS
    /* Setup the Test skeleton and export the interface */
    if (self->priv->enable_test) {
        self->priv->test_skeleton = mm_gdbus_test_skeleton_new ();
        g_signal_connect (self->priv->test_skeleton,
                          "handle-set-profile",
                          G_CALLBACK (handle_set_profile),
                          initable);
        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->priv->test_skeleton),
                                               self->priv->connection,
                                               MM_DBUS_PATH,
                                               error))
            return FALSE;
    }
#endif

    /* All good */
    return TRUE;
}

static void
finalize (GObject *object)
{
    MMBaseManager *self = MM_BASE_MANAGER (object);

    g_free (self->priv->initial_kernel_events);
#if !defined WITH_BUILTIN_PLUGINS
    g_free (self->priv->plugin_dir);
#endif

    g_hash_table_destroy (self->priv->inhibited_devices);
    g_hash_table_destroy (self->priv->devices);

#if defined WITH_UDEV
    if (self->priv->udev)
        g_object_unref (self->priv->udev);
#endif

#if defined WITH_QRTR
    if (self->priv->qrtr_bus_watcher)
        g_object_unref (self->priv->qrtr_bus_watcher);
#endif

    if (self->priv->filter)
        g_object_unref (self->priv->filter);

    if (self->priv->plugin_manager)
        g_object_unref (self->priv->plugin_manager);

    if (self->priv->object_manager)
        g_object_unref (self->priv->object_manager);

#if defined WITH_TESTS
    if (self->priv->test_skeleton)
        g_object_unref (self->priv->test_skeleton);
#endif

    if (self->priv->connection)
        g_object_unref (self->priv->connection);

    /* note: authp is a singleton, we don't keep a full reference */

    if (self->priv->authp_cancellable)
        g_object_unref (self->priv->authp_cancellable);

    G_OBJECT_CLASS (mm_base_manager_parent_class)->finalize (object);
}

static void
initable_iface_init (GInitableIface *iface)
{
    iface->init = initable_init;
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
mm_base_manager_class_init (MMBaseManagerClass *manager_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

    g_type_class_add_private (object_class, sizeof (MMBaseManagerPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    /* Properties */

    g_object_class_install_property
        (object_class, PROP_CONNECTION,
         g_param_spec_object (MM_BASE_MANAGER_CONNECTION,
                              "Connection",
                              "GDBus connection to the system bus.",
                              G_TYPE_DBUS_CONNECTION,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    g_object_class_install_property
        (object_class, PROP_AUTO_SCAN,
         g_param_spec_boolean (MM_BASE_MANAGER_AUTO_SCAN,
                               "Auto scan",
                               "Automatically look for new devices",
                               TRUE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property (
        object_class, PROP_FILTER_POLICY,
        g_param_spec_flags (MM_BASE_MANAGER_FILTER_POLICY,
                            "Filter policy",
                            "Mask of rules enabled in the filter",
                            MM_TYPE_FILTER_RULE,
                            MM_FILTER_RULE_NONE,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

#if !defined WITH_BUILTIN_PLUGINS
    g_object_class_install_property
        (object_class, PROP_PLUGIN_DIR,
         g_param_spec_string (MM_BASE_MANAGER_PLUGIN_DIR,
                              "Plugin directory",
                              "Where to look for plugins",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
#endif

    g_object_class_install_property
        (object_class, PROP_INITIAL_KERNEL_EVENTS,
         g_param_spec_string (MM_BASE_MANAGER_INITIAL_KERNEL_EVENTS,
                              "Initial kernel events",
                              "Path to a file with the list of initial kernel events",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

#if defined WITH_TESTS
    g_object_class_install_property
        (object_class, PROP_ENABLE_TEST,
         g_param_spec_boolean (MM_BASE_MANAGER_ENABLE_TEST,
                               "Enable tests",
                               "Enable the Test interface",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
#endif
}
