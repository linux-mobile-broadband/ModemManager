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
 * Copyright (C) 2013 Google, Inc.
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-sms-part-cdma.h"
#include "mm-log-test.h"

/********************* PDU PARSER TESTS *********************/

static void
common_test_part_from_hexpdu (const gchar *hexpdu,
                              MMSmsCdmaTeleserviceId expected_teleservice_id,
                              MMSmsCdmaServiceCategory expected_service_category,
                              const gchar *expected_address,
                              guint8 expected_bearer_reply_option,
                              const gchar *expected_text)
{
    MMSmsPart *part;
    GError *error = NULL;

    part = mm_sms_part_cdma_new_from_pdu (0, hexpdu, NULL, &error);
    g_assert_no_error (error);
    g_assert (part != NULL);

    if (expected_teleservice_id != MM_SMS_CDMA_TELESERVICE_ID_UNKNOWN)
        g_assert_cmpuint (expected_teleservice_id, ==, mm_sms_part_get_cdma_teleservice_id (part));
    if (expected_service_category != MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN)
        g_assert_cmpuint (expected_service_category, ==, mm_sms_part_get_cdma_service_category (part));
    if (expected_address) {
        if (expected_address[0])
            g_assert_cmpstr (expected_address, ==, mm_sms_part_get_number (part));
        else
            g_assert (mm_sms_part_get_number (part) == NULL);
    }
    if (expected_bearer_reply_option)
        g_assert_cmpuint (expected_bearer_reply_option, ==, mm_sms_part_get_message_reference (part));
    if (expected_text)
        g_assert_cmpstr (expected_text, ==, mm_sms_part_get_text (part));

    mm_sms_part_free (part);
}

static void
common_test_part_from_pdu (const guint8 *pdu,
                           gsize pdu_size,
                           MMSmsCdmaTeleserviceId expected_teleservice_id,
                           MMSmsCdmaServiceCategory expected_service_category,
                           const gchar *expected_address,
                           guint8 expected_bearer_reply_option,
                           const gchar *expected_text)
{
    gchar *hexpdu;

    hexpdu = mm_utils_bin2hexstr (pdu, pdu_size);
    common_test_part_from_hexpdu (hexpdu,
                                  expected_teleservice_id,
                                  expected_service_category,
                                  expected_address,
                                  expected_bearer_reply_option,
                                  expected_text);
    g_free (hexpdu);
}

static void
common_test_invalid_part_from_hexpdu (const gchar *hexpdu)
{
    MMSmsPart *part;
    GError *error = NULL;

    part = mm_sms_part_cdma_new_from_pdu (0, hexpdu, NULL, &error);
    g_assert (part == NULL);
    /* We don't care for the specific error type */
    g_assert (error != NULL);
    g_error_free (error);
}

static void
common_test_invalid_part_from_pdu (const guint8 *pdu,
                                   gsize pdu_size)
{
    gchar *hexpdu;

    hexpdu = mm_utils_bin2hexstr (pdu, pdu_size);
    common_test_invalid_part_from_hexpdu (hexpdu);
    g_free (hexpdu);
}

static void
test_pdu1 (void)
{
    static const guint8 pdu[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* originating address */
        0x02, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer reply option */
        0x06, 0x01,
        0xFC,
        /* bearer data */
        0x08, 0x15,
        0x00, 0x03, 0x16, 0x8D, 0x30, 0x01, 0x06,
        0x10, 0x24, 0x18, 0x30, 0x60, 0x80, 0x03,
        0x06, 0x10, 0x10, 0x04, 0x04, 0x48, 0x47
    };

    common_test_part_from_pdu (
        pdu, sizeof (pdu),
        MM_SMS_CDMA_TELESERVICE_ID_WMT,
        MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN,
        "3305773196",
        63,
        "AAAA");
}

