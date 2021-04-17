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
 * Copyright (C) 2011 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Google, Inc.
 */

#include <ctype.h>
#include <string.h>

#include <glib.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-helper-enums-types.h"
#include "mm-sms-part-3gpp.h"
#include "mm-charsets.h"
#include "mm-log.h"

#define PDU_SIZE 200

#define SMS_TP_MTI_MASK               0x03
#define  SMS_TP_MTI_SMS_DELIVER       0x00
#define  SMS_TP_MTI_SMS_SUBMIT        0x01
#define  SMS_TP_MTI_SMS_STATUS_REPORT 0x02

#define SMS_NUMBER_TYPE_MASK          0x70
#define SMS_NUMBER_TYPE_UNKNOWN       0x00
#define SMS_NUMBER_TYPE_INTL          0x10
#define SMS_NUMBER_TYPE_ALPHA         0x50

#define SMS_NUMBER_PLAN_MASK          0x0f
#define SMS_NUMBER_PLAN_TELEPHONE     0x01

#define SMS_TP_MMS                    0x04
#define SMS_TP_SRI                    0x20
#define SMS_TP_UDHI                   0x40
#define SMS_TP_RP                     0x80

#define SMS_DCS_CODING_MASK           0xec
#define  SMS_DCS_CODING_DEFAULT       0x00
#define  SMS_DCS_CODING_8BIT          0x04
#define  SMS_DCS_CODING_UCS2          0x08

#define SMS_DCS_CLASS_VALID           0x10
#define SMS_DCS_CLASS_MASK            0x03

#define SMS_TIMESTAMP_LEN 7
#define SMS_MIN_PDU_LEN (7 + SMS_TIMESTAMP_LEN)

static char sms_bcd_chars[] = "0123456789*#abc\0\0";

static void
sms_semi_octets_to_bcd_string (char *dest, const guint8 *octets, int num_octets)
{
    int i;

    for (i = 0 ; i < num_octets; i++) {
        *dest++ = sms_bcd_chars[octets[i] & 0xf];
        *dest++ = sms_bcd_chars[(octets[i] >> 4) & 0xf];
    }
    *dest++ = '\0';
}

static gboolean
char_to_bcd (char in, guint8 *out)
{
    guint32 z;

    if (isdigit (in)) {
        *out = in - 0x30;
        return TRUE;
    }

    for (z = 10; z < 16; z++) {
        if (in == sms_bcd_chars[z]) {
            *out = z;
            return TRUE;
        }
    }
    return FALSE;
}

static gsize
sms_string_to_bcd_semi_octets (guint8 *buf, gsize buflen, const char *string)
{
    guint i;
    guint8 bcd;
    gsize addrlen, slen;

    addrlen = slen = strlen (string);
    if (addrlen % 2)
        addrlen++;
    g_return_val_if_fail (buflen >= addrlen, 0);

    for (i = 0; i < addrlen; i += 2) {
        if (!char_to_bcd (string[i], &bcd))
            return 0;
        buf[i / 2] = bcd & 0xF;

        if (i >= slen - 1) {
            /* PDU address gets padded with 0xF if string is odd length */
            bcd = 0xF;
        } else if (!char_to_bcd (string[i + 1], &bcd))
            return 0;
        buf[i / 2] |= bcd << 4;
    }
    return addrlen / 2;
}

/* len is in semi-octets */
static gchar *
sms_decode_address (const guint8  *address,
                    gint           len,
                    GError       **error)
{
    guint8 addrtype, addrplan;
    gchar *utf8;

    addrtype = address[0] & SMS_NUMBER_TYPE_MASK;
    addrplan = address[0] & SMS_NUMBER_PLAN_MASK;
    address++;

    if (addrtype == SMS_NUMBER_TYPE_ALPHA) {
        g_autoptr(GByteArray)  unpacked_array = NULL;
        guint8                *unpacked = NULL;
        guint32                unpacked_len;

        unpacked = mm_charset_gsm_unpack (address, (len * 4) / 7, 0, &unpacked_len);
        unpacked_array = g_byte_array_new_take (unpacked, unpacked_len);
        utf8 = mm_modem_charset_bytearray_to_utf8 (unpacked_array, MM_MODEM_CHARSET_GSM, FALSE, error);
    } else if (addrtype == SMS_NUMBER_TYPE_INTL &&
               addrplan == SMS_NUMBER_PLAN_TELEPHONE) {
        /* International telphone number, format as "+1234567890" */
        utf8 = g_malloc (len + 3); /* '+' + digits + possible trailing 0xf + NUL */
        utf8[0] = '+';
        sms_semi_octets_to_bcd_string (utf8 + 1, address, (len + 1) / 2);
    } else {
        /*
         * All non-alphanumeric types and plans are just digits, but
         * don't apply any special formatting if we don't know the
         * format.
         */
        utf8 = g_malloc (len + 2); /* digits + possible trailing 0xf + NUL */
        sms_semi_octets_to_bcd_string (utf8, address, (len + 1) / 2);
    }

    return utf8;
}

static gchar *
sms_decode_timestamp (const guint8 *timestamp)
{
    /* ISO8601 format: YYYY-MM-DDTHH:MM:SS+HHMM */
    guint year, month, day, hour, minute, second;
    gint quarters, offset_minutes;

    year = 2000 + ((timestamp[0] & 0xf) * 10) + ((timestamp[0] >> 4) & 0xf);
    month = ((timestamp[1] & 0xf) * 10) + ((timestamp[1] >> 4) & 0xf);
    day = ((timestamp[2] & 0xf) * 10) + ((timestamp[2] >> 4) & 0xf);
    hour = ((timestamp[3] & 0xf) * 10) + ((timestamp[3] >> 4) & 0xf);
    minute = ((timestamp[4] & 0xf) * 10) + ((timestamp[4] >> 4) & 0xf);
    second = ((timestamp[5] & 0xf) * 10) + ((timestamp[5] >> 4) & 0xf);
    quarters = ((timestamp[6] & 0x7) * 10) + ((timestamp[6] >> 4) & 0xf);
    offset_minutes = quarters * 15;
    if (timestamp[6] & 0x08)
        offset_minutes = -1 * offset_minutes;

    return mm_new_iso8601_time (year, month, day, hour,
                                minute, second, TRUE, offset_minutes);
}

