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

#ifndef MM_IFACE_MODEM_FIRMWARE_H
#define MM_IFACE_MODEM_FIRMWARE_H

#include <glib-object.h>
#include <gio/gio.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"

#define MM_TYPE_IFACE_MODEM_FIRMWARE mm_iface_modem_firmware_get_type ()
G_DECLARE_INTERFACE (MMIfaceModemFirmware, mm_iface_modem_firmware, MM, IFACE_MODEM_FIRMWARE, MMIfaceModem)

#define MM_IFACE_MODEM_FIRMWARE_DBUS_SKELETON  "iface-modem-firmware-dbus-skeleton"
#define MM_IFACE_MODEM_FIRMWARE_IGNORE_CARRIER "iface-modem-firmware-ignore-carrier"

struct _MMIfaceModemFirmwareInterface {
    GTypeInterface g_iface;

    /* Get update settings (async) */
    void                       (* load_update_settings)        (MMIfaceModemFirmware  *self,
                                                                GAsyncReadyCallback    callback,
                                                                gpointer               user_data);
    MMFirmwareUpdateSettings * (* load_update_settings_finish) (MMIfaceModemFirmware  *self,
                                                                GAsyncResult          *res,
                                                                GError               **error);

    /* Get Firmware list (async) */
    void (* load_list) (MMIfaceModemFirmware *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data);
    GList * (* load_list_finish) (MMIfaceModemFirmware *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Get current firmware (async) */
    void (* load_current) (MMIfaceModemFirmware *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
    MMFirmwareProperties * (* load_current_finish) (MMIfaceModemFirmware *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Change current firmware (async) */
    void (* change_current) (MMIfaceModemFirmware *self,
                             const gchar *name,
                             GAsyncReadyCallback callback,
                             gpointer user_data);
    gboolean (* change_current_finish) (MMIfaceModemFirmware *self,
                                        GAsyncResult *res,
                                        GError **error);
};

/* Get generic device ids */
GPtrArray *mm_iface_firmware_build_generic_device_ids (MMIfaceModemFirmware  *self,
                                                       GError               **error);

/* Initialize Firmware interface (async) */
void     mm_iface_modem_firmware_initialize        (MMIfaceModemFirmware *self,
                                                    GCancellable *cancellable,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data);
gboolean mm_iface_modem_firmware_initialize_finish (MMIfaceModemFirmware *self,
                                                    GAsyncResult *res,
                                                    GError **error);

/* Shutdown Firmware interface */
void mm_iface_modem_firmware_shutdown (MMIfaceModemFirmware *self);

/* Bind properties for simple GetStatus() */
void mm_iface_modem_firmware_bind_simple_status (MMIfaceModemFirmware *self,
                                                 MMSimpleStatus *status);

/* Helper to load common update settings on the primary port */
void mm_iface_modem_firmware_load_update_settings_in_port (
    MMIfaceModemFirmware *self,
    MMPort               *port,
    GAsyncReadyCallback   callback,
    gpointer              user_data);
MMFirmwareUpdateSettings *mm_iface_modem_firmware_load_update_settings_in_port_finish (
    MMIfaceModemFirmware  *self,
    GAsyncResult          *res,
    GError               **error);

#endif /* MM_IFACE_MODEM_FIRMWARE_H */
