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
 * Copyright (C) 2011 Google, Inc.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gudev/gudev.h>

#include <ModemManager.h>
#include <mm-errors-types.h>
#include <mm-gdbus-modem.h>

#include "mm-base-modem.h"

#include "mm-log.h"
#include "mm-serial-enums-types.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"

G_DEFINE_ABSTRACT_TYPE (MMBaseModem, mm_base_modem, MM_GDBUS_TYPE_OBJECT_SKELETON);

enum {
    PROP_0,
    PROP_VALID,
    PROP_MAX_TIMEOUTS,
    PROP_DEVICE,
    PROP_DRIVERS,
    PROP_PLUGIN,
    PROP_VENDOR_ID,
    PROP_PRODUCT_ID,
    PROP_CONNECTION,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseModemPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;

    /* Modem-wide cancellable. If it ever gets cancelled, no further operations
     * should be done by the modem. */
    GCancellable *cancellable;
    gulong invalid_if_cancelled;

    gchar *device;
    gchar **drivers;
    gchar *plugin;

    guint vendor_id;
    guint product_id;

    gboolean hotplugged;
    gboolean valid;

    guint max_timeouts;

    /* The authorization provider */
    MMAuthProvider *authp;
    GCancellable *authp_cancellable;

    GHashTable *ports;
    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMQcdmSerialPort *qcdm;
    GList *data;

    /* GPS-enabled modems will have an AT port for control, and a raw serial
     * port to receive all GPS traces */
    MMAtSerialPort *gps_control;
    MMGpsSerialPort *gps;

#if defined WITH_QMI
    /* QMI ports */
    GList *qmi;
#endif

#if defined WITH_MBIM
    /* MBIM ports */
    GList *mbim;
#endif
};

static gchar *
get_hash_key (const gchar *subsys,
              const gchar *name)
{
    return g_strdup_printf ("%s%s", subsys, name);
}

MMPort *
mm_base_modem_get_port (MMBaseModem *self,
                        const gchar *subsys,
                        const gchar *name)
{
    MMPort *port;
    gchar *key;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (subsys != NULL, NULL);

    /* Only 'net' or 'tty' should be given */
    g_return_val_if_fail (g_str_equal (subsys, "net") ||
                          g_str_equal (subsys, "tty"),
                          NULL);

    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (self->priv->ports, key);
    g_free (key);

    return port;
}

gboolean
mm_base_modem_owns_port (MMBaseModem *self,
                         const gchar *subsys,
                         const gchar *name)
{
    return !!mm_base_modem_get_port (self, subsys, name);
}

static void
serial_port_timed_out_cb (MMSerialPort *port,
                          guint n_consecutive_timeouts,
                          gpointer user_data)
{
    MMBaseModem *self = (MM_BASE_MODEM (user_data));

    if (self->priv->max_timeouts > 0 &&
        n_consecutive_timeouts >= self->priv->max_timeouts) {
        mm_warn ("(%s/%s) port timed out %u times, marking modem '%s' as disabled",
                 mm_port_type_get_string (mm_port_get_port_type (MM_PORT (port))),
                 mm_port_get_device (MM_PORT (port)),
                 n_consecutive_timeouts,
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (self)));

        /* Only set action to invalidate modem if not already done */
        g_cancellable_cancel (self->priv->cancellable);
    }
}

gboolean
mm_base_modem_grab_port (MMBaseModem *self,
                         const gchar *subsys,
                         const gchar *name,
                         MMPortType ptype,
                         MMAtPortFlag at_pflags,
                         GError **error)
{
    MMPort *port;
    gchar *key;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);
    g_return_val_if_fail (subsys != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    /* Only allow 'tty', 'net' and 'cdc-wdm' ports */
    if (!g_str_equal (subsys, "net") &&
        !g_str_equal (subsys, "tty") &&
        !(g_str_has_prefix (subsys, "usb") && g_str_has_prefix (name, "cdc-wdm"))) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot add port '%s/%s', unhandled subsystem",
                     subsys,
                     name);
        return FALSE;
    }

    /* Check whether we already have it stored */
    key = get_hash_key (subsys, name);
    port = g_hash_table_lookup (self->priv->ports, key);
    if (port) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot add port '%s/%s', already exists",
                     subsys,
                     name);
        g_free (key);
        return FALSE;
    }

    /* Serial ports... */
    if (g_str_equal (subsys, "tty")) {
        if (ptype == MM_PORT_TYPE_QCDM)
            /* QCDM port */
            port = MM_PORT (mm_qcdm_serial_port_new (name));
        else if (ptype == MM_PORT_TYPE_AT) {
            /* AT port */
            port = MM_PORT (mm_at_serial_port_new (name));

            /* Set common response parser */
            mm_at_serial_port_set_response_parser (MM_AT_SERIAL_PORT (port),
                                                   mm_serial_parser_v1_parse,
                                                   mm_serial_parser_v1_new (),
                                                   mm_serial_parser_v1_destroy);
            /* Store flags already */
            mm_at_serial_port_set_flags (MM_AT_SERIAL_PORT (port), at_pflags);
        } else if (ptype == MM_PORT_TYPE_GPS) {
            /* Raw GPS port */
            port = MM_PORT (mm_gps_serial_port_new (name));
        } else {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_UNSUPPORTED,
                         "Cannot add port '%s/%s', unhandled serial type",
                         subsys,
                         name);
            g_free (key);
            return FALSE;
        }

        /* For serial ports, enable port timeout checks */
        g_signal_connect (port,
                          "timed-out",
                          G_CALLBACK (serial_port_timed_out_cb),
                          self);
    }
    /* Net ports... */
    else if (g_str_equal (subsys, "net")) {
        port = MM_PORT (g_object_new (MM_TYPE_PORT,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_NET,
                                      MM_PORT_TYPE, MM_PORT_TYPE_NET,
                                      NULL));
    }
    /* cdc-wdm ports... */
    else if (g_str_has_prefix (subsys, "usb") &&
             g_str_has_prefix (name, "cdc-wdm")) {
#if defined WITH_QMI
        if (ptype == MM_PORT_TYPE_QMI)
            port = MM_PORT (mm_qmi_port_new (name));
#endif
#if defined WITH_MBIM
        if (!port && ptype == MM_PORT_TYPE_MBIM)
            port = MM_PORT (mm_mbim_port_new (name));
#endif
        if (!port) {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_UNSUPPORTED,
                         "Cannot add port '%s/%s', unsupported",
                         subsys,
                         name);
            g_free (key);
            return FALSE;
        }
    }
    else
        /* We already filter out before all non-tty, non-net, non-cdc-wdm ports */
        g_assert_not_reached();

    mm_dbg ("(%s) type '%s' claimed by %s",
            name,
            mm_port_type_get_string (ptype),
            mm_base_modem_get_device (self));

    /* Add it to the tracking HT.
     * Note: 'key' and 'port' now owned by the HT. */
    g_hash_table_insert (self->priv->ports, key, port);

    return TRUE;
}

