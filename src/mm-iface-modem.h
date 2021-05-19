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
#include "mm-port-serial-at.h"
#include "mm-base-bearer.h"
#include "mm-base-sim.h"

#define MM_TYPE_IFACE_MODEM            (mm_iface_modem_get_type ())
#define MM_IFACE_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_IFACE_MODEM, MMIfaceModem))
#define MM_IS_IFACE_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_IFACE_MODEM))
#define MM_IFACE_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_IFACE_MODEM, MMIfaceModem))

#define MM_IFACE_MODEM_DBUS_SKELETON           "iface-modem-dbus-skeleton"
#define MM_IFACE_MODEM_STATE                   "iface-modem-state"
#define MM_IFACE_MODEM_SIM                     "iface-modem-sim"
#define MM_IFACE_MODEM_SIM_SLOTS               "iface-modem-sim-slots"
#define MM_IFACE_MODEM_BEARER_LIST             "iface-modem-bearer-list"
#define MM_IFACE_MODEM_SIM_HOT_SWAP_SUPPORTED  "iface-modem-sim-hot-swap-supported"
#define MM_IFACE_MODEM_SIM_HOT_SWAP_CONFIGURED "iface-modem-sim-hot-swap-configured"
#define MM_IFACE_MODEM_CARRIER_CONFIG_MAPPING  "iface-modem-carrier-config-mapping"
#define MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED      "iface-modem-periodic-signal-check-disabled"
#define MM_IFACE_MODEM_PERIODIC_ACCESS_TECH_CHECK_DISABLED "iface-modem-periodic-access-tech-check-disabled"

typedef struct _MMIfaceModem MMIfaceModem;

struct _MMIfaceModem {
    GTypeInterface g_iface;

