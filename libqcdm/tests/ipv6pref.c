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
qcdm_set_ipv6_enabled (int fd, uint8_t ipv6pref)
{
	int err;
	char buf[512];
	size_t len;
	QcdmResult *result;
	size_t reply_len;

	len = qcdm_cmd_nv_set_ipv6_enabled_new (buf, sizeof (buf), ipv6pref);
	assert (len);

	/* Send the command */
	if (!qcdm_send (fd, buf, len)) {
		fprintf (stderr, "E: failed to send QCDM IPv6 enabled command\n");
		return -1;
	}

	reply_len = qcdm_wait_reply (fd, buf, sizeof (buf));
	if (!reply_len) {
		fprintf (stderr, "E: failed to receive QCDM IPv6 enabled command reply\n");
		return -1;
	}

	/* Parse the response into a result structure */
	err = QCDM_SUCCESS;
	result = qcdm_cmd_nv_set_ipv6_enabled_result (buf, reply_len, &err);
	if (!result) {
		fprintf (stderr, "E: failed to parse QCDM IPv6 enabled command reply: %d\n", err);
		return -1;
	}

	qcdm_result_unref (result);
	return 0;
}

static int
qcdm_get_ipv6_enabled (int fd)
{
	int err;
	char buf[512];
	size_t len;
	QcdmResult *result;
	size_t reply_len;
	uint8_t mode;

	len = qcdm_cmd_nv_get_ipv6_enabled_new (buf, sizeof (buf));
	assert (len);

	/* Send the command */
	if (!qcdm_send (fd, buf, len)) {
		fprintf (stderr, "E: failed to send QCDM IPv6 enabled command\n");
		return -1;
	}

	reply_len = qcdm_wait_reply (fd, buf, sizeof (buf));
	if (!reply_len) {
		fprintf (stderr, "E: failed to receive QCDM IPv6 pref command reply\n");
		return -1;
	}

	/* Parse the response into a result structure */
	err = QCDM_SUCCESS;
	result = qcdm_cmd_nv_get_ipv6_enabled_result (buf, reply_len, &err);
	if (!result) {
		/* An inactive NVRAM entry has the same effect as setting "disabled" */
		if (err == -QCDM_ERROR_NV_ERROR_INACTIVE)
			return QCDM_CMD_NV_IPV6_ENABLED_OFF;

		fprintf (stderr, "E: failed to parse QCDM IPv6 enabled command reply: %d\n", err);
		return -1;
	}

    err = qcdm_result_get_u8 (result, QCDM_CMD_NV_GET_IPV6_ENABLED_ITEM_ENABLED, &mode);
	qcdm_result_unref (result);
	return mode;
}

/******************************************************************/

static void
usage (const char *prog)
{
	fprintf (stderr, "Usage: %s <DM port> [--enable|--disable] [--debug]\n", prog);
	fprintf (stderr, "         Current mode will always be printed.\n\n");
}

int
main (int argc, char *argv[])
{
	const char *dmport = argv[1];
	int fd, err, old, new = -1;

	if (argc < 2 || argc > 4) {
		usage (argv[0]);
		return 1;
	}

	if (argc >= 3) {
		if (strcasecmp (argv[2], "--debug") == 0)
			debug = 1;
		else if (strcasecmp (argv[2], "--enable") == 0)
			new = QCDM_CMD_NV_IPV6_ENABLED_ON;
		else if (strcasecmp (argv[2], "--disable") == 0)
			new = QCDM_CMD_NV_IPV6_ENABLED_OFF;
		else
			usage (argv[0]);

		if (argc >= 4 && strcasecmp (argv[3], "--debug") == 0)
			debug = 1;
	}

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

	old = qcdm_get_ipv6_enabled (fd);
	if (old < 0) {
		fprintf (stderr, "E: failed to get IPv6 state\n");
		return 1;
	}
	fprintf (stdout, "IPv6: %s\n", old ? "enabled" : "disabled");

	if (new >=0 && old != new) {
		if (qcdm_set_ipv6_enabled (fd, new))
			fprintf (stdout, "Failed to %s IPv6\n", new ? "enable" : "disable");
		else
			fprintf (stdout, "IPv6 successfully %s. Replug your device.\n", new ? "enabled" : "disabled");
	}

	return 0;
}
