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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <string.h>
#include <ctype.h>

#include <gmodule.h>
#include <gudev/gudev.h>

#include <ModemManager.h>
#include <mm-errors-types.h>
#include <mm-gdbus-manager.h>

#include "mm-manager.h"
#include "mm-device.h"
#include "mm-plugin-manager.h"
#include "mm-auth.h"
#include "mm-plugin.h"
#include "mm-log.h"

static void initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMManager, mm_manager, MM_GDBUS_TYPE_ORG_FREEDESKTOP_MODEM_MANAGER1_SKELETON, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                               initable_iface_init));

enum {
    PROP_0,
    PROP_CONNECTION,
    LAST_PROP
};

struct _MMManagerPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    /* The UDev client */
    GUdevClient *udev;
    /* The authorization provider */
    MMAuthProvider *authp;
    GCancellable *authp_cancellable;
    /* The Plugin Manager object */
    MMPluginManager *plugin_manager;
    /* The container of devices being prepared */
    GHashTable *devices;
    /* The Object Manager server */
    GDBusObjectManagerServer *object_manager;
};

/*****************************************************************************/

static MMDevice *
find_device_by_modem (MMManager *manager,
                      MMBaseModem *modem)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, manager->priv->devices);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMDevice *candidate = MM_DEVICE (value);

        if (modem == mm_device_peek_modem (candidate))
            return candidate;
    }
    return NULL;
}

static MMDevice *
find_device_by_port (MMManager *manager,
                     GUdevDevice *port)
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
find_device_by_sysfs_path (MMManager *self,
                           const gchar *sysfs_path)
{
    return g_hash_table_lookup (self->priv->devices,
                                sysfs_path);
}

static MMDevice *
find_device_by_udev_device (MMManager *manager,
                            GUdevDevice *udev_device)
{
    return find_device_by_sysfs_path (manager, g_udev_device_get_sysfs_path (udev_device));
}

/*****************************************************************************/

typedef struct {
    MMManager *self;
    MMDevice *device;
} FindDeviceSupportContext;

static void
find_device_support_context_free (FindDeviceSupportContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->device);
    g_slice_free (FindDeviceSupportContext, ctx);
}

static void
find_device_support_ready (MMPluginManager *plugin_manager,
                           GAsyncResult *result,
                           FindDeviceSupportContext *ctx)
{
    GError *error = NULL;

    if (!mm_plugin_manager_find_device_support_finish (plugin_manager, result, &error)) {
        mm_warn ("Couldn't find support for device at '%s': %s",
                 mm_device_get_path (ctx->device),
                 error->message);
        g_error_free (error);
    } else if (!mm_device_create_modem (ctx->device, ctx->self->priv->object_manager, &error)) {
        mm_warn ("Couldn't create modem for device at '%s': %s",
                 mm_device_get_path (ctx->device),
                 error->message);
        g_error_free (error);
    } else {
        mm_info ("Modem for device at '%s' successfully created",
                 mm_device_get_path (ctx->device));
    }

    find_device_support_context_free (ctx);
}

static GUdevDevice *
find_physical_device (GUdevDevice *child)
{
    GUdevDevice *iter, *old = NULL;
    GUdevDevice *physdev = NULL;
    const char *subsys, *type;
    guint32 i = 0;
    gboolean is_usb = FALSE, is_pci = FALSE, is_pcmcia = FALSE, is_platform = FALSE;

    g_return_val_if_fail (child != NULL, NULL);

    iter = g_object_ref (child);
    while (iter && i++ < 8) {
        subsys = g_udev_device_get_subsystem (iter);
        if (subsys) {
            if (is_usb || g_str_has_prefix (subsys, "usb")) {
                is_usb = TRUE;
                type = g_udev_device_get_devtype (iter);
                if (type && !strcmp (type, "usb_device")) {
                    physdev = iter;
                    break;
                }
            } else if (is_pcmcia || !strcmp (subsys, "pcmcia")) {
                GUdevDevice *pcmcia_parent;
                const char *tmp_subsys;

                is_pcmcia = TRUE;

                /* If the parent of this PCMCIA device is no longer part of
                 * the PCMCIA subsystem, we want to stop since we're looking
                 * for the base PCMCIA device, not the PCMCIA controller which
                 * is usually PCI or some other bus type.
                 */
                pcmcia_parent = g_udev_device_get_parent (iter);
                if (pcmcia_parent) {
                    tmp_subsys = g_udev_device_get_subsystem (pcmcia_parent);
                    if (tmp_subsys && strcmp (tmp_subsys, "pcmcia"))
                        physdev = iter;
                    g_object_unref (pcmcia_parent);
                    if (physdev)
                        break;
                }
            } else if (is_platform || !strcmp (subsys, "platform")) {
                /* Take the first platform parent as the physical device */
                is_platform = TRUE;
                physdev = iter;
                break;
            } else if (is_pci || !strcmp (subsys, "pci")) {
                is_pci = TRUE;
                physdev = iter;
                break;
            }
        }

        old = iter;
        iter = g_udev_device_get_parent (old);
        g_object_unref (old);
    }

    return physdev;
}

