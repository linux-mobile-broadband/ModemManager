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

#include "libwmc/src/utils.h"
#include "libwmc/src/errors.h"
#include "libwmc/src/commands.h"
#include "libwmc/src/com.h"

#include "libqcdm/src/utils.h"
#include "libqcdm/src/errors.h"
#include "libqcdm/src/commands.h"
#include "libqcdm/src/com.h"

static int debug = 0;

static void
print_buf (const char *detail, const char *buf, size_t len)
{
	int i = 0, z;
	wmcbool newline = FALSE;
	char tmp[500];
	u_int32_t flen;

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

static wmcbool
wmc_send (int fd, char *inbuf, size_t inbuf_len, size_t cmd_len)
{
	int status;
	int eagain_count = 1000;
	size_t i = 0, sendlen;
	char sendbuf[600];

	if (debug)
		print_buf ("\nWMC:RAW>>>", inbuf, cmd_len);

	/* Encapsulate the data for the device */
	sendlen = wmc_encapsulate (inbuf, cmd_len, inbuf_len, sendbuf, sizeof (sendbuf), TRUE);
	if (sendlen <= 0) {
		fprintf (stderr, "E: failed to encapsulate WMC command\n");
		return FALSE;
	}

	if (debug)
		print_buf ("WMC:ENC>>>", sendbuf, sendlen);

	while (i < sendlen) {
		errno = 0;
		status = write (fd, &sendbuf[i], 1);
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
wmc_wait_reply (int fd, char *buf, size_t len)
{
	fd_set in;
	int result;
	struct timeval timeout = { 1, 0 };
	char readbuf[2048];
	ssize_t bytes_read;
	int total = 0, retries = 0;
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
			wmcbool more = FALSE, success;
			size_t used = 0;

			total++;
			decap_len = 0;
			success = wmc_decapsulate (readbuf, total, buf, len, &decap_len, &used, &more, TRUE);

			if (success && !more && debug)
				print_buf ("WMC:RAW<<<", readbuf, total);

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
		print_buf ("WMC:DEC<<<", buf, decap_len);

	return decap_len;
}

static int
wmc_set_global_mode (const char *port, u_int8_t mode)
{
	int fd, err;
	char buf[1024];
	size_t len;
	WmcResult *result;
	size_t reply_len;

	fd = com_setup (port);
	if (fd < 0)
		return -1;

	err = wmc_port_setup (fd);
	if (err) {
		fprintf (stderr, "E: failed to set up WMC port %s: %d\n", port, err);
		goto error;
	}

	len = wmc_cmd_set_global_mode_new (buf, sizeof (buf), mode);
	assert (len);

	/* Send the command */
	if (!wmc_send (fd, buf, sizeof (buf), len)) {
		fprintf (stderr, "E: failed to send WMC global mode command\n");
		goto error;
	}

	reply_len = wmc_wait_reply (fd, buf, sizeof (buf));
	if (!reply_len) {
		fprintf (stderr, "E: failed to receive global mode command reply\n");
		goto error;
	}

	/* Parse the response into a result structure */
	result = wmc_cmd_set_global_mode_result (buf, reply_len);
	if (!result) {
		fprintf (stderr, "E: failed to parse global mode command reply\n");
		goto error;
	}
	wmc_result_unref (result);

	close (fd);
	return 0;

error:
	close (fd);
	return -1;
}

static const char *
wmc_get_global_mode (const char *port)
{
	int fd, err;
	char buf[1024];
	size_t len;
	WmcResult *result;
	size_t reply_len;
	u_int8_t mode = 0;
	const char *smode = NULL;

	fd = com_setup (port);
	if (fd < 0)
		return NULL;

	err = wmc_port_setup (fd);
	if (err) {
		fprintf (stderr, "E: failed to set up WMC port %s: %d\n", port, err);
		goto error;
	}

	len = wmc_cmd_get_global_mode_new (buf, sizeof (buf));
	assert (len);

	/* Send the command */
	if (!wmc_send (fd, buf, sizeof (buf), len)) {
		fprintf (stderr, "E: failed to send WMC global mode command\n");
		goto error;
	}

	reply_len = wmc_wait_reply (fd, buf, sizeof (buf));
	if (!reply_len) {
		fprintf (stderr, "E: failed to receive global mode command reply\n");
		goto error;
	}

	/* Parse the response into a result structure */
	result = wmc_cmd_get_global_mode_result (buf, reply_len);
	if (!result) {
		fprintf (stderr, "E: failed to parse global mode command reply\n");
		goto error;
	}
	wmc_result_unref (result);

    wmc_result_get_u8 (result, WMC_CMD_GET_GLOBAL_MODE_ITEM_MODE, &mode);
    switch (mode) {
    case WMC_NETWORK_MODE_AUTO_CDMA:
        smode = "CDMA/EVDO";
        break;
    case WMC_NETWORK_MODE_CDMA_ONLY:
        smode = "CDMA only";
        break;
    case WMC_NETWORK_MODE_EVDO_ONLY:
        smode = "EVDO only";
        break;
    case WMC_NETWORK_MODE_AUTO_GSM:
        smode = "GSM/UMTS";
        break;
    case WMC_NETWORK_MODE_GPRS_ONLY:
        smode = "GSM/GPRS/EDGE only";
        break;
    case WMC_NETWORK_MODE_UMTS_ONLY:
        smode = "UMTS/HSPA only";
        break;
    case WMC_NETWORK_MODE_AUTO:
        smode = "Auto";
        break;
    case WMC_NETWORK_MODE_LTE_ONLY:
        smode = "LTE only";
        break;
    default:
		break;
    }

	close (fd);
	return smode;

error:
	close (fd);
	return NULL;
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
	int total = 0, retries = 0;
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
qcdm_set_hdr_pref (const char *port, u_int8_t hdrpref)
{
	int fd, err;
	char buf[512];
	size_t len;
	QcdmResult *result;
	size_t reply_len;

	fd = com_setup (port);
	if (fd < 0)
		return -1;

	err = qcdm_port_setup (fd);
	if (err != QCDM_SUCCESS) {
		fprintf (stderr, "E: failed to set up DM port %s: %d\n", port, err);
		goto error;
	}

	len = qcdm_cmd_nv_set_hdr_rev_pref_new (buf, sizeof (buf), hdrpref);
	assert (len);

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
	result = qcdm_cmd_nv_set_hdr_rev_pref_result (buf, reply_len, &err);
	if (!result) {
		fprintf (stderr, "E: failed to parse HDR pref command reply: %d\n", err);
		goto error;
	}

	qcdm_result_unref (result);
	close (fd);
	return 0;

error:
	close (fd);
	return -1;
}

static const char *
qcdm_get_hdr_pref (const char *port)
{
	int fd, err;
	char buf[512];
	size_t len;
	QcdmResult *result = NULL;
	size_t reply_len;
    u_int8_t pref;
    const char *spref = NULL;

	fd = com_setup (port);
	if (fd < 0)
		return NULL;

	err = qcdm_port_setup (fd);
	if (err != QCDM_SUCCESS) {
		fprintf (stderr, "E: failed to set up DM port %s: %d\n", port, err);
		goto error;
	}

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

	qcdm_result_unref (result);
	close (fd);
	return spref;

error:
	if (result)
		qcdm_result_unref (result);
	close (fd);
	return NULL;
}

static int
qcdm_set_mode (const char *port, u_int8_t mode)
{
	int fd, err;
	char buf[512];
	size_t len;
	QcdmResult *result;
	size_t reply_len;

	fd = com_setup (port);
	if (fd < 0)
		return -1;

	err = qcdm_port_setup (fd);
	if (err != QCDM_SUCCESS) {
		fprintf (stderr, "E: failed to set up DM port %s: %d\n", port, err);
		goto error;
	}

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
	close (fd);
	return 0;

error:
	close (fd);
	return -1;
}

/******************************************************************/

static void
usage (const char *prog)
{
	fprintf (stderr, "Usage: %s <WMC port> <DM port> [<mode>] [--debug]\n", prog);
	fprintf (stderr, "         <mode> = lte, auto-cdma, auto, cdma, evdo, auto-gsm, gprs, umts\n");
	fprintf (stderr, "         If <mode> is missing, current mode will be printed.\n\n");
}

static wmcbool
parse_mode (const char *s,
            u_int8_t *out_mode,
            u_int8_t *out_hdrpref,
            wmcbool *out_set_evdo)
{
	if (strcasecmp (s, "lte") == 0) {
		*out_mode = WMC_NETWORK_MODE_LTE_ONLY;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "auto-cdma") == 0) {
		*out_mode = WMC_NETWORK_MODE_AUTO_CDMA;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "auto") == 0) {
		*out_mode = WMC_NETWORK_MODE_AUTO;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "cdma") == 0) {
		*out_mode = WMC_NETWORK_MODE_CDMA_ONLY;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "evdo") == 0) {
		*out_mode = WMC_NETWORK_MODE_EVDO_ONLY;
		*out_hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_A;
		*out_set_evdo = TRUE;
		return TRUE;
	}

	if (strcasecmp (s, "auto-gsm") == 0) {
		*out_mode = WMC_NETWORK_MODE_AUTO_GSM;
		*out_set_evdo = FALSE;
		return TRUE;
	}

	if (strcasecmp (s, "gprs") == 0) {
		*out_mode = WMC_NETWORK_MODE_GPRS_ONLY;
		*out_set_evdo = FALSE;
		return TRUE;
	}

	if (strcasecmp (s, "umts") == 0) {
		*out_mode = WMC_NETWORK_MODE_UMTS_ONLY;
		*out_set_evdo = FALSE;
		return TRUE;
	}

	return FALSE;
}


