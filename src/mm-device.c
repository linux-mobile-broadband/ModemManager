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
 * Copyright (C) 2012 Google, Inc.
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
#include "mm-log.h"

G_DEFINE_TYPE (MMDevice, mm_device, G_TYPE_OBJECT);

enum {
    PROP_0,
    PROP_UDEV_DEVICE,
    PROP_PLUGIN,
    PROP_MODEM,
    PROP_LAST
};

enum {
    SIGNAL_PORT_GRABBED,
    SIGNAL_PORT_RELEASED,
    SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

struct _MMDevicePrivate {
    /* Parent UDev device */
    GUdevDevice *udev_device;
    gchar *udev_device_path;
    guint16 vendor;
    guint16 product;

    /* Kernel drivers managing this device */
    gchar **drivers;

    /* Best plugin to manage this device */
    MMPlugin *plugin;

    /* Lists of port probes in the device */
    GList *port_probes;
    GList *ignored_port_probes;

    /* The Modem object for this device */
    MMBaseModem *modem;

    /* When exported, a reference to the object manager */
    GDBusObjectManagerServer *object_manager;
};

/*****************************************************************************/

static MMPortProbe *
device_find_probe_with_device (MMDevice    *self,
                               GUdevDevice *udev_port,
                               gboolean lookup_ignored)
{
    GList *l;

    for (l = self->priv->port_probes; l; l = g_list_next (l)) {
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (g_str_equal (g_udev_device_get_sysfs_path (mm_port_probe_peek_port (probe)),
                         g_udev_device_get_sysfs_path (udev_port)))
            return probe;
    }

    if (!lookup_ignored)
        return NULL;

    for (l = self->priv->ignored_port_probes; l; l = g_list_next (l)) {
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (g_str_equal (g_udev_device_get_sysfs_path (mm_port_probe_peek_port (probe)),
                         g_udev_device_get_sysfs_path (udev_port)))
            return probe;
    }

    return NULL;
}

gboolean
mm_device_owns_port (MMDevice    *self,
                     GUdevDevice *udev_port)
{
    return !!device_find_probe_with_device (self, udev_port, FALSE);
}

static gboolean
get_device_ids (GUdevDevice *device,
                guint16     *vendor,
                guint16     *product)
{
    GUdevDevice *parent = NULL;
    const gchar *vid = NULL, *pid = NULL, *parent_subsys;
    gboolean success = FALSE;

    parent = g_udev_device_get_parent (device);
    if (parent) {
        parent_subsys = g_udev_device_get_subsystem (parent);
        if (parent_subsys) {
            if (g_str_equal (parent_subsys, "bluetooth")) {
                /* Bluetooth devices report the VID/PID of the BT adapter here,
                 * which isn't really what we want.  Just return null IDs instead.
                 */
                success = TRUE;
                goto out;
            } else if (g_str_equal (parent_subsys, "pcmcia")) {
                /* For PCMCIA devices we need to grab the PCMCIA subsystem's
                 * manfid and cardid, since any IDs on the tty device itself
                 * may be from PCMCIA controller or something else.
                 */
                vid = g_udev_device_get_sysfs_attr (parent, "manf_id");
                pid = g_udev_device_get_sysfs_attr (parent, "card_id");
                if (!vid || !pid)
                    goto out;
            } else if (g_str_equal (parent_subsys, "platform")) {
                /* Platform devices don't usually have a VID/PID */
                success = TRUE;
                goto out;
            } else if (g_str_has_prefix (parent_subsys, "usb") &&
                       g_str_equal (g_udev_device_get_driver (parent), "qmi_wwan")) {
                /* Need to look for vendor/product in the parent of the QMI device */
                GUdevDevice *qmi_parent;

                qmi_parent = g_udev_device_get_parent (parent);
                if (qmi_parent) {
                    vid = g_udev_device_get_property (qmi_parent, "ID_VENDOR_ID");
                    pid = g_udev_device_get_property (qmi_parent, "ID_MODEL_ID");
                    g_object_unref (qmi_parent);
                }
            }
        }
    }

    if (!vid)
        vid = g_udev_device_get_property (device, "ID_VENDOR_ID");
    if (!vid)
        goto out;

    if (strncmp (vid, "0x", 2) == 0)
        vid += 2;
    if (strlen (vid) != 4)
        goto out;

    if (vendor) {
        *vendor = (guint16) (mm_utils_hex2byte (vid + 2) & 0xFF);
        *vendor |= (guint16) ((mm_utils_hex2byte (vid) & 0xFF) << 8);
    }

    if (!pid)
        pid = g_udev_device_get_property (device, "ID_MODEL_ID");
    if (!pid) {
        *vendor = 0;
        goto out;
    }

    if (strncmp (pid, "0x", 2) == 0)
        pid += 2;
    if (strlen (pid) != 4) {
        *vendor = 0;
        goto out;
    }

    if (product) {
        *product = (guint16) (mm_utils_hex2byte (pid + 2) & 0xFF);
        *product |= (guint16) ((mm_utils_hex2byte (pid) & 0xFF) << 8);
    }

    success = TRUE;

out:
    if (parent)
        g_object_unref (parent);
    return success;
}

const gchar *
mm_device_utils_get_port_driver (GUdevDevice *udev_port)
{
    const gchar *driver, *subsys;

    driver = g_udev_device_get_driver (udev_port);
    if (!driver) {
        GUdevDevice *parent;

        parent = g_udev_device_get_parent (udev_port);
        if (parent)
            driver = g_udev_device_get_driver (parent);

        /* Check for bluetooth; it's driver is a bunch of levels up so we
         * just check for the subsystem of the parent being bluetooth.
         */
        if (!driver && parent) {
            subsys = g_udev_device_get_subsystem (parent);
            if (subsys && !strcmp (subsys, "bluetooth"))
                driver = "bluetooth";
        }

        if (parent)
            g_object_unref (parent);
    }

    return driver;
}

static void
add_port_driver (MMDevice *self,
                 GUdevDevice *udev_port)
{
    const gchar *driver;
    guint n_items;
    guint i;

    driver = mm_device_utils_get_port_driver (udev_port);

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

    self->priv->drivers = g_realloc (self->priv->drivers,
                                     (n_items + 2) * sizeof (gchar *));
    self->priv->drivers[n_items] = g_strdup (driver);
    self->priv->drivers[n_items + 1] = NULL;
}

void
mm_device_grab_port (MMDevice    *self,
                     GUdevDevice *udev_port)
{
    MMPortProbe *probe;

    if (mm_device_owns_port (self, udev_port))
        return;

    /* Get the vendor/product IDs out of the first one that gives us
     * some valid value (it seems we may get NULL reported for VID in QMI
     * ports, e.g. Huawei E367) */
    if (!self->priv->vendor && !self->priv->product) {
        if (!get_device_ids (udev_port,
                             &self->priv->vendor,
                             &self->priv->product)) {
            mm_dbg ("(%s) could not get vendor/product ID",
                    self->priv->udev_device_path);
        }
    }

    /* Add new port driver */
    add_port_driver (self, udev_port);

    /* Create and store new port probe */
    probe = mm_port_probe_new (self, udev_port);
    self->priv->port_probes = g_list_prepend (self->priv->port_probes, probe);

    /* Notify about the grabbed port */
    g_signal_emit (self, signals[SIGNAL_PORT_GRABBED], 0, udev_port);
}

void
mm_device_release_port (MMDevice    *self,
                        GUdevDevice *udev_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, udev_port, TRUE);
    if (probe) {
        /* Found, remove from list and destroy probe */
        self->priv->port_probes = g_list_remove (self->priv->port_probes, probe);
        g_signal_emit (self, signals[SIGNAL_PORT_RELEASED], 0, mm_port_probe_peek_port (probe));
        g_object_unref (probe);
    }
}

