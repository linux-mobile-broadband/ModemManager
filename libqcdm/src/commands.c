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

#include "commands.h"
#include "errors.h"
#include "dm-commands.h"
#include "nv-items.h"
#include "result-private.h"
#include "utils.h"


/**********************************************************************/

static uint8_t
cdma_prev_to_qcdm (uint8_t cdma)
{
    switch (cdma) {
    case CDMA_PREV_IS_95:
        return QCDM_CDMA_PREV_IS_95;
    case CDMA_PREV_IS_95A:
        return QCDM_CDMA_PREV_IS_95A;
    case CDMA_PREV_IS_95A_TSB74:
        return QCDM_CDMA_PREV_IS_95A_TSB74;
    case CDMA_PREV_IS_95B_PHASE1:
        return QCDM_CDMA_PREV_IS_95B_PHASE1;
    case CDMA_PREV_IS_95B_PHASE2:
        return QCDM_CDMA_PREV_IS_95B_PHASE2;
    case CDMA_PREV_IS2000_REL0:
        return QCDM_CDMA_PREV_IS2000_REL0;
    case CDMA_PREV_IS2000_RELA:
        return QCDM_CDMA_PREV_IS2000_RELA;
    default:
        break;
    }
    return QCDM_CDMA_PREV_UNKNOWN;
}

static uint8_t
cdma_band_class_to_qcdm (uint8_t cdma)
{
    switch (cdma) {
    case CDMA_BAND_CLASS_0_CELLULAR_800:
        return QCDM_CDMA_BAND_CLASS_0_CELLULAR_800;
    case CDMA_BAND_CLASS_1_PCS:
        return QCDM_CDMA_BAND_CLASS_1_PCS;
    case CDMA_BAND_CLASS_2_TACS:
        return QCDM_CDMA_BAND_CLASS_2_TACS;
    case CDMA_BAND_CLASS_3_JTACS:
        return QCDM_CDMA_BAND_CLASS_3_JTACS;
    case CDMA_BAND_CLASS_4_KOREAN_PCS:
        return QCDM_CDMA_BAND_CLASS_4_KOREAN_PCS;
    case CDMA_BAND_CLASS_5_NMT450:
        return QCDM_CDMA_BAND_CLASS_5_NMT450;
    case CDMA_BAND_CLASS_6_IMT2000:
        return QCDM_CDMA_BAND_CLASS_6_IMT2000;
    case CDMA_BAND_CLASS_7_CELLULAR_700:
        return QCDM_CDMA_BAND_CLASS_7_CELLULAR_700;
    case CDMA_BAND_CLASS_8_1800:
        return QCDM_CDMA_BAND_CLASS_8_1800;
    case CDMA_BAND_CLASS_9_900:
        return QCDM_CDMA_BAND_CLASS_9_900;
    case CDMA_BAND_CLASS_10_SECONDARY_800:
        return QCDM_CDMA_BAND_CLASS_10_SECONDARY_800;
    case CDMA_BAND_CLASS_11_PAMR_400:
        return QCDM_CDMA_BAND_CLASS_11_PAMR_400;
    case CDMA_BAND_CLASS_12_PAMR_800:
        return QCDM_CDMA_BAND_CLASS_12_PAMR_800;
    case CDMA_BAND_CLASS_13_IMT2000_2500:
        return QCDM_CDMA_BAND_CLASS_13_IMT2000_2500;
    case CDMA_BAND_CLASS_14_US_PCS_1900:
        return QCDM_CDMA_BAND_CLASS_14_US_PCS_1900;
    case CDMA_BAND_CLASS_15_AWS:
        return QCDM_CDMA_BAND_CLASS_15_AWS;
    case CDMA_BAND_CLASS_16_US_2500:
        return QCDM_CDMA_BAND_CLASS_16_US_2500;
    case CDMA_BAND_CLASS_17_US_FLO_2500:
        return QCDM_CDMA_BAND_CLASS_17_US_FLO_2500;
    case CDMA_BAND_CLASS_18_US_PS_700:
        return QCDM_CDMA_BAND_CLASS_18_US_PS_700;
    case CDMA_BAND_CLASS_19_US_LOWER_700:
        return QCDM_CDMA_BAND_CLASS_19_US_LOWER_700;
    default:
        break;
    }
    return QCDM_CDMA_BAND_CLASS_UNKNOWN;
}

static uint8_t
nv_mode_pref_from_qcdm (uint8_t qcdm)
{
    switch (qcdm) {
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_DIGITAL:
        return DIAG_NV_MODE_PREF_DIGITAL;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_DIGITAL_ONLY:
        return DIAG_NV_MODE_PREF_DIGITAL_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_ANALOG:
        return DIAG_NV_MODE_PREF_ANALOG;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_ANALOG_ONLY:
        return DIAG_NV_MODE_PREF_ANALOG_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_AUTO:
        return DIAG_NV_MODE_PREF_AUTO;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_ONLY:
        return DIAG_NV_MODE_PREF_1X_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_HDR_ONLY:
        return DIAG_NV_MODE_PREF_HDR_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GPRS_ONLY:
        return DIAG_NV_MODE_PREF_GPRS_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_UMTS_ONLY:
        return DIAG_NV_MODE_PREF_UMTS_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_ONLY:
        return DIAG_NV_MODE_PREF_GSM_UMTS_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_ONLY:
        return DIAG_NV_MODE_PREF_1X_HDR_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_LTE_ONLY:
        return DIAG_NV_MODE_PREF_LTE_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_LTE_ONLY:
        return DIAG_NV_MODE_PREF_GSM_UMTS_LTE_ONLY;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_LTE_ONLY:
        return DIAG_NV_MODE_PREF_1X_HDR_LTE_ONLY;
    default:
        break;
    }
    return DIAG_NV_MODE_PREF_AUTO;
};


/**********************************************************************/

/*
 * utils_bin2hexstr
 *
 * Convert a byte-array into a hexadecimal string.
 *
 * Code originally by Alex Larsson <alexl@redhat.com> and
 *  copyright Red Hat, Inc. under terms of the LGPL.
 *
 */
static char *
bin2hexstr (const uint8_t *bytes, int len)
{
    const char hex_digits[] = "0123456789abcdef";
    char *result;
    int i;
    size_t buflen = (len * 2) + 1;

    qcdm_return_val_if_fail (bytes != NULL, NULL);
    qcdm_return_val_if_fail (len > 0, NULL);
    qcdm_return_val_if_fail (len < 4096, NULL);   /* Arbitrary limit */

    result = calloc (1, buflen);
    if (result == NULL)
        return NULL;

    for (i = 0; i < len; i++) {
        result[2*i] = hex_digits[(bytes[i] >> 4) & 0xf];
        result[2*i+1] = hex_digits[bytes[i] & 0xf];
    }
    result[buflen - 1] = '\0';
    return result;
}

/**********************************************************************/

