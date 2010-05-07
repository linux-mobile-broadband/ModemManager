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

#include "commands.h"
#include "error.h"
#include "dm-commands.h"
#include "nv-items.h"
#include "result-private.h"
#include "utils.h"


/**********************************************************************/

static guint8
cdma_prev_to_qcdm (guint8 cdma)
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

static guint8
cdma_band_class_to_qcdm (guint8 cdma)
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
    default:
        break;
    }
    return QCDM_CDMA_BAND_CLASS_UNKNOWN;
}

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
bin2hexstr (const guint8 *bytes, int len)
{
    static char hex_digits[] = "0123456789abcdef";
    char *result;
    int i;
    gsize buflen = (len * 2) + 1;

    g_return_val_if_fail (bytes != NULL, NULL);
    g_return_val_if_fail (len > 0, NULL);
    g_return_val_if_fail (len < 4096, NULL);   /* Arbitrary limit */

    result = g_malloc0 (buflen);
    for (i = 0; i < len; i++) {
        result[2*i] = hex_digits[(bytes[i] >> 4) & 0xf];
        result[2*i+1] = hex_digits[bytes[i] & 0xf];
    }
    result[buflen - 1] = '\0';
    return result;
}

static gboolean
check_command (const char *buf, gsize len, guint8 cmd, gsize min_len, GError **error)
{
    if (len < 1) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_MALFORMED_RESPONSE,
                     "DM command response malformed (must be at least 1 byte in length)");
        return FALSE;
    }

    switch (buf[0]) {
    case DIAG_CMD_BAD_CMD:
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_COMMAND,
                     "DM command %d unknown or unimplemented by the device",
                     cmd);
        return FALSE;
    case DIAG_CMD_BAD_PARM:
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_PARAMETER,
                     "DM command %d contained invalid parameter",
                     cmd);
        return FALSE;
    case DIAG_CMD_BAD_LEN:
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_LENGTH,
                     "DM command %d was the wrong size",
                     cmd);
        return FALSE;
    case DIAG_CMD_BAD_DEV:
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_NOT_ACCEPTED,
                     "DM command %d was not accepted by the device",
                     cmd);
        return FALSE;
    case DIAG_CMD_BAD_MODE:
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_MODE,
                     "DM command %d not allowed in the current device mode",
                     cmd);
        return FALSE;
    default:
        break;
    }

    if (buf[0] != cmd) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_UNEXPECTED,
                     "Unexpected DM command response (expected %d, got %d)",
                     cmd, buf[0]);
        return FALSE;
    }

    if (len < min_len) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_LENGTH,
                     "DM command %d response not long enough (got %zu, expected "
                     "at least %zu).", cmd, len, min_len);
        return FALSE;
    }

    return TRUE;
}

static gboolean
check_nv_cmd (DMCmdNVReadWrite *cmd, guint16 nv_item, GError **error)
{
    guint16 cmd_item;

    g_return_val_if_fail (cmd != NULL, FALSE);
    g_return_val_if_fail ((cmd->code == DIAG_CMD_NV_READ) || (cmd->code == DIAG_CMD_NV_WRITE), FALSE);

    /* NV read/write have a status byte at the end */
    if (cmd->status != 0) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_NVCMD_FAILED,
                        "The NV operation failed (status 0x%X).",
                        GUINT16_FROM_LE (cmd->status));
        return FALSE;
    }

    cmd_item = GUINT16_FROM_LE (cmd->nv_item);
    if (cmd_item != nv_item) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_UNEXPECTED,
                     "Unexpected DM NV command response (expected item %d, got "
                     "item %d)", nv_item, cmd_item);
        return FALSE;
    }

    return TRUE;
}

/**********************************************************************/

