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
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log-test.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
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
    { "^NDISSTATQry:1",     TRUE, TRUE,  FALSE, FALSE },
    { "^NDISSTATQry:1\r\n", TRUE, TRUE,  FALSE, FALSE },
    { "^NDISSTATQry:0",     TRUE, FALSE, FALSE, FALSE },
    { "^NDISSTATQry:0\r\n", TRUE, FALSE, FALSE, FALSE },
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
/* Test ^DHCP responses */

typedef struct {
    const gchar *str;
    const gchar *expected_addr;
    guint expected_prefix;
    const gchar *expected_gateway;
    const gchar *expected_dns1;
    const gchar *expected_dns2;
} DhcpTest;

static const DhcpTest dhcp_tests[] = {
    { "^DHCP:a3ec5c64,f8ffffff,a1ec5c64,a1ec5c64,2200b10a,74bba80a,236800,236800\r\n",
      "100.92.236.163", 29, "100.92.236.161", "10.177.0.34", "10.168.187.116" },
    { "^DHCP:0xa3ec5c64,0xf8ffffff,0xa1ec5c64,0xa1ec5c64,0x2200b10a,0x74bba80a,236800,236800\r\n",
      "100.92.236.163", 29, "100.92.236.161", "10.177.0.34", "10.168.187.116" },
    { "^DHCP: 1010A0A,FCFFFFFF,2010A0A,2010A0A,0,0,150000000,150000000\r\n",
      "10.10.1.1", 30, "10.10.1.2", "0.0.0.0", "0.0.0.0" },
    { "^DHCP: CCDB080A,F8FFFFFF,C9DB080A,C9DB080A,E67B59C0,E77B59C0,85600,85600\r\n",
      "10.8.219.204", 29, "10.8.219.201", "192.89.123.230", "192.89.123.231" },
    { "^DHCP: 0xCCDB080A,0xF8FFFFFF,0xC9DB080A,0xC9DB080A,0xE67B59C0,0xE77B59C0,85600,85600\r\n",
      "10.8.219.204", 29, "10.8.219.201", "192.89.123.230", "192.89.123.231" },
    { "^DHCP: 0XCCDB080A,0XF8FFFFFF,0XC9DB080A,0XC9DB080A,0XE67B59C0,0XE77B59C0,85600,85600\r\n",
      "10.8.219.204", 29, "10.8.219.201", "192.89.123.230", "192.89.123.231" },
    { NULL }
};

static void
test_dhcp (void)
{
    guint i;

    for (i = 0; dhcp_tests[i].str; i++) {
        GError *error = NULL;
        guint addr, prefix, gateway, dns1, dns2;

        g_assert (mm_huawei_parse_dhcp_response (
                      dhcp_tests[i].str,
                      &addr,
                      &prefix,
                      &gateway,
                      &dns1,
                      &dns2,
                      &error) == TRUE);
        g_assert_no_error (error);

        g_assert_cmpstr (inet_ntoa (*((struct in_addr *) &addr)), ==, dhcp_tests[i].expected_addr);
        g_assert_cmpint (prefix, ==, dhcp_tests[i].expected_prefix);
        g_assert_cmpstr (inet_ntoa (*((struct in_addr *) &gateway)), ==, dhcp_tests[i].expected_gateway);
        g_assert_cmpstr (inet_ntoa (*((struct in_addr *) &dns1)), ==, dhcp_tests[i].expected_dns1);
        g_assert_cmpstr (inet_ntoa (*((struct in_addr *) &dns2)), ==, dhcp_tests[i].expected_dns2);
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
/* Test ^PREFMODE=? responses */

#define MAX_PREFMODE_COMBINATIONS 3

typedef struct {
    const gchar *str;
    MMHuaweiPrefmodeCombination expected_modes[MAX_PREFMODE_COMBINATIONS];
} PrefmodeTest;

static const PrefmodeTest prefmode_tests[] = {
    {
        "^PREFMODE:(2,4,8)\r\n",
        {
            {
                .prefmode = 8,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .prefmode = 4,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_3G
            },
            {
                .prefmode = 2,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_2G
            }
        }
    },
    {
        "^PREFMODE:(2,4)\r\n",
        {
            {
                .prefmode = 4,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_3G
            },
            {
                .prefmode = 2,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_2G
            },
            { 0, 0, 0}
        }
    },
    {
        "^PREFMODE:(2)\r\n",
        {
            {
                .prefmode = 2,
                .allowed = MM_MODEM_MODE_2G,
                .preferred = MM_MODEM_MODE_NONE
            },
            { 0, 0, 0}
        }
    },
};

static void
test_prefmode (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (prefmode_tests); i++) {
        GError *error = NULL;
        GArray *combinations = NULL;
        guint j;
        guint n_expected_combinations = 0;

        for (j = 0; j < MAX_PREFMODE_COMBINATIONS; j++) {
            if (prefmode_tests[i].expected_modes[j].prefmode != 0)
                n_expected_combinations++;
        }

        combinations = mm_huawei_parse_prefmode_test (prefmode_tests[i].str, NULL, &error);
        g_assert_no_error (error);
        g_assert (combinations != NULL);
        g_assert_cmpuint (combinations->len, ==, n_expected_combinations);

        for (j = 0; j < combinations->len; j++) {
            MMHuaweiPrefmodeCombination *single;
            g_autofree gchar *allowed_str = NULL;
            g_autofree gchar *preferred_str = NULL;

            single = &g_array_index (combinations, MMHuaweiPrefmodeCombination, j);
            allowed_str = mm_modem_mode_build_string_from_mask (single->allowed);
            preferred_str = mm_modem_mode_build_string_from_mask (single->preferred);
            mm_obj_dbg (NULL, "test[%u], combination[%u]: %u, \"%s\", \"%s\"",
                        i, j, single->prefmode, allowed_str, preferred_str);
        }

        for (j = 0; j < combinations->len; j++) {
            MMHuaweiPrefmodeCombination *single;
            guint k;
            gboolean found = FALSE;

            single = &g_array_index (combinations, MMHuaweiPrefmodeCombination, j);
            for (k = 0; k <= n_expected_combinations; k++) {
                if (single->allowed == prefmode_tests[i].expected_modes[k].allowed &&
                    single->preferred == prefmode_tests[i].expected_modes[k].preferred &&
                    single->prefmode == prefmode_tests[i].expected_modes[k].prefmode) {
                    found = TRUE;
                    break;
                }
            }

            g_assert (found == TRUE);
        }

        g_array_unref (combinations);
    }
}

/*****************************************************************************/
/* Test ^PREFMODE? responses */

typedef struct {
    const gchar *str;
    const gchar *format;
    MMModemMode allowed;
    MMModemMode preferred;
} PrefmodeResponseTest;

static const PrefmodeResponseTest prefmode_response_tests[] = {
    {
        .str = "^PREFMODE:2\r\n",
        .format = "^PREFMODE:(2,4,8)\r\n",
        .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
        .preferred = MM_MODEM_MODE_2G
    },
    {
        .str = "^PREFMODE:4\r\n",
        .format = "^PREFMODE:(2,4,8)\r\n",
        .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
        .preferred = MM_MODEM_MODE_3G
    },
    {
        .str = "^PREFMODE:8\r\n",
        .format = "^PREFMODE:(2,4,8)\r\n",
        .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
        .preferred = MM_MODEM_MODE_NONE
    }
};

static void
test_prefmode_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (prefmode_response_tests); i++) {
        GArray *combinations = NULL;
        const MMHuaweiPrefmodeCombination *found;
        GError *error = NULL;

        combinations = mm_huawei_parse_prefmode_test (prefmode_response_tests[i].format, NULL, NULL);
        g_assert (combinations != NULL);

        found = mm_huawei_parse_prefmode_response (prefmode_response_tests[i].str,
                                                   combinations,
                                                   &error);
        g_assert_no_error (error);
        g_assert (found != NULL);
        g_assert_cmpuint (found->allowed, ==, prefmode_response_tests[i].allowed);
        g_assert_cmpuint (found->preferred, ==, prefmode_response_tests[i].preferred);

        g_array_unref (combinations);
    }
}

