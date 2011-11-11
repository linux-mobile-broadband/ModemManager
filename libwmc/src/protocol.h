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


#endif  /* LIBWMC_PROTOCOL_H */