static qcdmbool
check_command (const char *buf, size_t len, uint8_t cmd, size_t min_len, int *out_error)
{
    if (len < 1) {
        qcdm_err (0, "DM command response malformed (must be at least 1 byte in length)");
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_MALFORMED;
        return FALSE;
    }

    switch (buf[0]) {
    case DIAG_CMD_BAD_CMD:
        qcdm_err (0, "DM command %d unknown or unimplemented by the device", cmd);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_BAD_COMMAND;
        return FALSE;
    case DIAG_CMD_BAD_PARM:
        qcdm_err (0, "DM command %d contained invalid parameter", cmd);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_BAD_PARAMETER;
        return FALSE;
    case DIAG_CMD_BAD_LEN:
        qcdm_err (0, "DM command %d was the wrong size", cmd);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_BAD_LENGTH;
        return FALSE;
    case DIAG_CMD_BAD_DEV:
        qcdm_err (0, "DM command %d was not accepted by the device", cmd);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_NOT_ACCEPTED;
        return FALSE;
    case DIAG_CMD_BAD_MODE:
        qcdm_err (0, "DM command %d not allowed in the current device mode", cmd);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_BAD_MODE;
        return FALSE;
    case DIAG_CMD_BAD_SPC_MODE:
        qcdm_err (0, "DM command %d not allowed because the Service Programming Code is locked", cmd);
        if (out_error)
            *out_error = -QCDM_ERROR_SPC_LOCKED;
        return FALSE;
    default:
        break;
    }

    if (buf[0] != cmd) {
        qcdm_err (0, "Unexpected DM command response (expected %d, got %d)", cmd, buf[0]);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_UNEXPECTED;
        return FALSE;
    }

    if (len < min_len) {
        qcdm_err (0, "DM command %d response not long enough (got %zu, expected "
                  "at least %zu).", cmd, len, min_len);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_BAD_LENGTH;
        return FALSE;
    }

    return TRUE;
}

static int
nv_status_to_qcdm_error (uint16_t status)
{
    switch (status) {
    case DIAG_NV_STATUS_OK:
        return QCDM_SUCCESS;
    case DIAG_NV_STATUS_BUSY:
        return -QCDM_ERROR_NV_ERROR_BUSY;
    case DIAG_NV_STATUS_BAD_COMMAND:
        return -QCDM_ERROR_NV_ERROR_BAD_COMMAND;
    case DIAG_NV_STATUS_MEMORY_FULL:
        return -QCDM_ERROR_NV_ERROR_MEMORY_FULL;
    case DIAG_NV_STATUS_FAILED:
        return -QCDM_ERROR_NV_ERROR_FAILED;
    case DIAG_NV_STATUS_INACTIVE:
        return -QCDM_ERROR_NV_ERROR_INACTIVE;
    case DIAG_NV_STATUS_BAD_PARAMETER:
        return -QCDM_ERROR_NV_ERROR_BAD_PARAMETER;
    case DIAG_NV_STATUS_READ_ONLY:
        return -QCDM_ERROR_NV_ERROR_READ_ONLY;
    default:
        return -QCDM_ERROR_NVCMD_FAILED;
    }
}

static qcdmbool
check_nv_cmd (DMCmdNVReadWrite *cmd, uint16_t nv_item, int *out_error)
{
    uint16_t cmd_item;

    qcdm_return_val_if_fail (cmd != NULL, FALSE);
    qcdm_return_val_if_fail ((cmd->code == DIAG_CMD_NV_READ) || (cmd->code == DIAG_CMD_NV_WRITE), FALSE);

    /* NV read/write have a status byte at the end */
    if (cmd->status != 0) {
        qcdm_err (0, "The NV operation failed (status 0x%X).", le16toh (cmd->status));
        if (out_error)
            *out_error = nv_status_to_qcdm_error (le16toh (cmd->status));
        return FALSE;
    }

    cmd_item = le16toh (cmd->nv_item);
    if (cmd_item != nv_item) {
        qcdm_err (0, "Unexpected DM NV command response (expected item %d, got "
                  "item %d)", nv_item, cmd_item);
        if (out_error)
            *out_error = -QCDM_ERROR_RESPONSE_UNEXPECTED;
        return FALSE;
    }

    return TRUE;
}

/**********************************************************************/

size_t
qcdm_cmd_version_info_new (char *buf, size_t len)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_VERSION_INFO;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_version_info_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdVersionInfoRsp *rsp = (DMCmdVersionInfoRsp *) buf;
    char tmp[12];

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_VERSION_INFO, sizeof (DMCmdVersionInfoRsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (rsp->comp_date) <= sizeof (tmp));
    memcpy (tmp, rsp->comp_date, sizeof (rsp->comp_date));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_COMP_DATE, tmp);

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (rsp->comp_time) <= sizeof (tmp));
    memcpy (tmp, rsp->comp_time, sizeof (rsp->comp_time));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_COMP_TIME, tmp);

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (rsp->rel_date) <= sizeof (tmp));
    memcpy (tmp, rsp->rel_date, sizeof (rsp->rel_date));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_RELEASE_DATE, tmp);

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (rsp->rel_time) <= sizeof (tmp));
    memcpy (tmp, rsp->rel_time, sizeof (rsp->rel_time));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_RELEASE_TIME, tmp);

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (rsp->model) <= sizeof (tmp));
    memcpy (tmp, rsp->model, sizeof (rsp->model));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_MODEL, tmp);

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_esn_new (char *buf, size_t len)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_ESN;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_esn_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdEsnRsp *rsp = (DMCmdEsnRsp *) buf;
    char *tmp;
    uint8_t swapped[4];

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_ESN, sizeof (DMCmdEsnRsp), out_error))
        return NULL;

    /* Convert the ESN from binary to a hex string; it's LE so we have to
     * swap it to get the correct ordering.
     */
    swapped[0] = rsp->esn[3];
    swapped[1] = rsp->esn[2];
    swapped[2] = rsp->esn[1];
    swapped[3] = rsp->esn[0];

    tmp = bin2hexstr (&swapped[0], sizeof (swapped));
    if (tmp != NULL) {
        result = qcdm_result_new ();
        qcdm_result_add_string (result, QCDM_CMD_ESN_ITEM_ESN, tmp);
        free (tmp);
    }

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_control_new (char *buf, size_t len, uint8_t mode)
{
    char cmdbuf[5];
    DMCmdControl *cmd = (DMCmdControl *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_CONTROL;
    cmd->mode = htole16 ((uint16_t) mode);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_control_result (const char *buf, size_t len, int *out_error)
{
    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_CONTROL, sizeof (DMCmdControl), out_error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

size_t
qcdm_cmd_cdma_status_new (char *buf, size_t len)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_STATUS;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_cdma_status_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdStatusRsp *rsp = (DMCmdStatusRsp *) buf;
    char *tmp;
    uint8_t swapped[4];
    uint32_t tmp_num;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_STATUS, sizeof (DMCmdStatusRsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    /* Convert the ESN from binary to a hex string; it's LE so we have to
     * swap it to get the correct ordering.
     */
    swapped[0] = rsp->esn[3];
    swapped[1] = rsp->esn[2];
    swapped[2] = rsp->esn[1];
    swapped[3] = rsp->esn[0];

    tmp = bin2hexstr (&swapped[0], sizeof (swapped));
    qcdm_result_add_string (result, QCDM_CMD_CDMA_STATUS_ITEM_ESN, tmp);
    free (tmp);

    tmp_num = (uint32_t) le16toh (rsp->rf_mode);
    qcdm_result_add_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RF_MODE, tmp_num);

    tmp_num = (uint32_t) le16toh (rsp->cdma_rx_state);
    qcdm_result_add_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE, tmp_num);

    tmp_num = (uint32_t) le16toh (rsp->entry_reason);
    qcdm_result_add_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_ENTRY_REASON, tmp_num);

    tmp_num = (uint32_t) le16toh (rsp->curr_chan);
    qcdm_result_add_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_CURRENT_CHANNEL, tmp_num);

    qcdm_result_add_u8 (result, QCDM_CMD_CDMA_STATUS_ITEM_CODE_CHANNEL, rsp->cdma_code_chan);

    tmp_num = (uint32_t) le16toh (rsp->pilot_base);
    qcdm_result_add_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_PILOT_BASE, tmp_num);

    tmp_num = (uint32_t) le16toh (rsp->sid);
    qcdm_result_add_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_SID, tmp_num);

    tmp_num = (uint32_t) le16toh (rsp->nid);
    qcdm_result_add_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_NID, tmp_num);

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_sw_version_new (char *buf, size_t len)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SW_VERSION;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_sw_version_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdSwVersionRsp *rsp = (DMCmdSwVersionRsp *) buf;
    char tmp[32];

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SW_VERSION, sizeof (*rsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (rsp->version) <= sizeof (tmp));
    memcpy (tmp, rsp->version, sizeof (rsp->version));
    qcdm_result_add_string (result, QCDM_CMD_SW_VERSION_ITEM_VERSION, tmp);

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (rsp->comp_date) <= sizeof (tmp));
    memcpy (tmp, rsp->comp_date, sizeof (rsp->comp_date));
    qcdm_result_add_string (result, QCDM_CMD_SW_VERSION_ITEM_COMP_DATE, tmp);

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (rsp->comp_time) <= sizeof (tmp));
    memcpy (tmp, rsp->comp_time, sizeof (rsp->comp_time));
    qcdm_result_add_string (result, QCDM_CMD_SW_VERSION_ITEM_COMP_TIME, tmp);

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_status_snapshot_new (char *buf, size_t len)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_STATUS_SNAPSHOT;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

