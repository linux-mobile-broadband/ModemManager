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
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (C) 2011 The Chromium OS Authors.
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>

#include "mm-sms-utils.h"
#include "mm-utils.h"


#define TEST_ENTRY_EQ(hash, key, expectvalue) do { \
  GValue *value;                          \
  value = g_hash_table_lookup((hash), (key)); \
  g_assert(value); \
  g_assert(G_VALUE_HOLDS_STRING(value)); \
  g_assert_cmpstr(g_value_get_string(value), ==, (expectvalue)); \
  } while (0)

static void
test_pdu1 (void *f, gpointer d)
{
  static const guint8 pdu[] = {
    0x07, 0x91, 0x21, 0x04, 0x44, 0x29, 0x61, 0xf4,
    0x04, 0x0b, 0x91, 0x61, 0x71, 0x95, 0x72, 0x91,
    0xf8, 0x00, 0x00, 0x11, 0x20, 0x82, 0x11, 0x05,
    0x05, 0x0a,
    // user data:
    0x6a, 0xc8, 0xb2, 0xbc, 0x7c, 0x9a, 0x83, 0xc2,
    0x20, 0xf6, 0xdb, 0x7d, 0x2e, 0xcb, 0x41, 0xed,
    0xf2, 0x7c, 0x1e, 0x3e, 0x97, 0x41, 0x1b, 0xde,
    0x06, 0x75, 0x4f, 0xd3, 0xd1, 0xa0, 0xf9, 0xbb,
    0x5d, 0x06, 0x95, 0xf1, 0xf4, 0xb2, 0x9b, 0x5c,
    0x26, 0x83, 0xc6, 0xe8, 0xb0, 0x3c, 0x3c, 0xa6,
    0x97, 0xe5, 0xf3, 0x4d, 0x6a, 0xe3, 0x03, 0xd1,
    0xd1, 0xf2, 0xf7, 0xdd, 0x0d, 0x4a, 0xbb, 0x59,
    0xa0, 0x79, 0x7d, 0x8c, 0x06, 0x85, 0xe7, 0xa0,
    0x00, 0x28, 0xec, 0x26, 0x83, 0x2a, 0x96, 0x0b,
    0x28, 0xec, 0x26, 0x83, 0xbe, 0x60, 0x50, 0x78,
    0x0e, 0xba, 0x97, 0xd9, 0x6c, 0x17};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+12404492164");
  TEST_ENTRY_EQ (sms, "number", "+16175927198");
  TEST_ENTRY_EQ (sms, "timestamp", "110228115050-05");
  TEST_ENTRY_EQ (sms, "text",
                "Here's a longer message [{with some extended characters}] "
                "thrown in, such as £ and ΩΠΨ and §¿ as well.");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}

static void
test_pdu2 (void *f, gpointer d)
{
  static const guint8 pdu[] = {
    0x07, 0x91, 0x97, 0x30, 0x07, 0x11, 0x11, 0xf1,
    0x04, 0x14, 0xd0, 0x49, 0x37, 0xbd, 0x2c, 0x77,
    0x97, 0xe9, 0xd3, 0xe6, 0x14, 0x00, 0x08, 0x11,
    0x30, 0x92, 0x91, 0x02, 0x40, 0x61, 0x08, 0x04,
    0x42, 0x04, 0x35, 0x04, 0x41, 0x04, 0x42};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+79037011111");
  TEST_ENTRY_EQ (sms, "number", "InternetSMS");
  TEST_ENTRY_EQ (sms, "timestamp", "110329192004+04");
  TEST_ENTRY_EQ (sms, "text", "тест");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}

