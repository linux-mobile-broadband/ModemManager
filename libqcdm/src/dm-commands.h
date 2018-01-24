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

#ifndef LIBQCDM_DM_COMMANDS_H
#define LIBQCDM_DM_COMMANDS_H

enum {
    DIAG_CMD_VERSION_INFO = 0,  /* Version info */
    DIAG_CMD_ESN          = 1,  /* ESN */
    DIAG_CMD_PEEKB        = 2,  /* Peek byte */
    DIAG_CMD_PEEKW        = 3,  /* Peek word */
    DIAG_CMD_PEEKD        = 4,  /* Peek dword */
    DIAG_CMD_POKEB        = 5,  /* Poke byte */
    DIAG_CMD_POKEW        = 6,  /* Poke word */
    DIAG_CMD_POKED        = 7,  /* Poke dword */
    DIAG_CMD_OUTP         = 8,  /* Byte output */
    DIAG_CMD_OUTPW        = 9,  /* Word output */
    DIAG_CMD_INP          = 10, /* Byte input */
    DIAG_CMD_INPW         = 11, /* Word input */
    DIAG_CMD_STATUS       = 12, /* Station status */
    DIAG_CMD_LOGMASK      = 15, /* Set logging mask */
    DIAG_CMD_LOG          = 16, /* Log packet */
    DIAG_CMD_NV_PEEK      = 17, /* Peek NV memory */
    DIAG_CMD_NV_POKE      = 18, /* Poke NV memory */
    DIAG_CMD_BAD_CMD      = 19, /* Invalid command (response) */
    DIAG_CMD_BAD_PARM     = 20, /* Invalid parameter (response) */
    DIAG_CMD_BAD_LEN      = 21, /* Invalid packet length (response) */
    DIAG_CMD_BAD_DEV      = 22, /* Not accepted by the device (response) */
    DIAG_CMD_BAD_MODE     = 24, /* Not allowed in this mode (response) */
    DIAG_CMD_TAGRAPH      = 25, /* Info for TA power and voice graphs */
    DIAG_CMD_MARKOV       = 26, /* Markov stats */
    DIAG_CMD_MARKOV_RESET = 27, /* Reset Markov stats */
    DIAG_CMD_DIAG_VER     = 28, /* Diagnostic Monitor version */
    DIAG_CMD_TIMESTAMP    = 29, /* Return a timestamp */
    DIAG_CMD_TA_PARM      = 30, /* Set TA parameters */
    DIAG_CMD_MESSAGE      = 31, /* Request for msg report */
    DIAG_CMD_HS_KEY       = 32, /* Handset emulation -- keypress */
    DIAG_CMD_HS_LOCK      = 33, /* Handset emulation -- lock or unlock */
    DIAG_CMD_HS_SCREEN    = 34, /* Handset emulation -- display request */
    DIAG_CMD_PARM_SET     = 36, /* Parameter download */
    DIAG_CMD_NV_READ      = 38, /* Read NV item */
    DIAG_CMD_NV_WRITE     = 39, /* Write NV item */
    DIAG_CMD_CONTROL      = 41, /* Mode change request */
    DIAG_CMD_ERR_READ     = 42, /* Error record retrieval */
    DIAG_CMD_ERR_CLEAR    = 43, /* Error record clear */
    DIAG_CMD_SER_RESET    = 44, /* Symbol error rate counter reset */
    DIAG_CMD_SER_REPORT   = 45, /* Symbol error rate counter report */
    DIAG_CMD_TEST         = 46, /* Run a specified test */
    DIAG_CMD_GET_DIPSW    = 47, /* Retrieve the current DIP switch setting */
    DIAG_CMD_SET_DIPSW    = 48, /* Write new DIP switch setting */
    DIAG_CMD_VOC_PCM_LB   = 49, /* Start/Stop Vocoder PCM loopback */
    DIAG_CMD_VOC_PKT_LB   = 50, /* Start/Stop Vocoder PKT loopback */
    DIAG_CMD_ORIG         = 53, /* Originate a call */
    DIAG_CMD_END          = 54, /* End a call */
    DIAG_CMD_SW_VERSION   = 56, /* Get software version */
    DIAG_CMD_DLOAD        = 58, /* Switch to downloader */
    DIAG_CMD_TMOB         = 59, /* Test Mode Commands and FTM commands*/
    DIAG_CMD_STATE        = 63, /* Current state of the phone */
    DIAG_CMD_PILOT_SETS   = 64, /* Return all current sets of pilots */
    DIAG_CMD_SPC          = 65, /* Send the Service Programming Code to unlock */
    DIAG_CMD_BAD_SPC_MODE = 66, /* Invalid NV read/write because SP is locked */
    DIAG_CMD_PARM_GET2    = 67, /* (obsolete) */
    DIAG_CMD_SERIAL_CHG   = 68, /* Serial mode change */
    DIAG_CMD_PASSWORD     = 70, /* Send password to unlock secure operations */
    DIAG_CMD_BAD_SEC_MODE = 71, /* Operation not allowed in this security state */
    DIAG_CMD_PRL_WRITE         = 72,  /* Write PRL */
    DIAG_CMD_PRL_READ          = 73,  /* Read PRL */
    DIAG_CMD_SUBSYS            = 75,  /* Subsystem commands */
    DIAG_CMD_FEATURE_QUERY     = 81,
    DIAG_CMD_SMS_READ          = 83,  /* Read SMS message out of NV memory */
    DIAG_CMD_SMS_WRITE         = 84,  /* Write SMS message into NV memory */
    DIAG_CMD_SUP_FER           = 85,  /* Frame Error Rate info on multiple channels */
    DIAG_CMD_SUP_WALSH_CODES   = 86,  /* Supplemental channel walsh codes */
    DIAG_CMD_SET_MAX_SUP_CH    = 87,  /* Sets the maximum # supplemental channels */
    DIAG_CMD_PARM_GET_IS95B    = 88,  /* Get parameters including SUPP and MUX2 */
    DIAG_CMD_FS_OP             = 89,  /* Embedded File System (EFS) operations */
    DIAG_CMD_AKEY_VERIFY       = 90,  /* AKEY Verification */
    DIAG_CMD_HS_BMP_SCREEN     = 91,  /* Handset Emulation -- Bitmap screen */
    DIAG_CMD_CONFIG_COMM       = 92,  /* Configure communications */
    DIAG_CMD_EXT_LOGMASK       = 93,  /* Extended logmask for > 32 bits */
    DIAG_CMD_EVENT_REPORT      = 96,  /* Static Event reporting */
    DIAG_CMD_STREAMING_CONFIG  = 97,  /* Load balancing etc */
    DIAG_CMD_PARM_RETRIEVE     = 98,  /* Parameter retrieval */
    DIAG_CMD_STATUS_SNAPSHOT   = 99,  /* Status snapshot */
    DIAG_CMD_RPC               = 100, /* Used for RPC */
    DIAG_CMD_GET_PROPERTY      = 101,
    DIAG_CMD_PUT_PROPERTY      = 102,
    DIAG_CMD_GET_GUID          = 103, /* GUID requests */
    DIAG_CMD_USER_CMD          = 104, /* User callbacks */
    DIAG_CMD_GET_PERM_PROPERTY = 105,
    DIAG_CMD_PUT_PERM_PROPERTY = 106,
    DIAG_CMD_PERM_USER_CMD     = 107, /* Permanent user callbacks */
    DIAG_CMD_GPS_SESS_CTRL     = 108, /* GPS session control */
    DIAG_CMD_GPS_GRID          = 109, /* GPS search grid */
    DIAG_CMD_GPS_STATISTICS    = 110,
    DIAG_CMD_TUNNEL            = 111, /* Tunneling command code */
    DIAG_CMD_RAM_RW            = 112, /* Calibration RAM control using DM */
    DIAG_CMD_CPU_RW            = 113, /* Calibration CPU control using DM */
    DIAG_CMD_SET_FTM_TEST_MODE = 114, /* Field (or Factory?) Test Mode */
    DIAG_CMD_LOG_CONFIG        = 115, /* New logging config command */
    DIAG_CMD_EXT_BUILD_ID      = 124,
    DIAG_CMD_EXT_MESSAGE_CONFIG= 125,
    DIAG_CMD_EVENT_GET_MASK    = 129,
    DIAG_CMD_EVENT_SET_MASK    = 130,
    DIAG_CMD_SAMSUNG_IND       = 217, /* Unsolicited message seen on Samsung Z810 */
};