static MMSmsEncoding
sms_encoding_type (int dcs)
{
    MMSmsEncoding scheme = MM_SMS_ENCODING_UNKNOWN;

    switch ((dcs >> 4) & 0xf) {
        /* General data coding group */
        case 0: case 1:
        case 2: case 3:
            switch (dcs & 0x0c) {
                case 0x08:
                    scheme = MM_SMS_ENCODING_UCS2;
                    break;
                case 0x00:
                    /* reserved - spec says to treat it as default alphabet */
                    /* Fall through */
                case 0x0c:
                    scheme = MM_SMS_ENCODING_GSM7;
                    break;
                case 0x04:
                    scheme = MM_SMS_ENCODING_8BIT;
                    break;
                default:
                    g_assert_not_reached ();
            }
            break;

            /* Message waiting group (default alphabet) */
        case 0xc:
        case 0xd:
            scheme = MM_SMS_ENCODING_GSM7;
            break;

            /* Message waiting group (UCS2 alphabet) */
        case 0xe:
            scheme = MM_SMS_ENCODING_UCS2;
            break;

            /* Data coding/message class group */
        case 0xf:
            switch (dcs & 0x04) {
                case 0x00:
                    scheme = MM_SMS_ENCODING_GSM7;
                    break;
                case 0x04:
                    scheme = MM_SMS_ENCODING_8BIT;
                    break;
                default:
                    g_assert_not_reached ();
            }
            break;

            /* Reserved coding group values - spec says to treat it as default alphabet */
        default:
            scheme = MM_SMS_ENCODING_GSM7;
            break;
    }

    return scheme;
}

static gchar *
sms_decode_text (const guint8   *text,
                 int             len,
                 MMSmsEncoding   encoding,
                 int             bit_offset,
                 gpointer        log_object,
                 GError        **error)
{
    if (encoding == MM_SMS_ENCODING_GSM7) {
        g_autoptr(GByteArray)  unpacked_array = NULL;
        guint8                *unpacked = NULL;
        guint32                unpacked_len;
        gchar                 *utf8;

        unpacked = mm_charset_gsm_unpack ((const guint8 *) text, len, bit_offset, &unpacked_len);
        unpacked_array = g_byte_array_new_take (unpacked, unpacked_len);
        utf8 = mm_modem_charset_bytearray_to_utf8 (unpacked_array, MM_MODEM_CHARSET_GSM, FALSE, error);
        if (utf8)
            mm_obj_dbg (log_object, "converted SMS part text from GSM-7 to UTF-8: %s", utf8);
        return utf8;
    }

    /* Always assume UTF-16 instead of UCS-2! */
    if (encoding == MM_SMS_ENCODING_UCS2) {
        g_autoptr(GByteArray)  bytearray = NULL;
        gchar                 *utf8;

        bytearray = g_byte_array_append (g_byte_array_sized_new (len), (const guint8 *)text, len);
        utf8 = mm_modem_charset_bytearray_to_utf8 (bytearray, MM_MODEM_CHARSET_UTF16, FALSE, error);
        if (utf8)
            mm_obj_dbg (log_object, "converted SMS part text from UTF-16BE to UTF-8: %s", utf8);
        return utf8;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "Couldn't convert SMS part contents from %s to UTF-8",
                 mm_sms_encoding_get_string (encoding));
    return NULL;
}

static guint
relative_to_validity (guint8 relative)
{
    if (relative <= 143)
        return (relative + 1) * 5;

    if (relative <= 167)
        return 720 + (relative - 143) * 30;

    return (relative - 166) * 1440;
}

static guint8
validity_to_relative (guint validity)
{
    if (validity == 0)
        return 167; /* 24 hours */

    if (validity <= 720) {
        /* 5 minute units up to 12 hours */
        if (validity % 5)
            validity += 5;
        return (validity / 5) - 1;
    }

    if (validity > 720 && validity <= 1440) {
        /* 12 hours + 30 minute units up to 1 day */
        if (validity % 30)
            validity += 30;  /* round up to next 30 minutes */
        validity = MIN (validity, 1440);
        return 143 + ((validity - 720) / 30);
    }

    if (validity > 1440 && validity <= 43200) {
        /* 2 days up to 1 month */
        if (validity % 1440)
            validity += 1440;  /* round up to next day */
        validity = MIN (validity, 43200);
        return 167 + ((validity - 1440) / 1440);
    }

    /* 43200 = 30 days in minutes
     * 10080 = 7 days in minutes
     * 635040 = 63 weeks in minutes
     * 40320 = 4 weeks in minutes
     */
    if (validity > 43200 && validity <= 635040) {
        /* 5 weeks up to 63 weeks */
        if (validity % 10080)
            validity += 10080;  /* round up to next week */
        validity = MIN (validity, 635040);
        return 196 + ((validity - 40320) / 10080);
    }

    return 255; /* 63 weeks */
}

MMSmsPart *
mm_sms_part_3gpp_new_from_pdu (guint         index,
                               const gchar  *hexpdu,
                               gpointer      log_object,
                               GError      **error)
{
    g_autofree guint8 *pdu = NULL;
    gsize              pdu_len;

    /* Convert PDU from hex to binary */
    pdu = mm_utils_hexstr2bin (hexpdu, -1, &pdu_len, error);
    if (!pdu) {
        g_prefix_error (error, "Couldn't convert 3GPP PDU from hex to binary: ");
        return NULL;
    }

    return mm_sms_part_3gpp_new_from_binary_pdu (index, pdu, pdu_len, log_object, FALSE, error);
}