void
mm_base_modem_release_port (MMBaseModem *self,
                            const gchar *subsys,
                            const gchar *name)
{
    gchar *key;
    MMPort *port;
    GList *l;

    g_return_if_fail (MM_IS_BASE_MODEM (self));
    g_return_if_fail (name != NULL);
    g_return_if_fail (subsys != NULL);

    if (!g_str_equal (subsys, "tty") &&
        !g_str_equal (subsys, "net"))
        return;

    key = get_hash_key (subsys, name);

    /* Find the port */
    port = g_hash_table_lookup (self->priv->ports, key);
    if (!port) {
        mm_warn ("(%s/%s): cannot release port, not found",
                 subsys, name);
        g_free (key);
        return;
    }

    if (port == (MMPort *)self->priv->primary) {
        /* Cancel modem-wide cancellable; no further actions can be done
         * without a primary port. */
        g_cancellable_cancel (self->priv->cancellable);

        g_clear_object (&self->priv->primary);
    }

    l = g_list_find (self->priv->data, port);
    if (l) {
        g_object_unref (l->data);
        self->priv->data = g_list_delete_link (self->priv->data, l);
    }

    if (port == (MMPort *)self->priv->secondary)
        g_clear_object (&self->priv->secondary);

    if (port == (MMPort *)self->priv->qcdm)
        g_clear_object (&self->priv->qcdm);

    if (port == (MMPort *)self->priv->gps_control)
        g_clear_object (&self->priv->gps_control);

    if (port == (MMPort *)self->priv->gps)
        g_clear_object (&self->priv->gps);

#if defined WITH_QMI
    l = g_list_find (self->priv->qmi, port);
    if (l) {
        g_object_unref (l->data);
        self->priv->qmi = g_list_delete_link (self->priv->qmi, l);
    }
#endif

#if defined WITH_MBIM
    l = g_list_find (self->priv->mbim, port);
    if (l) {
        g_object_unref (l->data);
        self->priv->mbim = g_list_delete_link (self->priv->mbim, l);
    }
#endif

    /* Remove it from the tracking HT */
    mm_dbg ("(%s/%s) type %s released from %s",
            subsys,
            name,
            mm_port_type_get_string (mm_port_get_port_type (port)),
            mm_port_get_device (port));
    g_hash_table_remove (self->priv->ports, key);
    g_free (key);
}

gboolean
mm_base_modem_disable_finish (MMBaseModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    return MM_BASE_MODEM_GET_CLASS (self)->disable_finish (self, res, error);
}

void
mm_base_modem_disable (MMBaseModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->disable != NULL);
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->disable_finish != NULL);

    MM_BASE_MODEM_GET_CLASS (self)->disable (
        self,
        self->priv->cancellable,
        callback,
        user_data);
}

gboolean
mm_base_modem_enable_finish (MMBaseModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return MM_BASE_MODEM_GET_CLASS (self)->enable_finish (self, res, error);
}

void
mm_base_modem_enable (MMBaseModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->enable != NULL);
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->enable_finish != NULL);

    MM_BASE_MODEM_GET_CLASS (self)->enable (
        self,
        self->priv->cancellable,
        callback,
        user_data);
}

gboolean
mm_base_modem_initialize_finish (MMBaseModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return MM_BASE_MODEM_GET_CLASS (self)->initialize_finish (self, res, error);
}

void
mm_base_modem_initialize (MMBaseModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->initialize != NULL);
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->initialize_finish != NULL);

    MM_BASE_MODEM_GET_CLASS (self)->initialize (
        self,
        self->priv->cancellable,
        callback,
        user_data);
}

void
mm_base_modem_set_hotplugged (MMBaseModem *self,
                              gboolean hotplugged)
{
    g_return_if_fail (MM_IS_BASE_MODEM (self));

    self->priv->hotplugged = hotplugged;
}

