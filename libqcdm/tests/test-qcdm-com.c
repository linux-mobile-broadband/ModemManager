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
            print_buf ("<<<", readbuf, total);
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
test_com (void *f, void *data)
{
    TestComData *d = data;
    gboolean success;
    GError *error = NULL;
    char buf[512];
    const char *str;
    gint len;
    QCDMResult *result;
    gsize reply_len;

    success = qcdm_port_setup (d->fd, &error);
    if (!success) {
        g_warning ("%s: error setting up port: (%d) %s",
                   d->port,
                   error ? error->code : -1,
                   error && error->message ? error->message : "(unknown)");
    }
    g_assert (success);

    /*** Get the device's firmware version information ***/

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

    /*** Get the device's ESN ***/

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

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_ESN_ITEM_ESN, &str);
    g_message ("%s: ESN: %s", __func__, str);

    qcdm_result_unref (result);


    /*** Get the device's phone number ***/

    len = qcdm_cmd_nv_get_mdn_new (buf, sizeof (buf), 0, NULL);
    g_assert (len > 0);

    /* Send the command */
    success = send_command (d, buf, len);
    g_assert (success);

    /* Get a response */
    reply_len = wait_reply (d, buf, sizeof (buf));

    /* Parse the response into a result structure */
    result = qcdm_cmd_nv_get_mdn_result (buf, reply_len, &error);
    g_assert (result);

    str = NULL;
    qcdm_result_get_string (result, QCDM_CMD_NV_GET_MDN_ITEM_MDN, &str);
    g_message ("%s: MDN: %s", __func__, str);

    qcdm_result_unref (result);
}