MMSmsPart *
mm_sms_part_3gpp_new_from_binary_pdu (guint         index,
                                      const guint8  *pdu,
                                      gsize          pdu_len,
                                      gpointer       log_object,
                                      gboolean       transfer_route,
                                      GError       **error)
{
    MMSmsPart *sms_part;
    guint8 pdu_type;
    guint offset;
    guint smsc_addr_size_bytes;
    guint tp_addr_size_digits;
    guint tp_addr_size_bytes;
    guint8 validity_format = 0;
    gboolean has_udh = FALSE;
    /* The following offsets are OPTIONAL, as STATUS REPORTs may not have
     * them; we use '0' to indicate their absence */
    guint tp_pid_offset = 0;
    guint tp_dcs_offset = 0;
    guint tp_user_data_len_offset = 0;
    MMSmsEncoding user_data_encoding = MM_SMS_ENCODING_UNKNOWN;
    gchar *address;

    /* Create the new MMSmsPart */
    sms_part = mm_sms_part_new (index, MM_SMS_PDU_TYPE_UNKNOWN);

    if (index != SMS_PART_INVALID_INDEX)
        mm_obj_dbg (log_object, "parsing PDU (%u)...", index);
    else
        mm_obj_dbg (log_object, "parsing PDU...");

#define PDU_SIZE_CHECK(required_size, check_descr_str)                 \
    if (pdu_len < required_size) {                                     \
        g_set_error (error,                                            \
                     MM_CORE_ERROR,                                    \
                     MM_CORE_ERROR_FAILED,                             \
                     "PDU too short, %s: %" G_GSIZE_FORMAT " < %u",    \
                     check_descr_str,                                  \
                     pdu_len,                                          \
                     required_size);                                   \
        mm_sms_part_free (sms_part);                                   \
        return NULL;                                                   \
    }

    offset = 0;

    if (!transfer_route) {
        /* ---------------------------------------------------------------------- */
        /* SMSC, in address format, precedes the TPDU
         * First byte represents the number of BYTES for the address value */
        PDU_SIZE_CHECK (1, "cannot read SMSC address length");
        smsc_addr_size_bytes = pdu[offset++];
        if (smsc_addr_size_bytes > 0) {
             PDU_SIZE_CHECK (offset + smsc_addr_size_bytes, "cannot read SMSC address");
             /* SMSC may not be given in DELIVER PDUs */
             address = sms_decode_address (&pdu[1], 2 * (smsc_addr_size_bytes - 1), error);
             if (!address) {
                     g_prefix_error (error, "Couldn't read SMSC address: ");
                     mm_sms_part_free (sms_part);
                     return NULL;
             }
             mm_sms_part_take_smsc (sms_part, g_steal_pointer (&address));
             mm_obj_dbg (log_object, "  SMSC address parsed: '%s'", mm_sms_part_get_smsc (sms_part));
             offset += smsc_addr_size_bytes;
        } else
              mm_obj_dbg (log_object, "  no SMSC address given");
    } else
        mm_obj_dbg (log_object, "  This is a transfer-route message");


    /* ---------------------------------------------------------------------- */
    /* TP-MTI (1 byte) */
    PDU_SIZE_CHECK (offset + 1, "cannot read TP-MTI");

    pdu_type = (pdu[offset] & SMS_TP_MTI_MASK);
    switch (pdu_type) {
    case SMS_TP_MTI_SMS_DELIVER:
        mm_obj_dbg (log_object, "  deliver type PDU detected");
        mm_sms_part_set_pdu_type (sms_part, MM_SMS_PDU_TYPE_DELIVER);
        break;
    case SMS_TP_MTI_SMS_SUBMIT:
        mm_obj_dbg (log_object, "  submit type PDU detected");
        mm_sms_part_set_pdu_type (sms_part, MM_SMS_PDU_TYPE_SUBMIT);
        break;
    case SMS_TP_MTI_SMS_STATUS_REPORT:
        mm_obj_dbg (log_object, "  status report type PDU detected");
        mm_sms_part_set_pdu_type (sms_part, MM_SMS_PDU_TYPE_STATUS_REPORT);
        break;
    default:
        mm_sms_part_free (sms_part);
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unhandled message type: 0x%02x",
                     pdu_type);
        return NULL;
    }

    /* Delivery report was requested? */
    if (pdu[offset] & 0x20)
        mm_sms_part_set_delivery_report_request (sms_part, TRUE);

    /* PDU with validity? (only in SUBMIT PDUs) */
    if (pdu_type == SMS_TP_MTI_SMS_SUBMIT)
        validity_format = pdu[offset] & 0x18;

    /* PDU with user data header? */
    if (pdu[offset] & 0x40)
        has_udh = TRUE;

    offset++;

    /* ---------------------------------------------------------------------- */
    /* TP-MR (1 byte, in STATUS_REPORT and SUBMIT PDUs */
    if (pdu_type == SMS_TP_MTI_SMS_STATUS_REPORT ||
        pdu_type == SMS_TP_MTI_SMS_SUBMIT) {
        PDU_SIZE_CHECK (offset + 1, "cannot read message reference");

        mm_obj_dbg (log_object, "  message reference: %u", (guint)pdu[offset]);
        mm_sms_part_set_message_reference (sms_part, pdu[offset]);
        offset++;
    }


    /* ---------------------------------------------------------------------- */
    /* TP-DA or TP-OA or TP-RA
     * First byte represents the number of DIGITS in the number.
     * Round the sender address length up to an even number of
     * semi-octets, and thus an integral number of octets.
     */
    PDU_SIZE_CHECK (offset + 1, "cannot read number of digits in number");
    tp_addr_size_digits = pdu[offset++];
    tp_addr_size_bytes = (tp_addr_size_digits + 1) >> 1;

    PDU_SIZE_CHECK (offset + tp_addr_size_bytes, "cannot read number");
    address = sms_decode_address (&pdu[offset], tp_addr_size_digits, error);
    if (!address) {
        g_prefix_error (error, "Couldn't read address: ");
        mm_sms_part_free (sms_part);
        return NULL;
    }
    mm_sms_part_take_number (sms_part, g_steal_pointer (&address));
    mm_obj_dbg (log_object, "  number parsed: %s", mm_sms_part_get_number (sms_part));
    offset += (1 + tp_addr_size_bytes); /* +1 due to the Type of Address byte */

    /* ---------------------------------------------------------------------- */
    /* Get timestamps and indexes for TP-PID, TP-DCS and TP-UDL/TP-UD */

    if (pdu_type == SMS_TP_MTI_SMS_DELIVER) {
        PDU_SIZE_CHECK (offset + 9,
                        "cannot read PID/DCS/Timestamp"); /* 1+1+7=9 */

        /* ------ TP-PID (1 byte) ------ */
        tp_pid_offset = offset++;

        /* ------ TP-DCS (1 byte) ------ */
        tp_dcs_offset = offset++;

        /* ------ Timestamp (7 bytes) ------ */
        mm_sms_part_take_timestamp (sms_part,
                                    sms_decode_timestamp (&pdu[offset]));
        offset += 7;

        tp_user_data_len_offset = offset;
    } else if (pdu_type == SMS_TP_MTI_SMS_SUBMIT) {
        PDU_SIZE_CHECK (offset + 2 + !!validity_format,
                        "cannot read PID/DCS/Validity"); /* 1+1=2 */

        /* ------ TP-PID (1 byte) ------ */
        tp_pid_offset = offset++;

        /* ------ TP-DCS (1 byte) ------ */
        tp_dcs_offset = offset++;

        /* ----------- TP-Validity-Period (1 byte) ----------- */
        if (validity_format) {
            switch (validity_format) {
            case 0x10:
                mm_obj_dbg (log_object, "  validity available, format relative");
                mm_sms_part_set_validity_relative (sms_part,
                                                   relative_to_validity (pdu[offset]));
                offset++;
                break;
            case 0x08:
                /* TODO: support enhanced format; GSM 03.40 */
                mm_obj_dbg (log_object, "  validity available, format enhanced (not implemented)");
                /* 7 bytes for enhanced validity */
                offset += 7;
                break;
            case 0x18:
                /* TODO: support absolute format; GSM 03.40 */
                mm_obj_dbg (log_object, "  validity available, format absolute (not implemented)");
                /* 7 bytes for absolute validity */
                offset += 7;
                break;
            default:
                /* Cannot happen as we AND with the 0x18 mask */
                g_assert_not_reached ();
            }
        }

        tp_user_data_len_offset = offset;
    }
    else if (pdu_type == SMS_TP_MTI_SMS_STATUS_REPORT) {
        /* We have 2 timestamps in status report PDUs:
         *  first, the timestamp for when the PDU was received in the SMSC
         *  second, the timestamp for when the PDU was forwarded by the SMSC
         */
        PDU_SIZE_CHECK (offset + 15, "cannot read Timestamps/TP-STATUS"); /* 7+7+1=15 */

        /* ------ Timestamp (7 bytes) ------ */
        mm_sms_part_take_timestamp (sms_part,
                                    sms_decode_timestamp (&pdu[offset]));
        offset += 7;

        /* ------ Discharge Timestamp (7 bytes) ------ */
        mm_sms_part_take_discharge_timestamp (sms_part,
                                              sms_decode_timestamp (&pdu[offset]));
        offset += 7;

        /* ----- TP-STATUS (1 byte) ------ */
        mm_obj_dbg (log_object, "  delivery state: %u", (guint)pdu[offset]);
        mm_sms_part_set_delivery_state (sms_part, pdu[offset]);
        offset++;

        /* ------ TP-PI (1 byte) OPTIONAL ------ */
        if (offset < pdu_len) {
            guint next_optional_field_offset = offset + 1;

            /* TP-PID? */
            if (pdu[offset] & 0x01)
                tp_pid_offset = next_optional_field_offset++;

            /* TP-DCS? */
            if (pdu[offset] & 0x02)
                tp_dcs_offset = next_optional_field_offset++;

            /* TP-UserData? */
            if (pdu[offset] & 0x04)
                tp_user_data_len_offset = next_optional_field_offset;
        }
    } else
        g_assert_not_reached ();

    if (tp_pid_offset > 0) {
        PDU_SIZE_CHECK (tp_pid_offset + 1, "cannot read TP-PID");
        mm_obj_dbg (log_object, "  PID: %u", (guint)pdu[tp_pid_offset]);
    }

    /* Grab user data encoding and message class */
    if (tp_dcs_offset > 0) {
        PDU_SIZE_CHECK (tp_dcs_offset + 1, "cannot read TP-DCS");

        /* Encoding given in the 'alphabet' bits */
        user_data_encoding = sms_encoding_type (pdu[tp_dcs_offset]);
        switch (user_data_encoding) {
        case MM_SMS_ENCODING_GSM7:
            mm_obj_dbg (log_object, "  user data encoding is GSM7");
            break;
        case MM_SMS_ENCODING_UCS2:
            mm_obj_dbg (log_object, "  user data encoding is UCS2");
            break;
        case MM_SMS_ENCODING_8BIT:
            mm_obj_dbg (log_object, "  user data encoding is 8bit");
            break;
        case MM_SMS_ENCODING_UNKNOWN:
            mm_obj_dbg (log_object, "  user data encoding is unknown");
            break;
        default:
            g_assert_not_reached ();

        }
        mm_sms_part_set_encoding (sms_part, user_data_encoding);

        /* Class */
        if (pdu[tp_dcs_offset] & SMS_DCS_CLASS_VALID)
            mm_sms_part_set_class (sms_part,
                                   pdu[tp_dcs_offset] & SMS_DCS_CLASS_MASK);
    }

    if (tp_user_data_len_offset > 0) {
        guint tp_user_data_size_elements;
        guint tp_user_data_size_bytes;
        guint tp_user_data_offset;
        guint bit_offset;

        PDU_SIZE_CHECK (tp_user_data_len_offset + 1, "cannot read TP-UDL");
        tp_user_data_size_elements = pdu[tp_user_data_len_offset];
        mm_obj_dbg (log_object, "  user data length: %u elements", tp_user_data_size_elements);

        if (user_data_encoding == MM_SMS_ENCODING_GSM7)
            tp_user_data_size_bytes = (7 * (tp_user_data_size_elements + 1 )) / 8;
        else
            tp_user_data_size_bytes = tp_user_data_size_elements;
        mm_obj_dbg (log_object, "  user data length: %u bytes", tp_user_data_size_bytes);

        tp_user_data_offset = tp_user_data_len_offset + 1;
        PDU_SIZE_CHECK (tp_user_data_offset + tp_user_data_size_bytes, "cannot read TP-UD");

        bit_offset = 0;
        if (has_udh) {
            guint udhl, end;

            udhl = pdu[tp_user_data_offset] + 1;
            end = tp_user_data_offset + udhl;

            PDU_SIZE_CHECK (tp_user_data_offset + udhl, "cannot read UDH");

            for (offset = tp_user_data_offset + 1; (offset + 1) < end;) {
                guint8 ie_id, ie_len;

                ie_id = pdu[offset++];
                ie_len = pdu[offset++];

                switch (ie_id) {
                case 0x00:
                    if (offset + 2 >= end)
                        break;
                    /*
                     * Ignore the IE if one of the following is true:
                     *  - it claims to be part 0 of M
                     *  - it claims to be part N of M, N > M
                     */
                    if (pdu[offset + 2] == 0 ||
                        pdu[offset + 2] > pdu[offset + 1])
                        break;

                    mm_sms_part_set_concat_reference (sms_part, pdu[offset]);
                    mm_sms_part_set_concat_max (sms_part, pdu[offset + 1]);
                    mm_sms_part_set_concat_sequence (sms_part, pdu[offset + 2]);
                    break;
                case 0x08:
                    if (offset + 3 >= end)
                        break;
                    /* Concatenated short message, 16-bit reference */
                    if (pdu[offset + 3] == 0 ||
                        pdu[offset + 3] > pdu[offset + 2])
                        break;

                    mm_sms_part_set_concat_reference (sms_part, (pdu[offset] << 8) | pdu[offset + 1]);
                    mm_sms_part_set_concat_max (sms_part,pdu[offset + 2]);
                    mm_sms_part_set_concat_sequence (sms_part, pdu[offset + 3]);
                    break;
                default:
                    break;
                }

                offset += ie_len;
            }

            /*
             * Move past the user data headers to prevent it from being
             * decoded into garbage text.
             */
            tp_user_data_offset += udhl;
            tp_user_data_size_bytes -= udhl;
            if (user_data_encoding == MM_SMS_ENCODING_GSM7) {
                /*
                 * Find the number of bits we need to add to the length of the
                 * user data to get a multiple of 7 (the padding).
                 */
                bit_offset = (7 - udhl % 7) % 7;
                tp_user_data_size_elements -= (udhl * 8 + bit_offset) / 7;
            } else
                tp_user_data_size_elements -= udhl;
        }

        switch (user_data_encoding) {
        case MM_SMS_ENCODING_GSM7:
        case MM_SMS_ENCODING_UCS2:
            {
                gchar *text;

                /* Otherwise if it's 7-bit or UCS2 we can decode it */
                mm_obj_dbg (log_object, "decoding SMS text with %u elements", tp_user_data_size_elements);
                text = sms_decode_text (&pdu[tp_user_data_offset],
                                        tp_user_data_size_elements,
                                        user_data_encoding,
                                        bit_offset,
                                        log_object,
                                        error);
                if (!text) {
                    mm_sms_part_free (sms_part);
                    return NULL;
                }
                mm_sms_part_take_text (sms_part, text);
                break;
            }
        case MM_SMS_ENCODING_8BIT:
        case MM_SMS_ENCODING_UNKNOWN:
        default:
            {
                GByteArray *raw;

                mm_obj_dbg (log_object, "skipping SMS text: unknown encoding (0x%02X)", user_data_encoding);

                PDU_SIZE_CHECK (tp_user_data_offset + tp_user_data_size_bytes, "cannot read user data");

                /* 8-bit encoding is usually binary data, and we have no idea what
                 * actual encoding the data is in so we can't convert it.
                 */
                raw = g_byte_array_sized_new (tp_user_data_size_bytes);
                g_byte_array_append (raw, &pdu[tp_user_data_offset], tp_user_data_size_bytes);
                mm_sms_part_take_data (sms_part, raw);
                break;
            }
        }
    }

    return sms_part;
}

