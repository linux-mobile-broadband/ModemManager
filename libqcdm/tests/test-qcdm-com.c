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

#include <glib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

#include "test-qcdm-com.h"
#include "com.h"
#include "utils.h"
#include "result.h"
#include "commands.h"
#include "errors.h"

/************************************************************/

static const char *
prev_to_string (guint8 prev)
{
    switch (prev) {
    case QCDM_CDMA_PREV_IS_95:
        return "1 (IS-95)";
    case QCDM_CDMA_PREV_IS_95A:
        return "2 (IS-95A)";
    case QCDM_CDMA_PREV_IS_95A_TSB74:
        return "3 (IS-95A TSB-74)";
    case QCDM_CDMA_PREV_IS_95B_PHASE1:
        return "4 (IS-95B Phase I)";
    case QCDM_CDMA_PREV_IS_95B_PHASE2:
        return "5 (IS-95B Phase II)";
    case QCDM_CDMA_PREV_IS2000_REL0:
        return "6 (IS-2000 Release 0)";
    case QCDM_CDMA_PREV_IS2000_RELA:
        return "7 (IS-2000 Release A)";
    default:
        break;
    }
    return "unknown";
}

static const char *
band_class_to_string (guint8 band_class)
{
    switch (band_class) {
    case QCDM_CDMA_BAND_CLASS_0_CELLULAR_800:
        return "0 (Cellular 800)";
    case QCDM_CDMA_BAND_CLASS_1_PCS:
        return "1 (PCS 1900)";
    case QCDM_CDMA_BAND_CLASS_2_TACS:
        return "2 (TACS)";
    case QCDM_CDMA_BAND_CLASS_3_JTACS:
        return "3 (JTACS)";
    case QCDM_CDMA_BAND_CLASS_4_KOREAN_PCS:
        return "4 (Korean PCS)";
    case QCDM_CDMA_BAND_CLASS_5_NMT450:
        return "5 (NMT-450)";
    case QCDM_CDMA_BAND_CLASS_6_IMT2000:
        return "6 (IMT-2000)";
    case QCDM_CDMA_BAND_CLASS_7_CELLULAR_700:
        return "7 (Cellular 700)";
    case QCDM_CDMA_BAND_CLASS_8_1800:
        return "8 (1800 MHz)";
    case QCDM_CDMA_BAND_CLASS_9_900:
        return "9 (1900 MHz)";
    case QCDM_CDMA_BAND_CLASS_10_SECONDARY_800:
        return "10 (Secondary 800 MHz)";
    case QCDM_CDMA_BAND_CLASS_11_PAMR_400:
        return "11 (PAMR 400)";
    case QCDM_CDMA_BAND_CLASS_12_PAMR_800:
        return "11 (PAMR 800)";
    default:
        break;
    }
    return "unknown";
}

static const char *
hdr_rev_to_string (guint8 hdr_rev)
{
    switch (hdr_rev) {
    case QCDM_HDR_REV_0:
        return "0";
    case QCDM_HDR_REV_A:
        return "A";
    default:
        break;
    }
    return "unknown";
}

static const char *
status_snapshot_state_to_string (guint8 state)
{
    switch (state) {
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_NO_SERVICE:
        return "no service";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_INITIALIZATION:
        return "initialization";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_IDLE:
        return "idle";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_VOICE_CHANNEL_INIT:
        return "voice channel init";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_WAITING_FOR_ORDER:
        return "waiting for order";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_WAITING_FOR_ANSWER:
        return "waiting for answer";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_CONVERSATION:
        return "conversation";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_RELEASE:
        return "release";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_SYSTEM_ACCESS:
        return "system access";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_OFFLINE_CDMA:
        return "offline CDMA";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_OFFLINE_HDR:
        return "offline HDR";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_OFFLINE_ANALOG:
        return "offline analog";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_RESET:
        return "reset";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_POWER_DOWN:
        return "power down";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_POWER_SAVE:
        return "power save";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_POWER_UP:
        return "power up";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_LOW_POWER_MODE:
        return "low power mode";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_SEARCHER_DSMM:
        return "searcher DSMM";
    case QCDM_CMD_STATUS_SNAPSHOT_STATE_HDR:
        return "HDR";
    default:
        break;
    }
    return "unknown";
}

static const char *
cm_call_state_to_string (uint32_t state)
{
    switch (state) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_IDLE:
        return "idle";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_ORIGINATING:
        return "originating";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_ALERTING:
        return "alerting";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_ORIGINATION_ALERTING:
        return "originating alerting";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_CALL_STATE_CONVERSATION:
        return "conversation";
    default:
        break;
    }
    return "unknown";
}

static const char *
cm_system_mode_to_string (uint32_t mode)
{
    switch (mode) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_NO_SERVICE:
        return "no service";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_AMPS:
        return "AMPS";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_CDMA:
        return "CDMA";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GSM:
        return "GSM";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_HDR:
        return "HDR/EVDO";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_WCDMA:
        return "WCDMA";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_GW:
        return "GSM/WCDMA";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_WLAN:
        return "WLAN";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_LTE:
        return "LTE";
    default:
        break;
    }

    return "unknown";
}

/************************************************************/

typedef struct {
    char *port;
    int fd;
    struct termios old_t;
    gboolean debug;
} TestComData;