/* Subsystem IDs used with DIAG_CMD_SUBSYS; these often obsolete many of
 * the original DM commands.
 */
enum {
    DIAG_SUBSYS_WCDMA           = 4,
    DIAG_SUBSYS_HDR             = 5,  /* High Data Rate (ie, EVDO) */
    DIAG_SUBSYS_GSM             = 8,
    DIAG_SUBSYS_UMTS            = 9,
    DIAG_SUBSYS_OS              = 12,
    DIAG_SUBSYS_GPS             = 13,
    DIAG_SUBSYS_SMS             = 14, /* Wireless Messaging Service */
    DIAG_SUBSYS_CM              = 15, /* Call manager */
    DIAG_SUBSYS_FS              = 19, /* File System (EFS2) */
    DIAG_SUBSYS_NOVATEL_6500    = 50, /* for Novatel Wireless MSM6500-based devices */
    DIAG_SUBSYS_LTE             = 68,
    DIAG_SUBSYS_ZTE             = 101, /* for ZTE EVDO devices */
    DIAG_SUBSYS_NOVATEL_6800    = 250 /* for Novatel Wireless MSM6800-based devices */
};

/* WCDMA subsystem command codes */
enum {
    DIAG_SUBSYS_WCDMA_CALL_START  = 12, /* Starts a call */
    DIAG_SUBSYS_WCDMA_CALL_END    = 13, /* Ends an ongoing call */
    DIAG_SUBSYS_WCDMA_STATE_INFO  = 15, /* Gets WCDMA state */
};