static uint8_t
snapshot_state_to_qcdm (uint8_t cdma_state)
{
    /* CDMA_STATUS_SNAPSHOT_STATE_* -> QCDM_STATUS_SNAPSHOT_STATE_* */
    return cdma_state + 1;
}

static inline uint8_t
digit_fixup (uint8_t d)
{
    /* CDMA MCC/IMSI conversion adds 1 to each digit, and digits equal to
     * 10 are really zero.
     */
    if (d + 1 < 10)
        return d + 1;
    return 0;
}

QcdmResult *
qcdm_cmd_status_snapshot_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdStatusSnapshotRsp *rsp = (DMCmdStatusSnapshotRsp *) buf;
    char *tmp;
    uint8_t swapped[4];
    uint8_t tmcc[3];
    uint16_t mcc, hmcc;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_STATUS_SNAPSHOT, sizeof (*rsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    /* Convert the ESN from binary to a hex string; it's LE so we have to
     * swap it to get the correct ordering.
     */
    swapped[0] = rsp->esn[3];
    swapped[1] = rsp->esn[2];
    swapped[2] = rsp->esn[1];
    swapped[3] = rsp->esn[0];

    tmp = bin2hexstr (&swapped[0], sizeof (swapped));
    qcdm_result_add_string (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_ESN, tmp);
    free (tmp);

    /* Cheap binary -> decimal conversion */
    hmcc = le16toh (rsp->mcc);
    tmcc[2] = hmcc / 100;
    tmcc[1] = (hmcc - (tmcc[2] * 100)) / 10;
    tmcc[0] = (hmcc - (tmcc[2] * 100) - (tmcc[1] * 10));

    mcc = (100 * digit_fixup (tmcc[2])) + (10 * digit_fixup (tmcc[1])) + digit_fixup (tmcc[0]);
    qcdm_result_add_u32 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_HOME_MCC, mcc);

    qcdm_result_add_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_BAND_CLASS, cdma_band_class_to_qcdm (rsp->band_class));
    qcdm_result_add_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_BASE_STATION_PREV, cdma_prev_to_qcdm (rsp->prev));
    qcdm_result_add_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_MOBILE_PREV, cdma_prev_to_qcdm (rsp->mob_prev));
    qcdm_result_add_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_PREV_IN_USE, cdma_prev_to_qcdm (rsp->prev_in_use));
    qcdm_result_add_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_STATE, snapshot_state_to_qcdm (rsp->state & 0xF));

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_pilot_sets_new (char *buf, size_t len)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_PILOT_SETS;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

#define PILOT_SETS_CMD_ACTIVE_SET    "active-set"
#define PILOT_SETS_CMD_CANDIDATE_SET "candidate-set"
#define PILOT_SETS_CMD_NEIGHBOR_SET  "neighbor-set"

static const char *
set_num_to_str (uint32_t num)
{
    if (num == QCDM_CMD_PILOT_SETS_TYPE_ACTIVE)
        return PILOT_SETS_CMD_ACTIVE_SET;
    if (num == QCDM_CMD_PILOT_SETS_TYPE_CANDIDATE)
        return PILOT_SETS_CMD_CANDIDATE_SET;
    if (num == QCDM_CMD_PILOT_SETS_TYPE_NEIGHBOR)
        return PILOT_SETS_CMD_NEIGHBOR_SET;
    return NULL;
}

QcdmResult *
qcdm_cmd_pilot_sets_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdPilotSetsRsp *rsp = (DMCmdPilotSetsRsp *) buf;
    size_t sets_len;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_PILOT_SETS, sizeof (DMCmdPilotSetsRsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    sets_len = rsp->active_count * sizeof (DMCmdPilotSetsSet);
    if (sets_len > 0) {
        qcdm_result_add_u8_array (result,
                                  PILOT_SETS_CMD_ACTIVE_SET,
                                  (const uint8_t *) &rsp->sets[0],
                                  sets_len);
    }

    sets_len = rsp->candidate_count * sizeof (DMCmdPilotSetsSet);
    if (sets_len > 0) {
        qcdm_result_add_u8_array (result,
                                  PILOT_SETS_CMD_CANDIDATE_SET,
                                  (const uint8_t *) &rsp->sets[rsp->active_count],
                                  sets_len);
    }

    sets_len = rsp->neighbor_count * sizeof (DMCmdPilotSetsSet);
    if (sets_len > 0) {
        qcdm_result_add_u8_array (result,
                                  PILOT_SETS_CMD_NEIGHBOR_SET,
                                  (const uint8_t *) &rsp->sets[rsp->active_count + rsp->candidate_count],
                                  sets_len);
    }

    return result;
}

qcdmbool
qcdm_cmd_pilot_sets_result_get_num (QcdmResult *result,
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

    *out_num = array_len / sizeof (DMCmdPilotSetsSet);
    return TRUE;
}

