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
#if defined WITH_QRTR
#include "mm-kernel-device-qrtr.h"
#endif

#include "mm-log-object.h"
#include "mm-port-enums-types.h"
#include "mm-daemon-enums-types.h"
#include "mm-serial-parsers.h"
#include "mm-modem-helpers.h"
#include "mm-bind.h"

static void log_object_iface_init (MMLogObjectInterface *iface);
static void auth_iface_init (MMIfaceAuthInterface *iface);
static void op_lock_iface_init (MMIfaceOpLockInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MMBaseModem, mm_base_modem, MM_GDBUS_TYPE_OBJECT_SKELETON,
                                  G_IMPLEMENT_INTERFACE (MM_TYPE_LOG_OBJECT, log_object_iface_init)
                                  G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_AUTH, auth_iface_init)
                                  G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_OP_LOCK, op_lock_iface_init))

/* If we get 10 consecutive timeouts in a serial port, we consider the modem
 * invalid and we request re-probing. */
#define DEFAULT_MAX_TIMEOUTS 10

enum {
    PROP_0,
    PROP_VALID,
    PROP_MAX_TIMEOUTS,
    PROP_DEVICE,
    PROP_PHYSDEV,
    PROP_DRIVERS,
    PROP_PLUGIN,
    PROP_VENDOR_ID,
    PROP_PRODUCT_ID,
    PROP_SUBSYSTEM_VENDOR_ID,
    PROP_SUBSYSTEM_DEVICE_ID,

    PROP_CONNECTION,
    PROP_REPROBE,
    PROP_DATA_NET_SUPPORTED,
    PROP_DATA_TTY_SUPPORTED,
    PROP_LAST
};

enum {
    SIGNAL_LINK_PORT_GRABBED,
    SIGNAL_LINK_PORT_RELEASED,
    SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

struct _MMBaseModemPrivate {
    /* The connection to the system bus */
    GDBusConnection *connection;
    guint            dbus_id;

    /* Modem-wide cancellable. If it ever gets cancelled, no further operations
     * should be done by the modem. */
    GCancellable *cancellable;
    gulong invalid_if_cancelled;
    guint  invalid_from_idle;

    gchar *device;
    gchar *physdev;
    gchar **drivers;
    gchar *plugin;

    guint vendor_id;
    guint product_id;
    guint subsystem_vendor_id;
    guint subsystem_device_id;

    gboolean hotplugged;
    gboolean valid;
    gboolean reprobe;
    gboolean torn_down;

    guint max_timeouts;

    /* The authorization provider */
    MMAuthProvider *authp;
    GCancellable *authp_cancellable;

    GHashTable *ports;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;
    MMPortSerialQcdm *qcdm;

    GList    *data;
    gboolean  data_net_supported;
    gboolean  data_tty_supported;

    /* GPS-enabled modems will have an AT port for control, and a raw serial
     * port to receive all GPS traces */
    MMPortSerialAt *gps_control;
    MMPortSerialGps *gps;

    /* Some audio-capable devices will have a port for audio specifically */
    MMPortSerial *audio;

#if defined WITH_QMI
    /* QMI ports */
    GList *qmi;
#endif

#if defined WITH_MBIM
    /* MBIM ports */
    GList *mbim;
#endif

    /* Additional port links grabbed after having
     * organized ports */
    GHashTable *link_ports;

    /* Scheduled operations. The forbidden_forever flag will be set to TRUE
     * if an "override" operation is requested, and will never be set to FALSE
     * back, it is expected the modem object will eventually be removed after
     * the operation has finished,
     */
    GList    *scheduled_operations;
    gboolean  scheduled_operations_forbidden_forever;
};

static void   mm_base_modem_operation_lock        (MMBaseModem          *self,
                                                   MMOperationPriority   priority,
                                                   const gchar          *description,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
static gssize mm_base_modem_operation_lock_finish (MMBaseModem          *self,
                                                   GAsyncResult         *res,
                                                   GError              **error);


guint
mm_base_modem_get_dbus_id (MMBaseModem *self)
{
    return self->priv->dbus_id;
}

/******************************************************************************/

static void
port_removed_cb (MMPort      *port,
                 MMBaseModem *self)
{
    /* We have to do a full re-probe here because simply reopening the device
     * and restarting proxy would leave us without proper notifications. */
    mm_obj_msg (self, "port '%s' no longer controllable, reprobing",
                mm_port_get_device (MM_PORT (port)));
    self->priv->reprobe = TRUE;
    g_cancellable_cancel (self->priv->cancellable);
}

static void
port_timed_out_cb (MMPort       *port,
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
                                MMPortType   ptype,
                                const gchar *name)
{
    return MM_PORT (g_object_new (MM_TYPE_PORT,
                                  MM_PORT_DEVICE, name,
                                  MM_PORT_GROUP,  MM_PORT_GROUP_IGNORED,
                                  MM_PORT_TYPE,   ptype,
                                  NULL));
}

static MMPort *
base_modem_create_net_port (MMBaseModem *self,
                            const gchar *name)
{
    return MM_PORT (mm_port_net_new (name));
}

static MMPort *
base_modem_create_tty_port (MMBaseModem    *self,
                            const gchar    *name,
                            MMKernelDevice *kernel_device,
                            MMPortType      ptype)
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

#if defined WITH_QRTR
static MMPort *
base_modem_create_qrtr_port (MMBaseModem    *self,
                             const gchar    *name,
                             MMKernelDevice *kernel_device,
                             MMPortType      ptype)
{
    if (ptype == MM_PORT_TYPE_QMI) {
        g_autoptr(QrtrNode) node = NULL;

        g_assert (MM_IS_KERNEL_DEVICE_QRTR (kernel_device));
        node = mm_kernel_device_qrtr_get_node (MM_KERNEL_DEVICE_QRTR (kernel_device));
        return MM_PORT (mm_port_qmi_new_from_node (name, node));
    }
    return NULL;
}
#endif

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

    if (ptype == MM_PORT_TYPE_XMMRPC)
        return MM_PORT (g_object_new (MM_TYPE_PORT,
                                      MM_PORT_DEVICE, name,
                                      MM_PORT_SUBSYS, MM_PORT_SUBSYS_WWAN,
                                      MM_PORT_GROUP, MM_PORT_GROUP_USED,
                                      MM_PORT_TYPE, MM_PORT_TYPE_XMMRPC,
                                      NULL));

    return NULL;
}

static MMPort *
base_modem_create_virtual_port (MMBaseModem *self,
                                const gchar *name)
{
    return MM_PORT (mm_port_serial_at_new (name, MM_PORT_SUBSYS_UNIX));
}

static MMPort *
base_modem_internal_grab_port (MMBaseModem         *self,
                               MMKernelDevice      *kernel_device,
                               gboolean             link_port,
                               MMPortGroup          pgroup,
                               MMPortType           ptype,
                               MMPortSerialAtFlag   at_pflags,
                               GError             **error)
{
    MMPort           *port;
    const gchar      *subsys;
    const gchar      *name;
    g_autofree gchar *key = NULL;
    gboolean          port_monitoring = FALSE;

    subsys = mm_kernel_device_get_subsystem (kernel_device);
    name   = mm_kernel_device_get_name      (kernel_device);

    if (!self->priv->ports || (link_port && !self->priv->link_ports)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Cannot add port '%s/%s', no ports table", subsys, name);
        return NULL;
    }

    /* Check whether we already have it stored */
    key  = g_strdup_printf ("%s%s", subsys, name);
    port = g_hash_table_lookup (self->priv->ports, key);
    if (port) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot add port '%s/%s', already exists", subsys, name);
        return NULL;
    }

