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
qcdm_set_mode_pref (int fd, uint8_t modepref)
{
	int err;
	char buf[512];
	size_t len;
	QcdmResult *result;
	size_t reply_len;

	len = qcdm_cmd_nv_set_mode_pref_new (buf, sizeof (buf), 0, modepref);
	assert (len);

	/* Send the command */
	if (!qcdm_send (fd, buf, len)) {
		fprintf (stderr, "E: failed to send QCDM mode pref command\n");
		return -1;
	}

	reply_len = qcdm_wait_reply (fd, buf, sizeof (buf));
	if (!reply_len) {
		fprintf (stderr, "E: failed to receive QCDM mode pref command reply\n");
		return -1;
	}

	/* Parse the response into a result structure */
	err = QCDM_SUCCESS;
	result = qcdm_cmd_nv_set_mode_pref_result (buf, reply_len, &err);
	if (!result) {
		fprintf (stderr, "E: failed to parse QCDM mode pref command reply: %d\n", err);
		return -1;
	}

	qcdm_result_unref (result);
	return 0;
}

static const char *
qcdm_get_mode_pref (int fd)
{
	int err;
	char buf[512];
	size_t len;
	QcdmResult *result;
	size_t reply_len;
	const char *smode = NULL;
	uint8_t mode = 0;

	len = qcdm_cmd_nv_get_mode_pref_new (buf, sizeof (buf), 0);
	assert (len);

	/* Send the command */
	if (!qcdm_send (fd, buf, len)) {
		fprintf (stderr, "E: failed to send QCDM mode pref command\n");
		return NULL;
	}

	reply_len = qcdm_wait_reply (fd, buf, sizeof (buf));
	if (!reply_len) {
		fprintf (stderr, "E: failed to receive QCDM mode pref command reply\n");
		return NULL;
	}

	/* Parse the response into a result structure */
	err = QCDM_SUCCESS;
	result = qcdm_cmd_nv_get_mode_pref_result (buf, reply_len, &err);
	if (!result) {
		fprintf (stderr, "E: failed to parse QCDM mode pref command reply: %d\n", err);
		return NULL;
	}

    err = qcdm_result_get_u8 (result, QCDM_CMD_NV_GET_MODE_PREF_ITEM_MODE_PREF, &mode);
	if (err == QCDM_SUCCESS) {
	    switch (mode) {
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_DIGITAL:
		    smode = "digital";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_DIGITAL_ONLY:
		    smode = "digital only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_AUTO:
		    smode = "automatic";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_ONLY:
		    smode = "CDMA 1x only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_HDR_ONLY:
		    smode = "HDR only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GPRS_ONLY:
		    smode = "GPRS only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_UMTS_ONLY:
		    smode = "UMTS only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_ONLY:
		    smode = "GSM and UMTS only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_ONLY:
		    smode = "CDMA 1x and HDR only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_LTE_ONLY:
		    smode = "LTE only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_LTE_ONLY:
		    smode = "GSM/UMTS/LTE only";
		    break;
		case QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_LTE_ONLY:
		    smode = "CDMA 1x, HDR, and LTE only";
		    break;
		default:
		    break;
		}
	}

	qcdm_result_unref (result);
	return smode;
}

static int
qcdm_set_hdr_pref (int fd, uint8_t hdrpref)
{
	int err;
	char buf[512];
	size_t len;
	QcdmResult *result;
	size_t reply_len;

	len = qcdm_cmd_nv_set_hdr_rev_pref_new (buf, sizeof (buf), hdrpref);
	assert (len);

	/* Send the command */
	if (!qcdm_send (fd, buf, len)) {
		fprintf (stderr, "E: failed to send QCDM HDR pref command\n");
		return -1;
	}

	reply_len = qcdm_wait_reply (fd, buf, sizeof (buf));
	if (!reply_len) {
		fprintf (stderr, "E: failed to receive HDR pref command reply\n");
		return -1;
	}

	/* Parse the response into a result structure */
	err = QCDM_SUCCESS;
	result = qcdm_cmd_nv_set_hdr_rev_pref_result (buf, reply_len, &err);
	if (!result) {
		fprintf (stderr, "E: failed to parse HDR pref command reply: %d\n", err);
		return -1;
	}

	qcdm_result_unref (result);
	return 0;
}