/*****************************************************************************/
/* Test ^SYSCFG=? responses */

#define MAX_SYSCFG_COMBINATIONS 5

typedef struct {
    const gchar *str;
    MMHuaweiSyscfgCombination expected_modes[MAX_SYSCFG_COMBINATIONS];
} SyscfgTest;

static const SyscfgTest syscfg_tests[] = {
    {
        MM_HUAWEI_DEFAULT_SYSCFG_FMT,
        {
            {
                .mode = 2,
                .acqorder = 0,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode = 2,
                .acqorder = 1,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_2G
            },
            {
                .mode = 2,
                .acqorder = 2,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_3G
            },
            {
                .mode = 13,
                .acqorder = 0,
                .allowed = MM_MODEM_MODE_2G,
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode = 14,
                .acqorder = 0,
                .allowed = MM_MODEM_MODE_3G,
                .preferred = MM_MODEM_MODE_NONE
            }
        }
    },
    {
        "^SYSCFG:(2,13,14,16),(0-3),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        {
            {
                .mode = 2,
                .acqorder = 0,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode = 2,
                .acqorder = 1,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_2G
            },
            {
                .mode = 2,
                .acqorder = 2,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_3G
            },
            {
                .mode = 13,
                .acqorder = 0,
                .allowed = MM_MODEM_MODE_2G,
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode = 14,
                .acqorder = 0,
                .allowed = MM_MODEM_MODE_3G,
                .preferred = MM_MODEM_MODE_NONE
            }
        }
    },
    {
        "^SYSCFG:(2,13,14,16),(0),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        {
            {
                .mode = 2,
                .acqorder = 0,
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode = 13,
                .acqorder = 0,
                .allowed = MM_MODEM_MODE_2G,
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode = 14,
                .acqorder = 0,
                .allowed = MM_MODEM_MODE_3G,
                .preferred = MM_MODEM_MODE_NONE
            },
            { 0, 0, 0, 0 }
        }
    },
    {
        "^SYSCFG:(13),(0),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        {
            {
                .mode = 13,
                .acqorder = 0,
                .allowed = MM_MODEM_MODE_2G,
                .preferred = MM_MODEM_MODE_NONE
            },
            { 0, 0, 0, 0 }
        }
    }
};

