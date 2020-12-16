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

#include <ModemManager.h>
#include <ModemManager-tags.h>

#include <mm-errors-types.h>
#include <mm-gdbus-modem.h>

#include "mm-context.h"
#include "mm-base-modem.h"

#include "mm-log-object.h"
#include "mm-port-enums-types.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"

static void log_object_iface_init (MMLogObjectInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MMBaseModem, mm_base_modem, MM_GDBUS_TYPE_OBJECT_SKELETON,
                                  G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init))

/* If we get 10 consecutive timeouts in a serial port, we consider the modem
 * invalid and we request re-probing. */
#define DEFAULT_MAX_TIMEOUTS 10

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
    PROP_REPROBE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBaseModemPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    guint            dbus_id;

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
    gboolean reprobe;

    guint max_timeouts;

    /* The authorization provider */
    MMAuthProvider *authp;
    GCancellable *authp_cancellable;

    GHashTable *ports;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;
    MMPortSerialQcdm *qcdm;
    GList *data;

    /* GPS-enabled modems will have an AT port for control, and a raw serial
     * port to receive all GPS traces */
    MMPortSerialAt *gps_control;
    MMPortSerialGps *gps;

    /* Some audio-capable devices will have a port for audio specifically */
    MMPortSerial *audio;

    /* Support for parallel enable/disable operations */
    GList *enable_tasks;
    GList *disable_tasks;

#if defined WITH_QMI
    /* QMI ports */
    GList *qmi;
#endif

#if defined WITH_MBIM
    /* MBIM ports */
    GList *mbim;
#endif
};

guint
mm_base_modem_get_dbus_id (MMBaseModem *self)
{
    return self->priv->dbus_id;
}

/******************************************************************************/

static void
serial_port_timed_out_cb (MMPortSerial *port,
                          guint         n_consecutive_timeouts,
                          MMBaseModem  *self)
{
    /* If reached the maximum number of timeouts, invalidate modem */
    if (n_consecutive_timeouts >= self->priv->max_timeouts) {
        mm_obj_err (self, "port %s timed out %u consecutive times, marking modem as invalid",
                    mm_port_get_device (MM_PORT (port)),
                    n_consecutive_timeouts);
        g_cancellable_cancel (self->priv->cancellable);
        return;
    }

    if (n_consecutive_timeouts > 1)
        mm_obj_warn (self, "port %s timed out %u consecutive times",
                     mm_port_get_device (MM_PORT (port)),
                     n_consecutive_timeouts);
}

static MMPort *
base_modem_create_ignored_port (MMBaseModem *self,
                                const gchar *name)
{
    return MM_PORT (g_object_new (MM_TYPE_PORT,
                                  MM_PORT_DEVICE, name,
                                  MM_PORT_TYPE,   MM_PORT_TYPE_IGNORED,
                                  NULL));
}

static MMPort *
base_modem_create_net_port (MMBaseModem *self,
                            const gchar *name)
{
    return MM_PORT (g_object_new (MM_TYPE_PORT,
                                  MM_PORT_DEVICE, name,
                                  MM_PORT_SUBSYS, MM_PORT_SUBSYS_NET,
                                  MM_PORT_TYPE,   MM_PORT_TYPE_NET,
                                  NULL));
}

static MMPort *
base_modem_create_tty_port (MMBaseModem        *self,
                            const gchar        *name,
                            MMKernelDevice     *kernel_device,
                            MMPortType          ptype)
{
    MMPort      *port = NULL;
    const gchar *flow_control_tag;

    if (ptype == MM_PORT_TYPE_QCDM)
        port = MM_PORT (mm_port_serial_qcdm_new (name, MM_PORT_SUBSYS_TTY));
    else if (ptype == MM_PORT_TYPE_GPS)
        port = MM_PORT (mm_port_serial_gps_new (name));
    else if (ptype == MM_PORT_TYPE_AUDIO)
        port = MM_PORT (mm_port_serial_new (name, ptype));
    else if (ptype == MM_PORT_TYPE_AT)
        port = MM_PORT (mm_port_serial_at_new (name, MM_PORT_SUBSYS_TTY));

    if (!port)
        return NULL;

    /* Enable port timeout checks if requested to do so */
    if (self->priv->max_timeouts > 0)
        g_signal_connect (port,
                          "timed-out",
                          G_CALLBACK (serial_port_timed_out_cb),
                          self);

    /* Optional user-provided baudrate */
    if (mm_kernel_device_has_property (kernel_device, ID_MM_TTY_BAUDRATE))
        g_object_set (port,
                      MM_PORT_SERIAL_BAUD, mm_kernel_device_get_property_as_int (kernel_device, ID_MM_TTY_BAUDRATE),
                      NULL);

    /* Optional user-provided flow control */
    flow_control_tag = mm_kernel_device_get_property (kernel_device, ID_MM_TTY_FLOW_CONTROL);
    if (flow_control_tag) {
        MMFlowControl     flow_control;
        g_autoptr(GError) inner_error = NULL;

        flow_control = mm_flow_control_from_string (flow_control_tag, &inner_error);
        if (flow_control != MM_FLOW_CONTROL_UNKNOWN)
            g_object_set (port,
                          MM_PORT_SERIAL_FLOW_CONTROL, flow_control,
                          NULL);
        else
            mm_obj_warn (self, "unsupported flow control settings in port %s: %s",
                         name, inner_error->message);
    }

    return port;
}

