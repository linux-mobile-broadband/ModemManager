/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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

#ifndef LIBWMC_PROTOCOL_H
#define LIBWMC_PROTOCOL_H

#define WMC_CMD_MARKER ((uint8_t) 0xC8)

enum {
    WMC_CMD_GET_GLOBAL_MODE = 0x03,
    WMC_CMD_SET_GLOBAL_MODE = 0x04,
    WMC_CMD_DEVICE_INFO = 0x06,
    WMC_CMD_CONNECTION_INFO = 0x0A,
    WMC_CMD_NET_INFO = 0x0B,
    WMC_CMD_INIT = 0x0D,
    WMC_CMD_FIELD_TEST = 0x0F,
    WMC_CMD_SET_OPERATOR = 0x33,
    WMC_CMD_GET_FIRST_OPERATOR = 0x34,
    WMC_CMD_GET_NEXT_OPERATOR = 0x35,
    WMC_CMD_GET_APN = 0x4D,
};

/* MCC/MNC representation
 *
 * Various commands accept or return an MCC/MNC.  When sending, convert
 * the MCC/MNC into a number using eg. atoi() and store it as an LE 32-bit
 * value.  3-digit MNCs appear to be sent as 3-digit only if the firmware
 * reports them as 3-digit.  For example:
 *
 * T-Mobile US: 310-260
 * mcc_mnc = 0x00007932 = 31026  (note the 2-digit-only MNC)
 *
 * AT&T: 310-410
 * mcc_mnc = 0x0004bc8a = 310410
 */


/* Generic WMC command header */
struct WmcCmdHeader {
    uint8_t marker;  /* Always 0xC8 */
    uint8_t cmd;
} __attribute__ ((packed));
typedef struct WmcCmdHeader WmcCmdHeader;

/* Used on newer devices like the UML190 and later */
struct WmcCmdInit2 {
    WmcCmdHeader hdr;
    uint16_t year;
    uint8_t  month;
    uint16_t day;        /* big endian */
    uint16_t hours;      /* big endian */
    uint16_t minutes;    /* big endian */
    uint16_t seconds;    /* big endian */
    uint8_t _unknown1[3];
} __attribute__ ((packed));
typedef struct WmcCmdInit2 WmcCmdInit2;

struct WmcCmdInit2Rsp {
    WmcCmdHeader hdr;
    uint8_t _unknown1[4];
} __attribute__ ((packed));
typedef struct WmcCmdInit2Rsp WmcCmdInit2Rsp;

struct WmcCmdDeviceInfoRsp {
    WmcCmdHeader hdr;
    uint8_t  _unknown1[27];
    char      manf[64];
    char      model[64];
    char      fwrev[64];
    char      hwrev[64];
    uint8_t  _unknown2[64];
    uint8_t  _unknown3[64];
    char      min[10];        /* CDMA2000/IS-95 MIN */
    uint8_t  _unknown4[12];
    uint16_t home_sid;
    uint8_t  _unknown5[2];
    uint16_t prlver;
    uint8_t  _unknown6[2];
    uint16_t eriver;
    uint8_t _unknown7[4];
} __attribute__ ((packed));
typedef struct WmcCmdDeviceInfoRsp WmcCmdDeviceInfoRsp;

struct WmcCmdDeviceInfo2Rsp {
    WmcCmdHeader hdr;
    uint8_t _unknown1[27];
    char     manf[64];
    char     model[64];
    char     fwrev[64];
    char     hwrev[64];
    uint8_t _unknown2[64];
    uint8_t _unknown3[64];
    uint8_t min[10];        /* CDMA2000/IS-95 MIN */
    uint8_t _unknown4[12];
    uint16_t home_sid;
    uint8_t _unknown5[2];
    uint16_t prlver;
    uint8_t _unknown6[2];
    uint16_t eriver;
    uint8_t _unknown7[4];
    uint8_t _unknown8[64];
    uint8_t meid[14];
    uint8_t _unknown10[6];  /* always zero */
    uint8_t imei[16];
    uint8_t _unknown11[6];  /* always zero */
    uint8_t _unknown12[16];
    uint8_t iccid[20];
    uint8_t _unknown13[6];
} __attribute__ ((packed));
typedef struct WmcCmdDeviceInfo2Rsp WmcCmdDeviceInfo2Rsp;