static void
test_syscfg (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (syscfg_tests); i++) {
        GError *error = NULL;
        GArray *combinations = NULL;
        guint j;
        guint n_expected_combinations = 0;

        for (j = 0; j < MAX_SYSCFG_COMBINATIONS; j++) {
            if (syscfg_tests[i].expected_modes[j].mode != 0)
                n_expected_combinations++;
        }

        combinations = mm_huawei_parse_syscfg_test (syscfg_tests[i].str, NULL, &error);
        g_assert_no_error (error);
        g_assert (combinations != NULL);
        g_assert_cmpuint (combinations->len, ==, n_expected_combinations);

        for (j = 0; j < combinations->len; j++) {
            MMHuaweiSyscfgCombination *single;
            g_autofree gchar *allowed_str = NULL;
            g_autofree gchar *preferred_str = NULL;

            single = &g_array_index (combinations, MMHuaweiSyscfgCombination, j);
            allowed_str = mm_modem_mode_build_string_from_mask (single->allowed);
            preferred_str = mm_modem_mode_build_string_from_mask (single->preferred);
            mm_obj_dbg (NULL, "test[%u], combination[%u]: %u, %u, \"%s\", \"%s\"",
                        i, j, single->mode, single->acqorder, allowed_str, preferred_str);
        }

        for (j = 0; j < combinations->len; j++) {
            MMHuaweiSyscfgCombination *single;
            guint k;
            gboolean found = FALSE;

            single = &g_array_index (combinations, MMHuaweiSyscfgCombination, j);
            for (k = 0; k <= n_expected_combinations; k++) {
                if (single->allowed == syscfg_tests[i].expected_modes[k].allowed &&
                    single->preferred == syscfg_tests[i].expected_modes[k].preferred &&
                    single->mode == syscfg_tests[i].expected_modes[k].mode &&
                    single->acqorder == syscfg_tests[i].expected_modes[k].acqorder) {
                    found = TRUE;
                    break;
                }
            }

            g_assert (found == TRUE);
        }

        g_array_unref (combinations);
    }
}

/*****************************************************************************/
/* Test ^SYSCFG? responses */

typedef struct {
    const gchar *str;
    const gchar *format;
    MMModemMode allowed;
    MMModemMode preferred;
} SyscfgResponseTest;

static const SyscfgResponseTest syscfg_response_tests[] = {
    {
        .str = "^SYSCFG: 2,0,400000,0,3\r\n",
        .format = "^SYSCFG:(2,13,14,16),(0-3),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFG: 2,1,400000,0,3\r\n",
        .format = "^SYSCFG:(2,13,14,16),(0-3),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
        .preferred = MM_MODEM_MODE_2G
    },
    {
        .str = "^SYSCFG: 2,2,400000,0,3\r\n",
        .format = "^SYSCFG:(2,13,14,16),(0-3),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
        .preferred = MM_MODEM_MODE_3G
    },
    {
        .str = "^SYSCFG: 13,0,400000,0,3\r\n",
        .format = "^SYSCFG:(2,13,14,16),(0-3),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        .allowed = MM_MODEM_MODE_2G,
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFG: 14,0,400000,0,3\r\n",
        .format = "^SYSCFG:(2,13,14,16),(0-3),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        .allowed = MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        /* Non-sensical acquisition order (WCDMA-only but acquire WCDMA-then-GSM */
        .str = "^SYSCFG: 14,2,400000,0,3\r\n",
        .format = "^SYSCFG:(2,13,14,16),(0-3),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        .allowed = MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        /* Non-sensical acquisition order (GSM-only but acquire GSM-then-WCDMA */
        .str = "^SYSCFG: 13,1,400000,0,3\r\n",
        .format = "^SYSCFG:(2,13,14,16),(0-3),((400000,\"WCDMA2100\")),(0-2),(0-4)\r\n",
        .allowed = MM_MODEM_MODE_2G,
        .preferred = MM_MODEM_MODE_NONE
    }
};

static void
test_syscfg_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (syscfg_response_tests); i++) {
        GArray *combinations = NULL;
        const MMHuaweiSyscfgCombination *found;
        GError *error = NULL;

        combinations = mm_huawei_parse_syscfg_test (syscfg_response_tests[i].format, NULL, NULL);
        g_assert (combinations != NULL);

        found = mm_huawei_parse_syscfg_response (syscfg_response_tests[i].str,
                                                 combinations,
                                                 &error);

        g_assert_no_error (error);
        g_assert (found != NULL);
        g_assert_cmpuint (found->allowed, ==, syscfg_response_tests[i].allowed);
        g_assert_cmpuint (found->preferred, ==, syscfg_response_tests[i].preferred);

        g_array_unref (combinations);
    }
}

/*****************************************************************************/
/* Test ^SYSCFGEX=? responses */

#define MAX_SYSCFGEX_COMBINATIONS 5

typedef struct {
    const gchar *str;
    MMHuaweiSyscfgexCombination expected_modes[MAX_SYSCFGEX_COMBINATIONS];
} SyscfgexTest;

