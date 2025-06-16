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

#ifndef MM_BASE_MODEM_H
#define MM_BASE_MODEM_H

#include "config.h"

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <mm-gdbus-modem.h>

#include "mm-auth-provider.h"
#include "mm-kernel-device.h"
#include "mm-port.h"
#include "mm-port-net.h"
#include "mm-port-serial-at.h"
#include "mm-port-serial-qcdm.h"
#include "mm-port-serial-gps.h"
#include "mm-iface-port-at.h"
#include "mm-iface-op-lock.h"

#if defined WITH_QMI
#include "mm-port-qmi.h"
#endif

#if defined WITH_MBIM
#include "mm-port-mbim.h"
#endif

#define MM_TYPE_BASE_MODEM            (mm_base_modem_get_type ())
#define MM_BASE_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BASE_MODEM, MMBaseModem))
#define MM_BASE_MODEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BASE_MODEM, MMBaseModemClass))
#define MM_IS_BASE_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BASE_MODEM))
#define MM_IS_BASE_MODEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BASE_MODEM))
#define MM_BASE_MODEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BASE_MODEM, MMBaseModemClass))

typedef struct _MMBaseModem MMBaseModem;
typedef struct _MMBaseModemClass MMBaseModemClass;
typedef struct _MMBaseModemPrivate MMBaseModemPrivate;

#define MM_BASE_MODEM_MAX_TIMEOUTS        "base-modem-max-timeouts"
#define MM_BASE_MODEM_VALID               "base-modem-valid"
#define MM_BASE_MODEM_DEVICE              "base-modem-device"
#define MM_BASE_MODEM_PHYSDEV             "base-modem-physdev-path"
#define MM_BASE_MODEM_DRIVERS             "base-modem-drivers"
#define MM_BASE_MODEM_PLUGIN              "base-modem-plugin"
#define MM_BASE_MODEM_VENDOR_ID           "base-modem-vendor-id"
#define MM_BASE_MODEM_PRODUCT_ID          "base-modem-product-id"
#define MM_BASE_MODEM_SUBSYSTEM_VENDOR_ID "base-modem-subsystem-vendor-id"
#define MM_BASE_MODEM_SUBSYSTEM_DEVICE_ID "base-modem-subsystem-device-id"
#define MM_BASE_MODEM_REPROBE             "base-modem-reprobe"
#define MM_BASE_MODEM_DATA_NET_SUPPORTED  "base-modem-data-net-supported"
#define MM_BASE_MODEM_DATA_TTY_SUPPORTED  "base-modem-data-tty-supported"

#define MM_BASE_MODEM_SIGNAL_LINK_PORT_GRABBED  "base-modem-link-port-grabbed"
#define MM_BASE_MODEM_SIGNAL_LINK_PORT_RELEASED "base-modem-link-port-released"

struct _MMBaseModem {
    MmGdbusObjectSkeleton parent;
    MMBaseModemPrivate *priv;
};

/* Common state operation definitions */
typedef void     (* StateOperation)      (MMBaseModem          *self,
                                          GCancellable         *cancellable,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
typedef gboolean (*StateOperationFinish) (MMBaseModem          *self,
                                          GAsyncResult         *res,
                                          GError              **error);

struct _MMBaseModemClass {
    MmGdbusObjectSkeletonClass parent;

    /* Modem initialization. As soon as the ports are organized, this method gets called */
    StateOperation       initialize;
    StateOperationFinish initialize_finish;

    /* Modem enabling. User action requested from DBus, usually */
    StateOperation       enable;
    StateOperationFinish enable_finish;

    /* Modem disabling. Either user action or internally triggered. */
    StateOperation       disable;
    StateOperationFinish disable_finish;

#if defined WITH_SUSPEND_RESUME
    /* Modem synchronization.
     * When resuming in quick suspend/resume mode,
     * this method triggers a synchronization of all modem interfaces */
    StateOperation       sync;
    StateOperationFinish sync_finish;

    /* Modem terse.
     * When suspending in quick suspend/resume mode,
     * this method disables unsolicited events on the 3GPP interface only.
     * This is enough for phones to suspend properly, but it
     * might be useful to extend it to all other interfaces
     * that support it as well */
    StateOperation       terse;
    StateOperationFinish terse_finish;
#endif

    /* Allow plugins to subclass port object creation as needed */
    MMPort * (* create_tty_port)     (MMBaseModem    *self,
                                      const gchar    *name,
                                      MMKernelDevice *kernel_device,
                                      MMPortType      ptype);
    MMPort * (* create_usbmisc_port) (MMBaseModem    *self,
                                      const gchar    *name,
                                      MMPortType      ptype);
    MMPort * (* create_wwan_port)    (MMBaseModem    *self,
                                      const gchar    *name,
                                      MMPortType      ptype);

    /* signals */
    void (* link_port_grabbed)  (MMBaseModem *self,
                                 MMPort      *link_port);
    void (* link_port_released) (MMBaseModem *self,
                                 MMPort      *link_port);
};

GType mm_base_modem_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBaseModem, g_object_unref)

guint     mm_base_modem_get_dbus_id  (MMBaseModem *self);

gboolean  mm_base_modem_grab_port         (MMBaseModem         *self,
                                           MMKernelDevice      *kernel_device,
                                           MMPortGroup          pgroup,
                                           MMPortType           ptype,
                                           MMPortSerialAtFlag   at_pflags,
                                           GError             **error);
gboolean  mm_base_modem_grab_link_port    (MMBaseModem         *self,
                                           MMKernelDevice      *kernel_device,
                                           GError             **error);
gboolean  mm_base_modem_release_link_port (MMBaseModem         *self,
                                           const gchar         *subsystem,
                                           const gchar         *name,
                                           GError             **error);

