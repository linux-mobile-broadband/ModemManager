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
#include <time.h>

#include "commands.h"
#include "errors.h"
#include "result-private.h"
#include "utils.h"
#include "protocol.h"


/**********************************************************************/

static int
check_command (const char *buf, size_t len, u_int8_t cmd, size_t min_len)
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
        time_t now;
        struct tm *tm;

        wmc_return_val_if_fail (buflen >= sizeof (*cmd), 0);

        now = time (NULL);
        tm = localtime (&now);

        memset (cmd, 0, sizeof (*cmd));
        cmd->hdr.marker = WMC_CMD_MARKER;
        cmd->hdr.cmd = WMC_CMD_INIT;
        cmd->year = htole16 (tm->tm_year + 1900);
        cmd->month = tm->tm_mon + 1;
        cmd->day = htobe16 (tm->tm_mday);
        cmd->hours = htobe16 (tm->tm_hour);
        cmd->minutes = htobe16 (tm->tm_min);
        cmd->seconds = htobe16 (tm->tm_sec);
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
wmc_cmd_init_result (const char *buf, size_t buflen, int wmc2)
{
    wmc_return_val_if_fail (buf != NULL, NULL);

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
wmc_cmd_device_info_result (const char *buf, size_t buflen)
{
    WmcResult *r = NULL;
    WmcCmdDeviceInfoRsp *rsp = (WmcCmdDeviceInfoRsp *) buf;
    WmcCmdDeviceInfo2Rsp *rsp2 = (WmcCmdDeviceInfo2Rsp *) buf;
    WmcCmdDeviceInfo3Rsp *rsp3 = (WmcCmdDeviceInfo3Rsp *) buf;
    char tmp[65];

    wmc_return_val_if_fail (buf != NULL, NULL);

    if (check_command (buf, buflen, WMC_CMD_DEVICE_INFO, sizeof (WmcCmdDeviceInfo3Rsp)) < 0) {
        rsp3 = NULL;
        if (check_command (buf, buflen, WMC_CMD_DEVICE_INFO, sizeof (WmcCmdDeviceInfo2Rsp)) < 0) {
            rsp2 = NULL;
            if (check_command (buf, buflen, WMC_CMD_DEVICE_INFO, sizeof (WmcCmdDeviceInfoRsp)) < 0)
                return NULL;
        }
    }

    r = wmc_result_new ();

    /* Manf */
    memset (tmp, 0, sizeof (tmp));
    wmc_assert (sizeof (rsp->manf) <= sizeof (tmp));
    memcpy (tmp, rsp->manf, sizeof (rsp->manf));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_MANUFACTURER, tmp);

    /* Model */
    memset (tmp, 0, sizeof (tmp));
    wmc_assert (sizeof (rsp->model) <= sizeof (tmp));
    memcpy (tmp, rsp->model, sizeof (rsp->model));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_MODEL, tmp);

    /* Firmware revision */
    memset (tmp, 0, sizeof (tmp));
    wmc_assert (sizeof (rsp->fwrev) <= sizeof (tmp));
    memcpy (tmp, rsp->fwrev, sizeof (rsp->fwrev));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_FW_REVISION, tmp);

    /* Hardware revision */
    memset (tmp, 0, sizeof (tmp));
    wmc_assert (sizeof (rsp->hwrev) <= sizeof (tmp));
    memcpy (tmp, rsp->hwrev, sizeof (rsp->hwrev));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_HW_REVISION, tmp);

    /* MIN */
    memset (tmp, 0, sizeof (tmp));
    wmc_assert (sizeof (rsp->min) <= sizeof (tmp));
    memcpy (tmp, rsp->min, sizeof (rsp->min));
    wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_CDMA_MIN, tmp);

    wmc_result_add_u32 (r, WMC_CMD_DEVICE_INFO_ITEM_HOME_SID, le16toh (rsp->home_sid));
    wmc_result_add_u32 (r, WMC_CMD_DEVICE_INFO_ITEM_PRL_VERSION, le16toh (rsp->prlver));
    wmc_result_add_u32 (r, WMC_CMD_DEVICE_INFO_ITEM_ERI_VERSION, le16toh (rsp->eriver));

    if (rsp2) {
        /* MEID */
        memset (tmp, 0, sizeof (tmp));
        wmc_assert (sizeof (rsp2->meid) <= sizeof (tmp));
        memcpy (tmp, rsp2->meid, sizeof (rsp2->meid));
        wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_MEID, tmp);

        /* IMEI */
        memset (tmp, 0, sizeof (tmp));
        wmc_assert (sizeof (rsp2->imei) <= sizeof (tmp));
        memcpy (tmp, rsp2->imei, sizeof (rsp2->imei));
        wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_IMEI, tmp);

        /* IMSI */
        memset (tmp, 0, sizeof (tmp));
        wmc_assert (sizeof (rsp2->iccid) <= sizeof (tmp));
        memcpy (tmp, rsp2->iccid, sizeof (rsp2->iccid));
        wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_ICCID, tmp);
    }

    if (rsp3) {
        /* MCC */
        memset (tmp, 0, sizeof (tmp));
        wmc_assert (sizeof (rsp3->mcc) <= sizeof (tmp));
        memcpy (tmp, rsp3->mcc, sizeof (rsp3->mcc));
        wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_MCC, tmp);

        /* MNC */
        memset (tmp, 0, sizeof (tmp));
        wmc_assert (sizeof (rsp3->mnc) <= sizeof (tmp));
        memcpy (tmp, rsp3->mnc, sizeof (rsp3->mnc));
        wmc_result_add_string (r, WMC_CMD_DEVICE_INFO_ITEM_MNC, tmp);
    }

    return r;
}