gsize
qcdm_cmd_version_info_new (char *buf, gsize len, GError **error)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_VERSION_INFO;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_version_info_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdVersionInfoRsp *rsp = (DMCmdVersionInfoRsp *) buf;
    char tmp[12];

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_VERSION_INFO, sizeof (DMCmdVersionInfoRsp), error))
        return NULL;

    result = qcdm_result_new ();

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->comp_date) <= sizeof (tmp));
    memcpy (tmp, rsp->comp_date, sizeof (rsp->comp_date));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_COMP_DATE, tmp);

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->comp_time) <= sizeof (tmp));
    memcpy (tmp, rsp->comp_time, sizeof (rsp->comp_time));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_COMP_TIME, tmp);

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->rel_date) <= sizeof (tmp));
    memcpy (tmp, rsp->rel_date, sizeof (rsp->rel_date));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_RELEASE_DATE, tmp);

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->rel_time) <= sizeof (tmp));
    memcpy (tmp, rsp->rel_time, sizeof (rsp->rel_time));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_RELEASE_TIME, tmp);

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->model) <= sizeof (tmp));
    memcpy (tmp, rsp->model, sizeof (rsp->model));
    qcdm_result_add_string (result, QCDM_CMD_VERSION_INFO_ITEM_MODEL, tmp);

    return result;
}

/**********************************************************************/

gsize
qcdm_cmd_esn_new (char *buf, gsize len, GError **error)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_ESN;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_esn_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdEsnRsp *rsp = (DMCmdEsnRsp *) buf;
    char *tmp;
    guint8 swapped[4];

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_ESN, sizeof (DMCmdEsnRsp), error))
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
    qcdm_result_add_string (result, QCDM_CMD_ESN_ITEM_ESN, tmp);
    g_free (tmp);

    return result;
}

/**********************************************************************/

gsize
qcdm_cmd_cdma_status_new (char *buf, gsize len, GError **error)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_STATUS;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_cdma_status_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdStatusRsp *rsp = (DMCmdStatusRsp *) buf;
    char *tmp;
    guint8 swapped[4];
    guint32 tmp_num;

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_STATUS, sizeof (DMCmdStatusRsp), error))
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
    g_free (tmp);

    tmp_num = (guint32) GUINT16_FROM_LE (rsp->rf_mode);
    qcdm_result_add_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RF_MODE, tmp_num);

    tmp_num = (guint32) GUINT16_FROM_LE (rsp->cdma_rx_state);
    qcdm_result_add_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE, tmp_num);

    tmp_num = (guint32) GUINT16_FROM_LE (rsp->entry_reason);
    qcdm_result_add_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_ENTRY_REASON, tmp_num);

    tmp_num = (guint32) GUINT16_FROM_LE (rsp->curr_chan);
    qcdm_result_add_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_CURRENT_CHANNEL, tmp_num);

    qcdm_result_add_uint8 (result, QCDM_CMD_CDMA_STATUS_ITEM_CODE_CHANNEL, rsp->cdma_code_chan);

    tmp_num = (guint32) GUINT16_FROM_LE (rsp->pilot_base);
    qcdm_result_add_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_PILOT_BASE, tmp_num);

    tmp_num = (guint32) GUINT16_FROM_LE (rsp->sid);
    qcdm_result_add_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_SID, tmp_num);

    tmp_num = (guint32) GUINT16_FROM_LE (rsp->nid);
    qcdm_result_add_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_NID, tmp_num);

    return result;
}

/**********************************************************************/

gsize
qcdm_cmd_sw_version_new (char *buf, gsize len, GError **error)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SW_VERSION;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_sw_version_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdSwVersionRsp *rsp = (DMCmdSwVersionRsp *) buf;
    char tmp[25];

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SW_VERSION, sizeof (*rsp), error))
        return NULL;

    result = qcdm_result_new ();

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->version) <= sizeof (tmp));
    memcpy (tmp, rsp->version, sizeof (rsp->version));
    qcdm_result_add_string (result, QCDM_CMD_SW_VERSION_ITEM_VERSION, tmp);

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->comp_date) <= sizeof (tmp));
    memcpy (tmp, rsp->comp_date, sizeof (rsp->comp_date));
    qcdm_result_add_string (result, QCDM_CMD_SW_VERSION_ITEM_COMP_DATE, tmp);

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->comp_time) <= sizeof (tmp));
    memcpy (tmp, rsp->comp_time, sizeof (rsp->comp_time));
    qcdm_result_add_string (result, QCDM_CMD_SW_VERSION_ITEM_COMP_TIME, tmp);

    return result;
}

/**********************************************************************/

