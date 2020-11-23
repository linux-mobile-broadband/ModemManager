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
#include "mm-port.h"
#include "mm-kernel-device.h"
#include "mm-port-serial-at.h"
#include "mm-port-serial-qcdm.h"
#include "mm-port-serial-gps.h"

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

#define MM_BASE_MODEM_CONNECTION     "base-modem-connection"
#define MM_BASE_MODEM_MAX_TIMEOUTS   "base-modem-max-timeouts"
#define MM_BASE_MODEM_VALID          "base-modem-valid"
#define MM_BASE_MODEM_DEVICE         "base-modem-device"
#define MM_BASE_MODEM_DRIVERS        "base-modem-drivers"
#define MM_BASE_MODEM_PLUGIN         "base-modem-plugin"
#define MM_BASE_MODEM_VENDOR_ID      "base-modem-vendor-id"
#define MM_BASE_MODEM_PRODUCT_ID     "base-modem-product-id"
#define MM_BASE_MODEM_REPROBE        "base-modem-reprobe"

struct _MMBaseModem {
    MmGdbusObjectSkeleton parent;
    MMBaseModemPrivate *priv;
};

struct _MMBaseModemClass {
    MmGdbusObjectSkeletonClass parent;

    /* Modem initialization.
     * As soon as the ports are organized, this method gets called */
    void (* initialize) (MMBaseModem *self,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
    gboolean (*initialize_finish) (MMBaseModem *self,
                                   GAsyncResult *res,
                                   GError **error);

    /* Modem enabling.
     * User action requested from DBus, usually */
    void (* enable) (MMBaseModem *self,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data);
    gboolean (*enable_finish) (MMBaseModem *self,
                               GAsyncResult *res,
                               GError **error);

    /* Modem disabling.
     * User action requested from DBus, usually */
    void (* disable) (MMBaseModem *self,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data);
    gboolean (*disable_finish) (MMBaseModem *self,
                                GAsyncResult *res,
                                GError **error);
};

GType mm_base_modem_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMBaseModem, g_object_unref)

guint     mm_base_modem_get_dbus_id  (MMBaseModem *self);

gboolean  mm_base_modem_grab_port    (MMBaseModem         *self,
                                      MMKernelDevice      *kernel_device,
                                      MMPortType           ptype,
                                      MMPortSerialAtFlag   at_pflags,
                                      GError             **error);

gboolean  mm_base_modem_has_at_port  (MMBaseModem *self);

gboolean  mm_base_modem_organize_ports (MMBaseModem *self,
                                        GError **error);

MMPortSerialAt   *mm_base_modem_peek_port_primary      (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_peek_port_secondary    (MMBaseModem *self);
MMPortSerialQcdm *mm_base_modem_peek_port_qcdm         (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_peek_port_gps_control  (MMBaseModem *self);
MMPortSerialGps  *mm_base_modem_peek_port_gps          (MMBaseModem *self);
MMPortSerial     *mm_base_modem_peek_port_audio        (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_peek_best_at_port      (MMBaseModem *self, GError **error);
MMPort           *mm_base_modem_peek_best_data_port    (MMBaseModem *self, MMPortType type);
GList            *mm_base_modem_peek_data_ports        (MMBaseModem *self);

MMPortSerialAt   *mm_base_modem_get_port_primary      (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_get_port_secondary    (MMBaseModem *self);
MMPortSerialQcdm *mm_base_modem_get_port_qcdm         (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_get_port_gps_control  (MMBaseModem *self);
MMPortSerialGps  *mm_base_modem_get_port_gps          (MMBaseModem *self);
MMPortSerial     *mm_base_modem_get_port_audio        (MMBaseModem *self);
MMPortSerialAt   *mm_base_modem_get_best_at_port      (MMBaseModem *self, GError **error);
MMPort           *mm_base_modem_get_best_data_port    (MMBaseModem *self, MMPortType type);
GList            *mm_base_modem_get_data_ports        (MMBaseModem *self);

MMModemPortInfo *mm_base_modem_get_port_infos         (MMBaseModem *self,
                                                       guint *n_port_infos);

GList            *mm_base_modem_find_ports            (MMBaseModem *self,
                                                       MMPortSubsys subsys,
                                                       MMPortType type,
                                                       const gchar *name);

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
const gchar **mm_base_modem_get_drivers (MMBaseModem *self);
const gchar  *mm_base_modem_get_plugin  (MMBaseModem *self);

guint mm_base_modem_get_vendor_id  (MMBaseModem *self);
guint mm_base_modem_get_product_id (MMBaseModem *self);

GCancellable *mm_base_modem_peek_cancellable (MMBaseModem *self);
GCancellable *mm_base_modem_get_cancellable  (MMBaseModem *self);

void     mm_base_modem_authorize        (MMBaseModem *self,
                                         GDBusMethodInvocation *invocation,
                                         const gchar *authorization,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
gboolean mm_base_modem_authorize_finish (MMBaseModem *self,
                                         GAsyncResult *res,
                                         GError **error);

void     mm_base_modem_initialize        (MMBaseModem *self,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data);
gboolean mm_base_modem_initialize_finish (MMBaseModem *self,
                                          GAsyncResult *res,
                                          GError **error);

void     mm_base_modem_enable        (MMBaseModem *self,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean mm_base_modem_enable_finish (MMBaseModem *self,
                                      GAsyncResult *res,
                                      GError **error);

void     mm_base_modem_disable        (MMBaseModem *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
gboolean mm_base_modem_disable_finish (MMBaseModem *self,
                                       GAsyncResult *res,
                                       GError **error);

void mm_base_modem_process_sim_event (MMBaseModem *self);

#endif /* MM_BASE_MODEM_H */
