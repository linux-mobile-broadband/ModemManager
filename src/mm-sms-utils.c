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
 * Copyright (C) 2011 Red Hat, Inc.
 */

#include <glib.h>

#include "mm-charsets.h"
#include "mm-errors.h"
#include "mm-utils.h"
#include "mm-sms-utils.h"

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
    else if (encoding == MM_SMS_ENCODING_8BIT)
        utf8 = g_strndup ((const char *)text, len);
    else
        utf8 = g_strdup ("");

    return utf8;
}

static void
simple_free_gvalue (gpointer data)
{
    g_value_unset ((GValue *) data);
    g_slice_free (GValue, data);
}



static GValue *
simple_uint_value (guint32 i)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_UINT);
    g_value_set_uint (val, i);

    return val;
}

static GValue *
simple_boolean_value (gboolean b)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_BOOLEAN);
    g_value_set_boolean (val, b);

    return val;
}

static GValue *
simple_string_value (const char *str)
{
    GValue *val;

    val = g_slice_new0 (GValue);
    g_value_init (val, G_TYPE_STRING);
    g_value_set_string (val, str);

    return val;
}

GHashTable *
sms_parse_pdu (const char *hexpdu, GError **error)
{
    GHashTable *properties;
    gsize pdu_len;
    guint8 *pdu;
    int smsc_addr_num_octets, variable_length_items, msg_start_offset,
            sender_addr_num_digits, sender_addr_num_octets,
            tp_pid_offset, tp_dcs_offset, user_data_offset, user_data_len,
            user_data_len_offset, bit_offset;
    char *smsc_addr, *sender_addr, *sc_timestamp, *msg_text;
    SmsEncoding user_data_encoding;

    /* Convert PDU from hex to binary */
    pdu = (guint8 *) utils_hexstr2bin (hexpdu, &pdu_len);
    if (!pdu) {
        *error = g_error_new_literal (MM_MODEM_ERROR,
                                      MM_MODEM_ERROR_GENERAL,
                                      "Couldn't parse PDU of SMS GET response from hex");
        return NULL;
    }

    /* SMSC, in address format, precedes the TPDU */
    smsc_addr_num_octets = pdu[0];
    variable_length_items = smsc_addr_num_octets;
    if (pdu_len < variable_length_items + SMS_MIN_PDU_LEN) {
        *error = g_error_new (MM_MODEM_ERROR,
                              MM_MODEM_ERROR_GENERAL,
                              "PDU too short (1): %zd vs %d", pdu_len,
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
        *error = g_error_new (MM_MODEM_ERROR,
                              MM_MODEM_ERROR_GENERAL,
                              "PDU too short (2): %zd vs %d", pdu_len,
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
        *error = g_error_new (MM_MODEM_ERROR,
                              MM_MODEM_ERROR_GENERAL,
                              "PDU too short (3): %zd vs %d", pdu_len,
                              variable_length_items + SMS_MIN_PDU_LEN);
        g_free (pdu);
        return NULL;
    }

    /* Only handle SMS-DELIVER */
    if ((pdu[msg_start_offset] & SMS_TP_MTI_MASK) != SMS_TP_MTI_SMS_DELIVER) {
        *error = g_error_new (MM_MODEM_ERROR,
                              MM_MODEM_ERROR_GENERAL,
                              "Unhandled message type: 0x%02x",
                              pdu[msg_start_offset]);
        g_free (pdu);
        return NULL;
    }

    smsc_addr = sms_decode_address (&pdu[1], 2 * (pdu[0] - 1));
    sender_addr = sms_decode_address (&pdu[msg_start_offset + 2],
                                      pdu[msg_start_offset + 1]);
    sc_timestamp = sms_decode_timestamp (&pdu[tp_dcs_offset + 1]);
    bit_offset = 0;
    if (pdu[msg_start_offset] & SMS_TP_UDHI) {
        /*
         * Skip over the user data headers to prevent it from being
         * decoded into garbage text.
         */
        int udhl;
        udhl = pdu[user_data_offset] + 1;
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

    msg_text = sms_decode_text (&pdu[user_data_offset], user_data_len,
                                user_data_encoding, bit_offset);

    properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                        simple_free_gvalue);
    g_hash_table_insert (properties, "number",
                         simple_string_value (sender_addr));
    g_hash_table_insert (properties, "text",
                         simple_string_value (msg_text));
    g_hash_table_insert (properties, "smsc",
                         simple_string_value (smsc_addr));
    g_hash_table_insert (properties, "timestamp",
                         simple_string_value (sc_timestamp));
    if (pdu[tp_dcs_offset] & SMS_DCS_CLASS_VALID)
        g_hash_table_insert (properties, "class",
                             simple_uint_value (pdu[tp_dcs_offset] &
                                                SMS_DCS_CLASS_MASK));
    g_hash_table_insert (properties, "completed", simple_boolean_value (TRUE));

    g_free (smsc_addr);
    g_free (sender_addr);
    g_free (sc_timestamp);
    g_free (msg_text);
    g_free (pdu);


    return properties;
}