    g_assert (MM_BASE_MODEM_GET_CLASS (self)->create_tty_port);
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->create_usbmisc_port);
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->create_wwan_port);

    /* Explicitly ignored ports, grab them but explicitly flag them as ignored
     * right away, all the same way (i.e. regardless of subsystem). */
    if (pgroup == MM_PORT_GROUP_IGNORED)
        port = base_modem_create_ignored_port (self, ptype, name);
    else if (g_str_equal (subsys, "net"))
        port = base_modem_create_net_port (self, name);
    else if (g_str_equal (subsys, "tty"))
        port = MM_BASE_MODEM_GET_CLASS (self)->create_tty_port (self, name, kernel_device, ptype);
    else if (g_str_equal (subsys, "usbmisc"))
        port = MM_BASE_MODEM_GET_CLASS (self)->create_usbmisc_port (self, name, ptype);
    else if (g_str_equal (subsys, "rpmsg"))
        port = base_modem_create_rpmsg_port (self, name, ptype);
#if defined WITH_QRTR
    else if (g_str_equal (subsys, "qrtr"))
        port = base_modem_create_qrtr_port (self, name, kernel_device, ptype);
#endif
    else if (g_str_equal (subsys, "virtual"))
        port = base_modem_create_virtual_port (self, name);
    else if (g_str_equal (subsys, "wwan"))
        port = MM_BASE_MODEM_GET_CLASS (self)->create_wwan_port (self, name, ptype);

    if (!port) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot add port '%s/%s', unhandled port type", subsys, name);
        return NULL;
    }

    /* Setup consecutive ports and removal watchers in all control ports */
    if (pgroup == MM_PORT_GROUP_USED) {
        if (MM_IS_PORT_SERIAL_AT (port)) {
            mm_obj_dbg (port, "port monitoring enabled in AT port");
            port_monitoring = TRUE;
        } else if (MM_IS_PORT_SERIAL_QCDM (port)) {
            mm_obj_dbg (port, "port monitoring enabled in QCDM port");
            port_monitoring = TRUE;
        }
#if defined WITH_QMI
        else if (MM_IS_PORT_QMI (port)) {
            mm_obj_dbg (port, "port monitoring enabled in QMI port");
            port_monitoring = TRUE;
        }
#endif
#if defined WITH_MBIM
        else if (MM_IS_PORT_MBIM (port)) {
            mm_obj_dbg (port, "port monitoring enabled in MBIM port");
            port_monitoring = TRUE;
        }
#endif
    }

    if (port_monitoring) {
        if (self->priv->max_timeouts > 0)
            g_signal_connect (port,
                              MM_PORT_SIGNAL_TIMED_OUT,
                              G_CALLBACK (port_timed_out_cb),
                              self);
        g_signal_connect (port,
                          MM_PORT_SIGNAL_REMOVED,
                          G_CALLBACK (port_removed_cb),
                          self);
    }

    /* Store kernel device */
    g_object_set (port, MM_PORT_KERNEL_DEVICE, kernel_device, NULL);

    /* Set owner ID */
    mm_log_object_set_owner_id (MM_LOG_OBJECT (port), mm_log_object_get_id (MM_LOG_OBJECT (self)));

    /* Common setup for all AT ports from all subsystems */
    if (MM_IS_PORT_SERIAL_AT (port)) {
        mm_port_serial_at_set_response_parser (MM_PORT_SERIAL_AT (port),
                                               mm_serial_parser_v1_parse,
                                               mm_serial_parser_v1_remove_echo,
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

            /* Additionally, the ports may also be flagged as GPS control explicitly, if there is
             * one specific port to be used for that purpose */
            if (mm_kernel_device_get_property_as_boolean (kernel_device, ID_MM_PORT_TYPE_AT_GPS_CONTROL)) {
                mm_obj_dbg (port, "AT port flagged as GPS control");
                at_pflags = MM_PORT_SERIAL_AT_FLAG_GPS_CONTROL;
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
    if (link_port)
        g_hash_table_insert (self->priv->link_ports, g_steal_pointer (&key), port);
    else
        g_hash_table_insert (self->priv->ports, g_steal_pointer (&key), port);
    return port;
}

gboolean
mm_base_modem_grab_port (MMBaseModem         *self,
                         MMKernelDevice      *kernel_device,
                         MMPortGroup          pgroup,
                         MMPortType           ptype,
                         MMPortSerialAtFlag   at_pflags,
                         GError             **error)
{
    g_autoptr(GError) inner_error = NULL;

    if (!base_modem_internal_grab_port (self, kernel_device, FALSE, pgroup, ptype, at_pflags, &inner_error)) {
        /* If the port was REQUIRED via udev tags and we failed to grab it, we will report
         * a fatal error. */
        if (mm_kernel_device_get_property_as_boolean (kernel_device, ID_MM_REQUIRED)) {
            mm_obj_err (self, "required port '%s/%s' failed to be grabbed",
                        mm_kernel_device_get_subsystem (kernel_device),
                        mm_kernel_device_get_name      (kernel_device));
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                         "Required port failed to be grabbed");
        } else
            g_propagate_error (error, g_steal_pointer (&inner_error));
        return FALSE;
    }

    mm_obj_dbg (self, "port '%s/%s' grabbed",
                mm_kernel_device_get_subsystem (kernel_device),
                mm_kernel_device_get_name      (kernel_device));
    return TRUE;
}

/******************************************************************************/

gboolean
mm_base_modem_grab_link_port (MMBaseModem     *self,
                              MMKernelDevice  *kernel_device,
                              GError         **error)
{
    const gchar *subsystem;
    const gchar *name;
    MMPort      *port;

    /* To simplify things, we only support NET link ports at this point */
    subsystem = mm_kernel_device_get_subsystem (kernel_device);
    name      = mm_kernel_device_get_name      (kernel_device);

    if (!g_str_equal (subsystem, "net")) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot add port '%s/%s', unexpected link port subsystem", subsystem, name);
        return FALSE;
    }

    /* all the newly added link ports will NOT be 'organized'; i.e. they won't
     * be available as 'data ports' in the modem, but they can be looked up
     * by name */
    port = base_modem_internal_grab_port (self,
                                          kernel_device,
                                          TRUE,
                                          MM_PORT_GROUP_USED,
                                          MM_PORT_TYPE_NET,
                                          MM_PORT_SERIAL_AT_FLAG_NONE,
                                          error);
    if (!port)
        return FALSE;


    mm_obj_dbg (self, "link port '%s/%s' grabbed", subsystem, name);
    g_signal_emit (self, signals[SIGNAL_LINK_PORT_GRABBED], 0, port);
    return TRUE;
}