qcdmbool
qcdm_cmd_pilot_sets_result_get_pilot (QcdmResult *result,
                                      uint32_t set_type,
                                      uint32_t num,
                                      uint32_t *out_pn_offset,
                                      uint32_t *out_ecio,
                                      float *out_db)
{
    const char *set_name;
    DMCmdPilotSetsSet *set;
    const uint8_t *array = NULL;
    size_t array_len = 0;

    qcdm_return_val_if_fail (result != NULL, FALSE);

    set_name = set_num_to_str (set_type);
    qcdm_return_val_if_fail (set_name != NULL, FALSE);

    if (qcdm_result_get_u8_array (result, set_name, &array, &array_len))
        return FALSE;

    qcdm_return_val_if_fail (num < array_len / sizeof (DMCmdPilotSetsSet), FALSE);

    set = (DMCmdPilotSetsSet *) &array[num * sizeof (DMCmdPilotSetsSet)];
    *out_pn_offset = set->pn_offset;
    *out_ecio = set->ecio;
    /* EC/IO is in units of -0.5 dB per the specs */
    *out_db = (float) (set->ecio * -0.5);
    return TRUE;
}

/**********************************************************************/

size_t
qcdm_cmd_nv_get_mdn_new (char *buf, size_t len, uint8_t profile)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemMdn *req;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = htole16 (DIAG_NV_DIR_NUMBER);

    req = (DMNVItemMdn *) &cmd->data[0];
    req->profile = profile;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_get_mdn_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemMdn *mdn;
    char tmp[11];

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_DIR_NUMBER, out_error))
        return NULL;

    mdn = (DMNVItemMdn *) &rsp->data[0];

    result = qcdm_result_new ();

    qcdm_result_add_u8 (result, QCDM_CMD_NV_GET_MDN_ITEM_PROFILE, mdn->profile);

    memset (tmp, 0, sizeof (tmp));
    qcdm_assert (sizeof (mdn->mdn) <= sizeof (tmp));
    memcpy (tmp, mdn->mdn, sizeof (mdn->mdn));
    qcdm_result_add_string (result, QCDM_CMD_NV_GET_MDN_ITEM_MDN, tmp);

    return result;
}

/**********************************************************************/

static qcdmbool
roam_pref_validate (uint8_t dm)
{
    if (   dm == DIAG_NV_ROAM_PREF_HOME_ONLY
        || dm == DIAG_NV_ROAM_PREF_ROAM_ONLY
        || dm == DIAG_NV_ROAM_PREF_AUTO)
        return TRUE;
    return FALSE;
}

size_t
qcdm_cmd_nv_get_roam_pref_new (char *buf, size_t len, uint8_t profile)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemRoamPref *req;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = htole16 (DIAG_NV_ROAM_PREF);

    req = (DMNVItemRoamPref *) &cmd->data[0];
    req->profile = profile;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_get_roam_pref_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemRoamPref *roam;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_ROAM_PREF, out_error))
        return NULL;

    roam = (DMNVItemRoamPref *) &rsp->data[0];

    if (!roam_pref_validate (roam->roam_pref)) {
        qcdm_err (0, "Unknown roam preference 0x%X", roam->roam_pref);
        return NULL;
    }

    result = qcdm_result_new ();
    qcdm_result_add_u8 (result, QCDM_CMD_NV_GET_ROAM_PREF_ITEM_PROFILE, roam->profile);
    qcdm_result_add_u8 (result, QCDM_CMD_NV_GET_ROAM_PREF_ITEM_ROAM_PREF, roam->roam_pref);

    return result;
}

size_t
qcdm_cmd_nv_set_roam_pref_new (char *buf,
                               size_t len,
                               uint8_t profile,
                               uint8_t roam_pref)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemRoamPref *req;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    if (!roam_pref_validate (roam_pref)) {
        qcdm_err (0, "Invalid roam preference %d", roam_pref);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_WRITE;
    cmd->nv_item = htole16 (DIAG_NV_ROAM_PREF);

    req = (DMNVItemRoamPref *) &cmd->data[0];
    req->profile = profile;
    req->roam_pref = roam_pref;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_set_roam_pref_result (const char *buf, size_t len, int *out_error)
{
    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_WRITE, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd ((DMCmdNVReadWrite *) buf, DIAG_NV_ROAM_PREF, out_error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

size_t
qcdm_cmd_nv_get_mode_pref_new (char *buf, size_t len, uint8_t profile)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemModePref *req;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = htole16 (DIAG_NV_MODE_PREF);

    req = (DMNVItemModePref *) &cmd->data[0];
    req->profile = profile;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_get_mode_pref_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemModePref *mode;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_MODE_PREF, out_error))
        return NULL;

    mode = (DMNVItemModePref *) &rsp->data[0];

    result = qcdm_result_new ();
    qcdm_result_add_u8 (result, QCDM_CMD_NV_GET_MODE_PREF_ITEM_PROFILE, mode->profile);
    qcdm_result_add_u8 (result, QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF, mode->mode_pref);

    return result;
}

size_t
qcdm_cmd_nv_set_mode_pref_new (char *buf,
                               size_t len,
                               uint8_t profile,
                               uint8_t mode_pref)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemModePref *req;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_WRITE;
    cmd->nv_item = htole16 (DIAG_NV_MODE_PREF);

    req = (DMNVItemModePref *) &cmd->data[0];
    req->profile = profile;
    req->mode_pref = nv_mode_pref_from_qcdm (mode_pref);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_set_mode_pref_result (const char *buf, size_t len, int *out_error)
{
    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_WRITE, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd ((DMCmdNVReadWrite *) buf, DIAG_NV_MODE_PREF, out_error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

size_t
qcdm_cmd_nv_get_hybrid_pref_new (char *buf, size_t len)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = htole16 (DIAG_NV_HYBRID_PREF);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_get_hybrid_pref_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemHybridPref *hybrid;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_HYBRID_PREF, out_error))
        return NULL;

    hybrid = (DMNVItemHybridPref *) &rsp->data[0];

    if (hybrid->hybrid_pref > 1)
        qcdm_warn (0, "Unknown hybrid preference 0x%X", hybrid->hybrid_pref);

    result = qcdm_result_new ();
    qcdm_result_add_u8 (result, QCDM_CMD_NV_GET_HYBRID_PREF_ITEM_HYBRID_PREF, hybrid->hybrid_pref);

    return result;
}