int
main (int argc, char *argv[])
{
	u_int8_t mode = WMC_NETWORK_MODE_AUTO;
	u_int8_t hdrpref = QCDM_CMD_NV_HDR_REV_PREF_ITEM_REV_PREF_EHRPD;
	const char *wmcport = argv[1];
	const char *dmport = argv[2];
	const char *msg = NULL;
	wmcbool set_evdo = FALSE;
	wmcbool set_mode = FALSE;

	if (argc < 3 || argc > 5) {
		usage (argv[0]);
		return 1;
	}

	if (argc >= 4) {
		if (strcasecmp (argv[3], "--debug") == 0)
			debug = 1;
		else {
			set_mode = parse_mode (argv[3], &mode, &hdrpref, &set_evdo);
			if (!set_mode) {
				usage (argv[0]);
				return 1;
			}
		}

		if (argc >= 5 && strcasecmp (argv[4], "--debug") == 0)
			debug = 1;
	}

	if (debug) {
		putenv ("WMC_DEBUG=1");
		putenv ("QCDM_DEBUG=1");
	}

	if (set_mode) {
		if (wmc_set_global_mode (wmcport, mode))
			return 1;
		if (set_evdo && qcdm_set_hdr_pref (dmport, hdrpref))
			return 1;

		/* Send DM reset command */
		qcdm_set_mode (dmport, QCDM_CMD_CONTROL_MODE_OFFLINE);
		sleep (2);
		qcdm_set_mode (dmport, QCDM_CMD_CONTROL_MODE_RESET);
		sleep (2);

		fprintf (stdout, "Success setting mode to '%s': replug your device.\n", argv[3]);
	} else {
		msg = wmc_get_global_mode (wmcport);
		fprintf (stdout, "WMC Global Mode:   %s\n", msg ? msg : "(unknown)");
		msg = qcdm_get_hdr_pref (dmport);
		fprintf (stdout, "QCDM HDR Revision: %s\n", msg ? msg : "(unknown)");
	}

	return 0;
}

