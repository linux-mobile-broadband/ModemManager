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
    DIAG_CMD_ERR_READ     = 42, /* Error record retreival */
    DIAG_CMD_ERR_CLEAR    = 43, /* Error record clear */
    DIAG_CMD_SER_RESET    = 44, /* Symbol error rate counter reset */
    DIAG_CMD_SER_REPORT   = 45, /* Symbol error rate counter report */
    DIAG_CMD_TEST         = 46, /* Run a specified test */
    DIAG_CMD_GET_DIPSW    = 47, /* Retreive the current DIP switch setting */
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
};

/* Subsystem IDs used with DIAG_CMD_SUBSYS; these often obsolete many of
 * the original DM commands.
 */
enum {
    DIAG_SUBSYS_HDR             = 5,  /* High Data Rate (ie, EVDO) */
    DIAG_SUBSYS_GPS             = 13,
    DIAG_SUBSYS_SMS             = 14,
    DIAG_SUBSYS_CM              = 15, /* Call manager */
    DIAG_SUBSYS_NW_CONTROL_6500 = 50, /* for Novatel Wireless MSM6500-based devices */
    DIAG_SUBSYS_ZTE             = 101, /* for ZTE EVDO devices */
    DIAG_SUBSYS_NW_CONTROL_6800 = 250 /* for Novatel Wireless MSM6800-based devices */
};

/* HDR subsystem command codes */
enum {
    DIAG_SUBSYS_HDR_STATE_INFO  = 8, /* Gets EVDO state */
};

enum {
    DIAG_SUBSYS_CM_STATE_INFO = 0, /* Gets Call Manager state */
};

/* NW_CONTROL subsystem command codes (only for Novatel Wireless devices) */
enum {
    DIAG_SUBSYS_NW_CONTROL_AT_REQUEST     = 3, /* AT commands via diag */
    DIAG_SUBSYS_NW_CONTROL_AT_RESPONSE    = 4,
    DIAG_SUBSYS_NW_CONTROL_MODEM_SNAPSHOT = 7,
    DIAG_SUBSYS_NW_CONTROL_ERI            = 8, /* Extended Roaming Indicator */
    DIAG_SUBSYS_NW_CONTROL_PRL            = 12,
};

enum {
    DIAG_SUBSYS_NW_CONTROL_MODEM_SNAPSHOT_TECH_CDMA_EVDO = 7,
    DIAG_SUBSYS_NW_CONTROL_MODEM_SNAPSHOT_TECH_WCDMA = 20,
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
    guint8 code;
} __attribute__ ((packed));
typedef struct DMCmdHeader DMCmdHeader;

/* DIAG_CMD_SUBSYS */
struct DMCmdSubsysHeader {
    guint8 code;
    guint8 subsys_id;
    guint16 subsys_cmd;
} __attribute__ ((packed));
typedef struct DMCmdSubsysHeader DMCmdSubsysHeader;

/* DIAG_CMD_NV_READ / DIAG_CMD_NV_WRITE */
struct DMCmdNVReadWrite {
    guint8 code;
    guint16 nv_item;
    guint8 data[128];
    guint16 status;
} __attribute__ ((packed));
typedef struct DMCmdNVReadWrite DMCmdNVReadWrite;

/* DIAG_CMD_VERSION_INFO */
struct DMCmdVersionInfoRsp {
    guint8 code;
    char comp_date[11];
    char comp_time[8];
    char rel_date[11];
    char rel_time[8];
    char model[8];
    guint8 scm;
    guint8 mob_cai_rev;
    guint8 mob_model;
    guint16 mob_firmware_rev;
    guint8 slot_cycle_index;
    guint8 msm_ver;
    guint8 _unknown;
} __attribute__ ((packed));
typedef struct DMCmdVersionInfoRsp DMCmdVersionInfoRsp;

/* DIAG_CMD_ESN */
struct DMCmdEsnRsp {
    guint8 code;
    guint8 esn[4];
} __attribute__ ((packed));
typedef struct DMCmdEsnRsp DMCmdEsnRsp;

/* DIAG_CMD_STATUS */
struct DMCmdStatusRsp {
    guint8 code;
    guint8 _unknown[3];
    guint8 esn[4];
    guint16 rf_mode;
    guint8 min1_analog[4];
    guint8 min1_cdma[4];
    guint8 min2_analog[2];
    guint8 min2_cdma[2];
    guint8 _unknown1;
    guint16 cdma_rx_state;
    guint8 good_frames;
    guint16 analog_corrected_frames;
    guint16 analog_bad_frames;
    guint16 analog_word_syncs;
    guint16 entry_reason;
    guint16 curr_chan;
    guint8 cdma_code_chan;
    guint16 pilot_base;
    guint16 sid;
    guint16 nid;
    guint16 analog_locaid;
    guint16 analog_rssi;
    guint8 analog_power;
} __attribute__ ((packed));
typedef struct DMCmdStatusRsp DMCmdStatusRsp;

/* DIAG_CMD_SW_VERSION */
struct DMCmdSwVersionRsp {
    guint8 code;
    char version[20];
    char comp_date[11];
    char comp_time[8];
} __attribute__ ((packed));
typedef struct DMCmdSwVersionRsp DMCmdSwVersionRsp;

