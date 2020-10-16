/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2018 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef MMCLI_OUTPUT_H
#define MMCLI_OUTPUT_H

#define _LIBMM_INSIDE_MMCLI
#include <libmm-glib.h>

/******************************************************************************/
/* List of sections (grouped fields) displayed in the human-friendly output */

typedef enum {
    MMC_S_UNKNOWN = -1,
    /* Modem object related sections */
    MMC_S_MODEM_GENERAL = 0,
    MMC_S_MODEM_HARDWARE,
    MMC_S_MODEM_SYSTEM,
    MMC_S_MODEM_NUMBERS,
    MMC_S_MODEM_STATUS,
    MMC_S_MODEM_MODES,
    MMC_S_MODEM_BANDS,
    MMC_S_MODEM_IP,
    MMC_S_MODEM_3GPP,
    MMC_S_MODEM_3GPP_EPS,
    MMC_S_MODEM_3GPP_SCAN,
    MMC_S_MODEM_3GPP_USSD,
    MMC_S_MODEM_CDMA,
    MMC_S_MODEM_SIM,
    MMC_S_MODEM_BEARER,
    MMC_S_MODEM_TIME,
    MMC_S_MODEM_TIMEZONE,
    MMC_S_MODEM_MESSAGING,
    MMC_S_MODEM_SIGNAL,
    MMC_S_MODEM_SIGNAL_CDMA1X,
    MMC_S_MODEM_SIGNAL_EVDO,
    MMC_S_MODEM_SIGNAL_GSM,
    MMC_S_MODEM_SIGNAL_UMTS,
    MMC_S_MODEM_SIGNAL_LTE,
    MMC_S_MODEM_SIGNAL_5G,
    MMC_S_MODEM_OMA,
    MMC_S_MODEM_OMA_CURRENT,
    MMC_S_MODEM_OMA_PENDING,
    MMC_S_MODEM_LOCATION,
    MMC_S_MODEM_LOCATION_3GPP,
    MMC_S_MODEM_LOCATION_GPS,
    MMC_S_MODEM_LOCATION_CDMABS,
    MMC_S_MODEM_FIRMWARE,
    MMC_S_MODEM_FIRMWARE_FASTBOOT,
    MMC_S_MODEM_VOICE,
    MMC_S_BEARER_GENERAL,
    MMC_S_BEARER_STATUS,
    MMC_S_BEARER_PROPERTIES,
    MMC_S_BEARER_IPV4_CONFIG,
    MMC_S_BEARER_IPV6_CONFIG,
    MMC_S_BEARER_STATS,
    MMC_S_CALL_GENERAL,
    MMC_S_CALL_PROPERTIES,
    MMC_S_CALL_AUDIO_FORMAT,
    MMC_S_SMS_GENERAL,
    MMC_S_SMS_CONTENT,
    MMC_S_SMS_PROPERTIES,
    MMC_S_SIM_GENERAL,
    MMC_S_SIM_PROPERTIES,
} MmcS;

/******************************************************************************/
/* List of fields */