size_t
qcdm_cmd_nv_set_hybrid_pref_new (char *buf,
                                 size_t len,
                                 uint8_t hybrid_pref)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemHybridPref *req;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    if (hybrid_pref > QCDM_CMD_NV_HYBRID_PREF_ITEM_REV_HYBRID_ON) {
        qcdm_err (0, "Invalid hybrid preference %d", hybrid_pref);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_WRITE;
    cmd->nv_item = htole16 (DIAG_NV_HYBRID_PREF);

    req = (DMNVItemHybridPref *) &cmd->data[0];
    if (hybrid_pref == QCDM_CMD_NV_HYBRID_PREF_ITEM_REV_HYBRID_OFF)
        req->hybrid_pref = DIAG_NV_HYBRID_PREF_OFF;
    else if (hybrid_pref == QCDM_CMD_NV_HYBRID_PREF_ITEM_REV_HYBRID_ON)
        req->hybrid_pref = DIAG_NV_HYBRID_PREF_ON;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_set_hybrid_pref_result (const char *buf, size_t len, int *out_error)
{
    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_WRITE, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd ((DMCmdNVReadWrite *) buf, DIAG_NV_HYBRID_PREF, out_error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

size_t
qcdm_cmd_nv_get_ipv6_enabled_new (char *buf, size_t len)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = htole16 (DIAG_NV_IPV6_ENABLED);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_get_ipv6_enabled_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemIPv6Enabled *ipv6;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_IPV6_ENABLED, out_error))
        return NULL;

    ipv6 = (DMNVItemIPv6Enabled *) &rsp->data[0];

    if (ipv6->enabled > 1)
        qcdm_warn (0, "Unknown ipv6 preference 0x%X", ipv6->enabled);

    result = qcdm_result_new ();
    qcdm_result_add_u8 (result, QCDM_CMD_NV_GET_IPV6_ENABLED_ITEM_ENABLED, ipv6->enabled);

    return result;
}

size_t
qcdm_cmd_nv_set_ipv6_enabled_new (char *buf,
                                 size_t len,
                                 uint8_t enabled)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemIPv6Enabled *req;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    if (enabled > QCDM_CMD_NV_IPV6_ENABLED_ON) {
        qcdm_err (0, "Invalid ipv6 preference %d", enabled);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_WRITE;
    cmd->nv_item = htole16 (DIAG_NV_IPV6_ENABLED);

    req = (DMNVItemIPv6Enabled *) &cmd->data[0];
    if (enabled == QCDM_CMD_NV_IPV6_ENABLED_OFF)
        req->enabled = DIAG_NV_IPV6_ENABLED_OFF;
    else if (enabled == QCDM_CMD_NV_IPV6_ENABLED_ON)
        req->enabled = DIAG_NV_IPV6_ENABLED_ON;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_set_ipv6_enabled_result (const char *buf, size_t len, int *out_error)
{
    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_WRITE, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd ((DMCmdNVReadWrite *) buf, DIAG_NV_IPV6_ENABLED, out_error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

static qcdmbool
hdr_rev_pref_validate (uint8_t dm)
{
    if (   dm == DIAG_NV_HDR_REV_PREF_0
        || dm == DIAG_NV_HDR_REV_PREF_A
        || dm == DIAG_NV_HDR_REV_PREF_EHRPD)
        return TRUE;
    return FALSE;
}

size_t
qcdm_cmd_nv_get_hdr_rev_pref_new (char *buf, size_t len)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = htole16 (DIAG_NV_HDR_REV_PREF);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_get_hdr_rev_pref_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemHdrRevPref *rev;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_HDR_REV_PREF, out_error))
        return NULL;

    rev = (DMNVItemHdrRevPref *) &rsp->data[0];

    if (!hdr_rev_pref_validate (rev->rev_pref)) {
        qcdm_err (0, "Unknown HDR revision preference 0x%X", rev->rev_pref);
        return NULL;
    }

    result = qcdm_result_new ();
    qcdm_result_add_u8 (result, QCDM_CMD_NV_GET_HDR_REV_PREF_ITEM_REV_PREF, rev->rev_pref);

    return result;
}