static MMPort *
base_modem_create_usbmisc_port (MMBaseModem *self,
                                const gchar *name,
                                MMPortType   ptype)
{
#if defined WITH_QMI
    if (ptype == MM_PORT_TYPE_QMI)
        return MM_PORT (mm_port_qmi_new (name, MM_PORT_SUBSYS_USBMISC));
#endif
#if defined WITH_MBIM
    if (ptype == MM_PORT_TYPE_MBIM)
        return MM_PORT (mm_port_mbim_new (name, MM_PORT_SUBSYS_USBMISC));
#endif
    if (ptype == MM_PORT_TYPE_AT)
        return MM_PORT (mm_port_serial_at_new (name, MM_PORT_SUBSYS_USBMISC));
    return NULL;
}

static MMPort *
base_modem_create_rpmsg_port (MMBaseModem *self,
                              const gchar *name,
                              MMPortType   ptype)
{
#if defined WITH_QMI
    if (ptype == MM_PORT_TYPE_QMI)
        return MM_PORT (mm_port_qmi_new (name, MM_PORT_SUBSYS_RPMSG));
#endif
    if (ptype == MM_PORT_TYPE_AT)
        return MM_PORT (mm_port_serial_at_new (name, MM_PORT_SUBSYS_RPMSG));
    return NULL;
}

static MMPort *
base_modem_create_wwan_port (MMBaseModem *self,
                             const gchar *name,
                             MMPortType   ptype)
{
#if defined WITH_QMI
    if (ptype == MM_PORT_TYPE_QMI)
        return MM_PORT (mm_port_qmi_new (name, MM_PORT_SUBSYS_WWAN));
#endif

#if defined WITH_MBIM
    if (ptype == MM_PORT_TYPE_MBIM)
        return MM_PORT (mm_port_mbim_new (name, MM_PORT_SUBSYS_WWAN));
#endif

    if (ptype == MM_PORT_TYPE_QCDM)
        return MM_PORT (mm_port_serial_qcdm_new (name, MM_PORT_SUBSYS_WWAN));

    if (ptype == MM_PORT_TYPE_AT)
        return MM_PORT (mm_port_serial_at_new (name, MM_PORT_SUBSYS_WWAN));

    return NULL;
}

static MMPort *
base_modem_create_virtual_port (MMBaseModem *self,
                                const gchar *name)
{
    return MM_PORT (mm_port_serial_at_new (name, MM_PORT_SUBSYS_UNIX));
}