gboolean
mm_base_modem_release_link_port (MMBaseModem  *self,
                                 const gchar  *subsystem,
                                 const gchar  *name,
                                 GError      **error)
{
    g_autofree gchar *key = NULL;
    MMPort           *port;

    if (!self->priv->link_ports) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Cannot release link port '%s/%s', no link ports table", subsystem, name);
        return FALSE;
    }

    key  = g_strdup_printf ("%s%s", subsystem, name);
    port = g_hash_table_lookup (self->priv->link_ports, key);
    if (!port) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                     "Cannot release link port '%s/%s', not grabbed", subsystem, name);
        return FALSE;
    }

    /* make sure the port object is valid during the port release signal */
    g_object_ref (port);
    g_hash_table_remove (self->priv->link_ports, key);
    mm_obj_dbg (self, "link port '%s/%s' released", subsystem, name);
    g_signal_emit (self, signals[SIGNAL_LINK_PORT_RELEASED], 0, port);
    g_object_unref (port);
    return TRUE;
}

/******************************************************************************/

typedef struct {
    gchar  *name;
    gulong  link_port_grabbed_id;
    guint   timeout_id;
} WaitLinkPortContext;

static void
wait_link_port_context_free (WaitLinkPortContext *ctx)
{
    g_assert (!ctx->link_port_grabbed_id);
    g_assert (!ctx->timeout_id);
    g_free (ctx->name);
    g_slice_free (WaitLinkPortContext, ctx);
}

MMPort *
mm_base_modem_wait_link_port_finish (MMBaseModem   *self,
                                     GAsyncResult  *res,
                                     GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static gboolean
wait_link_port_timeout_cb (GTask *task)
{
    WaitLinkPortContext *ctx;
    MMBaseModem         *self;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data     (task);

    ctx->timeout_id = 0;
    g_signal_handler_disconnect (self, ctx->link_port_grabbed_id);
    ctx->link_port_grabbed_id = 0;

    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND,
                             "Timed out waiting for link port 'net/%s'",
                             ctx->name);
    g_object_unref (task);

    return G_SOURCE_REMOVE;
}

static void
wait_link_port_grabbed_cb (MMBaseModem *self,
                           MMPort      *link_port,
                           GTask       *task)
{
    WaitLinkPortContext *ctx;
    MMPortSubsys         link_port_subsystem;
    const gchar         *link_port_name;

    ctx = g_task_get_task_data (task);

    link_port_subsystem = mm_port_get_subsys (link_port);
    link_port_name = mm_port_get_device (link_port);

    if (link_port_subsystem != MM_PORT_SUBSYS_NET) {
        mm_obj_warn (self, "unexpected link port subsystem grabbed: %s/%s",
                     mm_port_subsys_get_string (link_port_subsystem),
                     link_port_name);
        return;
    }

    if (g_strcmp0 (link_port_name, ctx->name) != 0)
        return;

    /* we got it! */

    g_source_remove (ctx->timeout_id);
    ctx->timeout_id = 0;
    g_signal_handler_disconnect (self, ctx->link_port_grabbed_id);
    ctx->link_port_grabbed_id = 0;

    g_task_return_pointer (task, g_object_ref (link_port), g_object_unref);
    g_object_unref (task);
}

void
mm_base_modem_wait_link_port (MMBaseModem         *self,
                              const gchar         *subsystem,
                              const gchar         *name,
                              guint                timeout_ms,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
    WaitLinkPortContext *ctx;
    GTask               *task;
    g_autofree gchar    *key = NULL;
    MMPort              *port;

    task = g_task_new (self, NULL, callback, user_data);

    if (!g_str_equal (subsystem, "net")) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Cannot wait for port '%s/%s', unexpected link port subsystem", subsystem, name);
        g_object_unref (task);
        return;
    }

    if (!self->priv->link_ports) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Cannot wait for port '%s/%s', no link ports table", subsystem, name);
        g_object_unref (task);
        return;
    }

    key = g_strdup_printf ("%s%s", subsystem, name);
    port = g_hash_table_lookup (self->priv->link_ports, key);
    if (port) {
        mm_obj_dbg (self, "no need to wait for port '%s/%s': already grabbed", subsystem, name);
        g_task_return_pointer (task, g_object_ref (port), g_object_unref);
        g_object_unref (task);
        return;
    }

    ctx = g_slice_new0 (WaitLinkPortContext);
    ctx->name = g_strdup (name);
    g_task_set_task_data (task, ctx, (GDestroyNotify)wait_link_port_context_free);

    /* task ownership shared between timeout and signal handler */
    ctx->timeout_id = g_timeout_add (timeout_ms,
                                     (GSourceFunc) wait_link_port_timeout_cb,
                                     task);
    ctx->link_port_grabbed_id = g_signal_connect (self,
                                                  MM_BASE_MODEM_SIGNAL_LINK_PORT_GRABBED,
                                                  G_CALLBACK (wait_link_port_grabbed_cb),
                                                  task);

    mm_obj_dbg (self, "waiting for port '%s/%s'...", subsystem, name);
}

/******************************************************************************/
/* Common support to perform state update/sync operations with the base modem. */

typedef enum {
    STATE_OPERATION_TYPE_INITIALIZE,
    STATE_OPERATION_TYPE_ENABLE,
    STATE_OPERATION_TYPE_DISABLE,
#if defined WITH_SUSPEND_RESUME
    STATE_OPERATION_TYPE_SYNC,
    STATE_OPERATION_TYPE_TERSE,
#endif
} StateOperationType;

typedef struct {
    StateOperation       operation;
    StateOperationFinish operation_finish;
    gssize               operation_id;
} StateOperationContext;

static void
state_operation_context_free (StateOperationContext *ctx)
{
    g_assert (ctx->operation_id < 0);
    g_slice_free (StateOperationContext, ctx);
}

