/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBQCDM_COMMANDS_H
#define LIBQCDM_COMMANDS_H

#include "utils.h"
#include "result.h"

/**********************************************************************/

/* Generic enums */

enum {
    QCDM_CDMA_PREV_UNKNOWN       = 0,
    QCDM_CDMA_PREV_IS_95         = 1, /* and J_STD008 */
    QCDM_CDMA_PREV_IS_95A        = 2,
    QCDM_CDMA_PREV_IS_95A_TSB74  = 3,
    QCDM_CDMA_PREV_IS_95B_PHASE1 = 4,
    QCDM_CDMA_PREV_IS_95B_PHASE2 = 5,
    QCDM_CDMA_PREV_IS2000_REL0   = 6,
    QCDM_CDMA_PREV_IS2000_RELA   = 7
};

enum {
    QCDM_CDMA_BAND_CLASS_UNKNOWN          = 0,
    QCDM_CDMA_BAND_CLASS_0_CELLULAR_800   = 1,  /* 800 MHz cellular band */
    QCDM_CDMA_BAND_CLASS_1_PCS            = 2,  /* 1800 to 2000 MHz PCS band */
    QCDM_CDMA_BAND_CLASS_2_TACS           = 3,  /* 872 to 960 MHz TACS band */
    QCDM_CDMA_BAND_CLASS_3_JTACS          = 4,  /* 832 to 925 MHz JTACS band */
    QCDM_CDMA_BAND_CLASS_4_KOREAN_PCS     = 5,  /* 1750 to 1870 MHz Korean PCS band */
    QCDM_CDMA_BAND_CLASS_5_NMT450         = 6,  /* 450 MHz NMT band */
    QCDM_CDMA_BAND_CLASS_6_IMT2000        = 7,  /* 2100 MHz IMT-2000 band */
    QCDM_CDMA_BAND_CLASS_7_CELLULAR_700   = 8,  /* Upper 700 MHz band */
    QCDM_CDMA_BAND_CLASS_8_1800           = 9,  /* 1800 MHz band */
    QCDM_CDMA_BAND_CLASS_9_900            = 10, /* 900 MHz band */
    QCDM_CDMA_BAND_CLASS_10_SECONDARY_800 = 11, /* Secondary 800 MHz band */
    QCDM_CDMA_BAND_CLASS_11_PAMR_400      = 12, /* 400 MHz European PAMR band */
    QCDM_CDMA_BAND_CLASS_12_PAMR_800      = 13, /* 800 MHz PAMR band */
    QCDM_CDMA_BAND_CLASS_13_IMT2000_2500  = 14, /* 2500 MHz IMT-2000 Extension Band */
    QCDM_CDMA_BAND_CLASS_14_US_PCS_1900   = 15, /* US PCS 1900 MHz Band */
    QCDM_CDMA_BAND_CLASS_15_AWS           = 16, /* AWS 1700 MHz band */
    QCDM_CDMA_BAND_CLASS_16_US_2500       = 17, /* US 2500 MHz Band */
    QCDM_CDMA_BAND_CLASS_17_US_FLO_2500   = 18, /* US 2500 MHz Forward Link Only Band */
    QCDM_CDMA_BAND_CLASS_18_US_PS_700     = 19, /* 700 MHz Public Safety Band */
    QCDM_CDMA_BAND_CLASS_19_US_LOWER_700  = 20  /* Lower 700 MHz Band */
};

enum {
    QCDM_HDR_REV_UNKNOWN = 0x00,
    QCDM_HDR_REV_0 = 0x01,
    QCDM_HDR_REV_A = 0x02
};

enum {
    QCDM_ERI_ROAMING_ICON_ON = 0,
    QCDM_ERI_ROAMING_ICON_OFF = 1,
    QCDM_ERI_ROAMING_ICON_FLASH = 2,
    /* Values greater than 2 are OEM defined */
};

enum {
    /* Valid with QCDM_ERI_ROAMING_ICON_FLASH and greater */
    QCDM_ERI_ROAMING_ICON_MODE_NORMAL = 0,
    QCDM_ERI_ROAMING_ICON_MODE_FLASH = 1,
};

/**********************************************************************/