static void
test_pdu3 (void *f, gpointer d)
{
  static const guint8 pdu[] = {
    0x07, 0x91, 0x21, 0x43, 0x65, 0x87, 0x09, 0xf1,
    0x04, 0x0b, 0x91, 0x81, 0x00, 0x55, 0x15, 0x12,
    0xf2, 0x00, 0x00, 0x11, 0x10, 0x10, 0x21, 0x43,
    0x65, 0x00, 0x0a, 0xe8, 0x32, 0x9b, 0xfd, 0x46,
    0x97, 0xd9, 0xec, 0x37};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+12345678901");
  TEST_ENTRY_EQ (sms, "number", "+18005551212");
  TEST_ENTRY_EQ (sms, "timestamp", "110101123456+00");
  TEST_ENTRY_EQ (sms, "text", "hellohello");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}


static void
test_pdu3_nzpid (void *f, gpointer d)
{
  /* pid is nonzero (00 -> ff) */
  static const guint8 pdu[] = {
    0x07, 0x91, 0x21, 0x43, 0x65, 0x87, 0x09, 0xf1,
    0x04, 0x0b, 0x91, 0x81, 0x00, 0x55, 0x15, 0x12,
    0xf2, 0xff, 0x00, 0x11, 0x10, 0x10, 0x21, 0x43,
    0x65, 0x00, 0x0a, 0xe8, 0x32, 0x9b, 0xfd, 0x46,
    0x97, 0xd9, 0xec, 0x37};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+12345678901");
  TEST_ENTRY_EQ (sms, "number", "+18005551212");
  TEST_ENTRY_EQ (sms, "timestamp", "110101123456+00");
  TEST_ENTRY_EQ (sms, "text", "hellohello");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}



static void
test_pdu3_mms (void *f, gpointer d)
{
  /* mms is clear (04 -> 00) */
  static const guint8 pdu[] = {
    0x07, 0x91, 0x21, 0x43, 0x65, 0x87, 0x09, 0xf1,
    0x00, 0x0b, 0x91, 0x81, 0x00, 0x55, 0x15, 0x12,
    0xf2, 0x00, 0x00, 0x11, 0x10, 0x10, 0x21, 0x43,
    0x65, 0x00, 0x0a, 0xe8, 0x32, 0x9b, 0xfd, 0x46,
    0x97, 0xd9, 0xec, 0x37};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+12345678901");
  TEST_ENTRY_EQ (sms, "number", "+18005551212");
  TEST_ENTRY_EQ (sms, "timestamp", "110101123456+00");
  TEST_ENTRY_EQ (sms, "text", "hellohello");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}


static void
test_pdu3_natl (void *f, gpointer d)
{
  /* number is natl (91 -> 81) */
  static const guint8 pdu[] = {
    0x07, 0x91, 0x21, 0x43, 0x65, 0x87, 0x09, 0xf1,
    0x04, 0x0b, 0x81, 0x81, 0x00, 0x55, 0x15, 0x12,
    0xf2, 0x00, 0x00, 0x11, 0x10, 0x10, 0x21, 0x43,
    0x65, 0x00, 0x0a, 0xe8, 0x32, 0x9b, 0xfd, 0x46,
    0x97, 0xd9, 0xec, 0x37};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+12345678901");
  TEST_ENTRY_EQ (sms, "number", "18005551212"); /* no plus */
  TEST_ENTRY_EQ (sms, "timestamp", "110101123456+00");
  TEST_ENTRY_EQ (sms, "text", "hellohello");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}


static void
test_pdu3_8bit (void *f, gpointer d)
{
  static const guint8 pdu[] = {
    0x07, 0x91, 0x21, 0x43, 0x65, 0x87, 0x09, 0xf1,
    0x04, 0x0b, 0x91, 0x81, 0x00, 0x55, 0x15, 0x12,
    0xf2, 0x00, 0x04, 0x11, 0x10, 0x10, 0x21, 0x43,
    0x65, 0x00, 0x0a, 0xe8, 0x32, 0x9b, 0xfd, 0x46,
    0x97, 0xd9, 0xec, 0x37, 0xde};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+12345678901");
  TEST_ENTRY_EQ (sms, "number", "+18005551212");
  TEST_ENTRY_EQ (sms, "timestamp", "110101123456+00");
  TEST_ENTRY_EQ (sms, "text", "\xe8\x32\x9b\xfd\x46\x97\xd9\xec\x37\xde");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}