gboolean
mm_base_modem_get_hotplugged (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);

    return self->priv->hotplugged;
}

void
mm_base_modem_set_valid (MMBaseModem *self,
                         gboolean new_valid)
{
    g_return_if_fail (MM_IS_BASE_MODEM (self));

    /* If validity changed OR if both old and new were invalid, notify. This
     * last case is to cover failures during initialization. */
    if (self->priv->valid != new_valid ||
        !new_valid) {
        self->priv->valid = new_valid;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALID]);
    }
}

gboolean
mm_base_modem_get_valid (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);

    return self->priv->valid;
}

GCancellable *
mm_base_modem_peek_cancellable (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->cancellable;
}

GCancellable *
mm_base_modem_get_cancellable  (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return g_object_ref (self->priv->cancellable);
}

MMAtSerialPort *
mm_base_modem_get_port_primary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->primary ? g_object_ref (self->priv->primary) : NULL);
}

MMAtSerialPort *
mm_base_modem_peek_port_primary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->primary;
}

MMAtSerialPort *
mm_base_modem_get_port_secondary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->secondary ? g_object_ref (self->priv->secondary) : NULL);
}

MMAtSerialPort *
mm_base_modem_peek_port_secondary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->secondary;
}

MMQcdmSerialPort *
mm_base_modem_get_port_qcdm (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->qcdm ? g_object_ref (self->priv->qcdm) : NULL);
}

MMQcdmSerialPort *
mm_base_modem_peek_port_qcdm (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->qcdm;
}

MMAtSerialPort *
mm_base_modem_get_port_gps_control (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->gps_control ? g_object_ref (self->priv->gps_control) : NULL);
}

MMAtSerialPort *
mm_base_modem_peek_port_gps_control (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->gps_control;
}

MMGpsSerialPort *
mm_base_modem_get_port_gps (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->gps ? g_object_ref (self->priv->gps) : NULL);
}

MMGpsSerialPort *
mm_base_modem_peek_port_gps (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->gps;
}

#if defined WITH_QMI

MMQmiPort *
mm_base_modem_get_port_qmi (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    /* First QMI port in the list is the primary one always */
    return (self->priv->qmi ? ((MMQmiPort *)g_object_ref (self->priv->qmi->data)) : NULL);
}

MMQmiPort *
mm_base_modem_peek_port_qmi (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    /* First QMI port in the list is the primary one always */
    return (self->priv->qmi ? (MMQmiPort *)self->priv->qmi->data : NULL);
}

MMQmiPort *
mm_base_modem_get_port_qmi_for_data (MMBaseModem *self,
                                     MMPort *data,
                                     GError **error)
{
    MMQmiPort *qmi;

    qmi = mm_base_modem_peek_port_qmi_for_data (self, data, error);
    return (qmi ? (MMQmiPort *)g_object_ref (qmi) : NULL);
}

MMQmiPort *
mm_base_modem_peek_port_qmi_for_data (MMBaseModem *self,
                                      MMPort *data,
                                      GError **error)
{
    MMQmiPort *found;
    GUdevClient *client;
    GUdevDevice *data_device;
    GUdevDevice *data_device_parent;
    GList *l;

    if (mm_port_get_subsys (data) != MM_PORT_SUBSYS_NET) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot look for QMI port associated to a non-net data port");
        return NULL;
    }

    /* don't listen for uevents */
    client = g_udev_client_new (NULL);

    /* Get udev device for the data port */
    data_device = (g_udev_client_query_by_subsystem_and_name (
                       client,
                       "net",
                       mm_port_get_device (data)));
    if (!data_device) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't find udev device for port 'net/%s'",
                     mm_port_get_device (data));
        g_object_unref (client);
        return NULL;
    }

    /* Get parent of the data device */
    data_device_parent = g_udev_device_get_parent (data_device);
    if (!data_device_parent) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't get udev device parent for port 'net/%s'",
                     mm_port_get_device (data));
        g_object_unref (data_device);
        g_object_unref (client);
        return NULL;
    }

    /* Now walk the list of QMI ports looking for a match */
    found = NULL;
    for (l = self->priv->qmi; l && !found; l = g_list_next (l)) {
        GUdevDevice *qmi_device;
        GUdevDevice *qmi_device_parent;

        /* Get udev device for the QMI port */
        qmi_device = (g_udev_client_query_by_subsystem_and_name (
                          client,
                          "usb",
                          mm_port_get_device (MM_PORT (l->data))));
        if (!qmi_device) {
            qmi_device = (g_udev_client_query_by_subsystem_and_name (
                              client,
                              "usbmisc",
                              mm_port_get_device (MM_PORT (l->data))));
            if (!qmi_device) {
                mm_warn ("Couldn't get udev device for QMI port '%s'",
                         mm_port_get_device (MM_PORT (l->data)));
                continue;
            }
        }

        /* Get parent of the QMI device */
        qmi_device_parent = g_udev_device_get_parent (qmi_device);
        g_object_unref (qmi_device);

        if (!data_device_parent) {
            mm_warn ("Couldn't get udev device parent for QMI port '%s'",
                     mm_port_get_device (MM_PORT (l->data)));
            continue;
        }

        if (g_str_equal (g_udev_device_get_sysfs_path (data_device_parent),
                         g_udev_device_get_sysfs_path (qmi_device_parent)))
            found = MM_QMI_PORT (l->data);

        g_object_unref (qmi_device_parent);
    }

    g_object_unref (data_device_parent);
    g_object_unref (data_device);
    g_object_unref (client);

    if (!found) {
        /* For the case where we have only 1 data port and 1 QMI port and they
         * don't match with the previous rules (e.g. in some Huawei modems),
         * just return the found one */
        if (g_list_length (self->priv->data) == 1 &&
            g_list_length (self->priv->qmi) == 1 &&
            self->priv->data->data == data) {
            mm_info ("Assuming QMI port '%s' is associated to net/%s",
                     mm_port_get_device (MM_PORT (self->priv->qmi->data)),
                     mm_port_get_device (data));
            found = MM_QMI_PORT (self->priv->qmi->data);
        } else {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_NOT_FOUND,
                         "Couldn't find associated QMI port for 'net/%s'",
                         mm_port_get_device (data));
            return NULL;
        }
    }

    return found;
}

