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

#include "mm-log-test.h"
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

    { "%IPDPADDR: 1, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, fe80::1f:fad1:4c01, ::, 2001:4600:4:fff::54, 2001:4600:4:1fff::54, ::, ::, ::, ::\r\n",
      1, NULL, 0, NULL, NULL, NULL,
      "fe80::1f:fad1:4c01", "2001:4600:4:fff::54" },

    { "%IPDPADDR: 1, 46.157.76.179, 46.157.76.180, 193.213.112.4, 130.67.15.198, 0.0.0.0, 0.0.0.0, 255.0.0.0, 46.157.76.180, ::, ::, ::, ::, ::, ::, ::, ::\r\n",
      1, "46.157.76.179", 32, "46.157.76.180", "193.213.112.4", "130.67.15.198",
      NULL, NULL },

    { "%IPDPADDR: 1, 0.0.0.0, 0.0.0.0, 193.213.112.4, 130.67.15.198, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, ::, ::, 2001:4600:4:fff::52, 2001:4600:4:1fff::52, ::, ::, ::, ::",
      1, NULL, 0, NULL, NULL, NULL,
      NULL, "2001:4600:4:fff::52" },

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
            g_object_unref (ipv4);
        } else
            g_assert (ipv4 == NULL);

        /* IPv6 */
        if (ipdpaddr_tests[i].ipv6_addr || ipdpaddr_tests[i].ipv6_dns1) {
            struct in6_addr a6;
            g_assert (ipv6);

            if (ipdpaddr_tests[i].ipv6_addr) {
                g_assert_cmpstr (mm_bearer_ip_config_get_address (ipv6), ==, ipdpaddr_tests[i].ipv6_addr);
                g_assert_cmpint (mm_bearer_ip_config_get_prefix (ipv6), ==, 64);

                g_assert (inet_pton (AF_INET6, mm_bearer_ip_config_get_address (ipv6), &a6));
                if (IN6_IS_ADDR_LINKLOCAL (&a6))
                    g_assert_cmpint (mm_bearer_ip_config_get_method (ipv6), ==, MM_BEARER_IP_METHOD_DHCP);
                else
                    g_assert_cmpint (mm_bearer_ip_config_get_method (ipv6), ==, MM_BEARER_IP_METHOD_STATIC);
            } else
                g_assert_cmpint (mm_bearer_ip_config_get_method (ipv6), ==, MM_BEARER_IP_METHOD_DHCP);

            dns = mm_bearer_ip_config_get_dns (ipv6);
            g_assert (dns);
            dnslen = g_strv_length ((gchar **) dns);
            g_assert_cmpint (dnslen, ==, 1);
            g_assert_cmpstr (dns[0], ==, ipdpaddr_tests[i].ipv6_dns1);
            g_object_unref (ipv6);
        } else
            g_assert (ipv6 == NULL);
    }
}

/*****************************************************************************/
/* Test %IPDPCFG responses */

static void
test_ipdpcfg (void)
{
    MM3gppProfile *profile;
    GList         *profiles = NULL;
    GList         *l;
    GError        *error = NULL;
    gboolean       result;
    gboolean       cid_1_validated = FALSE;
    gboolean       cid_2_validated = FALSE;
    gboolean       cid_5_validated = FALSE;
    const gchar   *response =
        "%IPDPCFG: 1,0,0,,,0\r\n"
        "%IPDPCFG: 2,0,1,\"aaaa\",\"bbbbb\",0\r\n"
        "%IPDPCFG: 5,0,2,\"user\",\"pass\",0"; /* last line without CRLF */

    profile = mm_3gpp_profile_new ();
    mm_3gpp_profile_set_profile_id (profile, 1);
    mm_3gpp_profile_set_apn (profile, "internet");
    mm_3gpp_profile_set_ip_type (profile, MM_BEARER_IP_FAMILY_IPV4);
    profiles = g_list_append (profiles, profile);

    profile = mm_3gpp_profile_new ();
    mm_3gpp_profile_set_profile_id (profile, 2);
    mm_3gpp_profile_set_apn (profile, "internet2");
    mm_3gpp_profile_set_ip_type (profile, MM_BEARER_IP_FAMILY_IPV4V6);
    profiles = g_list_append (profiles, profile);

    profile = mm_3gpp_profile_new ();
    mm_3gpp_profile_set_profile_id (profile, 5);
    mm_3gpp_profile_set_apn (profile, "internet3");
    mm_3gpp_profile_set_ip_type (profile, MM_BEARER_IP_FAMILY_IPV6);
    profiles = g_list_append (profiles, profile);

    result = mm_icera_parse_ipdpcfg_query_response (response, profiles, NULL, &error);
    g_assert_no_error (error);
    g_assert (result);

    for (l = profiles; l; l = g_list_next (l)) {
        MM3gppProfile *iter = l->data;

        switch (mm_3gpp_profile_get_profile_id (iter)) {
            case 1:
                cid_1_validated = TRUE;
                g_assert_cmpuint (mm_3gpp_profile_get_allowed_auth (iter), ==, MM_BEARER_ALLOWED_AUTH_NONE);
                g_assert (!mm_3gpp_profile_get_user (iter));
                g_assert (!mm_3gpp_profile_get_password (iter));
                break;
            case 2:
                cid_2_validated = TRUE;
                g_assert_cmpuint (mm_3gpp_profile_get_allowed_auth (iter), ==, MM_BEARER_ALLOWED_AUTH_PAP);
                g_assert_cmpstr (mm_3gpp_profile_get_user (iter), ==, "aaaa");
                g_assert_cmpstr (mm_3gpp_profile_get_password (iter), ==, "bbbbb");
                break;
            case 5:
                cid_5_validated = TRUE;
                g_assert_cmpuint (mm_3gpp_profile_get_allowed_auth (iter), ==, MM_BEARER_ALLOWED_AUTH_CHAP);
                g_assert_cmpstr (mm_3gpp_profile_get_user (iter), ==, "user");
                g_assert_cmpstr (mm_3gpp_profile_get_password (iter), ==, "pass");
                break;
            default:
                g_assert_not_reached ();
        }
    }
    g_assert (cid_1_validated);
    g_assert (cid_2_validated);
    g_assert (cid_5_validated);

    g_list_free_full (profiles, (GDestroyNotify)g_object_unref);
}

/*****************************************************************************/

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/MM/icera/ipdpaddr", test_ipdpaddr);
    g_test_add_func ("/MM/icera/ipdpcfg",  test_ipdpcfg);

    return g_test_run ();
}
