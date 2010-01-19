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
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef MM_MODEM_H
#define MM_MODEM_H

#include <glib-object.h>

#include "mm-port.h"

typedef enum {
    MM_MODEM_STATE_UNKNOWN = 0,
    MM_MODEM_STATE_DISABLED = 10,
    MM_MODEM_STATE_DISABLING = 20,
    MM_MODEM_STATE_ENABLING = 30,
    MM_MODEM_STATE_ENABLED = 40,
    MM_MODEM_STATE_SEARCHING = 50,
    MM_MODEM_STATE_REGISTERED = 60,
    MM_MODEM_STATE_DISCONNECTING = 70,
    MM_MODEM_STATE_CONNECTING = 80,
    MM_MODEM_STATE_CONNECTED = 90,

    MM_MODEM_STATE_LAST = MM_MODEM_STATE_CONNECTED
} MMModemState;

typedef enum {
    MM_MODEM_STATE_REASON_NONE = 0
} MMModemStateReason;

#define DBUS_PATH_TAG "dbus-path"

#define MM_TYPE_MODEM      (mm_modem_get_type ())
#define MM_MODEM(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM, MMModem))
#define MM_IS_MODEM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM))
#define MM_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM, MMModem))

#define MM_MODEM_DBUS_INTERFACE "org.freedesktop.ModemManager.Modem"

#define MM_MODEM_DATA_DEVICE   "device"
#define MM_MODEM_MASTER_DEVICE "master-device"
#define MM_MODEM_DRIVER        "driver"
#define MM_MODEM_TYPE          "type"
#define MM_MODEM_IP_METHOD     "ip-method"
#define MM_MODEM_ENABLED       "enabled"
#define MM_MODEM_VALID         "valid"      /* not exported */
#define MM_MODEM_PLUGIN        "plugin"     /* not exported */
#define MM_MODEM_STATE         "state"      /* not exported */

#define MM_MODEM_TYPE_UNKNOWN  0
#define MM_MODEM_TYPE_GSM      1
#define MM_MODEM_TYPE_CDMA     2

#define MM_MODEM_IP_METHOD_PPP    0
#define MM_MODEM_IP_METHOD_STATIC 1
#define MM_MODEM_IP_METHOD_DHCP   2

typedef enum {
    MM_MODEM_PROP_FIRST = 0x1000,

    MM_MODEM_PROP_DATA_DEVICE = MM_MODEM_PROP_FIRST,
    MM_MODEM_PROP_MASTER_DEVICE,
    MM_MODEM_PROP_DRIVER,
    MM_MODEM_PROP_TYPE,
    MM_MODEM_PROP_IP_METHOD,
    MM_MODEM_PROP_VALID,       /* Not exported */
    MM_MODEM_PROP_PLUGIN,      /* Not exported */
    MM_MODEM_PROP_STATE,       /* Not exported */
    MM_MODEM_PROP_ENABLED
} MMModemProp;

typedef struct _MMModem MMModem;

typedef void (*MMModemFn) (MMModem *modem,
                           GError *error,
                           gpointer user_data);

typedef void (*MMModemUIntFn) (MMModem *modem,
                               guint32 result,
                               GError *error,
                               gpointer user_data);

typedef void (*MMModemStringFn) (MMModem *modem,
                                 const char *result,
                                 GError *error,
                                 gpointer user_data);

typedef void (*MMModemIp4Fn) (MMModem *modem,
                              guint32 address,
                              GArray *dns,
                              GError *error,
                              gpointer user_data);

typedef void (*MMModemInfoFn) (MMModem *modem,
                               const char *manufacturer,
                               const char *model,
                               const char *version,
                               GError *error,
                               gpointer user_data);

struct _MMModem {
    GTypeInterface g_iface;

    /* Methods */
    gboolean (*owns_port) (MMModem *self,
                           const char *subsys,
                           const char *name);

    gboolean (*grab_port) (MMModem *self,
                           const char *subsys,
                           const char *name,
                           MMPortType suggested_type,
                           gpointer user_data,
                           GError **error);

    void (*release_port) (MMModem *self,
                          const char *subsys,
                          const char *name);

    void (*enable) (MMModem *self,
                    MMModemFn callback,
                    gpointer user_data);

    void (*disable) (MMModem *self,
                     MMModemFn callback,
                     gpointer user_data);

    void (*connect) (MMModem *self,
                     const char *number,
                     MMModemFn callback,
                     gpointer user_data);

    void (*get_ip4_config) (MMModem *self,
                            MMModemIp4Fn callback,
                            gpointer user_data);

    void (*disconnect) (MMModem *self,
                        MMModemFn callback,
                        gpointer user_data);

    void (*get_info) (MMModem *self,
                      MMModemInfoFn callback,
                      gpointer user_data);

    /* Signals */
    void (*state_changed) (MMModem *self,
                           MMModemState new_state,
                           MMModemState old_state,
                           MMModemStateReason reason);
};

GType mm_modem_get_type (void);

gboolean mm_modem_owns_port (MMModem *self,
                             const char *subsys,
                             const char *name);

gboolean mm_modem_grab_port (MMModem *self,
                             const char *subsys,
                             const char *name,
                             MMPortType suggested_type,
                             gpointer user_data,
                             GError **error);

void mm_modem_release_port (MMModem *self,
                            const char *subsys,
                            const char *name);

void mm_modem_enable (MMModem *self,
                      MMModemFn callback,
                      gpointer user_data);

void mm_modem_disable (MMModem *self,
                       MMModemFn callback,
                       gpointer user_data);

void mm_modem_connect (MMModem *self,
                       const char *number,
                       MMModemFn callback,
                       gpointer user_data);

void mm_modem_get_ip4_config (MMModem *self,
                              MMModemIp4Fn callback,
                              gpointer user_data);

void mm_modem_disconnect (MMModem *self,
                          MMModemFn callback,
                          gpointer user_data);

void mm_modem_get_info (MMModem *self,
                        MMModemInfoFn callback,
                        gpointer user_data);

gboolean mm_modem_get_valid (MMModem *self);

char *mm_modem_get_device (MMModem *self);

MMModemState mm_modem_get_state (MMModem *self);

void mm_modem_set_state (MMModem *self,
                         MMModemState new_state,
                         MMModemStateReason reason);

GError *mm_modem_check_removed (MMModem *self, const GError *error);

#endif  /* MM_MODEM_H */