static const SyscfgexTest syscfgex_tests[] = {
    {
        "^SYSCFGEX: (\"00\",\"03\",\"02\",\"01\",\"99\"),"
        "((2000004e80380,\"GSM850/GSM900/GSM1800/GSM1900/WCDMA850/WCDMA900/WCDMA1900/WCDMA2100\"),(3fffffff,\"All Bands\")),"
        "(0-3),"
        "(0-4),"
        "((800c5,\"LTE2100/LTE1800/LTE2600/LTE900/LTE800\"),(7fffffffffffffff,\"All bands\"))"
        "\r\n",
        {
            {
                .mode_str = (gchar *) "00",
                .allowed = (MM_MODEM_MODE_4G | MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode_str = (gchar *) "03",
                .allowed = MM_MODEM_MODE_4G,
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode_str = (gchar *) "02",
                .allowed = MM_MODEM_MODE_3G,
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode_str = (gchar *) "01",
                .allowed = MM_MODEM_MODE_2G,
                .preferred = MM_MODEM_MODE_NONE
            },
            { NULL, 0, 0 }
        }
    },
    {
        "^SYSCFGEX: (\"030201\",\"0302\",\"03\",\"99\"),"
        "((2000004e80380,\"GSM850/GSM900/GSM1800/GSM1900/WCDMA850/WCDMA900/WCDMA1900/WCDMA2100\"),(3fffffff,\"All Bands\")),"
        "(0-3),"
        "(0-4),"
        "((800c5,\"LTE2100/LTE1800/LTE2600/LTE900/LTE800\"),(7fffffffffffffff,\"All bands\"))"
        "\r\n",
        {
            {
                .mode_str = (gchar *) "030201",
                .allowed = (MM_MODEM_MODE_4G | MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_4G
            },
            {
                .mode_str = (gchar *) "0302",
                .allowed = (MM_MODEM_MODE_4G | MM_MODEM_MODE_3G),
                .preferred = MM_MODEM_MODE_4G
            },
            {
                .mode_str = (gchar *) "03",
                .allowed = MM_MODEM_MODE_4G,
                .preferred = MM_MODEM_MODE_NONE
            },
            { NULL, 0, 0 }
        }
    },
    {
        "^SYSCFGEX: (\"03\"),"
        "((2000004e80380,\"GSM850/GSM900/GSM1800/GSM1900/WCDMA850/WCDMA900/WCDMA1900/WCDMA2100\"),(3fffffff,\"All Bands\")),"
        "(0-3),"
        "(0-4),"
        "((800c5,\"LTE2100/LTE1800/LTE2600/LTE900/LTE800\"),(7fffffffffffffff,\"All bands\"))"
        "\r\n",
        {
            {
                .mode_str = (gchar *) "03",
                .allowed = MM_MODEM_MODE_4G,
                .preferred = MM_MODEM_MODE_NONE
            },
            { NULL, 0, 0 }
        }
    },
    {
        "^SYSCFGEX: (\"00\",\"01\",\"02\",\"0102\",\"0201\"),"
        "((3fffffff,\"All Bands\"),(2000000400180,\"GSM900/GSM1800/WCDMA900/WCDMA2100\"),(6A80000,\"GSM850/GSM1900/WCDMA850/AWS/WCDMA1900\")),"
        "(0-2),"
        "(0-4),"
        "," /* NOTE: Non-LTE modem, LTE Bands EMPTY */
        "\r\n",
        {
            {
                .mode_str = (gchar *) "00",
                .allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode_str = (gchar *) "01",
                .allowed = MM_MODEM_MODE_2G,
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode_str = (gchar *) "02",
                .allowed = MM_MODEM_MODE_3G,
                .preferred = MM_MODEM_MODE_NONE
            },
            {
                .mode_str = (gchar *) "0102",
                .allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G),
                .preferred = MM_MODEM_MODE_2G
            },
            {
                .mode_str = (gchar *) "0201",
                .allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G),
                .preferred = MM_MODEM_MODE_3G
            }
        }
    }
};

static void
test_syscfgex (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (syscfgex_tests); i++) {
        GError *error = NULL;
        GArray *combinations = NULL;
        guint j;
        guint n_expected_combinations = 0;

        for (j = 0; j < MAX_SYSCFGEX_COMBINATIONS; j++) {
            if (syscfgex_tests[i].expected_modes[j].mode_str != NULL)
                n_expected_combinations++;
        }

        combinations = mm_huawei_parse_syscfgex_test (syscfgex_tests[i].str, &error);
        g_assert_no_error (error);
        g_assert (combinations != NULL);
        g_assert_cmpuint (combinations->len, ==, n_expected_combinations);

        for (j = 0; j < combinations->len; j++) {
            MMHuaweiSyscfgexCombination *single;
            g_autofree gchar *allowed_str = NULL;
            g_autofree gchar *preferred_str = NULL;

            single = &g_array_index (combinations, MMHuaweiSyscfgexCombination, j);
            allowed_str = mm_modem_mode_build_string_from_mask (single->allowed);
            preferred_str = mm_modem_mode_build_string_from_mask (single->preferred);
            mm_obj_dbg (NULL, "test[%u], combination[%u]: \"%s\", \"%s\", \"%s\"",
                        i, j, single->mode_str, allowed_str, preferred_str);
        }

        for (j = 0; j < combinations->len; j++) {
            MMHuaweiSyscfgexCombination *single;
            guint k;
            gboolean found = FALSE;

            single = &g_array_index (combinations, MMHuaweiSyscfgexCombination, j);
            for (k = 0; k <= n_expected_combinations; k++) {
                if (g_str_equal (single->mode_str, syscfgex_tests[i].expected_modes[k].mode_str) &&
                    single->allowed == syscfgex_tests[i].expected_modes[k].allowed &&
                    single->preferred == syscfgex_tests[i].expected_modes[k].preferred) {
                    found = TRUE;
                    break;
                }
            }

            g_assert (found == TRUE);
        }

        g_array_unref (combinations);
    }
}

/*****************************************************************************/
/* Test ^SYSCFGEX? responses */

typedef struct {
    const gchar *str;
    const gchar *format;
    MMModemMode allowed;
    MMModemMode preferred;
} SyscfgexResponseTest;

