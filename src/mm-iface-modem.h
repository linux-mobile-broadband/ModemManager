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
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_IFACE_MODEM_H
#define MM_IFACE_MODEM_H

#include <glib-object.h>
#include <gio/gio.h>

#define MM_TYPE_IFACE_MODEM            (mm_iface_modem_get_type ())
#define MM_IFACE_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM, MMIfaceModem))
#define MM_IS_IFACE_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM))
#define MM_IFACE_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM, MMIfaceModem))

#define MM_IFACE_MODEM_DBUS_SKELETON   "iface-modem-dbus-skeleton"
#define MM_IFACE_MODEM_STATE           "iface-modem-state"
#define MM_IFACE_MODEM_SIM             "iface-modem-sim"

typedef struct _MMIfaceModem MMIfaceModem;

struct _MMIfaceModem {
    GTypeInterface g_iface;

    /* Loading of the ModemCapabilities property */
    void (*load_modem_capabilities) (MMIfaceModem *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    MMModemCapability (*load_modem_capabilities_finish) (MMIfaceModem *self,
                                                         GAsyncResult *res,
                                                         GError **error);

    /* Loading of the CurrentCapabilities property */
    void (*load_current_capabilities) (MMIfaceModem *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    MMModemCapability (*load_current_capabilities_finish) (MMIfaceModem *self,
                                                           GAsyncResult *res,
                                                           GError **error);

    /* Loading of the MaxBearers property */
    void (*load_max_bearers) (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
    guint (*load_max_bearers_finish) (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GError **error);

    /* Loading of the MaxActiveBearers property */
    void (*load_max_active_bearers) (MMIfaceModem *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    guint (*load_max_active_bearers_finish) (MMIfaceModem *self,
                                             GAsyncResult *res,
                                             GError **error);

    /* Loading of the Manufacturer property */
    void (*load_manufacturer) (MMIfaceModem *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    gchar * (*load_manufacturer_finish) (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error);

    /* Loading of the Model property */
    void (*load_model) (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data);
    gchar * (*load_model_finish) (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Loading of the Revision property */
    void (*load_revision) (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
    gchar * (*load_revision_finish) (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error);

    /* Loading of the EquipmentIdentifier property */
    void (*load_equipment_identifier) (MMIfaceModem *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    gchar * (*load_equipment_identifier_finish) (MMIfaceModem *self,
                                                 GAsyncResult *res,
                                                 GError **error);

    /* Loading of the DeviceIdentifier property */
    void (*load_device_identifier) (MMIfaceModem *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
    gchar * (*load_device_identifier_finish) (MMIfaceModem *self,
                                              GAsyncResult *res,
                                              GError **error);

    /* Loading of the UnlockRequired property */
    void (*load_unlock_required) (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    MMModemLock (*load_unlock_required_finish) (MMIfaceModem *self,
                                                GAsyncResult *res,
                                                GError **error);

    /* Loading of the UnlockRetries property */
    void (*load_unlock_retries) (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);
    MMModemLock (*load_unlock_retries_finish) (MMIfaceModem *self,
                                               GAsyncResult *res,
                                               GError **error);

    /* Loading of the SupportedModes property */
    void (*load_supported_modes) (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    MMModemMode (*load_supported_modes_finish) (MMIfaceModem *self,
                                                GAsyncResult *res,
                                                GError **error);

    /* Loading of the SupportedBands property */
    void (*load_supported_bands) (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    MMModemBand (*load_supported_bands_finish) (MMIfaceModem *self,
                                                GAsyncResult *res,
                                                GError **error);

    /* Asynchronous reset operation */
    void (*reset) (MMIfaceModem *self,
                   GAsyncReadyCallback callback,
                   gpointer user_data);
    gboolean (*reset_finish) (MMIfaceModem *self,
                              GAsyncResult *res,
                              GError **error);

    /* Asynchronous factory-reset operation */
    void (*factory_reset) (MMIfaceModem *self,
                           const gchar *code,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
    gboolean (*factory_reset_finish) (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GError **error);

    /* Asynchronous allowed band setting operation */
    void (*set_allowed_bands) (MMIfaceModem *self,
                               MMModemBand bands,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    gboolean (*set_allowed_bands_finish) (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GError **error);

    /* Asynchronous allowed mode setting operation */
    void (*set_allowed_modes) (MMIfaceModem *self,
                               MMModemMode modes,
                               MMModemMode preferred,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    gboolean (*set_allowed_modes_finish) (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GError **error);
};

GType mm_iface_modem_get_type (void);

/* Initialize Modem interface (async) */
void     mm_iface_modem_initialize        (MMIfaceModem *self,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_iface_modem_initialize_finish (MMIfaceModem *self,
                                           GAsyncResult *res,
                                           GError **error);

/* Shutdown Modem interface */
gboolean mm_iface_modem_shutdown (MMIfaceModem *self,
                                  GError **error);

/* Request unlock recheck.
 * It will not only return the lock status, but also set the property values
 * in the DBus interface. */
void        mm_iface_modem_unlock_check        (MMIfaceModem *self,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
MMModemLock mm_iface_modem_unlock_check_finish (MMIfaceModem *self,
                                                GAsyncResult *res,
                                                GError **error);

#endif /* MM_IFACE_MODEM_H */
