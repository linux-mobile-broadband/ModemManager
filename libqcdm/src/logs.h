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

#ifndef LIBQCDM_LOGS_H
#define LIBQCDM_LOGS_H

#include "utils.h"
#include "result.h"

/**********************************************************************/

enum {
    QCDM_LOG_ITEM_EVDO_PILOT_SETS_V2_TYPE_UNKNOWN = 0,
    QCDM_LOG_ITEM_EVDO_PILOT_SETS_V2_TYPE_ACTIVE = 1,
    QCDM_LOG_ITEM_EVDO_PILOT_SETS_V2_TYPE_CANDIDATE = 2,
    QCDM_LOG_ITEM_EVDO_PILOT_SETS_V2_TYPE_REMAINING = 3,
};

QcdmResult *qcdm_log_item_evdo_pilot_sets_v2_new       (const char *buf,
                                                        size_t len,
                                                        int *out_error);

qcdmbool    qcdm_log_item_evdo_pilot_sets_v2_get_num   (QcdmResult *result,
                                                        uint32_t set_type,
                                                        uint32_t *out_num);

qcdmbool    qcdm_log_item_evdo_pilot_sets_v2_get_pilot (QcdmResult *result,
                                                        uint32_t set_type,
                                                        uint32_t num,
                                                        uint32_t *out_pilot_pn,
                                                        uint32_t *out_pilot_energy,
                                                        int32_t *out_rssi_dbm);

/**********************************************************************/

#endif  /* LIBQCDM_LOGS_H */
