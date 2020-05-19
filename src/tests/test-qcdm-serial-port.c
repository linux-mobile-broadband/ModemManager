/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2010 Red Hat, Inc.
 */

#include <config.h>
#include <glib.h>
#include <string.h>
#include <pty.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-port-serial-qcdm.h"
#include "libqcdm/src/commands.h"
#include "libqcdm/src/utils.h"
#include "libqcdm/src/com.h"
#include "libqcdm/src/errors.h"
#include "mm-log-test.h"

typedef struct {
    int master;
    int slave;
    gboolean valid;
    pid_t child;
} TestData;

static gboolean
wait_for_child (TestData *d, guint32 timeout)
{
    GTimeVal start, now;
    int status, ret;

    g_get_current_time (&start);
    do {
        status = 0;
        ret = waitpid (d->child, &status, WNOHANG);
        g_get_current_time (&now);
        if (d->child && (now.tv_sec - start.tv_sec > (glong)timeout)) {
            /* Kill it */
            if (g_test_verbose ())
                g_message ("Killing running child process %d", d->child);
            kill (d->child, SIGKILL);
            d->child = 0;
        }
        if (ret == 0)
            sleep (1);
    } while ((ret <= 0) || (!WIFEXITED (status) && !WIFSIGNALED (status)));

    d->child = 0;
    return (WIFEXITED (status) && WEXITSTATUS (status) == 0) ? TRUE : FALSE;
}

static void
print_buf (const char *detail, const char *buf, gsize len)
{
    guint i = 0;
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

static void
server_send_response (int fd, const char *buf, gsize len)
{
    int status;
    gsize i = 0;

    if (g_test_verbose ())
        print_buf (">>>", buf, len);

    while (i < len) {
        errno = 0;
        status = write (fd, &buf[i], 1);
        g_assert_cmpint (errno, ==, 0);
        g_assert (status == 1);
        i++;
        usleep (1000);
    }
}

static gsize
server_wait_request (int fd, char *buf, gsize len)
{
    fd_set in;
    int result;
    struct timeval timeout = { 1, 0 };
    char readbuf[1024];
    ssize_t bytes_read;
    guint total = 0, retries = 0;
    gsize decap_len = 0;

    FD_ZERO (&in);
    FD_SET (fd, &in);
    result = select (fd + 1, &in, NULL, NULL, &timeout);
    g_assert_cmpint (result, ==, 1);
    g_assert (FD_ISSET (fd, &in));

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
            gboolean success;
            gsize used = 0;
            qcdmbool more = FALSE;

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
            g_assert_not_reached ();
        }
    } while (total < sizeof (readbuf));

    if (g_test_verbose ()) {
        print_buf ("<<<", readbuf, total);
        print_buf ("D<<", buf, decap_len);
    }

    return decap_len;
}

static void
qcdm_verinfo_expect_success_cb (MMPortSerialQcdm *port,
                                GAsyncResult *res,
                                GMainLoop *loop)
{
    GError *error = NULL;
    GByteArray *response;

    response = mm_port_serial_qcdm_command_finish (port, res, &error);

    g_assert_no_error (error);
    g_assert (response->len > 0);
    g_byte_array_unref (response);
    g_main_loop_quit (loop);
}

static void
qcdm_request_verinfo (MMPortSerialQcdm *port,
                      GAsyncReadyCallback cb,
                      GMainLoop *loop)
{
    GByteArray *verinfo;
    gint len;

    /* Build up the probe command */
    verinfo = g_byte_array_sized_new (50);
    len = qcdm_cmd_version_info_new ((char *) verinfo->data, 50);
    if (len <= 0)
        g_byte_array_free (verinfo, TRUE);
    verinfo->len = len;

    mm_port_serial_qcdm_command (port, verinfo, 3, NULL, cb, loop);
    g_byte_array_unref (verinfo);
}