static gboolean
state_operation_finish (MMBaseModem   *self,
                        GAsyncResult  *res,
                        GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
state_operation_ready (MMBaseModem  *self,
                       GAsyncResult *res,
                       GTask        *task)
{
    GError *error = NULL;

    StateOperationContext *ctx;

    ctx = g_task_get_task_data (task);

    if (ctx->operation_id >= 0) {
        mm_iface_op_lock_unlock (MM_IFACE_OP_LOCK (self), ctx->operation_id);
        ctx->operation_id = (gssize) -1;
    }

    if (!ctx->operation_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

static void
state_operation_run (GTask *task)
{
    MMBaseModem           *self;
    StateOperationContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    ctx->operation (self,
                    self->priv->cancellable,
                    (GAsyncReadyCallback) state_operation_ready,
                    task);
}

static void
lock_before_state_operation_ready (MMBaseModem  *self,
                                   GAsyncResult *res,
                                   GTask        *task)
{
    GError                *error = NULL;
    StateOperationContext *ctx;

    ctx = g_task_get_task_data (task);

    ctx->operation_id = mm_base_modem_operation_lock_finish (self, res, &error);
    if (ctx->operation_id < 0) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    state_operation_run (task);
}

static void
state_operation (MMBaseModem         *self,
                 StateOperationType   operation_type,
                 MMOperationLock      operation_lock,
                 MMOperationPriority  operation_priority,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
    GTask                 *task;
    StateOperationContext *ctx;
    gboolean               optional;
    const gchar           *operation_description;

    ctx = g_slice_new0 (StateOperationContext);
    ctx->operation_id = (gssize) -1;

    /* configure operation to run */
    switch (operation_type) {
        case STATE_OPERATION_TYPE_INITIALIZE:
            operation_description = "initialization";
            optional = FALSE;
            ctx->operation        = MM_BASE_MODEM_GET_CLASS (self)->initialize;
            ctx->operation_finish = MM_BASE_MODEM_GET_CLASS (self)->initialize_finish;
            break;
        case STATE_OPERATION_TYPE_ENABLE:
            operation_description = "enabling";
            optional = FALSE;
            ctx->operation        = MM_BASE_MODEM_GET_CLASS (self)->enable;
            ctx->operation_finish = MM_BASE_MODEM_GET_CLASS (self)->enable_finish;
            break;
        case STATE_OPERATION_TYPE_DISABLE:
            operation_description = "disabling";
            optional = FALSE;
            ctx->operation        = MM_BASE_MODEM_GET_CLASS (self)->disable;
            ctx->operation_finish = MM_BASE_MODEM_GET_CLASS (self)->disable_finish;
            break;
#if defined WITH_SUSPEND_RESUME
        case STATE_OPERATION_TYPE_SYNC:
            operation_description = "sync";
            optional = TRUE;
            ctx->operation        = MM_BASE_MODEM_GET_CLASS (self)->sync;
            ctx->operation_finish = MM_BASE_MODEM_GET_CLASS (self)->sync_finish;
            break;
        case STATE_OPERATION_TYPE_TERSE:
            operation_description = "terse";
            optional = TRUE;
            ctx->operation        = MM_BASE_MODEM_GET_CLASS (self)->terse;
            ctx->operation_finish = MM_BASE_MODEM_GET_CLASS (self)->terse_finish;
            break;
#endif
        default:
            g_assert_not_reached ();
    }

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify) state_operation_context_free);

    if (optional && (!ctx->operation || !ctx->operation_finish)) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "Unsupported");
        g_object_unref (task);
        return;
    }
    g_assert (ctx->operation && ctx->operation_finish);

    if (operation_lock == MM_OPERATION_LOCK_ALREADY_ACQUIRED) {
        state_operation_run (task);
        return;
    }

    g_assert (operation_lock == MM_OPERATION_LOCK_REQUIRED);
    mm_base_modem_operation_lock (self,
                                  operation_priority,
                                  operation_description,
                                  (GAsyncReadyCallback) lock_before_state_operation_ready,
                                  task);
}

/******************************************************************************/

#if defined WITH_SUSPEND_RESUME

gboolean
mm_base_modem_sync_finish (MMBaseModem   *self,
                           GAsyncResult  *res,
                           GError       **error)
{
    return state_operation_finish (self, res, error);
}

void
mm_base_modem_sync (MMBaseModem              *self,
                    MMOperationLock      operation_lock,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
    state_operation (self,
                     STATE_OPERATION_TYPE_SYNC,
                     operation_lock,
                     MM_OPERATION_PRIORITY_DEFAULT,
                     callback,
                     user_data);
}

gboolean
mm_base_modem_terse_finish (MMBaseModem   *self,
                            GAsyncResult  *res,
                            GError       **error)
{
    return state_operation_finish (self, res, error);
}

void
mm_base_modem_terse (MMBaseModem              *self,
                     MMOperationLock  operation_lock,
                     GAsyncReadyCallback       callback,
                     gpointer                  user_data)
{
    state_operation (self,
                     STATE_OPERATION_TYPE_TERSE,
                     operation_lock,
                     MM_OPERATION_PRIORITY_DEFAULT,
                     callback,
                     user_data);
}

#endif /* WITH_SUSPEND_RESUME */

/******************************************************************************/

gboolean
mm_base_modem_disable_finish (MMBaseModem   *self,
                              GAsyncResult  *res,
                              GError       **error)
{
    return state_operation_finish (self, res, error);
}

void
mm_base_modem_disable (MMBaseModem         *self,
                       MMOperationLock      operation_lock,
                       MMOperationPriority  operation_priority,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
    state_operation (self,
                     STATE_OPERATION_TYPE_DISABLE,
                     operation_lock,
                     operation_priority,
                     callback,
                     user_data);
}

/******************************************************************************/

gboolean
mm_base_modem_enable_finish (MMBaseModem   *self,
                             GAsyncResult  *res,
                             GError       **error)
{
    return state_operation_finish (self, res, error);
}

void
mm_base_modem_enable (MMBaseModem         *self,
                      MMOperationLock      operation_lock,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    state_operation (self,
                     STATE_OPERATION_TYPE_ENABLE,
                     operation_lock,
                     MM_OPERATION_PRIORITY_DEFAULT,
                     callback,
                     user_data);
}

/******************************************************************************/

gboolean
mm_base_modem_initialize_finish (MMBaseModem   *self,
                                 GAsyncResult  *res,
                                 GError       **error)
{
    return state_operation_finish (self, res, error);
}

void
mm_base_modem_initialize (MMBaseModem         *self,
                          MMOperationLock      operation_lock,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    state_operation (self,
                     STATE_OPERATION_TYPE_INITIALIZE,
                     operation_lock,
                     MM_OPERATION_PRIORITY_DEFAULT,
                     callback,
                     user_data);
}

/******************************************************************************/

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

MMIfacePortAt *
mm_base_modem_get_best_at_port (MMBaseModem  *self,
                                GError      **error)
{
    MMIfacePortAt *best;

    best = mm_base_modem_peek_best_at_port (self, error);
    return (best ? g_object_ref (best) : NULL);
}

MMIfacePortAt *
mm_base_modem_peek_best_at_port (MMBaseModem  *self,
                                 GError      **error)
{
    gboolean supported;

#if defined WITH_MBIM
    /* Prefer an AT-capable MBIM port instead of a serial port */
    if (self->priv->mbim) {
        GList *l;

        for (l = self->priv->mbim; l; l = g_list_next (l)) {
            if (MM_IS_IFACE_PORT_AT (l->data) &&
                mm_iface_port_at_check_support (MM_IFACE_PORT_AT (l->data), &supported, NULL) &&
                supported)
                return MM_IFACE_PORT_AT (l->data);
        }
    }
#endif

#if defined WITH_QMI
    /* Prefer an AT-capable QMI port instead of a serial port */
    if (self->priv->qmi) {
        GList *l;

        for (l = self->priv->qmi; l; l = g_list_next (l)) {
            if (MM_IS_IFACE_PORT_AT (l->data) &&
                mm_iface_port_at_check_support (MM_IFACE_PORT_AT (l->data), &supported, NULL) &&
                supported)
                return MM_IFACE_PORT_AT (l->data);
        }
    }
#endif

    /* Decide which port to use */
    if (self->priv->primary && !mm_port_get_connected (MM_PORT (self->priv->primary))) {
        g_assert (MM_IS_IFACE_PORT_AT (self->priv->primary));
        g_assert (mm_iface_port_at_check_support (MM_IFACE_PORT_AT (self->priv->primary), &supported, NULL) && supported);
        return MM_IFACE_PORT_AT (self->priv->primary);
    }

    /* If primary port is connected, check if we can get the secondary
     * port */
    if (self->priv->secondary && !mm_port_get_connected (MM_PORT (self->priv->secondary))) {
        g_assert (MM_IS_IFACE_PORT_AT (self->priv->secondary));
        g_assert (mm_iface_port_at_check_support (MM_IFACE_PORT_AT (self->priv->secondary), &supported, NULL) && supported);
        return MM_IFACE_PORT_AT (self->priv->secondary);
    }

    /* Otherwise, we cannot get any port */
    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "No AT port available to run command");
    return NULL;
}