gboolean
mm_base_modem_grab_port (MMBaseModem         *self,
                         MMKernelDevice      *kernel_device,
                         MMPortType           ptype,
                         MMPortSerialAtFlag   at_pflags,
                         GError             **error)
{
    MMPort           *port;
    const gchar      *subsys;
    const gchar      *name;
    g_autofree gchar *key = NULL;

    subsys = mm_kernel_device_get_subsystem (kernel_device);
    name   = mm_kernel_device_get_name      (kernel_device);

    /* Check whether we already have it stored */
    key  = g_strdup_printf ("%s%s", subsys, name);
    port = g_hash_table_lookup (self->priv->ports, key);
    if (port) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot add port '%s/%s', already exists", subsys, name);
        return FALSE;
    }

    /* Explicitly ignored ports, grab them but explicitly flag them as ignored
     * right away, all the same way (i.e. regardless of subsystem). */
    if (ptype == MM_PORT_TYPE_IGNORED)
        port = base_modem_create_ignored_port (self, name);
    else if (g_str_equal (subsys, "net"))
        port = base_modem_create_net_port (self, name);
    else if (g_str_equal (subsys, "tty"))
        port = base_modem_create_tty_port (self, name, kernel_device, ptype);
    else if (g_str_equal (subsys, "usbmisc"))
        port = base_modem_create_usbmisc_port (self, name, ptype);
    else if (g_str_equal (subsys, "rpmsg"))
        port = base_modem_create_rpmsg_port (self, name, ptype);
    else if (g_str_equal (subsys, "virtual"))
        port = base_modem_create_virtual_port (self, name);
    else if (g_str_equal (subsys, "wwan"))
        port = base_modem_create_wwan_port (self, name, ptype);

    if (!port) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot add port '%s/%s', unhandled port type", subsys, name);
        return FALSE;
    }

    /* Store kernel device */
    g_object_set (port, MM_PORT_KERNEL_DEVICE, kernel_device, NULL);

    /* Set owner ID */
    mm_log_object_set_owner_id (MM_LOG_OBJECT (port), mm_log_object_get_id (MM_LOG_OBJECT (self)));

    /* Common setup for all AT ports from all subsystems */
    if (MM_IS_PORT_SERIAL_AT (port)) {
        mm_port_serial_at_set_response_parser (MM_PORT_SERIAL_AT (port),
                                               mm_serial_parser_v1_parse,
                                               mm_serial_parser_v1_new (),
                                               mm_serial_parser_v1_destroy);
        /* Prefer plugin-provided flags to the generic ones */
        if (at_pflags == MM_PORT_SERIAL_AT_FLAG_NONE) {
            if (mm_kernel_device_get_property_as_boolean (kernel_device, ID_MM_PORT_TYPE_AT_PRIMARY)) {
                mm_obj_dbg (port, "AT port flagged as primary");
                at_pflags = MM_PORT_SERIAL_AT_FLAG_PRIMARY;
            } else if (mm_kernel_device_get_property_as_boolean (kernel_device, ID_MM_PORT_TYPE_AT_SECONDARY)) {
                mm_obj_dbg (port, "AT port flagged as secondary");
                at_pflags = MM_PORT_SERIAL_AT_FLAG_SECONDARY;
            } else if (mm_kernel_device_get_property_as_boolean (kernel_device, ID_MM_PORT_TYPE_AT_PPP)) {
                mm_obj_dbg (port, "AT port flagged as PPP");
                at_pflags = MM_PORT_SERIAL_AT_FLAG_PPP;
            }
        }
        /* The plugin may specify NONE_NO_GENERIC to avoid the generic
         * port type hints from being applied. */
        if (at_pflags == MM_PORT_SERIAL_AT_FLAG_NONE_NO_GENERIC)
            at_pflags = MM_PORT_SERIAL_AT_FLAG_NONE;

        mm_port_serial_at_set_flags (MM_PORT_SERIAL_AT (port), at_pflags);
    }

    /* Add it to the tracking HT.
     * Note: 'key' and 'port' now owned by the HT. */
    mm_obj_dbg (port, "port grabbed");
    g_hash_table_insert (self->priv->ports, g_steal_pointer (&key), port);
    return TRUE;
}

gboolean
mm_base_modem_disable_finish (MMBaseModem   *self,
                              GAsyncResult  *res,
                              GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_ready (MMBaseModem  *self,
               GAsyncResult *res)
{
    GError *error = NULL;
    GList  *l;
    GList  *disable_tasks;

    g_assert (self->priv->disable_tasks);
    disable_tasks = self->priv->disable_tasks;
    self->priv->disable_tasks = NULL;

    MM_BASE_MODEM_GET_CLASS (self)->disable_finish (self, res, &error);
    for (l = disable_tasks; l; l = g_list_next (l)) {
        if (error)
            g_task_return_error (G_TASK (l->data), g_error_copy (error));
        else
            g_task_return_boolean (G_TASK (l->data), TRUE);
    }
    g_clear_error (&error);

    g_list_free_full (disable_tasks, g_object_unref);
}

void
mm_base_modem_disable (MMBaseModem         *self,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
    GTask    *task;
    gboolean  run_disable;

    g_assert (MM_BASE_MODEM_GET_CLASS (self)->disable != NULL);
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->disable_finish != NULL);

    /* If the list of disable tasks is empty, we need to run */
    run_disable = !self->priv->disable_tasks;

    /* Store task */
    task = g_task_new (self, self->priv->cancellable, callback, user_data);
    self->priv->disable_tasks = g_list_append (self->priv->disable_tasks, task);

    if (!run_disable)
        return;

    MM_BASE_MODEM_GET_CLASS (self)->disable (
        self,
        self->priv->cancellable,
        (GAsyncReadyCallback) disable_ready,
        NULL);
}