void      mm_base_modem_wait_link_port        (MMBaseModem          *self,
                                               const gchar          *subsystem,
                                               const gchar          *name,
                                               guint                 timeout_ms,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data);
MMPort   *mm_base_modem_wait_link_port_finish (MMBaseModem          *self,
                                               GAsyncResult         *res,
                                               GError              **error);

gboolean  mm_base_modem_organize_ports (MMBaseModem *self,
                                        GError **error);

MMPortSerialAt   *mm_base_modem_peek_port_primary      (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_peek_port_secondary    (MMBaseModem *self);
MMPortSerialQcdm *mm_base_modem_peek_port_qcdm         (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_peek_port_gps_control  (MMBaseModem *self);
MMPortSerialGps  *mm_base_modem_peek_port_gps          (MMBaseModem *self);
MMPortSerial     *mm_base_modem_peek_port_audio        (MMBaseModem *self);
MMIfacePortAt    *mm_base_modem_peek_best_at_port      (MMBaseModem *self, GError **error);
MMPort           *mm_base_modem_peek_best_data_port    (MMBaseModem *self, MMPortType type);
GList            *mm_base_modem_peek_data_ports        (MMBaseModem *self);

MMPortSerialAt   *mm_base_modem_get_port_primary      (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_get_port_secondary    (MMBaseModem *self);
MMPortSerialQcdm *mm_base_modem_get_port_qcdm         (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_get_port_gps_control  (MMBaseModem *self);
MMPortSerialGps  *mm_base_modem_get_port_gps          (MMBaseModem *self);
MMPortSerial     *mm_base_modem_get_port_audio        (MMBaseModem *self);
MMIfacePortAt    *mm_base_modem_get_best_at_port      (MMBaseModem *self, GError **error);
MMPort           *mm_base_modem_get_best_data_port    (MMBaseModem *self, MMPortType type);
GList            *mm_base_modem_get_data_ports        (MMBaseModem *self);

MMModemPortInfo *mm_base_modem_get_port_infos         (MMBaseModem *self,
                                                       guint *n_port_infos);
MMModemPortInfo *mm_base_modem_get_ignored_port_infos (MMBaseModem *self,
                                                       guint       *n_port_infos);

GList            *mm_base_modem_find_ports            (MMBaseModem  *self,
                                                       MMPortSubsys  subsys,
                                                       MMPortType    type);
MMPort           *mm_base_modem_peek_port             (MMBaseModem  *self,
                                                       const gchar  *name);
MMPort           *mm_base_modem_get_port              (MMBaseModem  *self,
                                                       const gchar  *name);

void     mm_base_modem_set_hotplugged (MMBaseModem *self,
                                       gboolean hotplugged);
gboolean mm_base_modem_get_hotplugged (MMBaseModem *self);

void     mm_base_modem_set_valid    (MMBaseModem *self,
                                     gboolean valid);
gboolean mm_base_modem_get_valid    (MMBaseModem *self);

void     mm_base_modem_set_reprobe (MMBaseModem *self,
                                    gboolean reprobe);
gboolean mm_base_modem_get_reprobe (MMBaseModem *self);

const gchar  *mm_base_modem_get_device  (MMBaseModem *self);
const gchar  *mm_base_modem_get_physdev (MMBaseModem *self);
const gchar **mm_base_modem_get_drivers (MMBaseModem *self);
const gchar  *mm_base_modem_get_plugin  (MMBaseModem *self);

guint mm_base_modem_get_vendor_id           (MMBaseModem *self);
guint mm_base_modem_get_product_id          (MMBaseModem *self);
guint mm_base_modem_get_subsystem_vendor_id (MMBaseModem *self);
guint mm_base_modem_get_subsystem_device_id (MMBaseModem *self);

GCancellable *mm_base_modem_peek_cancellable (MMBaseModem *self);

/******************************************************************************/
/* State operations */

void     mm_base_modem_initialize        (MMBaseModem          *self,
                                          MMOperationLock       operation_lock,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
gboolean mm_base_modem_initialize_finish (MMBaseModem          *self,
                                          GAsyncResult         *res,
                                          GError              **error);

void     mm_base_modem_enable            (MMBaseModem          *self,
                                          MMOperationLock       operation_lock,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
gboolean mm_base_modem_enable_finish     (MMBaseModem          *self,
                                          GAsyncResult         *res,
                                          GError              **error);

void     mm_base_modem_disable           (MMBaseModem          *self,
                                          MMOperationLock       operation_lock,
                                          MMOperationPriority   priority,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
gboolean mm_base_modem_disable_finish    (MMBaseModem          *self,
                                          GAsyncResult         *res,
                                          GError              **error);

#if defined WITH_SUSPEND_RESUME
void     mm_base_modem_sync              (MMBaseModem          *self,
                                          MMOperationLock       operation_lock,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
gboolean mm_base_modem_sync_finish       (MMBaseModem          *self,
                                          GAsyncResult         *res,
                                          GError              **error);
void     mm_base_modem_terse             (MMBaseModem              *self,
                                          MMOperationLock   operation_lock,
                                          GAsyncReadyCallback       callback,
                                          gpointer                  user_data);
gboolean mm_base_modem_terse_finish      (MMBaseModem              *self,
                                          GAsyncResult             *res,
                                          GError                  **error);
#endif

void     mm_base_modem_teardown_ports        (MMBaseModem         *self,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data);
gboolean mm_base_modem_teardown_ports_finish (MMBaseModem          *self,
                                              GAsyncResult         *res,
                                              GError              **error);

#endif /* MM_BASE_MODEM_H */
