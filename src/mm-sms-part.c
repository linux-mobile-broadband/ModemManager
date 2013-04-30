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

#include "mm-sms-part.h"
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
static char *
sms_decode_address (const guint8 *address, int len)
{
    guint8 addrtype, addrplan;
    char *utf8;

    addrtype = address[0] & SMS_NUMBER_TYPE_MASK;
    addrplan = address[0] & SMS_NUMBER_PLAN_MASK;
    address++;

    if (addrtype == SMS_NUMBER_TYPE_ALPHA) {
        guint8 *unpacked;
        guint32 unpacked_len;
        unpacked = gsm_unpack (address, (len * 4) / 7, 0, &unpacked_len);
        utf8 = (char *)mm_charset_gsm_unpacked_to_utf8 (unpacked,
                                                        unpacked_len);
        g_free(unpacked);
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

static char *
sms_decode_timestamp (const guint8 *timestamp)
{
    /* YYMMDDHHMMSS+ZZ */
    char *timestr;
    int quarters, hours;

    timestr = g_malloc0 (16);
    sms_semi_octets_to_bcd_string (timestr, timestamp, 6);
    quarters = ((timestamp[6] & 0x7) * 10) + ((timestamp[6] >> 4) & 0xf);
    hours = quarters / 4;
    if (timestamp[6] & 0x08)
        timestr[12] = '-';
    else
        timestr[12] = '+';
    timestr[13] = (hours / 10) + '0';
    timestr[14] = (hours % 10) + '0';
    /* TODO(njw): Change timestamp rep to something that includes quarter-hours */
    return timestr;
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
                    /* fallthrough */
                    /* reserved - spec says to treat it as default alphabet */
                case 0x0c:
                    scheme = MM_SMS_ENCODING_GSM7;
                    break;
                case 0x04:
                    scheme = MM_SMS_ENCODING_8BIT;
                    break;
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
            }
            break;

            /* Reserved coding group values - spec says to treat it as default alphabet */
        default:
            scheme = MM_SMS_ENCODING_GSM7;
            break;
    }

    return scheme;
}

static char *
sms_decode_text (const guint8 *text, int len, MMSmsEncoding encoding, int bit_offset)
{
    char *utf8;
    guint8 *unpacked;
    guint32 unpacked_len;

    if (encoding == MM_SMS_ENCODING_GSM7) {
        mm_dbg ("Converting SMS part text from GSM7 to UTF8...");
        unpacked = gsm_unpack ((const guint8 *) text, len, bit_offset, &unpacked_len);
        utf8 = (char *) mm_charset_gsm_unpacked_to_utf8 (unpacked, unpacked_len);
        mm_dbg ("   Got UTF-8 text: '%s'", utf8);
        g_free (unpacked);
    } else if (encoding == MM_SMS_ENCODING_UCS2) {
        mm_dbg ("Converting SMS part text from UCS-2BE to UTF8...");
        utf8 = g_convert ((char *) text, len, "UTF8", "UCS-2BE", NULL, NULL, NULL);
        mm_dbg ("   Got UTF-8 text: '%s'", utf8);
    } else {
        g_warn_if_reached ();
        utf8 = g_strdup ("");
    }

    return utf8;
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

struct _MMSmsPart {
    guint index;
    MMSmsPduType pdu_type;
    gchar *smsc;
    gchar *timestamp;
    gchar *discharge_timestamp;
    gchar *number;
    gchar *text;
    MMSmsEncoding encoding;
    GByteArray *data;
    gint  class;
    guint validity_relative;
    gboolean delivery_report_request;
    guint message_reference;
    /* NOT a MMSmsDeliveryState, which just includes the known values */
    guint delivery_state;

    gboolean should_concat;
    guint concat_reference;
    guint concat_max;
    guint concat_sequence;
};

void
mm_sms_part_free (MMSmsPart *self)
{
    g_free (self->discharge_timestamp);
    g_free (self->timestamp);
    g_free (self->smsc);
    g_free (self->number);
    g_free (self->text);
    if (self->data)
        g_byte_array_unref (self->data);
    g_slice_free (MMSmsPart, self);
}

#define PART_GET_FUNC(type, name)             \
    type                                      \
    mm_sms_part_get_##name (MMSmsPart *self)  \
    {                                         \
        return self->name;                    \
    }

#define PART_SET_FUNC(type, name)             \
    void                                      \
    mm_sms_part_set_##name (MMSmsPart *self,  \
                            type value)       \
    {                                         \
        self->name = value;                   \
    }