/* HDR subsystem command codes */
enum {
    DIAG_SUBSYS_HDR_STATE_INFO  = 8, /* Gets EVDO state */
};

/* GSM subsystem command codes */
enum {
    DIAG_SUBSYS_GSM_STATE_INFO  = 1, /* Gets GSM state */
};

/* CM subsystem command codes */
enum {
    DIAG_SUBSYS_CM_STATE_INFO = 0, /* Gets Call Manager state */
};

/* NOVATEL subsystem command codes (only for Novatel Wireless devices) */
enum {
    DIAG_SUBSYS_NOVATEL_AT_REQUEST     = 3, /* AT commands via diag */
    DIAG_SUBSYS_NOVATEL_AT_RESPONSE    = 4,
    DIAG_SUBSYS_NOVATEL_MODEM_SNAPSHOT = 7,
    DIAG_SUBSYS_NOVATEL_ERI            = 8, /* Extended Roaming Indicator */
    DIAG_SUBSYS_NOVATEL_PRL            = 12,
};

enum {
    DIAG_SUBSYS_NOVATEL_MODEM_SNAPSHOT_TECH_CDMA_EVDO = 7,
    DIAG_SUBSYS_NOVATEL_MODEM_SNAPSHOT_TECH_WCDMA     = 20,
};

enum {
    DIAG_SUBSYS_ZTE_STATUS = 0,
};

enum {
    CDMA_PREV_UNKNOWN       = 0,
    CDMA_PREV_IS_95         = 1, /* and J_STD008 */
    CDMA_PREV_IS_95A        = 2,
    CDMA_PREV_IS_95A_TSB74  = 3,
    CDMA_PREV_IS_95B_PHASE1 = 4,
    CDMA_PREV_IS_95B_PHASE2 = 5,
    CDMA_PREV_IS2000_REL0   = 6,
    CDMA_PREV_IS2000_RELA   = 7
};