static const SyscfgexResponseTest syscfgex_response_tests[] = {
    {
        .str = "^SYSCFGEX: \"00\",3FFFFFFF,1,2,7FFFFFFFFFFFFFFF",
        .format = "^SYSCFGEX: (\"00\",\"03\",\"02\",\"01\",\"99\"),"
                  "((2000004e80380,\"GSM850/GSM900/GSM1800/GSM1900/WCDMA850/WCDMA900/WCDMA1900/WCDMA2100\"),(3fffffff,\"All Bands\")),"
                  "(0-3),"
                  "(0-4),"
                  "((800c5,\"LTE2100/LTE1800/LTE2600/LTE900/LTE800\"),(7fffffffffffffff,\"All bands\"))"
                  "\r\n",
        .allowed = (MM_MODEM_MODE_4G | MM_MODEM_MODE_3G | MM_MODEM_MODE_2G),
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFGEX: \"03\",3FFFFFFF,1,2,7FFFFFFFFFFFFFFF",
        .format = "^SYSCFGEX: (\"00\",\"03\",\"02\",\"01\",\"99\"),"
                  "((2000004e80380,\"GSM850/GSM900/GSM1800/GSM1900/WCDMA850/WCDMA900/WCDMA1900/WCDMA2100\"),(3fffffff,\"All Bands\")),"
                  "(0-3),"
                  "(0-4),"
                  "((800c5,\"LTE2100/LTE1800/LTE2600/LTE900/LTE800\"),(7fffffffffffffff,\"All bands\"))"
                  "\r\n",
        .allowed = MM_MODEM_MODE_4G,
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFGEX: \"02\",3FFFFFFF,1,2,7FFFFFFFFFFFFFFF",
        .format = "^SYSCFGEX: (\"00\",\"03\",\"02\",\"01\",\"99\"),"
                  "((2000004e80380,\"GSM850/GSM900/GSM1800/GSM1900/WCDMA850/WCDMA900/WCDMA1900/WCDMA2100\"),(3fffffff,\"All Bands\")),"
                  "(0-3),"
                  "(0-4),"
                  "((800c5,\"LTE2100/LTE1800/LTE2600/LTE900/LTE800\"),(7fffffffffffffff,\"All bands\"))"
                  "\r\n",
        .allowed = MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFGEX: \"01\",3FFFFFFF,1,2,7FFFFFFFFFFFFFFF",
        .format = "^SYSCFGEX: (\"00\",\"03\",\"02\",\"01\",\"99\"),"
                  "((2000004e80380,\"GSM850/GSM900/GSM1800/GSM1900/WCDMA850/WCDMA900/WCDMA1900/WCDMA2100\"),(3fffffff,\"All Bands\")),"
                  "(0-3),"
                  "(0-4),"
                  "((800c5,\"LTE2100/LTE1800/LTE2600/LTE900/LTE800\"),(7fffffffffffffff,\"All bands\"))"
                  "\r\n",
        .allowed = MM_MODEM_MODE_2G,
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFGEX: \"00\",3fffffff,1,2,",
        .format = "^SYSCFGEX: (\"00\",\"01\",\"02\",\"0102\",\"0201\"),"
                  "((3fffffff,\"All Bands\"),(2000000400180,\"GSM900/GSM1800/WCDMA900/WCDMA2100\"),(6A80000,\"GSM850/GSM1900/WCDMA850/AWS/WCDMA1900\")),"
                  "(0-2),"
                  "(0-4),"
                  "," /* NOTE: Non-LTE modem, LTE Bands EMPTY */
                  "\r\n",
        .allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G),
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFGEX: \"01\",3fffffff,1,2,",
        .format = "^SYSCFGEX: (\"00\",\"01\",\"02\",\"0102\",\"0201\"),"
                  "((3fffffff,\"All Bands\"),(2000000400180,\"GSM900/GSM1800/WCDMA900/WCDMA2100\"),(6A80000,\"GSM850/GSM1900/WCDMA850/AWS/WCDMA1900\")),"
                  "(0-2),"
                  "(0-4),"
                  "," /* NOTE: Non-LTE modem, LTE Bands EMPTY */
                  "\r\n",
        .allowed = MM_MODEM_MODE_2G,
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFGEX: \"02\",3fffffff,1,2,",
        .format = "^SYSCFGEX: (\"00\",\"01\",\"02\",\"0102\",\"0201\"),"
                  "((3fffffff,\"All Bands\"),(2000000400180,\"GSM900/GSM1800/WCDMA900/WCDMA2100\"),(6A80000,\"GSM850/GSM1900/WCDMA850/AWS/WCDMA1900\")),"
                  "(0-2),"
                  "(0-4),"
                  "," /* NOTE: Non-LTE modem, LTE Bands EMPTY */
                  "\r\n",
        .allowed = MM_MODEM_MODE_3G,
        .preferred = MM_MODEM_MODE_NONE
    },
    {
        .str = "^SYSCFGEX: \"0102\",3fffffff,1,2,",
        .format = "^SYSCFGEX: (\"00\",\"01\",\"02\",\"0102\",\"0201\"),"
                  "((3fffffff,\"All Bands\"),(2000000400180,\"GSM900/GSM1800/WCDMA900/WCDMA2100\"),(6A80000,\"GSM850/GSM1900/WCDMA850/AWS/WCDMA1900\")),"
                  "(0-2),"
                  "(0-4),"
                  "," /* NOTE: Non-LTE modem, LTE Bands EMPTY */
                  "\r\n",
        .allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G),
        .preferred = MM_MODEM_MODE_2G
    },
    {
        .str = "^SYSCFGEX: \"0201\",3fffffff,1,2,",
        .format = "^SYSCFGEX: (\"00\",\"01\",\"02\",\"0102\",\"0201\"),"
                  "((3fffffff,\"All Bands\"),(2000000400180,\"GSM900/GSM1800/WCDMA900/WCDMA2100\"),(6A80000,\"GSM850/GSM1900/WCDMA850/AWS/WCDMA1900\")),"
                  "(0-2),"
                  "(0-4),"
                  "," /* NOTE: Non-LTE modem, LTE Bands EMPTY */
                  "\r\n",
        .allowed = (MM_MODEM_MODE_2G | MM_MODEM_MODE_3G),
        .preferred = MM_MODEM_MODE_3G
    }
};