static void
device_added (MMManager *manager,
              GUdevDevice *port,
              gboolean hotplugged,
              gboolean manual_scan)
{
    MMDevice *device;
    const char *subsys, *name, *physdev_path, *physdev_subsys;
    gboolean is_candidate;
    GUdevDevice *physdev = NULL;

    g_return_if_fail (port != NULL);

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    /* ignore VTs */
    if (strncmp (name, "tty", 3) == 0 && isdigit (name[3]))
        return;

    /* Ignore devices that aren't completely configured by udev yet.  If
     * ModemManager is started in parallel with udev, explicitly requesting
     * devices may return devices for which not all udev rules have yet been
     * applied (a bug in udev/gudev).  Since we often need those rules to match
     * the device to a specific ModemManager driver, we need to ensure that all
     * rules have been processed before handling a device.
     */
    is_candidate = g_udev_device_get_property_as_boolean (port, "ID_MM_CANDIDATE");
    if (!is_candidate)
        return;

    if (find_device_by_port (manager, port))
        return;

    /* Find the port's physical device's sysfs path.  This is the kernel device
     * that "owns" all the ports of the device, like the USB device or the PCI
     * device the provides each tty or network port.
     */
    physdev = find_physical_device (port);
    if (!physdev) {
        /* Warn about it, but filter out some common ports that we know don't have
         * anything to do with mobile broadband.
         */
        if (   strcmp (name, "console")
            && strcmp (name, "ptmx")
            && strcmp (name, "lo")
            && strcmp (name, "tty")
            && !strstr (name, "virbr"))
            mm_dbg ("(%s/%s): could not get port's parent device", subsys, name);

        goto out;
    }

    /* Is the device blacklisted? */
    if (g_udev_device_get_property_as_boolean (physdev, "ID_MM_DEVICE_IGNORE")) {
        mm_dbg ("(%s/%s): port's parent device is blacklisted", subsys, name);
        goto out;
    }

    /* Is the device in the manual-only greylist? If so, return if this is an
     * automatic scan. */
    if (!manual_scan && g_udev_device_get_property_as_boolean (physdev, "ID_MM_DEVICE_MANUAL_SCAN_ONLY")) {
        mm_dbg ("(%s/%s): port probed only in manual scan", subsys, name);
        goto out;
    }

    /* If the physdev is a 'platform' device that's not whitelisted, ignore it */
    physdev_subsys = g_udev_device_get_subsystem (physdev);
    if (   physdev_subsys
        && !strcmp (physdev_subsys, "platform")
        && !g_udev_device_get_property_as_boolean (physdev, "ID_MM_PLATFORM_DRIVER_PROBE")) {
        mm_dbg ("(%s/%s): port's parent platform driver is not whitelisted", subsys, name);
        goto out;
    }

    physdev_path = g_udev_device_get_sysfs_path (physdev);
    if (!physdev_path) {
        mm_dbg ("(%s/%s): could not get port's parent device sysfs path", subsys, name);
        goto out;
    }

    /* See if we already created an object to handle ports in this device */
    device = find_device_by_sysfs_path (manager, physdev_path);
    if (!device) {
        FindDeviceSupportContext *ctx;

        /* Keep the device listed in the Manager */
        device = mm_device_new (physdev, hotplugged);
        g_hash_table_insert (manager->priv->devices,
                             g_strdup (physdev_path),
                             device);

        /* Launch device support check */
        ctx = g_slice_new (FindDeviceSupportContext);
        ctx->self = g_object_ref (manager);
        ctx->device = g_object_ref (device);
        mm_plugin_manager_find_device_support (
            manager->priv->plugin_manager,
            device,
            (GAsyncReadyCallback)find_device_support_ready,
            ctx);
    }

    /* Grab the port in the existing device. */
    mm_device_grab_port (device, port);

out:
    if (physdev)
        g_object_unref (physdev);
}