gboolean
mm_base_modem_enable_finish (MMBaseModem   *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
enable_ready (MMBaseModem  *self,
              GAsyncResult *res)
{
    GError *error = NULL;
    GList  *l;
    GList  *enable_tasks;

    g_assert (self->priv->enable_tasks);
    enable_tasks = self->priv->enable_tasks;
    self->priv->enable_tasks = NULL;

    MM_BASE_MODEM_GET_CLASS (self)->enable_finish (self, res, &error);
    for (l = enable_tasks; l; l = g_list_next (l)) {
        if (error)
            g_task_return_error (G_TASK (l->data), g_error_copy (error));
        else
            g_task_return_boolean (G_TASK (l->data), TRUE);
    }
    g_clear_error (&error);

    g_list_free_full (enable_tasks, g_object_unref);
}

void
mm_base_modem_enable (MMBaseModem         *self,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    GTask    *task;
    gboolean  run_enable;

    g_assert (MM_BASE_MODEM_GET_CLASS (self)->enable != NULL);
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->enable_finish != NULL);

    /* If the list of enable tasks is empty, we need to run */
    run_enable = !self->priv->enable_tasks;

    /* Store task */
    task = g_task_new (self, self->priv->cancellable, callback, user_data);
    self->priv->enable_tasks = g_list_append (self->priv->enable_tasks, task);

    if (!run_enable)
        return;

    MM_BASE_MODEM_GET_CLASS (self)->enable (
        self,
        self->priv->cancellable,
        (GAsyncReadyCallback) enable_ready,
        NULL);
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

void
mm_base_modem_set_reprobe (MMBaseModem *self,
                           gboolean reprobe)
{
    g_return_if_fail (MM_IS_BASE_MODEM (self));

    self->priv->reprobe = reprobe;
}

gboolean
mm_base_modem_get_reprobe (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);

    return self->priv->reprobe;
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

MMPortSerialAt *
mm_base_modem_get_port_primary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->primary ? g_object_ref (self->priv->primary) : NULL);
}

MMPortSerialAt *
mm_base_modem_peek_port_primary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->primary;
}

MMPortSerialAt *
mm_base_modem_get_port_secondary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->secondary ? g_object_ref (self->priv->secondary) : NULL);
}

MMPortSerialAt *
mm_base_modem_peek_port_secondary (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->secondary;
}

MMPortSerialQcdm *
mm_base_modem_get_port_qcdm (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->qcdm ? g_object_ref (self->priv->qcdm) : NULL);
}

MMPortSerialQcdm *
mm_base_modem_peek_port_qcdm (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->qcdm;
}

MMPortSerialAt *
mm_base_modem_get_port_gps_control (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->gps_control ? g_object_ref (self->priv->gps_control) : NULL);
}

MMPortSerialAt *
mm_base_modem_peek_port_gps_control (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->gps_control;
}

MMPortSerialGps *
mm_base_modem_get_port_gps (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->gps ? g_object_ref (self->priv->gps) : NULL);
}

MMPortSerialGps *
mm_base_modem_peek_port_gps (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->gps;
}

MMPortSerial *
mm_base_modem_get_port_audio (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return (self->priv->audio ? g_object_ref (self->priv->audio) : NULL);
}

MMPortSerial *
mm_base_modem_peek_port_audio (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->audio;
}

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
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return g_list_copy_deep (self->priv->data, (GCopyFunc)g_object_ref, NULL);
}

GList *
mm_base_modem_peek_data_ports (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->data;
}

MMPortSerialAt *
mm_base_modem_get_best_at_port (MMBaseModem *self,
                                GError **error)
{
    MMPortSerialAt *best;

    best = mm_base_modem_peek_best_at_port (self, error);
    return (best ? g_object_ref (best) : NULL);
}

MMPortSerialAt *
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
        if (MM_IS_PORT_SERIAL_AT (value))
            return TRUE;
    }

    return FALSE;
}

static gint
port_info_cmp (const MMModemPortInfo *a,
               const MMModemPortInfo *b)
{
    /* default to alphabetical sorting on the port name */
    return g_strcmp0 (a->name, b->name);
}

MMModemPortInfo *
mm_base_modem_get_port_infos (MMBaseModem *self,
                              guint       *n_port_infos)
{
    GHashTableIter  iter;
    GArray         *port_infos;
    MMPort         *port;

    *n_port_infos = g_hash_table_size (self->priv->ports);
    port_infos = g_array_sized_new (FALSE, FALSE, sizeof (MMModemPortInfo), *n_port_infos);
    g_hash_table_iter_init (&iter, self->priv->ports);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer)&port)) {
        MMModemPortInfo port_info;

        port_info.name = g_strdup (mm_port_get_device (port));
        switch (mm_port_get_port_type (port)) {
        case MM_PORT_TYPE_NET:
            port_info.type = MM_MODEM_PORT_TYPE_NET;
            break;
        case MM_PORT_TYPE_AT:
            port_info.type = MM_MODEM_PORT_TYPE_AT;
            break;
        case MM_PORT_TYPE_QCDM:
            port_info.type = MM_MODEM_PORT_TYPE_QCDM;
            break;
        case MM_PORT_TYPE_GPS:
            port_info.type = MM_MODEM_PORT_TYPE_GPS;
            break;
        case MM_PORT_TYPE_AUDIO:
            port_info.type = MM_MODEM_PORT_TYPE_AUDIO;
            break;
        case MM_PORT_TYPE_QMI:
            port_info.type = MM_MODEM_PORT_TYPE_QMI;
            break;
        case MM_PORT_TYPE_MBIM:
            port_info.type = MM_MODEM_PORT_TYPE_MBIM;
            break;
        case MM_PORT_TYPE_IGNORED:
            port_info.type = MM_MODEM_PORT_TYPE_IGNORED;
            break;
        case MM_PORT_TYPE_UNKNOWN:
        default:
            port_info.type = MM_MODEM_PORT_TYPE_UNKNOWN;
            break;
        }

        g_array_append_val (port_infos, port_info);
    }

    g_assert (*n_port_infos == port_infos->len);
    g_array_sort (port_infos, (GCompareFunc) port_info_cmp);
    return (MMModemPortInfo *) g_array_free (port_infos, FALSE);
}