static void
test_invalid_parameter_length (void)
{
    static const guint8 pdu[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* originating address */
        0x02, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer reply option */
        0x06, 0x01,
        0xFC,
        /* bearer data */
        0x08, 0x20, /* wrong parameter length! */
        0x00, 0x03, 0x16, 0x8D, 0x30, 0x01, 0x06,
        0x10, 0x24, 0x18, 0x30, 0x60, 0x80, 0x03,
        0x06, 0x10, 0x10, 0x04, 0x04, 0x48, 0x47
    };

    common_test_invalid_part_from_pdu (pdu, sizeof (pdu));
}

static void
test_invalid_address_length (void)
{
    static const guint8 pdu[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* originating address (wrong num_fields) */
        0x02, 0x07,
        0x03, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer reply option */
        0x06, 0x01,
        0xFC,
        /* bearer data */
        0x08, 0x15,
        0x00, 0x03, 0x16, 0x8D, 0x30, 0x01, 0x06,
        0x10, 0x24, 0x18, 0x30, 0x60, 0x80, 0x03,
        0x06, 0x10, 0x10, 0x04, 0x04, 0x48, 0x47
    };

    common_test_part_from_pdu (
        pdu, sizeof (pdu),
        MM_SMS_CDMA_TELESERVICE_ID_WMT,
        MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN,
        "",
        63,
        NULL);
}

static void
test_created_by_us (void)
{
    static const guint8 pdu[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* destination address */
        0x04, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer data */
        0x08, 0x0D,
        0x00, 0x03, 0x20, 0x00, 0x00, /* message id */
        0x01, 0x06, 0x10, 0x24, 0x18, 0x30, 0x60, 0x80 /* user_data */
    };

    common_test_part_from_pdu (
        pdu, sizeof (pdu),
        MM_SMS_CDMA_TELESERVICE_ID_WMT,
        MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN,
        "3305773196",
        0,
        "AAAA");
}

static void
test_latin_encoding (void)
{
    static const guint8 pdu[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* originating address */
        0x02, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer reply option */
        0x06, 0x01,
        0xFC,
        /* bearer data */
        0x08, 0x39,
            /* message id */
            0x00, 0x03,
            0x13, 0x8D, 0x20,
            /* user data */
            0x01, 0x27,
            0x41, 0x29, 0x19, 0x22, 0xE1, 0x19, 0x1A, 0xE1,
            0x1A, 0x01, 0x19, 0xA1, 0x19, 0xA1, 0xA9, 0xB1,
            0xB9, 0xE9, 0x53, 0x4B, 0x23, 0xAB, 0x53, 0x23,
            0xAB, 0x23, 0x2B, 0xAB, 0xAB, 0x2B, 0x23, 0xAB,
            0x53, 0x23, 0x2B, 0xAB, 0x53, 0xAB, 0x20,
            /* message center timestamp */
            0x03, 0x06,
            0x13, 0x10, 0x23, 0x20, 0x06, 0x37,
            /* priority indicator */
            0x08, 0x01,
            0x00
    };

    common_test_part_from_pdu (
        pdu, sizeof (pdu),
        MM_SMS_CDMA_TELESERVICE_ID_WMT,
        MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN,
        "3305773196",
        63,
        /* this is ASCII-7 but message uses latin encoding */
        "#$\\##\\#@#4#4567=*idujdudeuuedujdeujud");
}

static void
test_latin_encoding_2 (void)
{
    static const guint8 pdu[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* originating address */
        0x02, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer reply option */
        0x06, 0x01,
        0xFC,
        /* bearer data */
        0x08, 0x1C,
            /* message id */
            0x00, 0x03,
            0x13, 0x8D, 0x20,
            /* user data */
            0x01, 0x0A,
            0x40, 0x42, 0x1B, 0x0B, 0x6B, 0x83, 0x2F, 0x9B,
            0x71, 0x08,
            /* message center timestamp */
            0x03, 0x06,
            0x13, 0x10, 0x23, 0x20, 0x06, 0x37,
            /* priority indicator */
            0x08, 0x01,
            0x00
    };

    common_test_part_from_pdu (
        pdu, sizeof (pdu),
        MM_SMS_CDMA_TELESERVICE_ID_WMT,
        MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN,
        "3305773196",
        63,
        /* this is latin and message uses latin encoding */
        "Campeón!");
}