gsize
qcdm_cmd_pilot_sets_new (char *buf, gsize len, GError **error)
{
    char cmdbuf[3];
    DMCmdHeader *cmd = (DMCmdHeader *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_PILOT_SETS;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

#define PILOT_SETS_CMD_ACTIVE_SET    "active-set"
#define PILOT_SETS_CMD_CANDIDATE_SET "candidate-set"
#define PILOT_SETS_CMD_NEIGHBOR_SET  "neighbor-set"

static const char *
set_num_to_str (guint32 num)
{
    if (num == QCDM_CMD_PILOT_SETS_TYPE_ACTIVE)
        return PILOT_SETS_CMD_ACTIVE_SET;
    if (num == QCDM_CMD_PILOT_SETS_TYPE_CANDIDATE)
        return PILOT_SETS_CMD_CANDIDATE_SET;
    if (num == QCDM_CMD_PILOT_SETS_TYPE_NEIGHBOR)
        return PILOT_SETS_CMD_NEIGHBOR_SET;
    return NULL;
}

QCDMResult *
qcdm_cmd_pilot_sets_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdPilotSetsRsp *rsp = (DMCmdPilotSetsRsp *) buf;
    GByteArray *array;
    gsize sets_len;

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_PILOT_SETS, sizeof (DMCmdPilotSetsRsp), error))
        return NULL;

    result = qcdm_result_new ();

    sets_len = rsp->active_count * sizeof (DMCmdPilotSetsSet);
    if (sets_len > 0) {
        array = g_byte_array_sized_new (sets_len);
        g_byte_array_append (array, (const guint8 *) &rsp->sets[0], sets_len);
        qcdm_result_add_boxed (result, PILOT_SETS_CMD_ACTIVE_SET, G_TYPE_BYTE_ARRAY, array);
    }

    sets_len = rsp->candidate_count * sizeof (DMCmdPilotSetsSet);
    if (sets_len > 0) {
        array = g_byte_array_sized_new (sets_len);
        g_byte_array_append (array, (const guint8 *) &rsp->sets[rsp->active_count], sets_len);
        qcdm_result_add_boxed (result, PILOT_SETS_CMD_CANDIDATE_SET, G_TYPE_BYTE_ARRAY, array);
    }

    sets_len = rsp->neighbor_count * sizeof (DMCmdPilotSetsSet);
    if (sets_len > 0) {
        array = g_byte_array_sized_new (sets_len);
        g_byte_array_append (array, (const guint8 *) &rsp->sets[rsp->active_count + rsp->candidate_count], sets_len);
        qcdm_result_add_boxed (result, PILOT_SETS_CMD_NEIGHBOR_SET, G_TYPE_BYTE_ARRAY, array);
    }

    return result;
}

gboolean
qcdm_cmd_pilot_sets_result_get_num (QCDMResult *result,
                                    guint32 set_type,
                                    guint32 *out_num)
{
    const char *set_name;
    GByteArray *array = NULL;

    g_return_val_if_fail (result != NULL, FALSE);

    set_name = set_num_to_str (set_type);
    g_return_val_if_fail (set_name != NULL, FALSE);

    if (!qcdm_result_get_boxed (result, set_name, (gpointer) &array))
        return FALSE;

    *out_num = array->len / sizeof (DMCmdPilotSetsSet);
    return TRUE;
}

gboolean
qcdm_cmd_pilot_sets_result_get_pilot (QCDMResult *result,
                                      guint32 set_type,
                                      guint32 num,
                                      guint32 *out_pn_offset,
                                      guint32 *out_ecio,
                                      float *out_db)
{
    const char *set_name;
    GByteArray *array = NULL;
    DMCmdPilotSetsSet *set;

    g_return_val_if_fail (result != NULL, FALSE);

    set_name = set_num_to_str (set_type);
    g_return_val_if_fail (set_name != NULL, FALSE);

    if (!qcdm_result_get_boxed (result, set_name, (gpointer) &array))
        return FALSE;

    g_return_val_if_fail (num < array->len / sizeof (DMCmdPilotSetsSet), FALSE);

    set = (DMCmdPilotSetsSet *) &array->data[num * sizeof (DMCmdPilotSetsSet)];
    *out_pn_offset = set->pn_offset;
    *out_ecio = set->ecio;
    /* EC/IO is in units of -0.5 dB per the specs */
    *out_db = (float) set->ecio * -0.5;
    return TRUE;
}