static gint
port_cmp (MMPort *a,
          MMPort *b)
{
    /* default to alphabetical sorting on the port name */
    return g_strcmp0 (mm_port_get_device (a), mm_port_get_device (b));
}

GList *
mm_base_modem_find_ports (MMBaseModem *self,
                          MMPortSubsys subsys,
                          MMPortType type,
                          const gchar *name)
{
    GList *out = NULL;
    GHashTableIter iter;
    gpointer value;
    gpointer key;

    if (!self->priv->ports)
        return NULL;

    /* We'll iterate the ht of ports, looking for any port which is matches
     * the compare function */
    g_hash_table_iter_init (&iter, self->priv->ports);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMPort *port = MM_PORT (value);

        if (subsys != MM_PORT_SUBSYS_UNKNOWN && mm_port_get_subsys (port) != subsys)
            continue;

        if (type != MM_PORT_TYPE_UNKNOWN && mm_port_get_port_type (port) != type)
            continue;

        if (name != NULL && !g_str_equal (mm_port_get_device (port), name))
            continue;

        out = g_list_append (out, g_object_ref (port));
    }

    return g_list_sort (out, (GCompareFunc) port_cmp);
}

static void
initialize_ready (MMBaseModem *self,
                  GAsyncResult *res)
{
    GError *error = NULL;

    if (mm_base_modem_initialize_finish (self, res, &error)) {
        mm_obj_dbg (self, "modem initialized");
        mm_base_modem_set_valid (self, TRUE);
        return;
    }

    /* Wrong state is returned when modem is found locked */
    if (g_error_matches (error, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE)) {
        /* Even with initialization errors, we do set the state to valid, so
         * that the modem gets exported and the failure notified to the user.
         */
        mm_obj_dbg (self, "couldn't finish initialization in the current state: '%s'", error->message);
        g_error_free (error);
        mm_base_modem_set_valid (self, TRUE);
        return;
    }

    /* Really fatal, we cannot even export the failed modem (e.g. error before
     * even trying to enable the Modem interface */
    mm_obj_warn (self, "couldn't initialize: '%s'", error->message);
    g_error_free (error);
}