static void
test_unicode_encoding (void)
{
    static const guint8 pdu[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* originating address */
        0x02, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer reply option */
        0x06, 0x01,
        0xFC,
        /* bearer data */
        0x08, 0x28,
            /* message id */
            0x00, 0x03,
            0x1B, 0x73, 0xF0,
            /* user data */
            0x01, 0x16,
            0x20, 0x52, 0x71, 0x6A, 0xB8, 0x5A, 0xA7, 0x92,
            0xDB, 0xC3, 0x37, 0xC4, 0xB7, 0xDA, 0xDA, 0x82,
            0x98, 0xB4, 0x50, 0x42, 0x94, 0x18,
            /* message center timestamp */
            0x03, 0x06,
            0x13, 0x10, 0x24, 0x10, 0x45, 0x28,
            /* priority indicator */
            0x08, 0x01,
            0x00
    };

    common_test_part_from_pdu (
        pdu, sizeof (pdu),
        MM_SMS_CDMA_TELESERVICE_ID_WMT,
        MM_SMS_CDMA_SERVICE_CATEGORY_UNKNOWN,
        "3305773196",
        63,
        "中國哲學書電子化計劃");
}

/********************* PDU CREATOR TESTS *********************/

static void
trace_pdu (const guint8 *pdu,
           guint         len)
{
    guint i;

    g_print ("n        ");
    for (i = 0; i < len; i++) {
        g_print ("  0x%02X", pdu[i]);
        if (((i + 1) % 12) == 0)
            g_print ("n        ");
    }
    g_print ("n");
}

static void
common_test_create_pdu (MMSmsCdmaTeleserviceId teleservice_id,
                        const gchar *number,
                        const gchar *text,
                        const guint8 *data,
                        gsize data_size,
                        const guint8 *expected,
                        gsize expected_size)
{
    MMSmsPart *part;
    guint8 *pdu;
    guint len = 0;
    GError *error = NULL;

    g_assert (number != NULL);

    part = mm_sms_part_new (0, MM_SMS_PDU_TYPE_CDMA_SUBMIT);
    mm_sms_part_set_cdma_teleservice_id (part, teleservice_id);
    mm_sms_part_set_number (part, number);
    if (text)
        mm_sms_part_set_text (part, text);
    else {
        GByteArray *data_bytearray;

        data_bytearray = g_byte_array_sized_new (data_size);
        g_byte_array_append (data_bytearray, data, data_size);
        mm_sms_part_take_data (part, data_bytearray);
    }

    pdu = mm_sms_part_cdma_get_submit_pdu (part, &len, NULL, &error);
    mm_sms_part_free (part);

    if (g_test_verbose ())
        trace_pdu (pdu, len);

    g_assert_no_error (error);
    g_assert (pdu != NULL);
    g_assert_cmpuint (len, ==, expected_size);
    g_assert_cmpint (memcmp (pdu, expected, len), ==, 0);

    g_free (pdu);
}

static void
test_create_pdu_text_ascii_encoding (void)
{
    static const char *number = "3305773196";
    static const char *text = "AAAA";
    static const guint8 expected[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* destination address */
        0x04, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer data */
        0x08, 0x0D,
        0x00, 0x03, 0x20, 0x00, 0x00, /* message id */
        0x01, 0x06, 0x10, 0x24, 0x18, 0x30, 0x60, 0x80 /* user_data */
    };

    common_test_create_pdu (MM_SMS_CDMA_TELESERVICE_ID_WMT,
                            number,
                            text,
                            NULL, 0,
                            expected, sizeof (expected));
}

