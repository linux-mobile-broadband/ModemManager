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
check_command (const char *buf, gsize len, guint8 cmd, gsize min_len)
{
    if (len < 1) {
        wmc_err (0, "Zero-length response");
        return -WMC_ERROR_RESPONSE_BAD_LENGTH;
    }

    if (buf[0] != cmd) {
        wmc_err (0, "Unexpected WMC command response (expected %d, got %d)",
                 cmd, buf[0]);
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

size_t
wmc_cmd_device_info_new (char *buf, size_t buflen)
{
    WmcCmdHeader *cmd = (WmcCmdHeader *) buf;

    wmc_return_val_if_fail (buf != NULL, 0);
    wmc_return_val_if_fail (buflen >= sizeof (*cmd), 0);

    memset (cmd, 0, sizeof (*cmd));
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