/**
 * mm_sms_part_3gpp_encode_address:
 *
 * @address: the phone number to encode
 * @buf: the buffer to encode @address in
 * @buflen: the size  of @buf
 * @is_smsc: if %TRUE encode size as number of octets of address information,
 *   otherwise if %FALSE encode size as number of digits of @address
 *
 * Returns: the size in bytes of the data added to @buf
 **/
guint
mm_sms_part_3gpp_encode_address (const gchar *address,
                                 guint8 *buf,
                                 gsize buflen,
                                 gboolean is_smsc)
{
    gsize len;

    g_return_val_if_fail (address != NULL, 0);
    g_return_val_if_fail (buf != NULL, 0);
    g_return_val_if_fail (buflen >= 2, 0);

    /* Handle number type & plan */
    buf[1] = 0x80;  /* Bit 7 always 1 */
    if (address[0] == '+') {
        buf[1] |= SMS_NUMBER_TYPE_INTL;
        address++;
    }
    buf[1] |= SMS_NUMBER_PLAN_TELEPHONE;

    len = sms_string_to_bcd_semi_octets (&buf[2], buflen, address);

    if (is_smsc)
        buf[0] = len + 1;  /* addr length + size byte */
    else
        buf[0] = strlen (address);  /* number of digits in address */

    return len ? len + 2 : 0;  /* addr length + size byte + number type/plan */
}

