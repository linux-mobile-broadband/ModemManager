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

#include <glib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

#include "test-wmc-com.h"
#include "com.h"
#include "utils.h"
#include "errors.h"
#include "commands.h"

/************************************************************/

typedef struct {
    char *port;
    int fd;
    struct termios old_t;
    wmcbool debug;
    wmcbool uml290;
} TestComData;

gpointer
test_com_setup (const char *port, wmcbool uml290, wmcbool debug)
{
    TestComData *d;
    int ret;

    d = g_malloc0 (sizeof (TestComData));
    g_assert (d);
    d->uml290 = uml290;
    d->debug = debug;

    if (getenv ("SERIAL_DEBUG"))
        d->debug = TRUE;

    errno = 0;
    d->fd = open (port, O_RDWR | O_EXCL | O_NONBLOCK | O_NOCTTY);
    if (d->fd < 0)
        g_warning ("%s: open failed: %d", port, errno);
    g_assert (d->fd >= 0);

    ret = ioctl (d->fd, TIOCEXCL);
    if (ret) {
        g_warning ("%s: lock failed: %d", port, errno);
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
print_buf (const char *detail, const char *buf, size_t len)
{
    int i = 0, z;
    wmcbool newline = FALSE;
    char *f;
    guint flen;

    f = g_strdup_printf ("%s (%zu)  ", detail, len);
    flen = strlen (f);
    g_print ("%s", f);
    for (i = 0; i < len; i++) {
        g_print ("%02x ", buf[i] & 0xFF);
        if (((i + 1) % 16) == 0) {
            g_print ("\n");
            z = flen;
            while (z--)
                g_print (" ");
            newline = TRUE;
        } else
            newline = FALSE;
    }

    if (!newline)
        g_print ("\n");
}

static wmcbool
send_command (TestComData *d,
              char *inbuf,
              size_t inbuf_len,
              size_t cmd_len)
{
    int status;
    int eagain_count = 1000;
    size_t i = 0, sendlen;
    char sendbuf[600];

    if (d->debug)
        print_buf ("\nRAW>>>", inbuf, cmd_len);

    /* Encapsulate the data for the device */
    sendlen = wmc_encapsulate (inbuf, cmd_len, inbuf_len, sendbuf, sizeof (sendbuf), d->uml290);
    if (sendlen <= 0) {
        g_warning ("Failed to encapsulate WMC command");
        return FALSE;
    }

    if (d->debug)
        print_buf ("ENC>>>", sendbuf, sendlen);

    while (i < sendlen) {
        errno = 0;
        status = write (d->fd, &sendbuf[i], 1);
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

static size_t
wait_reply (TestComData *d, char *buf, size_t len)
{
    fd_set in;
    int result;
    struct timeval timeout = { 1, 0 };
    char readbuf[2048];
    ssize_t bytes_read;
    int total = 0, retries = 0;
    size_t decap_len = 0;

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
            wmcbool more = FALSE, success;
            size_t used = 0;

            total++;
            decap_len = 0;
            success = wmc_decapsulate (readbuf, total, buf, len, &decap_len, &used, &more, d->uml290);

            if (success && !more && d->debug)
                print_buf ("RAW<<<", readbuf, total);

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

    if (d->debug)
        print_buf ("DCP<<<", buf, decap_len);

    return decap_len;
}

void
test_com_port_init (void *f, void *data)
{
    TestComData *d = data;
    int ret;

    ret = wmc_port_setup (d->fd);
    if (ret < 0)
        g_warning ("%s: error setting up serial port: (%d)", d->port, ret);
    g_assert_cmpint (ret, ==, 0);
}

void
test_com_init (void *f, void *data)
{
    TestComData *d = data;
    wmcbool success;
    char buf[512];
    gint len;
    WmcResult *result;
    size_t reply_len;

    len = wmc_cmd_init_new (buf, sizeof (buf), d->uml290);
    g_assert (len);

    /* Send the command */
    success = send_command (d, buf, sizeof (buf), len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = wmc_cmd_init_result (buf, reply_len, d->uml290);
    g_assert (result);

    wmc_result_unref (result);
}

void
test_com_device_info (void *f, void *data)
{
    TestComData *d = data;
    wmcbool success;
    char buf[2048];
    const char *str, *str2;
    gint len;
    WmcResult *result;
    size_t reply_len;
    guint32 u32;

    len = wmc_cmd_device_info_new (buf, sizeof (buf));
    g_assert (len == 2);

    /* Send the command */
    success = send_command (d, buf, sizeof (buf), len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = wmc_cmd_device_info_result (buf, reply_len);
    g_assert (result);

    g_print ("\n");

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_MANUFACTURER, &str);
    g_message ("%s: Manuf:    %s", __func__, str);

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_MODEL, &str);
    g_message ("%s: Model:    %s", __func__, str);

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_FW_REVISION, &str);
    g_message ("%s: FW Rev:   %s", __func__, str);

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_HW_REVISION, &str);
    g_message ("%s: HW Rev:   %s", __func__, str);

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_CDMA_MIN, &str);
    g_message ("%s: CDMA MIN: %s", __func__, str);

    u32 = 0;
    wmc_result_get_u32 (result, WMC_CMD_DEVICE_INFO_ITEM_HOME_SID, &u32);
    g_message ("%s: Home SID: %d", __func__, u32);

    u32 = 0;
    wmc_result_get_u32 (result, WMC_CMD_DEVICE_INFO_ITEM_PRL_VERSION, &u32);
    g_message ("%s: PRL Ver:  %d", __func__, u32);

    u32 = 0;
    wmc_result_get_u32 (result, WMC_CMD_DEVICE_INFO_ITEM_ERI_VERSION, &u32);
    g_message ("%s: ERI Ver:  %d", __func__, u32);

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_MEID, &str);
    g_message ("%s: MEID:     %s", __func__, str ? str : "(none)");

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_IMEI, &str);
    g_message ("%s: IMEI:     %s", __func__, str ? str : "(none)");

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_ICCID, &str);
    g_message ("%s: ICCID:    %s", __func__, str ? str : "(none)");

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_MCC, &str);
    str2 = NULL;
    wmc_result_get_string (result, WMC_CMD_DEVICE_INFO_ITEM_MNC, &str2);
    g_message ("%s: MCC/MNC:  %s %s", __func__,
               str ? str : "(none)",
               str2 ? str2 : "(none)");

    wmc_result_unref (result);
}