struct WmcCmdDeviceInfo3Rsp {
    WmcCmdHeader hdr;
    uint8_t _unknown1[27];
    char     manf[64];
    char     model[64];
    char     fwrev[64];
    char     hwrev[64];
    uint8_t _unknown2[64];
    uint8_t _unknown3[64];
    uint8_t min[10];        /* CDMA2000/IS-95 MIN */
    uint8_t _unknown4[12];
    uint16_t home_sid;
    uint8_t _unknown5[2];
    uint16_t prlver;
    uint8_t _unknown6[2];
    uint16_t eri_ver;
    uint8_t _unknown7[4];
    uint8_t _unknown8[64];
    uint8_t meid[14];
    uint8_t _unknown10[6];  /* always zero */
    uint8_t imei[16];
    uint8_t _unknown11[6];  /* always zero */
    uint8_t _unknown12[16];
    uint8_t iccid[20];
    uint8_t _unknown13[6];
    uint8_t mcc[16];
    uint8_t mnc[16];
    uint8_t _unknown14[4];
    uint8_t _unknown15[4];
    uint8_t _unknown16[4];
} __attribute__ ((packed));
typedef struct WmcCmdDeviceInfo3Rsp WmcCmdDeviceInfo3Rsp;

/*****************************************************/

enum {
    WMC_SERVICE_NONE = 0,
    WMC_SERVICE_AMPS = 1,
    WMC_SERVICE_IS95A = 2,
    WMC_SERVICE_IS95B = 3,
    WMC_SERVICE_GSM = 4,
    WMC_SERVICE_GPRS = 5,
    WMC_SERVICE_1XRTT = 6,
    WMC_SERVICE_EVDO_0 = 7,
    WMC_SERVICE_UMTS = 8,
    WMC_SERVICE_EVDO_A = 9,
    WMC_SERVICE_EDGE = 10,
    WMC_SERVICE_HSDPA = 11,
    WMC_SERVICE_HSUPA = 12,
    WMC_SERVICE_HSPA = 13,
    WMC_SERVICE_LTE = 14,
    WMC_SERVICE_EVDO_A_EHRPD = 15,
};

/* PC5740 response */
struct WmcCmdNetworkInfoRsp {
    WmcCmdHeader hdr;
    uint8_t  _unknown1;
    uint8_t  _unknown2[3];    /* Always zero */
    uint8_t  service;         /* One of WMC_SERVICE_* */
    uint8_t  _unknown3;       /* Either 0x00 or 0x01 */
    uint16_t ts_year;
    uint8_t  ts_month;
    uint16_t ts_day;          /* BE */
    uint16_t ts_hours;        /* BE */
    uint16_t ts_minutes;      /* BE */
    uint16_t ts_seconds;      /* BE */
    uint16_t counter1;        /* A timestamp/counter? */
    uint16_t _unknown4;
    uint8_t  _unknown5[3];    /* Always 0xFE 0xFF 0xFF */
    uint8_t  two_g_dbm;       /* 0x7D = no signal */
    uint8_t  _unknown6[37];   /* Always zero */
} __attribute__ ((packed));
typedef struct WmcCmdNetworkInfoRsp WmcCmdNetworkInfoRsp;

