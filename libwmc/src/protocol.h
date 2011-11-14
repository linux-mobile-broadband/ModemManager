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

#define WMC_CMD_MARKER ((u_int8_t) 0xC8)

enum {
    WMC_CMD_DEVICE_INFO = 0x06,
    WMC_CMD_IP_INFO = 0x0A,
    WMC_CMD_STATUS = 0x0B,
    WMC_CMD_INIT = 0x0D,
    WMC_CMD_EPS_BEARER_INFO = 0x4D,
};


/* Generic WMC command header */
struct WmcCmdHeader {
    u_int8_t marker;  /* Always 0xC8 */
    u_int8_t cmd;
} __attribute__ ((packed));
typedef struct WmcCmdHeader WmcCmdHeader;

/* Used on newer devices like the UML290 */
struct WmcCmdInit2 {
    WmcCmdHeader hdr;
    u_int8_t _unknown1[14];
} __attribute__ ((packed));
typedef struct WmcCmdInit2 WmcCmdInit2;

struct WmcCmdInit2Rsp {
    WmcCmdHeader hdr;
    u_int8_t _unknown1[4];
} __attribute__ ((packed));
typedef struct WmcCmdInit2Rsp WmcCmdInit2Rsp;

struct WmcCmdDeviceInfoRsp {
    WmcCmdHeader hdr;
    u_int8_t _unknown1[27];
    char     manf[64];
    char     model[64];
    char     fwrev[64];
    char     hwrev[64];
    u_int8_t _unknown2[64];
    u_int8_t _unknown3[64];
    u_int8_t _unknown4[22];
    u_int8_t _unknown5[8];
    u_int8_t _unknown6[6];
} __attribute__ ((packed));
typedef struct WmcCmdDeviceInfoRsp WmcCmdDeviceInfoRsp;

struct WmcCmdDeviceInfo2Rsp {
    WmcCmdHeader hdr;
    u_int8_t _unknown1[27];
    char     manf[64];
    char     model[64];
    char     fwrev[64];
    char     hwrev[64];
    u_int8_t _unknown2[64];
    u_int8_t _unknown3[64];
    u_int8_t min[10];       /* CDMA2000/IS-95 MIN */
    u_int8_t _unknown4[12];
    u_int16_t home_sid;     /* ? */
    u_int8_t _unknown5[6];
    u_int16_t eri_ver;      /* ? */
    u_int8_t _unknown6[3];
    u_int8_t _unknown7[64];
    u_int8_t meid[20];
    u_int8_t imei[22];
    u_int8_t _unknown9[16];
    u_int8_t iccid[22];
    u_int8_t _unknown10[4];
    u_int8_t mcc[16];
    u_int8_t mnc[16];
    u_int8_t _unknown11[4];
    u_int8_t _unknown12[4];
    u_int8_t _unknown13[4];
} __attribute__ ((packed));
typedef struct WmcCmdDeviceInfo2Rsp WmcCmdDeviceInfo2Rsp;

/* Shorter response used by earlier devices like PC5740 */
struct WmcCmdStatusRsp {
    WmcCmdHeader hdr;
    u_int8_t  _unknown1;
    u_int8_t  _unknown2[3];    /* Always zero */
    u_int8_t  _unknown3;       /* Always 0x06 */
    u_int8_t  _unknown4;       /* Either 0x00 or 0x01 */
    u_int8_t  magic[10];
    u_int16_t counter1;        /* A timestamp/counter? */
    u_int8_t  _unknown5;
    u_int8_t  _unknown6;
    u_int8_t  _unknown7[3];    /* Always 0xFE 0xFF 0xFF */
    u_int8_t  cdma1x_dbm;
    u_int8_t  _unknown8[37];   /* Always zero */
} __attribute__ ((packed));
typedef struct WmcCmdStatusRsp WmcCmdStatusRsp;

/* Long-format response used on newer devices like the UML290 */
struct WmcCmdStatus2Rsp {
    WmcCmdHeader hdr;
    u_int8_t  _unknown1;       /* 0x00 on LTE, 0x07 or 0x1F on CDMA */
    u_int8_t  _unknown2[3];    /* Always zero */
    u_int8_t  _unknown3;       /* 0x0E on LTE, 0x0F on CDMA */
    u_int8_t  _unknown4;
    u_int8_t  magic[10];       /* Whatever was passed in WMC_CMD_INIT with some changes */
    u_int16_t counter1;        /* A timestamp/counter? */
    u_int16_t counter2;        /* Time since firmware start? */
    u_int8_t  _unknown5;       /* 0x00 on LTE, various values (0xD4, 0x5C) on CDMA */
    u_int8_t  _unknown6[3];    /* always zero on LTE, 0xFE 0xFF 0xFF on CDMA */
    u_int8_t  cdma1x_dbm;      /* 0x7D = no signal */
    u_int8_t  _unknown7[3];    /* Always zero */
    u_int8_t  cdma_opname[16]; /* Zero terminated? */
    u_int8_t  _unknown8[18];   /* Always zero */
    u_int8_t  hdr_dbm;         /* 0x7D = no signal */
    u_int8_t  _unknown9[3];   /* Always zero */
    u_int8_t  _unknown10;      /* 0x01 on LTE, 0x40 on CDMA */
    u_int8_t  _unknown11[3];   /* Always zero */
    u_int8_t  _unknown12;      /* Always 0x01 */
    u_int8_t  lte_opname[8];   /* Zero terminated? Sometimes "MCC MNC" too */
    u_int8_t  _unknown13[60];  /* Always zero */
    u_int8_t  lte_dbm;         /* 0x00 if not in LTE mode */
    u_int8_t  _unknown14[3];   /* Always zero */
    u_int8_t  _unknown15[4];
} __attribute__ ((packed));
typedef struct WmcCmdStatus2Rsp WmcCmdStatus2Rsp;

struct WmcCmdIpInfoRsp {
    WmcCmdHeader hdr;
    u_int32_t rx_bytes;
    u_int32_t tx_bytes;
    u_int8_t  _unknown3[8];
    u_int8_t  _unknown4;       /* Either 0x01, 0x02, 0x03, or 0x04 */
    u_int8_t  _unknown5[7];    /* Always 0xc0 0x0b 0x00 0x01 0x00 0x00 0x00 */
    u_int8_t  ip4_address[16]; /* String format, ie "10.156.45.3" */
    u_int8_t  _unknown6[8];    /* Netmask? */
    u_int8_t  ip6_address[40]; /* String format */
} __attribute__ ((packed));
typedef struct WmcCmdIpInfoRsp WmcCmdIpInfoRsp;

#endif  /* LIBWMC_PROTOCOL_H */
