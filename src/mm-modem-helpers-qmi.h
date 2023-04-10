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
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc.
 */

#ifndef MM_MODEM_HELPERS_QMI_H
#define MM_MODEM_HELPERS_QMI_H

#include <config.h>

#include <ModemManager.h>
#include <libqmi-glib.h>

#include "mm-port.h"

#define MM_MODEM_CAPABILITY_MULTIMODE (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_CDMA_EVDO)

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
                                                   GArray                  *qmi_nr5g_bands,
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
                                                 const guint64           *qmi_nr5g_bands,
                                                 guint                    qmi_nr5g_bands_size,
                                                 gpointer                 log_object);
void mm_modem_bands_to_qmi_band_preference (GArray                  *mm_bands,
                                            QmiNasBandPreference    *qmi_bands,
                                            QmiNasLteBandPreference *qmi_lte_bands,
                                            guint64                 *extended_qmi_lte_bands,
                                            guint                    extended_qmi_lte_bands_size,
                                            guint64                 *qmi_nr5g_bands,
                                            guint                    qmi_nr5g_bands_size,
                                            gpointer                 log_object);

MMModem3gppRegistrationState mm_modem_3gpp_registration_state_from_qmi_registration_state (QmiNasAttachState attach_state,
                                                                                           QmiNasRegistrationState registration_state,
                                                                                           gboolean roaming);

MMModemCdmaRegistrationState mm_modem_cdma_registration_state_from_qmi_registration_state (QmiNasRegistrationState registration_state);

MMModemCdmaActivationState mm_modem_cdma_activation_state_from_qmi_activation_state (QmiDmsActivationState state);

/*****************************************************************************/
/* QMI NAS System Info processor */

void mm_modem_registration_state_from_qmi_system_info (QmiMessageNasGetSystemInfoOutput *response_output,
                                                       QmiIndicationNasSystemInfoOutput *indication_output,
                                                       MMModem3gppRegistrationState     *out_cs_registration_state,
                                                       MMModem3gppRegistrationState     *out_ps_registration_state,
                                                       MMModem3gppRegistrationState     *out_eps_registration_state,
                                                       MMModem3gppRegistrationState     *out_5gs_registration_state,
                                                       guint16                          *out_lac,
                                                       guint16                          *out_tac,
                                                       guint32                          *out_cid,
                                                       gchar                           **out_operator_id,
                                                       MMModemAccessTechnology          *out_act,
                                                       gpointer                          log_object);

/*****************************************************************************/
/* QMI/WMS to MM translations */

QmiWmsStorageType mm_sms_storage_to_qmi_storage_type (MMSmsStorage storage);
MMSmsStorage mm_sms_storage_from_qmi_storage_type (QmiWmsStorageType qmi_storage);

MMSmsState mm_sms_state_from_qmi_message_tag (QmiWmsMessageTagType tag);

/*****************************************************************************/
/* QMI/WDS to MM translations */

QmiWdsAuthentication mm_bearer_allowed_auth_to_qmi_authentication   (MMBearerAllowedAuth   auth,
                                                                     gpointer              log_object,
                                                                     GError              **error);
MMBearerAllowedAuth  mm_bearer_allowed_auth_from_qmi_authentication (QmiWdsAuthentication auth);
MMBearerIpFamily     mm_bearer_ip_family_from_qmi_ip_support_type   (QmiWdsIpSupportType ip_support_type);
MMBearerIpFamily     mm_bearer_ip_family_from_qmi_pdp_type          (QmiWdsPdpType pdp_type);
gboolean             mm_bearer_ip_family_to_qmi_pdp_type            (MMBearerIpFamily  ip_family,
                                                                     QmiWdsPdpType    *out_pdp_type);
QmiWdsApnTypeMask    mm_bearer_apn_type_to_qmi_apn_type             (MMBearerApnType apn_type,
                                                                     gpointer        log_object);
MMBearerApnType      mm_bearer_apn_type_from_qmi_apn_type           (QmiWdsApnTypeMask apn_type);

GError *qmi_mobile_equipment_error_from_verbose_call_end_reason_3gpp (QmiWdsVerboseCallEndReason3gpp vcer_3gpp,
                                                                      gpointer                       log_object);

/*****************************************************************************/
/* QMI/WDA to MM translations */

QmiDataEndpointType mm_port_net_driver_to_qmi_endpoint_type (const gchar *net_driver);

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
    /* Whether this is a multimode device or not */
    gboolean multimode;
    /* NAS System Selection Preference */
    QmiNasRatModePreference nas_ssp_mode_preference_mask;
    /* NAS Technology Preference */
    QmiNasRadioTechnologyPreference nas_tp_mask;
    /* DMS Capabilities */
    MMModemCapability dms_capabilities;
} MMQmiCurrentCapabilitiesContext;

MMModemCapability mm_current_capability_from_qmi_current_capabilities_context (MMQmiCurrentCapabilitiesContext *ctx,
                                                                               gpointer                         log_object);

/*****************************************************************************/
/* Utility to build list of supported capabilities from various sources */

typedef struct {
    /* Whether this is a multimode device or not */
    gboolean multimode;
    /* NAS System Selection Preference */
    gboolean nas_ssp_supported;
    /* NAS Technology Preference */
    gboolean nas_tp_supported;
    /* DMS Capabilities */
    MMModemCapability dms_capabilities;
} MMQmiSupportedCapabilitiesContext;

GArray *mm_supported_capabilities_from_qmi_supported_capabilities_context (MMQmiSupportedCapabilitiesContext *ctx,
                                                                           gpointer                           log_object);

/*****************************************************************************/
/* Utility to build list of supported modes from various sources */

typedef struct {
    /* Whether this is a multimode device or not */
    gboolean multimode;
    /* NAS System Selection Preference */
    gboolean nas_ssp_supported;
    /* NAS Technology Preference */
    gboolean nas_tp_supported;
    /* Mask with all supported modes */
    MMModemMode all;
    /* Current Capabilities */
    MMModemCapability current_capabilities;
} MMQmiSupportedModesContext;

GArray *mm_supported_modes_from_qmi_supported_modes_context (MMQmiSupportedModesContext *ctx,
                                                             gpointer                    log_object);

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
                                                  guint                             *o_pers_retries,
                                                  GError                           **error);

/*****************************************************************************/
/* UIM Get Configuration parsing */

gboolean mm_qmi_uim_get_configuration_output_parse (gpointer                              log_object,
                                                    QmiMessageUimGetConfigurationOutput  *output,
                                                    MMModem3gppFacility                  *o_lock,
                                                    GError                              **error);

gboolean qmi_personalization_feature_from_mm_modem_3gpp_facility (MMModem3gppFacility                          facility,
                                                                  QmiUimCardApplicationPersonalizationFeature *o_feature);

#endif  /* MM_MODEM_HELPERS_QMI_H */