/**
 * mm_sms_part_3gpp_get_submit_pdu:
 *
 * @part: the SMS message part
 * @out_pdulen: on success, the size of the returned PDU in bytes
 * @out_msgstart: on success, the byte index in the returned PDU where the
 *  message starts (ie, skipping the SMSC length byte and address, if present)
 * @error: on error, filled with the error that occurred
 *
 * Constructs a single-part SMS message with the given details, preferring to
 * use the UCS2 character set when the message will fit, otherwise falling back
 * to the GSM character set.
 *
 * Returns: the constructed PDU data on success, or %NULL on error
 **/
guint8 *
mm_sms_part_3gpp_get_submit_pdu (MMSmsPart *part,
                                 guint *out_pdulen,
                                 guint *out_msgstart,
                                 gpointer log_object,
                                 GError **error)
{
    guint8 *pdu;
    guint len, offset = 0;
    guint shift = 0;
    guint8 *udl_ptr;
    MMSmsEncoding encoding;

    g_return_val_if_fail (mm_sms_part_get_number (part) != NULL, NULL);
    g_return_val_if_fail (mm_sms_part_get_text (part) != NULL || mm_sms_part_get_data (part) != NULL, NULL);

    if (mm_sms_part_get_pdu_type (part) != MM_SMS_PDU_TYPE_SUBMIT) {
        g_set_error (error,
                     MM_MESSAGE_ERROR,
                     MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                     "Invalid PDU type to generate a 'submit' PDU: '%s'",
                     mm_sms_pdu_type_get_string (mm_sms_part_get_pdu_type (part)));
        return NULL;
    }

    mm_obj_dbg (log_object, "creating PDU for part...");

    /* Build up the PDU */
    pdu = g_malloc0 (PDU_SIZE);

    if (mm_sms_part_get_smsc (part)) {
        mm_obj_dbg (log_object, "  adding SMSC to PDU...");
        len = mm_sms_part_3gpp_encode_address (mm_sms_part_get_smsc (part), pdu, PDU_SIZE, TRUE);
        if (len == 0) {
            g_set_error (error,
                         MM_MESSAGE_ERROR,
                         MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                         "Invalid SMSC address '%s'", mm_sms_part_get_smsc (part));
            goto error;
        }
        offset += len;
    } else {
        /* No SMSC, use default */
        pdu[offset++] = 0x00;
    }

    if (out_msgstart)
        *out_msgstart = offset;

    /* ----------- First BYTE ----------- */
    pdu[offset] = 0;

    /* TP-VP present; format RELATIVE */
    if (mm_sms_part_get_validity_relative (part) > 0) {
        mm_obj_dbg (log_object, "  adding validity to PDU...");
        pdu[offset] |= 0x10;
    }

    /* Concatenation sequence only found in multipart SMS */
    if (mm_sms_part_get_concat_sequence (part)) {
        mm_obj_dbg (log_object, "  adding UDHI to PDU...");
        pdu[offset] |= 0x40; /* UDHI */
    }

    /* Delivery report requested in singlepart messages or in the last PDU of
     * multipart messages */
    if (mm_sms_part_get_delivery_report_request (part) &&
        (!mm_sms_part_get_concat_sequence (part) ||
         mm_sms_part_get_concat_max (part) == mm_sms_part_get_concat_sequence (part))) {
        mm_obj_dbg (log_object, "  requesting delivery report...");
        pdu[offset] |= 0x20;
    }

    /* TP-MTI = SMS-SUBMIT */
    pdu[offset++] |= 0x01;


    /* ----------- TP-MR (1 byte) ----------- */

    pdu[offset++] = 0x00;     /* TP-Message-Reference: filled by device */

    /* ----------- Destination address ----------- */

    len = mm_sms_part_3gpp_encode_address (mm_sms_part_get_number (part), &pdu[offset], PDU_SIZE - offset, FALSE);
    if (len == 0) {
        g_set_error (error,
                     MM_MESSAGE_ERROR,
                     MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                     "Invalid number '%s'", mm_sms_part_get_number (part));
        goto error;
    }
    offset += len;

    /* ----------- TP-PID (1 byte) ----------- */

    pdu[offset++] = 0x00;

    /* ----------- TP-DCS (1 byte) ----------- */
    pdu[offset] = 0x00;

    if (mm_sms_part_get_class (part) >= 0 && mm_sms_part_get_class (part) <= 3) {
        mm_obj_dbg (log_object, "  using class %d...", mm_sms_part_get_class (part));
        pdu[offset] |= SMS_DCS_CLASS_VALID;
        pdu[offset] |= mm_sms_part_get_class (part);
    }

    encoding = mm_sms_part_get_encoding (part);

    switch (encoding) {
    case MM_SMS_ENCODING_UCS2:
        mm_obj_dbg (log_object, "  using UCS2 encoding...");
        pdu[offset] |= SMS_DCS_CODING_UCS2;
        break;
    case MM_SMS_ENCODING_GSM7:
        mm_obj_dbg (log_object, "  using GSM7 encoding...");
        pdu[offset] |= SMS_DCS_CODING_DEFAULT;  /* GSM */
        break;
    case MM_SMS_ENCODING_8BIT:
    case MM_SMS_ENCODING_UNKNOWN:
    default:
        mm_obj_dbg (log_object, "  using 8bit encoding...");
        pdu[offset] |= SMS_DCS_CODING_8BIT;
        break;
    }
    offset++;

    /* ----------- TP-Validity-Period (1 byte): 4 days ----------- */
    /* Only if TP-VPF was set in first byte */

    if (mm_sms_part_get_validity_relative (part) > 0)
        pdu[offset++] = validity_to_relative (mm_sms_part_get_validity_relative (part));

    /* ----------- TP-User-Data-Length ----------- */
    /* Set to zero initially, and keep a ptr for easy access later */
    udl_ptr = &pdu[offset];
    pdu[offset++] = 0;

    /* Build UDH */
    if (mm_sms_part_get_concat_sequence (part)) {
        mm_obj_dbg (log_object, "  adding UDH header in PDU... (reference: %u, max: %u, sequence: %u)",
                mm_sms_part_get_concat_reference (part),
                mm_sms_part_get_concat_max (part),
                mm_sms_part_get_concat_sequence (part));
        pdu[offset++] = 0x05; /* udh len */
        pdu[offset++] = 0x00; /* mid */
        pdu[offset++] = 0x03; /* data len */
        pdu[offset++] = (guint8)mm_sms_part_get_concat_reference (part);
        pdu[offset++] = (guint8)mm_sms_part_get_concat_max (part);
        pdu[offset++] = (guint8)mm_sms_part_get_concat_sequence (part);

        /* if a UDH is present and the data encoding is the default 7-bit
         * alphabet, the user data must be 7-bit word aligned after the
         * UDH. This means up to 6 bits of zeros need to be inserted at the
         * start of the message.
         *
         * In our case the UDH is 6 bytes long, 48bits. The next multiple of
         * 7 is therefore 49, so we only need to include one bit of padding.
         */
        shift = 1;
    }

    if (encoding == MM_SMS_ENCODING_GSM7) {
        g_autoptr(GByteArray)  unpacked = NULL;
        g_autofree guint8     *packed = NULL;
        guint32                packlen = 0;

        unpacked = mm_modem_charset_bytearray_from_utf8 (mm_sms_part_get_text (part), MM_MODEM_CHARSET_GSM, FALSE, error);
        if (!unpacked)
            goto error;

        if (unpacked->len == 0) {
            g_set_error_literal (error,
                                 MM_MESSAGE_ERROR,
                                 MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                                 "Failed to convert message text to GSM");
            goto error;
        }

        /* Set real data length, in septets
         * If we had UDH, add 7 septets
         */
        *udl_ptr = mm_sms_part_get_concat_sequence (part) ? (7 + unpacked->len) : unpacked->len;
        mm_obj_dbg (log_object, "  user data length is %u septets (%s UDH)",
                    *udl_ptr,
                    mm_sms_part_get_concat_sequence (part) ? "with" : "without");

        packed = mm_charset_gsm_pack (unpacked->data, unpacked->len, shift, &packlen);
        if (!packed || packlen == 0) {
            g_set_error_literal (error,
                                 MM_MESSAGE_ERROR,
                                 MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                                 "Failed to pack message text to GSM");
            goto error;
        }

        memcpy (&pdu[offset], packed, packlen);
        offset += packlen;
    } else if (encoding == MM_SMS_ENCODING_UCS2) {
        g_autoptr(GByteArray) array = NULL;
        g_autoptr(GError)     inner_error = NULL;

        /* Always assume UTF-16 instead of UCS-2! */
        array = mm_modem_charset_bytearray_from_utf8 (mm_sms_part_get_text (part), MM_MODEM_CHARSET_UTF16, FALSE, &inner_error);
        if (!array) {
            g_set_error (error,
                         MM_MESSAGE_ERROR,
                         MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                         "Failed to convert message text to UTF-16: %s",
                         inner_error->message);
            goto error;
        }

        /* Set real data length, in octets
         * If we had UDH, add 6 octets
         */
        *udl_ptr = mm_sms_part_get_concat_sequence (part) ? (6 + array->len) : array->len;
        mm_obj_dbg (log_object, "  user data length is %u octets (%s UDH)",
                    *udl_ptr,
                    mm_sms_part_get_concat_sequence (part) ? "with" : "without");

        memcpy (&pdu[offset], array->data, array->len);
        offset += array->len;
    } else if (mm_sms_part_get_encoding (part) == MM_SMS_ENCODING_8BIT) {
        const GByteArray *data;

        data = mm_sms_part_get_data (part);

        /* Set real data length, in octets
         * If we had UDH, add 6 octets
         */
        *udl_ptr = mm_sms_part_get_concat_sequence (part) ? (6 + data->len) : data->len;
        mm_obj_dbg (log_object, "  binary user data length is %u octets (%s UDH)",
                *udl_ptr,
                mm_sms_part_get_concat_sequence (part) ? "with" : "without");

        memcpy (&pdu[offset], data->data, data->len);
        offset += data->len;
    } else
        g_assert_not_reached ();

    if (out_pdulen)
        *out_pdulen = offset;
    return pdu;

error:
    g_free (pdu);
    return NULL;
}

