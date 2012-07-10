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
#include <mm-errors-types.h>

#include "mm-device.h"

#include "mm-log.h"

G_DEFINE_TYPE (MMDevice, mm_device, G_TYPE_OBJECT);

enum {
    PROP_0,
    PROP_UDEV_DEVICE,
    PROP_PLUGIN,
    PROP_MODEM,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMDevicePrivate {
    /* Parent UDev device */
    GUdevDevice *udev_device;

    /* Best plugin to manage this device */
    MMPlugin *plugin;

    /* List of ports in the device */
    GList *udev_ports;

    /* The Modem object for this device */
    MMBaseModem *modem;

    /* When exported, a reference to the object manager */
    GDBusObjectManagerServer *object_manager;
};

/*****************************************************************************/

static gint
udev_port_cmp (GUdevDevice *a,
               GUdevDevice *b)
{
    return strcmp (g_udev_device_get_sysfs_path (a),
                   g_udev_device_get_sysfs_path (b));
}

gboolean
mm_device_owns_port (MMDevice    *self,
                     GUdevDevice *udev_port)
{
    return !!g_list_find_custom (self->priv->udev_ports,
                                 udev_port,
                                 (GCompareFunc)udev_port_cmp);
}

void
mm_device_grab_port (MMDevice    *self,
                     GUdevDevice *udev_port)
{
    if (!g_list_find_custom (self->priv->udev_ports,
                             udev_port,
                             (GCompareFunc)udev_port_cmp)) {
        self->priv->udev_ports = g_list_prepend (self->priv->udev_ports,
                                                 g_object_ref (udev_port));
    }
}

void
mm_device_release_port (MMDevice    *self,
                        GUdevDevice *udev_port)
{
    GList *found;

    found = g_list_find_custom (self->priv->udev_ports,
                                udev_port,
                                (GCompareFunc)udev_port_cmp);
    if (found) {
        g_object_unref (found->data);
        self->priv->udev_ports = g_list_delete_link (self->priv->udev_ports, found);
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
        /* Modem now valid, export it */
        export_modem (self);
    }
}

gboolean
mm_device_create_modem (MMDevice                  *self,
                        GDBusObjectManagerServer  *object_manager,
                        GError                   **error)
{
    g_assert (self->priv->modem == NULL);
    g_assert (self->priv->object_manager == NULL);
    g_assert (self->priv->udev_ports != NULL);

    mm_info ("Creating modem with plugin '%s' and '%u' ports",
             mm_plugin_get_name (self->priv->plugin),
             g_list_length (self->priv->udev_ports));

    self->priv->modem = mm_plugin_create_modem (self->priv->plugin,
                                                self->priv->udev_ports,
                                                error);
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
                      MMPlugin *plugin)
{
    g_object_set (self,
                  MM_DEVICE_PLUGIN, plugin,
                  NULL);
}

MMPlugin *
mm_device_peek_plugin (MMDevice *self)
{
    return self->priv->plugin;
}

MMPlugin *
mm_device_get_plugin (MMDevice *self)
{
    return (self->priv->plugin ?
            MM_PLUGIN (g_object_ref (self->priv->plugin)) :
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
    g_list_free_full (self->priv->udev_ports, (GDestroyNotify)g_object_unref);
    g_clear_object (&(self->priv->modem));

    G_OBJECT_CLASS (mm_device_parent_class)->dispose (object);
}

static void
mm_device_class_init (MMDeviceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMDevicePrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
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
}