/**********************************************************************/

gsize
qcdm_cmd_nv_get_mdn_new (char *buf, gsize len, guint8 profile, GError **error)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemMdn *req;

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = GUINT16_TO_LE (DIAG_NV_DIR_NUMBER);

    req = (DMNVItemMdn *) &cmd->data[0];
    req->profile = profile;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_nv_get_mdn_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemMdn *mdn;
    char tmp[11];

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_DIR_NUMBER, error))
        return NULL;

    mdn = (DMNVItemMdn *) &rsp->data[0];

    result = qcdm_result_new ();

    qcdm_result_add_uint8 (result, QCDM_CMD_NV_GET_MDN_ITEM_PROFILE, mdn->profile);

    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (mdn->mdn) <= sizeof (tmp));
    memcpy (tmp, mdn->mdn, sizeof (mdn->mdn));
    qcdm_result_add_string (result, QCDM_CMD_NV_GET_MDN_ITEM_MDN, tmp);

    return result;
}

/**********************************************************************/

static gboolean
roam_pref_validate (guint8 dm)
{
    if (   dm == DIAG_NV_ROAM_PREF_HOME_ONLY
        || dm == DIAG_NV_ROAM_PREF_ROAM_ONLY
        || dm == DIAG_NV_ROAM_PREF_AUTO)
        return TRUE;
    return FALSE;
}

gsize
qcdm_cmd_nv_get_roam_pref_new (char *buf, gsize len, guint8 profile, GError **error)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemRoamPref *req;

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = GUINT16_TO_LE (DIAG_NV_ROAM_PREF);

    req = (DMNVItemRoamPref *) &cmd->data[0];
    req->profile = profile;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_nv_get_roam_pref_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemRoamPref *roam;

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_ROAM_PREF, error))
        return NULL;

    roam = (DMNVItemRoamPref *) &rsp->data[0];

    if (!roam_pref_validate (roam->roam_pref)) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_PARAMETER,
                     "Unknown roam preference 0x%X",
                     roam->roam_pref);
        return NULL;
    }

    result = qcdm_result_new ();
    qcdm_result_add_uint8 (result, QCDM_CMD_NV_GET_ROAM_PREF_ITEM_PROFILE, roam->profile);
    qcdm_result_add_uint8 (result, QCDM_CMD_NV_GET_ROAM_PREF_ITEM_ROAM_PREF, roam->roam_pref);

    return result;
}

gsize
qcdm_cmd_nv_set_roam_pref_new (char *buf,
                               gsize len,
                               guint8 profile,
                               guint8 roam_pref,
                               GError **error)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemRoamPref *req;

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    if (!roam_pref_validate (roam_pref)) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_PARAMETER,
                     "Invalid roam preference %d", roam_pref);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_WRITE;
    cmd->nv_item = GUINT16_TO_LE (DIAG_NV_ROAM_PREF);

    req = (DMNVItemRoamPref *) &cmd->data[0];
    req->profile = profile;
    req->roam_pref = roam_pref;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_nv_set_roam_pref_result (const char *buf, gsize len, GError **error)
{
    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_WRITE, sizeof (DMCmdNVReadWrite), error))
        return NULL;

    if (!check_nv_cmd ((DMCmdNVReadWrite *) buf, DIAG_NV_ROAM_PREF, error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

static gboolean
mode_pref_validate (guint8 dm)
{
    if (   dm == DIAG_NV_MODE_PREF_1X_ONLY
        || dm == DIAG_NV_MODE_PREF_HDR_ONLY
        || dm == DIAG_NV_MODE_PREF_AUTO)
        return TRUE;
    return FALSE;
}

gsize
qcdm_cmd_nv_get_mode_pref_new (char *buf, gsize len, guint8 profile, GError **error)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemModePref *req;

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_READ;
    cmd->nv_item = GUINT16_TO_LE (DIAG_NV_MODE_PREF);

    req = (DMNVItemModePref *) &cmd->data[0];
    req->profile = profile;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_nv_get_mode_pref_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdNVReadWrite *rsp = (DMCmdNVReadWrite *) buf;
    DMNVItemModePref *mode;

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_READ, sizeof (DMCmdNVReadWrite), error))
        return NULL;

    if (!check_nv_cmd (rsp, DIAG_NV_MODE_PREF, error))
        return NULL;

    mode = (DMNVItemModePref *) &rsp->data[0];

    if (!mode_pref_validate (mode->mode_pref)) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_PARAMETER,
                     "Unknown mode preference 0x%X",
                     mode->mode_pref);
        return NULL;
    }

    result = qcdm_result_new ();
    qcdm_result_add_uint8 (result, QCDM_CMD_NV_GET_MODE_PREF_ITEM_PROFILE, mode->profile);
    qcdm_result_add_uint8 (result, QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF, mode->mode_pref);

    return result;
}

