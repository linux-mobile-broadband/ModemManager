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

    memset (cmd, 0, sizeof (cmd));
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

    memset (cmd, 0, sizeof (cmd));
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