#define PART_SET_TAKE_STR_FUNC(name)             \
    void                                         \
    mm_sms_part_set_##name (MMSmsPart *self,     \
                            const gchar *value)  \
    {                                            \
        g_free (self->name);                     \
        self->name = g_strdup (value);           \
    }                                            \
                                                 \
    void                                         \
    mm_sms_part_take_##name (MMSmsPart *self,    \
                             gchar *value)       \
    {                                            \
        g_free (self->name);                     \
        self->name = value;                      \
    }

PART_GET_FUNC (guint, index)
PART_SET_FUNC (guint, index)
PART_GET_FUNC (MMSmsPduType, pdu_type)
PART_SET_FUNC (MMSmsPduType, pdu_type)
PART_GET_FUNC (const gchar *, smsc)
PART_SET_TAKE_STR_FUNC (smsc)
PART_GET_FUNC (const gchar *, number)
PART_SET_TAKE_STR_FUNC (number)
PART_GET_FUNC (const gchar *, timestamp)
PART_SET_TAKE_STR_FUNC (timestamp)
PART_GET_FUNC (const gchar *, discharge_timestamp)
PART_SET_TAKE_STR_FUNC (discharge_timestamp)
PART_GET_FUNC (guint, concat_max)
PART_SET_FUNC (guint, concat_max)
PART_GET_FUNC (guint, concat_sequence)
PART_SET_FUNC (guint, concat_sequence)
PART_GET_FUNC (const gchar *, text)
PART_SET_TAKE_STR_FUNC (text)
PART_GET_FUNC (MMSmsEncoding, encoding)
PART_SET_FUNC (MMSmsEncoding, encoding)
PART_GET_FUNC (gint,  class)
PART_SET_FUNC (gint,  class)
PART_GET_FUNC (guint, validity_relative)
PART_SET_FUNC (guint, validity_relative)
PART_GET_FUNC (gboolean, delivery_report_request)
PART_SET_FUNC (gboolean, delivery_report_request)
PART_GET_FUNC (guint, message_reference)
PART_SET_FUNC (guint, message_reference)
PART_GET_FUNC (guint, delivery_state)
PART_SET_FUNC (guint, delivery_state)

PART_GET_FUNC (guint, concat_reference)

void
mm_sms_part_set_concat_reference (MMSmsPart *self,
                                  guint value)
{
    self->should_concat = TRUE;
    self->concat_reference = value;
}

PART_GET_FUNC (const GByteArray *, data)

void
mm_sms_part_set_data (MMSmsPart *self,
                      GByteArray *value)
{
    if (self->data)
        g_byte_array_unref (self->data);
    self->data = (value ? g_byte_array_ref (value) : NULL);
}

void
mm_sms_part_take_data (MMSmsPart *self,
                       GByteArray *value)
{
    if (self->data)
        g_byte_array_unref (self->data);
    self->data = value;
}

gboolean
mm_sms_part_should_concat (MMSmsPart *self)
{
    return self->should_concat;
}

MMSmsPart *
mm_sms_part_new (guint index,
                 MMSmsPduType pdu_type)
{
    MMSmsPart *sms_part;

    sms_part = g_slice_new0 (MMSmsPart);
    sms_part->index = index;
    sms_part->pdu_type = pdu_type;
    sms_part->encoding = MM_SMS_ENCODING_UNKNOWN;
    sms_part->delivery_state = MM_SMS_DELIVERY_STATE_UNKNOWN;
    sms_part->class = -1;

    return sms_part;
}