    /* Loading of the SupportedCapabilities property */
    void (*load_supported_capabilities) (MMIfaceModem *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    GArray * (*load_supported_capabilities_finish) (MMIfaceModem *self,
                                                    GAsyncResult *res,
                                                    GError **error);

    /* Loading of the CurrentCapabilities property */
    void (*load_current_capabilities) (MMIfaceModem *self,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
    MMModemCapability (*load_current_capabilities_finish) (MMIfaceModem *self,
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

    /* Loading of the HardwareRevision property */
    void (*load_hardware_revision) (MMIfaceModem *self,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);
    gchar * (*load_hardware_revision_finish) (MMIfaceModem *self,
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
                                  gboolean last_attempt,
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
    GArray * (*load_supported_modes_finish) (MMIfaceModem *self,
                                             GAsyncResult *res,
                                             GError **error);

    /* Loading of the Modes property */
    void (*load_current_modes) (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
    gboolean (*load_current_modes_finish) (MMIfaceModem *self,
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

    /* Loading of the SupportedIpFamilies property */
    void (* load_supported_ip_families) (MMIfaceModem *self,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
    MMBearerIpFamily (* load_supported_ip_families_finish) (MMIfaceModem *self,
                                                           GAsyncResult *res,
                                                           GError **error);

    /* Loading of the PowerState property */
    void (* load_power_state) (MMIfaceModem *self,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    MMModemPowerState (*load_power_state_finish) (MMIfaceModem *self,
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

    /* Asynchronous capabilities setting operation */
    void (*set_current_capabilities) (MMIfaceModem *self,
                                      MMModemCapability,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
    gboolean (*set_current_capabilities_finish) (MMIfaceModem *self,
                                                 GAsyncResult *res,
                                                 GError **error);

    /* Asynchronous current band setting operation */
    void (*set_current_bands) (MMIfaceModem *self,
                               GArray *bands_array,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    gboolean (*set_current_bands_finish) (MMIfaceModem *self,
                                          GAsyncResult *res,
                                          GError **error);

    /* Asynchronous current mode setting operation */
    void (*set_current_modes) (MMIfaceModem *self,
                               MMModemMode modes,
                               MMModemMode preferred,
                               GAsyncReadyCallback callback,
                               gpointer user_data);
    gboolean (*set_current_modes_finish) (MMIfaceModem *self,
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

    /* Asynchronous FCC unlock operation */
    void     (* fcc_unlock)        (MMIfaceModem         *self,
                                    GAsyncReadyCallback   callback,
                                    gpointer              user_data);
    gboolean (* fcc_unlock_finish) (MMIfaceModem         *self,
                                    GAsyncResult         *res,
                                    GError              **error);

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

    /* Asynchronous check to see if the SIM was swapped.
     * Useful for when the modem changes power states since we might
     * not get the relevant notifications from the modem. */
    void (*check_for_sim_swap) (MMIfaceModem *self,
                                const gchar *iccid,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
    gboolean (*check_for_sim_swap_finish) (MMIfaceModem *self,
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

    /* Asynchronous modem power-off operation */
    void (*modem_power_off) (MMIfaceModem *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data);
    gboolean (*modem_power_off_finish) (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error);
    /* Create SIM */
    void (*create_sim) (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data);
    MMBaseSim * (*create_sim_finish) (MMIfaceModem *self,
                                      GAsyncResult *res,
                                      GError **error);

    /* Create SIMs in all SIM slots */
    void     (* load_sim_slots)        (MMIfaceModem         *self,
                                        GAsyncReadyCallback   callback,
                                        gpointer              user_data);
    gboolean (* load_sim_slots_finish) (MMIfaceModem         *self,
                                        GAsyncResult         *res,
                                        GPtrArray           **sim_slots,
                                        guint                *primary_sim_slot,
                                        GError              **error);

    /* Set primary SIM slot */
    void     (* set_primary_sim_slot)        (MMIfaceModem         *self,
                                              guint                 sim_slot,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data);
    gboolean (* set_primary_sim_slot_finish) (MMIfaceModem         *self,
                                              GAsyncResult         *res,
                                              GError              **error);

    /* Create bearer */
    void (*create_bearer) (MMIfaceModem *self,
                           MMBearerProperties *properties,
                           GAsyncReadyCallback callback,
                           gpointer user_data);
    MMBaseBearer * (*create_bearer_finish) (MMIfaceModem *self,
                                            GAsyncResult *res,
                                            GError **error);
    /* Setup SIM hot swap */
    void (*setup_sim_hot_swap) (MMIfaceModem *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data);

    gboolean (*setup_sim_hot_swap_finish) (MMIfaceModem *self,
                                            GAsyncResult *res,
                                            GError **error);

    /* Load carrier config */
    void     (* load_carrier_config)        (MMIfaceModem         *self,
                                             GAsyncReadyCallback   callback,
                                             gpointer              user_data);
    gboolean (* load_carrier_config_finish) (MMIfaceModem         *self,
                                             GAsyncResult         *res,
                                             gchar               **carrier_config_name,
                                             gchar               **carrier_config_revision,
                                             GError              **error);

    /* Setup carrier config based on IMSI */
    void     (* setup_carrier_config)        (MMIfaceModem         *self,
                                              const gchar          *imsi,
                                              const gchar          *carrier_config_mapping,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data);
    gboolean (* setup_carrier_config_finish) (MMIfaceModem         *self,
                                              GAsyncResult         *res,
                                              GError              **error);
};

GType mm_iface_modem_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MMIfaceModem, g_object_unref)

/* Helpers to query access technologies */
MMModemAccessTechnology mm_iface_modem_get_access_technologies (MMIfaceModem *self);

/* Helpers to query capabilities */
MMModemCapability mm_iface_modem_get_current_capabilities (MMIfaceModem *self);
gboolean          mm_iface_modem_is_3gpp                  (MMIfaceModem *self);
gboolean          mm_iface_modem_is_3gpp_only             (MMIfaceModem *self);
gboolean          mm_iface_modem_is_3gpp_lte              (MMIfaceModem *self);
gboolean          mm_iface_modem_is_3gpp_5gnr             (MMIfaceModem *self);
gboolean          mm_iface_modem_is_cdma                  (MMIfaceModem *self);
gboolean          mm_iface_modem_is_cdma_only             (MMIfaceModem *self);

/* Helpers to query supported modes */
gboolean mm_iface_modem_is_2g      (MMIfaceModem *self);
gboolean mm_iface_modem_is_2g_only (MMIfaceModem *self);
gboolean mm_iface_modem_is_3g      (MMIfaceModem *self);
gboolean mm_iface_modem_is_3g_only (MMIfaceModem *self);
gboolean mm_iface_modem_is_4g      (MMIfaceModem *self);
gboolean mm_iface_modem_is_4g_only (MMIfaceModem *self);
gboolean mm_iface_modem_is_5g      (MMIfaceModem *self);
gboolean mm_iface_modem_is_5g_only (MMIfaceModem *self);

/* Helpers to query properties */
const gchar *mm_iface_modem_get_model          (MMIfaceModem  *self);
const gchar *mm_iface_modem_get_revision       (MMIfaceModem  *self);
gboolean     mm_iface_modem_get_carrier_config (MMIfaceModem  *self,
                                                const gchar  **name,
                                                const gchar  **revision);

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

/* Allow setting power state */
void     mm_iface_modem_set_power_state        (MMIfaceModem *self,
                                                MMModemPowerState power_state,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data);
gboolean mm_iface_modem_set_power_state_finish (MMIfaceModem *self,
                                                GAsyncResult *res,
                                                GError **error);

/* Shutdown Modem interface */
void mm_iface_modem_shutdown (MMIfaceModem *self);

/* Request lock info update.
 * It will not only return the lock status, but also set the property values
 * in the DBus interface. If 'known_lock' is given, that lock status will be
 * assumed. */
void        mm_iface_modem_update_lock_info        (MMIfaceModem *self,
                                                    MMModemLock known_lock,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data);
MMModemLock mm_iface_modem_update_lock_info_finish (MMIfaceModem *self,
                                                    GAsyncResult *res,
                                                    GError **error);

MMModemLock      mm_iface_modem_get_unlock_required (MMIfaceModem *self);
MMUnlockRetries *mm_iface_modem_get_unlock_retries  (MMIfaceModem *self);

void mm_iface_modem_update_unlock_retries (MMIfaceModem *self,
                                           MMUnlockRetries *unlock_retries);

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
void mm_iface_modem_update_failed_state (MMIfaceModem *self,
                                         MMModemStateFailedReason failed_reason);

/* Allow update own numbers */
void mm_iface_modem_update_own_numbers (MMIfaceModem *self,
                                        const GStrv own_numbers);

/* Allow reporting new access tech */
void mm_iface_modem_update_access_technologies (MMIfaceModem *self,
                                                MMModemAccessTechnology access_tech,
                                                guint32 mask);

/* Allow updating signal quality */
void mm_iface_modem_update_signal_quality (MMIfaceModem *self,
                                           guint signal_quality);

/* Allow requesting to refresh signal via polling */
void mm_iface_modem_refresh_signal (MMIfaceModem *self);

/* Allow setting allowed modes */
void     mm_iface_modem_set_current_modes        (MMIfaceModem *self,
                                                  MMModemMode allowed,
                                                  MMModemMode preferred,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
gboolean mm_iface_modem_set_current_modes_finish (MMIfaceModem *self,
                                                  GAsyncResult *res,
                                                  GError **error);

/* Allow setting bands */
void     mm_iface_modem_set_current_bands        (MMIfaceModem *self,
                                                  GArray *bands_array,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
gboolean mm_iface_modem_set_current_bands_finish (MMIfaceModem *self,
                                                  GAsyncResult *res,
                                                  GError **error);

/* Allow creating bearers */
void          mm_iface_modem_create_bearer         (MMIfaceModem *self,
                                                    MMBearerProperties *properties,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data);
MMBaseBearer *mm_iface_modem_create_bearer_finish  (MMIfaceModem *self,
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

/* Check if the SIM or eSIM profile has changed */
void     mm_iface_modem_check_for_sim_swap        (MMIfaceModem *self,
                                                   guint slot_index,
                                                   const gchar *iccid,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);
gboolean mm_iface_modem_check_for_sim_swap_finish (MMIfaceModem *self,
                                                   GAsyncResult *res,
                                                   GError **error);

#endif /* MM_IFACE_MODEM_H */