static gint
port_info_cmp (const MMModemPortInfo *a,
               const MMModemPortInfo *b)
{
    /* default to alphabetical sorting on the port name */
    return g_strcmp0 (a->name, b->name);
}

static MMModemPortInfo *
parse_port_infos (GHashTable *ports,
                  guint      *n_port_infos,
                  MMPortGroup pgroup_filter)
{
    GHashTableIter  iter;
    GArray         *port_infos;
    MMPort         *port;

    if (!ports) {
        *n_port_infos = 0;
        return NULL;
    }

    port_infos = g_array_new (FALSE, FALSE, sizeof (MMModemPortInfo));
    g_hash_table_iter_init (&iter, ports);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer)&port)) {
        MMModemPortInfo port_info;

        if (mm_port_get_port_group (port) != pgroup_filter)
            continue;

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
        case MM_PORT_TYPE_XMMRPC:
            port_info.type = MM_MODEM_PORT_TYPE_XMMRPC;
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

    *n_port_infos = port_infos->len;
    g_array_sort (port_infos, (GCompareFunc) port_info_cmp);
    return (MMModemPortInfo *) g_array_free (port_infos, FALSE);
}

MMModemPortInfo *
mm_base_modem_get_port_infos (MMBaseModem *self,
                              guint       *n_port_infos)
{
    return parse_port_infos (self->priv->ports, n_port_infos, MM_PORT_GROUP_USED);
}

MMModemPortInfo *
mm_base_modem_get_ignored_port_infos (MMBaseModem *self,
                                      guint       *n_port_infos)
{
    return parse_port_infos (self->priv->ports, n_port_infos, MM_PORT_GROUP_IGNORED);
}

static gint
port_cmp (MMPort *a,
          MMPort *b)
{
    /* default to alphabetical sorting on the port name */
    return g_strcmp0 (mm_port_get_device (a), mm_port_get_device (b));
}

GList *
mm_base_modem_find_ports (MMBaseModem  *self,
                          MMPortSubsys  subsys,
                          MMPortType    type)
{
    GList          *out = NULL;
    GHashTableIter  iter;
    gpointer        value;
    gpointer        key;

    if (!self->priv->ports)
        return NULL;

    g_hash_table_iter_init (&iter, self->priv->ports);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMPort *port = MM_PORT (value);

        if (subsys != MM_PORT_SUBSYS_UNKNOWN && mm_port_get_subsys (port) != subsys)
            continue;

        if (type != MM_PORT_TYPE_UNKNOWN && mm_port_get_port_type (port) != type)
            continue;

        out = g_list_append (out, g_object_ref (port));
    }

    return g_list_sort (out, (GCompareFunc) port_cmp);
}

static MMPort *
peek_port_in_ht (GHashTable  *ht,
                 const gchar *name)
{
    GHashTableIter iter;
    gpointer       value;
    gpointer       key;

    if (!ht)
        return NULL;

    g_hash_table_iter_init (&iter, ht);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        MMPort *port = MM_PORT (value);

        if (g_str_equal (mm_port_get_device (port), name))
            return port;
    }

    return NULL;
}

MMPort *
mm_base_modem_peek_port (MMBaseModem *self,
                         const gchar *name)
{
    MMPort *found;

    found = peek_port_in_ht (self->priv->ports, name);
    if (!found)
        found = peek_port_in_ht (self->priv->link_ports, name);

    return found;
}

MMPort *
mm_base_modem_get_port (MMBaseModem *self,
                        const gchar *name)
{
    MMPort *port;

    port = mm_base_modem_peek_port (self, name);
    return (port ? g_object_ref (port) : NULL);
}

