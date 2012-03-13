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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2011 Google, Inc.
 */

#include <string.h>
#include <ctype.h>

#include <gmodule.h>
#include <gudev/gudev.h>

#include <ModemManager.h>
#include <mm-errors-types.h>
#include <mm-gdbus-manager.h>

#include "mm-manager.h"
#include "mm-plugin-manager.h"
#include "mm-auth.h"
#include "mm-plugin.h"
#include "mm-log.h"
#include "mm-port-probe-cache.h"

static void grab_port (MMManager *manager,
                       MMPlugin *plugin,
                       GUdevDevice *device,
                       GUdevDevice *physical_device);

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
    /* The container of currently available modems */
    GHashTable *modems;
    /* DBus The Object Manager server */
    GDBusObjectManagerServer *object_manager;
};

typedef struct {
    MMManager *manager;
    GUdevDevice *device;
    GUdevDevice *physical_device;
} FindPortSupportContext;

static FindPortSupportContext *
find_port_support_context_new (MMManager *manager,
                               GUdevDevice *device,
                               GUdevDevice *physical_device)
{
    FindPortSupportContext *ctx;

    ctx = g_new0 (FindPortSupportContext, 1);
    ctx->manager = manager;
    ctx->device = g_object_ref (device);
    ctx->physical_device = g_object_ref (physical_device);

    return ctx;
}

static void
find_port_support_context_free (FindPortSupportContext *ctx)
{
    g_object_unref (ctx->device);
    g_object_unref (ctx->physical_device);
    g_free (ctx);
}

static void
remove_modem (MMManager *manager,
              MMBaseModem *modem)
{
    gchar *path;
    gchar *device;

    device = g_strdup (mm_base_modem_get_device (modem));
    path = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (modem)));

    /* If we get DBus object path, modem was exported */
    if (path) {
        g_dbus_object_manager_server_unexport (manager->priv->object_manager, path);
        g_object_set (modem,
                      MM_BASE_MODEM_CONNECTION, NULL,
                      NULL);

        mm_dbg ("Unexported modem '%s' from path '%s'", device, path);
        g_free (path);
    } else {
        mm_dbg ("Removing modem '%s', which wasn't exported yet", device);
    }

    /* Run dispose before unref-ing, in order to cleanup the SIM object,
     * if any (which also holds a reference to the modem object) */
    g_object_run_dispose (G_OBJECT (modem));
    g_hash_table_remove (manager->priv->modems, device);
    g_free (device);
}

static void
debug_modem_info (MMManager *self,
                  MMBaseModem *modem,
                  const gchar *path)
{
    GUdevDevice *physdev;
    const gchar *subsys;

    physdev = g_udev_client_query_by_sysfs_path (self->priv->udev,
                                                 mm_base_modem_get_device (modem));
    subsys = (physdev ?
              g_udev_device_get_subsystem (physdev) :
              NULL);

    mm_dbg ("(%s): '%s' modem, VID 0x%04X PID 0x%04X (%s)",
            path,
            mm_base_modem_get_plugin (modem),
            (mm_base_modem_get_vendor_id (modem) & 0xFFFF),
            (mm_base_modem_get_product_id (modem) & 0xFFFF),
            subsys ? subsys : "unknown");

    if (physdev)
        g_object_unref (physdev);
}