static inline void
log_port (MMBaseModem *self, MMPort *port, const char *desc)
{
    if (port) {
        mm_obj_dbg (self, "%s/%s %s",
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
    MMPortSerialAtFlag flags;
    MMPortSerialAt *backup_primary = NULL;
    MMPortSerialAt *primary = NULL;
    MMPortSerialAt *secondary = NULL;
    MMPortSerialAt *backup_secondary = NULL;
    MMPortSerialQcdm *qcdm = NULL;
    MMPortSerialAt *gps_control = NULL;
    MMPortSerialGps *gps = NULL;
    MMPortSerial *audio = NULL;
    MMPortSerialAt *data_at_primary = NULL;
    GList *l;
    /* These lists don't keep full references, so they should be
     * g_list_free()-ed on error exits */
    g_autoptr(GList) data_at = NULL;
    g_autoptr(GList) data_net = NULL;
#if defined WITH_QMI
    g_autoptr(GList) qmi = NULL;
#endif
#if defined WITH_MBIM
    g_autoptr(GList) mbim = NULL;
#endif

    g_return_val_if_fail (MM_IS_BASE_MODEM (self), FALSE);

    /* If ports have already been organized, just return success */
    if (self->priv->primary)
        return TRUE;

    g_hash_table_iter_init (&iter, self->priv->ports);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &candidate)) {
        switch (mm_port_get_port_type (candidate)) {

        case MM_PORT_TYPE_AT:
            g_assert (MM_IS_PORT_SERIAL_AT (candidate));
            flags = mm_port_serial_at_get_flags (MM_PORT_SERIAL_AT (candidate));

            if (flags & MM_PORT_SERIAL_AT_FLAG_PRIMARY) {
                if (!primary)
                    primary = MM_PORT_SERIAL_AT (candidate);
                else if (!backup_primary) {
                    /* Just in case the plugin gave us more than one primary
                     * and no secondaries, treat additional primary ports as
                     * secondary.
                     */
                    backup_primary = MM_PORT_SERIAL_AT (candidate);
                }
            }

            if (flags & MM_PORT_SERIAL_AT_FLAG_PPP) {
                if (!data_at_primary)
                    data_at_primary = MM_PORT_SERIAL_AT (candidate);
                else
                    data_at = g_list_append (data_at, candidate);
            }

            /* Explicitly flagged secondary ports trump NONE ports for secondary */
            if (flags & MM_PORT_SERIAL_AT_FLAG_SECONDARY) {
                if (!secondary || !(mm_port_serial_at_get_flags (secondary) & MM_PORT_SERIAL_AT_FLAG_SECONDARY))
                    secondary = MM_PORT_SERIAL_AT (candidate);
            }

            if (flags & MM_PORT_SERIAL_AT_FLAG_GPS_CONTROL) {
                if (!gps_control)
                    gps_control = MM_PORT_SERIAL_AT (candidate);
            }

            /* Fallback secondary */
            if (flags == MM_PORT_SERIAL_AT_FLAG_NONE) {
                if (!secondary)
                    secondary = MM_PORT_SERIAL_AT (candidate);
                else if (!backup_secondary)
                    backup_secondary = MM_PORT_SERIAL_AT (candidate);
            }
            break;

        case MM_PORT_TYPE_QCDM:
            g_assert (MM_IS_PORT_SERIAL_QCDM (candidate));
            if (!qcdm)
                qcdm = MM_PORT_SERIAL_QCDM (candidate);
            break;

        case MM_PORT_TYPE_NET:
            data_net = g_list_append (data_net, candidate);
            break;

        case MM_PORT_TYPE_GPS:
            g_assert (MM_IS_PORT_SERIAL_GPS (candidate));
            if (!gps)
                gps = MM_PORT_SERIAL_GPS (candidate);
            break;

        case MM_PORT_TYPE_AUDIO:
            g_assert (MM_IS_PORT_SERIAL (candidate));
            if (!audio)
                audio = MM_PORT_SERIAL (candidate);
            break;

#if defined WITH_QMI
        case MM_PORT_TYPE_QMI:
            qmi = g_list_append (qmi, candidate);
            break;
#endif

#if defined WITH_MBIM
        case MM_PORT_TYPE_MBIM:
            mbim = g_list_append (mbim, candidate);
            break;
#endif

        case MM_PORT_TYPE_UNKNOWN:
        case MM_PORT_TYPE_IGNORED:
#if !defined WITH_MBIM
        case MM_PORT_TYPE_MBIM:
#endif
#if !defined WITH_QMI
        case MM_PORT_TYPE_QMI:
#endif
        default:
            /* Ignore port */
            break;
        }
    }

    if (!primary) {
        /* Fall back to a secondary port if we didn't find a primary port */
        if (secondary) {
            primary = secondary;
            secondary = NULL;
        }
        /* Fallback to a data port if no primary or secondary */
        else if (data_at_primary) {
            primary = data_at_primary;
            data_at_primary = NULL;
        }
        else {
            gboolean allow_modem_without_at_port = FALSE;

#if defined WITH_QMI
            if (qmi)
                allow_modem_without_at_port = TRUE;
#endif

#if defined WITH_MBIM
            if (mbim)
                allow_modem_without_at_port = TRUE;
#endif

            if (!allow_modem_without_at_port) {
                g_set_error_literal (error,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Failed to find primary AT port");
                return FALSE;
            }
        }
    }

    /* If the plugin didn't give us any secondary ports, use any additional
     * primary ports or backup secondary ports as secondary.
     */
    if (!secondary)
        secondary = backup_primary ? backup_primary : backup_secondary;

#if defined WITH_QMI
    /* On QMI-based modems, we need to have at least a net port */
    if (qmi && !data_net) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Failed to find a net port in the QMI modem");
        return FALSE;
    }
#endif

#if defined WITH_MBIM
    /* On MBIM-based modems, we need to have at least a net port */
    if (mbim && !data_net) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Failed to find a net port in the MBIM modem");
        return FALSE;
    }
#endif

    /* Data port defaults to primary AT port */
    if (primary && !data_at_primary)
        data_at_primary = primary;

    /* Reset flags on all ports; clear data port first since it might also
     * be the primary or secondary port.
     */
    if (data_at_primary)
        mm_port_serial_at_set_flags (data_at_primary, MM_PORT_SERIAL_AT_FLAG_NONE);
    if (primary)
        mm_port_serial_at_set_flags (primary, MM_PORT_SERIAL_AT_FLAG_PRIMARY);
    if (secondary)
        mm_port_serial_at_set_flags (secondary, MM_PORT_SERIAL_AT_FLAG_SECONDARY);
    if (data_at_primary) {
        flags = mm_port_serial_at_get_flags (data_at_primary);
        mm_port_serial_at_set_flags (data_at_primary, flags | MM_PORT_SERIAL_AT_FLAG_PPP);
    }

    /* sort ports by name */