#define QCDM_CMD_VERSION_INFO_ITEM_COMP_DATE "comp-date"
#define QCDM_CMD_VERSION_INFO_ITEM_COMP_TIME "comp-time"
#define QCDM_CMD_VERSION_INFO_ITEM_RELEASE_DATE "release-date"
#define QCDM_CMD_VERSION_INFO_ITEM_RELEASE_TIME "release-time"
#define QCDM_CMD_VERSION_INFO_ITEM_MODEL "model"

size_t      qcdm_cmd_version_info_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_version_info_result (const char *buf,
                                          size_t len,
                                          int *out_error);

/**********************************************************************/

#define QCDM_CMD_ESN_ITEM_ESN "esn"

size_t      qcdm_cmd_esn_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_esn_result (const char *buf,
                                 size_t len,
                                 int *out_error);

/**********************************************************************/

enum {
    QCDM_CMD_CONTROL_MODE_OFFLINE = 1,
    QCDM_CMD_CONTROL_MODE_RESET = 2,
};

size_t      qcdm_cmd_control_new    (char *buf, size_t len, uint8_t mode);

QcdmResult *qcdm_cmd_control_result (const char *buf,
                                     size_t len,
                                     int *out_error);

/**********************************************************************/

/* Values for QCDM_CMD_CDMA_STATUS_ITEM_RF_MODE */
enum {
    QCDM_CMD_CDMA_STATUS_RF_MODE_ANALOG = 0,
    QCDM_CMD_CDMA_STATUS_RF_MODE_CDMA_CELLULAR = 1,
    QCDM_CMD_CDMA_STATUS_RF_MODE_CDMA_PCS = 2,
    QCDM_CMD_CDMA_STATUS_RF_MODE_SLEEP = 3,
    QCDM_CMD_CDMA_STATUS_RF_MODE_GPS = 4,
    QCDM_CMD_CDMA_STATUS_RF_MODE_HDR = 5,
};

/* Values for QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE */
enum {
    QCDM_CMD_CDMA_STATUS_RX_STATE_ENTERING_CDMA = 0,
    QCDM_CMD_CDMA_STATUS_RX_STATE_SYNC_CHANNEL = 1,
    QCDM_CMD_CDMA_STATUS_RX_STATE_PAGING_CHANNEL = 2,
    QCDM_CMD_CDMA_STATUS_RX_STATE_TRAFFIC_CHANNEL_INIT = 3,
    QCDM_CMD_CDMA_STATUS_RX_STATE_TRAFFIC_CHANNEL = 4,
    QCDM_CMD_CDMA_STATUS_RX_STATE_EXITING_CDMA = 5,
};

#define QCDM_CMD_CDMA_STATUS_ITEM_ESN             "esn"
#define QCDM_CMD_CDMA_STATUS_ITEM_RF_MODE         "rf-mode"
#define QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE        "rx-state"
#define QCDM_CMD_CDMA_STATUS_ITEM_ENTRY_REASON    "entry-reason"
#define QCDM_CMD_CDMA_STATUS_ITEM_CURRENT_CHANNEL "current-channel"
#define QCDM_CMD_CDMA_STATUS_ITEM_CODE_CHANNEL    "code-channel"
#define QCDM_CMD_CDMA_STATUS_ITEM_PILOT_BASE      "pilot-base"
#define QCDM_CMD_CDMA_STATUS_ITEM_SID             "sid"
#define QCDM_CMD_CDMA_STATUS_ITEM_NID             "nid"

size_t      qcdm_cmd_cdma_status_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_cdma_status_result (const char *buf,
                                         size_t len,
                                         int *out_error);

/**********************************************************************/

/* NOTE: this command does not appear to be implemented in recent
 * devices and probably returns (QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_COMMAND).
 */

#define QCDM_CMD_SW_VERSION_ITEM_VERSION   "version"
#define QCDM_CMD_SW_VERSION_ITEM_COMP_DATE "comp-date"
#define QCDM_CMD_SW_VERSION_ITEM_COMP_TIME "comp-time"

size_t      qcdm_cmd_sw_version_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_sw_version_result (const char *buf,
                                        size_t len,
                                        int *out_error);

/**********************************************************************/