void
mm_device_ignore_port  (MMDevice *self,
                        GUdevDevice *udev_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, udev_port, FALSE);
    if (probe) {
        /* Found, remove from list and add to the ignored list */
        mm_dbg ("Fully ignoring port '%s/%s' from now on",
                g_udev_device_get_subsystem (udev_port),
                g_udev_device_get_name (udev_port));
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
                      MM_BASE_MODEM_CONNECTION, NULL,
                      NULL);

        mm_dbg ("Unexported modem '%s' from path '%s'",
                g_udev_device_get_sysfs_path (self->priv->udev_device),
                path);
        g_free (path);
    }
}

/*****************************************************************************/

static void
export_modem (MMDevice *self)
{
    GDBusConnection *connection = NULL;
    static guint32 id = 0;
    gchar *path;

    g_assert (MM_IS_BASE_MODEM (self->priv->modem));
    g_assert (G_IS_DBUS_OBJECT_MANAGER (self->priv->object_manager));

    /* If modem not yet valid (not fully initialized), don't export it */
    if (!mm_base_modem_get_valid (self->priv->modem)) {
        mm_dbg ("Modem '%s' not yet fully initialized",
                g_udev_device_get_sysfs_path (self->priv->udev_device));
        return;
    }

    /* Don't export already exported modems */
    g_object_get (self->priv->modem,
                  "g-object-path", &path,
                  NULL);
    if (path) {
        g_free (path);
        mm_dbg ("Modem '%s' already exported",
                g_udev_device_get_sysfs_path (self->priv->udev_device));
        return;
    }

    /* No outstanding port tasks, so if the modem is valid we can export it */

    path = g_strdup_printf (MM_DBUS_MODEM_PREFIX "/%d", id++);
    g_object_get (self->priv->object_manager,
                  "connection", &connection,
                  NULL);
    g_object_set (self->priv->modem,
                  "g-object-path", path,
                  MM_BASE_MODEM_CONNECTION, connection,
                  NULL);
    g_object_unref (connection);

    g_dbus_object_manager_server_export (self->priv->object_manager,
                                         G_DBUS_OBJECT_SKELETON (self->priv->modem));

    mm_dbg ("Exported modem '%s' at path '%s'",
            g_udev_device_get_sysfs_path (self->priv->udev_device),
            path);

    /* Once connected, dump additional debug info about the modem */
    mm_dbg ("(%s): '%s' modem, VID 0x%04X PID 0x%04X (%s)",
            path,
            mm_base_modem_get_plugin (self->priv->modem),
            (mm_base_modem_get_vendor_id (self->priv->modem) & 0xFFFF),
            (mm_base_modem_get_product_id (self->priv->modem) & 0xFFFF),
            g_udev_device_get_subsystem (self->priv->udev_device));

    g_free (path);
}

