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
 * Copyright (C) 2025 Dan Williams <dan@ioncontrol.co>
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-sms-part-3gpp.h"
#include "mm-sms-list.h"
#include "mm-log-test.h"
#include "mm-base-modem.h"

#include "mm-iface-modem-messaging.h"

/****************************************************************/

static void
test_mbim_multipart_unstored (void)
{
    static const gchar *part1_pdu =
        "07912160130300f4440b915155685703f900005240713104738a3e050003c40202da6f37881e96"
        "9fcbf4b4fb0ccabfeb20f4fb0ea287e5e7323ded3e83dae17519747fcbd96490b95c6683d27310"
        "1d5d0601";
    static const gchar *part2_pdu =
        "07912160130300f4440b915155685703f900005240713104738aa0050003c40201ac69373d7c2e"
        "83e87538bc2cbf87e565d039dc2e83c220769a4e6797416132394d4fbfdda0fb5b4e4783c2ee3c"
        "888e2e83e86fd0db0c1a86e769f71b647eb3d9ef7bda7d06a5e7a0b09b0c9ab3df74109c1dce83"
        "e8e8301d44479741f9771d949e83e861f9b94c4fbbcf20f13b4c9f83e8e832485c068ddfedf6db"
        "0da2a3cba0fcbb0e1abfdb";
    MMSmsPart *part1, *part2;
    MMSmsList *list;
    MMBaseSms *sms1, *sms2;
    GError *error = NULL;

    /* Some MBIM devices (Dell 5821e) report SMSes via the MBIM_CID_SMS_READ
     * unsolicited notification usually used for Class-0 (flash/alert) messages.
     * These always have a message index of 0; thus when a multipart SMS
     * arrives it comes as two individual notifications both with 0 indexes.
     * The MBIM modem code used SMS_PART_INVALID_INDEX for unstored SMSes.
     * Ensure the MMSmsList can handle combining the two parts with the same
     * index.
     */
    part1 = mm_sms_part_3gpp_new_from_pdu (SMS_PART_INVALID_INDEX, part1_pdu, NULL, &error);
    g_assert_no_error (error);
    part2 = mm_sms_part_3gpp_new_from_pdu (SMS_PART_INVALID_INDEX, part2_pdu, NULL, &error);
    g_assert_no_error (error);

    list = mm_sms_list_new (NULL);

    sms1 = MM_BASE_SMS (g_object_new (MM_TYPE_BASE_SMS,
                                      MM_BASE_SMS_IS_3GPP, TRUE,
                                      MM_BASE_SMS_DEFAULT_STORAGE, MM_SMS_STORAGE_MT,
                                      NULL));
    mm_sms_list_take_part (list,
                           sms1,
                           part1,
                           MM_SMS_STATE_RECEIVED,
                           MM_SMS_STORAGE_MT,
                           &error);
    g_assert_no_error (error);

    sms2 = MM_BASE_SMS (g_object_new (MM_TYPE_BASE_SMS,
                                      MM_BASE_SMS_IS_3GPP, TRUE,
                                      MM_BASE_SMS_DEFAULT_STORAGE, MM_SMS_STORAGE_MT,
                                      NULL));
    mm_sms_list_take_part (list,
                           sms2,
                           part2,
                           MM_SMS_STATE_RECEIVED,
                           MM_SMS_STORAGE_MT,
                           &error);
    g_assert_no_error (error);

    g_assert_cmpint (mm_sms_list_get_count (list), ==, 1);
}

/****************************************************************/

static void
test_mbim_multipart_zero_index (void)
{
    static const gchar *pdu =
        "07912160130300f4440b915155685703f900005240713104738a3e050003c40202da6f37881e96"
        "9fcbf4b4fb0ccabfeb20f4fb0ea287e5e7323ded3e83dae17519747fcbd96490b95c6683d27310"
        "1d5d0601";
    MMSmsPart *part;
    MMSmsList *list;
    MMBaseSms *sms;
    GError *error = NULL;

    part = mm_sms_part_3gpp_new_from_pdu (0, pdu, NULL, &error);
    g_assert_no_error (error);

    list = mm_sms_list_new (NULL);

    sms = MM_BASE_SMS (g_object_new (MM_TYPE_BASE_SMS,
                                     MM_BASE_SMS_IS_3GPP, TRUE,
                                     MM_BASE_SMS_DEFAULT_STORAGE, MM_SMS_STORAGE_MT,
                                     NULL));
    mm_sms_list_take_part (list,
                           sms,
                           part,
                           MM_SMS_STATE_RECEIVED,
                           MM_SMS_STORAGE_MT,
                           &error);
    g_assert_no_error (error);

    g_assert_cmpint (mm_sms_list_get_count (list), ==, 1);
}

/****************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/SMS/3GPP/sms-list/zero-index", test_mbim_multipart_zero_index);
    g_test_add_func ("/MM/SMS/3GPP/sms-list/mbim-multipart-unstored", test_mbim_multipart_unstored);

    return g_test_run ();
}