static void
check_export_modem (MMManager *self,
                    MMBaseModem *modem)
{
    GError *error = NULL;
    static guint32 id = 0;
    const gchar *modem_physdev;
    const gchar *name;
    const gchar *subsys;
    gchar *path;

    /* A modem is only exported to D-Bus when both of the following are true:
     *
     *   1) the modem is valid
     *   2) all ports the modem provides have either been grabbed or are
     *       unsupported by any plugin
     *
     * This ensures that all the modem's ports are completely ready before
     * any clients can do anything with it.
     *
     * FIXME: if udev or the kernel are really slow giving us ports, there's a
     * chance that a port could show up after the modem is already created and
     * all other ports are already handled.  That chance is very small though.
     */

    modem_physdev = mm_base_modem_get_device (modem);
    g_assert (modem_physdev);

    /* Check for ports that are in the process of being interrogated by plugins */
    if (mm_plugin_manager_is_finding_device_support (self->priv->plugin_manager,
                                                     modem_physdev,
                                                     &subsys,
                                                     &name)) {
        mm_dbg ("(%s/%s): outstanding support task prevents export of '%s'",
                subsys, name, modem_physdev);
        return;
    }

    /* Plugin manager is not trying to find more ports supported by this device,
     * so we can organize the ports now (if not done already). */
    if (!mm_base_modem_organize_ports (modem, &error)) {
        /* If the ports were not properly organized, the modem will be marked as
         * invalid and therefore removed */
        mm_err ("Failed to organize modem ports: '%s'",
                error->message);
        g_error_free (error);
        remove_modem (self, modem);
        return;
    }

    /* If modem not yet valid (not fully initialized), don't export it */
    if (!mm_base_modem_get_valid (modem))
        return;

    /* Don't export already exported modems */
    g_object_get (modem,
                  "g-object-path", &path,
                  NULL);
    if (path) {
        g_free (path);
        mm_dbg ("Modem '%s' already exported", modem_physdev);
        return;
    }

    /* No outstanding port tasks, so if the modem is valid we can export it */
    path = g_strdup_printf (MM_DBUS_MODEM_PREFIX "/%d", id++);
    g_object_set (modem,
                  "g-object-path", path,
                  MM_BASE_MODEM_CONNECTION, self->priv->connection,
                  NULL);
    g_dbus_object_manager_server_export (self->priv->object_manager,
                                         G_DBUS_OBJECT_SKELETON (modem));
    mm_dbg ("Exported modem '%s' at path '%s'", modem_physdev, path);

    /* Once connected, dump additional debug info about the modem */
    debug_modem_info (self, modem, path);
    g_free (path);
}

static void
modem_valid (MMBaseModem *modem,
             GParamSpec *pspec,
             gpointer user_data)
{
    MMManager *manager = MM_MANAGER (user_data);

    if (mm_base_modem_get_valid (modem))
        check_export_modem (manager, modem);
    else
        remove_modem (manager, modem);
}

#define MANAGER_PLUGIN_TAG "manager-plugin"

static void
add_modem (MMManager *manager,
           MMBaseModem *modem,
           MMPlugin *plugin)
{
    const gchar *device;

    device = mm_base_modem_get_device (modem);
    if (!g_hash_table_lookup (manager->priv->modems, device)) {
        mm_dbg ("Added modem %s", device);
        g_hash_table_insert (manager->priv->modems,
                             g_strdup (device),
                             modem);
        g_object_set_data (G_OBJECT (modem), MANAGER_PLUGIN_TAG, plugin);
        g_signal_connect (modem, "notify::" MM_BASE_MODEM_VALID, G_CALLBACK (modem_valid), manager);
    }

    check_export_modem (manager, modem);
}

static MMBaseModem *
find_modem_for_device (MMManager *manager,
                       const gchar *device)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, manager->priv->modems);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMBaseModem *candidate = MM_BASE_MODEM (value);

        if (g_str_equal (device,
                         mm_base_modem_get_device (candidate)))
            return candidate;
    }

    return NULL;
}

static MMBaseModem *
find_modem_for_port (MMManager *manager,
                     const gchar *subsys,
                     const gchar *name)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, manager->priv->modems);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMBaseModem *candidate = MM_BASE_MODEM (value);

        if (mm_base_modem_owns_port (candidate, subsys, name))
            return candidate;
    }
    return NULL;
}

static void
find_port_support_ready_cb (MMPluginManager *plugin_manager,
                            GAsyncResult *result,
                            FindPortSupportContext *ctx)
{
    GError *error = NULL;
    MMPlugin *best_plugin;

    best_plugin = mm_plugin_manager_find_port_support_finish (plugin_manager,
                                                              result,
                                                              &error);
    if (!best_plugin) {
        MMBaseModem *existing;

        if (error) {
            mm_dbg ("(%s/%s): error checking support: '%s'",
                    g_udev_device_get_subsystem (ctx->device),
                    g_udev_device_get_name (ctx->device),
                    error->message);
            g_error_free (error);
        } else {
            mm_dbg ("(%s/%s): not supported by any plugin",
                    g_udev_device_get_subsystem (ctx->device),
                    g_udev_device_get_name (ctx->device));
        }

        /* So we couldn't get a plugin for this port, we should anyway check if
         * there is already an existing modem for the physical device, and if
         * so, check if it can already be exported. */
        existing = find_modem_for_device (
            ctx->manager,
            g_udev_device_get_sysfs_path (ctx->physical_device));
        if (existing)
            check_export_modem (ctx->manager, existing);
    } else {
        mm_dbg ("(%s/%s): found plugin '%s' giving best support",
                g_udev_device_get_subsystem (ctx->device),
                g_udev_device_get_name (ctx->device),
                mm_plugin_get_name ((MMPlugin *)best_plugin));

        grab_port (ctx->manager,
                   best_plugin,
                   ctx->device,
                   ctx->physical_device);
    }

    find_port_support_context_free (ctx);
}