#endif /* WITH_QMI */

#if defined WITH_MBIM

MMMbimPort *
mm_base_modem_get_port_mbim (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    /* First MBIM port in the list is the primary one always */
    return (self->priv->mbim ? ((MMMbimPort *)g_object_ref (self->priv->mbim->data)) : NULL);
}

MMMbimPort *
mm_base_modem_peek_port_mbim (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    /* First MBIM port in the list is the primary one always */
    return (self->priv->mbim ? (MMMbimPort *)self->priv->mbim->data : NULL);
}

MMMbimPort *
mm_base_modem_get_port_mbim_for_data (MMBaseModem *self,
                                      MMPort *data,
                                      GError **error)
{
    MMMbimPort *mbim;

    mbim = mm_base_modem_peek_port_mbim_for_data (self, data, error);
    return (mbim ? (MMMbimPort *)g_object_ref (mbim) : NULL);
}

MMMbimPort *
mm_base_modem_peek_port_mbim_for_data (MMBaseModem *self,
                                       MMPort *data,
                                       GError **error)
{
    MMMbimPort *found;
    GUdevClient *client;
    GUdevDevice *data_device;
    GUdevDevice *data_device_parent;
    GList *l;

    if (mm_port_get_subsys (data) != MM_PORT_SUBSYS_NET) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot look for MBIM port associated to a non-net data port");
        return NULL;
    }

    /* don't listen for uevents */
    client = g_udev_client_new (NULL);

    /* Get udev device for the data port */
    data_device = (g_udev_client_query_by_subsystem_and_name (
                       client,
                       "net",
                       mm_port_get_device (data)));
    if (!data_device) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't find udev device for port 'net/%s'",
                     mm_port_get_device (data));
        g_object_unref (client);
        return NULL;
    }

    /* Get parent of the data device */
    data_device_parent = g_udev_device_get_parent (data_device);
    if (!data_device_parent) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Couldn't get udev device parent for port 'net/%s'",
                     mm_port_get_device (data));
        g_object_unref (data_device);
        g_object_unref (client);
        return NULL;
    }

    /* Now walk the list of MBIM ports looking for a match */
    found = NULL;
    for (l = self->priv->mbim; l && !found; l = g_list_next (l)) {
        GUdevDevice *mbim_device;
        GUdevDevice *mbim_device_parent;

        /* Get udev device for the MBIM port */
        mbim_device = (g_udev_client_query_by_subsystem_and_name (
                          client,
                          "usb",
                          mm_port_get_device (MM_PORT (l->data))));
        if (!mbim_device) {
            mbim_device = (g_udev_client_query_by_subsystem_and_name (
                              client,
                              "usbmisc",
                              mm_port_get_device (MM_PORT (l->data))));
            if (!mbim_device) {
                mm_warn ("Couldn't get udev device for MBIM port '%s'",
                         mm_port_get_device (MM_PORT (l->data)));
                continue;
            }
        }

        /* Get parent of the MBIM device */
        mbim_device_parent = g_udev_device_get_parent (mbim_device);
        g_object_unref (mbim_device);

        if (!data_device_parent) {
            mm_warn ("Couldn't get udev device parent for MBIM port '%s'",
                     mm_port_get_device (MM_PORT (l->data)));
            continue;
        }

        if (g_str_equal (g_udev_device_get_sysfs_path (data_device_parent),
                         g_udev_device_get_sysfs_path (mbim_device_parent)))
            found = MM_MBIM_PORT (l->data);

        g_object_unref (mbim_device_parent);
    }

    g_object_unref (data_device_parent);
    g_object_unref (data_device);
    g_object_unref (client);

    if (!found) {
        /* For the case where we have only 1 data port and 1 MBIM port and they
         * don't match with the previous rules (e.g. in some Huawei modems),
         * just return the found one */
        if (g_list_length (self->priv->data) == 1 &&
            g_list_length (self->priv->mbim) == 1 &&
            self->priv->data->data == data) {
            mm_info ("Assuming MBIM port '%s' is associated to net/%s",
                     mm_port_get_device (MM_PORT (self->priv->mbim->data)),
                     mm_port_get_device (data));
            found = MM_MBIM_PORT (self->priv->mbim->data);
        } else {
            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_NOT_FOUND,
                         "Couldn't find associated MBIM port for 'net/%s'",
                         mm_port_get_device (data));
            return NULL;
        }
    }

    return found;
}

#endif /* WITH_MBIM */