gpointer
test_com_setup (const char *port)
{
	TestComData *d;
	int ret;

	d = g_malloc0 (sizeof (TestComData));
	g_assert (d);

    if (getenv ("SERIAL_DEBUG"))
        d->debug = TRUE;

    errno = 0;
    d->fd = open (port, O_RDWR | O_EXCL | O_NONBLOCK | O_NOCTTY);
    if (d->fd < 0)
        g_warning ("%s: open failed: (%d) %s", port, errno, strerror (errno));
    g_assert (d->fd >= 0);

    ret = ioctl (d->fd, TIOCEXCL);
    if (ret) {
        g_warning ("%s: lock failed: (%d) %s", port, errno, strerror (errno));
        close (d->fd);
        d->fd = -1;
    }
    g_assert (ret == 0);

    ret = ioctl (d->fd, TCGETA, &d->old_t);
    if (ret) {
        g_warning ("%s: old termios failed: (%d) %s", port, errno, strerror (errno));
        close (d->fd);
        d->fd = -1;
    }
    g_assert (ret == 0);

    d->port = g_strdup (port);
    return d;
}

void
test_com_teardown (gpointer user_data)
{
    TestComData *d = user_data;

    g_assert (d);

    g_free (d->port);
    close (d->fd);
    g_free (d);
}

static void
print_buf (const char *detail, const char *buf, gsize len)
{
    unsigned int i = 0;
    gboolean newline = FALSE;

    g_print ("%s (%zu)  ", detail, len);
    for (i = 0; i < len; i++) {
        g_print ("0x%02x ", buf[i] & 0xFF);
        if (((i + 1) % 12) == 0) {
            g_print ("\n");
            newline = TRUE;
        } else
            newline = FALSE;
    }

    if (!newline)
        g_print ("\n");
}

static gboolean
send_command (TestComData *d, char *buf, gsize len)
{
    int status;
    int eagain_count = 1000;
    gsize i = 0;

    if (d->debug)
        print_buf (">>>", buf, len);

    while (i < len) {
        errno = 0;
        status = write (d->fd, &buf[i], 1);
        if (status < 0) {
            if (errno == EAGAIN) {
                eagain_count--;
                if (eagain_count <= 0)
                    return FALSE;
            } else
                g_assert (errno == 0);
        } else
            i++;

        usleep (1000);
    }

    return TRUE;
}

static gsize
wait_reply (TestComData *d, char *buf, gsize len)
{
    fd_set in;
    int result;
    struct timeval timeout = { 1, 0 };
    char readbuf[1024];
    ssize_t bytes_read;
    unsigned int total = 0, retries = 0;
    gsize decap_len = 0;

    FD_ZERO (&in);
    FD_SET (d->fd, &in);
    result = select (d->fd + 1, &in, NULL, NULL, &timeout);
    if (result != 1 || !FD_ISSET (d->fd, &in))
        return 0;

    do {
        errno = 0;
        bytes_read = read (d->fd, &readbuf[total], 1);
        if ((bytes_read == 0) || (errno == EAGAIN)) {
            /* Haven't gotten the async control char yet */
            if (retries > 20)
                return 0; /* 2 seconds, give up */

            /* Otherwise wait a bit and try again */
            usleep (100000);
            retries++;
            continue;
        } else if (bytes_read == 1) {
            qcdmbool more = FALSE;
            gboolean success;
            gsize used = 0;

            total++;
            decap_len = 0;
            success = dm_decapsulate_buffer (readbuf, total, buf, len, &decap_len, &used, &more);

            /* Discard used data */
            if (used > 0) {
                total -= used;
                memmove (readbuf, &readbuf[used], total);
            }

            if (success && !more) {
                /* Success; we have a packet */
                break;
            }
        } else {
            /* Some error occurred */
            return 0;
        }
    } while (total < sizeof (readbuf));

    if (d->debug) {
        print_buf ("<<<", readbuf, total);
        print_buf ("D<<", buf, decap_len);
    }

    return decap_len;
}

void
test_com_port_init (void *f, void *data)
{
    TestComData *d = data;
    int err;

    err = qcdm_port_setup (d->fd);
    if (err != QCDM_SUCCESS)
        g_warning ("%s: error setting up port: %d", d->port, err);
    g_assert (err == QCDM_SUCCESS);
}

void
test_com_version_info (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    char buf[512];
    const char *str;
    gint len;
    QcdmResult *result;
    gsize reply_len;

    len = qcdm_cmd_version_info_new (buf, sizeof (buf));
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_version_info_result (buf, reply_len, NULL);
    g_assert (result);

    g_print ("\n");

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_VERSION_INFO_ITEM_COMP_DATE, &str);
    g_message ("%s: Compiled Date: %s", __func__, str);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_VERSION_INFO_ITEM_COMP_TIME, &str);
    g_message ("%s: Compiled Time: %s", __func__, str);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_VERSION_INFO_ITEM_RELEASE_DATE, &str);
    g_message ("%s: Release Date: %s", __func__, str);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_VERSION_INFO_ITEM_RELEASE_TIME, &str);
    g_message ("%s: Release Time: %s", __func__, str);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_VERSION_INFO_ITEM_MODEL, &str);
    g_message ("%s: Model: %s", __func__, str);

    qcdm_result_unref (result);
}

void
test_com_esn (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    char buf[512];
    const char *str;
    gint len;
    QcdmResult *result;
    gsize reply_len;

    len = qcdm_cmd_esn_new (buf, sizeof (buf));
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_esn_result (buf, reply_len, NULL);
    g_assert (result);

    g_print ("\n");

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_ESN_ITEM_ESN, &str);
    g_message ("%s: ESN: %s", __func__, str);

    qcdm_result_unref (result);
}