static void
test_pdu_dcsf1 (void *f, gpointer d)
{
  /* TP-DCS coding scheme is group F */
  static const guint8 pdu[] = {
    0x07,       // length of SMSC info
    0x91,       // type of address of SMSC (E.164)
    0x33, 0x06, 0x09, 0x10, 0x93, 0xF0,  // SMSC address (+33 60 90 01 39 0)
    0x04,       // SMS-DELIVER
    0x04,       // address length
    0x85,       // type of address
    0x81, 0x00, // sender address (1800)
    0x00,       // TP-PID protocol identifier
    0xF1,       // TP-DCS data coding scheme
    0x11, 0x60, 0x42, 0x31, 0x80, 0x51, 0x80,   // timestamp 11-06-24 13:08:51
    0xA0,       // TP-UDL user data length (160)
    // Content:
    0x49,
    0xB7, 0xF9, 0x0D, 0x9A, 0x1A, 0xA5, 0xA0, 0x16,
    0x68, 0xF8, 0x76, 0x9B, 0xD3, 0xE4, 0xB2, 0x9B,
    0x9E, 0x2E, 0xB3, 0x59, 0xA0, 0x3F, 0xC8, 0x5D,
    0x06, 0xA9, 0xC3, 0xED, 0x70, 0x7A, 0x0E, 0xA2,
    0xCB, 0xC3, 0xEE, 0x79, 0xBB, 0x4C, 0xA7, 0xCB,
    0xCB, 0xA0, 0x56, 0x43, 0x61, 0x7D, 0xA7, 0xC7,
    0x69, 0x90, 0xFD, 0x4D, 0x97, 0x97, 0x41, 0xEE,
    0x77, 0xDD, 0x5E, 0x0E, 0xD7, 0x41, 0xED, 0x37,
    0x1D, 0x44, 0x2E, 0x83, 0xE0, 0xE1, 0xF9, 0xBC,
    0x0C, 0xD2, 0x81, 0xE6, 0x77, 0xD9, 0xB8, 0x4C,
    0x06, 0xC1, 0xDF, 0x75, 0x39, 0xE8, 0x5C, 0x90,
    0x97, 0xE5, 0x20, 0xFB, 0x9B, 0x2E, 0x2F, 0x83,
    0xC6, 0xEF, 0x36, 0x9C, 0x5E, 0x06, 0x4D, 0x8D,
    0x52, 0xD0, 0xBC, 0x2E, 0x07, 0xDD, 0xEF, 0x77,
    0xD7, 0xDC, 0x2C, 0x77, 0x99, 0xE5, 0xA0, 0x77,
    0x1D, 0x04, 0x0F, 0xCB, 0x41, 0xF4, 0x02, 0xBB,
    0x00, 0x47, 0xBF, 0xDD, 0x65, 0x50, 0xB8, 0x0E,
    0xCA, 0xD9, 0x66};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+33609001390");
  TEST_ENTRY_EQ (sms, "number", "1800");
  TEST_ENTRY_EQ (sms, "timestamp", "110624130815+02");
  TEST_ENTRY_EQ (sms, "text",
                 "Info SFR - Confidentiel, à ne jamais transmettre -\r\n"
                 "Voici votre nouveau mot de passe : sw2ced pour gérer "
                 "votre compte SFR sur www.sfr.fr ou par téléphone au 963");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}