static inline void
log_port (MMBaseModem *self,
          MMPort      *port,
          const gchar *desc)
{
    if (!port)
        return;

    mm_obj_info (self, "%s/%s: %s",
                 mm_port_subsys_get_string (mm_port_get_subsys (port)),
                 mm_port_get_device (port),
                 desc);
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
    MMPort *xmmrpc = NULL;
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

    /* Ports table is created on init and removed on dispose(), not on
     * finalize(), so there is a chance this may happen */
    if (!self->priv->ports) {
        g_set_error_literal (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "No ports table");
        return FALSE;
    }

    g_hash_table_iter_init (&iter, self->priv->ports);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &candidate)) {
        /* Skip ports that should not be used */
        if (mm_port_get_port_group (candidate) != MM_PORT_GROUP_USED)
            continue;

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

        case MM_PORT_TYPE_XMMRPC:
            g_assert (MM_IS_PORT (candidate));
            if (!xmmrpc)
                xmmrpc = MM_PORT (candidate);
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
    log_port (self, MM_PORT (xmmrpc),          "xmmrpc");
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

    /* Append net ports to the final list of data ports, but only if the modem
     * supports them */
    if (data_net) {
        if (self->priv->data_net_supported) {
            g_list_foreach (data_net, (GFunc)g_object_ref, NULL);
            self->priv->data = g_list_concat (self->priv->data, g_steal_pointer (&data_net));
        } else
            mm_obj_dbg (self, "net ports available but ignored");
    }

    /* Append tty ports to the final list of data ports, but only if the modem
     * supports them */
    if (data_at_primary || data_at) {
        if (self->priv->data_tty_supported) {
            if (data_at_primary)
                self->priv->data = g_list_append (self->priv->data, g_object_ref (data_at_primary));
            if (data_at) {
                g_list_foreach (data_at, (GFunc)g_object_ref, NULL);
                self->priv->data = g_list_concat (self->priv->data, g_steal_pointer (&data_at));
            }
        } else
            mm_obj_dbg (self, "at data ports available but ignored");
    }

    /* Fail if we haven't added any single data port; this is probably a plugin
     * misconfiguration */
    if (!self->priv->data) {
        g_set_error_literal (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Failed to find a data port in the modem");
        return FALSE;
    }

#if defined WITH_QMI
    if (qmi) {
        /* The first item in the data list must be a net port, because
         * QMI modems only expect net ports */
        g_assert (MM_IS_PORT_NET (self->priv->data->data));
        /* let the MMPortQmi know which net driver is being used, taken
         * from the first item in the net port list */
        g_list_foreach (qmi, (GFunc)mm_port_qmi_set_net_details, (gpointer) MM_PORT (self->priv->data->data));

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

    return TRUE;
}

/*****************************************************************************/
/* Authorization */

static gboolean
mm_base_modem_authorize_finish (MMIfaceAuth *auth,
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

static void
mm_base_modem_authorize (MMIfaceAuth *auth,
                         GDBusMethodInvocation *invocation,
                         const gchar *authorization,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    MMBaseModem *self = MM_BASE_MODEM (auth);
    GTask       *task;

    task = g_task_new (self, self->priv->authp_cancellable, callback, user_data);

    mm_auth_provider_authorize (self->priv->authp,
                                invocation,
                                authorization,
                                self->priv->authp_cancellable,
                                (GAsyncReadyCallback)authorize_ready,
                                task);
}

/*****************************************************************************/
/* Exclusive operation */

typedef struct {
    gssize               id;
    MMOperationPriority  priority;
    gchar               *description;
    GTask               *wait_task;
} OperationInfo;

static void
operation_info_free (OperationInfo *info)
{
    g_assert (!info->wait_task);
    g_free (info->description);
    g_slice_free (OperationInfo, info);
}

/* Exclusive operation lock */

static gssize
mm_base_modem_operation_lock_finish (MMBaseModem   *self,
                                     GAsyncResult  *res,
                                     GError       **error)
{
    return g_task_propagate_int (G_TASK (res), error);
}

static void
base_modem_operation_run (MMBaseModem *self)
{
    OperationInfo *info;
    GTask         *task;

    if (!self->priv->scheduled_operations)
        return;

    /* Do nothing if head operation is already running */
    info = (OperationInfo *)(self->priv->scheduled_operations->data);
    if (!info->wait_task)
        return;

    /* Run the operation in head of the list */
    mm_obj_dbg (self, "[operation %" G_GSSIZE_FORMAT "] %s - %s: lock acquired",
                info->id,
                mm_operation_priority_get_string (info->priority),
                info->description);
    task = g_steal_pointer (&info->wait_task);
    g_task_return_int (task, info->id);
    g_object_unref (task);
}

static gboolean
abort_pending_operation_in_idle_cb (GTask *task)
{
    g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                             "Operation aborted");
    g_object_unref (task);
    return G_SOURCE_REMOVE;
}

static void
abort_pending_operations (MMBaseModem *self)
{
    GList *head = NULL;
    GList *abort_operations;

    /* Steal the whole list before iterating it */
    abort_operations = g_steal_pointer (&self->priv->scheduled_operations);

    while (abort_operations) {
        OperationInfo *info;
        GTask         *task;

        info = (OperationInfo *)(abort_operations->data);

        /* Head operation may already be running, we should not abort that one */
        if (!info->wait_task) {
            /* Keep the head item as a single-element list */
            g_assert (!head);
            head = abort_operations;
            abort_operations = g_list_remove_link (abort_operations, head);
            continue;
        }

        g_assert (info->wait_task);
        mm_obj_dbg (self, "[operation %" G_GSSIZE_FORMAT "] %s - %s: aborted early",
                    info->id,
                    mm_operation_priority_get_string (info->priority),
                    info->description);

        task = g_steal_pointer (&info->wait_task);
        g_idle_add ((GSourceFunc) abort_pending_operation_in_idle_cb, task);
        abort_operations = g_list_delete_link (abort_operations, abort_operations);
        operation_info_free (info);
    }

    /* Keep the running head, if any, in the list of scheduled operations */
    self->priv->scheduled_operations = head;
}

static void
mm_base_modem_operation_lock (MMBaseModem          *self,
                              MMOperationPriority   priority,
                              const gchar          *description,
                              GAsyncReadyCallback   callback,
                              gpointer              user_data)
{
    GTask          *task;
    OperationInfo  *info;
    static gssize   operation_id = 0;

    task = g_task_new (self, NULL, callback, user_data);
    if (self->priv->scheduled_operations_forbidden_forever) {
        mm_obj_dbg (self, "operation forbidden as override has already been requested");
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_ABORTED,
                                 "Operation aborted");
        g_object_unref (task);
        return;
    }

    info = g_slice_new0 (OperationInfo);
    info->id = operation_id;
    info->priority = priority;
    info->description = g_strdup (description);
    info->wait_task = task;

    if (operation_id == G_MAXSSIZE) {
        mm_obj_dbg (self, "operation id reset");
        operation_id = 0;
    } else
        operation_id++;

    if (info->priority == MM_OPERATION_PRIORITY_OVERRIDE) {
        mm_obj_dbg (self, "[operation %" G_GSSIZE_FORMAT "] %s - %s: override requested - no new operations will be allowed",
                    info->id,
                    mm_operation_priority_get_string (info->priority),
                    info->description);
        g_assert (!self->priv->scheduled_operations_forbidden_forever);
        self->priv->scheduled_operations_forbidden_forever = TRUE;
        abort_pending_operations (self);
        self->priv->scheduled_operations = g_list_append (self->priv->scheduled_operations, info);
    } else if (info->priority == MM_OPERATION_PRIORITY_DEFAULT) {
        mm_obj_dbg (self, "[operation %" G_GSSIZE_FORMAT "] %s - %s: scheduled",
                    info->id,
                    mm_operation_priority_get_string (info->priority),
                    info->description);
        self->priv->scheduled_operations = g_list_append (self->priv->scheduled_operations, info);
    } else
        g_assert_not_reached ();

    base_modem_operation_run (self);
}

/* Exclusive operation unlock */

static void
mm_base_modem_operation_unlock (MMIfaceOpLock *_self,
                                gssize         operation_id)
{
    MMBaseModem   *self = MM_BASE_MODEM (_self);
    OperationInfo *info;

    g_assert (self->priv->scheduled_operations);

    info = (OperationInfo *)(self->priv->scheduled_operations->data);
    g_assert (!info->wait_task);
    g_assert (info->id == operation_id);

    mm_obj_dbg (self, "[operation %" G_GSSIZE_FORMAT "] %s - %s: lock released",
                info->id,
                mm_operation_priority_get_string (info->priority),
                info->description);

    /* Remove head list item and free its contents */
    self->priv->scheduled_operations = g_list_delete_link (self->priv->scheduled_operations,
                                                           self->priv->scheduled_operations);
    operation_info_free (info);

    /* Run next, if any */
    base_modem_operation_run (self);
}

/*****************************************************************************/

typedef struct {
    GDBusMethodInvocation *invocation;
    MMOperationPriority    operation_priority;
    gchar                 *operation_description;
} AuthorizeAndOperationLockContext;

static void
authorize_and_operation_lock_context_free (AuthorizeAndOperationLockContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_free (ctx->operation_description);
    g_slice_free (AuthorizeAndOperationLockContext, ctx);
}

static gssize
mm_base_modem_authorize_and_operation_lock_finish (MMIfaceOpLock *self,
                                                   GAsyncResult  *res,
                                                   GError       **error)
{
    return g_task_propagate_int (G_TASK (res), error);
}

static void
lock_after_authorize_ready (MMBaseModem  *self,
                            GAsyncResult *res,
                            GTask        *task)
{
    GError *error = NULL;
    gssize  operation_id;

    operation_id = mm_base_modem_operation_lock_finish (self, res, &error);
    if (operation_id < 0)
        g_task_return_error (task, error);
    else
        g_task_return_int (task, operation_id);
    g_object_unref (task);
}

static void
authorize_before_lock_ready (MMIfaceAuth  *auth,
                             GAsyncResult *res,
                             GTask        *task)
{
    GError                           *error = NULL;
    AuthorizeAndOperationLockContext *ctx;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    ctx = g_task_get_task_data (task);
    mm_base_modem_operation_lock (MM_BASE_MODEM (auth),
                                  ctx->operation_priority,
                                  ctx->operation_description,
                                  (GAsyncReadyCallback) lock_after_authorize_ready,
                                  task);
}

static void
mm_base_modem_authorize_and_operation_lock (MMIfaceOpLock         *self,
                                            GDBusMethodInvocation *invocation,
                                            const gchar           *authorization,
                                            MMOperationPriority    operation_priority,
                                            const gchar           *operation_description,
                                            GAsyncReadyCallback    callback,
                                            gpointer               user_data)
{
    GTask                            *task;
    AuthorizeAndOperationLockContext *ctx;

    task = g_task_new (self, NULL, callback, user_data);

    ctx = g_slice_new0 (AuthorizeAndOperationLockContext);
    ctx->invocation = g_object_ref (invocation);
    ctx->operation_priority = operation_priority;
    ctx->operation_description = g_strdup (operation_description);
    g_task_set_task_data (task, ctx, (GDestroyNotify)authorize_and_operation_lock_context_free);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             authorization,
                             (GAsyncReadyCallback)authorize_before_lock_ready,
                             task);
}

