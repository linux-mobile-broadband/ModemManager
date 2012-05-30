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

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <malloc.h>
#include <fcntl.h>
#include <string.h>

#include "utils.h"
#include "errors.h"

/* QCDM protocol frames are pseudo Async HDLC frames which end with a 3-byte
 * trailer.  This trailer consists of the 16-bit CRC of the frame plus an ending
 * "async control character" whose value is 0x7E.  The frame *and* the CRC are
 * escaped before adding the trailing control character so that the control
 * character (0x7E) and the escape marker (0x7D) are never seen in the frame.
 */

/* Table of CRCs for each possible byte, with a generator polynomial of 0x8408 */
static const u_int16_t crc_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

/* Calculate the CRC for a buffer using a seed of 0xffff */
u_int16_t
wmc_crc16 (const char *buffer, size_t len, u_int16_t seed)
{
    u_int16_t crc = seed ? seed : 0xFFFF;

    while (len--)
            crc = crc_table[(crc ^ *buffer++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

#define DIAG_ESC_CHAR     0x7D  /* Escape sequence 1st character value */
#define DIAG_ESC_MASK     0x20  /* Escape sequence complement value */

/* Performs DM escaping on inbuf putting the result into outbuf, and returns
 * the final length of the buffer.
 */
size_t
hdlc_escape (const char *inbuf,
             size_t inbuf_len,
             wmcbool escape_all_ctrl,
             char *outbuf,
             size_t outbuf_len)
{
    const char *src = inbuf;
    char *dst = outbuf;
    size_t i = inbuf_len;

    wmc_return_val_if_fail (inbuf != NULL, 0);
    wmc_return_val_if_fail (inbuf_len > 0, 0);
    wmc_return_val_if_fail (outbuf != NULL, 0);
    wmc_return_val_if_fail (outbuf_len > inbuf_len, 0);

    /* Since escaping potentially doubles the # of bytes, short-circuit the
     * length check if destination buffer is clearly large enough.  Note the
     * 
     */
    if (outbuf_len <= inbuf_len << 1) {
        size_t outbuf_required = inbuf_len + 1; /* +1 for the trailing control char */

        /* Each escaped character takes up two bytes in the output buffer */
        while (i--) {
            if (   *src == DIAG_CONTROL_CHAR
                || *src == DIAG_ESC_CHAR
                || (escape_all_ctrl && *src <= 0x20))
                outbuf_required++;
            src++;
        }

        if (outbuf_len < outbuf_required)
            return 0;
    }

    /* Do the actual escaping.  Replace both the control character and
     * the escape character in the source buffer with the following sequence:
     *
     * <escape_char> <src_byte ^ escape_mask>
     */
    src = inbuf;
    i = inbuf_len;
    while (i--) {
        u_int8_t byte = (u_int8_t) *src++;

        if (   byte == DIAG_CONTROL_CHAR
            || byte == DIAG_ESC_CHAR
            || (escape_all_ctrl && byte <= 0x20)) {
            *dst++ = DIAG_ESC_CHAR;
            *dst++ = byte ^ DIAG_ESC_MASK;
        } else
            *dst++ = byte;
    }

    return (dst - outbuf);
}

size_t
hdlc_unescape (const char *inbuf,
               size_t inbuf_len,
               char *outbuf,
               size_t outbuf_len,
               wmcbool *escaping)
{
    size_t i, outsize;

    wmc_return_val_if_fail (inbuf_len > 0, 0);
    wmc_return_val_if_fail (outbuf_len >= inbuf_len, 0);
    wmc_return_val_if_fail (escaping != NULL, 0);

    for (i = 0, outsize = 0; i < inbuf_len; i++) {
        if (*escaping) {
            outbuf[outsize++] = inbuf[i] ^ DIAG_ESC_MASK;
            *escaping = FALSE;
        } else if (inbuf[i] == DIAG_ESC_CHAR)
            *escaping = TRUE;
        else
            outbuf[outsize++] = inbuf[i];

        /* About to overrun output buffer size */
        if (outsize >= outbuf_len)
            return 0;
    }

    return outsize;
}

/**
 * hdlc_encapsulate_buffer:
 * @inbuf: data buffer to encapsulate
 * @cmd_len: size of the data contained in @inbuf
 * @inbuf_len: total size of @inbuf itself (not just the data)
 * @crc_seed: if non-zero, CRC-16 seed to use; if 0, uses standard 0xFFFF
 * @add_trailer: if %TRUE, adds trailing 0x7E
 * @escape_all_ctrl: if %TRUE, escapes all control characters instead of only
 * special HDLC escape characters 0x7D and 0x7E
 * @outbuf: buffer in which to put the encapsulated data
 * @outbuf_len: total size of @outbuf
 *
 * Escapes and CRCs given data using HDLC-style mechanisms, and optionally adds
 * the trailing control character that denotes the end of the HDLC frame.
 *
 * Returns: size of the encapsulated data writted to @outbuf.
 **/
size_t
hdlc_encapsulate_buffer (char *inbuf,
                         size_t cmd_len,
                         size_t inbuf_len,
                         u_int16_t crc_seed,
                         wmcbool add_trailer,
                         wmcbool escape_all_ctrl,
                         char *outbuf,
                         size_t outbuf_len)
{
    u_int16_t crc;
    size_t escaped_len;

    wmc_return_val_if_fail (inbuf != NULL, 0);
    wmc_return_val_if_fail (cmd_len >= 1, 0);
    wmc_return_val_if_fail (inbuf_len >= cmd_len + 2, 0); /* space for CRC */
    wmc_return_val_if_fail (outbuf != NULL, 0);

    /* Add the CRC */
    crc = wmc_crc16 (inbuf, cmd_len, crc_seed ? crc_seed : 0xFFFF);
    inbuf[cmd_len++] = crc & 0xFF;
    inbuf[cmd_len++] = (crc >> 8) & 0xFF;

    escaped_len = hdlc_escape (inbuf, cmd_len, escape_all_ctrl, outbuf, outbuf_len);
    wmc_return_val_if_fail (outbuf_len > escaped_len, 0);

    if (add_trailer)
        outbuf[escaped_len++] = DIAG_CONTROL_CHAR;

    return escaped_len;
}

#define AT_WMC_PREFIX "AT*WMC="

/**
 * uml290_wmc_encapsulate:
 * @inbuf: data buffer to encapsulate
 * @cmd_len: size of the data contained in @inbuf
 * @inbuf_len: total size of @inbuf itself (not just the data)
 * @outbuf: buffer in which to put the encapsulated data
 * @outbuf_len: total size of @outbuf
 *
 * Escapes and CRCs given data using HDLC-style mechanisms with UML290 specific
 * quirks.
 *
 * Returns: size of the encapsulated data writted to @outbuf.
 */
static size_t
uml290_wmc_encapsulate (char *inbuf,
                        size_t cmd_len,
                        size_t inbuf_len,
                        char *outbuf,
                        size_t outbuf_len)
{
    size_t encap_len;
    size_t estimated_out_len;

    wmc_return_val_if_fail (inbuf != NULL, 0);
    wmc_return_val_if_fail (cmd_len >= 1, 0);
    wmc_return_val_if_fail (inbuf_len >= cmd_len + 2, 0); /* space for CRC */
    wmc_return_val_if_fail (outbuf != NULL, 0);

    estimated_out_len = cmd_len + strlen (AT_WMC_PREFIX);
    estimated_out_len += 3;  /* CRC + trailer */
    estimated_out_len += cmd_len * 2;  /* escaping */
    wmc_return_val_if_fail (outbuf_len > estimated_out_len, 0);

    memcpy (outbuf, AT_WMC_PREFIX, strlen (AT_WMC_PREFIX));

    encap_len = hdlc_encapsulate_buffer (inbuf, cmd_len, inbuf_len, 0xAAFE,
                                         FALSE, TRUE,
                                         outbuf + strlen (AT_WMC_PREFIX),
                                         outbuf_len);
    if (encap_len > 0) {
        encap_len += strlen (AT_WMC_PREFIX);
        outbuf[encap_len++] = 0x0D;  /* trailer */
    }

    return encap_len;
}

/**
 * wmc_encapsulate:
 * @inbuf: data buffer to encapsulate
 * @cmd_len: size of the data contained in @inbuf
 * @inbuf_len: total size of @inbuf itself (not just the data)
 * @outbuf: buffer in which to put the encapsulated data
 * @outbuf_len: total size of @outbuf
 * @uml290: if %TRUE return buffer suitable for sending to UML290 devices
 *
 * Escapes and CRCs given data using HDLC-style mechanisms.
 *
 * Returns: size of the encapsulated data writted to @outbuf.
 */
size_t
wmc_encapsulate (char *inbuf,
                 size_t cmd_len,
                 size_t inbuf_len,
                 char *outbuf,
                 size_t outbuf_len,
                 wmcbool uml290)
{
    wmc_return_val_if_fail (inbuf != NULL, 0);
    wmc_return_val_if_fail (cmd_len >= 1, 0);
    wmc_return_val_if_fail (inbuf_len >= cmd_len + 3, 0); /* space for CRC + trailer */
    wmc_return_val_if_fail (outbuf != NULL, 0);

    if (uml290)
        return uml290_wmc_encapsulate (inbuf, cmd_len, inbuf_len, outbuf, outbuf_len);

    /* Otherwise do normal WMC */
    return hdlc_encapsulate_buffer (inbuf, cmd_len, inbuf_len,
                                    0, TRUE, FALSE, outbuf, outbuf_len);
}

/**
 * hdlc_decapsulate_buffer:
 * @inbuf: buffer in which to look for an HDLC frame
 * @inbuf_len: length of valid data in @inbuf
 * @check_known_crc: if %TRUE, validate the CRC using @known_crc if the normal
 *  CRC check fails
 * @known_crc: if @check_known_crc is %TRUE, compare the frame's CRC against
 *  @known_crc if the normal CRC check fails.  @known_crc must be in Little
 *  Endian (LE) byte order.
 * @outbuf: buffer in which to put decapsulated data from the HDLC frame
 * @outbuf_len: max size of @outbuf
 * @out_decap_len: on success, size of the decapsulated data
 * @out_used: on either success or failure, amount of data used; caller should
 *  discard this much data from @inbuf before the next call to this function
 * @out_need_more: when TRUE, indicates that more data is required before
 *  a determination about a valid HDLC frame can be made; caller should add
 *  more data to @inbuf before calling this function again.
 *
 * Attempts to retrieve, unescape, and CRC-check an HDLC frame from the given
 * buffer.
 *
 * Returns: FALSE on error (packet was invalid or malformed, or the CRC check
 *  failed, etc) and places number of bytes to discard from @inbuf in @out_used.
 *  When TRUE, either more data is required (in which case @out_need_more will
 *  be TRUE), or a data packet was successfully retrieved from @inbuf and the
 *  decapsulated packet of length @out_decap_len was placed into @outbuf.  In
 *  all cases the caller should advance the buffer by the number of bytes
 *  returned in @out_used before calling this function again.
 **/
wmcbool
hdlc_decapsulate_buffer (const char *inbuf,
                         size_t inbuf_len,
                         wmcbool check_known_crc,
                         u_int16_t known_crc,
                         char *outbuf,
                         size_t outbuf_len,
                         size_t *out_decap_len,
                         size_t *out_used,
                         wmcbool *out_need_more)
{
    wmcbool escaping = FALSE;
    size_t i, pkt_len = 0, unesc_len;
    u_int16_t crc, pkt_crc;

    wmc_return_val_if_fail (inbuf != NULL, FALSE);
    wmc_return_val_if_fail (outbuf != NULL, FALSE);
    wmc_return_val_if_fail (outbuf_len > 0, FALSE);
    wmc_return_val_if_fail (out_decap_len != NULL, FALSE);
    wmc_return_val_if_fail (out_used != NULL, FALSE);
    wmc_return_val_if_fail (out_need_more != NULL, FALSE);

    *out_decap_len = 0;
    *out_used = 0;
    *out_need_more = FALSE;

    if (inbuf_len < 4) {
        *out_need_more = TRUE;
        return TRUE;
    }

    /* Find the async control character */
    for (i = 0; i < inbuf_len; i++) {
        if (inbuf[i] == DIAG_CONTROL_CHAR) {
            /* If the control character shows up in a position before a valid
             * QCDM packet length (4), the packet is malformed.
             */
            if (i < 3) {
                /* Tell the caller to advance the buffer past the control char */
                *out_used = i + 1;
                return FALSE;
            }

            pkt_len = i;
            break;
        }
    }

    /* No control char yet, need more data */
    if (!pkt_len) {
        *out_need_more = TRUE;
        return TRUE;
    }

    /* Unescape first; note that pkt_len */
    unesc_len = hdlc_unescape (inbuf, pkt_len, outbuf, outbuf_len, &escaping);
    if (!unesc_len) {
        /* Tell the caller to advance the buffer past the control char */
        *out_used = pkt_len + 1;
        return FALSE;
    }

    if (escaping) {
        *out_need_more = TRUE;
        return TRUE;
    }

    /* Check the CRC of the packet's data */
    crc = wmc_crc16 (outbuf, unesc_len - 2, 0);
    pkt_crc = outbuf[unesc_len - 2] & 0xFF;
    pkt_crc |= (outbuf[unesc_len - 1] & 0xFF) << 8;
    if (crc != pkt_crc) {
        if (!check_known_crc || (pkt_crc != known_crc)) {
            *out_used = pkt_len + 1; /* packet + CRC + 0x7E */
            return FALSE;
        }
    }

    *out_used = pkt_len + 1; /* packet + CRC + 0x7E */
    *out_decap_len = unesc_len - 2; /* decap_len should not include the CRC */
    return TRUE;
}

/**
 * wmc_decapsulate:
 * @inbuf: buffer in which to look for an HDLC frame
 * @inbuf_len: length of valid data in @inbuf
 * @outbuf: buffer in which to put decapsulated data from the HDLC frame
 * @outbuf_len: max size of @outbuf
 * @out_decap_len: on success, size of the decapsulated data
 * @out_used: on either success or failure, amount of data used; caller should
 *  discard this much data from @inbuf before the next call to this function
 * @out_need_more: when TRUE, indicates that more data is required before
 *  a determination about a valid HDLC frame can be made; caller should add
 *  more data to @inbuf before calling this function again.
 * @uml290: if %TRUE decapsulate response from UML290 devices
 *
 * Attempts to retrieve, unescape, and CRC-check an HDLC frame from the given
 * buffer.
 *
 * Returns: FALSE on error (packet was invalid or malformed, or the CRC check
 *  failed, etc) and places number of bytes to discard from @inbuf in @out_used.
 *  When TRUE, either more data is required (in which case @out_need_more will
 *  be TRUE), or a data packet was successfully retrieved from @inbuf and the
 *  decapsulated packet of length @out_decap_len was placed into @outbuf.  In
 *  all cases the caller should advance the buffer by the number of bytes
 *  returned in @out_used before calling this function again.
 **/
wmcbool
wmc_decapsulate (const char *inbuf,
                 size_t inbuf_len,
                 char *outbuf,
                 size_t outbuf_len,
                 size_t *out_decap_len,
                 size_t *out_used,
                 wmcbool *out_need_more,
                 wmcbool uml290)
{
    return hdlc_decapsulate_buffer (inbuf, inbuf_len,
                                    uml290, uml290 ? 0x3030 : 0,
                                    outbuf, outbuf_len,
                                    out_decap_len, out_used, out_need_more);
}