static void
test_pdu_dcsf_8bit (void *f, gpointer d)
{
  static const guint8 pdu[] = {
    0x07, 0x91, 0x21, 0x43, 0x65, 0x87, 0x09, 0xf1,
    0x04, 0x0b, 0x91, 0x81, 0x00, 0x55, 0x15, 0x12,
    0xf2, 0x00, 0xf4, 0x11, 0x10, 0x10, 0x21, 0x43,
    0x65, 0x00, 0x0a, 0xe8, 0x32, 0x9b, 0xfd, 0x46,
    0x97, 0xd9, 0xec, 0x37, 0xde};
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+12345678901");
  TEST_ENTRY_EQ (sms, "number", "+18005551212");
  TEST_ENTRY_EQ (sms, "timestamp", "110101123456+00");
  TEST_ENTRY_EQ (sms, "text", "\xe8\x32\x9b\xfd\x46\x97\xd9\xec\x37\xde");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}

static void
test_pdu_insufficient_data (void *f, gpointer d)
{
  static const guint8 pdu[] = {
    0x07, 0x91, 0x21, 0x43, 0x65, 0x87, 0x09, 0xf1,
    0x04, 0x0b, 0x91, 0x81, 0x00, 0x55, 0x15, 0x12,
    0xf2, 0x00, 0x00, 0x11, 0x10, 0x10, 0x21, 0x43,
    0x65, 0x00, 0x0b, 0xe8, 0x32, 0x9b, 0xfd, 0x46,
    0x97, 0xd9, 0xec, 0x37
  };
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu, sizeof(pdu));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms == NULL);

  g_free (hexpdu);
}


static void
test_pdu_udhi (void *f, gpointer d)
{
  /* Welcome message from KPN NL */
  static const char *hexpdu =
"07911356131313F64004850120390011609232239180A006080400100201D7327BFD6EB340E232"
"1BF46E83EA7790F59D1E97DBE1341B442F83C465763D3DA797E56537C81D0ECB41AB59CC1693C1"
"6031D96C064241E5656838AF03A96230982A269BCD462917C8FA4E8FCBED709A0D7ABBE9F6B0FB"
"5C7683D27350984D4FABC9A0B33C4C4FCF5D20EBFB2D079DCB62793DBD06D9C36E50FB2D4E97D9"
"A0B49B5E96BBCB";
  GHashTable *sms;
  GError *error;

  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "+31653131316");
  TEST_ENTRY_EQ (sms, "number", "1002");
  TEST_ENTRY_EQ (sms, "timestamp", "110629233219+02");
  TEST_ENTRY_EQ (sms, "text",
                 "Welkom, bel om uw Voicemail te beluisteren naar +31612001233"
                 " (PrePay: *100*1233#). Voicemail ontvangen is altijd gratis."
                 " Voor gebruik van mobiel interne");

  g_hash_table_unref (sms);
}

#if 0
static void
test_pduX (void *f, gpointer d)
{
  GHashTable *sms;
  GError *error;
  char *hexpdu;

  hexpdu = utils_bin2hexstr (pdu1, sizeof(pdu1));
  sms = sms_parse_pdu (hexpdu, &error);
  g_assert (sms);

  TEST_ENTRY_EQ (sms, "smsc", "");
  TEST_ENTRY_EQ (sms, "number", "");
  TEST_ENTRY_EQ (sms, "timestamp", "");
  TEST_ENTRY_EQ (sms, "text",
                "");

  g_free (hexpdu);
  g_hash_table_unref (sms);
}
#endif



#if GLIB_CHECK_VERSION(2,25,12)
typedef GTestFixtureFunc TCFunc;
#else
typedef void (*TCFunc)(void);
#endif

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

int main (int argc, char **argv)
{
    GTestSuite *suite;
    gint result;

    g_type_init ();

    g_test_init (&argc, &argv, NULL);

    suite = g_test_get_root ();

    g_test_suite_add (suite, TESTCASE (test_pdu1, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu2, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu3, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu3_nzpid, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu3_mms, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu3_natl, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu3_8bit, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu_dcsf1, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu_dcsf_8bit, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu_insufficient_data, NULL));
    g_test_suite_add (suite, TESTCASE (test_pdu_udhi, NULL));

    result = g_test_run ();

    return result;
}