void
test_com_mdn (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    char buf[512];
    const char *str;
    gint len;
    QcdmResult *result;
    gsize reply_len;
    int err = QCDM_SUCCESS;

    len = qcdm_cmd_nv_get_mdn_new (buf, sizeof (buf), 0);
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_mdn_result (buf, reply_len, &err);
    if (!result) {
        if (   err == -QCDM_ERROR_NVCMD_FAILED
            || err == -QCDM_ERROR_RESPONSE_BAD_PARAMETER
            || err == -QCDM_ERROR_NV_ERROR_INACTIVE
            || err == -QCDM_ERROR_NV_ERROR_BAD_PARAMETER)
            return;
        g_assert_cmpint (err, ==, QCDM_SUCCESS);
    }

    g_print ("\n");

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_NV_GET_MDN_ITEM_MDN, &str);
    g_message ("%s: MDN: %s", __func__, str);

    qcdm_result_unref (result);
}

void
test_com_read_roam_pref (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[512];
    guint8 pref;
    const char *msg;
    gint len;
    QcdmResult *result;
    gsize reply_len;

    len = qcdm_cmd_nv_get_roam_pref_new (buf, sizeof (buf), 0);
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_roam_pref_result (buf, reply_len, &err);
    if (!result) {
        if (   err == -QCDM_ERROR_NVCMD_FAILED
            || err == -QCDM_ERROR_RESPONSE_BAD_PARAMETER
            || err == -QCDM_ERROR_NV_ERROR_INACTIVE
            || err == -QCDM_ERROR_NV_ERROR_BAD_PARAMETER)
            return;
        g_assert_cmpint (err, ==, QCDM_SUCCESS);
    }
    g_assert (result);

    g_print ("\n");

    err = qcdm_result_get_u8 (result, QCDM_CMD_NV_GET_ROAM_PREF_ITEM_ROAM_PREF, &pref);
    g_assert_cmpint (err, ==, QCDM_SUCCESS);

    switch (pref) {
    case QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_HOME_ONLY:
        msg = "home only";
        break;
    case QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_ROAM_ONLY:
        msg = "roaming only";
        break;
    case QCDM_CMD_NV_ROAM_PREF_ITEM_ROAM_PREF_AUTO:
        msg = "automatic";
        break;
    default:
        g_assert_not_reached ();
    }
    g_message ("%s: Roam preference: 0x%02X (%s)", __func__, pref, msg);

    qcdm_result_unref (result);
}

void
test_com_read_mode_pref (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[512];
    guint8 pref;
    const char *msg;
    gint len;
    QcdmResult *result;
    gsize reply_len;

    len = qcdm_cmd_nv_get_mode_pref_new (buf, sizeof (buf), 0);
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_mode_pref_result (buf, reply_len, &err);
    if (!result) {
        if (   err == -QCDM_ERROR_NVCMD_FAILED
            || err == -QCDM_ERROR_RESPONSE_BAD_PARAMETER
            || err == -QCDM_ERROR_NV_ERROR_INACTIVE
            || err == -QCDM_ERROR_NV_ERROR_BAD_PARAMETER)
            return;
        g_assert_cmpint (err, ==, QCDM_SUCCESS);
    }

    g_print ("\n");

    err = qcdm_result_get_u8 (result, QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF, &pref);
    g_assert_cmpint (err, ==, QCDM_SUCCESS);

    switch (pref) {
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_DIGITAL:
        msg = "digital";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_DIGITAL_ONLY:
        msg = "digital only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_AUTO:
        msg = "automatic";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_ONLY:
        msg = "CDMA 1x only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_HDR_ONLY:
        msg = "HDR only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GPRS_ONLY:
        msg = "GPRS only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_UMTS_ONLY:
        msg = "UMTS only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_ONLY:
        msg = "GSM and UMTS only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_ONLY:
        msg = "CDMA 1x and HDR only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_LTE_ONLY:
        msg = "LTE only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_LTE_ONLY:
        msg = "GSM/UMTS/LTE only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_LTE_ONLY:
        msg = "CDMA 1x, HDR, and LTE only";
        break;
    default:
        msg = "unknown";
        break;
    }
    g_message ("%s: Mode preference: 0x%02X (%s)", __func__, pref, msg);

    qcdm_result_unref (result);
}

void
test_com_read_hybrid_pref (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[512];
    guint8 pref;
    gint len;
    QcdmResult *result;
    gsize reply_len;

    len = qcdm_cmd_nv_get_hybrid_pref_new (buf, sizeof (buf));
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_hybrid_pref_result (buf, reply_len, &err);
    if (!result) {
        if (   err == -QCDM_ERROR_NVCMD_FAILED
            || err == -QCDM_ERROR_RESPONSE_BAD_PARAMETER
            || err == -QCDM_ERROR_NV_ERROR_INACTIVE
            || err == -QCDM_ERROR_NV_ERROR_BAD_PARAMETER)
            return;
        g_assert_cmpint (err, ==, QCDM_SUCCESS);
    }

    g_print ("\n");

    err = qcdm_result_get_u8 (result, QCDM_CMD_NV_GET_HYBRID_PREF_ITEM_HYBRID_PREF, &pref);
    g_assert_cmpint (err, ==, QCDM_SUCCESS);
    g_message ("%s: Hybrid preference: 0x%02X", __func__, pref);

    qcdm_result_unref (result);
}