MMPort *
mm_base_modem_get_best_data_port (MMBaseModem *self,
                                  MMPortType type)
{
    MMPort *port;

    port = mm_base_modem_peek_best_data_port (self, type);
    return (port ? g_object_ref (port) : NULL);
}

MMPort *
mm_base_modem_peek_best_data_port (MMBaseModem *self,
                                   MMPortType type)
{
    GList *l;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    /* Return first not-connected data port */
    for (l = self->priv->data; l; l = g_list_next (l)) {
        if (!mm_port_get_connected ((MMPort *)l->data) &&
            (mm_port_get_port_type ((MMPort *)l->data) == type ||
             type == MM_PORT_TYPE_UNKNOWN)) {
            return (MMPort *)l->data;
        }
    }

    return NULL;
}

GList *
mm_base_modem_get_data_ports (MMBaseModem *self)
{
    GList *copy;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    copy = g_list_copy (self->priv->data);
    g_list_foreach (copy, (GFunc)g_object_ref, NULL);
    return copy;
}

GList *
mm_base_modem_peek_data_ports (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->data;
}

MMAtSerialPort *
mm_base_modem_get_best_at_port (MMBaseModem *self,
                                GError **error)
{
    MMAtSerialPort *best;

    best = mm_base_modem_peek_best_at_port (self, error);
    return (best ? g_object_ref (best) : NULL);
}

MMAtSerialPort *
mm_base_modem_peek_best_at_port (MMBaseModem *self,
                                 GError **error)
{
    /* Decide which port to use */
    if (self->priv->primary &&
        !mm_port_get_connected (MM_PORT (self->priv->primary)))
        return self->priv->primary;

    /* If primary port is connected, check if we can get the secondary
     * port */
    if (self->priv->secondary &&
        !mm_port_get_connected (MM_PORT (self->priv->secondary)))
        return self->priv->secondary;

    /* Otherwise, we cannot get any port */
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_CONNECTED,
                 "No AT port available to run command");
    return NULL;
}

gboolean
mm_base_modem_has_at_port (MMBaseModem *self)
{
    GHashTableIter iter;
    gpointer value;
    gpointer key;

    /* We'll iterate the ht of ports, looking for any port which is AT */
    g_hash_table_iter_init (&iter, self->priv->ports);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        if (MM_IS_AT_SERIAL_PORT (value))
            return TRUE;
    }

    return FALSE;
}

static void
initialize_ready (MMBaseModem *self,
                  GAsyncResult *res)
{
    GError *error = NULL;

    if (mm_base_modem_initialize_finish (self, res, &error)) {
        mm_dbg ("modem properly initialized");
        mm_base_modem_set_valid (self, TRUE);
        return;
    }

    /* Wrong state is returned when modem is found locked */
    if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE)) {
        /* Even with initialization errors, we do set the state to valid, so
         * that the modem gets exported and the failure notified to the user.
         */
        mm_dbg ("Couldn't finish initialization in the current state: '%s'",
                error->message);
        g_error_free (error);
        mm_base_modem_set_valid (self, TRUE);
        return;
    }

    /* Really fatal, we cannot even export the failed modem (e.g. error before
     * even trying to enable the Modem interface */
    mm_warn ("couldn't initialize the modem: '%s'", error->message);
    g_error_free (error);
}

static inline void
log_port (MMBaseModem *self, MMPort *port, const char *desc)
{
    if (port) {
        mm_dbg ("(%s) %s/%s %s",
                self->priv->device,
                mm_port_subsys_get_string (mm_port_get_subsys (port)),
                mm_port_get_device (port),
                desc);
    }
}