static void
qcdm_test_child (int fd, GAsyncReadyCallback cb)
{
    MMPortSerialQcdm *port;
    GMainLoop *loop;
    gboolean success;
    GError *error = NULL;

    loop = g_main_loop_new (NULL, FALSE);

    port = mm_port_serial_qcdm_new_fd (fd);
    g_assert (port);

    success = mm_port_serial_open (MM_PORT_SERIAL (port), &error);
    g_assert_no_error (error);
    g_assert (success);

    qcdm_request_verinfo (port, cb, loop);
    g_main_loop_run (loop);
    g_main_loop_unref (loop);

    mm_port_serial_close (MM_PORT_SERIAL (port));
    g_object_unref (port);
}

/* Test that a Version Info request/response is processed correctly to
 * make sure things in general are working.
 */
static void
test_verinfo (TestData *d)
{
    char req[512];
    gsize req_len;
    pid_t cpid;
    const char rsp[] = {
        0x00, 0x41, 0x75, 0x67, 0x20, 0x31, 0x39, 0x20, 0x32, 0x30, 0x30, 0x38,
        0x32, 0x30, 0x3a, 0x34, 0x38, 0x3a, 0x34, 0x37, 0x4f, 0x63, 0x74, 0x20,
        0x32, 0x39, 0x20, 0x32, 0x30, 0x30, 0x37, 0x31, 0x39, 0x3a, 0x30, 0x30,
        0x3a, 0x30, 0x30, 0x53, 0x43, 0x4e, 0x52, 0x5a, 0x2e, 0x2e, 0x2e, 0x2a,
        0x06, 0x04, 0xb9, 0x0b, 0x02, 0x00, 0xb2, 0x19, 0xc4, 0x7e
    };

    signal (SIGCHLD, SIG_DFL);
    cpid = fork ();
    g_assert (cpid >= 0);

    if (cpid == 0) {
        /* In the child */
        qcdm_test_child (d->slave, (GAsyncReadyCallback)qcdm_verinfo_expect_success_cb);
        exit (0);
    }
    /* Parent */
    d->child = cpid;

    req_len = server_wait_request (d->master, req, sizeof (req));
    g_assert (req_len == 1);
    g_assert_cmpint (req[0], ==, 0x00);

    server_send_response (d->master, rsp, sizeof (rsp));
    g_assert (wait_for_child (d, 3));
}

static void
qcdm_verinfo_expect_fail_cb (MMPortSerialQcdm *port,
                             GAsyncResult *res,
                             GMainLoop *loop)
{
    GError *error = NULL;
    GByteArray *response;

    response = mm_port_serial_qcdm_command_finish (port, res, &error);

    /* Expect any kind of error */
    g_assert (error != NULL);
    g_error_free (error);
    g_assert (response == NULL);

    g_main_loop_quit (loop);
}

/* Test that a Sierra CnS response to a Version Info command correctly
 * raises an error in the child's response handler.
 */
static void
test_sierra_cns_rejected (TestData *d)
{
    char req[512];
    gsize req_len;
    pid_t cpid;
    const char rsp[] = {
        0x7e, 0x00, 0x0a, 0x6b, 0x6d, 0x00, 0x00, 0x07, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e
    };

    signal (SIGCHLD, SIG_DFL);
    cpid = fork ();
    g_assert (cpid >= 0);

    if (cpid == 0) {
        /* In the child */
        qcdm_test_child (d->slave, (GAsyncReadyCallback)qcdm_verinfo_expect_fail_cb);
        exit (0);
    }
    /* Parent */
    d->child = cpid;

    req_len = server_wait_request (d->master, req, sizeof (req));
    g_assert (req_len == 1);
    g_assert_cmpint (req[0], ==, 0x00);

    server_send_response (d->master, rsp, sizeof (rsp));

    /* We expect the child to exit normally */
    g_assert (wait_for_child (d, 3));
}

/* Test that a random response to a Version Info command correctly
 * raises an error in the child's response handler.
 */
static void
test_random_data_rejected (TestData *d)
{
    char req[512];
    gsize req_len;
    pid_t cpid;
    const char rsp[] = {
        0x7e, 0x7e, 0x7e, 0x6b, 0x6d, 0x7e, 0x7e, 0x7e, 0x7e,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e
    };

    signal (SIGCHLD, SIG_DFL);
    cpid = fork ();
    g_assert (cpid >= 0);

    if (cpid == 0) {
        /* In the child */
        qcdm_test_child (d->slave, (GAsyncReadyCallback)qcdm_verinfo_expect_fail_cb);
        exit (0);
    }
    /* Parent */
    d->child = cpid;

    req_len = server_wait_request (d->master, req, sizeof (req));
    g_assert (req_len == 1);
    g_assert_cmpint (req[0], ==, 0x00);

    server_send_response (d->master, rsp, sizeof (rsp));

    /* We expect the child to exit normally */
    g_assert (wait_for_child (d, 3));
}

