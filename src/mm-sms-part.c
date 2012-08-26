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
#include <mm-errors-types.h>

#include "mm-sms-part.h"
#include "mm-charsets.h"
#include "mm-utils.h"
#include "mm-log.h"

#define PDU_SIZE 200

#define SMS_TP_MTI_MASK               0x03
#define  SMS_TP_MTI_SMS_DELIVER       0x00
#define  SMS_TP_MTI_SMS_SUBMIT_REPORT 0x01
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

typedef enum {
    MM_SMS_ENCODING_UNKNOWN = 0x0,
    MM_SMS_ENCODING_GSM7,
    MM_SMS_ENCODING_8BIT,
    MM_SMS_ENCODING_UCS2
} SmsEncoding;

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

static SmsEncoding
sms_encoding_type (int dcs)
{
    SmsEncoding scheme = MM_SMS_ENCODING_UNKNOWN;

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
sms_decode_text (const guint8 *text, int len, SmsEncoding encoding, int bit_offset)
{
    char *utf8;
    guint8 *unpacked;
    guint32 unpacked_len;

    if (encoding == MM_SMS_ENCODING_GSM7) {
        unpacked = gsm_unpack ((const guint8 *) text, len, bit_offset, &unpacked_len);
        utf8 = (char *) mm_charset_gsm_unpacked_to_utf8 (unpacked, unpacked_len);
        g_free (unpacked);
    } else if (encoding == MM_SMS_ENCODING_UCS2)
        utf8 = g_convert ((char *) text, len, "UTF8", "UCS-2BE", NULL, NULL, NULL);
    else {
        g_warn_if_reached ();
        utf8 = g_strdup ("");
    }

    return utf8;
}

struct _MMSmsPart {
    guint index;
    gchar *smsc;
    gchar *timestamp;
    gchar *number;
    gchar *text;
    GByteArray *data;
    guint data_coding_scheme;
    guint class;
    guint validity;

    gboolean should_concat;
    guint concat_reference;
    guint concat_max;
    guint concat_sequence;
};

void
mm_sms_part_free (MMSmsPart *self)
{
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

#define PART_SET_UINT_FUNC(name)              \
    void                                      \
    mm_sms_part_set_##name (MMSmsPart *self,  \
                            guint value)      \
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
PART_SET_UINT_FUNC (index)
PART_GET_FUNC (const gchar *, smsc)
PART_SET_TAKE_STR_FUNC (smsc)
PART_GET_FUNC (const gchar *, number)
PART_SET_TAKE_STR_FUNC (number)
PART_GET_FUNC (const gchar *, timestamp)
PART_SET_TAKE_STR_FUNC (timestamp)
PART_GET_FUNC (guint, concat_max)
PART_SET_UINT_FUNC (concat_max)
PART_GET_FUNC (guint, concat_sequence)
PART_SET_UINT_FUNC (concat_sequence)
PART_GET_FUNC (const gchar *, text)
PART_SET_TAKE_STR_FUNC (text)
PART_GET_FUNC (guint, data_coding_scheme)
PART_SET_UINT_FUNC (data_coding_scheme)
PART_GET_FUNC (guint, class)
PART_SET_UINT_FUNC (class)
PART_GET_FUNC (guint, validity)
PART_SET_UINT_FUNC (validity)

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
    self->data = g_byte_array_ref (value);
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
mm_sms_part_new (guint index)
{
    MMSmsPart *sms_part;

    sms_part = g_slice_new0 (MMSmsPart);
    sms_part->index = index;

    return sms_part;
}

MMSmsPart *
mm_sms_part_new_from_pdu (guint index,
                          const gchar *hexpdu,
                          GError **error)
{
    MMSmsPart *sms_part;
    gsize pdu_len;
    guint8 *pdu;
    guint smsc_addr_num_octets, variable_length_items, msg_start_offset,
            sender_addr_num_digits, sender_addr_num_octets,
            tp_pid_offset, tp_dcs_offset, user_data_offset, user_data_len,
            user_data_len_offset, bit_offset;
    SmsEncoding user_data_encoding;
    GByteArray *raw;

    /* Convert PDU from hex to binary */
    pdu = (guint8 *) utils_hexstr2bin (hexpdu, &pdu_len);
    if (!pdu) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Couldn't convert PDU from hex to binary");
        return NULL;
    }

    /* SMSC, in address format, precedes the TPDU */
    smsc_addr_num_octets = pdu[0];
    variable_length_items = smsc_addr_num_octets;
    if (pdu_len < variable_length_items + SMS_MIN_PDU_LEN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "PDU too short (1): %zd < %d",
                     pdu_len,
                     variable_length_items + SMS_MIN_PDU_LEN);
        g_free (pdu);
        return NULL;
    }

    /* where in the PDU the actual SMS protocol message begins */
    msg_start_offset = 1 + smsc_addr_num_octets;
    sender_addr_num_digits = pdu[msg_start_offset + 1];

    /*
     * round the sender address length up to an even number of
     * semi-octets, and thus an integral number of octets
     */
    sender_addr_num_octets = (sender_addr_num_digits + 1) >> 1;
    variable_length_items += sender_addr_num_octets;
    if (pdu_len < variable_length_items + SMS_MIN_PDU_LEN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "PDU too short (2): %zd < %d",
                     pdu_len,
                     variable_length_items + SMS_MIN_PDU_LEN);
        g_free (pdu);
        return NULL;
    }

    tp_pid_offset = msg_start_offset + 3 + sender_addr_num_octets;
    tp_dcs_offset = tp_pid_offset + 1;

    user_data_len_offset = tp_dcs_offset + 1 + SMS_TIMESTAMP_LEN;
    user_data_offset = user_data_len_offset + 1;
    user_data_len = pdu[user_data_len_offset];
    user_data_encoding = sms_encoding_type(pdu[tp_dcs_offset]);

    if (user_data_encoding == MM_SMS_ENCODING_GSM7)
        variable_length_items += (7 * (user_data_len + 1 )) / 8;
    else
        variable_length_items += user_data_len;

    if (pdu_len < variable_length_items + SMS_MIN_PDU_LEN) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "PDU too short (3): %zd < %d",
                     pdu_len,
                     variable_length_items + SMS_MIN_PDU_LEN);
        g_free (pdu);
        return NULL;
    }

    /* Only handle SMS-DELIVER */
    if ((pdu[msg_start_offset] & SMS_TP_MTI_MASK) != SMS_TP_MTI_SMS_DELIVER) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unhandled message type: 0x%02x",
                     pdu[msg_start_offset]);
        g_free (pdu);
        return NULL;
    }

    /* Create the new MMSmsPart */
    sms_part = mm_sms_part_new (index);
    mm_sms_part_take_smsc (sms_part,
                           sms_decode_address (&pdu[1], 2 * (pdu[0] - 1)));
    mm_sms_part_take_number (sms_part,
                             sms_decode_address (&pdu[msg_start_offset + 2],
                                                 pdu[msg_start_offset + 1]));
    mm_sms_part_take_timestamp (sms_part,
                                sms_decode_timestamp (&pdu[tp_dcs_offset + 1]));

    bit_offset = 0;
    if (pdu[msg_start_offset] & SMS_TP_UDHI) {
        int udhl, end, offset;
        udhl = pdu[user_data_offset] + 1;
        end = user_data_offset + udhl;

        for (offset = user_data_offset + 1; offset < end;) {
            guint8 ie_id, ie_len;

            ie_id = pdu[offset++];
            ie_len = pdu[offset++];

            switch (ie_id) {
                case 0x00:
                    /*
                     * Ignore the IE if one of the following is true:
                     *  - it claims to be part 0 of M
                     *  - it claims to be part N of M, N > M
                     */
                    if (pdu[offset + 2] == 0 ||
                        pdu[offset + 2] > pdu[offset + 1])
                        break;

                    mm_sms_part_set_concat_reference (sms_part,
                                                      pdu[offset]);
                    mm_sms_part_set_concat_max (sms_part,
                                                pdu[offset + 1]);
                    mm_sms_part_set_concat_sequence (sms_part,
                                                     pdu[offset + 2]);
                    break;
                case 0x08:
                    /* Concatenated short message, 16-bit reference */
                    if (pdu[offset + 3] == 0 ||
                        pdu[offset + 3] > pdu[offset + 2])
                        break;

                    mm_sms_part_set_concat_reference (sms_part,
                                                      (pdu[offset] << 8) | pdu[offset + 1]);
                    mm_sms_part_set_concat_max (sms_part,
                                                pdu[offset + 2]);
                    mm_sms_part_set_concat_sequence (sms_part,
                                                     pdu[offset + 3]);
                    break;
            }

            offset += ie_len;
        }

        /*
         * Move past the user data headers to prevent it from being
         * decoded into garbage text.
         */
        user_data_offset += udhl;
        if (user_data_encoding == MM_SMS_ENCODING_GSM7) {
            /*
             * Find the number of bits we need to add to the length of the
             * user data to get a multiple of 7 (the padding).
             */
            bit_offset = (7 - udhl % 7) % 7;
            user_data_len -= (udhl * 8 + bit_offset) / 7;
        } else
            user_data_len -= udhl;
    }

    if (   user_data_encoding == MM_SMS_ENCODING_8BIT
        || user_data_encoding == MM_SMS_ENCODING_UNKNOWN) {
        /* 8-bit encoding is usually binary data, and we have no idea what
         * actual encoding the data is in so we can't convert it.
         */
        mm_sms_part_set_text (sms_part, "");
    } else {
        /* Otherwise if it's 7-bit or UCS2 we can decode it */
        mm_sms_part_take_text (sms_part,
                               sms_decode_text (&pdu[user_data_offset], user_data_len,
                                                user_data_encoding, bit_offset));
        g_warn_if_fail (sms_part->text != NULL);
    }

    /* Add the raw PDU data */
    raw = g_byte_array_sized_new (user_data_len);
    g_byte_array_append (raw, &pdu[user_data_offset], user_data_len);
    mm_sms_part_take_data (sms_part, raw);
    mm_sms_part_set_data_coding_scheme (sms_part, pdu[tp_dcs_offset] & 0xFF);

    if (pdu[tp_dcs_offset] & SMS_DCS_CLASS_VALID)
        mm_sms_part_set_class (sms_part,
                               pdu[tp_dcs_offset] & SMS_DCS_CLASS_MASK);

    g_free (pdu);

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