size_t
qcdm_cmd_nv_set_hdr_rev_pref_new (char *buf,
                                  size_t len,
                                  uint8_t rev_pref)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemHdrRevPref *req;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    if (!hdr_rev_pref_validate (rev_pref)) {
        qcdm_err (0, "Invalid HDR revision preference %d", rev_pref);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_WRITE;
    cmd->nv_item = htole16 (DIAG_NV_HDR_REV_PREF);

    req = (DMNVItemHdrRevPref *) &cmd->data[0];
    req->rev_pref = rev_pref;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nv_set_hdr_rev_pref_result (const char *buf, size_t len, int *out_error)
{
    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_WRITE, sizeof (DMCmdNVReadWrite), out_error))
        return NULL;

    if (!check_nv_cmd ((DMCmdNVReadWrite *) buf, DIAG_NV_HDR_REV_PREF, out_error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

size_t
qcdm_cmd_cm_subsys_state_info_new (char *buf, size_t len)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    cmd->subsys_id = DIAG_SUBSYS_CM;
    cmd->subsys_cmd = htole16 (DIAG_SUBSYS_CM_STATE_INFO);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_cm_subsys_state_info_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdSubsysCMStateInfoRsp *rsp = (DMCmdSubsysCMStateInfoRsp *) buf;
    uint32_t tmp_num;
    uint32_t roam_pref;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysCMStateInfoRsp), out_error))
        return NULL;

    roam_pref = (uint32_t) le32toh (rsp->roam_pref);
    if (!roam_pref_validate (roam_pref)) {
        qcdm_err (0, "Unknown roam preference 0x%X", roam_pref);
        return NULL;
    }

    result = qcdm_result_new ();

    tmp_num = (uint32_t) le32toh (rsp->call_state);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_CALL_STATE, tmp_num);

    tmp_num = (uint32_t) le32toh (rsp->oper_mode);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE, tmp_num);

    tmp_num = (uint32_t) le32toh (rsp->system_mode);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE, tmp_num);

    tmp_num = (uint32_t) le32toh (rsp->mode_pref);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_MODE_PREF, tmp_num);

    tmp_num = (uint32_t) le32toh (rsp->band_pref);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_BAND_PREF, tmp_num);

    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF, roam_pref);

    tmp_num = (uint32_t) le32toh (rsp->srv_domain_pref);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SERVICE_DOMAIN_PREF, tmp_num);

    tmp_num = (uint32_t) le32toh (rsp->acq_order_pref);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ACQ_ORDER_PREF, tmp_num);

    tmp_num = (uint32_t) le32toh (rsp->hybrid_pref);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_HYBRID_PREF, tmp_num);

    tmp_num = (uint32_t) le32toh (rsp->network_sel_mode_pref);
    qcdm_result_add_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_NETWORK_SELECTION_PREF, tmp_num);

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_hdr_subsys_state_info_new (char *buf, size_t len)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    cmd->subsys_id = DIAG_SUBSYS_HDR;
    cmd->subsys_cmd = htole16 (DIAG_SUBSYS_HDR_STATE_INFO);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_hdr_subsys_state_info_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdSubsysHDRStateInfoRsp *rsp = (DMCmdSubsysHDRStateInfoRsp *) buf;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysHDRStateInfoRsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_AT_STATE, rsp->at_state);
    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE, rsp->session_state);
    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE, rsp->almp_state);
    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_INIT_STATE, rsp->init_state);
    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_IDLE_STATE, rsp->idle_state);
    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_CONNECTED_STATE, rsp->connected_state);
    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ROUTE_UPDATE_STATE, rsp->route_update_state);
    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_OVERHEAD_MSG_STATE, rsp->overhead_msg_state);
    qcdm_result_add_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_HDR_HYBRID_MODE, rsp->hdr_hybrid_mode);

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_ext_logmask_new (char *buf,
                          size_t len,
                          uint32_t items[],
                          uint16_t maxlog)
{
    char cmdbuf[sizeof (DMCmdExtLogMask) + 2];
    DMCmdExtLogMask *cmd = (DMCmdExtLogMask *) &cmdbuf[0];
    uint16_t highest = 0;
    size_t total = 3;
    uint32_t i;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_EXT_LOGMASK;

    if (items) {
        for (i = 0; items[i] > 0; i++) {
            qcdm_warn_if_fail (items[i] > 0);
            qcdm_warn_if_fail (items[i] < 4095);
            cmd->mask[items[i] / 8] |= 1 << items[i] % 8;

            if (items[i] > highest)
                highest = items[i];
        }
    }

    qcdm_return_val_if_fail (highest <= maxlog, 0);
    cmd->len = htole16 (maxlog);
    total += maxlog / 8;
    if (maxlog && maxlog % 8)
        total++;

    return dm_encapsulate_buffer (cmdbuf, total, sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_ext_logmask_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdExtLogMask *rsp = (DMCmdExtLogMask *) buf;
    uint32_t masklen = 0, maxlog = 0;
    size_t minlen = 0;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    /* Ensure size is at least enough for the command header */
    if (len < 1) {
        qcdm_err (0, "DM command %d response not long enough (got %zu, expected "
                  "at least %d).", DIAG_CMD_EXT_LOGMASK, len, 3);
        return NULL;
    }

    /* Result of a 'set' operation will be only 1 byte in size; result of
     * a 'get' operation (ie setting len to 0x0000 in the request) will be
     * the size of the header (3) plus the max log length.
     */

    if (len == 1)
        minlen = 1;
    else {
        /* Ensure size is equal to max # of log items + 3 */
        maxlog = le16toh (rsp->len);
        masklen = maxlog / 8;
        if (maxlog % 8)
            masklen++;

        if (len < (masklen + 3)) {
            qcdm_err (0, "DM command %d response not long enough (got %zu, expected "
                      "at least %d).", DIAG_CMD_EXT_LOGMASK, len, masklen + 3);
            return NULL;
        }
        minlen = masklen + 3;
    }

    if (!check_command (buf, len, DIAG_CMD_EXT_LOGMASK, minlen, out_error))
        return NULL;

    result = qcdm_result_new ();

    if (minlen != 4)
        qcdm_result_add_u32 (result, QCDM_CMD_EXT_LOGMASK_ITEM_MAX_ITEMS, maxlog);

    return result;
}

qcdmbool
qcmd_cmd_ext_logmask_result_get_item (QcdmResult *result,
                                      uint16_t item)
{
    return FALSE;
}

/**********************************************************************/

size_t
qcdm_cmd_event_report_new (char *buf, size_t len, qcdmbool start)
{
    char cmdbuf[4];
    DMCmdEventReport *cmd = (DMCmdEventReport *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_EVENT_REPORT;
    cmd->on = start ? 1 : 0;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_event_report_result (const char *buf, size_t len, int *out_error)
{
    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_EVENT_REPORT, sizeof (DMCmdEventReport), out_error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

size_t
qcdm_cmd_zte_subsys_status_new (char *buf, size_t len)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    cmd->subsys_id = DIAG_SUBSYS_ZTE;
    cmd->subsys_cmd = htole16 (DIAG_SUBSYS_ZTE_STATUS);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_zte_subsys_status_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdSubsysZteStatusRsp *rsp = (DMCmdSubsysZteStatusRsp *) buf;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysZteStatusRsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    qcdm_result_add_u8 (result, QCDM_CMD_ZTE_SUBSYS_STATUS_ITEM_SIGNAL_INDICATOR, rsp->signal_ind);

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_nw_subsys_modem_snapshot_cdma_new (char *buf,
                                            size_t len,
                                            uint8_t chipset)
{
    char cmdbuf[sizeof (DMCmdSubsysNwSnapshotReq) + 2];
    DMCmdSubsysNwSnapshotReq *cmd = (DMCmdSubsysNwSnapshotReq *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    /* Validate chipset */
    if (chipset != QCDM_NW_CHIPSET_6500 && chipset != QCDM_NW_CHIPSET_6800) {
        qcdm_err (0, "Unknown Novatel chipset 0x%X", chipset);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->hdr.code = DIAG_CMD_SUBSYS;
    switch (chipset) {
    case QCDM_NW_CHIPSET_6500:
        cmd->hdr.subsys_id = DIAG_SUBSYS_NOVATEL_6500;
        break;
    case QCDM_NW_CHIPSET_6800:
        cmd->hdr.subsys_id = DIAG_SUBSYS_NOVATEL_6800;
        break;
    default:
        qcdm_assert_not_reached ();
    }
    cmd->hdr.subsys_cmd = htole16 (DIAG_SUBSYS_NOVATEL_MODEM_SNAPSHOT);
    cmd->technology = DIAG_SUBSYS_NOVATEL_MODEM_SNAPSHOT_TECH_CDMA_EVDO;
    cmd->snapshot_mask = htole32 (0xFFFF);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nw_subsys_modem_snapshot_cdma_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdSubsysNwSnapshotRsp *rsp = (DMCmdSubsysNwSnapshotRsp *) buf;
    DMCmdSubsysNwSnapshotCdma *cdma = (DMCmdSubsysNwSnapshotCdma *) &rsp->data;
    uint32_t num;
    uint8_t num8;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysNwSnapshotRsp), out_error))
        return NULL;

    /* FIXME: check response_code when we know what it means */

    result = qcdm_result_new ();

    num = le32toh (cdma->rssi);
    qcdm_result_add_u32 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_RSSI, num);

    num8 = cdma_prev_to_qcdm (cdma->prev);
    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_PREV, num8);

    num8 = cdma_band_class_to_qcdm (cdma->band_class);
    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_BAND_CLASS, num8);

    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_ERI, cdma->eri);

    num8 = QCDM_HDR_REV_UNKNOWN;
    switch (cdma->hdr_rev) {
    case 0:
        num8 = QCDM_HDR_REV_0;
        break;
    case 1:
        num8 = QCDM_HDR_REV_A;
        break;
    default:
        break;
    }
    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV, num8);

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_nw_subsys_eri_new (char *buf,
                            size_t len,
                            uint8_t chipset)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    /* Validate chipset */
    if (chipset != QCDM_NW_CHIPSET_6500 && chipset != QCDM_NW_CHIPSET_6800) {
        qcdm_err (0, "Unknown Novatel chipset 0x%X", chipset);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    switch (chipset) {
    case QCDM_NW_CHIPSET_6500:
        cmd->subsys_id = DIAG_SUBSYS_NOVATEL_6500;
        break;
    case QCDM_NW_CHIPSET_6800:
        cmd->subsys_id = DIAG_SUBSYS_NOVATEL_6800;
        break;
    default:
        qcdm_assert_not_reached ();
    }
    cmd->subsys_cmd = htole16 (DIAG_SUBSYS_NOVATEL_ERI);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_nw_subsys_eri_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdSubsysNwEriRsp *rsp = (DMCmdSubsysNwEriRsp *) buf;
    char str[50];

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysNwEriRsp), out_error))
        return NULL;

    /* FIXME: check 'status' when we know what it means */

    result = qcdm_result_new ();

    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ROAM, rsp->roam);
    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_INDICATOR_ID, rsp->indicator_id);
    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_ID, rsp->icon_id);
    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_MODE, rsp->icon_mode);
    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_CALL_PROMPT_ID, rsp->call_prompt_id);
    qcdm_result_add_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ALERT_ID, rsp->alert_id);

    qcdm_warn_if_fail (rsp->text_len < sizeof (str));
    if (rsp->text_len < sizeof (str)) {
        qcdm_assert (sizeof (str) > sizeof (rsp->text));
        memcpy (str, rsp->text, sizeof (rsp->text));
        str[rsp->text_len] = '\0';
        qcdm_result_add_string (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_TEXT, str);
    }

    return result;
}