void
test_com_read_ipv6_enabled (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[512];
    guint8 pref;
    const char *msg;
    gint len;
    QcdmResult *result;
    gsize reply_len;

    len = qcdm_cmd_nv_get_ipv6_enabled_new (buf, sizeof (buf));
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_ipv6_enabled_result (buf, reply_len, &err);
    if (!result) {
        if (   err == -QCDM_ERROR_NVCMD_FAILED
            || err == -QCDM_ERROR_RESPONSE_BAD_PARAMETER
            || err == -QCDM_ERROR_NV_ERROR_INACTIVE
            || err == -QCDM_ERROR_NV_ERROR_BAD_PARAMETER)
            return;
        g_assert_cmpint (err, ==, QCDM_SUCCESS);
    }

    g_print ("\n");

    err = qcdm_result_get_u8 (result, QCDM_CMD_NV_GET_IPV6_ENABLED_ITEM_ENABLED, &pref);
    g_assert_cmpint (err, ==, QCDM_SUCCESS);

    switch (pref) {
    case QCDM_CMD_NV_IPV6_ENABLED_OFF:
        msg = "disabled";
        break;
    case QCDM_CMD_NV_IPV6_ENABLED_ON:
        msg = "enabled";
        break;
    default:
        msg = "unknown";
        break;
    }
    g_message ("%s: IPv6 preference: 0x%02X (%s)", __func__, pref, msg);

    qcdm_result_unref (result);
}

void
test_com_read_hdr_rev_pref (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[512];
    guint8 pref;
    const char *msg;
    gint len;
    QcdmResult *result;
    gsize reply_len;

    len = qcdm_cmd_nv_get_hdr_rev_pref_new (buf, sizeof (buf));
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_hdr_rev_pref_result (buf, reply_len, &err);
    if (!result) {
        if (   err == -QCDM_ERROR_NVCMD_FAILED
            || err == -QCDM_ERROR_RESPONSE_BAD_PARAMETER
            || err == -QCDM_ERROR_NV_ERROR_INACTIVE
            || err == -QCDM_ERROR_NV_ERROR_BAD_PARAMETER)
            return;
        g_assert_cmpint (err, ==, QCDM_SUCCESS);
    }

    g_print ("\n");

    err = qcdm_result_get_u8 (result, QCDM_CMD_NV_GET_HDR_REV_PREF_ITEM_REV_PREF, &pref);
    g_assert_cmpint (err, ==, QCDM_SUCCESS);

    switch (pref) {
    case QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_0:
        msg = "rev0";
        break;
    case QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A:
        msg = "revA";
        break;
    case QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD:
        msg = "eHRPD";
        break;
    default:
        msg = "unknown";
        break;
    }
    g_message ("%s: HDR rev preference: 0x%02X (%s)", __func__, pref, msg);

    qcdm_result_unref (result);
}

void
test_com_status (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[100];
    const char *str, *detail;
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint32 n32;
    guint8 n8;

    len = qcdm_cmd_cdma_status_new (buf, sizeof (buf));
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_cdma_status_result (buf, reply_len, &err);
    if (!result) {
        /* WCDMA/GSM devices don't implement this command */
        g_assert_cmpint (err, ==, -QCDM_ERROR_RESPONSE_BAD_COMMAND);
        return;
    }
    g_assert (result);

    g_print ("\n");

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_CDMA_STATUS_ITEM_ESN, &str);
    g_message ("%s: ESN: %s", __func__, str);

    n32 = 0;
    detail = NULL;
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RF_MODE, &n32);
    switch (n32) {
    case QCDM_CMD_CDMA_STATUS_RF_MODE_ANALOG:
        detail = "analog";
        break;
    case QCDM_CMD_CDMA_STATUS_RF_MODE_CDMA_CELLULAR:
        detail = "CDMA cellular";
        break;
    case QCDM_CMD_CDMA_STATUS_RF_MODE_CDMA_PCS:
        detail = "CDMA PCS";
        break;
    case QCDM_CMD_CDMA_STATUS_RF_MODE_SLEEP:
        detail = "sleep";
        break;
    case QCDM_CMD_CDMA_STATUS_RF_MODE_GPS:
        detail = "GPS";
        break;
    case QCDM_CMD_CDMA_STATUS_RF_MODE_HDR:
        detail = "HDR";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: CDMA RF Mode: %u (%s)", __func__, n32, detail);

    n32 = 0;
    detail = NULL;
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE, &n32);
    switch (n32) {
    case QCDM_CMD_CDMA_STATUS_RX_STATE_ENTERING_CDMA:
        detail = "entering CDMA";
        break;
    case QCDM_CMD_CDMA_STATUS_RX_STATE_SYNC_CHANNEL:
        detail = "sync channel";
        break;
    case QCDM_CMD_CDMA_STATUS_RX_STATE_PAGING_CHANNEL:
        detail = "paging channel";
        break;
    case QCDM_CMD_CDMA_STATUS_RX_STATE_TRAFFIC_CHANNEL_INIT:
        detail = "traffic channel init";
        break;
    case QCDM_CMD_CDMA_STATUS_RX_STATE_TRAFFIC_CHANNEL:
        detail = "traffic channel";
        break;
    case QCDM_CMD_CDMA_STATUS_RX_STATE_EXITING_CDMA:
        detail = "exiting CDMA";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: CDMA RX State: %u (%s)", __func__, n32, detail);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_ENTRY_REASON, &n32);
    g_message ("%s: Entry Reason: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_CURRENT_CHANNEL, &n32);
    g_message ("%s: Current Channel: %u", __func__, n32);

    n8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_CDMA_STATUS_ITEM_CODE_CHANNEL, &n8);
    g_message ("%s: Code Channel: %u", __func__, n8);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_PILOT_BASE, &n32);
    g_message ("%s: Pilot Base: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_SID, &n32);
    g_message ("%s: CDMA System ID: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CDMA_STATUS_ITEM_NID, &n32);
    g_message ("%s: CDMA Network ID: %u", __func__, n32);

    qcdm_result_unref (result);
}

void
test_com_sw_version (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[100];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    const char *str;

    len = qcdm_cmd_sw_version_new (buf, sizeof (buf));
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_sw_version_result (buf, reply_len, &err);
    if (!result) {
        g_assert_cmpint (err, ==, -QCDM_ERROR_RESPONSE_BAD_COMMAND);
        return;
    }

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_SW_VERSION_ITEM_VERSION, &str);
    g_message ("%s: SW Version: %s", __func__, str);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_SW_VERSION_ITEM_COMP_DATE, &str);
    g_message ("%s: Compiled Date: %s", __func__, str);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_SW_VERSION_ITEM_COMP_TIME, &str);
    g_message ("%s: Compiled Time: %s", __func__, str);

    qcdm_result_unref (result);
}

