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

#ifndef MM_MODEM_HELPERS_QMI_H
#define MM_MODEM_HELPERS_QMI_H

#include <config.h>

#include <ModemManager.h>
#include <libqmi-glib.h>

/*****************************************************************************/
/* QMI/DMS to MM translations */

MMModemCapability mm_modem_capability_from_qmi_radio_interface (QmiDmsRadioInterface network,
                                                                gpointer             log_object);

MMModemMode mm_modem_mode_from_qmi_radio_interface (QmiDmsRadioInterface network,
                                                    gpointer             log_object);

MMModemLock mm_modem_lock_from_qmi_uim_pin_status (QmiDmsUimPinStatus status,
                                                       gboolean pin1);

gboolean mm_pin_enabled_from_qmi_uim_pin_status (QmiDmsUimPinStatus status);
QmiDmsUimFacility mm_3gpp_facility_to_qmi_uim_facility (MMModem3gppFacility mm);

GArray *mm_modem_bands_from_qmi_band_capabilities (QmiDmsBandCapability     qmi_bands,
                                                   QmiDmsLteBandCapability  qmi_lte_bands,
                                                   GArray                  *extended_qmi_lte_bands,
                                                   gpointer                 log_object);

/*****************************************************************************/
/* QMI/NAS to MM translations */

MMModemAccessTechnology mm_modem_access_technology_from_qmi_radio_interface (QmiNasRadioInterface interface);
MMModemAccessTechnology mm_modem_access_technologies_from_qmi_radio_interface_array (GArray *radio_interfaces);

MMModemAccessTechnology mm_modem_access_technology_from_qmi_data_capability (QmiNasDataCapability cap);
MMModemAccessTechnology mm_modem_access_technologies_from_qmi_data_capability_array (GArray *data_capabilities);

MMModemMode mm_modem_mode_from_qmi_nas_radio_interface (QmiNasRadioInterface iface);

MMModemMode mm_modem_mode_from_qmi_radio_technology_preference (QmiNasRadioTechnologyPreference qmi);
QmiNasRadioTechnologyPreference mm_modem_mode_to_qmi_radio_technology_preference (MMModemMode mode,
                                                                                  gboolean is_cdma);

MMModemMode mm_modem_mode_from_qmi_rat_mode_preference (QmiNasRatModePreference qmi);
QmiNasRatModePreference mm_modem_mode_to_qmi_rat_mode_preference (MMModemMode mode,
                                                                  gboolean is_cdma,
                                                                  gboolean is_3gpp);

MMModemCapability mm_modem_capability_from_qmi_rat_mode_preference (QmiNasRatModePreference qmi);
QmiNasRatModePreference mm_modem_capability_to_qmi_rat_mode_preference (MMModemCapability caps);

GArray *mm_modem_capability_to_qmi_acquisition_order_preference (MMModemCapability  caps);
GArray *mm_modem_mode_to_qmi_acquisition_order_preference       (MMModemMode        allowed,
                                                                 MMModemMode        preferred,
                                                                 GArray            *all);

MMModemCapability mm_modem_capability_from_qmi_radio_technology_preference (QmiNasRadioTechnologyPreference qmi);
QmiNasRadioTechnologyPreference mm_modem_capability_to_qmi_radio_technology_preference (MMModemCapability caps);

MMModemCapability mm_modem_capability_from_qmi_band_preference (QmiNasBandPreference qmi);

MMModemMode mm_modem_mode_from_qmi_gsm_wcdma_acquisition_order_preference (QmiNasGsmWcdmaAcquisitionOrderPreference qmi,
                                                                           gpointer                                 log_object);
QmiNasGsmWcdmaAcquisitionOrderPreference mm_modem_mode_to_qmi_gsm_wcdma_acquisition_order_preference (MMModemMode mode,
                                                                                                      gpointer    log_object);

GArray *mm_modem_bands_from_qmi_rf_band_information_array (GArray *info_array);

GArray *mm_modem_bands_from_qmi_band_preference (QmiNasBandPreference     qmi_bands,
                                                 QmiNasLteBandPreference  qmi_lte_bands,
                                                 const guint64           *extended_qmi_lte_bands,
                                                 guint                    extended_qmi_lte_bands_size,
                                                 gpointer                 log_object);
void mm_modem_bands_to_qmi_band_preference (GArray                  *mm_bands,
                                            QmiNasBandPreference    *qmi_bands,
                                            QmiNasLteBandPreference *qmi_lte_bands,
                                            guint64                 *extended_qmi_lte_bands,
                                            guint                    extended_qmi_lte_bands_size,
                                            gpointer                 log_object);