#if defined WITH_QMI
    qmi = g_list_sort (qmi, (GCompareFunc) port_cmp);
#endif
#if defined WITH_MBIM
    mbim = g_list_sort (mbim, (GCompareFunc) port_cmp);
#endif
    data_net = g_list_sort (data_net, (GCompareFunc) port_cmp);
    data_at  = g_list_sort (data_at,  (GCompareFunc) port_cmp);

    log_port (self, MM_PORT (primary),         "at (primary)");
    log_port (self, MM_PORT (secondary),       "at (secondary)");
    log_port (self, MM_PORT (data_at_primary), "at (data primary)");
    for (l = data_at; l; l = g_list_next (l))
        log_port (self, MM_PORT (l->data),     "at (data secondary)");
    for (l = data_net; l; l = g_list_next (l))
        log_port (self, MM_PORT (l->data),     "net (data)");
    log_port (self, MM_PORT (qcdm),            "qcdm");
    log_port (self, MM_PORT (gps_control),     "gps (control)");
    log_port (self, MM_PORT (gps),             "gps (nmea)");
    log_port (self, MM_PORT (audio),           "audio");
#if defined WITH_QMI
    for (l = qmi; l; l = g_list_next (l))
        log_port (self, MM_PORT (l->data),     "qmi");
#endif
#if defined WITH_MBIM
    for (l = mbim; l; l = g_list_next (l))
        log_port (self, MM_PORT (l->data),     "mbim");
#endif

    /* We keep new refs to the objects here */

    self->priv->primary = (primary ? g_object_ref (primary) : NULL);
    self->priv->secondary = (secondary ? g_object_ref (secondary) : NULL);
    self->priv->qcdm = (qcdm ? g_object_ref (qcdm) : NULL);
    self->priv->gps_control = (gps_control ? g_object_ref (gps_control) : NULL);
    self->priv->gps = (gps ? g_object_ref (gps) : NULL);

    /* Build the final list of data ports, NET ports preferred */
    if (data_net) {
        g_list_foreach (data_net, (GFunc)g_object_ref, NULL);
        self->priv->data = g_list_concat (self->priv->data, g_steal_pointer (&data_net));
    }

    if (data_at_primary)
        self->priv->data = g_list_append (self->priv->data, g_object_ref (data_at_primary));
    if (data_at) {
        g_list_foreach (data_at, (GFunc)g_object_ref, NULL);
        self->priv->data = g_list_concat (self->priv->data, g_steal_pointer (&data_at));
    }

#if defined WITH_QMI
    if (qmi) {
        g_list_foreach (qmi, (GFunc)g_object_ref, NULL);
        self->priv->qmi = g_steal_pointer (&qmi);
    }
#endif

#if defined WITH_MBIM
    if (mbim) {
        g_list_foreach (mbim, (GFunc)g_object_ref, NULL);
        self->priv->mbim = g_steal_pointer (&mbim);
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
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
authorize_ready (MMAuthProvider *authp,
                 GAsyncResult *res,
                 GTask *task)
{
    GError *error = NULL;

    if (!mm_auth_provider_authorize_finish (authp, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);

    g_object_unref (task);
}

void
mm_base_modem_authorize (MMBaseModem *self,
                         GDBusMethodInvocation *invocation,
                         const gchar *authorization,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, self->priv->authp_cancellable, callback, user_data);

    /* When running in the session bus for tests, default to always allow */
    if (mm_context_get_test_session ()) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                authorization,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)authorize_ready,
                                task);
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

static void
after_sim_switch_disable_ready (MMBaseModem  *self,
                                GAsyncResult *res)
{
    g_autoptr(GError) error = NULL;

    mm_base_modem_disable_finish (self, res, &error);
    if (error)
        mm_obj_err (self, "failed to disable after SIM switch event: %s", error->message);
    else
        mm_base_modem_set_valid (self, FALSE);
}

void
mm_base_modem_process_sim_event (MMBaseModem *self)
{
    mm_base_modem_set_reprobe (self, TRUE);
    mm_base_modem_disable (self, (GAsyncReadyCallback) after_sim_switch_disable_ready, NULL);
}

/*****************************************************************************/