gboolean
mm_base_modem_organize_ports (MMBaseModem *self,
                              GError **error)
{
    GHashTableIter iter;
    MMPort *candidate;
    MMAtPortFlag flags;
    MMAtSerialPort *backup_primary = NULL;
    MMAtSerialPort *primary = NULL;
    MMAtSerialPort *secondary = NULL;
    MMAtSerialPort *backup_secondary = NULL;
    MMQcdmSerialPort *qcdm = NULL;
    MMAtSerialPort *gps_control = NULL;
    MMGpsSerialPort *gps = NULL;
    MMPort *data_primary = NULL;
    GList *data = NULL;
#if defined WITH_QMI
    MMPort *qmi_primary = NULL;
    GList *qmi = NULL;
#endif
#if defined WITH_MBIM
    MMPort *mbim_primary = NULL;
    GList *mbim = NULL;
#endif
    GList *l;

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);

    /* If ports have already been organized, just return success */
    if (self->priv->primary)
        return TRUE;

    g_hash_table_iter_init (&iter, self->priv->ports);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &candidate)) {
        switch (mm_port_get_port_type (candidate)) {

        case MM_PORT_TYPE_AT:
            g_assert (MM_IS_AT_SERIAL_PORT (candidate));
            flags = mm_at_serial_port_get_flags (MM_AT_SERIAL_PORT (candidate));

            if (flags & MM_AT_PORT_FLAG_PRIMARY) {
                if (!primary)
                    primary = MM_AT_SERIAL_PORT (candidate);
                else if (!backup_primary) {
                    /* Just in case the plugin gave us more than one primary
                     * and no secondaries, treat additional primary ports as
                     * secondary.
                     */
                    backup_primary = MM_AT_SERIAL_PORT (candidate);
                }
            }

            if (flags & MM_AT_PORT_FLAG_PPP) {
                if (!data_primary)
                    data_primary = candidate;
                else
                    data = g_list_append (data, candidate);
            }

            /* Explicitly flagged secondary ports trump NONE ports for secondary */
            if (flags & MM_AT_PORT_FLAG_SECONDARY) {
                if (!secondary || !(mm_at_serial_port_get_flags (secondary) & MM_AT_PORT_FLAG_SECONDARY))
                    secondary = MM_AT_SERIAL_PORT (candidate);
            }

            if (flags & MM_AT_PORT_FLAG_GPS_CONTROL) {
                if (!gps_control)
                    gps_control = MM_AT_SERIAL_PORT (candidate);
            }

            /* Fallback secondary */
            if (flags == MM_AT_PORT_FLAG_NONE) {
                if (!secondary)
                    secondary = MM_AT_SERIAL_PORT (candidate);
                else if (!backup_secondary)
                    backup_secondary = MM_AT_SERIAL_PORT (candidate);
            }
            break;

        case MM_PORT_TYPE_QCDM:
            g_assert (MM_IS_QCDM_SERIAL_PORT (candidate));
            if (!qcdm)
                qcdm = MM_QCDM_SERIAL_PORT (candidate);
            break;

        case MM_PORT_TYPE_NET:
            if (!data_primary)
                data_primary = candidate;
            else if (MM_IS_AT_SERIAL_PORT (data_primary)) {
                /* Net device (if any) is the preferred data port */
                data = g_list_append (data, data_primary);
                data_primary = candidate;
            }
            else
                /* All non-primary net ports get added to the list of data ports */
                data = g_list_append (data, candidate);
            break;

        case MM_PORT_TYPE_GPS:
            g_assert (MM_IS_GPS_SERIAL_PORT (candidate));
            if (!gps)
                gps = MM_GPS_SERIAL_PORT (candidate);
            break;

#if defined WITH_QMI
        case MM_PORT_TYPE_QMI:
            if (!qmi_primary)
                qmi_primary = candidate;
            else
                /* All non-primary QMI ports get added to the list of QMI ports */
                qmi = g_list_append (qmi, candidate);
            break;
#endif

#if defined WITH_MBIM
        case MM_PORT_TYPE_MBIM:
            if (!mbim_primary)
                mbim_primary = candidate;
            else
                /* All non-primary MBIM ports get added to the list of MBIM ports */
                mbim = g_list_append (mbim, candidate);
            break;
#endif

        default:
            /* Ignore port */
            break;
        }
    }

    /* Fall back to a secondary port if we didn't find a primary port */
    if (!primary) {
        if (!secondary) {
            gboolean allow_modem_without_at_port = FALSE;

#if defined WITH_QMI
            if (qmi_primary)
                allow_modem_without_at_port = TRUE;
#endif

#if defined WITH_MBIM
            if (mbim_primary)
                allow_modem_without_at_port = TRUE;
#endif

            if (!allow_modem_without_at_port) {
                g_set_error_literal (error,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Failed to find primary AT port");
                return FALSE;
            }
        } else  {
            primary = secondary;
            secondary = NULL;
        }
    }

    /* If the plugin didn't give us any secondary ports, use any additional
     * primary ports or backup secondary ports as secondary.
     */
    if (!secondary)
        secondary = backup_primary ? backup_primary : backup_secondary;

#if defined WITH_QMI
    /* On QMI-based modems, we need to have at least a net port */
    if (qmi_primary && !data_primary) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Failed to find a net port in the QMI modem");
        return FALSE;
    }
#endif

#if defined WITH_MBIM
    /* On MBIM-based modems, we need to have at least a net port */
    if (mbim_primary && !data_primary) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Failed to find a net port in the MBIM modem");
        return FALSE;
    }
#endif

    /* Data port defaults to primary AT port */
    if (!data_primary)
        data_primary = MM_PORT (primary);
    g_assert (data_primary);

    /* Reset flags on all ports; clear data port first since it might also
     * be the primary or secondary port.
     */
    if (MM_IS_AT_SERIAL_PORT (data_primary))
        mm_at_serial_port_set_flags (MM_AT_SERIAL_PORT (data_primary), MM_AT_PORT_FLAG_NONE);

    if (primary)
        mm_at_serial_port_set_flags (primary, MM_AT_PORT_FLAG_PRIMARY);
    if (secondary)
        mm_at_serial_port_set_flags (secondary, MM_AT_PORT_FLAG_SECONDARY);

    if (MM_IS_AT_SERIAL_PORT (data_primary)) {
        flags = mm_at_serial_port_get_flags (MM_AT_SERIAL_PORT (data_primary));
        mm_at_serial_port_set_flags (MM_AT_SERIAL_PORT (data_primary), flags | MM_AT_PORT_FLAG_PPP);
    }

    log_port (self, MM_PORT (primary),      "at (primary)");
    log_port (self, MM_PORT (secondary),    "at (secondary)");
    log_port (self, MM_PORT (data_primary), "data (primary)");
    for (l = data; l; l = g_list_next (l))
        log_port (self, MM_PORT (l->data),  "data (secondary)");
    log_port (self, MM_PORT (qcdm),         "qcdm");
    log_port (self, MM_PORT (gps_control),  "gps (control)");
    log_port (self, MM_PORT (gps),          "gps (nmea)");
#if defined WITH_QMI
    log_port (self, MM_PORT (qmi_primary),  "qmi (primary)");
    for (l = qmi; l; l = g_list_next (l))
        log_port (self, MM_PORT (l->data),  "qmi (secondary)");
