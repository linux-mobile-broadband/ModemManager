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
 * Copyright (C) 2016 Aleksander Morgado <aleksander@aleksander.es>
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-ublox.h"

/*****************************************************************************/
/* Test UUSBCONF? responses */

typedef struct {
    const gchar       *str;
    MMUbloxUsbProfile  profile;
} UusbconfResponseTest;

static const UusbconfResponseTest uusbconf_response_tests[] = {
    {
        .str     = "+UUSBCONF: 3,\"RNDIS\",,\"0x1146\"\r\n",
        .profile = MM_UBLOX_USB_PROFILE_RNDIS
    },
    {
        .str     = "+UUSBCONF: 2,\"ECM\",,\"0x1143\"\r\n",
        .profile = MM_UBLOX_USB_PROFILE_ECM
    },
    {
        .str     = "+UUSBCONF: 0,\"\",,\"0x1141\"\r\n",
        .profile = MM_UBLOX_USB_PROFILE_BACK_COMPATIBLE
    },
};

static void
test_uusbconf_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (uusbconf_response_tests); i++) {
        MMUbloxUsbProfile profile = MM_UBLOX_USB_PROFILE_UNKNOWN;
        GError *error = NULL;
        gboolean success;

        success = mm_ublox_parse_uusbconf_response (uusbconf_response_tests[i].str, &profile, &error);
        g_assert_no_error (error);
        g_assert (success);
        g_assert_cmpuint (uusbconf_response_tests[i].profile, ==, profile);
    }
}

/*****************************************************************************/
/* Test UBMCONF? responses */

typedef struct {
    const gchar           *str;
    MMUbloxNetworkingMode  mode;
} UbmconfResponseTest;

static const UbmconfResponseTest ubmconf_response_tests[] = {
    {
        .str  = "+UBMCONF: 1\r\n",
        .mode = MM_UBLOX_NETWORKING_MODE_ROUTER
    },
    {
        .str  = "+UBMCONF: 2\r\n",
        .mode = MM_UBLOX_NETWORKING_MODE_BRIDGE
    },
};

static void
test_ubmconf_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (ubmconf_response_tests); i++) {
        MMUbloxNetworkingMode mode = MM_UBLOX_NETWORKING_MODE_UNKNOWN;
        GError *error = NULL;
        gboolean success;

        success = mm_ublox_parse_ubmconf_response (ubmconf_response_tests[i].str, &mode, &error);
        g_assert_no_error (error);
        g_assert (success);
        g_assert_cmpuint (ubmconf_response_tests[i].mode, ==, mode);
    }
}

/*****************************************************************************/
/* Test UIPADDR=N responses */

typedef struct {
    const gchar *str;
    guint        cid;
    const gchar *if_name;
    const gchar *ipv4_address;
    const gchar *ipv4_subnet;
    const gchar *ipv6_global_address;
    const gchar *ipv6_link_local_address;
} UipaddrResponseTest;

static const UipaddrResponseTest uipaddr_response_tests[] = {
    {
        .str = "+UIPADDR: 1,\"ccinet0\",\"5.168.120.13\",\"255.255.255.0\",\"\",\"\"",
        .cid = 1,
        .if_name = "ccinet0",
        .ipv4_address = "5.168.120.13",
        .ipv4_subnet = "255.255.255.0",
    },
    {
        .str = "+UIPADDR: 2,\"ccinet1\",\"\",\"\",\"2001::1:200:FF:FE00:0/64\",\"FE80::200:FF:FE00:0/64\"",
        .cid = 2,
        .if_name = "ccinet1",
        .ipv6_global_address = "2001::1:200:FF:FE00:0/64",
        .ipv6_link_local_address = "FE80::200:FF:FE00:0/64",
    },
    {
        .str = "+UIPADDR: 3,\"ccinet2\",\"5.10.100.2\",\"255.255.255.0\",\"2001::1:200:FF:FE00:0/64\",\"FE80::200:FF:FE00:0/64\"",
        .cid = 3,
        .if_name = "ccinet2",
        .ipv4_address = "5.10.100.2",
        .ipv4_subnet = "255.255.255.0",
        .ipv6_global_address = "2001::1:200:FF:FE00:0/64",
        .ipv6_link_local_address = "FE80::200:FF:FE00:0/64",
    },
};

static void
test_uipaddr_response (void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (uipaddr_response_tests); i++) {
        GError   *error = NULL;
        gboolean  success;
        guint     cid = G_MAXUINT;
        gchar    *if_name = NULL;
        gchar    *ipv4_address = NULL;
        gchar    *ipv4_subnet = NULL;
        gchar    *ipv6_global_address = NULL;
        gchar    *ipv6_link_local_address = NULL;

        success = mm_ublox_parse_uipaddr_response (uipaddr_response_tests[i].str,
                                                   &cid,
                                                   &if_name,
                                                   &ipv4_address,
                                                   &ipv4_subnet,
                                                   &ipv6_global_address,
                                                   &ipv6_link_local_address,
                                                   &error);
        g_assert_no_error (error);
        g_assert (success);
        g_assert_cmpuint (uipaddr_response_tests[i].cid,                     ==, cid);
        g_assert_cmpstr  (uipaddr_response_tests[i].if_name,                 ==, if_name);
        g_assert_cmpstr  (uipaddr_response_tests[i].ipv4_address,            ==, ipv4_address);
        g_assert_cmpstr  (uipaddr_response_tests[i].ipv4_subnet,             ==, ipv4_subnet);
        g_assert_cmpstr  (uipaddr_response_tests[i].ipv6_global_address,     ==, ipv6_global_address);
        g_assert_cmpstr  (uipaddr_response_tests[i].ipv6_link_local_address, ==, ipv6_link_local_address);

        g_free (if_name);
        g_free (ipv4_address);
        g_free (ipv4_subnet);
        g_free (ipv6_global_address);
        g_free (ipv6_link_local_address);
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

    g_test_add_func ("/MM/ublox/uusbconf/response", test_uusbconf_response);
    g_test_add_func ("/MM/ublox/ubmconf/response",  test_ubmconf_response);
    g_test_add_func ("/MM/ublox/uipaddr/response",  test_uipaddr_response);

    return g_test_run ();
}