void
test_com_status_snapshot (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[100];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint8 n8;
    guint32 n32;

    len = qcdm_cmd_status_snapshot_new (buf, sizeof (buf));
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_status_snapshot_result (buf, reply_len, &err);
    if (!result) {
        /* WCDMA/GSM devices don't implement this command */
        g_assert_cmpint (err, ==, -QCDM_ERROR_RESPONSE_BAD_COMMAND);
        return;
    }
    g_assert (result);

    g_print ("\n");

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_HOME_MCC, &n32);
    g_message ("%s: Home MCC: %d", __func__, n32);

    n8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_BAND_CLASS, &n8);
    g_message ("%s: Band Class: %s", __func__, band_class_to_string (n8));

    n8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_BASE_STATION_PREV, &n8);
    g_message ("%s: Base station P_REV: %s", __func__, prev_to_string (n8));

    n8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_MOBILE_PREV, &n8);
    g_message ("%s: Mobile P_REV: %s", __func__, prev_to_string (n8));

    n8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_PREV_IN_USE, &n8);
    g_message ("%s: P_REV in-use: %s", __func__, prev_to_string (n8));

    n8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_STATUS_SNAPSHOT_ITEM_STATE, &n8);
    g_message ("%s: State: %d (%s)", __func__, n8, status_snapshot_state_to_string (n8));

    qcdm_result_unref (result);
}

void
test_com_pilot_sets (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[256];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint32 num, i;

    len = qcdm_cmd_pilot_sets_new (buf, sizeof (buf));
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_pilot_sets_result (buf, reply_len, &err);
    if (!result) {
        /* WCDMA/GSM devices don't implement this command */
        g_assert_cmpint (err, ==, -QCDM_ERROR_RESPONSE_BAD_COMMAND);
        return;
    }
    g_assert (result);

    num = 0;
    qcdm_cmd_pilot_sets_result_get_num (result, QCDM_CMD_PILOT_SETS_TYPE_ACTIVE, &num);
    g_message ("%s: Active Pilots: %d", __func__, num);
    for (i = 0; i < num; i++) {
        guint32 pn_offset = 0, ecio = 0;
        float db = 0;

        qcdm_cmd_pilot_sets_result_get_pilot (result,
                                              QCDM_CMD_PILOT_SETS_TYPE_ACTIVE,
                                              i,
                                              &pn_offset,
                                              &ecio,
                                              &db);
        g_message ("   %d: PN offset %d", i, pn_offset);
        g_message ("      EC/IO     %d  (%.1lf dB)", ecio, (double)db);
    }

    num = 0;
    qcdm_cmd_pilot_sets_result_get_num (result, QCDM_CMD_PILOT_SETS_TYPE_CANDIDATE, &num);
    g_message ("%s: Candidate Pilots: %d", __func__, num);

    num = 0;
    qcdm_cmd_pilot_sets_result_get_num (result, QCDM_CMD_PILOT_SETS_TYPE_NEIGHBOR, &num);
    g_message ("%s: Neighbor Pilots: %d", __func__, num);

    qcdm_result_unref (result);
}

static const char *
operating_mode_to_string (guint32 mode)
{
    switch (mode) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_POWER_OFF:
        return "powering off";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_FIELD_TEST_MODE:
        return "field test mode";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_OFFLINE:
        return "offline";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_OFFLINE_AMPS:
        return "online (AMPS)";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_OFFLINE_CDMA:
        return "online (CDMA)";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_ONLINE:
        return "online";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_LOW_POWER_MODE:
        return "low power mode";
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_RESET:
        return "reset";
    default:
        return "unknown";
    }
}