/*****************************************************************************/

void
mm_device_remove_modem (MMDevice  *self)
{
    if (!self->priv->modem)
        return;

    unexport_modem (self);

    /* Run dispose before unref-ing, in order to cleanup the SIM object,
     * if any (which also holds a reference to the modem object) */
    g_object_run_dispose (G_OBJECT (self->priv->modem));
    g_clear_object (&(self->priv->modem));
    g_clear_object (&(self->priv->object_manager));
}

/*****************************************************************************/

static void
modem_valid (MMBaseModem *modem,
             GParamSpec  *pspec,
             MMDevice    *self)
{
    if (!mm_base_modem_get_valid (modem)) {
        /* Modem no longer valid */
        mm_device_remove_modem (self);
    } else {
        /* Modem now valid, export it, but only if we really have it around.
         * It may happen that the initialization sequence fails because the
         * modem gets disconnected, and in that case we don't really need
         * to export it */
        if (self->priv->modem)
            export_modem (self);
        else
            mm_dbg ("Not exporting modem; no longer available");
    }
}

gboolean
mm_device_create_modem (MMDevice                  *self,
                        GDBusObjectManagerServer  *object_manager,
                        GError                   **error)
{
    g_assert (self->priv->modem == NULL);
    g_assert (self->priv->object_manager == NULL);

    if (!self->priv->port_probes) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Not creating a device without ports");
        return FALSE;
    }

    mm_info ("Creating modem with plugin '%s' and '%u' ports",
             mm_plugin_get_name (self->priv->plugin),
             g_list_length (self->priv->port_probes));

    self->priv->modem = mm_plugin_create_modem (self->priv->plugin, self, error);
    if (self->priv->modem) {
        /* Keep the object manager */
        self->priv->object_manager = g_object_ref (object_manager);

        /* We want to get notified when the modem becomes valid/invalid */
        g_signal_connect (self->priv->modem,
                          "notify::" MM_BASE_MODEM_VALID,
                          G_CALLBACK (modem_valid),
                          self);
    }

    return !!self->priv->modem;
}