#define QCDM_CMD_STATUS_SNAPSHOT_ITEM_ESN                "esn"

#define QCDM_CMD_STATUS_SNAPSHOT_ITEM_HOME_MCC           "mcc"

/* One of QCDM_CDMA_BAND_CLASS_* */
#define QCDM_CMD_STATUS_SNAPSHOT_ITEM_BAND_CLASS         "band-class"

/* The protocol revision of the base station.  One of QCDM_CDMA_PREV_* */
#define QCDM_CMD_STATUS_SNAPSHOT_ITEM_BASE_STATION_PREV  "prev"

/* The protocol revision of the mobile terminal.  One of QCDM_CDMA_PREV_* */
#define QCDM_CMD_STATUS_SNAPSHOT_ITEM_MOBILE_PREV        "mob-prev"

/* The protocol revision currently in-use.  One of QCDM_CDMA_PREV_* */
#define QCDM_CMD_STATUS_SNAPSHOT_ITEM_PREV_IN_USE        "prev-in-use"

enum {
    QCDM_CMD_STATUS_SNAPSHOT_STATE_UNKNOWN            = 0x00,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_NO_SERVICE         = 0x01,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_INITIALIZATION     = 0x02,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_IDLE               = 0x03,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_VOICE_CHANNEL_INIT = 0x04,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_WAITING_FOR_ORDER  = 0x05,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_WAITING_FOR_ANSWER = 0x06,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_CONVERSATION       = 0x07,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_RELEASE            = 0x08,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_SYSTEM_ACCESS      = 0x09,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_OFFLINE_CDMA       = 0x11,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_OFFLINE_HDR        = 0x12,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_OFFLINE_ANALOG     = 0x13,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_RESET              = 0x14,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_POWER_DOWN         = 0x15,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_POWER_SAVE         = 0x16,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_POWER_UP           = 0x17,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_LOW_POWER_MODE     = 0x18,
    QCDM_CMD_STATUS_SNAPSHOT_STATE_SEARCHER_DSMM      = 0x19, /* Dedicated System Measurement Mode */
    QCDM_CMD_STATUS_SNAPSHOT_STATE_HDR                = 0x41,
};

/* The protocol revision currently in-use.  One of QCDM_STATUS_SNAPSHOT_STATE_* */
#define QCDM_CMD_STATUS_SNAPSHOT_ITEM_STATE              "state"

size_t      qcdm_cmd_status_snapshot_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_status_snapshot_result (const char *buf,
                                             size_t len,
                                             int *out_error);

/**********************************************************************/

enum {
    QCDM_CMD_PILOT_SETS_TYPE_UNKNOWN = 0,
    QCDM_CMD_PILOT_SETS_TYPE_ACTIVE = 1,
    QCDM_CMD_PILOT_SETS_TYPE_CANDIDATE = 2,
    QCDM_CMD_PILOT_SETS_TYPE_NEIGHBOR = 3,
};

size_t      qcdm_cmd_pilot_sets_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_pilot_sets_result (const char *buf,
                                        size_t len,
                                        int *out_error);

qcdmbool    qcdm_cmd_pilot_sets_result_get_num   (QcdmResult *result,
                                                  uint32_t set_type,
                                                  uint32_t *out_num);

qcdmbool    qcdm_cmd_pilot_sets_result_get_pilot (QcdmResult *result,
                                                  uint32_t set_type,
                                                  uint32_t num,
                                                  uint32_t *out_pn_offset,
                                                  uint32_t *out_ecio,
                                                  float *out_db);

/**********************************************************************/

#define QCDM_CMD_NV_GET_MDN_ITEM_PROFILE "profile"
#define QCDM_CMD_NV_GET_MDN_ITEM_MDN "mdn"

size_t      qcdm_cmd_nv_get_mdn_new    (char *buf, size_t len, uint8_t profile);

QcdmResult *qcdm_cmd_nv_get_mdn_result (const char *buf,
                                        size_t len,
                                        int *out_error);

/**********************************************************************/

/* Values for QCDM_CMD_NV_GET_ROAM_PREF_ITEM_ROAM_PREF */
enum {
    QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_HOME_ONLY = 0x01,
    QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_ROAM_ONLY = 0x06,
    QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_AUTO = 0xFF,
};