MMSmsPart *
mm_sms_part_new_from_pdu (guint index,
                          const gchar *hexpdu,
                          GError **error)
{
    gsize pdu_len;
    guint8 *pdu;
    MMSmsPart *part;

    /* Convert PDU from hex to binary */
    pdu = (guint8 *) mm_utils_hexstr2bin (hexpdu, &pdu_len);
    if (!pdu) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Couldn't convert PDU from hex to binary");
        return NULL;
    }

    part = mm_sms_part_new_from_binary_pdu (index, pdu, pdu_len, error);
    g_free (pdu);

    return part;
}

MMSmsPart *
mm_sms_part_new_from_binary_pdu (guint index,
                                 const guint8 *pdu,
                                 gsize pdu_len,
                                 GError **error)
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

    /* Create the new MMSmsPart */
    sms_part = mm_sms_part_new (index, MM_SMS_PDU_TYPE_UNKNOWN);

    if (index != SMS_PART_INVALID_INDEX)
        mm_dbg ("Parsing PDU (%u)...", index);
    else
        mm_dbg ("Parsing PDU...");

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

    /* ---------------------------------------------------------------------- */
    /* SMSC, in address format, precedes the TPDU
     * First byte represents the number of BYTES for the address value */
    PDU_SIZE_CHECK (1, "cannot read SMSC address length");
    smsc_addr_size_bytes = pdu[offset++];
    if (smsc_addr_size_bytes > 0) {
        PDU_SIZE_CHECK (offset + smsc_addr_size_bytes, "cannot read SMSC address");
        /* SMSC may not be given in DELIVER PDUs */
        mm_sms_part_take_smsc (sms_part,
                               sms_decode_address (&pdu[1], 2 * (smsc_addr_size_bytes - 1)));
        mm_dbg ("  SMSC address parsed: '%s'", mm_sms_part_get_smsc (sms_part));
        offset += smsc_addr_size_bytes;
    } else
        mm_dbg ("  No SMSC address given");


    /* ---------------------------------------------------------------------- */
    /* TP-MTI (1 byte) */
    PDU_SIZE_CHECK (offset + 1, "cannot read TP-MTI");

    pdu_type = (pdu[offset] & SMS_TP_MTI_MASK);
    switch (pdu_type) {
    case SMS_TP_MTI_SMS_DELIVER:
        mm_dbg ("  Deliver type PDU detected");
        mm_sms_part_set_pdu_type (sms_part, MM_SMS_PDU_TYPE_DELIVER);
        break;
    case SMS_TP_MTI_SMS_SUBMIT:
        mm_dbg ("  Submit type PDU detected");
        mm_sms_part_set_pdu_type (sms_part, MM_SMS_PDU_TYPE_SUBMIT);
        break;
    case SMS_TP_MTI_SMS_STATUS_REPORT:
        mm_dbg ("  Status report type PDU detected");
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

        mm_dbg ("  message reference: %u", (guint)pdu[offset]);
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
    mm_sms_part_take_number (sms_part,
                             sms_decode_address (&pdu[offset],
                                                 tp_addr_size_digits));
    mm_dbg ("  Number parsed: '%s'", mm_sms_part_get_number (sms_part));
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
                mm_dbg ("  validity available, format relative");
                mm_sms_part_set_validity_relative (sms_part,
                                                   relative_to_validity (pdu[offset]));
                offset++;
                break;
            case 0x08:
                /* TODO: support enhanced format; GSM 03.40 */
                mm_dbg ("  validity available, format enhanced (not implemented)");
                /* 7 bytes for enhanced validity */
                offset += 7;
                break;
            case 0x18:
                /* TODO: support absolute format; GSM 03.40 */
                mm_dbg ("  validity available, format absolute (not implemented)");
                /* 7 bytes for absolute validity */
                offset += 7;
                break;
            default:
                /* Cannot happen as we AND with the 0x18 mask */
                g_assert_not_reached();
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
        mm_dbg ("  delivery state: %u", (guint)pdu[offset]);
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
        mm_dbg ("  PID: %u", (guint)pdu[tp_pid_offset]);
    }

    /* Grab user data encoding and message class */
    if (tp_dcs_offset > 0) {
        PDU_SIZE_CHECK (tp_dcs_offset + 1, "cannot read TP-DCS");

        /* Encoding given in the 'alphabet' bits */
        user_data_encoding = sms_encoding_type(pdu[tp_dcs_offset]);
        switch (user_data_encoding) {
        case MM_SMS_ENCODING_GSM7:
            mm_dbg ("  user data encoding is GSM7");
            break;
        case MM_SMS_ENCODING_UCS2:
            mm_dbg ("  user data encoding is UCS2");
            break;
        case MM_SMS_ENCODING_8BIT:
            mm_dbg ("  user data encoding is 8bit");
            break;
        default:
            mm_dbg ("  user data encoding is unknown");
            break;
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
        mm_dbg ("  user data length: %u elements", tp_user_data_size_elements);

        if (user_data_encoding == MM_SMS_ENCODING_GSM7)
            tp_user_data_size_bytes = (7 * (tp_user_data_size_elements + 1 )) / 8;
        else
            tp_user_data_size_bytes = tp_user_data_size_elements;
        mm_dbg ("  user data length: %u bytes", tp_user_data_size_bytes);

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
            /* Otherwise if it's 7-bit or UCS2 we can decode it */
            mm_dbg ("Decoding SMS text with '%u' elements", tp_user_data_size_elements);
            mm_sms_part_take_text (sms_part,
                                   sms_decode_text (&pdu[tp_user_data_offset],
                                                    tp_user_data_size_elements,
                                                    user_data_encoding,
                                                    bit_offset));
            g_warn_if_fail (sms_part->text != NULL);
            break;

        default:
            {
                GByteArray *raw;

                mm_dbg ("Skipping SMS text: Unknown encoding (0x%02X)", user_data_encoding);

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
 * mm_sms_part_encode_address:
 *
 * @address: the phone number to encode
 * @buf: the buffer to encode @address in
 * @buflen: the size  of @buf
 * @is_smsc: if %TRUE encode size as number of octets of address infromation,
 *   otherwise if %FALSE encode size as number of digits of @address
 *
 * Returns: the size in bytes of the data added to @buf
 **/
guint
mm_sms_part_encode_address (const gchar *address,
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
 * mm_sms_part_get_submit_pdu:
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
mm_sms_part_get_submit_pdu (MMSmsPart *part,
                            guint *out_pdulen,
                            guint *out_msgstart,
                            GError **error)
{
    guint8 *pdu;
    guint len, offset = 0;
    guint shift = 0;
    guint8 *udl_ptr;

    g_return_val_if_fail (part->number != NULL, NULL);
    g_return_val_if_fail (part->text != NULL || part->data != NULL, NULL);

    mm_dbg ("Creating PDU for part...");

    /* Build up the PDU */
    pdu = g_malloc0 (PDU_SIZE);

    if (part->smsc) {
        mm_dbg ("  adding SMSC to PDU...");
        len = mm_sms_part_encode_address (part->smsc, pdu, PDU_SIZE, TRUE);
        if (len == 0) {
            g_set_error (error,
                         MM_MESSAGE_ERROR,
                         MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                         "Invalid SMSC address '%s'", part->smsc);
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
    if (part->validity_relative > 0) {
        mm_dbg ("  adding validity to PDU...");
        pdu[offset] |= 0x10;
    }

    /* Concatenation sequence only found in multipart SMS */
    if (part->concat_sequence) {
        mm_dbg ("  adding UDHI to PDU...");
        pdu[offset] |= 0x40; /* UDHI */
    }

    /* Delivery report requested in singlepart messages or in the last PDU of
     * multipart messages */
    if (part->delivery_report_request &&
        (!part->concat_sequence ||
         part->concat_max == part->concat_sequence)) {
        mm_dbg ("  requesting delivery report...");
        pdu[offset] |= 0x20;
    }

    /* TP-MTI = SMS-SUBMIT */
    pdu[offset++] |= 0x01;


    /* ----------- TP-MR (1 byte) ----------- */

    pdu[offset++] = 0x00;     /* TP-Message-Reference: filled by device */

    /* ----------- Destination address ----------- */

    len = mm_sms_part_encode_address (part->number, &pdu[offset], PDU_SIZE - offset, FALSE);
    if (len == 0) {
        g_set_error (error,
                     MM_MESSAGE_ERROR,
                     MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                     "Invalid number '%s'", part->number);
        goto error;
    }
    offset += len;

    /* ----------- TP-PID (1 byte) ----------- */

    pdu[offset++] = 0x00;

    /* ----------- TP-DCS (1 byte) ----------- */
    pdu[offset] = 0x00;

    if (part->class >= 0 && part->class <= 3) {
        mm_dbg ("  using class %d...", part->class);
        pdu[offset] |= SMS_DCS_CLASS_VALID;
        pdu[offset] |= part->class;
    }

    if (part->encoding == MM_SMS_ENCODING_UCS2) {
        mm_dbg ("  using UCS2 encoding...");
        pdu[offset] |= SMS_DCS_CODING_UCS2;
    } else if (part->encoding == MM_SMS_ENCODING_GSM7) {
        mm_dbg ("  using GSM7 encoding...");
        pdu[offset] |= SMS_DCS_CODING_DEFAULT;  /* GSM */
    } else {
        mm_dbg ("  using 8bit encoding...");
        pdu[offset] |= SMS_DCS_CODING_8BIT;
    }
    offset++;

    /* ----------- TP-Validity-Period (1 byte): 4 days ----------- */
    /* Only if TP-VPF was set in first byte */

    if (part->validity_relative > 0)
        pdu[offset++] = validity_to_relative (part->validity_relative);

    /* ----------- TP-User-Data-Length ----------- */
    /* Set to zero initially, and keep a ptr for easy access later */
    udl_ptr = &pdu[offset];
    pdu[offset++] = 0;

    /* Build UDH */
    if (part->concat_sequence) {
        mm_dbg ("  adding UDH header in PDU... (reference: %u, max: %u, sequence: %u)",
                part->concat_reference,
                part->concat_max,
                part->concat_sequence);
        pdu[offset++] = 0x05; /* udh len */
        pdu[offset++] = 0x00; /* mid */
        pdu[offset++] = 0x03; /* data len */
        pdu[offset++] = (guint8)part->concat_reference;
        pdu[offset++] = (guint8)part->concat_max;
        pdu[offset++] = (guint8)part->concat_sequence;

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

    if (part->encoding == MM_SMS_ENCODING_GSM7) {
        guint8 *unpacked, *packed;
        guint32 unlen = 0, packlen = 0;

        unpacked = mm_charset_utf8_to_unpacked_gsm (part->text, &unlen);
        if (!unpacked || unlen == 0) {
            g_free (unpacked);
            g_set_error_literal (error,
                                 MM_MESSAGE_ERROR,
                                 MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                                 "Failed to convert message text to GSM");
            goto error;
        }

        /* Set real data length, in septets
         * If we had UDH, add 7 septets
         */
        *udl_ptr = part->concat_sequence ? (7 + unlen) : unlen;
        mm_dbg ("  user data length is '%u' septets (%s UDH)",
                *udl_ptr,
                part->concat_sequence ? "with" : "without");

        packed = gsm_pack (unpacked, unlen, shift, &packlen);
        g_free (unpacked);
        if (!packed || packlen == 0) {
            g_free (packed);
            g_set_error_literal (error,
                                 MM_MESSAGE_ERROR,
                                 MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                                 "Failed to pack message text to GSM");
            goto error;
        }

        memcpy (&pdu[offset], packed, packlen);
        g_free (packed);
        offset += packlen;
    } else if (part->encoding == MM_SMS_ENCODING_UCS2) {
        GByteArray *array;

        /* Try to guess a good value for the array */
        array = g_byte_array_sized_new (strlen (part->text) * 2);
        if (!mm_modem_charset_byte_array_append (array, part->text, FALSE, MM_MODEM_CHARSET_UCS2)) {
            g_byte_array_free (array, TRUE);
            g_set_error_literal (error,
                                 MM_MESSAGE_ERROR,
                                 MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                                 "Failed to convert message text to UCS2");
            goto error;
        }

        /* Set real data length, in octets
         * If we had UDH, add 6 octets
         */
        *udl_ptr = part->concat_sequence ? (6 + array->len) : array->len;
        mm_dbg ("  user data length is '%u' octets (%s UDH)",
                *udl_ptr,
                part->concat_sequence ? "with" : "without");

        memcpy (&pdu[offset], array->data, array->len);
        offset += array->len;
        g_byte_array_free (array, TRUE);
    } else if (part->encoding == MM_SMS_ENCODING_8BIT) {
        /* Set real data length, in octets
         * If we had UDH, add 6 octets
         */
        *udl_ptr = part->concat_sequence ? (6 + part->data->len) : part->data->len;
        mm_dbg ("  binary user data length is '%u' octets (%s UDH)",
                *udl_ptr,
                part->concat_sequence ? "with" : "without");

        memcpy (&pdu[offset], part->data->data, part->data->len);
        offset += part->data->len;
    } else
        g_assert_not_reached ();

    if (out_pdulen)
        *out_pdulen = offset;
    return pdu;

error:
    g_free (pdu);
    return NULL;
}

gchar **
mm_sms_part_util_split_text (const gchar *text,
                             MMSmsEncoding *encoding)
{
    guint gsm_unsupported = 0;
    gchar **out;
    guint n_chunks;
    guint i;
    guint j;
    gsize in_len;

    if (!text)
        return NULL;

    in_len = strlen (text);

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
     *
     * This method does the split of the input string into N strings, so that
     * each of the strings can be placed in a SMS part.
     */

    /* Check if we can do GSM encoding */
    mm_charset_get_encoded_len (text,
                                MM_MODEM_CHARSET_GSM,
                                &gsm_unsupported);
    if (gsm_unsupported > 0) {
        /* If cannot do it in GSM encoding, do it in UCS-2 */
        GByteArray *array;

        *encoding = MM_SMS_ENCODING_UCS2;

        /* Guess more or less the size of the output array to avoid multiple
         * allocations */
        array = g_byte_array_sized_new (in_len * 2);
        if (!mm_modem_charset_byte_array_append (array,
                                                 text,
                                                 FALSE,
                                                 MM_MODEM_CHARSET_UCS2)) {
            g_byte_array_unref (array);
            return NULL;
        }

        /* Our bytearray has it in UCS-2 now.
         * UCS-2 is a fixed-size encoding, which means that the text has exactly
         * 2 bytes for each unicode point. We can now split this array into
         * chunks of 67 UCS-2 characters (134 bytes).
         *
         * Note that UCS-2 covers unicode points between U+0000 and U+FFFF, which
         * means that there is no direct relationship between the size of the
         * input text in UTF-8 and the size of the text in UCS-2. A 3-byte UTF-8
         * encoded character will still be represented with 2 bytes in UCS-2.
         */
        if (array->len <= 140) {
            out = g_new (gchar *, 2);
            out[0] = g_strdup (text);
            out[1] = NULL;
        } else {
            n_chunks = array->len / 134;
            if (array->len % 134 != 0)
                n_chunks++;

            out = g_new0 (gchar *, n_chunks + 1);
            for (i = 0, j = 0; i < n_chunks; i++, j += 134) {
                out[i] = sms_decode_text (&array->data[j],
                                          MIN (array->len - j, 134),
                                          MM_SMS_ENCODING_UCS2,
                                          0);
            }
        }
        g_byte_array_unref (array);
    } else {
        /* Do it with GSM encoding */
        *encoding = MM_SMS_ENCODING_GSM7;

        if (in_len <= 160) {
            out = g_new (gchar *, 2);
            out[0] = g_strdup (text);
            out[1] = NULL;
        } else {
            n_chunks = in_len / 153;
            if (in_len % 153 != 0)
                n_chunks++;

            out = g_new0 (gchar *, n_chunks + 1);
            for (i = 0, j = 0; i < n_chunks; i++, j += 153) {
                out[i] = g_strndup (&text[j], 153);
            }
        }
    }

    return out;
}

GByteArray **
mm_sms_part_util_split_data (const guint8 *data,
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