/* Test that a bunch of frame markers at the beginning of a valid response
 * to a Version Info command is parsed correctly.
 */
static void
test_leading_frame_markers (TestData *d)
{
    char req[512];
    gsize req_len;
    pid_t cpid;
    const char rsp[] = {
        0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e,
        0x00, 0x41, 0x75, 0x67, 0x20, 0x31, 0x39, 0x20, 0x32, 0x30, 0x30, 0x38,
        0x32, 0x30, 0x3a, 0x34, 0x38, 0x3a, 0x34, 0x37, 0x4f, 0x63, 0x74, 0x20,
        0x32, 0x39, 0x20, 0x32, 0x30, 0x30, 0x37, 0x31, 0x39, 0x3a, 0x30, 0x30,
        0x3a, 0x30, 0x30, 0x53, 0x43, 0x4e, 0x52, 0x5a, 0x2e, 0x2e, 0x2e, 0x2a,
        0x06, 0x04, 0xb9, 0x0b, 0x02, 0x00, 0xb2, 0x19, 0xc4, 0x7e
    };

    signal (SIGCHLD, SIG_DFL);
    cpid = fork ();
    g_assert (cpid >= 0);

    if (cpid == 0) {
        /* In the child */
        qcdm_test_child (d->slave, (GAsyncReadyCallback)qcdm_verinfo_expect_success_cb);
        exit (0);
    }
    /* Parent */
    d->child = cpid;

    req_len = server_wait_request (d->master, req, sizeof (req));
    g_assert (req_len == 1);
    g_assert_cmpint (req[0], ==, 0x00);

    server_send_response (d->master, rsp, sizeof (rsp));

    /* We expect the child to exit normally */
    g_assert (wait_for_child (d, 3));
}

static void
test_pty_create (TestData *d)
{
    struct termios stbuf;
    int ret, err;

    ret = openpty (&d->master, &d->slave, NULL, NULL, NULL);
    g_assert (ret == 0);
    d->valid = TRUE;

    /* set raw mode on the slave using kernel default parameters */
    memset (&stbuf, 0, sizeof (stbuf));
    tcgetattr (d->slave, &stbuf);
    tcflush (d->slave, TCIOFLUSH);
    cfmakeraw (&stbuf);
    tcsetattr (d->slave, TCSANOW, &stbuf);
    fcntl (d->slave, F_SETFL, O_NONBLOCK);

    fcntl (d->master, F_SETFL, O_NONBLOCK);
    err = qcdm_port_setup (d->master);
    g_assert_cmpint (err, ==, QCDM_SUCCESS);
}

static void
test_pty_cleanup (TestData *d)
{
    /* For some reason the cleanup function gets called more times
     * than the setup function does...
     */
    if (d->valid) {
        if (d->child)
            kill (d->child, SIGKILL);
        if (d->master >= 0)
            close (d->master);
        if (d->slave >= 0)
            close (d->slave);
        memset (d, 0, sizeof (*d));
    }
}

typedef void (*TCFunc) (TestData *, gconstpointer);
#define TESTCASE_PTY(s, t) g_test_add (s, TestData, NULL, (TCFunc)test_pty_create, (TCFunc)t, (TCFunc)test_pty_cleanup);

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    TESTCASE_PTY ("/MM/QCDM/Verinfo", test_verinfo);
    TESTCASE_PTY ("/MM/QCDM/Sierra-Cns-Rejected", test_sierra_cns_rejected);
    TESTCASE_PTY ("/MM/QCDM/Random-Data-Rejected", test_random_data_rejected);
    TESTCASE_PTY ("/MM/QCDM/Leading-Frame-Markers", test_leading_frame_markers);

    return g_test_run ();
}