/* DIAG_CMD_STATUS_SNAPSHOT */
struct DMCmdStatusSnapshotRsp {
    guint8 code;
    guint8 esn[4];
    guint8 imsi_s1[4];
    guint8 imsi_s2[2];
    guint8 imsi_s[8];
    guint8 imsi_11_12;
    guint16 mcc;
    guint8 imsi_addr_num;
    guint16 sid;
    guint16 nid;
    guint8 prev;
    guint8 prev_in_use;
    guint8 mob_prev;
    guint8 band_class;
    guint16 frequency;
    guint8 oper_mode;
    guint8 state;
    guint8 sub_state;
} __attribute__ ((packed));
typedef struct DMCmdStatusSnapshotRsp DMCmdStatusSnapshotRsp;

/* DIAG_SUBSYS_CM_STATE_INFO subsys command */
struct DMCmdSubsysCMStateInfoRsp {
    DMCmdSubsysHeader header;
    guint32 call_state;
    guint32 oper_mode;
    guint32 system_mode;
    guint32 mode_pref;
    guint32 band_pref;
    guint32 roam_pref;
    guint32 srv_domain_pref;
    guint32 acq_order_pref;
    guint32 hybrid_pref;
    guint32 network_sel_mode_pref;
} __attribute__ ((packed));
typedef struct DMCmdSubsysCMStateInfoRsp DMCmdSubsysCMStateInfoRsp;

/* DIAG_SUBSYS_HDR_STATE_INFO subsys command */
struct DMCmdSubsysHDRStateInfoRsp {
    DMCmdSubsysHeader header;
    guint8 at_state;
    guint8 session_state;
    guint8 almp_state;
    guint8 init_state;
    guint8 idle_state;
    guint8 connected_state;
    guint8 route_update_state;
    guint8 overhead_msg_state;
    guint8 hdr_hybrid_mode;
} __attribute__ ((packed));
typedef struct DMCmdSubsysHDRStateInfoRsp DMCmdSubsysHDRStateInfoRsp;


/* DIAG_SUBSYS_ZTE_STATUS subsys command */
struct DMCmdSubsysZteStatusRsp {
    DMCmdSubsysHeader header;
    guint8 _unknown1[8];
    guint8 signal_ind;
    guint8 _unknown2;
} __attribute__ ((packed));
typedef struct DMCmdSubsysZteStatusRsp DMCmdSubsysZteStatusRsp;

/* DIAG_CMD_PILOT_SETS command */
struct DMCmdPilotSetsSet {
    guint16 pn_offset;
    guint16 ecio;
} __attribute__ ((packed));
typedef struct DMCmdPilotSetsSet DMCmdPilotSetsSet;

struct DMCmdPilotSetsRsp {
    guint8 code;
    guint16 pilot_inc;
    guint8 active_count;
    guint8 candidate_count;
    guint8 neighbor_count;
    DMCmdPilotSetsSet sets[52];
} __attribute__ ((packed));
typedef struct DMCmdPilotSetsRsp DMCmdPilotSetsRsp;

struct DMCmdExtLogMask {
    guint8 code;
    /* Bit number of highest '1' in 'mask'; set to 0 to get current mask. */
    guint16 len;
    /* Bitfield of log messages to receive */
    guint8 mask[512];
} __attribute__ ((packed));
typedef struct DMCmdExtLogMask DMCmdExtLogMask;

struct DMCmdEventReport {
    guint8 code;
    guint8 on;
} __attribute__ ((packed));
typedef struct DMCmdEventReport DMCmdEventReport;

struct DMCmdEventReportRsp {
    guint8 code;
    guint16 len;
    guint16 event_id;
    guint8 data[0];
} __attribute__ ((packed));
typedef struct DMCmdEventReportRsp DMCmdEventReportRsp;

/* DIAG_SUBSYS_NW_CONTROL_* subsys command */
struct DMCmdSubsysNwSnapshotReq {
    DMCmdSubsysHeader hdr;
    guint8 technology;        /* DIAG_SUBSYS_NW_CONTROL_MODEM_SNAPSHOT_TECH_* */
    guint32 snapshot_mask;
} __attribute__ ((packed));
typedef struct DMCmdSubsysNwSnapshotReq DMCmdSubsysNwSnapshotReq;

/* DIAG_SUBSYS_NW_CONTROL_MODEM_SNAPSHOT response */
struct DMCmdSubsysNwSnapshotRsp {
    DMCmdSubsysHeader hdr;
    guint8 response_code;
    guint32 bitfield1;
    guint32 bitfield2;
    guint8 data[100];
} __attribute__ ((packed));
typedef struct DMCmdSubsysNwSnapshotRsp DMCmdSubsysNwSnapshotRsp;

struct DMCmdSubsysNwSnapshotCdma {
    guint32 rssi;
    guint32 battery_level;
    guint8 call_info;
    guint8 new_sms_ind;
    guint8 missed_calls;
    guint32 voicemail_ind;
    guint8 pkt_call_ctrl_state;
    guint8 mip_rrp_err_code;
    guint8 cur_packet_zone_id;
    guint8 prev;
    guint8 band_class;
    guint8 eri;
    guint8 eri_alert_id;
    guint32 cur_call_total_time;
    guint32 cur_call_active_time;
    guint32 cur_call_tx_ip_bytes;
    guint32 cur_call_rx_ip_bytes;
    guint8 connection_status;
    guint16 dominant_pn;
    guint8 wdisable_mask;
    guint8 hdr_rev;
} __attribute__ ((packed));
typedef struct DMCmdSubsysNwSnapshotCdma DMCmdSubsysNwSnapshotCdma;

#endif  /* LIBQCDM_DM_COMMANDS_H */

