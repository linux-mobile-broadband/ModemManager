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
#define WMC_CMD_DEVICE_INFO_ITEM_IMEI         "imei"
#define WMC_CMD_DEVICE_INFO_ITEM_ICCID        "iccid"
#define WMC_CMD_DEVICE_INFO_ITEM_MCC          "mcc"
#define WMC_CMD_DEVICE_INFO_ITEM_MNC          "mnc"

size_t      wmc_cmd_device_info_new    (char *buf, size_t buflen);

WmcResult * wmc_cmd_device_info_result (const char *buf, size_t len);

/**********************************************************************/

#define WMC_CMD_STATUS_ITEM_CDMA_DBM    "cdma-dbm"
#define WMC_CMD_STATUS_ITEM_HDR_DBM     "hdr-dbm"
#define WMC_CMD_STATUS_ITEM_LTE_DBM     "lte-dbm"
#define WMC_CMD_STATUS_ITEM_OPNAME      "opname"

size_t      wmc_cmd_status_new    (char *buf, size_t buflen);

WmcResult * wmc_cmd_status_result (const char *buf, size_t len);

/**********************************************************************/

#endif  /* LIBWMC_COMMANDS_H */
