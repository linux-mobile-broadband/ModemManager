/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2014 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <assert.h>
#include <unistd.h>

#include "utils.h"
#include "errors.h"
#include "commands.h"
#include "com.h"

static int debug = 0;

static void
print_buf (const char *detail, const char *buf, size_t len)
{
    unsigned int i, z;
    qcdmbool newline = FALSE;
    char tmp[500];
    uint32_t flen;

    flen = snprintf (tmp, sizeof (tmp) - 1, "%s (%zu)  ", detail, len);
    fprintf (stdout, "%s", tmp);
    for (i = 0; i < len; i++) {
        fprintf (stdout, "%02x ", buf[i] & 0xFF);
        if (((i + 1) % 16) == 0) {
            fprintf (stdout, "\n");
            z = flen;
            while (z--)
                fprintf (stdout, " ");
            newline = TRUE;
        } else
            newline = FALSE;
    }

    if (!newline)
        fprintf (stdout, "\n");
}

static int
com_setup (const char *port)
{
    int ret, fd;

    errno = 0;
    fd = open (port, O_RDWR | O_EXCL | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) {
        fprintf (stderr, "E: failed to open port %s\n", port);
        return -1;
    }

    ret = ioctl (fd, TIOCEXCL);
    if (ret) {
        fprintf (stderr, "E: failed to lock port %s\n", port);
        close (fd);
        return -1;
    }

    return fd;
}

/******************************************************************/

static qcdmbool
qcdm_send (int fd, char *buf, size_t len)
{
    int status;
    int eagain_count = 1000;
    size_t i = 0;

    if (debug)
        print_buf ("DM:ENC>>>", buf, len);

    while (i < len) {
        errno = 0;
        status = write (fd, &buf[i], 1);
        if (status < 0) {
            if (errno == EAGAIN) {
                eagain_count--;
                if (eagain_count <= 0)
                    return FALSE;
            } else
                assert (errno == 0);
        } else
            i++;

        usleep (1000);
    }

    return TRUE;
}

static size_t
qcdm_wait_reply (int fd, char *buf, size_t len)
{
    fd_set in;
    int result;
    struct timeval timeout = { 1, 0 };
    char readbuf[1024];
    ssize_t bytes_read;
    unsigned int total = 0, retries = 0;
    size_t decap_len = 0;

    FD_ZERO (&in);
    FD_SET (fd, &in);
    result = select (fd + 1, &in, NULL, NULL, &timeout);
    if (result != 1 || !FD_ISSET (fd, &in))
        return 0;

    do {
        errno = 0;
        bytes_read = read (fd, &readbuf[total], 1);
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
            qcdmbool success;
            size_t used = 0;

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

    if (debug)
        print_buf ("QCDM:DEC<<", buf, decap_len);

    return decap_len;
}

static int
qcdm_set_mode (int fd, uint8_t mode)
{
    int err;
    char buf[512];
    size_t len;
    QcdmResult *result;
    size_t reply_len;

    len = qcdm_cmd_control_new (buf, sizeof (buf), mode);
    assert (len);

    /* Send the command */
    if (!qcdm_send (fd, buf, len)) {
        fprintf (stderr, "E: failed to send QCDM Control command\n");
        goto error;
    }

    reply_len = qcdm_wait_reply (fd, buf, sizeof (buf));
    if (!reply_len) {
        fprintf (stderr, "E: failed to receive Control command reply\n");
        goto error;
    }

    /* Parse the response into a result structure */
    err = QCDM_SUCCESS;
    result = qcdm_cmd_control_result (buf, reply_len, &err);
    if (!result) {
        fprintf (stderr, "E: failed to parse Control command reply: %d\n", err);
        goto error;
    }

    qcdm_result_unref (result);
    return 0;

error:
    return -1;
}

/******************************************************************/

static void
usage (const char *prog)
{
    fprintf (stderr, "Usage: %s <DM port> [--debug]\n", prog);
}

int
main (int argc, char *argv[])
{
    const char *dmport = argv[1];
    int fd, err;

    if (argc < 1 || argc > 3) {
        usage (argv[0]);
        return 1;
    }

    if (argc == 3 && strcasecmp (argv[2], "--debug") == 0)
        debug = 1;

    if (debug)
        putenv ((char *)"QCDM_DEBUG=1");

    fd = com_setup (dmport);
    if (fd < 0)
        return 1;

    err = qcdm_port_setup (fd);
    if (err != QCDM_SUCCESS) {
        fprintf (stderr, "E: failed to set up DM port %s: %d\n", dmport, err);
        return 1;
    }

    /* Send DM reset command */
    printf ("setting offline...\n");
    qcdm_set_mode (fd, QCDM_CMD_CONTROL_MODE_OFFLINE);
    sleep (2);
    printf ("reset...\n");
    qcdm_set_mode (fd, QCDM_CMD_CONTROL_MODE_RESET);
    sleep (2);
    printf ("done\n");

    return 0;
}