static const char *
service_to_string (u_int8_t service)
{
    switch (service) {
    case WMC_NETWORK_SERVICE_NONE:
        return "none";
    case WMC_NETWORK_SERVICE_AMPS:
        return "AMPS";
    case WMC_NETWORK_SERVICE_IS95A:
        return "IS95-A";
    case WMC_NETWORK_SERVICE_IS95B:
        return "IS95-B";
    case WMC_NETWORK_SERVICE_GSM:
        return "GSM";
    case WMC_NETWORK_SERVICE_GPRS:
        return "GPRS";
    case WMC_NETWORK_SERVICE_1XRTT:
        return "1xRTT";
    case WMC_NETWORK_SERVICE_EVDO_0:
        return "EVDO r0";
    case WMC_NETWORK_SERVICE_UMTS:
        return "UMTS";
    case WMC_NETWORK_SERVICE_EVDO_A:
        return "EVDO rA";
    case WMC_NETWORK_SERVICE_EDGE:
        return "EDGE";
    case WMC_NETWORK_SERVICE_HSDPA:
        return "HSDPA";
    case WMC_NETWORK_SERVICE_HSUPA:
        return "HSUPA";
    case WMC_NETWORK_SERVICE_HSPA:
        return "HSPA";
    case WMC_NETWORK_SERVICE_LTE:
        return "LTE";
    default:
        return "unknown";
    }
}

void
test_com_network_info (void *f, void *data)
{
    TestComData *d = data;
    wmcbool success;
    char buf[1024];
    const char *str;
    u_int8_t dbm, service;
    gint len;
    WmcResult *result;
    size_t reply_len;

    len = wmc_cmd_network_info_new (buf, sizeof (buf));
    g_assert (len == 2);

    /* Send the command */
    success = send_command (d, buf, sizeof (buf), len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = wmc_cmd_network_info_result (buf, reply_len);
    g_assert (result);

    g_print ("\n");

    service = 0;
    wmc_result_get_u8 (result, WMC_CMD_NETWORK_INFO_ITEM_SERVICE, &service);
    g_message ("%s: Service: %d (%s)", __func__, service, service_to_string (service));

    dbm = 0;
    wmc_result_get_u8 (result, WMC_CMD_NETWORK_INFO_ITEM_2G_DBM, &dbm);
    g_message ("%s: 2G dBm: -%d", __func__, dbm);

    dbm = 0;
    wmc_result_get_u8 (result, WMC_CMD_NETWORK_INFO_ITEM_3G_DBM, &dbm);
    g_message ("%s: 3G dBm: -%d", __func__, dbm);

    dbm = 0;
    wmc_result_get_u8 (result, WMC_CMD_NETWORK_INFO_ITEM_LTE_DBM, &dbm);
    g_message ("%s: LTE dBm: -%d", __func__, dbm);

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_NETWORK_INFO_ITEM_OPNAME, &str);
    g_message ("%s: Operator Name: %s", __func__, str ? str : "(none)");

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_NETWORK_INFO_ITEM_MCC, &str);
    g_message ("%s: MCC: %s", __func__, str ? str : "(none)");

    str = NULL;
    wmc_result_get_string (result, WMC_CMD_NETWORK_INFO_ITEM_MNC, &str);
    g_message ("%s: MNC: %s", __func__, str ? str : "(none)");

    wmc_result_unref (result);
}

static const char *
mode_to_string (u_int8_t service)
{
    switch (service) {
    case WMC_NETWORK_MODE_AUTO_CDMA:
        return "CDMA/EVDO";
    case WMC_NETWORK_MODE_CDMA_ONLY:
        return "CDMA only";
    case WMC_NETWORK_MODE_EVDO_ONLY:
        return "EVDO only";
    case WMC_NETWORK_MODE_AUTO_GSM:
        return "GSM/UMTS";
    case WMC_NETWORK_MODE_GPRS_ONLY:
        return "GSM/GPRS/EDGE only";
    case WMC_NETWORK_MODE_UMTS_ONLY:
        return "UMTS/HSPA only";
    case WMC_NETWORK_MODE_AUTO:
        return "Auto";
    case WMC_NETWORK_MODE_LTE_ONLY:
        return "LTE only";
    default:
        return "unknown";
    }
}

void
test_com_get_global_mode (void *f, void *data)
{
    TestComData *d = data;
    wmcbool success;
    char buf[1024];
    u_int8_t mode;
    gint len;
    WmcResult *result;
    size_t reply_len;

    len = wmc_cmd_get_global_mode_new (buf, sizeof (buf));
    g_assert (len == 3);

    /* Send the command */
    success = send_command (d, buf, sizeof (buf), len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = wmc_cmd_get_global_mode_result (buf, reply_len);
    g_assert (result);

    g_print ("\n");

    mode = 0;
    wmc_result_get_u8 (result, WMC_CMD_GET_GLOBAL_MODE_ITEM_MODE, &mode);
    g_message ("%s: Mode: %d (%s)", __func__, mode, mode_to_string (mode));

    wmc_result_unref (result);
}