static gboolean
base_modem_invalid_idle (MMBaseModem *self)
{
    /* Ensure the modem is set invalid if we get the modem-wide cancellable
     * cancelled */
    mm_base_modem_set_valid (self, FALSE);
    g_object_unref (self);
    return G_SOURCE_REMOVE;
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
setup_ports_table (MMBaseModem *self)
{
    g_assert (!self->priv->ports);
    self->priv->ports = g_hash_table_new_full (g_str_hash,
                                               g_str_equal,
                                               g_free,
                                               g_object_unref);
}

static void
cleanup_modem_port (MMBaseModem *self,
                    MMPort      *port)
{
    mm_obj_dbg (self, "cleaning up port '%s/%s'...",
                mm_port_subsys_get_string (mm_port_get_subsys (MM_PORT (port))),
                mm_port_get_device (MM_PORT (port)));

    /* Cleanup for serial ports */
    if (MM_IS_PORT_SERIAL (port)) {
        g_signal_handlers_disconnect_by_func (port, serial_port_timed_out_cb, self);
        return;
    }

#if defined WITH_MBIM
    /* We need to close the MBIM port cleanly when disposing the modem object */
    if (MM_IS_PORT_MBIM (port)) {
        mm_port_mbim_close (MM_PORT_MBIM (port), NULL, NULL);
        return;
    }
#endif

#if defined WITH_QMI
    /* We need to close the QMI port cleanly when disposing the modem object,
     * otherwise the allocated CIDs will be kept allocated, and if we end up
     * allocating too many newer allocations will fail with client-ids-exhausted
     * errors. */
    if (MM_IS_PORT_QMI (port)) {
        mm_port_qmi_close (MM_PORT_QMI (port), NULL, NULL);
        return;
    }
#endif
}

static void
teardown_ports_table (MMBaseModem *self)
{
    GHashTableIter iter;
    gpointer       value;
    gpointer       key;

    if (!self->priv->ports)
        return;

    g_hash_table_iter_init (&iter, self->priv->ports);
    while (g_hash_table_iter_next (&iter, &key, &value))
        cleanup_modem_port (self, MM_PORT (value));
    g_hash_table_destroy (g_steal_pointer (&self->priv->ports));
}

/*****************************************************************************/

static gchar *
log_object_build_id (MMLogObject *_self)
{
    MMBaseModem *self;

    self = MM_BASE_MODEM (_self);
    return g_strdup_printf ("modem%u", self->priv->dbus_id);
}

/*****************************************************************************/

static void
mm_base_modem_init (MMBaseModem *self)
{
    static guint id = 0;

    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BASE_MODEM,
                                              MMBaseModemPrivate);

    /* Each modem is given a unique id to build its own DBus path */
    self->priv->dbus_id = id++;

    /* Setup authorization provider */
    self->priv->authp = mm_auth_provider_get ();
    self->priv->authp_cancellable = g_cancellable_new ();

    /* Setup modem-wide cancellable */
    self->priv->cancellable = g_cancellable_new ();
    self->priv->invalid_if_cancelled =
        g_cancellable_connect (self->priv->cancellable,
                               G_CALLBACK (base_modem_cancelled),
                               self,
                               NULL);

    self->priv->max_timeouts = DEFAULT_MAX_TIMEOUTS;

    setup_ports_table (self);
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
    case PROP_REPROBE:
        mm_base_modem_set_reprobe (self, g_value_get_boolean (value));
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
    case PROP_REPROBE:
        g_value_set_boolean (value, self->priv->reprobe);
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

    g_assert (!self->priv->enable_tasks);
    g_assert (!self->priv->disable_tasks);

    mm_obj_dbg (self, "completely disposed");

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

    /* note: authp is a singleton, we don't keep a full reference */

    /* Ensure we cancel any ongoing operation, but before
     * disconnect our own signal handler, or we'll end up with
     * another reference of the modem object around. */
    g_cancellable_disconnect (self->priv->cancellable,
                              self->priv->invalid_if_cancelled);
    g_cancellable_cancel (self->priv->cancellable);
    g_clear_object (&self->priv->cancellable);

    g_clear_object (&self->priv->primary);
    g_clear_object (&self->priv->secondary);
    g_list_free_full (g_steal_pointer (&self->priv->data), g_object_unref);
    g_clear_object (&self->priv->qcdm);
    g_clear_object (&self->priv->gps_control);
    g_clear_object (&self->priv->gps);
    g_clear_object (&self->priv->audio);
#if defined WITH_QMI
    g_list_free_full (g_steal_pointer (&self->priv->qmi), g_object_unref);
#endif
#if defined WITH_MBIM
    g_list_free_full (g_steal_pointer (&self->priv->mbim), g_object_unref);
#endif

    teardown_ports_table (self);

    g_clear_object (&self->priv->connection);

    G_OBJECT_CLASS (mm_base_modem_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
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
                           0, G_MAXUINT, DEFAULT_MAX_TIMEOUTS,
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

    properties[PROP_REPROBE] =
        g_param_spec_boolean (MM_BASE_MODEM_REPROBE,
                              "Reprobe",
                              "Whether the modem needs to be reprobed or not.",
                              FALSE,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_REPROBE, properties[PROP_REPROBE]);
}
