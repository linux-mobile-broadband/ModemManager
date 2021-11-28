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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#ifndef MM_MODEM_HELPERS_MBIM_H
#define MM_MODEM_HELPERS_MBIM_H

#include <config.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <libmbim-glib.h>

/*****************************************************************************/
/* MBIM/BasicConnect to MM translations */

MMModemCapability mm_modem_capability_from_mbim_device_caps (MbimCellularClass  caps_cellular_class,
                                                             MbimDataClass      caps_data_class,
                                                             const gchar       *caps_custom_data_class);

MMModemLock mm_modem_lock_from_mbim_pin_type (MbimPinType pin_type);

MMModem3gppRegistrationState mm_modem_3gpp_registration_state_from_mbim_register_state (MbimRegisterState state);

MbimDataClass mm_mbim_data_class_from_mbim_data_class_v3_and_subclass (MbimDataClassV3  data_class_v3,
                                                                       MbimDataSubclass data_subclass);

MMModemMode mm_modem_mode_from_mbim_data_class (MbimDataClass data_class);

MbimDataClass mm_mbim_data_class_from_modem_mode (MMModemMode modem_mode,
                                                  gboolean    is_3gpp,
                                                  gboolean    is_cdma);

MMModemAccessTechnology mm_modem_access_technology_from_mbim_data_class (MbimDataClass data_class);

MMModem3gppNetworkAvailability mm_modem_3gpp_network_availability_from_mbim_provider_state (MbimProviderState state);

GList *mm_3gpp_network_info_list_from_mbim_providers (const MbimProvider *const *providers, guint n_providers);

MbimPinType mbim_pin_type_from_mm_modem_3gpp_facility (MMModem3gppFacility facility);

GError *mm_mobile_equipment_error_from_mbim_nw_error (MbimNwError nw_error,
                                                      gpointer    log_object);

MMBearerAllowedAuth mm_bearer_allowed_auth_from_mbim_auth_protocol (MbimAuthProtocol      auth_protocol);
MbimAuthProtocol    mm_bearer_allowed_auth_to_mbim_auth_protocol   (MMBearerAllowedAuth   bearer_auth,
                                                                    gpointer              log_object,
                                                                    GError              **error);
MMBearerIpFamily    mm_bearer_ip_family_from_mbim_context_ip_type  (MbimContextIpType     ip_type);
MbimContextIpType   mm_bearer_ip_family_to_mbim_context_ip_type    (MMBearerIpFamily      ip_family,
                                                                    GError              **error);
MMBearerApnType     mm_bearer_apn_type_from_mbim_context_type      (MbimContextType       context_type);
MbimContextType     mm_bearer_apn_type_to_mbim_context_type        (MMBearerApnType       apn_type,
                                                                    gpointer              log_object,
                                                                    GError              **error);

gboolean mm_signal_error_rate_percentage_from_coded_value (guint      coded_value,
                                                           gdouble   *out_percentage,
                                                           gboolean   is_gsm,
                                                           GError   **error);

gboolean mm_signal_rssi_from_coded_value (guint      coded_value,
                                          gdouble   *out_rssi,
                                          GError   **error);

gboolean mm_signal_rsrp_from_coded_value (guint      coded_value,
                                          gdouble   *out_rsrp,
                                          GError   **error);

gboolean mm_signal_snr_from_coded_value (guint      coded_value,
                                         gdouble   *out_snr,
                                         GError   **error);

MMModem3gppMicoMode mm_modem_3gpp_mico_mode_from_mbim_mico_mode (MbimMicoMode        mico_mode);
MbimMicoMode        mm_modem_3gpp_mico_mode_to_mbim_mico_mode   (MMModem3gppMicoMode mico_mode);
MMModem3gppDrxCycle mm_modem_3gpp_drx_cycle_from_mbim_drx_cycle (MbimDrxCycle        drx_cycle);
MbimDrxCycle        mm_modem_3gpp_drx_cycle_to_mbim_drx_cycle   (MMModem3gppDrxCycle drx_cycle);

/*****************************************************************************/
/* MBIM/SMS to MM translations */

MMSmsState mm_sms_state_from_mbim_message_status (MbimSmsStatus status);

/*****************************************************************************/

guint mm_signal_quality_from_mbim_signal_state (guint                 rssi,
                                                MbimRsrpSnrInfoArray *rsrp_snr,
                                                guint32               rsrp_snr_count,
                                                gpointer              log_object);

gboolean mm_signal_from_mbim_signal_state (MbimDataClass          data_class,
                                           guint                  coded_rssi,
                                           guint                  coded_error_rate,
                                           MbimRsrpSnrInfoArray  *rsrp_snr,
                                           guint32                rsrp_snr_count,
                                           gpointer               log_object,
                                           MMSignal             **out_cdma,
                                           MMSignal             **out_evdo,
                                           MMSignal             **out_gsm,
                                           MMSignal             **out_umts,
                                           MMSignal             **out_lte,
                                           MMSignal             **out_nr5g);

#endif  /* MM_MODEM_HELPERS_MBIM_H */