void
test_com_cm_subsys_state_info (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[100];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint32 n32;
    const char *detail;

    len = qcdm_cmd_cm_subsys_state_info_new (buf, sizeof (buf));
    g_assert (len == 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_cm_subsys_state_info_result (buf, reply_len, &err);
    g_assert (result);

    g_print ("\n");

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_CALL_STATE, &n32);
    g_message ("%s: Call State: %u (%s)", __func__, n32, cm_call_state_to_string (n32));

    n32 = 0;
    detail = NULL;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE, &n32);
    g_message ("%s: Operating Mode: %u (%s)", __func__, n32, operating_mode_to_string (n32));

    n32 = 0;
    detail = NULL;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE, &n32);
    g_message ("%s: System Mode: %u (%s)", __func__, n32, cm_system_mode_to_string (n32));

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_MODE_PREF, &n32);
    switch (n32) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_AMPS_ONLY:
        detail = "AMPS only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_DIGITAL_ONLY:
        detail = "digital only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_AUTO:
        detail = "automatic";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_EMERGENCY:
        detail = "emergency";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_1X_ONLY:
        detail = "1X only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_HDR_ONLY:
        detail = "HDR only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_1X_AMPS_ONLY:
        detail = "1x/AMPS only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_GPS_ONLY:
        detail = "GPS only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_GSM_ONLY:
        detail = "GSM only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_WCDMA_ONLY:
        detail = "WCDMA only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_LTE_ONLY:
        detail = "LTE only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_GSM_WCDMA_LTE_ONLY:
        detail = "GSM/WCDMA/LTE only";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Mode Preference: 0x%02X (%s)", __func__, n32 & 0xFF, detail);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_BAND_PREF, &n32);
    g_message ("%s: Band Preference: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF, &n32);
    switch (n32) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_HOME_ONLY:
        detail = "home only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_ROAM_ONLY:
        detail = "roaming only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_ROAM_PREF_AUTO:
        detail = "automatic";
        break;
    default:
        g_assert_not_reached ();
    }
    g_message ("%s: Roam Preference: 0x%02X (%s)", __func__, n32 & 0xFF, detail);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SERVICE_DOMAIN_PREF, &n32);
    g_message ("%s: Service Domain Preference: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ACQ_ORDER_PREF, &n32);
    g_message ("%s: Acquisition Order Preference: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_HYBRID_PREF, &n32);
    g_message ("%s: Hybrid Preference: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_NETWORK_SELECTION_PREF, &n32);
    g_message ("%s: Network Selection Preference: %u", __func__, n32);

    qcdm_result_unref (result);
}

void
test_com_hdr_subsys_state_info (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[100];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint8 num;
    const char *detail;

    len = qcdm_cmd_hdr_subsys_state_info_new (buf, sizeof (buf));
    g_assert (len == 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_hdr_subsys_state_info_result (buf, reply_len, &err);
    if (!result) {
        /* 1x-only devices won't implement the HDR subsystem of course */
        g_assert_cmpint (err, ==, -QCDM_ERROR_RESPONSE_BAD_COMMAND);
        g_message ("%s: device does not implement the HDR subsystem", __func__);
        return;
    }
    g_assert (result);

    num = 0;
    detail = NULL;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_AT_STATE, &num);
    switch (num) {
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_INACTIVE:
        detail = "inactive";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_ACQUISITION:
        detail = "acquisition";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_SYNC:
        detail = "sync";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_IDLE:
        detail = "idle";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_ACCESS:
        detail = "access";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_AT_STATE_CONNECTED:
        detail = "connected";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: AT State: %u (%s)", __func__, num, detail);

    num = 0;
    detail = NULL;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE, &num);
    switch (num) {
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSED:
        detail = "closed";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_SETUP:
        detail = "setup";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_AT_INIT:
        detail = "AT init";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_AN_INIT:
        detail = "AN init";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_OPEN:
        detail = "open";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSING:
        detail = "closing";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Session State: %u (%s)", __func__, num, detail);

    num = 0;
    detail = NULL;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE, &num);
    switch (num) {
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INACTIVE:
        detail = "inactive";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INIT:
        detail = "init";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_IDLE:
        detail = "idle";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_CONNECTED:
        detail = "connected";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: ALMP State: %u (%s)", __func__, num, detail);

    num = 0;
    detail = NULL;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_INIT_STATE, &num);
    switch (num) {
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_INACTIVE:
        detail = "inactive";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_NET_DETERMINE:
        detail = "searching";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_ACQUISITION:
        detail = "acquisition";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_INIT_STATE_SYNC:
        detail = "sync";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Init State: %u (%s)", __func__, num, detail);

    num = 0;
    detail = NULL;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_IDLE_STATE, &num);
    switch (num) {
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_INACTIVE:
        detail = "inactive";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_SLEEP:
        detail = "sleep";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_MONITOR:
        detail = "monitor";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_IDLE_STATE_SETUP:
        detail = "setup";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Idle State: %u (%s)", __func__, num, detail);

    num = 0;
    detail = NULL;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_CONNECTED_STATE, &num);
    switch (num) {
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_INACTIVE:
        detail = "inactive";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_OPEN:
        detail = "open";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_CONNECTED_STATE_CLOSING:
        detail = "closing";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Connected State: %u (%s)", __func__, num, detail);

    num = 0;
    detail = NULL;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ROUTE_UPDATE_STATE, &num);
    switch (num) {
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_INACTIVE:
        detail = "inactive";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_IDLE:
        detail = "idle";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_ROUTE_UPDATE_STATE_CONNECTED:
        detail = "connected";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Route Update State: %u (%s)", __func__, num, detail);

    num = 0;
    detail = NULL;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_OVERHEAD_MSG_STATE, &num);
    switch (num) {
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_INIT:
        detail = "initial";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_INACTIVE:
        detail = "inactive";
        break;
    case QCDM_CMD_HDR_SUBSYS_STATE_INFO_OVERHEAD_MSG_STATE_ACTIVE:
        detail = "active";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Overhead Msg State: %u (%s)", __func__, num, detail);

    num = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_HDR_HYBRID_MODE, &num);
    g_message ("%s: HDR Hybrid Mode: %u", __func__, num);

    qcdm_result_unref (result);
}

void
test_com_ext_logmask (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[520];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    uint32_t items[] = { 0x002C, 0x002E, 0 };
    guint32 maxlog = 0;

    /* First get # of items the device supports */
    len = qcdm_cmd_ext_logmask_new (buf, sizeof (buf), NULL, 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_ext_logmask_result (buf, reply_len, &err);
    g_assert (result);

    qcdm_result_get_u32 (result, QCDM_CMD_EXT_LOGMASK_ITEM_MAX_ITEMS, &maxlog);
    g_message ("%s: Max # Log Items: %u (0x%X)", __func__, maxlog, maxlog);

    qcdm_result_unref (result);

    /* Now enable some log items */
    len = qcdm_cmd_ext_logmask_new (buf, sizeof (buf), items, (uint16_t) maxlog);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_ext_logmask_result (buf, reply_len, &err);
    g_assert (result);

    qcdm_result_unref (result);

    /* Wait for a log packet */
    reply_len = wait_reply (d, buf, sizeof (buf));
}

void
test_com_event_report (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[520];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint32 i;

    /* Turn event reporting on */
    len = qcdm_cmd_event_report_new (buf, sizeof (buf), TRUE);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_event_report_result (buf, reply_len, &err);
    g_assert (result);

    qcdm_result_unref (result);

    /* Wait for a few events */
    for (i = 0; i < 4; i++)
        reply_len = wait_reply (d, buf, sizeof (buf));

    /* Turn event reporting off */
    len = qcdm_cmd_event_report_new (buf, sizeof (buf), FALSE);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));
}

void
test_com_log_config (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[520];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    uint32_t num_items = 0;
    const uint16_t *items = NULL, *reread_items;
    size_t items_len = 0, reread_len;
    uint32_t i;
    uint16_t test_items[] = { 0x1004, 0x1005, 0x1006, 0x1007, 0x1008, 0x102C, 0x102E, 0 };

    /* Get existing mask for CDMA/EVDO equip ID */
    len = qcdm_cmd_log_config_get_mask_new (buf, sizeof (buf), 0x01);
    g_assert (len);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_log_config_get_mask_result (buf, reply_len, &err);
    g_assert (result);

    qcdm_result_get_u32 (result, QCDM_CMD_LOG_CONFIG_MASK_ITEM_NUM_ITEMS, &num_items);
    g_message ("%s: Num Log Items: %u (0x%X)", __func__, num_items, num_items);

    qcdm_result_get_u16_array (result, QCDM_CMD_LOG_CONFIG_MASK_ITEM_ITEMS,
                               &items, &items_len);
    for (i = 0; i < items_len; i++)
        g_message ("%s:    Enabled: 0x%04x", __func__, items[i]);

    qcdm_result_unref (result);

    /* Turn on some log messages */
    len = qcdm_cmd_log_config_set_mask_new (buf, sizeof (buf), 0x01, test_items);
    g_assert (len);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_log_config_set_mask_result (buf, reply_len, &err);
    g_assert (result);

    qcdm_result_unref (result);

    /* Get the mask again so we can compare it to what we just set */
    len = qcdm_cmd_log_config_get_mask_new (buf, sizeof (buf), 0x01);
    g_assert (len);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_log_config_get_mask_result (buf, reply_len, &err);
    g_assert (result);

    qcdm_result_get_u16_array (result, QCDM_CMD_LOG_CONFIG_MASK_ITEM_ITEMS,
                               &reread_items, &reread_len);
    g_assert_cmpint (reread_len, ==, (sizeof (test_items) - 1) / sizeof (test_items[0]));
    g_assert (memcmp (reread_items, test_items, reread_len * sizeof (test_items[0])) == 0);

    qcdm_result_unref (result);

    /* Wait for a few log packets */
    for (i = 0; i < 5; i++)
        reply_len = wait_reply (d, buf, sizeof (buf));
}

void
test_com_zte_subsys_status (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[100];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint8 ind = 0;

    len = qcdm_cmd_zte_subsys_status_new (buf, sizeof (buf));
    g_assert (len == 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_zte_subsys_status_result (buf, reply_len, &err);
    if (!result) {
        /* Obviously not all devices implement this command */
        g_assert_cmpint (err, ==, -QCDM_ERROR_RESPONSE_BAD_COMMAND);
        g_message ("%s: device does not implement the ZTE subsystem", __func__);
        return;
    }
    g_assert (result);

    qcdm_result_get_u8 (result, QCDM_CMD_ZTE_SUBSYS_STATUS_ITEM_SIGNAL_INDICATOR, &ind);
    g_message ("%s: Signal Indicator: %d", __func__, ind);

    qcdm_result_unref (result);
}

void
test_com_nw_subsys_modem_snapshot_cdma (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[200];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint8 num8 = 0;
    guint32 num32 = 0;

    len = qcdm_cmd_nw_subsys_modem_snapshot_cdma_new (buf, sizeof (buf), QCDM_NW_CHIPSET_6800);
    g_assert (len == 12);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result (buf, reply_len, &err);
    if (!result) {
        /* Obviously not all devices implement this command */
        if (   err == -QCDM_ERROR_RESPONSE_BAD_COMMAND
            || err == -QCDM_ERROR_RESPONSE_BAD_LENGTH)
            return;
        g_assert_cmpint (err, ==, QCDM_SUCCESS);
    }
    g_assert (result);

    qcdm_result_get_u32 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_RSSI, &num32);
    g_message ("%s: RSSI: %d", __func__, num32);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_PREV, &num8);
    g_message ("%s: P_REV: %s", __func__, prev_to_string (num8));

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_BAND_CLASS, &num8);
    g_message ("%s: Band Class: %s", __func__, band_class_to_string (num8));

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_ERI, &num8);
    g_message ("%s: ERI: %d", __func__, num8);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV, &num8);
    g_message ("%s: HDR Revision: %s", __func__, hdr_rev_to_string (num8));

    qcdm_result_unref (result);
}

