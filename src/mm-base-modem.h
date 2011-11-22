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

#include <glib.h>
#include <glib-object.h>

#include <mm-gdbus-modem.h>

#include "mm-port.h"
#include "mm-at-serial-port.h"
#include "mm-qcdm-serial-port.h"
#include "mm-modem.h"

#define MM_TYPE_BASE_MODEM            (mm_base_modem_get_type ())
#define MM_BASE_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BASE_MODEM, MMBaseModem))
#define MM_BASE_MODEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BASE_MODEM, MMBaseModemClass))
#define MM_IS_BASE_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BASE_MODEM))
#define MM_IS_BASE_MODEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BASE_MODEM))
#define MM_BASE_MODEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BASE_MODEM, MMBaseModemClass))

typedef struct _MMBaseModem MMBaseModem;
typedef struct _MMBaseModemClass MMBaseModemClass;
typedef struct _MMBaseModemPrivate MMBaseModemPrivate;

#define MM_BASE_MODEM_MAX_TIMEOUTS   "base-modem-max-timeouts"
#define MM_BASE_MODEM_VALID          "base-modem-valid"
#define MM_BASE_MODEM_DEVICE         "base-modem-device"
#define MM_BASE_MODEM_DRIVER         "base-modem-driver"
#define MM_BASE_MODEM_PLUGIN         "base-modem-plugin"
#define MM_BASE_MODEM_VENDOR_ID      "base-modem-vendor-id"
#define MM_BASE_MODEM_PRODUCT_ID     "base-modem-product-id"

struct _MMBaseModem {
    MmGdbusObjectSkeleton parent;
    MMBaseModemPrivate *priv;
};

struct _MMBaseModemClass {
    MmGdbusObjectSkeletonClass parent;
};

GType mm_base_modem_get_type (void);

gboolean  mm_base_modem_grab_port    (MMBaseModem *self,
                                      const gchar *subsys,
                                      const gchar *name,
                                      MMPortType suggested_type);
void      mm_base_modem_release_port (MMBaseModem *self,
                                      const gchar *subsys,
                                      const gchar *name);
MMPort   *mm_base_modem_get_port     (MMBaseModem *self,
                                      const gchar *subsys,
                                      const gchar *name);
gboolean  mm_base_modem_owns_port    (MMBaseModem *self,
                                      const gchar *subsys,
                                      const gchar *name);

MMAtSerialPort   *mm_base_modem_get_port_primary   (MMBaseModem *self);
MMAtSerialPort   *mm_base_modem_get_port_secondary (MMBaseModem *self);
MMQcdmSerialPort *mm_base_modem_get_port_qcdm      (MMBaseModem *self);

void     mm_base_modem_set_valid    (MMBaseModem *self,
                                     gboolean valid);
gboolean mm_base_modem_get_valid    (MMBaseModem *self);

gboolean mm_base_modem_auth_request (MMBaseModem *self,
                                     const gchar *authorization,
                                     GDBusMethodInvocation *context,
                                     MMAuthRequestCb callback,
                                     gpointer callback_data,
                                     GDestroyNotify notify,
                                     GError **error);
gboolean mm_base_modem_auth_finish  (MMBaseModem *self,
                                     MMAuthRequest *req,
                                     GError **error);

const gchar *mm_base_modem_get_device (MMBaseModem *self);
const gchar *mm_base_modem_get_driver (MMBaseModem *self);
const gchar *mm_base_modem_get_plugin (MMBaseModem *self);

guint mm_base_modem_get_vendor_id  (MMBaseModem *self);
guint mm_base_modem_get_product_id (MMBaseModem *self);

#endif /* MM_BASE_MODEM_H */