MMModem3gppRegistrationState mm_modem_3gpp_registration_state_from_qmi_registration_state (QmiNasAttachState attach_state,
                                                                                           QmiNasRegistrationState registration_state,
                                                                                           gboolean roaming);

MMModemCdmaRegistrationState mm_modem_cdma_registration_state_from_qmi_registration_state (QmiNasRegistrationState registration_state);

MMModemCdmaActivationState mm_modem_cdma_activation_state_from_qmi_activation_state (QmiDmsActivationState state);

/*****************************************************************************/
/* QMI/WMS to MM translations */

QmiWmsStorageType mm_sms_storage_to_qmi_storage_type (MMSmsStorage storage);
MMSmsStorage mm_sms_storage_from_qmi_storage_type (QmiWmsStorageType qmi_storage);

MMSmsState mm_sms_state_from_qmi_message_tag (QmiWmsMessageTagType tag);

/*****************************************************************************/
/* QMI/WDS to MM translations */

QmiWdsAuthentication mm_bearer_allowed_auth_to_qmi_authentication   (MMBearerAllowedAuth auth);
MMBearerAllowedAuth  mm_bearer_allowed_auth_from_qmi_authentication (QmiWdsAuthentication auth);
MMBearerIpFamily     mm_bearer_ip_family_from_qmi_ip_support_type   (QmiWdsIpSupportType ip_support_type);
MMBearerIpFamily     mm_bearer_ip_family_from_qmi_pdp_type          (QmiWdsPdpType pdp_type);
gboolean             mm_bearer_ip_family_to_qmi_pdp_type            (MMBearerIpFamily  ip_family,
                                                                     QmiWdsPdpType    *out_pdp_type);

/*****************************************************************************/
/* QMI/OMA to MM translations */

MMOmaSessionType mm_oma_session_type_from_qmi_oma_session_type (QmiOmaSessionType qmi_session_type);
QmiOmaSessionType mm_oma_session_type_to_qmi_oma_session_type (MMOmaSessionType mm_session_type);

MMOmaSessionState mm_oma_session_state_from_qmi_oma_session_state (QmiOmaSessionState qmi_session_state);

MMOmaSessionStateFailedReason mm_oma_session_state_failed_reason_from_qmi_oma_session_failed_reason (QmiOmaSessionFailedReason qmi_session_failed_reason);

/*****************************************************************************/
/* QMI/LOC to MM translations */

gboolean mm_error_from_qmi_loc_indication_status (QmiLocIndicationStatus   status,
                                                  GError                 **error);

/*****************************************************************************/
/* Utility to gather current capabilities from various sources */

typedef struct {
    /* NAS System Selection Preference */
    QmiNasRatModePreference nas_ssp_mode_preference_mask;
    /* NAS Technology Preference */
    QmiNasRadioTechnologyPreference nas_tp_mask;
    /* DMS Capabilities */
    MMModemCapability dms_capabilities;
} MMQmiCapabilitiesContext;

MMModemCapability mm_modem_capability_from_qmi_capabilities_context (MMQmiCapabilitiesContext *ctx,
                                                                     gpointer                  log_object);

/*****************************************************************************/
/* QMI unique id manipulation */

gchar  *mm_qmi_unique_id_to_firmware_unique_id (GArray       *qmi_unique_id,
                                                GError      **error);
GArray *mm_firmware_unique_id_to_qmi_unique_id (const gchar  *unique_id,
                                                GError      **error);

/*****************************************************************************/
/* Common UIM Get Card Status parsing */

gboolean mm_qmi_uim_get_card_status_output_parse (gpointer                           log_object,
                                                  QmiMessageUimGetCardStatusOutput  *output,
                                                  MMModemLock                       *o_lock,
                                                  QmiUimPinState                    *o_pin1_state,
                                                  guint                             *o_pin1_retries,
                                                  guint                             *o_puk1_retries,
                                                  QmiUimPinState                    *o_pin2_state,
                                                  guint                             *o_pin2_retries,
                                                  guint                             *o_puk2_retries,
                                                  GError                           **error);

/*****************************************************************************/
/* UIM Get Slot Status parsing */
gchar *mm_qmi_uim_decode_eid (const gchar *eid, gsize eid_len);

#endif  /* MM_MODEM_HELPERS_QMI_H */