static void
grab_port (MMManager *manager,
           MMPlugin *plugin,
           GUdevDevice *device,
           GUdevDevice *physical_device)
{
    GError *error = NULL;
    MMBaseModem *modem;
    MMBaseModem *existing;

    existing = g_hash_table_lookup (manager->priv->modems,
                                    g_udev_device_get_sysfs_path (physical_device));

    /* While grabbing the first port, modem will get created */
    modem = mm_plugin_grab_port (plugin,
                                 g_udev_device_get_subsystem (device),
                                 g_udev_device_get_name (device),
                                 existing,
                                 &error);
    if (!modem) {
        mm_warn ("plugin '%s' claimed to support %s/%s but couldn't: (%d) %s",
                 mm_plugin_get_name (plugin),
                 g_udev_device_get_subsystem (device),
                 g_udev_device_get_name (device),
                 error ? error->code : -1,
                 (error && error->message) ? error->message : "(unknown)");
        g_clear_error (&error);

        if (existing)
            check_export_modem (manager, existing);
        return;
    }

    mm_info ("(%s): modem %s claimed port %s",
             mm_plugin_get_name (plugin),
             mm_base_modem_get_device (modem),
             g_udev_device_get_name (device));

    if (existing) {
        g_assert (existing == modem);
        check_export_modem (manager, modem);
    } else {
        /* If the modem was just created, store it */
        add_modem (manager, modem, plugin);
    }
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
            if (is_usb || !strcmp (subsys, "usb")) {
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
              GUdevDevice *device)
{
    const char *subsys, *name, *physdev_path, *physdev_subsys;
    gboolean is_candidate;
    GUdevDevice *physdev = NULL;
    MMPlugin *plugin;
    MMBaseModem *existing;

    g_return_if_fail (device != NULL);

    subsys = g_udev_device_get_subsystem (device);
    name = g_udev_device_get_name (device);

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
    is_candidate = g_udev_device_get_property_as_boolean (device, "ID_MM_CANDIDATE");
    if (!is_candidate)
        return;

    if (find_modem_for_port (manager, subsys, name))
        return;

    /* Find the port's physical device's sysfs path.  This is the kernel device
     * that "owns" all the ports of the device, like the USB device or the PCI
     * device the provides each tty or network port.
     */
    physdev = find_physical_device (device);
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

    /* Already launched the same port support check? */
    if (mm_plugin_manager_is_finding_port_support (manager->priv->plugin_manager,
                                                   subsys,
                                                   name,
                                                   physdev_path)) {
        mm_dbg ("(%s/%s): support check already requested in port", subsys, name);
        goto out;
    }

    /* If this port's physical modem is already owned by a plugin, don't bother
     * asking all plugins whether they support this port, just let the owning
     * plugin check if it supports the port.
     */
    existing = find_modem_for_device (manager, physdev_path);
    plugin = (existing ?
              MM_PLUGIN (g_object_get_data (G_OBJECT (existing), MANAGER_PLUGIN_TAG)) :
              NULL);

    /* Launch supports check in the Plugin Manager */
    mm_plugin_manager_find_port_support (
        manager->priv->plugin_manager,
        subsys,
        name,
        physdev_path,
        plugin,
        existing,
        (GAsyncReadyCallback)find_port_support_ready_cb,
        find_port_support_context_new (manager,
                                        device,
                                        physdev));

out:
    if (physdev)
        g_object_unref (physdev);
}

static void
device_removed (MMManager *manager,
                GUdevDevice *device)
{
    MMBaseModem *modem;
    const char *subsys, *name;

    g_return_if_fail (device != NULL);

    subsys = g_udev_device_get_subsystem (device);
    name = g_udev_device_get_name (device);

    /* Ensure cached port probe infos get removed when the port is gone */
    mm_port_probe_cache_remove (device);

    if (strcmp (subsys, "usb") != 0) {
        /* find_modem_for_port handles tty and net removal */
        modem = find_modem_for_port (manager, subsys, name);
        if (modem) {
            mm_info ("(%s/%s): released by modem %s",
                     subsys,
                     name,
                     mm_base_modem_get_device (modem));
            mm_base_modem_release_port (modem, subsys, name);
            return;
        }
    } else {
        /* This case is designed to handle the case where, at least with kernel 2.6.31, unplugging
         * an in-use ttyACMx device results in udev generating remove events for the usb, but the
         * ttyACMx device (subsystem tty) is not removed, since it was in-use.  So if we have not
         * found a modem for the port (above), we're going to look here to see if we have a modem
         * associated with the newly removed device.  If so, we'll remove the modem, since the
         * device has been removed.  That way, if the device is reinserted later, we'll go through
         * the process of exporting it.
         */
        const char *sysfs_path = g_udev_device_get_sysfs_path (device);

        modem = find_modem_for_device (manager, sysfs_path);
        if (modem) {
            mm_dbg ("Removing modem claimed by removed device %s", sysfs_path);
            remove_modem (manager, modem);
            return;
        }
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
    const char *subsys;

    g_return_if_fail (action != NULL);

    /* A bit paranoid */
    subsys = g_udev_device_get_subsystem (device);
    g_return_if_fail (subsys != NULL);

    g_return_if_fail (!strcmp (subsys, "tty") || !strcmp (subsys, "net") || !strcmp (subsys, "usb"));

    /* We only care about tty/net devices when adding modem ports,
     * but for remove, also handle usb parent device remove events
     */
    if (   (!strcmp (action, "add") || !strcmp (action, "move") || !strcmp (action, "change"))
        && (strcmp (subsys, "usb") != 0))
        device_added (self, device);
    else if (!strcmp (action, "remove"))
        device_removed (self, device);
}

void
mm_manager_start (MMManager *manager)
{
    GList *devices, *iter;

    g_return_if_fail (manager != NULL);
    g_return_if_fail (MM_IS_MANAGER (manager));

    mm_dbg ("Starting device scan...");

    devices = g_udev_client_query_by_subsystem (manager->priv->udev, "tty");
    for (iter = devices; iter; iter = g_list_next (iter)) {
        device_added (manager, G_UDEV_DEVICE (iter->data));
        g_object_unref (G_OBJECT (iter->data));
    }
    g_list_free (devices);

    devices = g_udev_client_query_by_subsystem (manager->priv->udev, "net");
    for (iter = devices; iter; iter = g_list_next (iter)) {
        device_added (manager, G_UDEV_DEVICE (iter->data));
        g_object_unref (G_OBJECT (iter->data));
    }
    g_list_free (devices);

    mm_dbg ("Finished device scan...");
}

typedef struct {
    MMManager *manager;
    MMBaseModem *modem;
} RemoveInfo;

static gboolean
remove_disable_one (gpointer user_data)
{
    RemoveInfo *info = user_data;

    remove_modem (info->manager, info->modem);
    g_free (info);
    return FALSE;
}

static void
remove_disable_done (MMBaseModem *modem,
                     GError *error,
                     gpointer user_data)
{
    RemoveInfo *info;

    /* Schedule modem removal from an idle handler since we get here deep
     * in the modem removal callchain and can't remove it quite yet from here.
     */
    info = g_malloc0 (sizeof (RemoveInfo));
    info->manager = MM_MANAGER (user_data);
    info->modem = modem;
    g_idle_add (remove_disable_one, info);
}

void
mm_manager_shutdown (MMManager *self)
{
    GList *modems, *iter;

    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MANAGER (self));

    /* Cancel all ongoing auth requests */
    g_cancellable_cancel (self->priv->authp_cancellable);


    modems = g_hash_table_get_values (self->priv->modems);
    for (iter = modems; iter; iter = g_list_next (iter)) {
        MMBaseModem *modem = MM_BASE_MODEM (iter->data);

        /* TODO */
        /* if (mm_modem_get_state (modem) >= MM_MODEM_STATE_ENABLING) */
        /*     mm_modem_disable (modem, remove_disable_done, self); */
        /* else */
        remove_disable_done (modem, NULL, self);
    }
    g_list_free (modems);

    /* Disabling may take a few iterations of the mainloop, so the caller
     * has to iterate the mainloop until all devices have been disabled and
     * removed.
     */
}

guint32
mm_manager_num_modems (MMManager *self)
{
    g_return_val_if_fail (self != NULL, 0);
    g_return_val_if_fail (MM_IS_MANAGER (self), 0);

    return g_hash_table_size (self->priv->modems);
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
        mm_manager_start (MM_MANAGER (ctx->self));
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
    const char *subsys[4] = { "tty", "net", "usb", NULL };

    /* Setup private data */
    manager->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE ((manager),
                                                        MM_TYPE_MANAGER,
                                                        MMManagerPrivate);

    /* Setup authorization provider */
    priv->authp = mm_auth_get_provider ();
    priv->authp_cancellable = g_cancellable_new ();

    /* Setup internal list of modem objects */
    priv->modems = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

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

    g_hash_table_destroy (priv->modems);

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