static void
test_create_pdu_text_latin_encoding (void)
{
    static const char *number = "3305773196";
    static const char *text = "Campeón!";
    static const guint8 expected[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* destination address */
        0x04, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer data */
        0x08, 0x11,
            /* message id */
            0x00, 0x03,
            0x20, 0x00, 0x00,
            /* user data */
            0x01, 0x0A,
            0x40, 0x42, 0x1B, 0x0B, 0x6B, 0x83, 0x2F, 0x9B,
            0x71, 0x08
    };

    common_test_create_pdu (MM_SMS_CDMA_TELESERVICE_ID_WMT,
                            number,
                            text,
                            NULL, 0,
                            expected, sizeof (expected));
}

static void
test_create_pdu_text_unicode_encoding (void)
{
    static const char *number = "3305773196";
    static const char *text = "中國哲學書電子化計劃";
    static const guint8 expected[] = {
        /* message type */
        0x00,
        /* teleservice id */
        0x00, 0x02,
        0x10, 0x02,
        /* destination address */
        0x04, 0x07,
        0x02, 0x8C, 0xE9, 0x5D, 0xCC, 0x65, 0x80,
        /* bearer data */
        0x08, 0x1D,
            /* message id */
            0x00, 0x03,
            0x20, 0x00, 0x00,
            /* user data */
            0x01, 0x16,
            0x20, 0x52, 0x71, 0x6A, 0xB8, 0x5A, 0xA7, 0x92,
            0xDB, 0xC3, 0x37, 0xC4, 0xB7, 0xDA, 0xDA, 0x82,
            0x98, 0xB4, 0x50, 0x42, 0x94, 0x18
    };

    common_test_create_pdu (MM_SMS_CDMA_TELESERVICE_ID_WMT,
                            number,
                            text,
                            NULL, 0,
                            expected, sizeof (expected));
}

static void
test_create_parse_pdu_text_ascii_encoding (void)
{
#define MAX_TEXT_LEN 100
    guint i;
    gchar text[MAX_TEXT_LEN + 1];

    memset (text, 0, sizeof (text));

    for (i = 0; i < MAX_TEXT_LEN; i++) {
        MMSmsPart *part;
        guint8 *pdu;
        guint len = 0;
        GError *error = NULL;

        text[i]='A';

        part = mm_sms_part_new (0, MM_SMS_PDU_TYPE_CDMA_SUBMIT);
        mm_sms_part_set_cdma_teleservice_id (part, MM_SMS_CDMA_TELESERVICE_ID_WMT);
        mm_sms_part_set_number (part, "123456789");
        mm_sms_part_set_text (part, text);
        pdu = mm_sms_part_cdma_get_submit_pdu (part, &len, NULL, &error);
        g_assert_no_error (error);
        g_assert (pdu != NULL);
        mm_sms_part_free (part);

        part = mm_sms_part_cdma_new_from_binary_pdu (0, pdu, len, NULL, &error);
        g_assert_no_error (error);
        g_assert (part != NULL);
        g_assert_cmpuint (MM_SMS_CDMA_TELESERVICE_ID_WMT, ==, mm_sms_part_get_cdma_teleservice_id (part));
        g_assert_cmpstr ("123456789", ==, mm_sms_part_get_number (part));
        g_assert_cmpstr (text, ==, mm_sms_part_get_text (part));
        mm_sms_part_free (part);

        g_free (pdu);
    }
}

/************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/pdu1", test_pdu1);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/invalid-parameter-length", test_invalid_parameter_length);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/invalid-address-length", test_invalid_address_length);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/created-by-us", test_created_by_us);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/latin-encoding", test_latin_encoding);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/latin-encoding-2", test_latin_encoding_2);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/unicode-encoding", test_unicode_encoding);

    g_test_add_func ("/MM/SMS/CDMA/PDU-Creator/ascii-encoding", test_create_pdu_text_ascii_encoding);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Creator/latin-encoding", test_create_pdu_text_latin_encoding);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Creator/unicode-encoding", test_create_pdu_text_unicode_encoding);

    g_test_add_func ("/MM/SMS/CDMA/PDU-Creator-Parser/ascii-encoding", test_create_parse_pdu_text_ascii_encoding);

    return g_test_run ();
}