/**********************************************************************/

static size_t
qcdm_cmd_log_config_new (char *buf,
                         size_t len,
                         uint32_t op,
                         uint32_t equip_id,
                         uint16_t items[])
{
    DMCmdLogConfig *cmd;
    uint16_t highest = 0;
    uint32_t items_len = 0;
    size_t cmdsize = 0, cmdbufsize;
    uint32_t i;
    uint16_t log_code;
    size_t ret;

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail ((equip_id & 0xFFF0) == 0, 0);

    /* Find number of log items */
    if (items) {
        while (items_len < 4095 && items[items_len]) {
            /* Find highest log item so we can size the items mask */
            log_code = items[items_len] & 0x0FFF;
            if (log_code > highest)
                highest = log_code;
            items_len++;
        }
    }
    cmdsize = sizeof (DMCmdLogConfig) + ((highest + 7) / 8);
    cmdbufsize = cmdsize + DIAG_TRAILER_LEN;

    qcdm_return_val_if_fail (len >= cmdsize, 0);

    cmd = calloc (1, cmdbufsize);
    cmd->code = DIAG_CMD_LOG_CONFIG;
    cmd->op = htole32 (op);
    cmd->equipid = htole32 (equip_id);

    if (items) {
        /* Set up the bitmask of log items */
        for (i = 0; i < items_len; i++) {
            log_code = items[i] & 0x0FFF;  /* Strip off equip ID */
            cmd->mask[log_code / 8] |= 1 << log_code % 8;
        }
        cmd->num_items = htole32 (highest);
    }

    ret = dm_encapsulate_buffer ((char *) cmd, cmdsize, cmdbufsize, buf, len);
    free (cmd);

    return ret;
}

size_t
qcdm_cmd_log_config_get_mask_new (char *buf,
                                  size_t len,
                                  uint32_t equip_id)
{
    return qcdm_cmd_log_config_new (buf,
                                    len,
                                    DIAG_CMD_LOG_CONFIG_OP_GET_MASK,
                                    equip_id,
                                    NULL);
}

static int
check_log_config_respose (const char *buf, size_t len, uint32_t op)
{
    DMCmdLogConfigRsp *rsp = (DMCmdLogConfigRsp *) buf;
    size_t minlen = 16; /* minimum valid resposne */
    int err;

    /* Ensure size is at least enough for the command header */
    if (len < 1) {
        qcdm_err (0, "DIAG_CMD_LOG_CONFIG response not long enough (got %zu, "
                  "expected at least %d).", len, 3);
        return -QCDM_ERROR_RESPONSE_BAD_LENGTH;
    }

    if (rsp->code == DIAG_CMD_LOG_CONFIG) {
        uint32_t rspop;

        if (len < 16) {
            /* At least enough for code + op + result + equipid */
            qcdm_err (0, "DIAG_CMD_LOG_CONFIG response not long enough (got %zu, "
                      "expected at least %d).", len, 16);
            return -QCDM_ERROR_RESPONSE_BAD_LENGTH;
        }

        rspop = le32toh (rsp->op);
        if (rspop != op) {
            qcdm_err (0, "DIAG_CMD_LOG_CONFIG response operation mismatch (got "
                      "op %u, expected %u)", rspop, op);
            return -QCDM_ERROR_RESPONSE_BAD_COMMAND;
        }

        /* check for success */
        if (le32toh (rsp->result) != 0) {
            qcdm_err (0, "DIAG_CMD_LOG_CONFIG response failed with result %u.",
                      le32toh (rsp->result));
            return -QCDM_ERROR_RESPONSE_FAILED;
        }

        switch (rspop) {
        case DIAG_CMD_LOG_CONFIG_OP_GET_RANGE:
            minlen += 16; /* get_range_items */
            break;
        case DIAG_CMD_LOG_CONFIG_OP_SET_MASK:
        case DIAG_CMD_LOG_CONFIG_OP_GET_MASK:
            if (len < 16) {
                qcdm_err (0, "DIAG_CMD_LOG_CONFIG response not long enough "
                          "(got %zu, expected at least %d).", len, 16);
                return -QCDM_ERROR_RESPONSE_BAD_LENGTH;
            }
            minlen += 4;  /* num_items */
            minlen += (le32toh (rsp->u.get_set_items.num_items) + 7) / 8;
            break;
        default:
            qcdm_err (0, "Unknown DIAG_CMD_LOG_CONFIG response operation %d", rspop);
            return -QCDM_ERROR_RESPONSE_UNEXPECTED;
        }
    }

    if (!check_command (buf, len, DIAG_CMD_LOG_CONFIG, minlen, &err))
        return err;

    return 0;
}

#define LOG_CODE_SET(mask, code)  (mask[code / 8] & (1 << (code % 8)))

static QcdmResult *
log_config_get_set_result (const char *buf, size_t len, uint32_t op, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdLogConfigRsp *rsp = (DMCmdLogConfigRsp *) buf;
    int err;
    uint32_t num_items;
    uint32_t equipid;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    err = check_log_config_respose (buf, len, op);
    if (err) {
        if (out_error)
            *out_error = err;
        return NULL;
    }

    result = qcdm_result_new ();

    equipid = le32toh (rsp->equipid);
    qcdm_result_add_u32 (result, QCDM_CMD_LOG_CONFIG_MASK_ITEM_EQUIP_ID, equipid);

    num_items = le32toh (rsp->u.get_set_items.num_items);
    qcdm_result_add_u32 (result, QCDM_CMD_LOG_CONFIG_MASK_ITEM_NUM_ITEMS, num_items);

    if (num_items > 0) {
        uint32_t i, num_result_items = 0, count = 0;
        uint16_t *items;

        /* First pass to find out how many are actually enabled */
        for (i = 0; i < num_items; i++) {
            /* Check if the bit corresponding to this log item is set */
            if (LOG_CODE_SET (rsp->u.get_set_items.mask, i))
                num_result_items++;
        }

        if (num_result_items) {
            items = malloc (sizeof (*items) * num_result_items);
            for (i = 0; i < num_items; i++) {
                if (LOG_CODE_SET (rsp->u.get_set_items.mask, i))
                    items[count++] = (equipid << 12) | (i & 0x0FFF);
            }

            qcdm_result_add_u16_array (result, QCDM_CMD_LOG_CONFIG_MASK_ITEM_ITEMS, items, count);
            free (items);
        }
    }

    return result;
}

