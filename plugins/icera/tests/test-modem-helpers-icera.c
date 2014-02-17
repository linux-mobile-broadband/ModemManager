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
#include "mm-modem-helpers-icera.h"

/*****************************************************************************/
/* Test %IPDPADDR responses */

typedef struct {
    const gchar *str;
    const guint expected_cid;

    /* IPv4 */
    const gchar *ipv4_addr;
    const guint ipv4_prefix;
    const gchar *ipv4_gw;
    const gchar *ipv4_dns1;
    const gchar *ipv4_dns2;

    /* IPv6 */
    const gchar *ipv6_addr;
    const gchar *ipv6_dns1;
} IpdpaddrTest;

static const IpdpaddrTest ipdpaddr_tests[] = {
    /* Sierra USB305 */
    { "%IPDPADDR: 2, 21.93.217.11, 21.93.217.10, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0\r\n", 
        2, "21.93.217.11", 32, "21.93.217.10", "10.177.0.34", "10.161.171.220",
        NULL, NULL },

    /* ZTE/Vodafone K3805-Z */
    { "%IPDPADDR: 5, 21.93.217.11, 21.93.217.10, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0, 255.0.0.0, 255.255.255.0, 21.93.217.10,\r\n",
        5, "21.93.217.11", 24, "21.93.217.10", "10.177.0.34", "10.161.171.220",
        NULL, NULL },

    /* Secondary gateway check */
    { "%IPDPADDR: 5, 21.93.217.11, 0.0.0.0, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0, 255.0.0.0, 255.255.255.0, 21.93.217.10,\r\n",
        5, "21.93.217.11", 24, "21.93.217.10", "10.177.0.34", "10.161.171.220",
        NULL, NULL },

    /* Secondary gateway check #2 */
    { "%IPDPADDR: 5, 27.107.96.189, 0.0.0.0, 121.242.190.210, 121.242.190.181, 0.0.0.0, 0.0.0.0, 255.255.255.254, 27.107.96.188\r\n",
        5, "27.107.96.189", 31, "27.107.96.188", "121.242.190.210", "121.242.190.181",
        NULL, NULL },

    /* Nokia 21M */
    { "%IPDPADDR: 1, 33.196.7.127, 33.196.7.128, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0, 255.0.0.0, 33.196.7.128, fe80::f:9135:5901, ::, fd00:976a::9, ::, ::, ::, ::, ::\r\n",
        1, "33.196.7.127", 32, "33.196.7.128", "10.177.0.34", "10.161.171.220",
        "fe80::f:9135:5901", "fd00:976a::9" },

    { "%IPDPADDR: 3, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, fe80::2e:437b:7901, ::, fd00:976a::9, ::, ::, ::, ::, ::\r\n",
        3, NULL, 0, NULL, NULL, NULL,
        "fe80::2e:437b:7901", "fd00:976a::9" },

    /* Some development chip (cnsbg.p1001.rev2, CL477342) */
    { "%IPDPADDR: 5, 27.107.96.189, 27.107.96.188, 121.242.190.210, 121.242.190.181, 0.0.0.0, 0.0.0.0, 255.255.255.254, 27.107.96.188\r\n",
        5, "27.107.96.189", 31, "27.107.96.188", "121.242.190.210", "121.242.190.181",
        NULL, NULL },

    /* 21M with newer firmware */
    { "%IPDPADDR: 2, 188.150.116.13, 188.150.116.14, 188.149.250.16, 0.0.0.0, 0.0.0.0, 0.0.0.0, 255.255.0.0, 188.150.116.14, fe80::1:e414:eb01, ::, 2a00:e18:0:3::6, ::, ::, ::, ::, ::\r\n",
        2, "188.150.116.13", 16, "188.150.116.14", "188.149.250.16", NULL,
        "fe80::1:e414:eb01", "2a00:e18:0:3::6" },

    { NULL }
};

static void
test_ipdpaddr (void)
{
    guint i;

    for (i = 0; ipdpaddr_tests[i].str; i++) {
        gboolean success;
        GError *error = NULL;
        MMBearerIpConfig *ipv4 = NULL;
        MMBearerIpConfig *ipv6 = NULL;
        const gchar **dns;
        guint dnslen;

        success = mm_icera_parse_ipdpaddr_response (
                        ipdpaddr_tests[i].str,
                        ipdpaddr_tests[i].expected_cid,
                        &ipv4,
                        &ipv6,
                        &error);
        g_assert_no_error (error);
        g_assert (success);

        /* IPv4 */
        if (ipdpaddr_tests[i].ipv4_addr) {
            g_assert (ipv4);
            g_assert_cmpint (mm_bearer_ip_config_get_method (ipv4), ==, MM_BEARER_IP_METHOD_STATIC);
            g_assert_cmpstr (mm_bearer_ip_config_get_address (ipv4), ==, ipdpaddr_tests[i].ipv4_addr);
            g_assert_cmpint (mm_bearer_ip_config_get_prefix (ipv4), ==, ipdpaddr_tests[i].ipv4_prefix);
            g_assert_cmpstr (mm_bearer_ip_config_get_gateway (ipv4), ==, ipdpaddr_tests[i].ipv4_gw);

            dns = mm_bearer_ip_config_get_dns (ipv4);
            g_assert (dns);
            dnslen = g_strv_length ((gchar **) dns);
            if (ipdpaddr_tests[i].ipv4_dns2 != NULL)
                g_assert_cmpint (dnslen, ==, 2);
            else
                g_assert_cmpint (dnslen, ==, 1);
            g_assert_cmpstr (dns[0], ==, ipdpaddr_tests[i].ipv4_dns1);
            g_assert_cmpstr (dns[1], ==, ipdpaddr_tests[i].ipv4_dns2);
        } else
            g_assert (ipv4 == NULL);

        /* IPv6 */
        if (ipdpaddr_tests[i].ipv6_addr) {
            struct in6_addr a6;
            g_assert (ipv6);

            g_assert_cmpstr (mm_bearer_ip_config_get_address (ipv6), ==, ipdpaddr_tests[i].ipv6_addr);
            g_assert_cmpint (mm_bearer_ip_config_get_prefix (ipv6), ==, 64);

            g_assert (inet_pton (AF_INET6, mm_bearer_ip_config_get_address (ipv6), &a6));
            if (IN6_IS_ADDR_LINKLOCAL (&a6))
                g_assert_cmpint (mm_bearer_ip_config_get_method (ipv6), ==, MM_BEARER_IP_METHOD_DHCP);
            else
                g_assert_cmpint (mm_bearer_ip_config_get_method (ipv6), ==, MM_BEARER_IP_METHOD_STATIC);

            dns = mm_bearer_ip_config_get_dns (ipv6);
            g_assert (dns);
            dnslen = g_strv_length ((gchar **) dns);
            g_assert_cmpint (dnslen, ==, 1);
            g_assert_cmpstr (dns[0], ==, ipdpaddr_tests[i].ipv6_dns1);
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

    g_test_add_func ("/MM/icera/ipdpaddr", test_ipdpaddr);

    return g_test_run ();
}
