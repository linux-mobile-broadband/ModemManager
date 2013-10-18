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
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>

#include "mm-modem-helpers-huawei.h"

/*****************************************************************************/
/* Test ^NDISSTAT / ^NDISSTATQRY responses */

typedef struct {
    const gchar *str;
    gboolean expected_ipv4_available;
    gboolean expected_ipv4_connected;
    gboolean expected_ipv6_available;
    gboolean expected_ipv6_connected;
} NdisstatqryTest;

static const NdisstatqryTest ndisstatqry_tests[] = {
    { "^NDISSTAT: 1,,,IPV4\r\n", TRUE,  TRUE,  FALSE, FALSE },
    { "^NDISSTAT: 0,,,IPV4\r\n", TRUE,  FALSE, FALSE, FALSE },
    { "^NDISSTAT: 1,,,IPV6\r\n", FALSE, FALSE, TRUE,  TRUE  },
    { "^NDISSTAT: 0,,,IPV6\r\n", FALSE, FALSE, TRUE,  FALSE },
    { "^NDISSTAT: 1,,,IPV4\r\n"
      "^NDISSTAT: 1,,,IPV6\r\n", TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTAT: 1,,,IPV4\r\n"
      "^NDISSTAT: 0,,,IPV6\r\n", TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTAT: 0,,,IPV4\r\n"
      "^NDISSTAT: 1,,,IPV6\r\n", TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTAT: 0,,,IPV4\r\n"
      "^NDISSTAT: 0,,,IPV6\r\n", TRUE,  FALSE, TRUE,  FALSE },
    { "^NDISSTAT: 1,,,IPV4",     TRUE,  TRUE,  FALSE, FALSE },
    { "^NDISSTAT: 0,,,IPV4",     TRUE,  FALSE, FALSE, FALSE },
    { "^NDISSTAT: 1,,,IPV6",     FALSE, FALSE, TRUE,  TRUE  },
    { "^NDISSTAT: 0,,,IPV6",     FALSE, FALSE, TRUE,  FALSE },
    { "^NDISSTAT: 1,,,IPV4\r\n"
      "^NDISSTAT: 1,,,IPV6",     TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTAT: 1,,,IPV4\r\n"
      "^NDISSTAT: 0,,,IPV6",     TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTAT: 0,,,IPV4\r\n"
      "^NDISSTAT: 1,,,IPV6",     TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTAT: 0,,,IPV4\r\n"
      "^NDISSTAT: 0,,,IPV6",     TRUE,  FALSE, TRUE,  FALSE },
    { "^NDISSTAT: 1,,,\"IPV4\",1,,,\"IPV6\"",     TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTAT: 1,,,\"IPV4\",0,,,\"IPV6\"",     TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTAT: 0,,,\"IPV4\",1,,,\"IPV6\"",     TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTAT: 0,,,\"IPV4\",0,,,\"IPV6\"",     TRUE,  FALSE, TRUE,  FALSE },
    { "^NDISSTAT: 1,,,\"IPV4\",1,,,\"IPV6\"\r\n", TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTAT: 1,,,\"IPV4\",0,,,\"IPV6\"\r\n", TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTAT: 0,,,\"IPV4\",1,,,\"IPV6\"\r\n", TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTAT: 0,,,\"IPV4\",0,,,\"IPV6\"\r\n", TRUE,  FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,IPV4\r\n", TRUE,  TRUE,  FALSE, FALSE },
    { "^NDISSTATQRY: 0,,,IPV4\r\n", TRUE,  FALSE, FALSE, FALSE },
    { "^NDISSTATQRY: 1,,,IPV6\r\n", FALSE, FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,IPV6\r\n", FALSE, FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,IPV4\r\n"
      "^NDISSTATQRY: 1,,,IPV6\r\n", TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTATQRY: 1,,,IPV4\r\n"
      "^NDISSTATQRY: 0,,,IPV6\r\n", TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTATQRY: 0,,,IPV4\r\n"
      "^NDISSTATQRY: 1,,,IPV6\r\n", TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,IPV4\r\n"
      "^NDISSTATQRY: 0,,,IPV6\r\n", TRUE,  FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,IPV4",     TRUE,  TRUE,  FALSE, FALSE },
    { "^NDISSTATQRY: 0,,,IPV4",     TRUE,  FALSE, FALSE, FALSE },
    { "^NDISSTATQRY: 1,,,IPV6",     FALSE, FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,IPV6",     FALSE, FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,IPV4\r\n"
      "^NDISSTATQRY: 1,,,IPV6",     TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTATQRY: 1,,,IPV4\r\n"
      "^NDISSTATQRY: 0,,,IPV6",     TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTATQRY: 0,,,IPV4\r\n"
      "^NDISSTATQRY: 1,,,IPV6",     TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,IPV4\r\n"
      "^NDISSTATQRY: 0,,,IPV6",     TRUE,  FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,\"IPV4\",1,,,\"IPV6\"",     TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTATQRY: 1,,,\"IPV4\",0,,,\"IPV6\"",     TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTATQRY: 0,,,\"IPV4\",1,,,\"IPV6\"",     TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,\"IPV4\",0,,,\"IPV6\"",     TRUE,  FALSE, TRUE,  FALSE },
    { "^NDISSTATQRY: 1,,,\"IPV4\",1,,,\"IPV6\"\r\n", TRUE,  TRUE,  TRUE,  TRUE  },
    { "^NDISSTATQRY: 1,,,\"IPV4\",0,,,\"IPV6\"\r\n", TRUE,  TRUE,  TRUE,  FALSE },
    { "^NDISSTATQRY: 0,,,\"IPV4\",1,,,\"IPV6\"\r\n", TRUE,  FALSE, TRUE,  TRUE  },
    { "^NDISSTATQRY: 0,,,\"IPV4\",0,,,\"IPV6\"\r\n", TRUE,  FALSE, TRUE,  FALSE },
    { NULL, FALSE, FALSE, FALSE, FALSE }
};

static void
test_ndisstatqry (void)
{
    guint i;

    for (i = 0; ndisstatqry_tests[i].str; i++) {
        GError *error = NULL;
        gboolean ipv4_available;
        gboolean ipv4_connected;
        gboolean ipv6_available;
        gboolean ipv6_connected;

        g_assert (mm_huawei_parse_ndisstatqry_response (
                      ndisstatqry_tests[i].str,
                      &ipv4_available,
                      &ipv4_connected,
                      &ipv6_available,
                      &ipv6_connected,
                      &error) == TRUE);
        g_assert_no_error (error);

        g_assert (ipv4_available == ndisstatqry_tests[i].expected_ipv4_available);
        if (ipv4_available)
            g_assert (ipv4_connected == ndisstatqry_tests[i].expected_ipv4_connected);
        g_assert (ipv6_available == ndisstatqry_tests[i].expected_ipv6_available);
        if (ipv6_available)
            g_assert (ipv6_connected == ndisstatqry_tests[i].expected_ipv6_connected);
    }
}

/*****************************************************************************/
/* Test ^SYSINFO responses */

typedef struct {
    const gchar *str;
    guint expected_srv_status;
    guint expected_srv_domain;
    guint expected_roam_status;
    guint expected_sys_mode;
    guint expected_sim_state;
    gboolean expected_sys_submode_valid;
    guint expected_sys_submode;
} SysinfoTest;

static const SysinfoTest sysinfo_tests[] = {
    { "^SYSINFO:2,4,5,3,1",      2, 4, 5, 3, 1, FALSE, 0 },
    { "^SYSINFO:2,4,5,3,1,",     2, 4, 5, 3, 1, FALSE, 0 },
    { "^SYSINFO:2,4,5,3,1,,",    2, 4, 5, 3, 1, FALSE, 0 },
    { "^SYSINFO:2,4,5,3,1,6",    2, 4, 5, 3, 1, FALSE, 6 },
    { "^SYSINFO:2,4,5,3,1,6,",   2, 4, 5, 3, 1, FALSE, 6 },
    { "^SYSINFO:2,4,5,3,1,,6",   2, 4, 5, 3, 1, TRUE,  6 },
    { "^SYSINFO:2,4,5,3,1,0,6",  2, 4, 5, 3, 1, TRUE,  6 },
    { "^SYSINFO: 2,4,5,3,1,0,6", 2, 4, 5, 3, 1, TRUE,  6 },
    { NULL,                      0, 0, 0, 0, 0, FALSE, 0 }
};

static void
test_sysinfo (void)
{
    guint i;

    for (i = 0; sysinfo_tests[i].str; i++) {
        GError *error = NULL;
        guint srv_status = 0;
        guint srv_domain = 0;
        guint roam_status = 0;
        guint sys_mode = 0;
        guint sim_state = 0;
        gboolean sys_submode_valid = FALSE;
        guint sys_submode = 0;

        g_assert (mm_huawei_parse_sysinfo_response (sysinfo_tests[i].str,
                                                    &srv_status,
                                                    &srv_domain,
                                                    &roam_status,
                                                    &sys_mode,
                                                    &sim_state,
                                                    &sys_submode_valid,
                                                    &sys_submode,
                                                    &error) == TRUE);
        g_assert_no_error (error);

        g_assert (srv_status == sysinfo_tests[i].expected_srv_status);
        g_assert (srv_domain == sysinfo_tests[i].expected_srv_domain);
        g_assert (roam_status == sysinfo_tests[i].expected_roam_status);
        g_assert (sys_mode == sysinfo_tests[i].expected_sys_mode);
        g_assert (sim_state == sysinfo_tests[i].expected_sim_state);
        g_assert (sys_submode_valid == sysinfo_tests[i].expected_sys_submode_valid);
        if (sys_submode_valid)
            g_assert (sys_submode == sysinfo_tests[i].expected_sys_submode);
    }
}

/*****************************************************************************/
/* Test ^SYSINFOEX responses */

typedef struct {
    const gchar *str;
    guint expected_srv_status;
    guint expected_srv_domain;
    guint expected_roam_status;
    guint expected_sim_state;
    guint expected_sys_mode;
    guint expected_sys_submode;
} SysinfoexTest;

static const SysinfoexTest sysinfoex_tests[] = {
    { "^SYSINFOEX:2,4,5,1,,3,WCDMA,41,HSPA+",           2, 4, 5, 1, 3, 41 },
    { "^SYSINFOEX:2,4,5,1,,3,\"WCDMA\",41,\"HSPA+\"",   2, 4, 5, 1, 3, 41 },
    { "^SYSINFOEX: 2,4,5,1,0,3,\"WCDMA\",41,\"HSPA+\"", 2, 4, 5, 1, 3, 41 },
    { NULL,                                             0, 0, 0, 0, 0, 0  }
};

static void
test_sysinfoex (void)
{
    guint i;

    for (i = 0; sysinfoex_tests[i].str; i++) {
        GError *error = NULL;
        guint srv_status = 0;
        guint srv_domain = 0;
        guint roam_status = 0;
        guint sim_state = 0;
        guint sys_mode = 0;
        guint sys_submode = 0;

        g_assert (mm_huawei_parse_sysinfoex_response (sysinfoex_tests[i].str,
                                                      &srv_status,
                                                      &srv_domain,
                                                      &roam_status,
                                                      &sim_state,
                                                      &sys_mode,
                                                      &sys_submode,
                                                      &error) == TRUE);
        g_assert_no_error (error);

        g_assert (srv_status == sysinfoex_tests[i].expected_srv_status);
        g_assert (srv_domain == sysinfoex_tests[i].expected_srv_domain);
        g_assert (roam_status == sysinfoex_tests[i].expected_roam_status);
        g_assert (sim_state == sysinfoex_tests[i].expected_sim_state);
        g_assert (sys_mode == sysinfoex_tests[i].expected_sys_mode);
        g_assert (sys_submode == sysinfoex_tests[i].expected_sys_submode);
    }
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/huawei/ndisstatqry", test_ndisstatqry);
    g_test_add_func ("/MM/huawei/sysinfo", test_sysinfo);
    g_test_add_func ("/MM/huawei/sysinfoex", test_sysinfoex);

    return g_test_run ();
}