enum {
    CDMA_BAND_CLASS_0_CELLULAR_800   = 0,  /* 800 MHz cellular band */
    CDMA_BAND_CLASS_1_PCS            = 1,  /* 1800 to 2000 MHz PCS band */
    CDMA_BAND_CLASS_2_TACS           = 2,  /* 872 to 960 MHz TACS band */
    CDMA_BAND_CLASS_3_JTACS          = 3,  /* 832 to 925 MHz JTACS band */
    CDMA_BAND_CLASS_4_KOREAN_PCS     = 4,  /* 1750 to 1870 MHz Korean PCS band */
    CDMA_BAND_CLASS_5_NMT450         = 5,  /* 450 MHz NMT band */
    CDMA_BAND_CLASS_6_IMT2000        = 6,  /* 2100 MHz IMT-2000 band */
    CDMA_BAND_CLASS_7_CELLULAR_700   = 7,  /* Upper 700 MHz band */
    CDMA_BAND_CLASS_8_1800           = 8,  /* 1800 MHz band */
    CDMA_BAND_CLASS_9_900            = 9,  /* 900 MHz band */
    CDMA_BAND_CLASS_10_SECONDARY_800 = 10, /* Secondary 800 MHz band */
    CDMA_BAND_CLASS_11_PAMR_400      = 11, /* 400 MHz European PAMR band */
    CDMA_BAND_CLASS_12_PAMR_800      = 12, /* 800 MHz PAMR band */
    CDMA_BAND_CLASS_13_IMT2000_2500  = 13, /* 2500 MHz IMT-2000 Extension Band */
    CDMA_BAND_CLASS_14_US_PCS_1900   = 14, /* US PCS 1900 MHz Band */
    CDMA_BAND_CLASS_15_AWS           = 15, /* AWS 1700 MHz band */
    CDMA_BAND_CLASS_16_US_2500       = 16, /* US 2500 MHz Band */
    CDMA_BAND_CLASS_17_US_FLO_2500   = 17, /* US 2500 MHz Forward Link Only Band */
    CDMA_BAND_CLASS_18_US_PS_700     = 18, /* 700 MHz Public Safety Band */
    CDMA_BAND_CLASS_19_US_LOWER_700  = 19  /* Lower 700 MHz Band */
};

enum {
    CDMA_STATUS_SNAPSHOT_STATE_NO_SERVICE         = 0x00,
    CDMA_STATUS_SNAPSHOT_STATE_INITIALIZATION     = 0x01,
    CDMA_STATUS_SNAPSHOT_STATE_IDLE               = 0x02,
    CDMA_STATUS_SNAPSHOT_STATE_VOICE_CHANNEL_INIT = 0x03,
    CDMA_STATUS_SNAPSHOT_STATE_WAITING_FOR_ORDER  = 0x04,
    CDMA_STATUS_SNAPSHOT_STATE_WAITING_FOR_ANSWER = 0x05,
    CDMA_STATUS_SNAPSHOT_STATE_CONVERSATION       = 0x06,
    CDMA_STATUS_SNAPSHOT_STATE_RELEASE            = 0x07,
    CDMA_STATUS_SNAPSHOT_STATE_SYSTEM_ACCESS      = 0x08,
    CDMA_STATUS_SNAPSHOT_STATE_OFFLINE_CDMA       = 0x10,
    CDMA_STATUS_SNAPSHOT_STATE_OFFLINE_HDR        = 0x11,
    CDMA_STATUS_SNAPSHOT_STATE_OFFLINE_ANALOG     = 0x12,
    CDMA_STATUS_SNAPSHOT_STATE_RESET              = 0x13,
    CDMA_STATUS_SNAPSHOT_STATE_POWER_DOWN         = 0x14,
    CDMA_STATUS_SNAPSHOT_STATE_POWER_SAVE         = 0x15,
    CDMA_STATUS_SNAPSHOT_STATE_POWER_UP           = 0x16,
    CDMA_STATUS_SNAPSHOT_STATE_LOW_POWER_MODE     = 0x17,
    CDMA_STATUS_SNAPSHOT_STATE_SEARCHER_DSMM      = 0x18, /* Dedicated System Measurement Mode */
    CDMA_STATUS_SNAPSHOT_STATE_HDR                = 0x40,
};

/* Generic DM command header */
struct DMCmdHeader {
    uint8_t code;
} __attribute__ ((packed));
typedef struct DMCmdHeader DMCmdHeader;

/* DIAG_CMD_SUBSYS */
struct DMCmdSubsysHeader {
    uint8_t code;
    uint8_t subsys_id;
    uint16_t subsys_cmd;
} __attribute__ ((packed));
typedef struct DMCmdSubsysHeader DMCmdSubsysHeader;

typedef enum {
    DM_CONTROL_MODE_OFFLINE = 1,
    DM_CONTROL_MODE_RESET = 2,
} DMControlMode;

/* DIAG_CMD_CONTROL */
struct DMCmdControl {
    uint8_t code;
    /* DMControlMode */
    uint16_t mode;
} __attribute__ ((packed));
typedef struct DMCmdControl DMCmdControl;

/* DIAG_CMD_NV_READ / DIAG_CMD_NV_WRITE */
struct DMCmdNVReadWrite {
    uint8_t code;
    uint16_t nv_item;
    uint8_t data[128];
    uint16_t status;
} __attribute__ ((packed));
typedef struct DMCmdNVReadWrite DMCmdNVReadWrite;

