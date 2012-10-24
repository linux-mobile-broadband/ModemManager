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

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-charsets.h"
#include "mm-at-serial-port.h"
#include "mm-bearer.h"
#include "mm-sim.h"

#define MM_TYPE_IFACE_MODEM            (mm_iface_modem_get_type ())
#define MM_IFACE_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM, MMIfaceModem))
#define MM_IS_IFACE_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM))
#define MM_IFACE_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM, MMIfaceModem))

#define MM_IFACE_MODEM_DBUS_SKELETON "iface-modem-dbus-skeleton"
#define MM_IFACE_MODEM_STATE         "iface-modem-state"
#define MM_IFACE_MODEM_SIM           "iface-modem-sim"
#define MM_IFACE_MODEM_BEARER_LIST   "iface-modem-bearer-list"

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

    /* Asynchronous modem power-down operation run during initialization */
    void (*modem_init_power_down) (MMIfaceModem *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
    gboolean (*modem_init_power_down_finish) (MMIfaceModem *self,
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

    /* Loading of the OwnNumbers property */
    void (*load_own_numbers) (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
    GStrv (*load_own_numbers_finish) (MMIfaceModem *self,
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
    MMUnlockRetries * (*load_unlock_retries_finish) (MMIfaceModem *self,
                                                     GAsyncResult *res,
                                                     GError **error);

    /* Loading of the SupportedModes property */
    void (*load_supported_modes) (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    MMModemMode (*load_supported_modes_finish) (MMIfaceModem *self,
                                                GAsyncResult *res,
                                                GError **error);

    /* Loading of the AllowedModes and PreferredMode properties */
    void (*load_allowed_modes) (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
    gboolean (*load_allowed_modes_finish) (MMIfaceModem *self,
                                           GAsyncResult *res,
                                           MMModemMode *allowed,
                                           MMModemMode *preferred,
                                           GError **error);

    /* Loading of the SupportedBands property */
    void (*load_supported_bands) (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    GArray * (*load_supported_bands_finish) (MMIfaceModem *self,
                                             GAsyncResult *res,
                                             GError **error);

    /* Loading of the Bands property */
    void (*load_current_bands) (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
    GArray * (*load_current_bands_finish) (MMIfaceModem *self,
                                           GAsyncResult *res,
                                           GError **error);

    /* Loading of the SignalQuality property */
    void  (*load_signal_quality) (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    guint (*load_signal_quality_finish) (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error);

    /* Loading of the AccessTechnologies property */
    void  (*load_access_technologies) (MMIfaceModem *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    gboolean (*load_access_technologies_finish) (MMIfaceModem *self,
                                                 GAsyncResult *res,
                                                 MMModemAccessTechnology *access_technologies,
                                                 guint *mask,
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

    /* Asynchronous command operation */
    void (*command) (MMIfaceModem *self,
                     const gchar *cmd,
                     guint timeout,
                     GAsyncReadyCallback callback,
                     gpointer user_data);
    const gchar * (*command_finish) (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error);

    /* Asynchronous allowed band setting operation */
    void (*set_bands) (MMIfaceModem *self,
                       GArray *bands_array,
                       GAsyncReadyCallback callback,
                       gpointer user_data);
    gboolean (*set_bands_finish) (MMIfaceModem *self,
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

    /* Asynchronous modem initialization operation */
    void (*modem_init) (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data);
    gboolean (*modem_init_finish) (MMIfaceModem *self,
                                   GAsyncResult *res,
                                   GError **error);

    /* Asynchronous method to wait for the SIM to be ready after having
     * unlocked it. */
    void (*modem_after_sim_unlock) (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    gboolean (*modem_after_sim_unlock_finish) (MMIfaceModem *self,
                                               GAsyncResult *res,
                                               GError **error);

    /* Asynchronous modem power-up operation */
    void (*modem_power_up) (MMIfaceModem *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
    gboolean (*modem_power_up_finish) (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GError **error);

    /* Asynchronous additional setup needed after power-up,
     * Plugins can implement this to provide custom setups. */
    void (*modem_after_power_up) (MMIfaceModem *self,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);
    gboolean (*modem_after_power_up_finish) (MMIfaceModem *self,
                                             GAsyncResult *res,
                                             GError **error);

    /* Asynchronous flow control setup */
    void (*setup_flow_control) (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
    gboolean (*setup_flow_control_finish) (MMIfaceModem *self,
                                           GAsyncResult *res,
                                           GError **error);

    /* Asynchronous loading of supported charsets */
    void (*load_supported_charsets) (MMIfaceModem *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data);
    MMModemCharset (*load_supported_charsets_finish) (MMIfaceModem *self,
                                                      GAsyncResult *res,
                                                      GError **error);

    /* Asynchronous charset setting setup */
    void (*setup_charset) (MMIfaceModem *self,
                           MMModemCharset charset,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
    gboolean (*setup_charset_finish) (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GError **error);

    /* Asynchronous modem power-down operation */
    void (*modem_power_down) (MMIfaceModem *self,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
    gboolean (*modem_power_down_finish) (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error);

    /* Create SIM */
    void (*create_sim) (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data);
    MMSim * (*create_sim_finish) (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error);

    /* Create bearer */
    void (*create_bearer) (MMIfaceModem *self,
                           MMBearerProperties *properties,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
    MMBearer * (*create_bearer_finish) (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error);
};

GType mm_iface_modem_get_type (void);

/* Helpers to query capabilities */
MMModemCapability mm_iface_modem_get_current_capabilities (MMIfaceModem *self);
gboolean          mm_iface_modem_is_3gpp                  (MMIfaceModem *self);
gboolean          mm_iface_modem_is_3gpp_only             (MMIfaceModem *self);
gboolean          mm_iface_modem_is_3gpp_lte              (MMIfaceModem *self);
gboolean          mm_iface_modem_is_3gpp_lte_only         (MMIfaceModem *self);
gboolean          mm_iface_modem_is_cdma                  (MMIfaceModem *self);
gboolean          mm_iface_modem_is_cdma_only             (MMIfaceModem *self);

/* Helpers to query supported modes */
MMModemMode mm_iface_modem_get_supported_modes (MMIfaceModem *self);
gboolean    mm_iface_modem_is_2g               (MMIfaceModem *self);
gboolean    mm_iface_modem_is_2g_only          (MMIfaceModem *self);
gboolean    mm_iface_modem_is_3g               (MMIfaceModem *self);
gboolean    mm_iface_modem_is_3g_only          (MMIfaceModem *self);
gboolean    mm_iface_modem_is_4g               (MMIfaceModem *self);
gboolean    mm_iface_modem_is_4g_only          (MMIfaceModem *self);

/* Initialize Modem interface (async) */
void     mm_iface_modem_initialize        (MMIfaceModem *self,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);
gboolean mm_iface_modem_initialize_finish (MMIfaceModem *self,
                                           GAsyncResult *res,
                                           GError **error);

/* Enable Modem interface (async) */
void     mm_iface_modem_enable        (MMIfaceModem *self,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
gboolean mm_iface_modem_enable_finish (MMIfaceModem *self,
                                       GAsyncResult *res,
                                       GError **error);

/* Disable Modem interface (async) */
void     mm_iface_modem_disable        (MMIfaceModem *self,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);
gboolean mm_iface_modem_disable_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error);

/* Shutdown Modem interface */
void mm_iface_modem_shutdown (MMIfaceModem *self);

/* Request unlock recheck.
 * It will not only return the lock status, but also set the property values
 * in the DBus interface. */
void        mm_iface_modem_unlock_check        (MMIfaceModem *self,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
MMModemLock mm_iface_modem_unlock_check_finish (MMIfaceModem *self,
                                                GAsyncResult *res,
                                                GError **error);

/* Check unlock retries */
void mm_iface_modem_update_unlock_retries            (MMIfaceModem *self,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data);
gboolean mm_iface_modem_update_unlock_retries_finish (MMIfaceModem *self,
                                                      GAsyncResult *res,
                                                      GError **error);

/* Request signal quality check update.
 * It will not only return the signal quality status, but also set the property
 * values in the DBus interface. */
void  mm_iface_modem_signal_quality_check        (MMIfaceModem *self,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
guint mm_iface_modem_signal_quality_check_finish (MMIfaceModem *self,
                                                  GAsyncResult *res,
                                                  gboolean *recent,
                                                  GError **error);

/* Allow reporting new modem state */
void mm_iface_modem_update_subsystem_state (MMIfaceModem *self,
                                            const gchar *subsystem,
                                            MMModemState new_state,
                                            MMModemStateChangeReason reason);
void mm_iface_modem_update_state (MMIfaceModem *self,
                                  MMModemState new_state,
                                  MMModemStateChangeReason reason);

/* Allow reporting new access tech */
void mm_iface_modem_update_access_technologies (MMIfaceModem *self,
                                                MMModemAccessTechnology access_tech,
                                                guint32 mask);

/* Allow updating signal quality */
void mm_iface_modem_update_signal_quality (MMIfaceModem *self,
                                           guint signal_quality);

/* Allow setting allowed modes */
void     mm_iface_modem_set_allowed_modes        (MMIfaceModem *self,
                                                  MMModemMode allowed,
                                                  MMModemMode preferred,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
gboolean mm_iface_modem_set_allowed_modes_finish (MMIfaceModem *self,
                                                  GAsyncResult *res,
                                                  GError **error);

/* Allow setting bands */
void     mm_iface_modem_set_bands        (MMIfaceModem *self,
                                          GArray *bands_array,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data);
gboolean mm_iface_modem_set_bands_finish (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GError **error);

/* Allow creating bearers */
void     mm_iface_modem_create_bearer         (MMIfaceModem *self,
                                               MMBearerProperties *properties,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
MMBearer *mm_iface_modem_create_bearer_finish (MMIfaceModem *self,
                                               GAsyncResult *res,
                                               GError **error);

/* Helper method to wait for a final state */
void         mm_iface_modem_wait_for_final_state        (MMIfaceModem *self,
                                                         MMModemState final_state,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);
MMModemState mm_iface_modem_wait_for_final_state_finish (MMIfaceModem *self,
                                                         GAsyncResult *res,
                                                         GError **error);

void mm_iface_modem_bind_simple_status (MMIfaceModem *self,
                                        MMSimpleStatus *status);

#endif /* MM_IFACE_MODEM_H */