void
test_com_nw_subsys_eri (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[200];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint8 num8 = 0;
    const char *str = NULL;

    len = qcdm_cmd_nw_subsys_eri_new (buf, sizeof (buf), QCDM_NW_CHIPSET_6800);
    g_assert_cmpint (len, ==, 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_nw_subsys_eri_result (buf, reply_len, &err);
    if (!result) {
        /* Obviously not all devices implement this command */
        if (   err == -QCDM_ERROR_RESPONSE_BAD_COMMAND
            || err == -QCDM_ERROR_RESPONSE_BAD_LENGTH)
            return;
        g_assert_cmpint (err, ==, QCDM_SUCCESS);
    }
    g_assert (result);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ROAM, &num8);
    g_message ("%s: Roam: %d", __func__, num8);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_INDICATOR_ID, &num8);
    g_message ("%s: Indicator ID: %d", __func__, num8);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_ID, &num8);
    g_message ("%s: Icon ID: %d", __func__, num8);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ICON_MODE, &num8);
    g_message ("%s: Icon Mode: %d", __func__, num8);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_CALL_PROMPT_ID, &num8);
    g_message ("%s: Call Prompt ID: %d", __func__, num8);

    qcdm_result_get_u8 (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_ALERT_ID, &num8);
    g_message ("%s: Alert ID: %d", __func__, num8);

    qcdm_result_get_string (result, QCDM_CMD_NW_SUBSYS_ERI_ITEM_TEXT, &str);
    g_message ("%s: Banner: '%s'", __func__, str);

    qcdm_result_unref (result);
}