#define QCDM_CMD_NV_GET_ROAM_PREF_ITEM_PROFILE   "profile"
#define QCDM_CMD_NV_GET_ROAM_PREF_ITEM_ROAM_PREF "roam-pref"

size_t      qcdm_cmd_nv_get_roam_pref_new    (char *buf,
                                              size_t len,
                                              uint8_t profile);

QcdmResult *qcdm_cmd_nv_get_roam_pref_result (const char *buf,
                                              size_t len,
                                              int *out_error);

size_t      qcdm_cmd_nv_set_roam_pref_new    (char *buf,
                                              size_t len,
                                              uint8_t profile,
                                              uint8_t roam_pref);

QcdmResult *qcdm_cmd_nv_set_roam_pref_result (const char *buf,
                                              size_t len,
                                              int *out_error);

/**********************************************************************/

/* Values for QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF */
enum {
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_DIGITAL         = 0x00,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_DIGITAL_ONLY    = 0x01,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_ANALOG          = 0x02,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_ANALOG_ONLY     = 0x03,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_AUTO            = 0x04,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_ONLY         = 0x09,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_HDR_ONLY        = 0x0A,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GPRS_ONLY       = 0x0D,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_UMTS_ONLY       = 0x0E,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_ONLY   = 0x11,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_ONLY     = 0x13,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_LTE_ONLY        = 0x1E,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_LTE_ONLY = 0x1F,
    QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_LTE_ONLY = 0x24,
};

#define QCDM_CMD_NV_GET_MODE_PREF_ITEM_PROFILE   "profile"
#define QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF "mode-pref"

size_t      qcdm_cmd_nv_get_mode_pref_new    (char *buf,
                                              size_t len,
                                              uint8_t profile);

QcdmResult *qcdm_cmd_nv_get_mode_pref_result (const char *buf,
                                              size_t len,
                                              int *out_error);

size_t      qcdm_cmd_nv_set_mode_pref_new    (char *buf,
                                              size_t len,
                                              uint8_t profile,
                                              uint8_t mode_pref);

QcdmResult *qcdm_cmd_nv_set_mode_pref_result (const char *buf,
                                              size_t len,
                                              int *out_error);

/**********************************************************************/

enum {
    QCDM_CMD_NV_HYBRID_PREF_ITEM_REV_HYBRID_OFF = 0x00,
    QCDM_CMD_NV_HYBRID_PREF_ITEM_REV_HYBRID_ON = 0x01,
};

#define QCDM_CMD_NV_GET_HYBRID_PREF_ITEM_HYBRID_PREF "hybrid-pref"

size_t      qcdm_cmd_nv_get_hybrid_pref_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_nv_get_hybrid_pref_result (const char *buf,
                                                size_t len,
                                                int *out_error);

size_t      qcdm_cmd_nv_set_hybrid_pref_new    (char *buf,
                                                size_t len,
                                                uint8_t hybrid_pref);

QcdmResult *qcdm_cmd_nv_set_hybrid_pref_result (const char *buf,
                                                size_t len,
                                                int *out_error);

/**********************************************************************/

enum {
    QCDM_CMD_NV_IPV6_ENABLED_OFF = 0x00,
    QCDM_CMD_NV_IPV6_ENABLED_ON = 0x01,
};

#define QCDM_CMD_NV_GET_IPV6_ENABLED_ITEM_ENABLED "ipv6-enabled"

size_t      qcdm_cmd_nv_get_ipv6_enabled_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_nv_get_ipv6_enabled_result (const char *buf,
                                                size_t len,
                                                int *out_error);

size_t      qcdm_cmd_nv_set_ipv6_enabled_new    (char *buf,
                                                size_t len,
                                                uint8_t enabled);

QcdmResult *qcdm_cmd_nv_set_ipv6_enabled_result (const char *buf,
                                                size_t len,
                                                int *out_error);

/**********************************************************************/

/* Values for QCDM_CMD_NV_GET_HDR_REV_PREF_ITEM_REV_PREF */
enum {
    QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_0 = 0x00,
    QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A = 0x01,
    QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD = 0x04,
};