/*****************************************************************************/

const gchar *
mm_device_get_path (MMDevice *self)
{
    return self->priv->udev_device_path;
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

GUdevDevice *
mm_device_peek_udev_device (MMDevice *self)
{
    return self->priv->udev_device;
}

GUdevDevice *
mm_device_get_udev_device (MMDevice *self)
{
    return G_UDEV_DEVICE (g_object_ref (self->priv->udev_device));
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
            g_object_ref (self->priv->plugin) :
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
mm_device_peek_port_probe (MMDevice *self,
                           GUdevDevice *udev_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, udev_port, FALSE);
    return (probe ? G_OBJECT (probe) : NULL);
}

GObject *
mm_device_get_port_probe (MMDevice *self,
                          GUdevDevice *udev_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, udev_port, FALSE);
    return (probe ? g_object_ref (probe) : NULL);
}

GList *
mm_device_peek_port_probe_list (MMDevice *self)
{
    return self->priv->port_probes;
}

GList *
mm_device_get_port_probe_list (MMDevice *self)
{
    GList *copy;

    copy = g_list_copy (self->priv->port_probes);
    g_list_foreach (copy, (GFunc)g_object_ref, NULL);
    return copy;
}

/*****************************************************************************/

MMDevice *
mm_device_new (GUdevDevice *udev_device)
{
    return MM_DEVICE (g_object_new (MM_TYPE_DEVICE,
                                    MM_DEVICE_UDEV_DEVICE, udev_device,
                                    NULL));
}

static void
mm_device_init (MMDevice *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_DEVICE,
                                              MMDevicePrivate);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMDevice *self = MM_DEVICE (object);

    switch (prop_id) {
    case PROP_UDEV_DEVICE:
        /* construct only */
        self->priv->udev_device = g_value_dup_object (value);
        self->priv->udev_device_path = g_strdup (g_udev_device_get_sysfs_path (self->priv->udev_device));
        break;
    case PROP_PLUGIN:
        g_clear_object (&(self->priv->plugin));
        self->priv->plugin = g_value_dup_object (value);
        break;
    case PROP_MODEM:
        g_clear_object (&(self->priv->modem));
        self->priv->modem = g_value_dup_object (value);
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
    case PROP_UDEV_DEVICE:
        g_value_set_object (value, self->priv->udev_device);
        break;
    case PROP_PLUGIN:
        g_value_set_object (value, self->priv->plugin);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
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

    g_clear_object (&(self->priv->udev_device));
    g_clear_object (&(self->priv->plugin));
    g_list_free_full (self->priv->port_probes, (GDestroyNotify)g_object_unref);
    g_list_free_full (self->priv->ignored_port_probes, (GDestroyNotify)g_object_unref);
    g_clear_object (&(self->priv->modem));

    G_OBJECT_CLASS (mm_device_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    MMDevice *self = MM_DEVICE (object);

    g_free (self->priv->udev_device_path);
    g_strfreev (self->priv->drivers);

    G_OBJECT_CLASS (mm_device_parent_class)->finalize (object);
}

static void
mm_device_class_init (MMDeviceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMDevicePrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_UDEV_DEVICE] =
        g_param_spec_object (MM_DEVICE_UDEV_DEVICE,
                             "UDev Device",
                             "UDev device object",
                             G_UDEV_TYPE_DEVICE,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_UDEV_DEVICE, properties[PROP_UDEV_DEVICE]);

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

    signals[SIGNAL_PORT_GRABBED] =
        g_signal_new (MM_DEVICE_PORT_GRABBED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMDeviceClass, port_grabbed),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_UDEV_TYPE_DEVICE);

    signals[SIGNAL_PORT_RELEASED] =
        g_signal_new (MM_DEVICE_PORT_RELEASED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMDeviceClass, port_released),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_UDEV_TYPE_DEVICE);
}