/**********************************************************************/

size_t
wmc_cmd_network_info_new (char *buf, size_t buflen)
{
    WmcCmdHeader *cmd = (WmcCmdHeader *) buf;

    wmc_return_val_if_fail (buf != NULL, 0);
    wmc_return_val_if_fail (buflen >= sizeof (*cmd), 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->marker = WMC_CMD_MARKER;
    cmd->cmd = WMC_CMD_NET_INFO;
    return sizeof (*cmd);
}

static wmcbool
is_gsm_service (u_int8_t service)
{
    return (service == WMC_SERVICE_GSM || service == WMC_SERVICE_GPRS || service == WMC_SERVICE_EDGE);
}

static wmcbool
is_umts_service (u_int8_t service)
{
    return (service == WMC_SERVICE_UMTS || service == WMC_SERVICE_HSDPA
            || service == WMC_SERVICE_HSUPA || service == WMC_SERVICE_HSPA);
}

static wmcbool
is_cdma_service (u_int8_t service)
{
    return (service == WMC_SERVICE_IS95A || service == WMC_SERVICE_IS95B || service == WMC_SERVICE_1XRTT);
}

static wmcbool
is_evdo_service (u_int8_t service)
{
    return (service == WMC_SERVICE_EVDO_0 || service == WMC_SERVICE_EVDO_A || service == WMC_SERVICE_EVDO_A_EHRPD);
}

static wmcbool
is_lte_service (u_int8_t service)
{
    return (service == WMC_SERVICE_LTE);
}

static u_int8_t
sanitize_dbm (u_int8_t in_dbm, u_int8_t service)
{
    u_int8_t cutoff;

    /* 0x6A (-106 dBm) = no signal for GSM/GPRS/EDGE */
    /* 0x7D (-125 dBm) = no signal for everything else */
    cutoff = is_gsm_service (service) ? 0x6A : 0x7D;

    return in_dbm >= cutoff ? 0 : in_dbm;
}


WmcResult *
wmc_cmd_network_info_result (const char *buf, size_t buflen)
{
    WmcResult *r = NULL;
    WmcCmdNetworkInfoRsp *rsp = (WmcCmdNetworkInfoRsp *) buf;
    WmcCmdNetworkInfo2Rsp *rsp2 = (WmcCmdNetworkInfo2Rsp *) buf;
    WmcCmdNetworkInfo3Rsp *rsp3 = (WmcCmdNetworkInfo3Rsp *) buf;
    char tmp[65];
    int err;
    u_int32_t mccmnc = 0, mcc, mnc;

    wmc_return_val_if_fail (buf != NULL, NULL);

    err = check_command (buf, buflen, WMC_CMD_NET_INFO, sizeof (WmcCmdNetworkInfo3Rsp));
    if (err != WMC_SUCCESS) {
        if (err != -WMC_ERROR_RESPONSE_BAD_LENGTH)
            return NULL;
        rsp3 = NULL;

        err = check_command (buf, buflen, WMC_CMD_NET_INFO, sizeof (WmcCmdNetworkInfo2Rsp));
        if (err != WMC_SUCCESS) {
            if (err != -WMC_ERROR_RESPONSE_BAD_LENGTH)
                return NULL;
            rsp2 = NULL;

            err = check_command (buf, buflen, WMC_CMD_NET_INFO, sizeof (WmcCmdNetworkInfoRsp));
            if (err != WMC_SUCCESS)
                return NULL;
        }
    }

    r = wmc_result_new ();

    wmc_result_add_u8 (r, WMC_CMD_NETWORK_INFO_ITEM_SERVICE, rsp->service);

    if (rsp2) {
        wmc_result_add_u8 (r, WMC_CMD_NETWORK_INFO_ITEM_2G_DBM, sanitize_dbm (rsp2->two_g_dbm, rsp->service));
        wmc_result_add_u8 (r, WMC_CMD_NETWORK_INFO_ITEM_3G_DBM, sanitize_dbm (rsp2->three_g_dbm, WMC_SERVICE_NONE));

        memset (tmp, 0, sizeof (tmp));
        if (   (is_cdma_service (rsp->service) && sanitize_dbm (rsp2->two_g_dbm, rsp->service))
            || (is_evdo_service (rsp->service) && sanitize_dbm (rsp2->three_g_dbm, rsp->service))) {
            /* CDMA2000 operator name */
            wmc_assert (sizeof (rsp2->cdma_opname) <= sizeof (tmp));
            memcpy (tmp, rsp2->cdma_opname, sizeof (rsp2->cdma_opname));
            wmc_result_add_string (r, WMC_CMD_NETWORK_INFO_ITEM_OPNAME, tmp);
        } else {
            if (   (is_gsm_service (rsp->service) && sanitize_dbm (rsp2->two_g_dbm, rsp->service))
                || (is_umts_service (rsp->service) && sanitize_dbm (rsp2->three_g_dbm, rsp->service))) {
                /* GSM/UMTS operator name */
                wmc_assert (sizeof (rsp2->tgpp_opname) <= sizeof (tmp));
                memcpy (tmp, rsp2->tgpp_opname, sizeof (rsp2->tgpp_opname));
                wmc_result_add_string (r, WMC_CMD_NETWORK_INFO_ITEM_OPNAME, tmp);
            }
        }

        /* MCC/MNC */
        mccmnc = le32toh (rsp2->mcc_mnc);
        if (mccmnc < 100000)
            mccmnc *= 10;    /* account for possible 2-digit MNC */
        mcc = mccmnc / 1000;
        mnc = mccmnc - (mcc * 1000);

        if (mcc > 100) {
            memset (tmp, 0, sizeof (tmp));
            snprintf (tmp, sizeof (tmp), "%u", mccmnc / 1000);
            wmc_result_add_string (r, WMC_CMD_NETWORK_INFO_ITEM_MCC, tmp);

            memset (tmp, 0, sizeof (tmp));
            snprintf (tmp, sizeof (tmp), "%03u", mnc);
            wmc_result_add_string (r, WMC_CMD_NETWORK_INFO_ITEM_MNC, tmp);
        }
    } else {
        /* old format */
        wmc_result_add_u8 (r, WMC_CMD_NETWORK_INFO_ITEM_2G_DBM, sanitize_dbm (rsp->two_g_dbm, rsp->service));
    }

    if (rsp3) {
        wmc_result_add_u8 (r, WMC_CMD_NETWORK_INFO_ITEM_LTE_DBM, sanitize_dbm (rsp3->lte_dbm, WMC_SERVICE_NONE));

        memset (tmp, 0, sizeof (tmp));
        if (is_lte_service (rsp->service) && sanitize_dbm (rsp3->lte_dbm, rsp->service)) {
            /* LTE operator name */
            wmc_assert (sizeof (rsp2->tgpp_opname) <= sizeof (tmp));
            memcpy (tmp, rsp2->tgpp_opname, sizeof (rsp2->tgpp_opname));
            wmc_result_add_string (r, WMC_CMD_NETWORK_INFO_ITEM_OPNAME, tmp);
        }
    }

    return r;
}

/**********************************************************************/

size_t
wmc_cmd_get_global_mode_new (char *buf, size_t buflen)
{
    WmcCmdGetGlobalMode *cmd = (WmcCmdGetGlobalMode *) buf;

    wmc_return_val_if_fail (buf != NULL, 0);
    wmc_return_val_if_fail (buflen >= sizeof (*cmd), 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->hdr.marker = WMC_CMD_MARKER;
    cmd->hdr.cmd = WMC_CMD_GET_GLOBAL_MODE;
    return sizeof (*cmd);
}

WmcResult *
wmc_cmd_get_global_mode_result (const char *buf, size_t buflen)
{
    WmcResult *r = NULL;
    WmcCmdGetGlobalModeRsp *rsp = (WmcCmdGetGlobalModeRsp *) buf;

    wmc_return_val_if_fail (buf != NULL, NULL);

    if (check_command (buf, buflen, WMC_CMD_GET_GLOBAL_MODE, sizeof (WmcCmdGetGlobalModeRsp)) < 0)
        return NULL;

    r = wmc_result_new ();
    wmc_result_add_u8 (r, WMC_CMD_GET_GLOBAL_MODE_ITEM_MODE, rsp->mode);
    return r;
}

/**********************************************************************/

static wmcbool
validate_mode (u_int8_t mode)
{
    switch (mode) {
    case WMC_NETWORK_MODE_AUTO_CDMA:
    case WMC_NETWORK_MODE_CDMA_ONLY:
    case WMC_NETWORK_MODE_EVDO_ONLY:
    case WMC_NETWORK_MODE_AUTO_GSM:
    case WMC_NETWORK_MODE_GPRS_ONLY:
    case WMC_NETWORK_MODE_UMTS_ONLY:
    case WMC_NETWORK_MODE_AUTO:
    case WMC_NETWORK_MODE_LTE_ONLY:
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

size_t
wmc_cmd_set_global_mode_new (char *buf, size_t buflen, u_int8_t mode)
{
    WmcCmdSetGlobalMode *cmd = (WmcCmdSetGlobalMode *) buf;

    wmc_return_val_if_fail (buf != NULL, 0);
    wmc_return_val_if_fail (buflen >= sizeof (*cmd), 0);
    wmc_return_val_if_fail (validate_mode (mode) == TRUE, 0);

    memset (cmd, 0, sizeof (*cmd));
    cmd->hdr.marker = WMC_CMD_MARKER;
    cmd->hdr.cmd = WMC_CMD_SET_GLOBAL_MODE;
    cmd->_unknown1 = 0x01;
    cmd->mode = mode;
    cmd->_unknown2 = 0x05;
    cmd->_unknown3 = 0x00;
    return sizeof (*cmd);
}

WmcResult *
wmc_cmd_set_global_mode_result (const char *buf, size_t buflen)
{
    wmc_return_val_if_fail (buf != NULL, NULL);

    if (check_command (buf, buflen, WMC_CMD_SET_GLOBAL_MODE, sizeof (WmcCmdGetGlobalModeRsp)) < 0)
        return NULL;

    return wmc_result_new ();
}

/**********************************************************************/