#endif
#if defined WITH_MBIM
    log_port (self, MM_PORT (mbim_primary),  "mbim (primary)");
    for (l = mbim; l; l = g_list_next (l))
        log_port (self, MM_PORT (l->data),   "mbim (secondary)");
#endif

    /* We keep new refs to the objects here */
    self->priv->primary = (primary ? g_object_ref (primary) : NULL);
    self->priv->secondary = (secondary ? g_object_ref (secondary) : NULL);
    self->priv->qcdm = (qcdm ? g_object_ref (qcdm) : NULL);
    self->priv->gps_control = (gps_control ? g_object_ref (gps_control) : NULL);
    self->priv->gps = (gps ? g_object_ref (gps) : NULL);

    /* Build the final list of data ports, primary port first */
    self->priv->data = g_list_append (self->priv->data, g_object_ref (data_primary));
    g_list_foreach (data, (GFunc)g_object_ref, NULL);
    self->priv->data = g_list_concat (self->priv->data, data);

#if defined WITH_QMI
    /* Build the final list of QMI ports, primary port first */
    if (qmi_primary) {
        self->priv->qmi = g_list_append (self->priv->qmi, g_object_ref (qmi_primary));
        g_list_foreach (qmi, (GFunc)g_object_ref, NULL);
        self->priv->qmi = g_list_concat (self->priv->qmi, qmi);
    }
#endif

#if defined WITH_MBIM
    /* Build the final list of MBIM ports, primary port first */
    if (mbim_primary) {
        self->priv->mbim = g_list_append (self->priv->mbim, g_object_ref (mbim_primary));
        g_list_foreach (mbim, (GFunc)g_object_ref, NULL);
        self->priv->mbim = g_list_concat (self->priv->mbim, mbim);
    }
#endif

    /* As soon as we get the ports organized, we initialize the modem */
    mm_base_modem_initialize (self,
                              (GAsyncReadyCallback)initialize_ready,
                              NULL);

    return TRUE;
}

/*****************************************************************************/
/* Authorization */

gboolean
mm_base_modem_authorize_finish (MMBaseModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
authorize_ready (MMAuthProvider *authp,
                 GAsyncResult *res,
                 GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_base_modem_authorize (MMBaseModem *self,
                         GDBusMethodInvocation *invocation,
                         const gchar *authorization,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_base_modem_authorize);
    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                authorization,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)authorize_ready,
                                result);
}

/*****************************************************************************/

const gchar *
mm_base_modem_get_device (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->device;
}

const gchar **
mm_base_modem_get_drivers (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (const gchar **)self->priv->drivers;
}

const gchar *
mm_base_modem_get_plugin (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->plugin;
}

guint
mm_base_modem_get_vendor_id  (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), 0);

    return self->priv->vendor_id;
}

guint
mm_base_modem_get_product_id (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), 0);

    return self->priv->product_id;
}

/*****************************************************************************/

static gboolean
base_modem_invalid_idle (MMBaseModem *self)
{
    /* Ensure the modem is set invalid if we get the modem-wide cancellable
     * cancelled */
    mm_base_modem_set_valid (self, FALSE);
    g_object_unref (self);
    return FALSE;
}

static void
base_modem_cancelled (GCancellable *cancellable,
                      MMBaseModem *self)
{
    /* NOTE: Don't call set_valid() directly here, do it in an idle, and ensure
     * that we pass a valid reference of the modem object as context. */
    g_idle_add ((GSourceFunc)base_modem_invalid_idle, g_object_ref (self));
}

/*****************************************************************************/

