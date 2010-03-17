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

    g_return_val_if_fail (buf != NULL, NULL);

    if (!check_command (buf, len, DIAG_CMD_SUBSYS, sizeof (DMCmdSubsysCMStateInfoRsp), error))
        return NULL;

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

    tmp_num = (guint32) GUINT32_FROM_LE (rsp->roam_pref);
    qcdm_result_add_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF, tmp_num);

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