/* UML190 response */
struct WmcCmdNetworkInfo2Rsp {
    WmcCmdHeader hdr;
    uint8_t  _unknown1;       /* 0x00 on LTE, 0x07 or 0x1F on CDMA */
    uint8_t  _unknown2[3];    /* Always zero */
    uint8_t  service;         /* One of WMC_SERVICE_* */
    uint8_t  _unknown3;
    uint16_t ts_year;
    uint8_t  ts_month;
    uint16_t ts_day;          /* BE */
    uint16_t ts_hours;        /* BE */
    uint16_t ts_minutes;      /* BE */
    uint16_t ts_seconds;      /* BE */
    uint8_t  _unknown4;       /* always zero */
    uint16_t uptime_secs;
    uint8_t  _unknown5;
    uint8_t  _unknown6[3];    /* always zero on LTE, 0xFE 0xFF 0xFF on CDMA */
    uint8_t  two_g_dbm;       /* 0x7D = no CDMA signal, 0x6a = no GSM signal */
    uint8_t  _unknown7[3];    /* Always zero */
    uint8_t  cdma_opname[16]; /* Zero terminated? */
    uint8_t  _unknown8[18];   /* Always zero */
    uint8_t  three_g_dbm;     /* 0x7D = no signal */
    uint8_t  _unknown9[3];    /* Always zero */
    uint8_t  _unknown10;      /* 0x01 on LTE, 0x40 on CDMA */
    uint8_t  _unknown11[3];   /* Always zero */
    uint8_t  _unknown12;      /* Always 0x01 */
    uint8_t  tgpp_opname[8];  /* 3GPP operator name (Zero terminated? Sometimes "MCC MNC" too */
    uint8_t  _unknown13[4];   /* Always zero */
    uint32_t _unknown14;      /* 43 75 3a 00 on GSM/WCDMA mode, zero on others  */
    uint32_t _unknown15;      /* 49 7d 3a 00 on GSM/WCDMA mode, zero on others  */
    uint8_t  _unknown16[44];  /* Always zero */
    uint32_t mcc_mnc;         /* GSM/WCDMA only, see MCC/MNC format note */
} __attribute__ ((packed));
typedef struct WmcCmdNetworkInfo2Rsp WmcCmdNetworkInfo2Rsp;

/* UML290 response */
struct WmcCmdNetworkInfo3Rsp {
    WmcCmdHeader hdr;
    uint8_t  _unknown1;       /* 0x00 on LTE, 0x07 or 0x1F on CDMA */
    uint8_t  _unknown2[3];    /* Always zero */
    uint8_t  service;         /* One of WMC_SERVICE_* */
    uint8_t  _unknown3;
    uint16_t ts_year;
    uint8_t  ts_month;
    uint16_t ts_day;          /* BE */
    uint16_t ts_hours;        /* BE */
    uint16_t ts_minutes;      /* BE */
    uint16_t ts_seconds;      /* BE */
    uint8_t  _unknown4;       /* always zero */
    uint16_t uptime_secs;
    uint8_t  _unknown5;
    uint8_t  _unknown6[3];    /* always zero on LTE, 0xFE 0xFF 0xFF on CDMA */
    uint8_t  two_g_dbm;       /* 0x7D = no CDMA signal, 0x6a = no GSM signal */
    uint8_t  _unknown7[3];    /* Always zero */
    uint8_t  cdma_opname[16]; /* Zero terminated? */
    uint8_t  _unknown8[18];   /* Always zero */
    uint8_t  three_g_dbm;     /* 0x7D = no signal */
    uint8_t  _unknown9[3];    /* Always zero */
    uint8_t  _unknown10;      /* 0x01 on LTE, 0x40 on CDMA */
    uint8_t  _unknown11[3];   /* Always zero */
    uint8_t  _unknown12;      /* Always 0x01 */
    uint8_t  tgpp_opname[8];   /* Zero terminated? Sometimes "MCC MNC" too */
    uint8_t  _unknown13[4];   /* Always zero */
    uint32_t _unknown14;      /* 43 75 3a 00 on GSM/WCDMA mode, zero on others  */
    uint32_t _unknown15;      /* 49 7d 3a 00 on GSM/WCDMA mode, zero on others  */
    uint8_t  _unknown16[44];  /* Always zero */
    uint32_t mcc_mnc;         /* GSM/WCDMA only, see MCC/MNC format note */
    uint8_t  lte_dbm;         /* 0x00 if not in LTE mode */
    uint8_t  _unknown17[3];   /* Always zero */
    uint8_t  _unknown18[4];
} __attribute__ ((packed));
typedef struct WmcCmdNetworkInfo3Rsp WmcCmdNetworkInfo3Rsp;

/*****************************************************/

