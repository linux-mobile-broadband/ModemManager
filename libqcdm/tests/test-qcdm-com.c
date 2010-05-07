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
#include "error.h"

/************************************************************/

static const char *
prev_to_string (guint8 prev)
{
    switch (prev) {
    case QCDM_CDMA_PREV_IS_95:
        return "IS-95";
    case QCDM_CDMA_PREV_IS_95A:
        return "IS-95A";
    case QCDM_CDMA_PREV_IS_95A_TSB74:
        return "IS-95A TSB-74";
    case QCDM_CDMA_PREV_IS_95B_PHASE1:
        return "IS-95B Phase I";
    case QCDM_CDMA_PREV_IS_95B_PHASE2:
        return "IS-95B Phase II";
    case QCDM_CDMA_PREV_IS2000_REL0:
        return "IS-2000 Release 0";
    case QCDM_CDMA_PREV_IS2000_RELA:
        return "IS-2000 Release A";
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
    int i = 0;
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
    int total = 0, retries = 0;
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
            gboolean more = FALSE, success;
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
    GError *error = NULL;
    gboolean success;

    success = qcdm_port_setup (d->fd, &error);
    if (!success) {
        g_warning ("%s: error setting up port: (%d) %s",
                   d->port,
                   error ? error->code : -1,
                   error && error->message ? error->message : "(unknown)");
    }
    g_assert (success);
}

void
test_com_version_info (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[512];
    const char *str;
    gint len;
    QCDMResult *result;
    gsize reply_len;

    len = qcdm_cmd_version_info_new (buf, sizeof (buf), NULL);
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_version_info_result (buf, reply_len, &error);
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
    GError *error = NULL;
    char buf[512];
    const char *str;
    gint len;
    QCDMResult *result;
    gsize reply_len;

    len = qcdm_cmd_esn_new (buf, sizeof (buf), NULL);
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_esn_result (buf, reply_len, &error);
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
    GError *error = NULL;
    char buf[512];
    const char *str;
    gint len;
    QCDMResult *result;
    gsize reply_len;

    len = qcdm_cmd_nv_get_mdn_new (buf, sizeof (buf), 0, NULL);
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_mdn_result (buf, reply_len, &error);
    if (!result) {
        g_assert_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_NVCMD_FAILED);
        return;
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
    GError *error = NULL;
    char buf[512];
    guint8 pref;
    const char *msg;
    gint len;
    QCDMResult *result;
    gsize reply_len;

    len = qcdm_cmd_nv_get_roam_pref_new (buf, sizeof (buf), 0, NULL);
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_roam_pref_result (buf, reply_len, &error);
    g_assert (result);

    g_print ("\n");

    success = qcdm_result_get_uint8 (result, QCDM_CMD_NV_GET_ROAM_PREF_ITEM_ROAM_PREF, &pref);
    g_assert (success);

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
    GError *error = NULL;
    char buf[512];
    guint8 pref;
    const char *msg;
    gint len;
    QCDMResult *result;
    gsize reply_len;

    len = qcdm_cmd_nv_get_mode_pref_new (buf, sizeof (buf), 0, NULL);
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_mode_pref_result (buf, reply_len, &error);
    if (!result) {
        g_assert_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_NVCMD_FAILED);
        return;
    }

    g_print ("\n");

    success = qcdm_result_get_uint8 (result, QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF, &pref);
    g_assert (success);

    switch (pref) {
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_ONLY:
        msg = "1X only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_HDR_ONLY:
        msg = "HDR only";
        break;
    case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_AUTO:
        msg = "automatic";
        break;
    default:
        msg = "unknown";
        break;
    }
    g_message ("%s: Mode preference: 0x%02X (%s)", __func__, pref, msg);

    qcdm_result_unref (result);
}