static void
test_syscfgex_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (syscfgex_response_tests); i++) {
        GArray *combinations = NULL;
        const MMHuaweiSyscfgexCombination *found;
        GError *error = NULL;

        combinations = mm_huawei_parse_syscfgex_test (syscfgex_response_tests[i].format, NULL);
        g_assert (combinations != NULL);

        found = mm_huawei_parse_syscfgex_response (syscfgex_response_tests[i].str,
                                                   combinations,
                                                   &error);

        g_assert_no_error (error);
        g_assert (found != NULL);
        g_assert_cmpuint (found->allowed, ==, syscfgex_response_tests[i].allowed);
        g_assert_cmpuint (found->preferred, ==, syscfgex_response_tests[i].preferred);

        g_array_unref (combinations);
    }
}

/*****************************************************************************/
/* Test ^NWTIME responses */

typedef struct {
    const gchar *str;
    gboolean ret;
    gboolean test_iso8601;
    gboolean test_tz;
    const gchar *iso8601;
    gint32 offset;
    gint32 dst_offset;
    gint32 leap_seconds;
} NwtimeTest;

#define NWT_UNKNOWN    MM_NETWORK_TIMEZONE_LEAP_SECONDS_UNKNOWN

static const NwtimeTest nwtime_tests[] = {
    { "^NWTIME: 14/08/05,04:00:21+40,00", TRUE, TRUE, FALSE,
        "2014-08-05T04:00:21+10:00", 600, 0, NWT_UNKNOWN },
    { "^NWTIME: 14/08/05,04:00:21+40,00", TRUE, FALSE, TRUE,
        "2014-08-05T04:00:21+10:00", 600, 0, NWT_UNKNOWN },
    { "^NWTIME: 14/08/05,04:00:21+40,00", TRUE, TRUE, TRUE,
        "2014-08-05T04:00:21+10:00", 600, 0, NWT_UNKNOWN },

    { "^NWTIME: 14/08/05,04:00:21+20,00", TRUE, TRUE, FALSE,
        "2014-08-05T04:00:21+05:00", 300, 0, NWT_UNKNOWN },
    { "^NWTIME: 14/08/05,04:00:21+20,00", TRUE, FALSE, TRUE,
        "2014-08-05T04:00:21+05:00", 300, 0, NWT_UNKNOWN },
    { "^NWTIME: 14/08/05,04:00:21+20,00", TRUE, TRUE, TRUE,
        "2014-08-05T04:00:21+05:00", 300, 0, NWT_UNKNOWN },

    { "^NWTIME: 14/08/05,04:00:21+40,01", TRUE, TRUE, FALSE,
        "2014-08-05T04:00:21+11:00", 600, 60, NWT_UNKNOWN },
    { "^NWTIME: 14/08/05,04:00:21+40,01", TRUE, FALSE, TRUE,
        "2014-08-05T04:00:21+11:00", 600, 60, NWT_UNKNOWN },
    { "^NWTIME: 14/08/05,04:00:21+40,01", TRUE, TRUE, TRUE,
        "2014-08-05T04:00:21+11:00", 600, 60, NWT_UNKNOWN },

    { "^NWTIME: 14/08/05,04:00:21+40,02", TRUE, TRUE, FALSE,
        "2014-08-05T04:00:21+12:00", 600, 120, NWT_UNKNOWN },
    { "^NWTIME: 14/08/05,04:00:21+40,02", TRUE, FALSE, TRUE,
        "2014-08-05T04:00:21+12:00", 600, 120, NWT_UNKNOWN },
    { "^NWTIME: 14/08/05,04:00:21+40,02", TRUE, TRUE, TRUE,
        "2014-08-05T04:00:21+12:00", 600, 120, NWT_UNKNOWN },

    { "^TIME: XX/XX/XX,XX:XX:XX+XX,XX", FALSE, TRUE, FALSE,
        NULL, NWT_UNKNOWN, NWT_UNKNOWN, NWT_UNKNOWN },

    { "^TIME: 14/08/05,04:00:21+40,00", FALSE, TRUE, FALSE,
        NULL, NWT_UNKNOWN, NWT_UNKNOWN, NWT_UNKNOWN },

    { NULL, FALSE, FALSE, FALSE, NULL, NWT_UNKNOWN, NWT_UNKNOWN, NWT_UNKNOWN }
};