enum {
    WMC_CONNECTION_STATE_UNKNOWN = 0,
    WMC_CONNECTION_STATE_IDLE = 1,
    WMC_CONNECTION_STATE_CONNECTING = 2,
    WMC_CONNECTION_STATE_AUTHENTICATING = 3,
    WMC_CONNECTION_STATE_CONNECTED = 4,
    WMC_CONNECTION_STATE_DORMANT = 5,
    WMC_CONNECTION_STATE_UPDATING_NAM = 6,
    WMC_CONNECTION_STATE_UPDATING_PRL = 7,
    WMC_CONNECTION_STATE_DISCONNECTING = 8,
    WMC_CONNECTION_STATE_ERROR = 9,
    WMC_CONNECTION_STATE_UPDATING_UICC = 10,
    WMC_CONNECTION_STATE_UPDATING_PLMN = 11
};

/* Used on UML190 */
struct WmcCmdConnectionInfoRsp {
    WmcCmdHeader hdr;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint8_t  _unknown1[8];
    uint8_t  state;           /* One of WMC_CONNECTION_STATE_* */
    uint8_t  _unknown2[3];    /* Always 0xc0 0x0b 0x00 */
} __attribute__ ((packed));
typedef struct WmcCmdConnectionInfoRsp WmcCmdConnectionInfoRsp;

/* Used on UML290 */
struct WmcCmdConnectionInfo2Rsp {
    WmcCmdHeader hdr;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint8_t  _unknown1[8];
    uint8_t  state;           /* One of WMC_CONNECTION_STATE_* */
    uint8_t  _unknown2[3];    /* Always 0xc0 0x0b 0x00 */
    uint8_t  _unknown3[4];    /* Always 0x01 0x00 0x00 0x00 */
    uint8_t  ip4_address[16]; /* String format, ie "10.156.45.3" */
    uint8_t  _unknown4[8];    /* Netmask? */
    uint8_t  ip6_address[40]; /* String format */
} __attribute__ ((packed));
typedef struct WmcCmdConnection2InfoRsp WmcCmdConnection2InfoRsp;

/*****************************************************/

enum {
    WMC_GLOBAL_MODE_AUTO_CDMA = 0x00,
    WMC_GLOBAL_MODE_CDMA_ONLY = 0x01,
    WMC_GLOBAL_MODE_EVDO_ONLY = 0x02,
    WMC_GLOBAL_MODE_AUTO_GSM  = 0x0A,
    WMC_GLOBAL_MODE_GPRS_ONLY = 0x0B,
    WMC_GLOBAL_MODE_UMTS_ONLY = 0x0C,
    WMC_GLOBAL_MODE_AUTO      = 0x14,
    WMC_GLOBAL_MODE_LTE_ONLY  = 0x1E,
};

struct WmcCmdGetGlobalMode {
    WmcCmdHeader hdr;
    uint8_t _unknown1;  /* always 0 */
} __attribute__ ((packed));
typedef struct WmcCmdGetGlobalMode WmcCmdGetGlobalMode;

struct WmcCmdGetGlobalModeRsp {
    WmcCmdHeader hdr;
    uint8_t _unknown1;  /* always 0x01 */
    uint8_t mode;       /* one of WMC_GLOBAL_MODE_* */
    uint8_t _unknown2;  /* always 0x05 */
    uint8_t _unknown3;  /* always 0x00 */
} __attribute__ ((packed));
typedef struct WmcCmdGetGlobalModeRsp WmcCmdGetGlobalModeRsp;

/*****************************************************/

struct WmcCmdSetGlobalMode {
    WmcCmdHeader hdr;
    uint8_t _unknown1;  /* always 0x01 */
    uint8_t mode;       /* one of WMC_GLOBAL_MODE_* */
    uint8_t _unknown2;  /* always 0x05 */
    uint8_t _unknown3;  /* always 0x00 */
} __attribute__ ((packed));
typedef struct WmcCmdSetGlobalMode WmcCmdSetGlobalMode;

struct WmcCmdSetGlobalModeRsp {
    WmcCmdHeader hdr;
    uint8_t _unknown1;   /* always 0x01 */
    uint32_t _unknown2;  /* always zero */
} __attribute__ ((packed));
typedef struct WmcCmdSetGlobalModeRsp WmcCmdSetGlobalModeRsp;

/*****************************************************/