#define QCDM_CMD_NV_GET_HDR_REV_PREF_ITEM_REV_PREF "rev-pref"

size_t      qcdm_cmd_nv_get_hdr_rev_pref_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_nv_get_hdr_rev_pref_result (const char *buf,
                                                 size_t len,
                                                 int *out_error);

size_t      qcdm_cmd_nv_set_hdr_rev_pref_new    (char *buf,
                                                 size_t len,
                                                 uint8_t rev_pref);

QcdmResult *qcdm_cmd_nv_set_hdr_rev_pref_result (const char *buf,
                                                 size_t len,
                                                 int *out_error);

/**********************************************************************/

/* Values for QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE */
enum {
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_POWER_OFF = 0,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_FIELD_TEST_MODE = 1,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_OFFLINE = 2,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_OFFLINE_AMPS = 3,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_OFFLINE_CDMA = 4,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_ONLINE = 5,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_LOW_POWER_MODE = 6,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_RESET = 7
};

/* Values for QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE */
enum {
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_NO_SERVICE = 0,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_AMPS = 1,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_CDMA = 2,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GSM = 3,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_HDR = 4,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_WCDMA = 5,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GPS = 6,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GW = 7,     /* GSM & WCDMA */
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_WLAN = 8,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_LTE = 9,
};

enum {
    QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_IDLE = 0,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_ORIGINATING = 1,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_ALERTING = 3,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_ORIGINATION_ALERTING = 4,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_CONVERSATION = 5,
};

/* Values for QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF */
enum {
    QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_HOME_ONLY = 0x01,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_ROAM_ONLY = 0x06,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_AUTO = 0xFF,
};

/* Values for QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_MODE_PREF */
enum {
    /* Note: not the same values as QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF has;
     * AUTO really is 0x02 here, not 0x04 like the NV item value for AUTO.
     */
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_AMPS_ONLY = 0x00,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_DIGITAL_ONLY = 0x01,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_AUTO = 0x02,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_EMERGENCY = 0x03,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_1X_ONLY = 0x09,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_HDR_ONLY = 0x0A,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_1X_AMPS_ONLY = 0x0B,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_GPS_ONLY = 0x0C,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_GSM_ONLY = 0x0D,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_WCDMA_ONLY = 0x0E,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_PERSISTENT_MODE = 0x0F,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_NO_CHANGE = 0x10,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_LTE_ONLY = 0x26,
    QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_GSM_WCDMA_LTE_ONLY = 0x27,
};

#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_CALL_STATE             "call-state"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE         "operating-mode"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE            "system-mode"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_MODE_PREF              "mode-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_BAND_PREF              "band-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF              "roam-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SERVICE_DOMAIN_PREF    "service-domain-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ACQ_ORDER_PREF         "acq-order-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_HYBRID_PREF            "hybrid-pref"
#define QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_NETWORK_SELECTION_PREF "network-selection-pref"

size_t      qcdm_cmd_cm_subsys_state_info_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_cm_subsys_state_info_result (const char *buf,
                                                  size_t len,
                                                  int *out_error);

