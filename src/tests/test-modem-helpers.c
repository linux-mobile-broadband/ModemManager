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
 */

#include <glib.h>
#include <string.h>

#include "mm-modem-helpers.h"

typedef struct {
    GPtrArray *solicited_creg;
    GPtrArray *unsolicited_creg;
} TestData;

#define MM_SCAN_TAG_STATUS "status"
#define MM_SCAN_TAG_OPER_LONG "operator-long"
#define MM_SCAN_TAG_OPER_SHORT "operator-short"
#define MM_SCAN_TAG_OPER_NUM "operator-num"
#define MM_SCAN_TAG_ACCESS_TECH "access-tech"

typedef struct {
    const char *status;
    const char *oper_long;
    const char *oper_short;
    const char *oper_num;
    const char *tech;
} OperEntry;

#define ARRAY_LEN(i) (sizeof (i) / sizeof (i[0]))

static void
test_cops_results (const char *desc,
                   const char *reply,
                   OperEntry *expected_results,
                   guint32 expected_results_len)
{
    guint i;
    GError *error = NULL;
    GPtrArray *results;

    g_print ("\nTesting %s +COPS response...\n", desc);

    results = mm_gsm_parse_scan_response (reply, &error);
    g_assert (results);
    g_assert (error == NULL);

    g_assert (results->len == expected_results_len);

    for (i = 0; i < results->len; i++) {
        GHashTable *entry = g_ptr_array_index (results, i);
        const char *value;
        OperEntry *expected = &expected_results[i];

        value = g_hash_table_lookup (entry, MM_SCAN_TAG_STATUS);
        g_assert (value);
        g_assert (strcmp (value, expected->status) == 0);

        value = g_hash_table_lookup (entry, MM_SCAN_TAG_OPER_LONG);
        if (expected->oper_long) {
            g_assert (value);
            g_assert (strcmp (value, expected->oper_long) == 0);
        } else
            g_assert (value == NULL);

        value = g_hash_table_lookup (entry, MM_SCAN_TAG_OPER_SHORT);
        if (expected->oper_short) {
            g_assert (value);
            g_assert (strcmp (value, expected->oper_short) == 0);
        } else
            g_assert (value == NULL);

        value = g_hash_table_lookup (entry, MM_SCAN_TAG_OPER_NUM);
        g_assert (expected->oper_num);
        g_assert (value);
        g_assert (strcmp (value, expected->oper_num) == 0);

        value = g_hash_table_lookup (entry, MM_SCAN_TAG_ACCESS_TECH);
        if (expected->tech) {
            g_assert (value);
            g_assert (strcmp (value, expected->tech) == 0);
        } else
            g_assert (value == NULL);
    }

    mm_gsm_destroy_scan_data (results);
}

