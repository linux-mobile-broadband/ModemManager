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

#include <string.h>

#include "commands.h"
#include "errors.h"
#include "result-private.h"
#include "utils.h"
#include "protocol.h"


/**********************************************************************/

static int
check_command (const char *buf, gsize len, u_int8_t cmd, size_t min_len)
{
    if (len < 1) {
        wmc_err (0, "Zero-length response");
        return -WMC_ERROR_RESPONSE_BAD_LENGTH;
    }

    if ((u_int8_t) buf[0] != WMC_CMD_MARKER) {
        wmc_err (0, "Missing WMC command marker (expected 0x%02X, got 0x%02X)",
                 WMC_CMD_MARKER, (u_int8_t) buf[0]);
        return -WMC_ERROR_RESPONSE_UNEXPECTED;
    }

    if ((u_int8_t) buf[1] != cmd) {
        wmc_err (0, "Unexpected WMC command response (expected 0x%02X, got 0x%02X)",
                 (u_int8_t) cmd, (u_int8_t) buf[1]);
        return -WMC_ERROR_RESPONSE_UNEXPECTED;
    }

    if (len < min_len) {
        wmc_err (0, "WMC command %d response not long enough (got %zu, expected "
                 "at least %zu).", cmd, len, min_len);
        return -WMC_ERROR_RESPONSE_BAD_LENGTH;
    }

    return 0;
}

/**********************************************************************/

/**
 * wmc_cmd_init_new:
 * @buf: buffer in which to store constructed command
 * @buflen: size of @buf
 * @wmc2: if %TRUE add additional data that later-model devices (UML290) want
 *
 * Returns: size of the constructed command on success, or 0 on failure
 */
size_t
wmc_cmd_init_new (char *buf, size_t buflen, int wmc2)
{
    wmc_return_val_if_fail (buf != NULL, 0);

    if (wmc2) {
        WmcCmdInit2 *cmd = (WmcCmdInit2 *) buf;
        const char data[] = { 0xda, 0x07, 0x0c, 0x00, 0x1e, 0x00, 0x09, 0x00, 0x39,
                              0x00, 0x18, 0x00, 0x04, 0x00 };

        wmc_return_val_if_fail (buflen >= sizeof (*cmd), 0);

        memset (cmd, 0, sizeof (*cmd));
        cmd->hdr.marker = WMC_CMD_MARKER;
        cmd->hdr.cmd = WMC_CMD_INIT;
        memcpy (cmd->_unknown1, data, sizeof (data));
        return sizeof (*cmd);
    } else {
        WmcCmdHeader *cmd = (WmcCmdHeader *) buf;

        wmc_return_val_if_fail (buflen >= sizeof (*cmd), 0);

        memset (cmd, 0, sizeof (*cmd));
        cmd->marker = WMC_CMD_MARKER;
        cmd->cmd = WMC_CMD_INIT;
        return sizeof (*cmd);
    }
}

WmcResult *
wmc_cmd_init_result (const char *buf, gsize buflen, int wmc2)
{
    g_return_val_if_fail (buf != NULL, NULL);

    if (wmc2) {
        if (check_command (buf, buflen, WMC_CMD_INIT, sizeof (WmcCmdInit2Rsp)) < 0)
            return NULL;
    } else {
        if (check_command (buf, buflen, WMC_CMD_INIT, sizeof (WmcCmdHeader)) < 0)
            return NULL;
    }

    return wmc_result_new ();
}

/**********************************************************************/

size_t
wmc_cmd_device_info_new (char *buf, size_t buflen)
{
    WmcCmdHeader *cmd = (WmcCmdHeader *) buf;

    wmc_return_val_if_fail (buf != NULL, 0);
    wmc_return_val_if_fail (buflen >= sizeof (*cmd), 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->marker = WMC_CMD_MARKER;
    cmd->cmd = WMC_CMD_DEVICE_INFO;
    return sizeof (*cmd);
}

WmcResult *
wmc_cmd_device_info_result (const char *buf, gsize buflen)
{
    WmcResult *r = NULL;
    WmcCmdDeviceInfoRsp *rsp = (WmcCmdDeviceInfoRsp *) buf;
    char tmp[65];

    g_return_val_if_fail (buf != NULL, NULL);

    if (check_command (buf, buflen, WMC_CMD_DEVICE_INFO, sizeof (WmcCmdDeviceInfoRsp)) < 0)
        return NULL;

    r = wmc_result_new ();

    /* Manf */
    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->manf) <= sizeof (tmp));
    memcpy (tmp, rsp->manf, sizeof (rsp->manf));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_MANUFACTURER, tmp);

    /* Model */
    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->model) <= sizeof (tmp));
    memcpy (tmp, rsp->model, sizeof (rsp->model));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_MODEL, tmp);

    /* Firmware revision */
    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->fwrev) <= sizeof (tmp));
    memcpy (tmp, rsp->fwrev, sizeof (rsp->fwrev));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_FW_REVISION, tmp);

    /* Hardware revision */
    memset (tmp, 0, sizeof (tmp));
    g_assert (sizeof (rsp->hwrev) <= sizeof (tmp));
    memcpy (tmp, rsp->hwrev, sizeof (rsp->hwrev));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_HW_REVISION, tmp);

    return r;
}

/**********************************************************************/