struct WmcCmdSetOperator {
    WmcCmdHeader hdr;
    uint8_t automatic;      /* 0x00 = manual, 0x01 = auto */
    uint8_t _unknown1;      /* always 0x50 */
    uint8_t _unknown2[3];   /* always zero */
    uint32_t mcc_mnc;       /* MCC/MNC for manual reg (see format note), zero for auto */
    uint8_t _unknown3[56];  /* always zero */
} __attribute__ ((packed));
typedef struct WmcCmdSetOperator WmcCmdSetOperator;

enum {
    WMC_SET_OPERATOR_STATUS_OK = 0,
    WMC_SET_OPERATOR_STATUS_REGISTERING = 0x63,
    WMC_SET_OPERATOR_STATUS_FAILED = 0x68,
};

struct WmcCmdSetOperatorRsp {
    WmcCmdHeader hdr;
    uint8_t status;        /* one of WMC_SET_OPERATOR_STATUS_* */
    uint8_t _unknown1[3];  /* always zero */
} __attribute__ ((packed));
typedef struct WmcCmdSetOperatorRsp WmcCmdSetOperatorRsp;

/*****************************************************/

enum {
    WMC_OPERATOR_SERVICE_UNKNOWN = 0,
    WMC_OPERATOR_SERVICE_GSM = 1,
    WMC_OPERATOR_SERVICE_UMTS = 2,
};

/* Response for both GET_FIRST_OPERATOR and GET_NEXT_OPERATOR */
struct WmcCmdGetOperatorRsp {
    WmcCmdHeader hdr;
    uint8_t  _unknown1;     /* Usually 0x50, sometimes 0x00 */
    uint8_t  _unknown2[3];  /* always zero */
    uint32_t mcc_mnc;       /* see format note */
    uint8_t  opname[8];
    uint8_t  _unknown3[56]; /* always zero */
    uint8_t  stat;          /* follows 3GPP TS27.007 +COPS <stat> ? */
    uint8_t  _unknown4[3];  /* always zero */
    uint8_t  service;       /* one of WMC_OPERATOR_SERVICE_* */
    uint8_t  _unknown5[3];  /* always zero */
    uint8_t  _unknown6;     /* 0x63 (GET_FIRST_OP) might mean "wait" */
    uint8_t  _unknown7;     /* 0x00 or 0x01 */
    uint8_t  _unknown8[2];  /* always zero */
} __attribute__ ((packed));
typedef struct WmcCmdGetOperatorRsp WmcCmdGetOperatorRsp;

/*****************************************************/

enum {
    WMC_FIELD_TEST_MOBILE_IP_MODE_MIP_OFF = 0,
    WMC_FIELD_TEST_MOBILE_IP_MODE_MIP_PREF = 1,
    WMC_FIELD_TEST_MOBILE_IP_MODE_MIP_ONLY = 2
};

/* Later devices return all zeros for this command */
struct WmcCmdFieldTestRsp {
    WmcCmdHeader hdr;
    uint8_t  prl_requirements;
    uint8_t  eri_support;
    char      nam_name[7];
    uint8_t  _unknown1;         /* always zero */
    uint8_t  _unknown2[3];      /* always 0x0A 0x0A 0x0A */
    uint8_t  _unknown3[5];      /* always zero */
    uint8_t  _unknown4[10];     /* all 0x0F */
    uint16_t home_sid;
    uint16_t home_nid;
    char      min1[7];
    char      min2[3];
    char      mcc[3];
    char      imsi_s[10];
    char      mnc[2];
    uint16_t primary_cdma_chan_a;
    uint16_t secondary_cdma_chan_a;
    uint16_t primary_cdma_chan_b;
    uint16_t secondary_cdma_chan_b;
    uint8_t  accolc;
    char      sw_version[64];
    char      hw_version[64];
    uint16_t prlver;
    uint16_t eriver;
    uint16_t nid;
    uint8_t  last_call_end_reason;  /* ? */
    uint8_t  rssi;
    uint16_t channel;
    uint8_t  prev;
    uint16_t pn_offset;
    uint8_t  sys_select_pref;
    uint8_t  mip_pref;
    uint8_t  hybrid_pref;
} __attribute__ ((packed));
typedef struct WmcCmdFieldTestRsp WmcCmdFieldTestRsp;

/*****************************************************/

#endif  /* LIBWMC_PROTOCOL_H */