static void
test_cops_response_tm506 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"\",\"T-Mobile\",\"31026\",0),(2,\"T - Mobile\",\"T - Mobile\",\"310260\"),2),(1,\"AT&T\",\"AT&T\",\"310410\"),0)";
    static OperEntry expected[] = {
        { "2", NULL, "T-Mobile", "31026", "0" },
        { "2", "T - Mobile", "T - Mobile", "310260", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" }
    };

    test_cops_results ("TM-506", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_gt3gplus (void *f, gpointer d)
{
    const char *reply = "+COPS: (1,\"T-Mobile US\",\"TMO US\",\"31026\",0),(1,\"Cingular\",\"Cingular\",\"310410\",0),,(0, 1, 3),(0-2)";
    static OperEntry expected[] = {
        { "1", "T-Mobile US", "TMO US", "31026", "0" },
        { "1", "Cingular", "Cingular", "310410", "0" },
    };

    test_cops_results ("GlobeTrotter 3G+ (nozomi)", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_ac881 (void *f, gpointer d)
{
    const char *reply = "+COPS: (1,\"T-Mobile\",\"TMO\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",2),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),)";
    static OperEntry expected[] = {
        { "1", "T-Mobile", "TMO", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_cops_results ("Sierra AirCard 881", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_gtmax36 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile US\",\"TMO US\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",2),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0, 1,)";
    static OperEntry expected[] = {
        { "2", "T-Mobile US", "TMO US", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_cops_results ("Option GlobeTrotter MAX 3.6", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_ac860 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"TMO\",\"31026\",0),(1,\"Cingular\",\"Cinglr\",\"310410\",2),(1,\"Cingular\",\"Cinglr\",\"310410\",0),,)";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "TMO", "31026", "0" },
        { "1", "Cingular", "Cinglr", "310410", "2" },
        { "1", "Cingular", "Cinglr", "310410", "0" },
    };

    test_cops_results ("Sierra AirCard 860", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_gtm378 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"T-Mobile\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",2),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0, 1, 3),(0-2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "T-Mobile", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_cops_results ("Option GTM378", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_motoc (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"\",\"310260\"),(0,\"Cingular Wireless\",\"\",\"310410\")";
    static OperEntry expected[] = {
        { "2", "T-Mobile", NULL, "310260", NULL },
        { "0", "Cingular Wireless", NULL, "310410", NULL },
    };

    test_cops_results ("BUSlink SCWi275u (Motorola C-series)", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_mf627a (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"AT&T@\",\"AT&TD\",\"310410\",0),(3,\"Voicestream Wireless Corporation\",\"VSTREAM\",\"31026\",0),";
    static OperEntry expected[] = {
        { "2", "AT&T@", "AT&TD", "310410", "0" },
        { "3", "Voicestream Wireless Corporation", "VSTREAM", "31026", "0" },
    };

    test_cops_results ("ZTE MF627 (A)", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_mf627b (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"AT&Tp\",\"AT&T@\",\"310410\",0),(3,\"\",\"\",\"31026\",0),";
    static OperEntry expected[] = {
        { "2", "AT&Tp", "AT&T@", "310410", "0" },
        { "3", NULL, NULL, "31026", "0" },
    };

    test_cops_results ("ZTE MF627 (B)", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_e160g (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"TMO\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "TMO", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_cops_results ("Huawei E160G", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_mercury (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"\",\"\",\"310410\",2),(1,\"AT&T\",\"AT&T\",\"310410\",0),(1,\"T-Mobile\",\"TMO\",\"31026\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "2", NULL, NULL, "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" },
        { "1", "T-Mobile", "TMO", "31026", "0" },
    };

    test_cops_results ("Sierra AT&T USBConnect Mercury", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_quicksilver (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"AT&T\",\"\",\"310410\",0),(2,\"\",\"\",\"3104100\",2),(1,\"AT&T\",\"\",\"310260\",0),,(0-4),(0-2)";
    static OperEntry expected[] = {
        { "2", "AT&T", NULL, "310410", "0" },
        { "2", NULL, NULL, "3104100", "2" },
        { "1", "AT&T", NULL, "310260", "0" },
    };

    test_cops_results ("Option AT&T Quicksilver", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_icon225 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile US\",\"TMO US\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0, 1, 3),(0-2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile US", "TMO US", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_cops_results ("Option iCON 225", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_icon452 (void *f, gpointer d)
{
    const char *reply = "+COPS: (1,\"T-Mobile US\",\"TMO US\",\"31026\",0),(2,\"T-Mobile\",\"T-Mobile\",\"310260\",2),(1,\"AT&T\",\"AT&T\",\"310410\",2),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "1", "T-Mobile US", "TMO US", "31026", "0" },
        { "2", "T-Mobile", "T-Mobile", "310260", "2" },
        { "1", "AT&T", "AT&T", "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" }
    };

    test_cops_results ("Option iCON 452", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_f3507g (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T - Mobile\",\"T - Mobile\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",0),(1,\"AT&T\",\"AT&T\",\"310410\",2)";
    static OperEntry expected[] = {
        { "2", "T - Mobile", "T - Mobile", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "0" },
        { "1", "AT&T", "AT&T", "310410", "2" }
    };

    test_cops_results ("Ericsson F3507g", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_f3607gw (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T - Mobile\",\"T - Mobile\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\"),2),(1,\"AT&T\",\"AT&T\",\"310410\"),0)";
    static OperEntry expected[] = {
        { "2", "T - Mobile", "T - Mobile", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" }
    };

    test_cops_results ("Ericsson F3607gw", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_mc8775 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"T-Mobile\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",2),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "T-Mobile", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" }
    };

    test_cops_results ("Sierra MC8775", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_n80 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T - Mobile\",,\"31026\"),(1,\"Einstein PCS\",,\"31064\"),(1,\"Cingular\",,\"31041\"),,(0,1,3),(0,2)";
    static OperEntry expected[] = {
        { "2", "T - Mobile", NULL, "31026", NULL },
        { "1", "Einstein PCS", NULL, "31064", NULL },
        { "1", "Cingular", NULL, "31041", NULL },
    };

    test_cops_results ("Nokia N80", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_e1550 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"TMO\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "TMO", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_cops_results ("Huawei E1550", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_mf622 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"T-Mobile\",\"31026\",0),(1,\"\",\"\",\"310410\",0),";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "T-Mobile", "31026", "0" },
        { "1", NULL, NULL, "310410", "0" },
    };

    test_cops_results ("ZTE MF622", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_e226 (void *f, gpointer d)
{
    const char *reply = "+COPS: (1,\"\",\"\",\"31026\",0),(1,\"\",\"\",\"310410\",2),(1,\"\",\"\",\"310410\",0),,(0,1,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "1", NULL, NULL, "31026", "0" },
        { "1", NULL, NULL, "310410", "2" },
        { "1", NULL, NULL, "310410", "0" },
    };

    test_cops_results ("Huawei E226", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_xu870 (void *f, gpointer d)
{
    const char *reply = "+COPS: (0,\"AT&T MicroCell\",\"AT&T MicroCell\",\"310410\",2)\r\n+COPS: (1,\"AT&T MicroCell\",\"AT&T MicroCell\",\"310410\",0)\r\n+COPS: (1,\"T-Mobile\",\"TMO\",\"31026\",0)\r\n";
    static OperEntry expected[] = {
        { "0", "AT&T MicroCell", "AT&T MicroCell", "310410", "2" },
        { "1", "AT&T MicroCell", "AT&T MicroCell", "310410", "0" },
        { "1", "T-Mobile", "TMO", "31026", "0" },
    };

    test_cops_results ("Novatel XU870", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_gtultraexpress (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile US\",\"TMO US\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",2),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile US", "TMO US", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_cops_results ("Option GlobeTrotter Ultra Express", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_n2720 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T - Mobile\",,\"31026\",0),\r\n(1,\"AT&T\",,\"310410\",0),,(0,1,3),(0,2)";
    static OperEntry expected[] = {
        { "2", "T - Mobile", NULL, "31026", "0" },
        { "1", "AT&T", NULL, "310410", "0" },
    };

    test_cops_results ("Nokia 2720", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_gobi (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"T-Mobile\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",2),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "T-Mobile", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "2" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_cops_results ("Qualcomm Gobi", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_sek600i (void *f, gpointer d)
{
    /* Phone is stupid enough to support 3G but not report cell technology,
     * mixing together 2G and 3G cells without any way of distinguishing
     * which is which...
     */
    const char *reply = "+COPS: (2,\"blau\",\"\",\"26203\"),(2,\"blau\",\"\",\"26203\"),(3,\"\",\"\",\"26201\"),(3,\"\",\"\",\"26202\"),(3,\"\",\"\",\"26207\"),(3,\"\",\"\",\"26201\"),(3,\"\",\"\",\"26207\")";
    static OperEntry expected[] = {
        { "2", "blau", NULL, "26203", NULL },
        { "2", "blau", NULL, "26203", NULL },
        { "3", NULL, NULL, "26201", NULL },
        { "3", NULL, NULL, "26202", NULL },
        { "3", NULL, NULL, "26207", NULL },
        { "3", NULL, NULL, "26201", NULL },
        { "3", NULL, NULL, "26207", NULL },
    };

    test_cops_results ("Sony-Ericsson K600i", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_gsm_invalid (void *f, gpointer d)
{
    const char *reply = "+COPS: (0,1,2,3),(1,2,3,4)";
    GPtrArray *results;
    GError *error = NULL;

    results = mm_gsm_parse_scan_response (reply, &error);
    g_assert (results != NULL);
    g_assert (error == NULL);
}

static void
test_cops_response_umts_invalid (void *f, gpointer d)
{
    const char *reply = "+COPS: (0,1,2,3,4),(1,2,3,4,5)";
    GPtrArray *results;
    GError *error = NULL;

    results = mm_gsm_parse_scan_response (reply, &error);
    g_assert (results != NULL);
    g_assert (error == NULL);
}

typedef struct {
    guint32 state;
    gulong lac;
    gulong ci;
    gint act;

    guint regex_num;
    gboolean cgreg;
} CregResult;

static void
test_creg_match (const char *test,
                 gboolean solicited,
                 const char *reply,
                 TestData *data,
                 const CregResult *result)
{
    int i;
    GMatchInfo *info  = NULL;
    guint32 state = 0;
    gulong lac = 0, ci = 0;
    gint access_tech = -1;
    GError *error = NULL;
    gboolean success, cgreg = FALSE;
    guint regex_num = 0;
    GPtrArray *array;

    g_assert (reply);
    g_assert (test);
    g_assert (data);
    g_assert (result);

    g_print ("\nTesting %s +CREG %s response...\n",
             test,
             solicited ? "solicited" : "unsolicited");

    array = solicited ? data->solicited_creg : data->unsolicited_creg;
    for (i = 0; i < array->len; i++) {
        GRegex *r = g_ptr_array_index (array, i);

        if (g_regex_match (r, reply, 0, &info)) {
            regex_num = i + 1;
            break;
        }
        g_match_info_free (info);
        info = NULL;
    }

    g_assert (info != NULL);
    g_assert (regex_num == result->regex_num);

    success = mm_gsm_parse_creg_response (info, &state, &lac, &ci, &access_tech, &cgreg, &error);
    g_assert (success);
    g_assert (error == NULL);
    g_assert (state == result->state);
    g_assert (lac == result->lac);
    g_assert (ci == result->ci);
    g_assert (access_tech == result->act);
    g_assert (cgreg == result->cgreg);
}

static void
test_creg1_solicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "+CREG: 1,3";
    const CregResult result = { 3, 0, 0, -1 , 2, FALSE};

    test_creg_match ("CREG=1", TRUE, reply, data, &result);
}

static void
test_creg1_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CREG: 3\r\n";
    const CregResult result = { 3, 0, 0, -1 , 1, FALSE};

    test_creg_match ("CREG=1", FALSE, reply, data, &result);
}

static void
test_creg2_mercury_solicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "+CREG: 0,1,84CD,00D30173";
    const CregResult result = { 1, 0x84cd, 0xd30173, -1 , 4, FALSE};

    test_creg_match ("Sierra Mercury CREG=2", TRUE, reply, data, &result);
}

static void
test_creg2_mercury_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CREG: 1,84CD,00D30156\r\n";
    const CregResult result = { 1, 0x84cd, 0xd30156, -1 , 3, FALSE};

    test_creg_match ("Sierra Mercury CREG=2", FALSE, reply, data, &result);
}

static void
test_creg2_sek850i_solicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "+CREG: 2,1,\"CE00\",\"01CEAD8F\"";
    const CregResult result = { 1, 0xce00, 0x01cead8f, -1 , 4, FALSE};

    test_creg_match ("Sony Ericsson K850i CREG=2", TRUE, reply, data, &result);
}

static void
test_creg2_sek850i_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CREG: 1,\"CE00\",\"00005449\"\r\n";
    const CregResult result = { 1, 0xce00, 0x5449, -1 , 3, FALSE};

    test_creg_match ("Sony Ericsson K850i CREG=2", FALSE, reply, data, &result);
}

static void
test_creg2_e160g_solicited_unregistered (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "+CREG: 2,0,00,0";
    const CregResult result = { 0, 0, 0, -1 , 4, FALSE};

    test_creg_match ("Huawei E160G unregistered CREG=2", TRUE, reply, data, &result);
}

static void
test_creg2_e160g_solicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "+CREG: 2,1,8BE3,2BAF";
    const CregResult result = { 1, 0x8be3, 0x2baf, -1 , 4, FALSE};

    test_creg_match ("Huawei E160G CREG=2", TRUE, reply, data, &result);
}

static void
test_creg2_e160g_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CREG: 2,8BE3,2BAF\r\n";
    const CregResult result = { 2, 0x8be3, 0x2baf, -1 , 3, FALSE};

    test_creg_match ("Huawei E160G CREG=2", FALSE, reply, data, &result);
}

static void
test_creg2_tm506_solicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "+CREG: 2,1,\"8BE3\",\"00002BAF\"";
    const CregResult result = { 1, 0x8BE3, 0x2BAF, -1 , 4, FALSE};

    /* Test leading zeros in the CI */
    test_creg_match ("Sony Ericsson TM-506 CREG=2", TRUE, reply, data, &result);
}

static void
test_creg2_xu870_unsolicited_unregistered (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CREG: 2,,\r\n";
    const CregResult result = { 2, 0, 0, -1 , 3, FALSE};

    test_creg_match ("Novatel XU870 unregistered CREG=2", FALSE, reply, data, &result);
}

static void
test_cgreg1_solicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "+CGREG: 1,3";
    const CregResult result = { 3, 0, 0, -1 , 2, TRUE};

    test_creg_match ("CGREG=1", TRUE, reply, data, &result);
}

static void
test_cgreg1_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CGREG: 3\r\n";
    const CregResult result = { 3, 0, 0, -1 , 1, TRUE};

    test_creg_match ("CGREG=1", FALSE, reply, data, &result);
}

static void
test_cgreg2_f3607gw_solicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "+CGREG: 2,1,\"8BE3\",\"00002B5D\",3";
    const CregResult result = { 1, 0x8BE3, 0x2B5D, 3 , 6, TRUE};

    test_creg_match ("Ericsson F3607gw CGREG=2", TRUE, reply, data, &result);
}

static void
test_cgreg2_f3607gw_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CGREG: 1,\"8BE3\",\"00002B5D\",3\r\n";
    const CregResult result = { 1, 0x8BE3, 0x2B5D, 3 , 5, TRUE};

    test_creg_match ("Ericsson F3607gw CGREG=2", FALSE, reply, data, &result);
}

static void
test_creg2_md400_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CREG: 2,5,\"0502\",\"0404736D\"\r\n";
    const CregResult result = { 5, 0x0502, 0x0404736D, -1 , 4, FALSE};

    test_creg_match ("Sony-Ericsson MD400 CREG=2", FALSE, reply, data, &result);
}

static void
test_cgreg2_md400_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CGREG: 5,\"0502\",\"0404736D\",2\r\n";
    const CregResult result = { 5, 0x0502, 0x0404736D, 2, 5, TRUE};

    test_creg_match ("Sony-Ericsson MD400 CGREG=2", FALSE, reply, data, &result);
}

static void
test_creg_cgreg_multi_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CREG: 5\r\n\r\n+CGREG: 0\r\n";
    const CregResult result = { 5, 0, 0, -1, 1, FALSE};

    test_creg_match ("Multi CREG/CGREG", FALSE, reply, data, &result);
}

static void
test_creg_cgreg_multi2_unsolicited (void *f, gpointer d)
{
    TestData *data = (TestData *) d;
    const char *reply = "\r\n+CGREG: 0\r\n\r\n+CREG: 5\r\n";
    const CregResult result = { 0, 0, 0, -1, 1, TRUE};

    test_creg_match ("Multi CREG/CGREG #2", FALSE, reply, data, &result);
}

static void
test_cscs_icon225_support_response (void *f, gpointer d)
{
    const char *reply = "\r\n+CSCS: (\"IRA\",\"GSM\",\"UCS2\")\r\n";
    MMModemCharset charsets = MM_MODEM_CHARSET_UNKNOWN;
    gboolean success;

    success = mm_gsm_parse_cscs_support_response (reply, &charsets);
    g_assert (success);

    g_assert (charsets == (MM_MODEM_CHARSET_IRA |
                           MM_MODEM_CHARSET_GSM |
                           MM_MODEM_CHARSET_UCS2));
}

static void
test_cscs_sierra_mercury_support_response (void *f, gpointer d)
{
    const char *reply = "\r\n+CSCS: (\"IRA\",\"GSM\",\"UCS2\",\"PCCP437\")\r\n";
    MMModemCharset charsets = MM_MODEM_CHARSET_UNKNOWN;
    gboolean success;

    success = mm_gsm_parse_cscs_support_response (reply, &charsets);
    g_assert (success);

    g_assert (charsets == (MM_MODEM_CHARSET_IRA |
                           MM_MODEM_CHARSET_GSM |
                           MM_MODEM_CHARSET_UCS2 |
                           MM_MODEM_CHARSET_PCCP437));
}

static void
test_cscs_buslink_support_response (void *f, gpointer d)
{
    const char *reply = "\r\n+CSCS: (\"8859-1\",\"ASCII\",\"GSM\",\"UCS2\",\"UTF8\")\r\n";
    MMModemCharset charsets = MM_MODEM_CHARSET_UNKNOWN;
    gboolean success;

    success = mm_gsm_parse_cscs_support_response (reply, &charsets);
    g_assert (success);

    g_assert (charsets == (MM_MODEM_CHARSET_8859_1 |
                           MM_MODEM_CHARSET_IRA |
                           MM_MODEM_CHARSET_GSM |
                           MM_MODEM_CHARSET_UCS2 |
                           MM_MODEM_CHARSET_UTF8));
}

static void
test_cscs_blackberry_support_response (void *f, gpointer d)
{
    const char *reply = "\r\n+CSCS: \"IRA\"\r\n";
    MMModemCharset charsets = MM_MODEM_CHARSET_UNKNOWN;
    gboolean success;

    success = mm_gsm_parse_cscs_support_response (reply, &charsets);
    g_assert (success);

    g_assert (charsets == MM_MODEM_CHARSET_IRA);
}

static TestData *
test_data_new (void)
{
    TestData *data;

    data = g_malloc0 (sizeof (TestData));
    data->solicited_creg = mm_gsm_creg_regex_get (TRUE);
    data->unsolicited_creg = mm_gsm_creg_regex_get (FALSE);
    return data;
}

static void
test_data_free (TestData *data)
{
    mm_gsm_creg_regex_destroy (data->solicited_creg);
    mm_gsm_creg_regex_destroy (data->unsolicited_creg);
    g_free (data);
}


typedef void (*TCFunc)(void);

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

int main (int argc, char **argv)
{
	GTestSuite *suite;
    TestData *data;
    gint result;

	g_test_init (&argc, &argv, NULL);

	suite = g_test_get_root ();
    data = test_data_new ();

	g_test_suite_add (suite, TESTCASE (test_cops_response_tm506, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_gt3gplus, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_ac881, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_gtmax36, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_ac860, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_gtm378, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_motoc, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_mf627a, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_mf627b, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_e160g, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_mercury, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_quicksilver, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_icon225, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_icon452, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_f3507g, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_f3607gw, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_mc8775, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_n80, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_e1550, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_mf622, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_e226, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_xu870, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_gtultraexpress, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_n2720, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_gobi, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_sek600i, NULL));

	g_test_suite_add (suite, TESTCASE (test_cops_response_gsm_invalid, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_umts_invalid, NULL));

    g_test_suite_add (suite, TESTCASE (test_creg1_solicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg1_unsolicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_mercury_solicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_mercury_unsolicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_sek850i_solicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_sek850i_unsolicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_e160g_solicited_unregistered, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_e160g_solicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_e160g_unsolicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_tm506_solicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_xu870_unsolicited_unregistered, data));
    g_test_suite_add (suite, TESTCASE (test_creg2_md400_unsolicited, data));

    g_test_suite_add (suite, TESTCASE (test_cgreg1_solicited, data));
    g_test_suite_add (suite, TESTCASE (test_cgreg1_unsolicited, data));
    g_test_suite_add (suite, TESTCASE (test_cgreg2_f3607gw_solicited, data));
    g_test_suite_add (suite, TESTCASE (test_cgreg2_f3607gw_unsolicited, data));
    g_test_suite_add (suite, TESTCASE (test_cgreg2_md400_unsolicited, data));

    g_test_suite_add (suite, TESTCASE (test_creg_cgreg_multi_unsolicited, data));
    g_test_suite_add (suite, TESTCASE (test_creg_cgreg_multi2_unsolicited, data));

    g_test_suite_add (suite, TESTCASE (test_cscs_icon225_support_response, data));
    g_test_suite_add (suite, TESTCASE (test_cscs_sierra_mercury_support_response, data));
    g_test_suite_add (suite, TESTCASE (test_cscs_buslink_support_response, data));
    g_test_suite_add (suite, TESTCASE (test_cscs_blackberry_support_response, data));

    result = g_test_run ();

    test_data_free (data);

    return result;
}