static gchar **
util_split_text_gsm7 (const gchar *text,
                      gsize        text_len,
                      gpointer     log_object)
{
    gchar **out;
    guint   n_chunks;
    guint   i;
    guint   j;

    /* No splitting needed? */
    if (text_len <= 160) {
        out = g_new0 (gchar *, 2);
        out[0] = g_strdup (text);
        return out;
    }

    /* Compute number of chunks needed */
    n_chunks = text_len / 153;
    if (text_len % 153 != 0)
        n_chunks++;

    /* Fill in all chunks */
    out = g_new0 (gchar *, n_chunks + 1);
    for (i = 0, j = 0; i < n_chunks; i++, j += 153)
        out[i] = g_strndup (&text[j], 153);

    return out;
}

static gchar **
util_split_text_utf16_or_ucs2 (const gchar *text,
                               gsize        text_len,
                               gpointer     log_object)
{
    g_autoptr(GPtrArray)  chunks = NULL;
    const gchar          *walker;
    const gchar          *chunk_start;
    glong                 encoded_chunk_length;
    glong                 total_encoded_chunk_length;

    chunks = g_ptr_array_new_with_free_func ((GDestroyNotify)g_free);

    walker = text;
    chunk_start = text;
    encoded_chunk_length = 0;
    total_encoded_chunk_length = 0;
    while (walker && *walker) {
        g_autofree gunichar2 *unichar2 = NULL;
        glong                 unichar2_written = 0;
        glong                 unichar2_written_bytes = 0;
        gunichar              single;

        single = g_utf8_get_char (walker);
        unichar2 = g_ucs4_to_utf16 (&single, 1, NULL, &unichar2_written, NULL);
        g_assert (unichar2_written > 0);

        /* When splitting for UCS-2 encoding, only one single unichar2 will be
         * written, because all codepoints represented in UCS2 fit in the BMP.
         * When splitting for UTF-16, though, we may end up writing one or two
         * unichar2 (without or with surrogate pairs), because UTF-16 covers the
         * whole Unicode spectrum. */
        unichar2_written_bytes = (unichar2_written * sizeof (gunichar2));
        if ((encoded_chunk_length + unichar2_written_bytes) > 134) {
            g_ptr_array_add (chunks, g_strndup (chunk_start, walker - chunk_start));
            chunk_start = walker;
            encoded_chunk_length = unichar2_written_bytes;
        } else
            encoded_chunk_length += unichar2_written_bytes;

        total_encoded_chunk_length += unichar2_written_bytes;
        walker = g_utf8_next_char (walker);
    }

    /* We have split the original string in chunks, where each chunk
     * does not require more than 134 bytes when encoded in UTF-16.
     * As a special case now, we consider the case that no splitting
     * is necessary, i.e. if the total amount of bytes after encoding
     * in UTF-16 is less or equal than 140. */
    if (total_encoded_chunk_length <= 140) {
        gchar **out;

        out = g_new0 (gchar *, 2);
        out[0] = g_strdup (text);
        return out;
    }

    /* Otherwise, we do need the splitted chunks. Add the last one
     * with contents plus the last trailing NULL */
    g_ptr_array_add (chunks, g_strndup (chunk_start, walker - chunk_start));
    g_ptr_array_add (chunks, NULL);

    return (gchar **) g_ptr_array_free (g_steal_pointer (&chunks), FALSE);
}