typedef enum {
    MMC_F_UNKNOWN = -1,
    /* General section */
    MMC_F_GENERAL_DBUS_PATH = 0,
    MMC_F_GENERAL_DEVICE_ID,
    /* Hardware section */
    MMC_F_HARDWARE_MANUFACTURER,
    MMC_F_HARDWARE_MODEL,
    MMC_F_HARDWARE_REVISION,
    MMC_F_HARDWARE_CARRIER_CONF,
    MMC_F_HARDWARE_CARRIER_CONF_REV,
    MMC_F_HARDWARE_HW_REVISION,
    MMC_F_HARDWARE_SUPPORTED_CAPABILITIES,
    MMC_F_HARDWARE_CURRENT_CAPABILITIES,
    MMC_F_HARDWARE_EQUIPMENT_ID,
    /* System section */
    MMC_F_SYSTEM_DEVICE,
    MMC_F_SYSTEM_DRIVERS,
    MMC_F_SYSTEM_PLUGIN,
    MMC_F_SYSTEM_PRIMARY_PORT,
    MMC_F_SYSTEM_PORTS,
    /* Numbers section */
    MMC_F_NUMBERS_OWN,
    /* Status section */
    MMC_F_STATUS_LOCK,
    MMC_F_STATUS_UNLOCK_RETRIES,
    MMC_F_STATUS_STATE,
    MMC_F_STATUS_FAILED_REASON,
    MMC_F_STATUS_POWER_STATE,
    MMC_F_STATUS_ACCESS_TECH,
    MMC_F_STATUS_SIGNAL_QUALITY_VALUE,
    MMC_F_STATUS_SIGNAL_QUALITY_RECENT,
    /* Modes section */
    MMC_F_MODES_SUPPORTED,
    MMC_F_MODES_CURRENT,
    /* Bands section */
    MMC_F_BANDS_SUPPORTED,
    MMC_F_BANDS_CURRENT,
    /* IP section */
    MMC_F_IP_SUPPORTED,
    /* 3GPP section */
    MMC_F_3GPP_IMEI,
    MMC_F_3GPP_ENABLED_LOCKS,
    MMC_F_3GPP_OPERATOR_ID,
    MMC_F_3GPP_OPERATOR_NAME,
    MMC_F_3GPP_REGISTRATION,
    MMC_F_3GPP_PCO,
    /* 3GPP EPS section */
    MMC_F_3GPP_EPS_UE_MODE,
    MMC_F_3GPP_EPS_INITIAL_BEARER_PATH,
    MMC_F_3GPP_EPS_BEARER_SETTINGS_APN,
    MMC_F_3GPP_EPS_BEARER_SETTINGS_IP_TYPE,
    MMC_F_3GPP_EPS_BEARER_SETTINGS_USER,
    MMC_F_3GPP_EPS_BEARER_SETTINGS_PASSWORD,
    /* 3GPP scan section */
    MMC_F_3GPP_SCAN_NETWORKS,
    /* USSD section */
    MMC_F_3GPP_USSD_STATUS,
    MMC_F_3GPP_USSD_NETWORK_REQUEST,
    MMC_F_3GPP_USSD_NETWORK_NOTIFICATION,
    /* CDMA section */
    MMC_F_CDMA_MEID,
    MMC_F_CDMA_ESN,
    MMC_F_CDMA_SID,
    MMC_F_CDMA_NID,
    MMC_F_CDMA_REGISTRATION_CDMA1X,
    MMC_F_CDMA_REGISTRATION_EVDO,
    MMC_F_CDMA_ACTIVATION,
    /* SIM section */
    MMC_F_SIM_PATH,
    MMC_F_SIM_PRIMARY_SLOT,
    MMC_F_SIM_SLOT_PATHS,
    /* Bearer section */
    MMC_F_BEARER_PATHS,
    /* Time section */
    MMC_F_TIME_CURRENT,
    MMC_F_TIMEZONE_CURRENT,
    MMC_F_TIMEZONE_DST_OFFSET,
    MMC_F_TIMEZONE_LEAP_SECONDS,
    /* Messaging section */
    MMC_F_MESSAGING_SUPPORTED_STORAGES,
    MMC_F_MESSAGING_DEFAULT_STORAGES,
    /* Signal section */
    MMC_F_SIGNAL_REFRESH_RATE,
    MMC_F_SIGNAL_CDMA1X_RSSI,
    MMC_F_SIGNAL_CDMA1X_ECIO,
    MMC_F_SIGNAL_EVDO_RSSI,
    MMC_F_SIGNAL_EVDO_ECIO,
    MMC_F_SIGNAL_EVDO_SINR,
    MMC_F_SIGNAL_EVDO_IO,
    MMC_F_SIGNAL_GSM_RSSI,
    MMC_F_SIGNAL_UMTS_RSSI,
    MMC_F_SIGNAL_UMTS_RSCP,
    MMC_F_SIGNAL_UMTS_ECIO,
    MMC_F_SIGNAL_LTE_RSSI,
    MMC_F_SIGNAL_LTE_RSRQ,
    MMC_F_SIGNAL_LTE_RSRP,
    MMC_F_SIGNAL_LTE_SNR,
    MMC_F_SIGNAL_5G_RSRQ,
    MMC_F_SIGNAL_5G_RSRP,
    MMC_F_SIGNAL_5G_SNR,
    /* OMA section */
    MMC_F_OMA_FEATURES,
    MMC_F_OMA_CURRENT_TYPE,
    MMC_F_OMA_CURRENT_STATE,
    MMC_F_OMA_PENDING_SESSIONS,
    /* Location status section */
    MMC_F_LOCATION_CAPABILITIES,
    MMC_F_LOCATION_ENABLED,
    MMC_F_LOCATION_SIGNALS,
    MMC_F_LOCATION_GPS_REFRESH_RATE,
    MMC_F_LOCATION_GPS_SUPL_SERVER,
    MMC_F_LOCATION_GPS_ASSISTANCE,
    MMC_F_LOCATION_GPS_ASSISTANCE_SERVERS,
    MMC_F_LOCATION_3GPP_MCC,
    MMC_F_LOCATION_3GPP_MNC,
    MMC_F_LOCATION_3GPP_LAC,
    MMC_F_LOCATION_3GPP_TAC,
    MMC_F_LOCATION_3GPP_CID,
    MMC_F_LOCATION_GPS_NMEA,
    MMC_F_LOCATION_GPS_UTC,
    MMC_F_LOCATION_GPS_LONG,
    MMC_F_LOCATION_GPS_LAT,
    MMC_F_LOCATION_GPS_ALT,
    MMC_F_LOCATION_CDMABS_LONG,
    MMC_F_LOCATION_CDMABS_LAT,
    /* Firmware status section */
    MMC_F_FIRMWARE_LIST,
    MMC_F_FIRMWARE_METHOD,
    MMC_F_FIRMWARE_DEVICE_IDS,
    MMC_F_FIRMWARE_VERSION,
    MMC_F_FIRMWARE_FASTBOOT_AT,
    /* Voice section */
    MMC_F_VOICE_EMERGENCY_ONLY,
    /* Bearer general section */
    MMC_F_BEARER_GENERAL_DBUS_PATH,
    MMC_F_BEARER_GENERAL_TYPE,
    /* Bearer status section */
    MMC_F_BEARER_STATUS_CONNECTED,
    MMC_F_BEARER_STATUS_SUSPENDED,
    MMC_F_BEARER_STATUS_INTERFACE,
    MMC_F_BEARER_STATUS_IP_TIMEOUT,
    /* Bearer properties section */
    MMC_F_BEARER_PROPERTIES_APN,
    MMC_F_BEARER_PROPERTIES_ROAMING,
    MMC_F_BEARER_PROPERTIES_IP_TYPE,
    MMC_F_BEARER_PROPERTIES_ALLOWED_AUTH,
    MMC_F_BEARER_PROPERTIES_USER,
    MMC_F_BEARER_PROPERTIES_PASSWORD,
    MMC_F_BEARER_PROPERTIES_NUMBER,
    MMC_F_BEARER_PROPERTIES_RM_PROTOCOL,
    MMC_F_BEARER_IPV4_CONFIG_METHOD,
    MMC_F_BEARER_IPV4_CONFIG_ADDRESS,
    MMC_F_BEARER_IPV4_CONFIG_PREFIX,
    MMC_F_BEARER_IPV4_CONFIG_GATEWAY,
    MMC_F_BEARER_IPV4_CONFIG_DNS,
    MMC_F_BEARER_IPV4_CONFIG_MTU,
    MMC_F_BEARER_IPV6_CONFIG_METHOD,
    MMC_F_BEARER_IPV6_CONFIG_ADDRESS,
    MMC_F_BEARER_IPV6_CONFIG_PREFIX,
    MMC_F_BEARER_IPV6_CONFIG_GATEWAY,
    MMC_F_BEARER_IPV6_CONFIG_DNS,
    MMC_F_BEARER_IPV6_CONFIG_MTU,
    MMC_F_BEARER_STATS_DURATION,
    MMC_F_BEARER_STATS_BYTES_RX,
    MMC_F_BEARER_STATS_BYTES_TX,
    MMC_F_BEARER_STATS_ATTEMPTS,
    MMC_F_BEARER_STATS_FAILED_ATTEMPTS,
    MMC_F_BEARER_STATS_TOTAL_DURATION,
    MMC_F_BEARER_STATS_TOTAL_BYTES_RX,
    MMC_F_BEARER_STATS_TOTAL_BYTES_TX,
    MMC_F_CALL_GENERAL_DBUS_PATH,
    MMC_F_CALL_PROPERTIES_NUMBER,
    MMC_F_CALL_PROPERTIES_DIRECTION,
    MMC_F_CALL_PROPERTIES_MULTIPARTY,
    MMC_F_CALL_PROPERTIES_STATE,
    MMC_F_CALL_PROPERTIES_STATE_REASON,
    MMC_F_CALL_PROPERTIES_AUDIO_PORT,
    MMC_F_CALL_AUDIO_FORMAT_ENCODING,
    MMC_F_CALL_AUDIO_FORMAT_RESOLUTION,
    MMC_F_CALL_AUDIO_FORMAT_RATE,
    MMC_F_SMS_GENERAL_DBUS_PATH,
    MMC_F_SMS_CONTENT_NUMBER,
    MMC_F_SMS_CONTENT_TEXT,
    MMC_F_SMS_CONTENT_DATA,
    MMC_F_SMS_PROPERTIES_PDU_TYPE,
    MMC_F_SMS_PROPERTIES_STATE,
    MMC_F_SMS_PROPERTIES_VALIDITY,
    MMC_F_SMS_PROPERTIES_STORAGE,
    MMC_F_SMS_PROPERTIES_SMSC,
    MMC_F_SMS_PROPERTIES_CLASS,
    MMC_F_SMS_PROPERTIES_TELESERVICE_ID,
    MMC_F_SMS_PROPERTIES_SERVICE_CATEGORY,
    MMC_F_SMS_PROPERTIES_DELIVERY_REPORT,
    MMC_F_SMS_PROPERTIES_MSG_REFERENCE,
    MMC_F_SMS_PROPERTIES_TIMESTAMP,
    MMC_F_SMS_PROPERTIES_DELIVERY_STATE,
    MMC_F_SMS_PROPERTIES_DISCH_TIMESTAMP,
    MMC_F_SIM_GENERAL_DBUS_PATH,
    MMC_F_SIM_PROPERTIES_ACTIVE,
    MMC_F_SIM_PROPERTIES_IMSI,
    MMC_F_SIM_PROPERTIES_ICCID,
    MMC_F_SIM_PROPERTIES_EID,
    MMC_F_SIM_PROPERTIES_OPERATOR_ID,
    MMC_F_SIM_PROPERTIES_OPERATOR_NAME,
    MMC_F_SIM_PROPERTIES_EMERGENCY_NUMBERS,
    /* Lists */
    MMC_F_MODEM_LIST_DBUS_PATH,
    MMC_F_SMS_LIST_DBUS_PATH,
    MMC_F_CALL_LIST_DBUS_PATH,
} MmcF;