static void
device_removed (MMManager *self,
                GUdevDevice *udev_device)
{
    MMDevice *device;
    const gchar *subsys;
    const gchar *name;

    g_return_if_fail (udev_device != NULL);

    subsys = g_udev_device_get_subsystem (udev_device);
    name = g_udev_device_get_name (udev_device);

    if (!g_str_has_prefix (subsys, "usb") ||
        (name && g_str_has_prefix (name, "cdc-wdm"))) {
        /* Handle tty/net/wdm port removal */
        device = find_device_by_port (self, udev_device);
        if (device) {
            mm_info ("(%s/%s): released by modem %s",
                     subsys,
                     name,
                     g_udev_device_get_sysfs_path (mm_device_peek_udev_device (device)));
            mm_device_release_port (device, udev_device);

            /* If port probe list gets empty, remove the device object iself */
            if (!mm_device_peek_port_probe_list (device)) {
                mm_dbg ("Removing empty device '%s'", mm_device_get_path (device));
                mm_device_remove_modem (device);
                g_hash_table_remove (self->priv->devices, mm_device_get_path (device));
            }
        }

        return;
    }

    /* This case is designed to handle the case where, at least with kernel 2.6.31, unplugging
     * an in-use ttyACMx device results in udev generating remove events for the usb, but the
     * ttyACMx device (subsystem tty) is not removed, since it was in-use.  So if we have not
     * found a modem for the port (above), we're going to look here to see if we have a modem
     * associated with the newly removed device.  If so, we'll remove the modem, since the
     * device has been removed.  That way, if the device is reinserted later, we'll go through
     * the process of exporting it.
     */
    device = find_device_by_udev_device (self, udev_device);
    if (device) {
        mm_dbg ("Removing device '%s'", mm_device_get_path (device));
        mm_device_remove_modem (device);
        g_hash_table_remove (self->priv->devices, mm_device_get_path (device));
        return;
    }

    /* Maybe a plugin is checking whether or not the port is supported.
     * TODO: Cancel every possible supports check in this port. */
}

static void
handle_uevent (GUdevClient *client,
               const char *action,
               GUdevDevice *device,
               gpointer user_data)
{
    MMManager *self = MM_MANAGER (user_data);
    const gchar *subsys;
    const gchar *name;

    g_return_if_fail (action != NULL);

    /* A bit paranoid */
    subsys = g_udev_device_get_subsystem (device);
    g_return_if_fail (subsys != NULL);
    g_return_if_fail (g_str_equal (subsys, "tty") || g_str_equal (subsys, "net") || g_str_has_prefix (subsys, "usb"));

    /* We only care about tty/net and usb/cdc-wdm devices when adding modem ports,
     * but for remove, also handle usb parent device remove events
     */
    name = g_udev_device_get_name (device);
    if (   (g_str_equal (action, "add") || g_str_equal (action, "move") || g_str_equal (action, "change"))
        && (!g_str_has_prefix (subsys, "usb") || (name && g_str_has_prefix (name, "cdc-wdm"))))
        device_added (self, device, TRUE, FALSE);
    else if (g_str_equal (action, "remove"))
        device_removed (self, device);
}

typedef struct {
    MMManager *self;
    GUdevDevice *device;
    gboolean manual_scan;
} StartDeviceAdded;

static gboolean
start_device_added_idle (StartDeviceAdded *ctx)
{
    device_added (ctx->self, ctx->device, FALSE, ctx->manual_scan);
    g_object_unref (ctx->self);
    g_object_unref (ctx->device);
    g_slice_free (StartDeviceAdded, ctx);
    return FALSE;
}

