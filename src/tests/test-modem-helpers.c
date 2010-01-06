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
test_results (const char *desc,
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

    test_results ("TM-506", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_gt3gplus (void *f, gpointer d)
{
    const char *reply = "+COPS: (1,\"T-Mobile US\",\"TMO US\",\"31026\",0),(1,\"Cingular\",\"Cingular\",\"310410\",0),,(0, 1, 3),(0-2)";
    static OperEntry expected[] = {
        { "1", "T-Mobile US", "TMO US", "31026", "0" },
        { "1", "Cingular", "Cingular", "310410", "0" },
    };

    test_results ("GlobeTrotter 3G+ (nozomi)", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Sierra AirCard 881", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Option GlobeTrotter MAX 3.6", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Sierra AirCard 860", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Option GTM378", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_motoc (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"\",\"310260\"),(0,\"Cingular Wireless\",\"\",\"310410\")";
    static OperEntry expected[] = {
        { "2", "T-Mobile", NULL, "310260", NULL },
        { "0", "Cingular Wireless", NULL, "310410", NULL },
    };

    test_results ("BUSlink SCWi275u (Motorola C-series)", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_mf627a (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"AT&T@\",\"AT&TD\",\"310410\",0),(3,\"Voicestream Wireless Corporation\",\"VSTREAM\",\"31026\",0),";
    static OperEntry expected[] = {
        { "2", "AT&T@", "AT&TD", "310410", "0" },
        { "3", "Voicestream Wireless Corporation", "VSTREAM", "31026", "0" },
    };

    test_results ("ZTE MF627 (A)", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_mf627b (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"AT&Tp\",\"AT&T@\",\"310410\",0),(3,\"\",\"\",\"31026\",0),";
    static OperEntry expected[] = {
        { "2", "AT&Tp", "AT&T@", "310410", "0" },
        { "3", NULL, NULL, "31026", "0" },
    };

    test_results ("ZTE MF627 (B)", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_e160g (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"TMO\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "TMO", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_results ("Huawei E160G", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Sierra AT&T USBConnect Mercury", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Option AT&T Quicksilver", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_icon225 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile US\",\"TMO US\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0, 1, 3),(0-2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile US", "TMO US", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_results ("Option iCON 225", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Option iCON 452", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Ericsson F3507g", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Ericsson F3607gw", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Sierra MC8775", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Nokia N80", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_e1550 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"TMO\",\"31026\",0),(1,\"AT&T\",\"AT&T\",\"310410\",0),,(0,1,2,3,4),(0,1,2)";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "TMO", "31026", "0" },
        { "1", "AT&T", "AT&T", "310410", "0" },
    };

    test_results ("Huawei E1550", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_mf622 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T-Mobile\",\"T-Mobile\",\"31026\",0),(1,\"\",\"\",\"310410\",0),";
    static OperEntry expected[] = {
        { "2", "T-Mobile", "T-Mobile", "31026", "0" },
        { "1", NULL, NULL, "310410", "0" },
    };

    test_results ("ZTE MF622", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Huawei E226", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Novatel XU870", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Option GlobeTrotter Ultra Express", reply, &expected[0], ARRAY_LEN (expected));
}

static void
test_cops_response_n2720 (void *f, gpointer d)
{
    const char *reply = "+COPS: (2,\"T - Mobile\",,\"31026\",0),\r\n(1,\"AT&T\",,\"310410\",0),,(0,1,3),(0,2)";
    static OperEntry expected[] = {
        { "2", "T - Mobile", NULL, "31026", "0" },
        { "1", "AT&T", NULL, "310410", "0" },
    };

    test_results ("Nokia 2720", reply, &expected[0], ARRAY_LEN (expected));
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

    test_results ("Qualcomm Gobi", reply, &expected[0], ARRAY_LEN (expected));
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


typedef void (*TCFunc)(void);

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

int main (int argc, char **argv)
{
	GTestSuite *suite;

	g_test_init (&argc, &argv, NULL);

	suite = g_test_get_root ();

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

	g_test_suite_add (suite, TESTCASE (test_cops_response_gsm_invalid, NULL));
	g_test_suite_add (suite, TESTCASE (test_cops_response_umts_invalid, NULL));

	return g_test_run ();
}