gsize
qcdm_cmd_nv_set_mode_pref_new (char *buf,
                               gsize len,
                               guint8 profile,
                               guint8 mode_pref,
                               GError **error)
{
    char cmdbuf[sizeof (DMCmdNVReadWrite) + 2];
    DMCmdNVReadWrite *cmd = (DMCmdNVReadWrite *) &cmdbuf[0];
    DMNVItemModePref *req;

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    if (!mode_pref_validate (mode_pref)) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_PARAMETER,
                     "Invalid mode preference %d", mode_pref);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_NV_WRITE;
    cmd->nv_item = GUINT16_TO_LE (DIAG_NV_MODE_PREF);

    req = (DMNVItemModePref *) &cmd->data[0];
    req->profile = profile;
    req->mode_pref = mode_pref;

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_nv_set_mode_pref_result (const char *buf, gsize len, GError **error)
{
    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_NV_WRITE, sizeof (DMCmdNVReadWrite), error))
        return NULL;

    if (!check_nv_cmd ((DMCmdNVReadWrite *) buf, DIAG_NV_MODE_PREF, error))
        return NULL;

    return qcdm_result_new ();
}

/**********************************************************************/

gsize
qcdm_cmd_cm_subsys_state_info_new (char *buf, gsize len, GError **error)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    cmd->subsys_id = DIAG_SUBSYS_CM;
    cmd->subsys_cmd = GUINT16_TO_LE (DIAG_SUBSYS_CM_STATE_INFO);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_cm_subsys_state_info_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdSubsysCMStateInfoRsp *rsp = (DMCmdSubsysCMStateInfoRsp *) buf;
    guint32 tmp_num;
    guint32 roam_pref;

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysCMStateInfoRsp), error))
        return NULL;

    roam_pref = (guint32) GUINT32_FROM_LE (rsp->roam_pref);
    if (!roam_pref_validate (roam_pref)) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_PARAMETER,
                     "Unknown roam preference 0x%X",
                     roam_pref);
        return NULL;
    }

    result = qcdm_result_new ();

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->call_state);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_CALL_STATE, tmp_num);

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->oper_mode);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE, tmp_num);

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->system_mode);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE, tmp_num);

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->mode_pref);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_MODE_PREF, tmp_num);

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->band_pref);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_BAND_PREF, tmp_num);

    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF, roam_pref);

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->srv_domain_pref);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SERVICE_DOMAIN_PREF, tmp_num);

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->acq_order_pref);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ACQ_ORDER_PREF, tmp_num);

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->hybrid_pref);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_HYBRID_PREF, tmp_num);

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->network_sel_mode_pref);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_NETWORK_SELECTION_PREF, tmp_num);

    return result;
}

/**********************************************************************/

gsize
qcdm_cmd_hdr_subsys_state_info_new (char *buf, gsize len, GError **error)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    cmd->subsys_id = DIAG_SUBSYS_HDR;
    cmd->subsys_cmd = GUINT16_TO_LE (DIAG_SUBSYS_HDR_STATE_INFO);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_hdr_subsys_state_info_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdSubsysHDRStateInfoRsp *rsp = (DMCmdSubsysHDRStateInfoRsp *) buf;

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysHDRStateInfoRsp), error))
        return NULL;

    result = qcdm_result_new ();

    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_AT_STATE, rsp->at_state);
    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE, rsp->session_state);
    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE, rsp->almp_state);
    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_INIT_STATE, rsp->init_state);
    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_IDLE_STATE, rsp->idle_state);
    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_CONNECTED_STATE, rsp->connected_state);
    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ROUTE_UPDATE_STATE, rsp->route_update_state);
    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_OVERHEAD_MSG_STATE, rsp->overhead_msg_state);
    qcdm_result_add_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_HDR_HYBRID_MODE, rsp->hdr_hybrid_mode);

    return result;
}