static void
mm_base_modem_init (MMBaseModem *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BASE_MODEM,
                                              MMBaseModemPrivate);

    /* Setup authorization provider */
    self->priv->authp = mm_auth_get_provider ();
    self->priv->authp_cancellable = g_cancellable_new ();

    /* Setup modem-wide cancellable */
    self->priv->cancellable = g_cancellable_new ();
    self->priv->invalid_if_cancelled =
        g_cancellable_connect (self->priv->cancellable,
                               G_CALLBACK (base_modem_cancelled),
                               self,
                               NULL);

    self->priv->ports = g_hash_table_new_full (g_str_hash,
                                               g_str_equal,
                                               g_free,
                                               g_object_unref);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBaseModem *self = MM_BASE_MODEM (object);

    switch (prop_id) {
    case PROP_VALID:
        mm_base_modem_set_valid (self, g_value_get_boolean (value));
        break;
    case PROP_MAX_TIMEOUTS:
        self->priv->max_timeouts = g_value_get_uint (value);
        break;
    case PROP_DEVICE:
        g_free (self->priv->device);
        self->priv->device = g_value_dup_string (value);
        break;
    case PROP_DRIVERS:
        g_strfreev (self->priv->drivers);
        self->priv->drivers = g_value_dup_boxed (value);
        break;
    case PROP_PLUGIN:
        g_free (self->priv->plugin);
        self->priv->plugin = g_value_dup_string (value);
        break;
    case PROP_VENDOR_ID:
        self->priv->vendor_id = g_value_get_uint (value);
        break;
    case PROP_PRODUCT_ID:
        self->priv->product_id = g_value_get_uint (value);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);
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
    MMBaseModem *self = MM_BASE_MODEM (object);

    switch (prop_id) {
    case PROP_VALID:
        g_value_set_boolean (value, self->priv->valid);
        break;
    case PROP_MAX_TIMEOUTS:
        g_value_set_uint (value, self->priv->max_timeouts);
        break;
    case PROP_DEVICE:
        g_value_set_string (value, self->priv->device);
        break;
    case PROP_DRIVERS:
        g_value_set_boxed (value, self->priv->drivers);
        break;
    case PROP_PLUGIN:
        g_value_set_string (value, self->priv->plugin);
        break;
    case PROP_VENDOR_ID:
        g_value_set_uint (value, self->priv->vendor_id);
        break;
    case PROP_PRODUCT_ID:
        g_value_set_uint (value, self->priv->product_id);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMBaseModem *self = MM_BASE_MODEM (object);

    /* TODO
     * mm_auth_provider_cancel_for_owner (self->priv->authp, object);
    */

    mm_dbg ("Modem (%s) '%s' completely disposed",
            self->priv->plugin,
            self->priv->device);

    g_free (self->priv->device);
    g_strfreev (self->priv->drivers);
    g_free (self->priv->plugin);

    G_OBJECT_CLASS (mm_base_modem_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMBaseModem *self = MM_BASE_MODEM (object);

    /* Cancel all ongoing auth requests */
    g_cancellable_cancel (self->priv->authp_cancellable);
    g_clear_object (&self->priv->authp_cancellable);
    g_clear_object (&self->priv->authp);

    /* Ensure we cancel any ongoing operation, but before
     * disconnect our own signal handler, or we'll end up with
     * another reference of the modem object around. */
    g_cancellable_disconnect (self->priv->cancellable,
                              self->priv->invalid_if_cancelled);
    g_cancellable_cancel (self->priv->cancellable);
    g_clear_object (&self->priv->cancellable);

    g_clear_object (&self->priv->primary);
    g_clear_object (&self->priv->secondary);
    g_list_free_full (self->priv->data, g_object_unref);
    self->priv->data = NULL;
    g_clear_object (&self->priv->qcdm);
    g_clear_object (&self->priv->gps_control);
    g_clear_object (&self->priv->gps);
#if defined WITH_QMI
    /* We need to close the QMI port cleanly when disposing the modem object,
     * otherwise the allocated CIDs will be kept allocated, and if we end up
     * allocating too many newer allocations will fail with client-ids-exhausted
     * errors. */
    g_list_foreach (self->priv->qmi, (GFunc)mm_qmi_port_close, NULL);
    g_list_free_full (self->priv->qmi, g_object_unref);
    self->priv->qmi = NULL;
#endif
#if defined WITH_MBIM
    /* We need to close the MBIM port cleanly when disposing the modem object */
    g_list_foreach (self->priv->mbim, (GFunc)mm_mbim_port_close, NULL);
    g_list_free_full (self->priv->mbim, g_object_unref);
    self->priv->mbim = NULL;
#endif

    if (self->priv->ports) {
        g_hash_table_destroy (self->priv->ports);
        self->priv->ports = NULL;
    }

    g_clear_object (&self->priv->connection);

    G_OBJECT_CLASS (mm_base_modem_parent_class)->dispose (object);
}

static void
mm_base_modem_class_init (MMBaseModemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBaseModemPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_MAX_TIMEOUTS] =
        g_param_spec_uint (MM_BASE_MODEM_MAX_TIMEOUTS,
                           "Max timeouts",
                           "Maximum number of consecutive timed out commands sent to "
                           "the modem before disabling it. If 0, this feature is disabled.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MAX_TIMEOUTS, properties[PROP_MAX_TIMEOUTS]);

    properties[PROP_VALID] =
        g_param_spec_boolean (MM_BASE_MODEM_VALID,
                              "Valid",
                              "Whether the modem is to be considered valid or not.",
                              FALSE,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_VALID, properties[PROP_VALID]);

    properties[PROP_DEVICE] =
        g_param_spec_string (MM_BASE_MODEM_DEVICE,
                             "Device",
                             "Master modem parent device of all the modem's ports",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DEVICE, properties[PROP_DEVICE]);

    properties[PROP_DRIVERS] =
        g_param_spec_boxed (MM_BASE_MODEM_DRIVERS,
                            "Drivers",
                            "Kernel drivers",
                            G_TYPE_STRV,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DRIVERS, properties[PROP_DRIVERS]);

    properties[PROP_PLUGIN] =
        g_param_spec_string (MM_BASE_MODEM_PLUGIN,
                             "Plugin",
                             "Name of the plugin managing this modem",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PLUGIN, properties[PROP_PLUGIN]);

    properties[PROP_VENDOR_ID] =
        g_param_spec_uint (MM_BASE_MODEM_VENDOR_ID,
                           "Hardware vendor ID",
                           "Hardware vendor ID. May be unknown for serial devices.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_VENDOR_ID, properties[PROP_VENDOR_ID]);

    properties[PROP_PRODUCT_ID] =
        g_param_spec_uint (MM_BASE_MODEM_PRODUCT_ID,
                           "Hardware product ID",
                           "Hardware product ID. May be unknown for serial devices.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PRODUCT_ID, properties[PROP_PRODUCT_ID]);

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BASE_MODEM_CONNECTION,
                             "Connection",
                             "GDBus connection to the system bus.",
                             G_TYPE_DBUS_CONNECTION,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_CONNECTION, properties[PROP_CONNECTION]);
}