void
test_com_wcdma_subsys_state_info (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[200];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    guint8 num8 = 0;
    const char *str;

    len = qcdm_cmd_wcdma_subsys_state_info_new (buf, sizeof (buf));
    g_assert (len == 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_wcdma_subsys_state_info_result (buf, reply_len, &err);
    if (!result) {
        /* Obviously not all devices implement this command */
        g_assert_cmpint (err, ==, -QCDM_ERROR_RESPONSE_BAD_COMMAND);
        return;
    }
    g_assert (result);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_IMEI, &str);
    g_message ("%s: IMEI: %s", __func__, str);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_IMSI, &str);
    g_message ("%s: IMSI: %s", __func__, str);

    str = "unknown";
    qcdm_result_get_u8 (result, QCDM_CMD_WCDMA_SUBSYS_STATE_INFO_ITEM_L1_STATE, &num8);
    switch (num8) {
    case QCDM_WCDMA_L1_STATE_IDLE:
        str = "Idle";
        break;
    case QCDM_WCDMA_L1_STATE_FS:
        str = "FS";
        break;
    case QCDM_WCDMA_L1_STATE_ACQ:
        str = "ACQ";
        break;
    case QCDM_WCDMA_L1_STATE_BCH:
        str = "BCH";
        break;
    case QCDM_WCDMA_L1_STATE_PCH:
        str = "PCH";
        break;
    case QCDM_WCDMA_L1_STATE_FACH:
        str = "FACH";
        break;
    case QCDM_WCDMA_L1_STATE_DCH:
        str = "DCH";
        break;
    case QCDM_WCDMA_L1_STATE_DEACTIVATE:
        str = "Deactivated";
        break;
    case QCDM_WCDMA_L1_STATE_PCH_SLEEP:
        str = "PCH Sleep";
        break;
    case QCDM_WCDMA_L1_STATE_DEEP_SLEEP:
        str = "Deep Sleep";
        break;
    case QCDM_WCDMA_L1_STATE_STOPPED:
        str = "Stopped";
        break;
    case QCDM_WCDMA_L1_STATE_SUSPENDED:
        str = "Suspended";
        break;
    case QCDM_WCDMA_L1_STATE_PCH_BPLMN:
        str = "PCH BPLMN";
        break;
    case QCDM_WCDMA_L1_STATE_WAIT_TRM_STOP:
        str = "Wait TRM Stop";
        break;
    default:
        break;
    }
    g_message ("%s: L1 state: %d (%s)", __func__, num8, str);

    qcdm_result_unref (result);
}

void
test_com_gsm_subsys_state_info (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    int err = QCDM_SUCCESS;
    char buf[200];
    gint len;
    QcdmResult *result;
    gsize reply_len;
    const char *str;
    uint32_t num;
    uint8_t u8;

    len = qcdm_cmd_gsm_subsys_state_info_new (buf, sizeof (buf));
    g_assert (len == 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_gsm_subsys_state_info_result (buf, reply_len, &err);
    if (!result) {
        /* Obviously not all devices implement this command */
        g_assert_cmpint (err, ==, -QCDM_ERROR_RESPONSE_BAD_COMMAND);
        return;
    }
    g_assert (result);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_IMEI, &str);
    g_message ("%s: IMEI: %s", __func__, str);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_IMSI, &str);
    g_message ("%s: IMSI: %s", __func__, str);

    num = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_MCC, &num);
    g_message ("%s: MCC: %d", __func__, num);

    num = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_MNC, &num);
    g_message ("%s: MNC: %d", __func__, num);

    num = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_LAI_LAC, &num);
    g_message ("%s: LAC: 0x%04X", __func__, num);

    num = 0;
    qcdm_result_get_u32 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CELLID, &num);
    g_message ("%s: Cell ID: 0x%04X", __func__, num);

    u8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_CALL_STATE, &u8);
    g_message ("%s: CM Call State: %d (%s)", __func__, u8, cm_call_state_to_string (u8));

    u8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_OP_MODE, &u8);
    g_message ("%s: CM Opmode: %d (%s)", __func__, u8, operating_mode_to_string (u8));

    u8 = 0;
    qcdm_result_get_u8 (result, QCDM_CMD_GSM_SUBSYS_STATE_INFO_ITEM_CM_SYS_MODE, &u8);
    g_message ("%s: CM Sysmode: %d (%s)", __func__, u8, cm_system_mode_to_string (u8));

    qcdm_result_unref (result);
}