gchar **
mm_sms_part_3gpp_util_split_text (const gchar   *text,
                                  MMSmsEncoding *encoding,
                                  gpointer       log_object)
{
    if (!text)
        return NULL;

    /* Some info about the rules for splitting.
     *
     * The User Data can be up to 140 bytes in the SMS part:
     *  0) If we only need one chunk, it can be of up to 140 bytes.
     *     If we need more than one chunk, these have to be of 140 - 6 = 134
     *     bytes each, as we need place for the UDH header.
     *  1) If we're using GSM7 encoding, this gives us up to 160 characters,
     *     as we can pack 160 characters of 7bits each into 140 bytes.
     *      160 * 7 = 140 * 8 = 1120.
     *     If we only have 134 bytes allowed, that would mean that we can pack
     *     up to 153 input characters:
     *      134 * 8 = 1072; 1072/7=153.14
     *  2) If we're using UCS2 encoding, we can pack up to 70 characters in
     *     140 bytes (each with 2 bytes), or up to 67 characters in 134 bytes.
     *  3) If we're using UTF-16 encoding (instead of UCS2), the amount of
     *     characters we can pack is variable, depends on how the characters
     *     are encoded in UTF-16 (e.g. if there are characters out of the BMP
     *     we'll need surrogate pairs and a single character will need 4 bytes
     *     instead of 2).
     *
     * This method does the split of the input string into N strings, so that
     * each of the strings can be placed in a SMS part.
     */

    /* Check if we can do GSM encoding */
    if (mm_charset_can_convert_to (text, MM_MODEM_CHARSET_GSM)) {
        *encoding = MM_SMS_ENCODING_GSM7;
        return util_split_text_gsm7 (text, strlen (text), log_object);
    }

    /* Otherwise fallback to report UCS-2 and split supporting UTF-16 */
    *encoding = MM_SMS_ENCODING_UCS2;
    return util_split_text_utf16_or_ucs2 (text, strlen (text), log_object);
}

GByteArray **
mm_sms_part_3gpp_util_split_data (const guint8 *data,
                                  gsize data_len)
{
    GByteArray **out;

    /* Some info about the rules for splitting.
     *
     * The User Data can be up to 140 bytes in the SMS part:
     *  0) If we only need one chunk, it can be of up to 140 bytes.
     *     If we need more than one chunk, these have to be of 140 - 6 = 134
     *     bytes each, as we need place for the UDH header.
     */

    if (data_len <= 140) {
        out = g_new0 (GByteArray *, 2);
        out[0] = g_byte_array_append (g_byte_array_sized_new (data_len),
                                      data,
                                      data_len);
    } else {
        guint n_chunks;
        guint i;
        guint j;

        n_chunks = data_len / 134;
        if (data_len % 134 != 0)
            n_chunks ++;

        out = g_new0 (GByteArray *, n_chunks + 1);
        for (i = 0, j = 0; i < n_chunks; i++, j+= 134) {
            out[i] = g_byte_array_append (g_byte_array_sized_new (134),
                                          &data[j],
                                          MIN (data_len - j, 134));
        }
    }

    return out;
}