/******************************************************************************/
/* Output type selection */

typedef enum {
    MMC_OUTPUT_TYPE_NONE,
    MMC_OUTPUT_TYPE_HUMAN,
    MMC_OUTPUT_TYPE_KEYVALUE,
    MMC_OUTPUT_TYPE_JSON
} MmcOutputType;

void          mmcli_output_set (MmcOutputType type);
MmcOutputType mmcli_output_get (void);

/******************************************************************************/
/* Generic output management */

void mmcli_output_string                (MmcF            field,
                                         const gchar    *str);
void mmcli_output_string_take           (MmcF            field,
                                         gchar          *str);
void mmcli_output_string_list           (MmcF            field,
                                         const gchar    *str);
void mmcli_output_string_list_take      (MmcF            field,
                                         gchar          *str);
void mmcli_output_string_multiline      (MmcF            field,
                                         const gchar    *str);
void mmcli_output_string_multiline_take (MmcF            field,
                                         gchar          *str);
void mmcli_output_string_array          (MmcF            field,
                                         const gchar   **strv,
                                         gboolean        multiline);
void mmcli_output_string_array_take     (MmcF           field,
                                         gchar        **strv,
                                         gboolean       multiline);
void mmcli_output_string_take_typed     (MmcF           field,
                                         gchar         *value,
                                         const gchar   *type);
void mmcli_output_listitem              (MmcF           field,
                                         const gchar   *prefix,
                                         const gchar   *value,
                                         const gchar   *extra);

/******************************************************************************/
/* Custom output management */

void mmcli_output_signal_quality   (guint                      value,
                                    gboolean                   recent);
void mmcli_output_state            (MMModemState               state,
                                    MMModemStateFailedReason   reason);
void mmcli_output_sim_slots        (gchar                    **sim_slot_paths,
                                    guint                      primary_sim_slot);
void mmcli_output_scan_networks    (GList                     *network_list);
void mmcli_output_firmware_list    (GList                     *firmware_list,
                                    MMFirmwareProperties      *selected);
void mmcli_output_pco_list         (GList                     *pco_list);

/******************************************************************************/
/* Dump output */

void mmcli_output_dump      (void);
void mmcli_output_list_dump (MmcF field);

#endif /* MMCLI_OUTPUT_H */