/**
 * mm_sms_part_get_submit_pdu:
 *
 * @number: the subscriber number to send this message to
 * @text: the body of this SMS
 * @smsc: if given, the SMSC address
 * @validity: minutes until the SMS should expire in the SMSC, or 0 for a
 *  suitable default
 * @class: unused
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
    MMModemCharset best_cs = MM_MODEM_CHARSET_GSM;
    guint ucs2len = 0, gsm_unsupported = 0;
    guint textlen = 0;

    g_return_val_if_fail (part->number != NULL, NULL);
    g_return_val_if_fail (part->text != NULL, NULL);

    /* FIXME: support multiple fragments. */

    textlen = mm_charset_get_encoded_len (part->text, MM_MODEM_CHARSET_GSM, &gsm_unsupported);
    if (textlen > 160) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_UNSUPPORTED,
                             "Cannot encode message to fit into an SMS.");
        return NULL;
    }

    /* If there are characters that are unsupported in the GSM charset, try
     * UCS2.  If the UCS2 encoded string is too long to fit in an SMS, then
     * just use GSM and suck up the unconverted chars.
     */
    if (gsm_unsupported > 0) {
        ucs2len = mm_charset_get_encoded_len (part->text, MM_MODEM_CHARSET_UCS2, NULL);
        if (ucs2len <= 140) {
            best_cs = MM_MODEM_CHARSET_UCS2;
            textlen = ucs2len;
        }
    }

    /* Build up the PDU */
    pdu = g_malloc0 (PDU_SIZE);

    if (part->smsc) {
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

    if (part->validity > 0)
        pdu[offset] = 1 << 4; /* TP-VP present; format RELATIVE */
    else
        pdu[offset] = 0;      /* TP-VP not present */
    pdu[offset++] |= 0x01;    /* TP-MTI = SMS-SUBMIT */

    pdu[offset++] = 0x00;     /* TP-Message-Reference: filled by device */

    len = mm_sms_part_encode_address (part->number, &pdu[offset], PDU_SIZE - offset, FALSE);
    if (len == 0) {
        g_set_error (error,
                     MM_MESSAGE_ERROR,
                     MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                     "Invalid number '%s'", part->number);
        goto error;
    }
    offset += len;

    /* TP-PID */
    pdu[offset++] = 0x00;

    /* TP-DCS */
    if (best_cs == MM_MODEM_CHARSET_UCS2)
        pdu[offset++] = 0x08;
    else
        pdu[offset++] = 0x00;  /* GSM */

    /* TP-Validity-Period: 4 days */
    if (part->validity > 0)
        pdu[offset++] = validity_to_relative (part->validity);

    /* TP-User-Data-Length */
    pdu[offset++] = textlen;

    if (best_cs == MM_MODEM_CHARSET_GSM) {
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

        packed = gsm_pack (unpacked, unlen, 0, &packlen);
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
    } else if (best_cs == MM_MODEM_CHARSET_UCS2) {
        GByteArray *array;

        array = g_byte_array_sized_new (textlen / 2);
        if (!mm_modem_charset_byte_array_append (array, part->text, FALSE, best_cs)) {
            g_byte_array_free (array, TRUE);
            g_set_error_literal (error,
                                 MM_MESSAGE_ERROR,
                                 MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER,
                                 "Failed to convert message text to UCS2");
            goto error;
        }

        memcpy (&pdu[offset], array->data, array->len);
        offset += array->len;
        g_byte_array_free (array, TRUE);
    } else
        g_assert_not_reached ();

    if (out_pdulen)
        *out_pdulen = offset;
    return pdu;

error:
    g_free (pdu);
    return NULL;
}