/*****************************************************************************/

const gchar *
mm_base_modem_get_device (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->device;
}

const gchar *
mm_base_modem_get_physdev (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), NULL);

    return self->priv->physdev;
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

guint
mm_base_modem_get_subsystem_vendor_id (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), 0);

    return self->priv->subsystem_vendor_id;
}

guint
mm_base_modem_get_subsystem_device_id (MMBaseModem *self)
{
    g_return_val_if_fail (MM_IS_BASE_MODEM (self), 0);

    return self->priv->subsystem_device_id;
}

/*****************************************************************************/

static void
clear_invalid_from_idle (MMBaseModem *self)
{
    if (self->priv->invalid_from_idle)
        g_source_remove (self->priv->invalid_from_idle);
    self->priv->invalid_from_idle = 0;
}

static gboolean
base_modem_invalid_idle (MMBaseModem *self)
{
    clear_invalid_from_idle (self);

    /* Ensure the modem is set invalid if we get the modem-wide cancellable
     * cancelled */
    mm_base_modem_set_valid (self, FALSE);
    return G_SOURCE_REMOVE;
}

static void
base_modem_cancelled (GCancellable *cancellable,
                      MMBaseModem *self)
{
    clear_invalid_from_idle (self);

    /* NOTE: Don't call set_valid() directly here, do it in an idle, and ensure
     * that we pass a valid reference of the modem object as context. */
    self->priv->invalid_from_idle = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                                     (GSourceFunc)base_modem_invalid_idle,
                                                     g_object_ref (self),
                                                     (GDestroyNotify) g_object_unref);
}

/*****************************************************************************/