void
test_com_status (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[100];
    const char *str, *detail;
    gint len;
    QCDMResult *result;
    gsize reply_len;
    guint32 n32;
    guint8 n8;

    len = qcdm_cmd_cdma_status_new (buf, sizeof (buf), NULL);
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_cdma_status_result (buf, reply_len, &error);
    g_assert (result);

    g_print ("\n");

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_CDMA_STATUS_ITEM_ESN, &str);
    g_message ("%s: ESN: %s", __func__, str);

    n32 = 0;
    detail = NULL;
    qcdm_result_get_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RF_MODE, &n32);
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
    qcdm_result_get_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_RX_STATE, &n32);
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
    qcdm_result_get_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_ENTRY_REASON, &n32);
    g_message ("%s: Entry Reason: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_CURRENT_CHANNEL, &n32);
    g_message ("%s: Current Channel: %u", __func__, n32);

    n8 = 0;
    qcdm_result_get_uint8 (result, QCDM_CMD_CDMA_STATUS_ITEM_CODE_CHANNEL, &n8);
    g_message ("%s: Code Channel: %u", __func__, n8);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_PILOT_BASE, &n32);
    g_message ("%s: Pilot Base: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_SID, &n32);
    g_message ("%s: CDMA System ID: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CDMA_STATUS_ITEM_NID, &n32);
    g_message ("%s: CDMA Network ID: %u", __func__, n32);

    qcdm_result_unref (result);
}

void
test_com_sw_version (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[100];
    gint len;
    QCDMResult *result;
    gsize reply_len;

    len = qcdm_cmd_sw_version_new (buf, sizeof (buf), NULL);
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_sw_version_result (buf, reply_len, &error);

    /* Recent devices don't appear to implement this command */
    g_assert (result == NULL);
    g_assert_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_COMMAND);

/*
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
*/
}

void
test_com_pilot_sets (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[256];
    gint len;
    QCDMResult *result;
    gsize reply_len;
    guint32 num, i;

    len = qcdm_cmd_pilot_sets_new (buf, sizeof (buf), NULL);
    g_assert (len == 4);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_pilot_sets_result (buf, reply_len, &error);
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
        g_message ("      EC/IO     %d  (%.1f dB)", ecio, db);
    }

    num = 0;
    qcdm_cmd_pilot_sets_result_get_num (result, QCDM_CMD_PILOT_SETS_TYPE_CANDIDATE, &num);
    g_message ("%s: Candidate Pilots: %d", __func__, num);

    num = 0;
    qcdm_cmd_pilot_sets_result_get_num (result, QCDM_CMD_PILOT_SETS_TYPE_NEIGHBOR, &num);
    g_message ("%s: Neighbor Pilots: %d", __func__, num);

    qcdm_result_unref (result);
}

void
test_com_cm_subsys_state_info (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[100];
    gint len;
    QCDMResult *result;
    gsize reply_len;
    guint32 n32;
    const char *detail;

    len = qcdm_cmd_cm_subsys_state_info_new (buf, sizeof (buf), NULL);
    g_assert (len == 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_cm_subsys_state_info_result (buf, reply_len, &error);
    g_assert (result);

    g_print ("\n");

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_CALL_STATE, &n32);
    g_message ("%s: Call State: %u", __func__, n32);

    n32 = 0;
    detail = NULL;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_OPERATING_MODE, &n32);
    switch (n32) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_ONLINE:
        detail = "online";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Operating Mode: %u (%s)", __func__, n32, detail);

    n32 = 0;
    detail = NULL;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SYSTEM_MODE, &n32);
    switch (n32) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_NO_SERVICE:
        detail = "no service";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_AMPS:
        detail = "AMPS";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_CDMA:
        detail = "CDMA";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_HDR:
        detail = "HDR/EVDO";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_WCDMA:
        detail = "WCDMA";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: System Mode: %u (%s)", __func__, n32, detail);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_MODE_PREF, &n32);
    switch (n32) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_DIGITAL_ONLY:
        detail = "digital only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_AUTO:
        detail = "automatic";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_1X_ONLY:
        detail = "1X only";
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_MODE_PREF_HDR_ONLY:
        detail = "HDR only";
        break;
    default:
        detail = "unknown";
        break;
    }
    g_message ("%s: Mode Preference: 0x%02X (%s)", __func__, n32 & 0xFF, detail);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_BAND_PREF, &n32);
    g_message ("%s: Band Preference: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ROAM_PREF, &n32);
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
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_SERVICE_DOMAIN_PREF, &n32);
    g_message ("%s: Service Domain Preference: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_ACQ_ORDER_PREF, &n32);
    g_message ("%s: Acquisition Order Preference: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_HYBRID_PREF, &n32);
    g_message ("%s: Hybrid Preference: %u", __func__, n32);

    n32 = 0;
    qcdm_result_get_uint32 (result, QCDM_CMD_CM_SUBSYS_STATE_INFO_ITEM_NETWORK_SELECTION_PREF, &n32);
    g_message ("%s: Network Selection Preference: %u", __func__, n32);

    qcdm_result_unref (result);
}

void
test_com_hdr_subsys_state_info (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[100];
    gint len;
    QCDMResult *result;
    gsize reply_len;
    guint8 num;
    const char *detail;

    len = qcdm_cmd_hdr_subsys_state_info_new (buf, sizeof (buf), NULL);
    g_assert (len == 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_hdr_subsys_state_info_result (buf, reply_len, &error);
    if (!result) {
        /* 1x-only devices won't implement the HDR subsystem of course */
        g_assert_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_COMMAND);
        g_message ("%s: device does not implement the HDR subsystem", __func__);
        return;
    }
    g_assert (result);

    num = 0;
    detail = NULL;
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_AT_STATE, &num);
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
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_SESSION_STATE, &num);
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
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ALMP_STATE, &num);
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
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_INIT_STATE, &num);
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
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_IDLE_STATE, &num);
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
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_CONNECTED_STATE, &num);
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
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_ROUTE_UPDATE_STATE, &num);
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
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_OVERHEAD_MSG_STATE, &num);
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
    qcdm_result_get_uint8 (result, QCDM_CMD_HDR_SUBSYS_STATE_INFO_ITEM_HDR_HYBRID_MODE, &num);
    g_message ("%s: HDR Hybrid Mode: %u", __func__, num);

    qcdm_result_unref (result);
}

