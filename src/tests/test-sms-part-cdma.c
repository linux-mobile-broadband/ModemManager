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
#include "mm-log.h"

/* If defined will print debugging traces */
#ifdef TEST_SMS_PART_ENABLE_TRACE
#define trace_pdu(pdu, pdu_len) do {      \
        guint i;                          \
                                          \
        g_print ("\n        ");           \
        for (i = 0; i < len; i++) {       \
            g_print ("  0x%02X", pdu[i]); \
            if (((i + 1) % 12) == 0)      \
                g_print ("\n        ");   \
        }                                 \
        g_print ("\n");                   \
    } while (0)
#else
#define trace_pdu(...)
#endif

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

    mm_dbg (" ");
    part = mm_sms_part_cdma_new_from_pdu (0, hexpdu, &error);
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

    mm_dbg (" ");
    part = mm_sms_part_cdma_new_from_pdu (0, hexpdu, &error);
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

/************************************************************/

void
_mm_log (const char *loc,
         const char *func,
         guint32 level,
         const char *fmt,
         ...)
{
#if defined ENABLE_TEST_MESSAGE_TRACES
    /* Dummy log function */
    va_list args;
    gchar *msg;

    va_start (args, fmt);
    msg = g_strdup_vprintf (fmt, args);
    va_end (args);
    g_print ("%s\n", msg);
    g_free (msg);
#endif
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/pdu1", test_pdu1);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/invalid-parameter-length", test_invalid_parameter_length);
    g_test_add_func ("/MM/SMS/CDMA/PDU-Parser/invalid-address-length", test_invalid_address_length);

    return g_test_run ();
}