/* DIAG_CMD_VERSION_INFO */
struct DMCmdVersionInfoRsp {
    uint8_t code;
    char comp_date[11];
    char comp_time[8];
    char rel_date[11];
    char rel_time[8];
    char model[8];
    uint8_t scm;
    uint8_t mob_cai_rev;
    uint8_t mob_model;
    uint16_t mob_firmware_rev;
    uint8_t slot_cycle_index;
    uint8_t msm_ver;
    uint8_t _unknown;
} __attribute__ ((packed));
typedef struct DMCmdVersionInfoRsp DMCmdVersionInfoRsp;

/* DIAG_CMD_ESN */
struct DMCmdEsnRsp {
    uint8_t code;
    uint8_t esn[4];
} __attribute__ ((packed));
typedef struct DMCmdEsnRsp DMCmdEsnRsp;

/* DIAG_CMD_STATUS */
struct DMCmdStatusRsp {
    uint8_t code;
    uint8_t _unknown[3];
    uint8_t esn[4];
    uint16_t rf_mode;
    uint8_t min1_analog[4];
    uint8_t min1_cdma[4];
    uint8_t min2_analog[2];
    uint8_t min2_cdma[2];
    uint8_t _unknown1;
    uint16_t cdma_rx_state;
    uint8_t good_frames;
    uint16_t analog_corrected_frames;
    uint16_t analog_bad_frames;
    uint16_t analog_word_syncs;
    uint16_t entry_reason;
    uint16_t curr_chan;
    uint8_t cdma_code_chan;
    uint16_t pilot_base;
    uint16_t sid;
    uint16_t nid;
    uint16_t analog_locaid;
    uint16_t analog_rssi;
    uint8_t analog_power;
} __attribute__ ((packed));
typedef struct DMCmdStatusRsp DMCmdStatusRsp;

/* DIAG_CMD_SW_VERSION */
struct DMCmdSwVersionRsp {
    uint8_t code;
    char version[31];
    char comp_date[11];
    uint8_t _unknown1[2];
    char comp_time[8];
    uint8_t _unknown2[2];
} __attribute__ ((packed));
typedef struct DMCmdSwVersionRsp DMCmdSwVersionRsp;

typedef enum {
    DM_OPER_MODE_POWER_OFF = 0,
    DM_OPER_MODE_FIELD_TEST_MODE = 1,
    DM_OPER_MODE_OFFLINE = 2,
    DM_OPER_MODE_OFFLINE_AMPS = 3,
    DM_OPER_MODE_OFFLINE_CDMA = 4,
    DM_OPER_MODE_ONLINE = 5,
    DM_OPER_MODE_LOW_POWER_MODE = 6,
    DM_OPER_MODE_RESETTING = 7,
} DMOperMode;

/* DIAG_CMD_STATUS_SNAPSHOT */
struct DMCmdStatusSnapshotRsp {
    uint8_t code;
    uint8_t esn[4];
    uint8_t imsi_s1[4];
    uint8_t imsi_s2[2];
    uint8_t imsi_s[8];
    uint8_t imsi_11_12;
    uint16_t mcc;
    uint8_t imsi_addr_num;
    uint16_t sid;
    uint16_t nid;
    uint8_t prev;
    uint8_t prev_in_use;
    uint8_t mob_prev;
    uint8_t band_class;
    uint16_t frequency;
    uint8_t oper_mode;
    uint8_t state;
    uint8_t sub_state;
} __attribute__ ((packed));
typedef struct DMCmdStatusSnapshotRsp DMCmdStatusSnapshotRsp;

/* DIAG_SUBSYS_CM_STATE_INFO subsys command */
struct DMCmdSubsysCMStateInfoRsp {
    DMCmdSubsysHeader header;
    uint32_t call_state;
    uint32_t oper_mode;
    uint32_t system_mode;
    uint32_t mode_pref;
    uint32_t band_pref;
    uint32_t roam_pref;
    uint32_t srv_domain_pref;
    uint32_t acq_order_pref;
    uint32_t hybrid_pref;
    uint32_t network_sel_mode_pref;
} __attribute__ ((packed));
typedef struct DMCmdSubsysCMStateInfoRsp DMCmdSubsysCMStateInfoRsp;