static void
start_device_added (MMManager *self,
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

void
mm_manager_start (MMManager *manager,
                  gboolean manual_scan)
{
    GList *devices, *iter;

    g_return_if_fail (manager != NULL);
    g_return_if_fail (MM_IS_MANAGER (manager));

    mm_dbg ("Starting %s device scan...", manual_scan ? "manual" : "automatic");

    devices = g_udev_client_query_by_subsystem (manager->priv->udev, "tty");
    for (iter = devices; iter; iter = g_list_next (iter)) {
        start_device_added (manager, G_UDEV_DEVICE (iter->data), manual_scan);
        g_object_unref (G_OBJECT (iter->data));
    }
    g_list_free (devices);

    devices = g_udev_client_query_by_subsystem (manager->priv->udev, "net");
    for (iter = devices; iter; iter = g_list_next (iter)) {
        start_device_added (manager, G_UDEV_DEVICE (iter->data), manual_scan);
        g_object_unref (G_OBJECT (iter->data));
    }
    g_list_free (devices);

    devices = g_udev_client_query_by_subsystem (manager->priv->udev, "usb");
    for (iter = devices; iter; iter = g_list_next (iter)) {
        const gchar *name;

        name = g_udev_device_get_name (G_UDEV_DEVICE (iter->data));
        if (name && g_str_has_prefix (name, "cdc-wdm"))
            start_device_added (manager, G_UDEV_DEVICE (iter->data), manual_scan);
        g_object_unref (G_OBJECT (iter->data));
    }
    g_list_free (devices);

    /* Newer kernels report 'usbmisc' subsystem */
    devices = g_udev_client_query_by_subsystem (manager->priv->udev, "usbmisc");
    for (iter = devices; iter; iter = g_list_next (iter)) {
        const gchar *name;

        name = g_udev_device_get_name (G_UDEV_DEVICE (iter->data));
        if (name && g_str_has_prefix (name, "cdc-wdm"))
            start_device_added (manager, G_UDEV_DEVICE (iter->data), manual_scan);
        g_object_unref (G_OBJECT (iter->data));
    }
    g_list_free (devices);

    mm_dbg ("Finished device scan...");
}

/*****************************************************************************/

static void
remove_disable_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      MMManager *self)
{
    MMDevice *device;

    /* We don't care about errors disabling at this point */
    mm_base_modem_disable_finish (modem, res, NULL);

    device = find_device_by_modem (self, modem);
    if (device) {
        mm_device_remove_modem (device);
        g_hash_table_remove (self->priv->devices, device);
    }
}

static void
foreach_disable (gpointer key,
                 MMDevice *device,
                 MMManager *self)
{
    MMBaseModem *modem;

    modem = mm_device_peek_modem (device);
    if (modem)
        mm_base_modem_disable (modem, (GAsyncReadyCallback)remove_disable_ready, self);
}

void
mm_manager_shutdown (MMManager *self)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MANAGER (self));

    /* Cancel all ongoing auth requests */
    g_cancellable_cancel (self->priv->authp_cancellable);

    g_hash_table_foreach (self->priv->devices, (GHFunc)foreach_disable, self);

    /* Disabling may take a few iterations of the mainloop, so the caller
     * has to iterate the mainloop until all devices have been disabled and
     * removed.
     */
}

guint32
mm_manager_num_modems (MMManager *self)
{
    GHashTableIter iter;
    gpointer key, value;
    guint32 n;

    g_return_val_if_fail (self != NULL, 0);
    g_return_val_if_fail (MM_IS_MANAGER (self), 0);

    n = 0;
    g_hash_table_iter_init (&iter, self->priv->devices);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        n += !!mm_device_peek_modem (MM_DEVICE (value));
    }

    return n;
}

/*****************************************************************************/