/**********************************************************************/

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_AT_STATE */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_INACTIVE = 0,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_ACQUISITION = 1,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_SYNC = 2,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_IDLE = 3,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_ACCESS = 4,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_CONNECTED = 5
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSED = 0,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_SETUP = 1,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_AT_INIT = 2,  /* initiated by Access Terminal */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_AN_INIT = 3,  /* initiated by Access Node */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_OPEN = 4,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSING = 5
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE (TIA-856-A section 9.2.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INACTIVE = 0,  /* initial state */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INIT = 1,  /* terminal has yet to acquire network */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_IDLE = 2,  /* network acquired but no connection */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_CONNECTED = 3,  /* open connection to the network */
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_INIT_STATE (TIA-856-A section 9.3.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_INACTIVE = 0,  /* protocol waiting for ACTIVATE command */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_NET_DETERMINE = 1,  /* choosing a network to operate on */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_ACQUISITION = 2,  /* acquiring Forward Pilot Channel */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_SYNC = 3,  /* synchronizing to Control Channel */
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_IDLE_STATE (TIA-856-A section 9.4.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_INACTIVE = 0,  /* protocol waiting for ACTIVATE command */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_SLEEP = 1,  /* sleeping */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_MONITOR = 2,  /* monitoring the Control Channel */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_SETUP = 3,  /* setting up a connection */
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_CONNECTED_STATE (TIA-856-A section 9.6.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_INACTIVE = 0,  /* protocol waiting for ACTIVATE command */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_OPEN = 1,  /* connection is open */
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_CLOSING = 2,  /* connection is closed */
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ROUTE_UPDATE (TIA-856-A section 9.7.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_INACTIVE = 0,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_IDLE = 1,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_CONNECTED = 2,
};

/* Values for QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_OVERHEAD_MSG (TIA-856-A section 9.9.1) */
enum {
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_INIT = 0,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_INACTIVE = 1,
    QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_ACTIVE = 2,
};

#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_AT_STATE           "at-state"  /* State of Access Terminal */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE      "session-state"  /* Current session state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE         "almp-state"  /* Air Link Management Protocol (ALMP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_INIT_STATE         "init-state"  /* Initialization State Protocol (ISP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_IDLE_STATE         "idle-state"  /* Idle State Protocol (IDP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_CONNECTED_STATE    "connected-state"  /* Connected State Protocol (CSP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ROUTE_UPDATE_STATE "route-update-state"  /* Route Update Protocol (RUP) state */
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_OVERHEAD_MSG_STATE "overhead-msg-state"
#define QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_HDR_HYBRID_MODE    "hdr-hybrid-mode"

size_t      qcdm_cmd_hdr_subsys_state_info_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_hdr_subsys_state_info_result (const char *buf,
                                                   size_t len,
                                                   int *out_error);

/**********************************************************************/

/* Max # of log items this device supports */
#define QCDM_CMD_EXT_LOGMASK_ITEM_MAX_ITEMS   "max-items"

size_t      qcdm_cmd_ext_logmask_new    (char *buf,
                                         size_t len,
                                         uint32_t items[], /* terminated by 0 */
                                         uint16_t maxlog);

QcdmResult *qcdm_cmd_ext_logmask_result (const char *buf,
                                         size_t len,
                                         int *out_error);

/* Returns TRUE if 'item' is set in the log mask */
qcdmbool    qcmd_cmd_ext_logmask_result_get_item (QcdmResult *result,
                                                  uint16_t item);

/**********************************************************************/

size_t      qcdm_cmd_event_report_new    (char *buf,
                                          size_t len,
                                          qcdmbool start);

QcdmResult *qcdm_cmd_event_report_result (const char *buf,
                                          size_t len,
                                          int *out_error);

/**********************************************************************/

size_t qcdm_cmd_log_config_get_mask_new (char *buf,
                                         size_t len,
                                         uint32_t equip_id);

size_t qcdm_cmd_log_config_set_mask_new (char *buf,
                                         size_t len,
                                         uint32_t equip_id,
                                         uint16_t items[]);

#define QCDM_CMD_LOG_CONFIG_MASK_ITEM_EQUIP_ID  "equip-id"

#define QCDM_CMD_LOG_CONFIG_MASK_ITEM_NUM_ITEMS "num-items"

#define QCDM_CMD_LOG_CONFIG_MASK_ITEM_ITEMS     "items"

QcdmResult *qcdm_cmd_log_config_get_mask_result (const char *buf,
                                                 size_t len,
                                                 int *out_error);

QcdmResult *qcdm_cmd_log_config_set_mask_result (const char *buf,
                                                 size_t len,
                                                 int *out_error);

qcdmbool    qcmd_cmd_log_config_mask_result_code_set (QcdmResult *result,
                                                      uint32_t equipid,
                                                      uint16_t log_code);

/**********************************************************************/

#define QCDM_CMD_ZTE_SUBSYS_STATUS_ITEM_SIGNAL_INDICATOR    "signal-indicator"

size_t      qcdm_cmd_zte_subsys_status_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_zte_subsys_status_result (const char *buf,
                                               size_t len,
                                               int *out_error);

/**********************************************************************/

#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_RSSI       "rssi"

/* One of QCDM_CDMA_PREV_* */
#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_PREV       "prev"

/* One of QCDM_CDMA_BAND_CLASS_* */
#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_BAND_CLASS "band-class"

#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_ERI        "eri"

/* One of QCDM_HDR_REV_* */
#define QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV    "hdr-rev"

enum {
    QCDM_NW_CHIPSET_UNKNOWN = 0,
    QCDM_NW_CHIPSET_6500 = 1,
    QCDM_NW_CHIPSET_6800 = 2,
};

size_t      qcdm_cmd_nw_subsys_modem_snapshot_cdma_new    (char *buf,
                                                           size_t len,
                                                           uint8_t chipset);

QcdmResult *qcdm_cmd_nw_subsys_modem_snapshot_cdma_result (const char *buf,
                                                           size_t len,
                                                           int *out_error);

/**********************************************************************/

#define QCDM_CMD_NW_SUBSYS_ERI_ITEM_ROAM           "roam"

#define QCDM_CMD_NW_SUBSYS_ERI_ITEM_INDICATOR_ID   "indicator-id"

/* One of QCDM_ERI_ROAMING_ICON_* */
#define QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_ID        "icon-id"

/* One of QCDM_ERI_ROAMING_ICON_MODE_* */
#define QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_MODE      "icon-mode"

#define QCDM_CMD_NW_SUBSYS_ERI_ITEM_CALL_PROMPT_ID "call-prompt-id"

#define QCDM_CMD_NW_SUBSYS_ERI_ITEM_ALERT_ID       "alert-id"

#define QCDM_CMD_NW_SUBSYS_ERI_ITEM_TEXT           "text"

size_t      qcdm_cmd_nw_subsys_eri_new    (char *buf,
                                           size_t len,
                                           uint8_t chipset);

QcdmResult *qcdm_cmd_nw_subsys_eri_result (const char *buf,
                                           size_t len,
                                           int *out_error);

/**********************************************************************/

#define QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_IMEI     "imei"

#define QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_IMSI     "imsi"

/* Values for QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_L1_STATE */

enum {
    QCDM_WCDMA_L1_STATE_IDLE = 0,
    QCDM_WCDMA_L1_STATE_FS = 1,
    QCDM_WCDMA_L1_STATE_ACQ = 2,
    QCDM_WCDMA_L1_STATE_BCH = 3,
    QCDM_WCDMA_L1_STATE_PCH = 4,
    QCDM_WCDMA_L1_STATE_FACH = 5,
    QCDM_WCDMA_L1_STATE_DCH = 6,
    QCDM_WCDMA_L1_STATE_DEACTIVATE = 7,
    QCDM_WCDMA_L1_STATE_PCH_SLEEP = 8,
    QCDM_WCDMA_L1_STATE_DEEP_SLEEP = 9,
    QCDM_WCDMA_L1_STATE_STOPPED = 10,
    QCDM_WCDMA_L1_STATE_SUSPENDED = 11,
    QCDM_WCDMA_L1_STATE_PCH_BPLMN = 12,
    QCDM_WCDMA_L1_STATE_WAIT_TRM_STOP = 13,
};

/* One of QCDM_WCDMA_L1_STATE_* */
#define QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_L1_STATE "l1-state"

size_t      qcdm_cmd_wcdma_subsys_state_info_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_wcdma_subsys_state_info_result (const char *buf,
                                                     size_t len,
                                                     int *out_error);

/**********************************************************************/

#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_IMEI     "imei"

#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_IMSI     "imsi"

#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_MCC  "lai-mcc"

#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_MNC  "lai-mnc"

#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_LAC  "lai-lac"

#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CELLID   "cellid"

/* One of QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_* */
#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_CALL_STATE  "cm-call-state"

/* One of QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_* */
#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_OP_MODE     "cm-op-mode"

/* One of QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_* */
#define QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_SYS_MODE    "cm-sys-mode"

size_t      qcdm_cmd_gsm_subsys_state_info_new    (char *buf, size_t len);

QcdmResult *qcdm_cmd_gsm_subsys_state_info_result (const char *buf,
                                                   size_t len,
                                                   int *out_error);

/**********************************************************************/

#endif  /* LIBQCDM_COMMANDS_H */
