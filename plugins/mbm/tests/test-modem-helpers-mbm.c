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
 * Copyright (C) 2014 Dan Williams <dcbw@redhat.com>
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-mbm.h"

/*****************************************************************************/
/* Test *E2IPCFG responses */

typedef struct {
    const gchar *str;

    /* IPv4 */
    const gchar *ipv4_addr;
    const gchar *ipv4_gw;
    const gchar *ipv4_dns1;
    const gchar *ipv4_dns2;

    /* IPv6 */
    const gchar *ipv6_addr;
    const gchar *ipv6_dns1;
    const gchar *ipv6_dns2;
} E2ipcfgTest;

static const E2ipcfgTest tests[] = {
    { "*E2IPCFG: (1,\"46.157.32.246\")(2,\"46.157.32.243\")(3,\"193.213.112.4\")(3,\"130.67.15.198\")\r\n", 
        "46.157.32.246", "46.157.32.243", "193.213.112.4", "130.67.15.198",
        NULL, NULL },

    { "*E2IPCFG: (1,\"fe80:0000:0000:0000:0000:0000:e537:1801\")(3,\"2001:4600:0004:0fff:0000:0000:0000:0054\")(3,\"2001:4600:0004:1fff:0000:0000:0000:0054\")\r\n",
        NULL, NULL, NULL, NULL,
        "fe80:0000:0000:0000:0000:0000:e537:1801", "2001:4600:0004:0fff:0000:0000:0000:0054", "2001:4600:0004:1fff:0000:0000:0000:0054" },

    { "*E2IPCFG: (1,\"fe80:0000:0000:0000:0000:0027:b7fe:9401\")(3,\"fd00:976a:0000:0000:0000:0000:0000:0009\")\r\n",
        NULL, NULL, NULL, NULL,
        "fe80:0000:0000:0000:0000:0027:b7fe:9401", "fd00:976a:0000:0000:0000:0000:0000:0009", NULL },

    { NULL }
};

static void
test_e2ipcfg (void)
{
    guint i;

    for (i = 0; tests[i].str; i++) {
        gboolean success;
        GError *error = NULL;
        MMBearerIpConfig *ipv4 = NULL;
        MMBearerIpConfig *ipv6 = NULL;
        const gchar **dns;
        guint dnslen;

        success = mm_mbm_parse_e2ipcfg_response (tests[i].str, &ipv4, &ipv6, &error);
        g_assert_no_error (error);
        g_assert (success);

        /* IPv4 */
        if (tests[i].ipv4_addr) {
            g_assert (ipv4);
            g_assert_cmpint (mm_bearer_ip_config_get_method (ipv4), ==, MM_BEARER_IP_METHOD_STATIC);
            g_assert_cmpstr (mm_bearer_ip_config_get_address (ipv4), ==, tests[i].ipv4_addr);
            g_assert_cmpint (mm_bearer_ip_config_get_prefix (ipv4), ==, 28);
            g_assert_cmpstr (mm_bearer_ip_config_get_gateway (ipv4), ==, tests[i].ipv4_gw);

            dns = mm_bearer_ip_config_get_dns (ipv4);
            g_assert (dns);
            dnslen = g_strv_length ((gchar **) dns);
            if (tests[i].ipv4_dns2 != NULL)
                g_assert_cmpint (dnslen, ==, 2);
            else
                g_assert_cmpint (dnslen, ==, 1);
            g_assert_cmpstr (dns[0], ==, tests[i].ipv4_dns1);
            g_assert_cmpstr (dns[1], ==, tests[i].ipv4_dns2);
        } else
            g_assert (ipv4 == NULL);

        /* IPv6 */
        if (tests[i].ipv6_addr) {
            struct in6_addr a6;
            g_assert (ipv6);

            g_assert_cmpstr (mm_bearer_ip_config_get_address (ipv6), ==, tests[i].ipv6_addr);
            g_assert_cmpint (mm_bearer_ip_config_get_prefix (ipv6), ==, 64);

            g_assert (inet_pton (AF_INET6, mm_bearer_ip_config_get_address (ipv6), &a6));
            if (IN6_IS_ADDR_LINKLOCAL (&a6))
                g_assert_cmpint (mm_bearer_ip_config_get_method (ipv6), ==, MM_BEARER_IP_METHOD_DHCP);
            else
                g_assert_cmpint (mm_bearer_ip_config_get_method (ipv6), ==, MM_BEARER_IP_METHOD_STATIC);

            dns = mm_bearer_ip_config_get_dns (ipv6);
            g_assert (dns);
            dnslen = g_strv_length ((gchar **) dns);
            if (tests[i].ipv6_dns2 != NULL)
                g_assert_cmpint (dnslen, ==, 2);
            else
                g_assert_cmpint (dnslen, ==, 1);
            g_assert_cmpstr (dns[0], ==, tests[i].ipv6_dns1);
            g_assert_cmpstr (dns[1], ==, tests[i].ipv6_dns2);
        } else
            g_assert (ipv6 == NULL);
    }
}

/*****************************************************************************/

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

    g_test_add_func ("/MM/mbm/e2ipcfg", test_e2ipcfg);

    return g_test_run ();
}