/**********************************************************************/

gsize
qcdm_cmd_zte_subsys_status_new (char *buf, gsize len, GError **error)
{
    char cmdbuf[sizeof (DMCmdSubsysHeader) + 2];
    DMCmdSubsysHeader *cmd = (DMCmdSubsysHeader *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->code = DIAG_CMD_SUBSYS;
    cmd->subsys_id = DIAG_SUBSYS_ZTE;
    cmd->subsys_cmd = GUINT16_TO_LE (DIAG_SUBSYS_ZTE_STATUS);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_zte_subsys_status_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdSubsysZteStatusRsp *rsp = (DMCmdSubsysZteStatusRsp *) buf;

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysZteStatusRsp), error))
        return NULL;

    result = qcdm_result_new ();

    qcdm_result_add_uint8 (result, QCDM_CMD_ZTE_SUBSYS_STATUS_ITEM_SIGNAL_INDICATOR, rsp->signal_ind);

    return result;
}

/**********************************************************************/

gsize
qcdm_cmd_nw_subsys_modem_snapshot_cdma_new (char *buf,
                                            gsize len,
                                            guint8 chipset,
                                            GError **error)
{
    char cmdbuf[sizeof (DMCmdSubsysNwSnapshotReq) + 2];
    DMCmdSubsysNwSnapshotReq *cmd = (DMCmdSubsysNwSnapshotReq *) &cmdbuf[0];

    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (len >= sizeof (*cmd) + DIAG_TRAILER_LEN, 0);

    /* Validate chipset */
    if (chipset != QCDM_NW_CHIPSET_6500 && chipset != QCDM_NW_CHIPSET_6800) {
        g_set_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_PARAMETER,
                     "Unknown Novatel chipset 0x%X",
                     chipset);
        return 0;
    }

    memset (cmd, 0, sizeof (*cmd));
    cmd->hdr.code = DIAG_CMD_SUBSYS;
    switch (chipset) {
    case QCDM_NW_CHIPSET_6500:
        cmd->hdr.subsys_id = DIAG_SUBSYS_NW_CONTROL_6500;
        break;
    case QCDM_NW_CHIPSET_6800:
        cmd->hdr.subsys_id = DIAG_SUBSYS_NW_CONTROL_6800;
        break;
    default:
        g_assert_not_reached ();
    }
    cmd->hdr.subsys_cmd = GUINT16_TO_LE (DIAG_SUBSYS_NW_CONTROL_MODEM_SNAPSHOT);
    cmd->technology = DIAG_SUBSYS_NW_CONTROL_MODEM_SNAPSHOT_TECH_CDMA_EVDO;
    cmd->snapshot_mask = GUINT32_TO_LE (0xFFFF);

    return dm_encapsulate_buffer (cmdbuf, sizeof (*cmd), sizeof (cmdbuf), buf, len);
}

QCDMResult *
qcdm_cmd_nw_subsys_modem_snapshot_cdma_result (const char *buf, gsize len, GError **error)
{
    QCDMResult *result = NULL;
    DMCmdSubsysNwSnapshotRsp *rsp = (DMCmdSubsysNwSnapshotRsp *) buf;
    DMCmdSubsysNwSnapshotCdma *cdma = (DMCmdSubsysNwSnapshotCdma *) &rsp->data;
    guint32 num;
    guint8 num8;

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysNwSnapshotRsp), error))
        return NULL;

    /* FIXME: check response_code when we know what it means */

    result = qcdm_result_new ();

    num = GUINT32_FROM_LE (cdma->rssi);
    qcdm_result_add_uint32 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_RSSI, num);

    num8 = cdma_prev_to_qcdm (cdma->prev);
    qcdm_result_add_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_PREV, num8);

    num8 = cdma_band_class_to_qcdm (cdma->band_class);
    qcdm_result_add_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_BAND_CLASS, num8);

    qcdm_result_add_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_ERI, cdma->eri);

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
    qcdm_result_add_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV, num8);

    return result;
}

/**********************************************************************/