/* DIAG_SUBSYS_HDR_STATE_INFO subsys command */
struct DMCmdSubsysHDRStateInfoRsp {
    DMCmdSubsysHeader header;
    uint8_t at_state;
    uint8_t session_state;
    uint8_t almp_state;
    uint8_t init_state;
    uint8_t idle_state;
    uint8_t connected_state;
    uint8_t route_update_state;
    uint8_t overhead_msg_state;
    uint8_t hdr_hybrid_mode;
} __attribute__ ((packed));
typedef struct DMCmdSubsysHDRStateInfoRsp DMCmdSubsysHDRStateInfoRsp;


/* DIAG_SUBSYS_ZTE_STATUS subsys command */
struct DMCmdSubsysZteStatusRsp {
    DMCmdSubsysHeader header;
    uint8_t _unknown1[8];
    uint8_t signal_ind;
    uint8_t _unknown2;
} __attribute__ ((packed));
typedef struct DMCmdSubsysZteStatusRsp DMCmdSubsysZteStatusRsp;

/* DIAG_CMD_PILOT_SETS command */
struct DMCmdPilotSetsSet {
    uint16_t pn_offset;
    uint16_t ecio;
} __attribute__ ((packed));
typedef struct DMCmdPilotSetsSet DMCmdPilotSetsSet;

struct DMCmdPilotSetsRsp {
    uint8_t code;
    uint16_t pilot_inc;
    uint8_t active_count;
    uint8_t candidate_count;
    uint8_t neighbor_count;
    DMCmdPilotSetsSet sets[52];
} __attribute__ ((packed));
typedef struct DMCmdPilotSetsRsp DMCmdPilotSetsRsp;

struct DMCmdLog {
    uint8_t code;
    uint8_t more;
    uint16_t len;       /* size of packet after this member */
    uint16_t _unknown2; /* contains same value as len */
    uint16_t log_code;
    uint64_t timestamp;
    uint8_t data[0];
} __attribute__ ((packed));
typedef struct DMCmdLog DMCmdLog;

struct DMCmdExtLogMask {
    uint8_t code;
    /* Bit number of highest '1' in 'mask'; set to 0 to get current mask. */
    uint16_t len;
    /* Bitfield of log messages to receive */
    uint8_t mask[512];
} __attribute__ ((packed));
typedef struct DMCmdExtLogMask DMCmdExtLogMask;

struct DMCmdEventReport {
    uint8_t code;
    uint8_t on;
} __attribute__ ((packed));
typedef struct DMCmdEventReport DMCmdEventReport;

struct DMCmdEventReportRsp {
    uint8_t code;
    uint16_t len;
    uint16_t event_id;
    uint8_t data[0];
} __attribute__ ((packed));
typedef struct DMCmdEventReportRsp DMCmdEventReportRsp;

/* DIAG_SUBSYS_NOVATEL_* subsys command */
struct DMCmdSubsysNwSnapshotReq {
    DMCmdSubsysHeader hdr;
    uint8_t technology;        /* DIAG_SUBSYS_NOVATEL_MODEM_SNAPSHOT_TECH_* */
    uint32_t snapshot_mask;
} __attribute__ ((packed));
typedef struct DMCmdSubsysNwSnapshotReq DMCmdSubsysNwSnapshotReq;

/* DIAG_SUBSYS_NOVATEL_MODEM_SNAPSHOT response */
struct DMCmdSubsysNwSnapshotRsp {
    DMCmdSubsysHeader hdr;
    uint8_t response_code;
    uint32_t bitfield1;
    uint32_t bitfield2;
    uint8_t data[100];     /* DMCmdSubsysNwSnapshotCdma */
} __attribute__ ((packed));
typedef struct DMCmdSubsysNwSnapshotRsp DMCmdSubsysNwSnapshotRsp;

struct DMCmdSubsysNwSnapshotCdma {
    uint32_t rssi;
    uint32_t battery_level;
    uint8_t call_info;
    uint8_t new_sms_ind;
    uint8_t missed_calls;
    uint32_t voicemail_ind;
    uint8_t pkt_call_ctrl_state;
    uint8_t mip_rrp_err_code;
    uint8_t cur_packet_zone_id;
    uint8_t prev;
    uint8_t band_class;
    uint8_t eri;
    uint8_t eri_alert_id;
    uint32_t cur_call_total_time;
    uint32_t cur_call_active_time;
    uint32_t cur_call_tx_ip_bytes;
    uint32_t cur_call_rx_ip_bytes;
    uint8_t connection_status;
    uint16_t dominant_pn;
    uint8_t wdisable_mask;
    uint8_t hdr_rev;
} __attribute__ ((packed));
typedef struct DMCmdSubsysNwSnapshotCdma DMCmdSubsysNwSnapshotCdma;

