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

#ifndef LIBWMC_COMMANDS_H
#define LIBWMC_COMMANDS_H

#include "result.h"

/**********************************************************************/

/* Generic enums */

/**********************************************************************/

size_t      wmc_cmd_init_new    (char *buf, size_t buflen, int wmc2);

WmcResult * wmc_cmd_init_result (const char *buf, size_t len, int wmc2);

/**********************************************************************/

#define WMC_CMD_DEVICE_INFO_ITEM_MANUFACTURER "manufacturer"
#define WMC_CMD_DEVICE_INFO_ITEM_MODEL        "model"
#define WMC_CMD_DEVICE_INFO_ITEM_FW_REVISION  "firmware-revision"
#define WMC_CMD_DEVICE_INFO_ITEM_HW_REVISION  "hardware-revision"
#define WMC_CMD_DEVICE_INFO_ITEM_CDMA_MIN     "cdma-min"
#define WMC_CMD_DEVICE_INFO_ITEM_HOME_SID     "home-sid"
#define WMC_CMD_DEVICE_INFO_ITEM_PRL_VERSION  "prl-version"
#define WMC_CMD_DEVICE_INFO_ITEM_ERI_VERSION  "eri-version"
#define WMC_CMD_DEVICE_INFO_ITEM_MEID         "meid"
#define WMC_CMD_DEVICE_INFO_ITEM_IMEI         "imei"
#define WMC_CMD_DEVICE_INFO_ITEM_ICCID        "iccid"
#define WMC_CMD_DEVICE_INFO_ITEM_MCC          "mcc"
#define WMC_CMD_DEVICE_INFO_ITEM_MNC          "mnc"

size_t      wmc_cmd_device_info_new    (char *buf, size_t buflen);

WmcResult * wmc_cmd_device_info_result (const char *buf, size_t len);

/**********************************************************************/

enum {
    WMC_NETWORK_SERVICE_NONE = 0,
    WMC_NETWORK_SERVICE_AMPS = 1,
    WMC_NETWORK_SERVICE_IS95A = 2,
    WMC_NETWORK_SERVICE_IS95B = 3,
    WMC_NETWORK_SERVICE_GSM = 4,
    WMC_NETWORK_SERVICE_GPRS = 5,
    WMC_NETWORK_SERVICE_1XRTT = 6,
    WMC_NETWORK_SERVICE_EVDO_0 = 7,
    WMC_NETWORK_SERVICE_UMTS = 8,
    WMC_NETWORK_SERVICE_EVDO_A = 9,
    WMC_NETWORK_SERVICE_EDGE = 10,
    WMC_NETWORK_SERVICE_HSDPA = 11,
    WMC_NETWORK_SERVICE_HSUPA = 12,
    WMC_NETWORK_SERVICE_HSPA = 13,
    WMC_NETWORK_SERVICE_LTE = 14,
    WMC_NETWORK_SERVICE_EVDO_A_EHRPD = 15
};

/* One of WMC_NETWORK_SERVICE_* */
#define WMC_CMD_NETWORK_INFO_ITEM_SERVICE       "service"

#define WMC_CMD_NETWORK_INFO_ITEM_2G_DBM   "2g-dbm"
#define WMC_CMD_NETWORK_INFO_ITEM_3G_DBM   "3g-dbm"
#define WMC_CMD_NETWORK_INFO_ITEM_LTE_DBM  "lte-dbm"
#define WMC_CMD_NETWORK_INFO_ITEM_OPNAME   "opname"
#define WMC_CMD_NETWORK_INFO_ITEM_MCC      "mcc"
#define WMC_CMD_NETWORK_INFO_ITEM_MNC      "mnc"

size_t      wmc_cmd_network_info_new    (char *buf, size_t buflen);

WmcResult * wmc_cmd_network_info_result (const char *buf, size_t len);

/**********************************************************************/

enum {
    WMC_NETWORK_MODE_AUTO_CDMA = 0x00,
    WMC_NETWORK_MODE_CDMA_ONLY = 0x01,
    WMC_NETWORK_MODE_EVDO_ONLY = 0x02,
    WMC_NETWORK_MODE_AUTO_GSM  = 0x0A,
    WMC_NETWORK_MODE_GPRS_ONLY = 0x0B,
    WMC_NETWORK_MODE_UMTS_ONLY = 0x0C,
    WMC_NETWORK_MODE_AUTO      = 0x14,
    WMC_NETWORK_MODE_LTE_ONLY  = 0x1E,
};

/* One of WMC_NETWORK_MODE_* */
#define WMC_CMD_GET_GLOBAL_MODE_ITEM_MODE   "mode"

size_t      wmc_cmd_get_global_mode_new    (char *buf, size_t buflen);

WmcResult * wmc_cmd_get_global_mode_result (const char *buf, size_t len);

/**********************************************************************/

size_t      wmc_cmd_set_global_mode_new    (char *buf, size_t buflen, u_int8_t mode);

WmcResult * wmc_cmd_set_global_mode_result (const char *buf, size_t len);

/**********************************************************************/

#endif  /* LIBWMC_COMMANDS_H */