static void
setup_ports_table (GHashTable **ht)
{
    g_assert (ht && !*ht);
    *ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

typedef struct {
    volatile gint  refcount;
    GError        *error;
    /* The Context owns the GTask so that we can ensure the Task returns only
     * when all ports are closed. The reference count tracks open ports and
     * the context will return the Task when it reaches zero (indicating all
     * outstanding operations are complete and ports are closed).
     */
    GTask         *task;
} TeardownContext;

static TeardownContext *
teardown_context_new (GObject             *source_object,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
    TeardownContext *ctx;

    ctx = g_slice_new0 (TeardownContext);
    ctx->refcount = 1;
    ctx->task = g_task_new (source_object, NULL, callback, user_data);
    return ctx;
}

static void
teardown_context_ref (TeardownContext *ctx)
{
    g_atomic_int_inc (&ctx->refcount);
}

static void
teardown_context_unref (TeardownContext *ctx)
{
    if (g_atomic_int_dec_and_test (&ctx->refcount)) {
        if (ctx->error)
            g_task_return_error (ctx->task, g_steal_pointer (&ctx->error));
        else
            g_task_return_boolean (ctx->task, TRUE);
        g_clear_object (&ctx->task);
        g_slice_free (TeardownContext, ctx);
    }
}

#if defined (WITH_MBIM) || defined (WITH_QMI)

static void
teardown_context_add_error (TeardownContext *ctx,
                            MMPort          *port,
                            GError          *error)
{
    if (!ctx->error) {
        ctx->error = g_error_copy (error);
        g_prefix_error (&ctx->error,
                        "[%s] teardown error: ",
                        mm_port_get_device (port));
    } else {
        g_prefix_error (&ctx->error,
                        "[%s] teardown error: %s; ",
                        mm_port_get_device (port),
                        error->message);
    }
}

#endif

#if defined WITH_MBIM

static void
mbim_port_close_ready (MMPortMbim      *port,
                       GAsyncResult    *res,
                       TeardownContext *ctx)
{
    g_autoptr(GError) error = NULL;

    if (!mm_port_mbim_close_finish (port, res, &error))
        teardown_context_add_error (ctx, MM_PORT (port), error);
    teardown_context_unref (ctx);
}

#endif

#if defined WITH_QMI

static void
qmi_port_close_ready (MMPortQmi       *port,
                      GAsyncResult    *res,
                      TeardownContext *ctx)
{
    g_autoptr(GError) error = NULL;

    if (!mm_port_qmi_close_finish (port, res, &error))
        teardown_context_add_error (ctx, MM_PORT (port), error);
    teardown_context_unref (ctx);
}

#endif

static void
cleanup_modem_port (MMBaseModem     *self,
                    MMPort          *port,
                    TeardownContext *ctx)
{
    mm_obj_dbg (self, "cleaning up port '%s/%s'...",
                mm_port_subsys_get_string (mm_port_get_subsys (MM_PORT (port))),
                mm_port_get_device (MM_PORT (port)));

    /* Cleanup on all control ports */
    g_signal_handlers_disconnect_by_func (port, port_timed_out_cb, self);
    g_signal_handlers_disconnect_by_func (port, port_removed_cb, self);

    if (ctx)
        teardown_context_ref (ctx);

    /* No need to close serial ports here as they do not require a specific
     * shutdown procedure with message exchanges and callbacks. They will be
     * closed when the modem is invalidated or disposed.
     */

#if defined WITH_MBIM
    /* We need to close the MBIM port cleanly when disposing the modem object */
    if (MM_IS_PORT_MBIM (port)) {
        mm_port_mbim_close (MM_PORT_MBIM (port),
                            ctx ? (GAsyncReadyCallback)mbim_port_close_ready : NULL,
                            ctx);
        return;
    }
#endif

#if defined WITH_QMI
    /* We need to close the QMI port cleanly when disposing the modem object,
     * otherwise the allocated CIDs will be kept allocated, and if we end up
     * allocating too many newer allocations will fail with client-ids-exhausted
     * errors. */
    if (MM_IS_PORT_QMI (port)) {
        mm_port_qmi_close (MM_PORT_QMI (port),
                           ctx ? (GAsyncReadyCallback)qmi_port_close_ready : NULL,
                           ctx);
        return;
    }
#endif

    if (ctx)
        teardown_context_unref (ctx);
}

static void
teardown_ports_tables (MMBaseModem     *self,
                       TeardownContext *ctx)
{
    GHashTable **tables[2] = {
        &self->priv->link_ports,
        &self->priv->ports,
    };
    GHashTableIter iter;
    gpointer       value;
    gpointer       key;
    guint          i;

    for (i = 0; i < G_N_ELEMENTS (tables); i++) {
        if (*tables[i]) {
            g_hash_table_iter_init (&iter, *tables[i]);
            while (g_hash_table_iter_next (&iter, &key, &value))
                cleanup_modem_port (self, MM_PORT (value), ctx);
            g_hash_table_destroy (*tables[i]);
            *tables[i] = NULL;
        }
    }
}

gboolean
mm_base_modem_teardown_ports_finish (MMBaseModem   *self,
                                     GAsyncResult  *res,
                                     GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_base_modem_teardown_ports (MMBaseModem         *self,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
    TeardownContext *ctx;

    self->priv->torn_down = TRUE;

    ctx = teardown_context_new (G_OBJECT (self), callback, user_data);
    teardown_ports_tables (self, ctx);
    teardown_context_unref (ctx);
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

    setup_ports_table (&self->priv->ports);
    setup_ports_table (&self->priv->link_ports);
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
    case PROP_PHYSDEV:
        g_free (self->priv->physdev);
        self->priv->physdev = g_value_dup_string (value);
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
    case PROP_SUBSYSTEM_VENDOR_ID:
        self->priv->subsystem_vendor_id = g_value_get_uint (value);
        break;
    case PROP_SUBSYSTEM_DEVICE_ID:
        self->priv->subsystem_device_id = g_value_get_uint (value);
        break;
    case PROP_CONNECTION:
        g_clear_object (&self->priv->connection);
        self->priv->connection = g_value_dup_object (value);
        break;
    case PROP_DATA_NET_SUPPORTED:
        self->priv->data_net_supported = g_value_get_boolean (value);
        break;
    case PROP_DATA_TTY_SUPPORTED:
        self->priv->data_tty_supported = g_value_get_boolean (value);
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
    case PROP_PHYSDEV:
        g_value_set_string (value, self->priv->physdev);
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
    case PROP_SUBSYSTEM_VENDOR_ID:
        g_value_set_uint (value, self->priv->subsystem_vendor_id);
        break;
    case PROP_SUBSYSTEM_DEVICE_ID:
        g_value_set_uint (value, self->priv->subsystem_device_id);
        break;
    case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
    case PROP_DATA_NET_SUPPORTED:
        g_value_set_boolean (value, self->priv->data_net_supported);
        break;
    case PROP_DATA_TTY_SUPPORTED:
        g_value_set_boolean (value, self->priv->data_tty_supported);
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

    g_assert (!self->priv->scheduled_operations);

    mm_obj_dbg (self, "completely disposed");

    g_free (self->priv->device);
    g_free (self->priv->physdev);
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

    clear_invalid_from_idle (self);

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

    if (!self->priv->torn_down)
        mm_obj_warn (self, "teardown not called before dispose");
    teardown_ports_tables (self, NULL);

    g_clear_object (&self->priv->connection);

    G_OBJECT_CLASS (mm_base_modem_parent_class)->dispose (object);
}

static void
log_object_iface_init (MMLogObjectInterface *iface)
{
    iface->build_id = log_object_build_id;
}

static void
auth_iface_init (MMIfaceAuthInterface *iface)
{
    iface->authorize = mm_base_modem_authorize;
    iface->authorize_finish = mm_base_modem_authorize_finish;
}

static void
op_lock_iface_init (MMIfaceOpLockInterface *iface)
{
    iface->authorize_and_lock = mm_base_modem_authorize_and_operation_lock;
    iface->authorize_and_lock_finish = mm_base_modem_authorize_and_operation_lock_finish;
    iface->unlock = mm_base_modem_operation_unlock;
}

static void
mm_base_modem_class_init (MMBaseModemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBaseModemPrivate));

    klass->create_tty_port = base_modem_create_tty_port;
    klass->create_usbmisc_port = base_modem_create_usbmisc_port;
    klass->create_wwan_port = base_modem_create_wwan_port;

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
                             "Main modem parent device of all the modem's ports",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DEVICE, properties[PROP_DEVICE]);

    properties[PROP_PHYSDEV] =
        g_param_spec_string (MM_BASE_MODEM_PHYSDEV,
                             "Physdev path",
                             "Main modem parent physical device path",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PHYSDEV, properties[PROP_PHYSDEV]);

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

    properties[PROP_SUBSYSTEM_VENDOR_ID] =
        g_param_spec_uint (MM_BASE_MODEM_SUBSYSTEM_VENDOR_ID,
                           "Hardware subsystem vendor ID",
                           "Hardware subsystem vendor ID. Available for pci devices.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_SUBSYSTEM_VENDOR_ID, properties[PROP_SUBSYSTEM_VENDOR_ID]);

    properties[PROP_SUBSYSTEM_DEVICE_ID] =
        g_param_spec_uint (MM_BASE_MODEM_SUBSYSTEM_DEVICE_ID,
                           "Hardware subsystem device ID",
                           "Hardware subsystem device ID. Available for pci devices.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_SUBSYSTEM_DEVICE_ID, properties[PROP_SUBSYSTEM_DEVICE_ID]);

    properties[PROP_CONNECTION] =
        g_param_spec_object (MM_BINDABLE_CONNECTION,
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

    properties[PROP_DATA_NET_SUPPORTED] =
        g_param_spec_boolean (MM_BASE_MODEM_DATA_NET_SUPPORTED,
                              "Data NET supported",
                              "Whether the modem supports connection via a NET port.",
                              FALSE,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_DATA_NET_SUPPORTED, properties[PROP_DATA_NET_SUPPORTED]);

    properties[PROP_DATA_TTY_SUPPORTED] =
        g_param_spec_boolean (MM_BASE_MODEM_DATA_TTY_SUPPORTED,
                              "Data TTY supported",
                              "Whether the modem supports connection via a TTY port.",
                              FALSE,
                              G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_DATA_TTY_SUPPORTED, properties[PROP_DATA_TTY_SUPPORTED]);

    signals[SIGNAL_LINK_PORT_GRABBED] =
        g_signal_new (MM_BASE_MODEM_SIGNAL_LINK_PORT_GRABBED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMBaseModemClass, link_port_grabbed),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, MM_TYPE_PORT);

    signals[SIGNAL_LINK_PORT_RELEASED] =
        g_signal_new (MM_BASE_MODEM_SIGNAL_LINK_PORT_RELEASED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMBaseModemClass, link_port_released),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, MM_TYPE_PORT);
}