QcdmResult *
qcdm_cmd_log_config_get_mask_result (const char *buf, size_t len, int *out_error)
{
    return log_config_get_set_result (buf, len, DIAG_CMD_LOG_CONFIG_OP_GET_MASK, out_error);
}

size_t
qcdm_cmd_log_config_set_mask_new (char *buf,
                                  size_t len,
                                  uint32_t equip_id,
                                  uint16_t items[])
{
    return qcdm_cmd_log_config_new (buf,
                                    len,
                                    DIAG_CMD_LOG_CONFIG_OP_SET_MASK,
                                    equip_id,
                                    items);
}

QcdmResult *
qcdm_cmd_log_config_set_mask_result (const char *buf, size_t len, int *out_error)
{
    return log_config_get_set_result (buf, len, DIAG_CMD_LOG_CONFIG_OP_SET_MASK, out_error);
}

qcdmbool
qcmd_cmd_log_config_mask_result_code_set (QcdmResult *result,
                                          uint32_t equipid,
                                          uint16_t log_code)
{
    const uint16_t *items = NULL;
    size_t len = 0;
    uint32_t i, tmp;

    qcdm_return_val_if_fail (result != NULL, FALSE);

    if (qcdm_result_get_u32 (result, QCDM_CMD_LOG_CONFIG_MASK_ITEM_EQUIP_ID, &tmp) != 0)
        return FALSE;
    qcdm_return_val_if_fail (equipid != tmp, FALSE);

    if (qcdm_result_get_u16_array (result,
                                   QCDM_CMD_LOG_CONFIG_MASK_ITEM_ITEMS,
                                   &items,
                                   &len)) {
        for (i = 0; i < len; i++) {
            if ((items[i] & 0x0FFF) == (log_code & 0x0FFF))
                return TRUE;
        }
    }
    return FALSE;
}

/**********************************************************************/

static char bcd_chars[] = "0123456789\0\0\0\0\0\0";

static qcdmbool
imxi_bcd_to_string (uint8_t bytes[8], size_t len, char *buf, size_t buflen)
{
    char *p;
    uint32_t i;

    if (bytes[0] == 0)
        return TRUE;

    qcdm_return_val_if_fail (len == 8, FALSE);
    qcdm_return_val_if_fail (buf != NULL, FALSE);
    qcdm_return_val_if_fail (buflen > (len * 2), FALSE);

    p = buf;
    for (i = 0 ; i < len; i++) {
        /* IMxI are 15 chars long, so the lower 4-bits of the first
         * byte of the IMxI is skipped.  Not sure what it does.
         */
        if (i > 0) {
            *p = bcd_chars[bytes[i] & 0xf];
            if (!*p)
                return FALSE;
            p++;
        }
        *p = bcd_chars[(bytes[i] >> 4) & 0xf];
        if (!*p)
            return FALSE;
        p++;
    }
    *p++ = '\0';
    return TRUE;
}

size_t
qcdm_cmd_wcdma_subsys_state_info_new (char *buf, size_t len)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    cmd->subsys_id = DIAG_SUBSYS_WCDMA;
    cmd->subsys_cmd = htole16 (DIAG_SUBSYS_WCDMA_STATE_INFO);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_wcdma_subsys_state_info_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdSubsysWcdmaStateInfoRsp *rsp = (DMCmdSubsysWcdmaStateInfoRsp *) buf;
    char imxi[18];

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysWcdmaStateInfoRsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    qcdm_result_add_u8 (result, QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_L1_STATE, rsp->l1_state);

    memset (imxi, 0, sizeof (imxi));
    if (imxi_bcd_to_string (rsp->imei, rsp->imei_len, imxi, sizeof (imxi)))
        qcdm_result_add_string (result, QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_IMEI, imxi);

    memset (imxi, 0, sizeof (imxi));
    if (imxi_bcd_to_string (rsp->imsi, rsp->imsi_len, imxi, sizeof (imxi)))
        qcdm_result_add_string (result, QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_IMSI, imxi);

    return result;
}

/**********************************************************************/

size_t
qcdm_cmd_gsm_subsys_state_info_new (char *buf, size_t len)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    qcdm_return_val_if_fail (buf != NULL, 0);
    qcdm_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    cmd->subsys_id = DIAG_SUBSYS_GSM;
    cmd->subsys_cmd = htole16 (DIAG_SUBSYS_GSM_STATE_INFO);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QcdmResult *
qcdm_cmd_gsm_subsys_state_info_result (const char *buf, size_t len, int *out_error)
{
    QcdmResult *result = NULL;
    DMCmdSubsysGsmStateInfoRsp *rsp = (DMCmdSubsysGsmStateInfoRsp *) buf;
    char imxi[18];
    uint32_t mcc = 0, mnc = 0;
    uint8_t mnc3;

    qcdm_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysGsmStateInfoRsp), out_error))
        return NULL;

    result = qcdm_result_new ();

    memset (imxi, 0, sizeof (imxi));
    if (imxi_bcd_to_string (rsp->imei, rsp->imei_len, imxi, sizeof (imxi)))
        qcdm_result_add_string (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_IMEI, imxi);

    memset (imxi, 0, sizeof (imxi));
    if (imxi_bcd_to_string (rsp->imsi, rsp->imsi_len, imxi, sizeof (imxi)))
        qcdm_result_add_string (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_IMSI, imxi);

    qcdm_result_add_u8 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_CALL_STATE, rsp->cm_call_state);
    qcdm_result_add_u8 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_OP_MODE, rsp->cm_opmode);
    qcdm_result_add_u8 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_SYS_MODE, rsp->cm_sysmode);

    /* MCC/MNC, LAC, and CI don't seem to be valid when the modem is not in GSM mode */
    if (   rsp->cm_sysmode == QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GSM
        || rsp->cm_sysmode == QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GW) {
        /* Quick convert BCD LAI into MCC/MNC/LAC */
        mcc = (rsp->lai[0] & 0xF) * 100;
        mcc += ((rsp->lai[0] >> 4) & 0xF) * 10;
        mcc += rsp->lai[1] & 0xF;
        qcdm_result_add_u32 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_MCC, mcc);

        mnc = (rsp->lai[2] & 0XF) * 100;
        mnc += ((rsp->lai[2] >> 4) & 0xF) * 10;
        mnc3 = (rsp->lai[1] >> 4) & 0xF;
        if (mnc3 != 0xF)
            mnc += mnc3;
        qcdm_result_add_u32 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_MNC, mnc);

        qcdm_result_add_u32 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_LAC,
                             rsp->lai[4] << 8 | rsp->lai[3]);

        qcdm_result_add_u32 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CELLID, le16toh (rsp->cellid));
    }

    return result;
}

/**********************************************************************/
