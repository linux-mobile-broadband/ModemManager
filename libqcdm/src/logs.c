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

#include <string.h>
#include <stdlib.h>
#include <endian.h>

#include "log-items.h"
#include "logs.h"
#include "errors.h"
#include "dm-commands.h"
#include "result-private.h"
#include "utils.h"


/**********************************************************************/

static qcdmbool
check_log_item (const char *buf, size_t len, uint16_t log_code, size_t min_len, int *out_error)
{
    DMCmdLog *log_cmd = (DMCmdLog *) buf;

    if (len < sizeof (DMCmdLog)) {
        qcdm_err (0, "DM log item malformed (must be at least %zu bytes in length)", sizeof (DMCmdLog));
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_MALFORMED;
        return FALSE;
    }

    if (buf[0] != DIAG_CMD_LOG) {
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_UNEXPECTED;
        return FALSE;
    }

    if (le16toh (log_cmd->log_code) != log_code) {
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_UNEXPECTED;
        return FALSE;
    }

    if (len < sizeof (DMCmdLog) + min_len) {
        qcdm_err (0, "DM log item response not long enough (got %zu, expected "
                  "at least %zu).", len, sizeof (DMCmdLog) + min_len);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_BAD_LENGTH;
        return FALSE;
    }

    return TRUE;
}

/**********************************************************************/

#define PILOT_SETS_LOG_ACTIVE_SET    "active-set"
#define PILOT_SETS_LOG_CANDIDATE_SET "candidate-set"
#define PILOT_SETS_LOG_REMAINING_SET  "remaining-set"

static const char *
set_num_to_str (uint32_t num)
{
    if (num == QCDM_LOG_ITEM_EVDO_PILOT_SETS_V2_TYPE_ACTIVE)
        return PILOT_SETS_LOG_ACTIVE_SET;
    if (num == QCDM_LOG_ITEM_EVDO_PILOT_SETS_V2_TYPE_CANDIDATE)
        return PILOT_SETS_LOG_CANDIDATE_SET;
    if (num == QCDM_LOG_ITEM_EVDO_PILOT_SETS_V2_TYPE_REMAINING)
        return PILOT_SETS_LOG_REMAINING_SET;
    return NULL;
}

QcdmResult *
qcdm_log_item_evdo_pilot_sets_v2_new (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMLogItemEvdoPilotSetsV2 *pilot_sets;
    DMCmdLog *log_cmd = (DMCmdLog *) buf;
    size_t sets_len;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_log_item (buf, len, DM_LOG_ITEM_EVDO_PILOT_SETS_V2, sizeof (DMLogItemEvdoPilotSetsV2), out_error))
        return NULL;

    pilot_sets = (DMLogItemEvdoPilotSetsV2 *) log_cmd->data;

    result = qcdm_result_new ();

    sets_len = pilot_sets->active_count * sizeof (DMLogItemEvdoPilotSetsV2Pilot);
    if (sets_len > 0) {
        qcdm_result_add_u8_array (result,
                                  PILOT_SETS_LOG_ACTIVE_SET,
                                  (const uint8_t *) &pilot_sets->sets[0],
                                  sets_len);
    }

    sets_len = pilot_sets->candidate_count * sizeof (DMLogItemEvdoPilotSetsV2Pilot);
    if (sets_len > 0) {
        qcdm_result_add_u8_array (result,
                                  PILOT_SETS_LOG_CANDIDATE_SET,
                                  (const uint8_t *) &pilot_sets->sets[pilot_sets->active_count],
                                  sets_len);
    }

    sets_len = pilot_sets->remaining_count * sizeof (DMLogItemEvdoPilotSetsV2Pilot);
    if (sets_len > 0) {
        qcdm_result_add_u8_array (result,
                                  PILOT_SETS_LOG_REMAINING_SET,
                                  (const uint8_t *) &pilot_sets->sets[pilot_sets->active_count + pilot_sets->candidate_count],
                                  sets_len);
    }

    return result;

}

qcdmbool
qcdm_log_item_evdo_pilot_sets_v2_get_num (QcdmResult *result,
                                          uint32_t set_type,
                                          uint32_t *out_num)
{
    const char *set_name;
    const uint8_t *array = NULL;
    size_t array_len = 0;

    qcdm_return_val_if_fail (result != NULL, FALSE);

    set_name = set_num_to_str (set_type);
    qcdm_return_val_if_fail (set_name != NULL, FALSE);

    if (qcdm_result_get_u8_array (result, set_name, &array, &array_len))
        return FALSE;

    *out_num = array_len / sizeof (DMLogItemEvdoPilotSetsV2Pilot);
    return TRUE;
}

#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

qcdmbool
qcdm_log_item_evdo_pilot_sets_v2_get_pilot (QcdmResult *result,
                                            uint32_t set_type,
                                            uint32_t num,
                                            uint32_t *out_pilot_pn,
                                            uint32_t *out_pilot_energy,
                                            int32_t *out_rssi_dbm)
{
    const char *set_name;
    DMLogItemEvdoPilotSetsV2Pilot *pilot;
    const uint8_t *array = NULL;
    size_t array_len = 0;

    qcdm_return_val_if_fail (result != NULL, FALSE);

    set_name = set_num_to_str (set_type);
    qcdm_return_val_if_fail (set_name != NULL, FALSE);

    if (qcdm_result_get_u8_array (result, set_name, &array, &array_len))
        return FALSE;

    qcdm_return_val_if_fail (num < array_len / sizeof (DMLogItemEvdoPilotSetsV2Pilot), FALSE);

    pilot = (DMLogItemEvdoPilotSetsV2Pilot *) &array[num * sizeof (DMLogItemEvdoPilotSetsV2Pilot)];
    *out_pilot_pn = le16toh (pilot->pilot_pn);
    *out_pilot_energy = le16toh (pilot->pilot_energy);
    *out_rssi_dbm = (int32_t) (-110.0 + (MAX (le16toh (pilot->pilot_energy) - 50, 0) / 14.0));
    return TRUE;
}

/**********************************************************************/