typedef struct {
    MMManager *self;
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
set_logging_auth_ready (MMAuthProvider *authp,
                        GAsyncResult *res,
                        SetLoggingContext *ctx)
{
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
	else if (!mm_log_set_level (ctx->level, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_info ("logging: level '%s'", ctx->level);
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
    ctx->self = g_object_ref (manager);
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

typedef struct {
    MMManager *self;
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
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        /* Otherwise relaunch device scan */
        mm_manager_start (MM_MANAGER (ctx->self), TRUE);
        mm_gdbus_org_freedesktop_modem_manager1_complete_scan_devices (
            MM_GDBUS_ORG_FREEDESKTOP_MODEM_MANAGER1 (ctx->self),
            ctx->invocation);
    }

    scan_devices_context_free (ctx);
}

static gboolean
handle_scan_devices (MmGdbusOrgFreedesktopModemManager1 *manager,
                     GDBusMethodInvocation *invocation)
{
    ScanDevicesContext *ctx;

    ctx = g_new (ScanDevicesContext, 1);
    ctx->self = g_object_ref (manager);
    ctx->invocation = g_object_ref (invocation);

    mm_auth_provider_authorize (ctx->self->priv->authp,
                                invocation,
                                MM_AUTHORIZATION_MANAGER_CONTROL,
                                ctx->self->priv->authp_cancellable,
                                (GAsyncReadyCallback)scan_devices_auth_ready,
                                ctx);
    return TRUE;
}

MMManager *
mm_manager_new (GDBusConnection *connection,
                GError **error)
{
    g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);

    return g_initable_new (MM_TYPE_MANAGER,
                           NULL, /* cancellable */
                           error,
                           MM_MANAGER_CONNECTION, connection,
                           NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMManagerPrivate *priv = MM_MANAGER (object)->priv;

    switch (prop_id) {
    case PROP_CONNECTION:
        if (priv->connection)
            g_object_unref (priv->connection);
        priv->connection = g_value_dup_object (value);
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
    MMManagerPrivate *priv = MM_MANAGER (object)->priv;

    switch (prop_id) {
    case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_manager_init (MMManager *manager)
{
    MMManagerPrivate *priv;
    const gchar *subsys[5] = { "tty", "net", "usb", "usbmisc", NULL };

    /* Setup private data */
    manager->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE ((manager),
                                                        MM_TYPE_MANAGER,
                                                        MMManagerPrivate);

    /* Setup authorization provider */
    priv->authp = mm_auth_get_provider ();
    priv->authp_cancellable = g_cancellable_new ();

    /* Setup internal lists of device objects */
    priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    /* Setup UDev client */
    priv->udev = g_udev_client_new (subsys);
    g_signal_connect (priv->udev, "uevent", G_CALLBACK (handle_uevent), manager);

    /* Setup Object Manager Server */
    priv->object_manager = g_dbus_object_manager_server_new (MM_DBUS_PATH);

    /* Enable processing of input DBus messages */
    g_signal_connect (manager,
                      "handle-set-logging",
                      G_CALLBACK (handle_set_logging),
                      NULL);
    g_signal_connect (manager,
                      "handle-scan-devices",
                      G_CALLBACK (handle_scan_devices),
                      NULL);
}

static gboolean
initable_init (GInitable *initable,
               GCancellable *cancellable,
               GError **error)
{
    MMManagerPrivate *priv = MM_MANAGER (initable)->priv;

    /* Create plugin manager */
    priv->plugin_manager = mm_plugin_manager_new (error);
    if (!priv->plugin_manager)
        return FALSE;

    /* Export the manager interface */
    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (initable),
                                           priv->connection,
                                           MM_DBUS_PATH,
                                           error))
        return FALSE;

    /* Export the Object Manager interface */
    g_dbus_object_manager_server_set_connection (priv->object_manager,
                                                 priv->connection);

    /* All good */
    return TRUE;
}

static void
finalize (GObject *object)
{
    MMManagerPrivate *priv = MM_MANAGER (object)->priv;

    g_hash_table_destroy (priv->devices);

    if (priv->udev)
        g_object_unref (priv->udev);

    if (priv->plugin_manager)
        g_object_unref (priv->plugin_manager);

    if (priv->object_manager)
        g_object_unref (priv->object_manager);

    if (priv->connection)
        g_object_unref (priv->connection);

    if (priv->authp)
        g_object_unref (priv->authp);

    if (priv->authp_cancellable)
        g_object_unref (priv->authp_cancellable);

    G_OBJECT_CLASS (mm_manager_parent_class)->finalize (object);
}

static void
initable_iface_init (GInitableIface *iface)
{
	iface->init = initable_init;
}

static void
mm_manager_class_init (MMManagerClass *manager_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

    g_type_class_add_private (object_class, sizeof (MMManagerPrivate));

    /* Virtual methods */
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    /* Properties */
    g_object_class_install_property
        (object_class, PROP_CONNECTION,
         g_param_spec_object (MM_MANAGER_CONNECTION,
                              "Connection",
                              "GDBus connection to the system bus.",
                              G_TYPE_DBUS_CONNECTION,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}