void
test_com_zte_subsys_status (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[100];
    gint len;
    QCDMResult *result;
    gsize reply_len;
    guint8 ind = 0;

    len = qcdm_cmd_zte_subsys_status_new (buf, sizeof (buf), NULL);
    g_assert (len == 7);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_zte_subsys_status_result (buf, reply_len, &error);
    if (!result) {
        /* Obviously not all devices implement this command */
        g_assert_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_COMMAND);
        g_message ("%s: device does not implement the ZTE subsystem", __func__);
        return;
    }
    g_assert (result);

    qcdm_result_get_uint8 (result, QCDM_CMD_ZTE_SUBSYS_STATUS_ITEM_SIGNAL_INDICATOR, &ind);
    g_message ("%s: Signal Indicator: %d", __func__, ind);

    qcdm_result_unref (result);
}

void
test_com_nw_subsys_modem_snapshot_cdma (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[200];
    gint len;
    QCDMResult *result;
    gsize reply_len;
    guint8 num8 = 0;
    guint32 num32 = 0;

    len = qcdm_cmd_nw_subsys_modem_snapshot_cdma_new (buf, sizeof (buf), QCDM_NW_CHIPSET_6800, NULL);
    g_assert (len == 12);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    g_print ("\n");

    /* Parse the response into a result structure */
    result = qcdm_cmd_nw_subsys_modem_snapshot_cdma_result (buf, reply_len, &error);
    if (!result) {
        /* Obviously not all devices implement this command */
        g_assert_error (error, QCDM_COMMAND_ERROR, QCDM_COMMAND_BAD_COMMAND);
        return;
    }
    g_assert (result);

    qcdm_result_get_uint32 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_RSSI, &num32);
    g_message ("%s: RSSI: %d", __func__, num32);

    qcdm_result_get_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_PREV, &num8);
    g_message ("%s: P_REV: %s", __func__, prev_to_string (num8));

    qcdm_result_get_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_BAND_CLASS, &num8);
    g_message ("%s: Band Class: %s", __func__, band_class_to_string (num8));

    qcdm_result_get_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_ERI, &num8);
    g_message ("%s: ERI: %d", __func__, num8);

    qcdm_result_get_uint8 (result, QCDM_CMD_NW_SUBSYS_MODEM_SNAPSHOT_CDMA_ITEM_HDR_REV, &num8);
    g_message ("%s: HDR Revision: %s", __func__, hdr_rev_to_string (num8));

    qcdm_result_unref (result);
}