/* DIAG_SUBSYS_NOVATEL_MODEM_SNAPSHOT response */
struct DMCmdSubsysNwEriRsp {
    DMCmdSubsysHeader hdr;
    uint8_t status;
    uint16_t error;
    uint8_t roam;
    uint8_t eri_header[6];
    uint8_t eri_call_prompt[38];

    /* Roaming Indicator */
    uint8_t indicator_id;
    uint8_t icon_id;
    uint8_t icon_mode;
    uint8_t call_prompt_id;  /* Call Guard? */
    uint8_t alert_id;        /* Ringer? */
    uint8_t encoding_type;
    uint8_t text_len;
    uint8_t text[32];
} __attribute__ ((packed));
typedef struct DMCmdSubsysNwEriRsp DMCmdSubsysNwEriRsp;

enum {
    DIAG_CMD_LOG_CONFIG_OP_GET_RANGE = 0x01,
    DIAG_CMD_LOG_CONFIG_OP_SET_MASK = 0x03,
    DIAG_CMD_LOG_CONFIG_OP_GET_MASK = 0x04,
};

struct DMCmdLogConfig {
    uint8_t code;
    uint8_t pad[3];
    uint32_t op;
    uint32_t equipid;
    uint32_t num_items;
    uint8_t mask[0];
} __attribute__ ((packed));
typedef struct DMCmdLogConfig DMCmdLogConfig;

struct DMCmdLogConfigRsp {
    uint8_t code;
    uint8_t pad[3];
    uint32_t op;
    uint32_t result;  /* 0 = success */
    uint32_t equipid;
    union {
        uint32_t get_range_items[16];
        struct {
            uint32_t num_items;
            uint8_t mask[0];
        } get_set_items;
    } u;
} __attribute__ ((packed));
typedef struct DMCmdLogConfigRsp DMCmdLogConfigRsp;

/* DIAG_SUBSYS_WCDMA_CALL_START command */
struct DMCmdSubsysWcdmaCallStart {
    DMCmdSubsysHeader hdr;
    uint8_t number_len;
    uint8_t number_digits[32];
    uint8_t amr_rate;  /* default to 7 */
} __attribute__ ((packed));
typedef struct DMCmdSubsysWcdmaCallStart DMCmdSubsysWcdmaCallStart;

/* DIAG_SUBSYS_WCDMA_STATE_INFO response */
struct DMCmdSubsysWcdmaStateInfoRsp {
    DMCmdSubsysHeader hdr;
    uint8_t imei_len;
    uint8_t imei[8];
    uint8_t imsi_len;
    uint8_t imsi[8];
    uint8_t l1_state;
} __attribute__ ((packed));
typedef struct DMCmdSubsysWcdmaStateInfoRsp DMCmdSubsysWcdmaStateInfoRsp;

/* DIAG_SUBSYS_GSM_STATE_INFO response */
struct DMCmdSubsysGsmStateInfoRsp {
    DMCmdSubsysHeader hdr;
    uint8_t imei_len;
    uint8_t imei[8];
    uint8_t imsi_len;
    uint8_t imsi[8];
    uint8_t lai[5];
    uint16_t cellid;
    uint8_t cm_call_state;
    uint8_t cm_opmode;
    uint8_t cm_sysmode;
} __attribute__ ((packed));
typedef struct DMCmdSubsysGsmStateInfoRsp DMCmdSubsysGsmStateInfoRsp;

/* DIAG_CMD_SAMSUNG_IND response */
struct DMCmdSamsungIndRsp {
    DMCmdHeader hdr;
    uint8_t _unknown1;  /* always zero */
    uint8_t _unknown2;  /* 0x0c */
    uint8_t  _unknown3[4];  /* always zero */
    uint8_t _unknown4;  /* 0x05 */
    uint8_t _unknown5;  /* always zero */
    uint8_t _unknown6;  /* 0x01 */
    uint8_t _unknown7;  /* always zero */
    uint8_t signal;  /* 0 - 5 */
} __attribute__ ((packed));
typedef struct DMCmdSamsungIndRsp DMCmdSamsungIndRsp;

#endif  /* LIBQCDM_DM_COMMANDS_H */