static void
test_nwtime (void)
{
    guint i;

    for (i = 0; nwtime_tests[i].str; i++) {
        GError *error = NULL;
        gchar *iso8601 = NULL;
        MMNetworkTimezone *tz = NULL;
        gboolean ret;

        ret = mm_huawei_parse_nwtime_response (nwtime_tests[i].str,
                                               nwtime_tests[i].test_iso8601 ? &iso8601 : NULL,
                                               nwtime_tests[i].test_tz ? &tz : NULL,
                                               &error);

        g_assert (ret == nwtime_tests[i].ret);
        g_assert (ret == (error ? FALSE : TRUE));

        g_clear_error (&error);

        if (nwtime_tests[i].test_iso8601)
            g_assert_cmpstr (nwtime_tests[i].iso8601, ==, iso8601);

        if (nwtime_tests[i].test_tz) {
            g_assert (nwtime_tests[i].offset == mm_network_timezone_get_offset (tz));
            g_assert (nwtime_tests[i].dst_offset == mm_network_timezone_get_dst_offset (tz));
            g_assert (nwtime_tests[i].leap_seconds == mm_network_timezone_get_leap_seconds (tz));
        }

        g_free (iso8601);

        if (tz)
            g_object_unref (tz);
    }
}

/*****************************************************************************/
/* Test ^TIME responses */

typedef struct {
    const gchar *str;
    gboolean ret;
    const gchar *iso8601;
} TimeTest;

static const TimeTest time_tests[] = {
    { "^TIME: 14/08/05 04:00:21", TRUE, "2014-08-05T04:00:21" },
    { "^TIME: 2014/08/05 04:00:21", TRUE, "2014-08-05T04:00:21" },
    { "^TIME: 14-08-05 04:00:21", FALSE, NULL },
    { "^TIME: 14-08-05,04:00:21", FALSE, NULL },
    { "^TIME: 14/08/05 04:00:21 AEST", FALSE, NULL },
    { NULL, FALSE, NULL }
};

static void
test_time (void)
{
    guint i;

    for (i = 0; time_tests[i].str; i++) {
        GError *error = NULL;
        gchar *iso8601 = NULL;
        gboolean ret;

        ret = mm_huawei_parse_time_response (time_tests[i].str,
                                             &iso8601,
                                             NULL,
                                             &error);

        g_assert (ret == time_tests[i].ret);
        g_assert (ret == (error ? FALSE : TRUE));
        g_clear_error (&error);

        g_assert_cmpstr (time_tests[i].iso8601, ==, iso8601);
        g_free (iso8601);
    }
}

/*****************************************************************************/
/* Test ^HCSQ responses */

typedef struct {
    const gchar *str;
    gboolean ret;
    MMModemAccessTechnology act;
    guint value1;
    guint value2;
    guint value3;
    guint value4;
    guint value5;
} HcsqTest;

static const HcsqTest hcsq_tests[] = {
    { "^HCSQ:\"LTE\",30,19,66,0\r\n",  TRUE,  MM_MODEM_ACCESS_TECHNOLOGY_LTE,     30, 19,   66,  0, 0 },
    { "^HCSQ: \"WCDMA\",30,30,58\r\n", TRUE,  MM_MODEM_ACCESS_TECHNOLOGY_UMTS,    30, 30,   58,  0, 0 },
    { "^HCSQ: \"GSM\",36,255\r\n",     TRUE,  MM_MODEM_ACCESS_TECHNOLOGY_GSM,     36, 255,   0,  0, 0 },
    { "^HCSQ: LTE,33,40,135,11\r\n",   TRUE,  MM_MODEM_ACCESS_TECHNOLOGY_LTE,     33,  40, 135, 11, 0 },
    { "^HCSQ: \"NOSERVICE\"\r\n",      FALSE, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,  0,   0,   0,  0, 0 },
    { "^HCSQ: NOSERVICE\r\n",          FALSE, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,  0,   0,   0,  0, 0 },
    { NULL,                            FALSE, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,  0,   0,   0,  0, 0 }
};

static void
test_hcsq (void)
{
    guint i;

    for (i = 0; hcsq_tests[i].str; i++) {
        GError *error = NULL;
        MMModemAccessTechnology act;
        guint value1 = 0;
        guint value2 = 0;
        guint value3 = 0;
        guint value4 = 0;
        guint value5 = 0;
        gboolean ret;

        ret = mm_huawei_parse_hcsq_response (hcsq_tests[i].str,
                                             &act,
                                             &value1,
                                             &value2,
                                             &value3,
                                             &value4,
                                             &value5,
                                             &error);
        g_assert (ret == hcsq_tests[i].ret);
        if (ret) {
            g_assert_no_error (error);
            g_assert_cmpint (hcsq_tests[i].act, ==, act);
            g_assert_cmpint (hcsq_tests[i].value1, ==, value1);
            g_assert_cmpint (hcsq_tests[i].value2, ==, value2);
            g_assert_cmpint (hcsq_tests[i].value3, ==, value3);
            g_assert_cmpint (hcsq_tests[i].value4, ==, value4);
            g_assert_cmpint (hcsq_tests[i].value5, ==, value5);
        } else
            g_assert (error);
        g_clear_error (&error);
    }
}

/*****************************************************************************/
/* Test ^GETPORTMODE response */

typedef struct {
    const gchar      *str;
    guint             n_modes;
    MMHuaweiPortMode  modes[8];
} GetportmodeTest;

