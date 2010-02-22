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

#include <glib.h>

#include "result.h"

/**********************************************************************/

#define QCDM_CMD_VERSION_INFO_ITEM_COMP_DATE "comp-date"
#define QCDM_CMD_VERSION_INFO_ITEM_COMP_TIME "comp-time"
#define QCDM_CMD_VERSION_INFO_ITEM_RELEASE_DATE "release-date"
#define QCDM_CMD_VERSION_INFO_ITEM_RELEASE_TIME "release-time"
#define QCDM_CMD_VERSION_INFO_ITEM_MODEL "model"

gsize       qcdm_cmd_version_info_new    (char *buf,
                                          gsize len,
                                          GError **error);

QCDMResult *qcdm_cmd_version_info_result (const char *buf,
                                          gsize len,
                                          GError **error);

/**********************************************************************/

#define QCDM_CMD_ESN_ITEM_ESN "esn"

gsize       qcdm_cmd_esn_new    (char *buf,
                                 gsize len,
                                 GError **error);

QCDMResult *qcdm_cmd_esn_result (const char *buf,
                                 gsize len,
                                 GError **error);

/**********************************************************************/

#define QCDM_CMD_CDMA_STATUS_ITEM_ESN             "esn"
#define QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE        "rx-state"
#define QCDM_CMD_CDMA_STATUS_ITEM_ENTRY_REASON    "entry-reason"
#define QCDM_CMD_CDMA_STATUS_ITEM_CURRENT_CHANNEL "current-channel"
#define QCDM_CMD_CDMA_STATUS_ITEM_CODE_CHANNEL    "code-channel"
#define QCDM_CMD_CDMA_STATUS_ITEM_PILOT_BASE      "pilot-base"
#define QCDM_CMD_CDMA_STATUS_ITEM_SID             "sid"
#define QCDM_CMD_CDMA_STATUS_ITEM_NID             "nid"

gsize       qcdm_cmd_cdma_status_new    (char *buf,
                                         gsize len,
                                         GError **error);

QCDMResult *qcdm_cmd_cdma_status_result (const char *buf,
                                         gsize len,
                                         GError **error);

/**********************************************************************/

#define QCDM_CMD_NV_GET_MDN_ITEM_PROFILE "profile"
#define QCDM_CMD_NV_GET_MDN_ITEM_MDN "mdn"

gsize       qcdm_cmd_nv_get_mdn_new    (char *buf,
                                        gsize len,
                                        guint8 profile,
                                        GError **error);

QCDMResult *qcdm_cmd_nv_get_mdn_result (const char *buf,
                                        gsize len,
                                        GError **error);

/**********************************************************************/

#endif  /* LIBQCDM_COMMANDS_H */