static const char *
qcdm_get_hdr_pref (int fd)
{
	int err;
	char buf[512];
	size_t len;
	QcdmResult *result = NULL;
	size_t reply_len;
    uint8_t pref;
    const char *spref = NULL;

    len = qcdm_cmd_nv_get_hdr_rev_pref_new (buf, sizeof (buf));
    assert (len > 0);

	/* Send the command */
	if (!qcdm_send (fd, buf, len)) {
		fprintf (stderr, "E: failed to send QCDM HDR pref command\n");
		goto error;
	}

	reply_len = qcdm_wait_reply (fd, buf, sizeof (buf));
	if (!reply_len) {
		fprintf (stderr, "E: failed to receive HDR pref command reply\n");
		goto error;
	}

	/* Parse the response into a result structure */
	err = QCDM_SUCCESS;
	result = qcdm_cmd_nv_get_hdr_rev_pref_result (buf, reply_len, &err);
	if (!result) {
		fprintf (stderr, "E: failed to parse HDR pref command reply: %d\n", err);
		goto error;
	}

    err = qcdm_result_get_u8 (result, QCDM_CMD_NV_GET_HDR_REV_PREF_ITEM_REV_PREF, &pref);
	if (err != QCDM_SUCCESS)
		goto error;

    switch (pref) {
    case QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_0:
        spref = "rev0";
        break;
    case QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A:
        spref = "revA";
        break;
    case QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD:
        spref = "eHRPD";
        break;
    default:
        break;
    }

error:
	if (result)
		qcdm_result_unref (result);
	return spref;
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
	fprintf (stderr, "Usage: %s <DM port> [<mode>] [--debug]\n", prog);
	fprintf (stderr, "         <mode> = auto, lte, auto-cdma-lte, auto-cdma, cdma, evdo, auto-gsm-lte, auto-gsm, gsm, umts\n");
	fprintf (stderr, "         If <mode> is missing, current mode will be printed.\n\n");
}

static qcdmbool
parse_mode (const char *s,
            uint8_t *out_mode,
            uint8_t *out_hdrpref,
            qcdmbool *out_set_evdo)
{
	if (strcasecmp (s, "lte") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_LTE_ONLY;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD;
		return TRUE;
	}

	if (strcasecmp (s, "auto-cdma-lte") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_LTE_ONLY;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "auto-cdma") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_HDR_ONLY;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "auto") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_AUTO;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "cdma") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_1X_ONLY;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "evdo") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_HDR_ONLY;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "auto-gsm-lte") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_LTE_ONLY;
		return TRUE;
	}

	if (strcasecmp (s, "auto-gsm") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GSM_UMTS_ONLY;
		return TRUE;
	}

	if (strcasecmp (s, "gsm") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_GPRS_ONLY;
		return TRUE;
	}

	if (strcasecmp (s, "umts") == 0) {
		*out_mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_UMTS_ONLY;
		return TRUE;
	}

	return FALSE;
}

int
main (int argc, char *argv[])
{
	uint8_t mode = QCDM_CMD_NV_MODE_PREF_ITEM_MODE_PREF_AUTO;
	uint8_t hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD;
	const char *dmport = argv[1];
	const char *smode = argv[2];
	const char *msg;
	qcdmbool set_evdo = FALSE;
	qcdmbool set_mode = FALSE;
	int fd, err;

	if (argc < 2 || argc > 4) {
		usage (argv[0]);
		return 1;
	}

	if (argc >= 3) {
		if (strcasecmp (argv[2], "--debug") == 0)
			debug = 1;
		else {
			set_mode = parse_mode (argv[2], &mode, &hdrpref, &set_evdo);
			if (!set_mode) {
				usage (argv[0]);
				return 1;
			}
		}

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

	if (set_mode) {
		if (qcdm_set_mode_pref (fd, mode))
			return 1;
		if (set_evdo && qcdm_set_hdr_pref (fd, hdrpref))
			return 1;

		/* Send DM reset command */
		qcdm_set_mode (fd, QCDM_CMD_CONTROL_MODE_OFFLINE);
		sleep (2);
		qcdm_set_mode (fd, QCDM_CMD_CONTROL_MODE_RESET);
		sleep (2);

		fprintf (stdout, "Success setting mode to '%s': replug your device.\n", smode);
	} else {
		msg = qcdm_get_mode_pref (fd);
		fprintf (stdout, "Mode preference: %s\n", msg ? msg : "(unknown)");
		msg = qcdm_get_hdr_pref (fd);
		fprintf (stdout, "HDR revision:    %s\n", msg ? msg : "(unknown)");
	}

	return 0;
}