static const GetportmodeTest getportmode_tests[] = {
    {
        "^GETPORTMODE: TYPE: WCDMA: huawei,PCUI:0,MDM:1",
        2, { MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_MODEM }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: huawei,MDM:0,PCUI:1,NDIS:2,CDROM:3,SD:4,",
        5, { MM_HUAWEI_PORT_MODE_MODEM,
             MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_CDROM,
             MM_HUAWEI_PORT_MODE_SD }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: huawei,MDM:0,PCUI:1,NDIS:2,GPS:3,BT:4,",
        5, { MM_HUAWEI_PORT_MODE_MODEM,
             MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_GPS,
             MM_HUAWEI_PORT_MODE_BT
        }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: huawei,PCUI:0,MDM:1,NDIS:2,CDROM:3,SD:4,GPS:5,BT:6",
        7, { MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_MODEM,
             MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_CDROM,
             MM_HUAWEI_PORT_MODE_SD,
             MM_HUAWEI_PORT_MODE_GPS,
             MM_HUAWEI_PORT_MODE_BT
        }
    },
    {
        "^getportmode:type:WCDMA:Qualcomm,NDIS:0,DIAG:1,PCUI:2,MDM:3,SD:4",
        5, { MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_DIAG,
             MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_MODEM,
             MM_HUAWEI_PORT_MODE_SD
        }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: ,pcui:1,modem:2,ncm:3,mass:4,mass_two:5,",
        5, { MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_MODEM,
             MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_SD,
             MM_HUAWEI_PORT_MODE_SD
        }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: huawei ,, rndis: 0, pcui: 1, c_shell: 2, a_shell: 3,3g_diag: 4, gps: 5, 4g_diag: 6, mass_two: 7",
        8, { MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_SHELL,
             MM_HUAWEI_PORT_MODE_SHELL,
             MM_HUAWEI_PORT_MODE_DIAG,
             MM_HUAWEI_PORT_MODE_GPS,
             MM_HUAWEI_PORT_MODE_DIAG,
             MM_HUAWEI_PORT_MODE_SD
        }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: huawei,ecm:1,pcui:2,c_shell:3,a_shell:4,3g_diag:5,gps:6,4g_diag:7,mass:8,",
        8, { MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_SHELL,
             MM_HUAWEI_PORT_MODE_SHELL,
             MM_HUAWEI_PORT_MODE_DIAG,
             MM_HUAWEI_PORT_MODE_GPS,
             MM_HUAWEI_PORT_MODE_DIAG,
             MM_HUAWEI_PORT_MODE_SD
        }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: huawei,rndis:1,pcui:2,c_shell:3,a_shell:4,3g_diag:5,gps:6,4g_diag:7,mass:8,",
        8, { MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_SHELL,
             MM_HUAWEI_PORT_MODE_SHELL,
             MM_HUAWEI_PORT_MODE_DIAG,
             MM_HUAWEI_PORT_MODE_GPS,
             MM_HUAWEI_PORT_MODE_DIAG,
             MM_HUAWEI_PORT_MODE_SD
        }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: huawei,,pcui:0,3g_modem:1,ncm:2,mass:3,mass_two:4",
        5, { MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_MODEM,
             MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_SD,
             MM_HUAWEI_PORT_MODE_SD
        }
    },
    {
        "^GETPORTMODE:TYPE:WCDMA:Qualcomm,MDM:0,NDIS:1,DIAG:2,PCUI:3,CDROM:4,SD:5",
        6, { MM_HUAWEI_PORT_MODE_MODEM,
             MM_HUAWEI_PORT_MODE_NET,
             MM_HUAWEI_PORT_MODE_DIAG,
             MM_HUAWEI_PORT_MODE_PCUI,
             MM_HUAWEI_PORT_MODE_CDROM,
             MM_HUAWEI_PORT_MODE_SD
        }
    },
    {
        "^GETPORTMODE: TYPE: WCDMA: Huawei Technologies Co.,Ltd.,",
        0
    },
};

static void
test_getportmode (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (getportmode_tests); i++) {
        g_autoptr(GArray) modes = NULL;
        g_autoptr(GError) error = NULL;

        mm_obj_dbg (NULL, "testing ^GETPORTMODE response: '%s'", getportmode_tests[i].str);

        modes = mm_huawei_parse_getportmode_response (getportmode_tests[i].str, NULL, &error);
        if (modes) {
            guint j;

            g_assert_no_error (error);
            g_assert_cmpuint (modes->len, ==, getportmode_tests[i].n_modes);
            for (j = 0; j < getportmode_tests[i].n_modes; j++)
                g_assert_cmpuint (g_array_index (modes, MMHuaweiPortMode, j), ==, getportmode_tests[i].modes[j]);
        } else
            g_assert (error);
    }
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/huawei/ndisstatqry", test_ndisstatqry);
    g_test_add_func ("/MM/huawei/dhcp", test_dhcp);
    g_test_add_func ("/MM/huawei/sysinfo", test_sysinfo);
    g_test_add_func ("/MM/huawei/sysinfoex", test_sysinfoex);
    g_test_add_func ("/MM/huawei/prefmode", test_prefmode);
    g_test_add_func ("/MM/huawei/prefmode/response", test_prefmode_response);
    g_test_add_func ("/MM/huawei/syscfg", test_syscfg);
    g_test_add_func ("/MM/huawei/syscfg/response", test_syscfg_response);
    g_test_add_func ("/MM/huawei/syscfgex", test_syscfgex);
    g_test_add_func ("/MM/huawei/syscfgex/response", test_syscfgex_response);
    g_test_add_func ("/MM/huawei/nwtime", test_nwtime);
    g_test_add_func ("/MM/huawei/time", test_time);
    g_test_add_func ("/MM/huawei/hcsq", test_hcsq);
    g_test_add_func ("/MM/huawei/getportmode", test_getportmode);

    return g_test_run ();
}
