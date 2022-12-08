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
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2012 - 2013 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2014 Dan Williams <dcbw@redhat.com>
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-icera.h"

/*****************************************************************************/
/* %IPDPADDR response parser */

static MMBearerIpConfig *
parse_ipdpaddr_v4 (const gchar **items, guint num_items, GError **error)
{
    MMBearerIpConfig *config;
    const gchar *dns[3] = { 0 };
    guint dns_i = 0, tmp;
    const gchar *netmask = NULL;

    /* IP address and prefix */
    tmp = 0;
    if (!inet_pton (AF_INET, items[1], &tmp)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't parse IPv4 address '%s'", items[1]);
        return NULL;
    }

    if (!tmp) {
        /* No IPv4 config */
        return NULL;
    }

    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_STATIC);
    mm_bearer_ip_config_set_address (config, items[1]);
    mm_bearer_ip_config_set_prefix (config, 32); /* default prefix */

    /* Gateway */
    tmp = 0;
    if (inet_pton (AF_INET, items[2], &tmp)) {
        if (tmp)
            mm_bearer_ip_config_set_gateway (config, items[2]);
    } else {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't parse gateway address '%s'", items[2]);
        goto error;
    }

    /* DNS */
    tmp = 0;
    if (inet_pton (AF_INET, items[3], &tmp) && tmp)
        dns[dns_i++] = items[3];
    else {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Couldn't parse DNS address '%s'", items[3]);
        goto error;
    }

    /* DNS2 - sometimes missing and set to 0.0.0.0 */
    tmp = 0;
    if (inet_pton (AF_INET, items[4], &tmp) && tmp)
        dns[dns_i++] = items[4];
    if (dns_i > 0)
        mm_bearer_ip_config_set_dns (config, (const gchar **) dns);

    /* Short form (eg, Sierra USB305) */
    if (num_items < 9)
        return config;

    /* Devices return netmask and secondary gateway in one of two
     * positions.  The netmask may be either at index 7 or 8, while
     * the secondary gateway may be at position 8 or 9.
     */

    if (items[7] && strstr (items[7], "255.") && !strstr (items[7], "255.0.0.0"))
        netmask = items[7];
    if (items[8] && strstr (items[8], "255.") && !strstr (items[8], "255.0.0.0"))
        netmask = items[8];
    if (netmask) {
        if (!inet_pton (AF_INET, netmask, &tmp)) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse netmask '%s'",
                         netmask);
            goto error;
        }
        mm_bearer_ip_config_set_prefix (config, mm_netmask_to_cidr (netmask));
    }

    /* Secondary gateway */
    if (!mm_bearer_ip_config_get_gateway (config)) {
        const char *gw2 = NULL;

        if (num_items >= 10 && items[9] && !strstr (items[9], "255.") && !strstr (items[9], "::"))
            gw2 = items[9];
        /* Prefer position 8 */
        if (items[8] && !strstr (items[8], "255."))
            gw2 = items[8];

        if (gw2 && inet_pton (AF_INET, gw2, &tmp) && tmp)
            mm_bearer_ip_config_set_gateway (config, gw2);
        else {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse secondary gateway address '%s'",
                         gw2 ? gw2 : "(unknown)");
            goto error;
        }
    }

    return config;

error:
    g_object_unref (config);
    return NULL;
}

static MMBearerIpConfig *
parse_ipdpaddr_v6 (const gchar **items, guint num_items, GError **error)
{
    MMBearerIpConfig *config;
    const gchar *dns[2] = { 0 };
    struct in6_addr tmp6 = IN6ADDR_ANY_INIT;

    if (num_items < 12)
        return NULL;

    /* No IPv6 IP and no IPv6 DNS, return NULL without error. */
    if (g_strcmp0 (items[9], "::") == 0 && g_strcmp0 (items[11], "::") == 0)
        return NULL;

    config = mm_bearer_ip_config_new ();

    /* It appears that for IPv6 %IPDPADDR returns only the expected
     * link-local address and a DNS address, and that to retrieve the
     * default router, extra DNS, and search domains, the host must listen
     * for IPv6 Router Advertisements on the net port.
     */
    if (g_strcmp0 (items[9], "::") != 0) {
        mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_STATIC);
        /* IP address and prefix */
        if (inet_pton (AF_INET6, items[9], &tmp6) != 1 ||
            IN6_IS_ADDR_UNSPECIFIED (&tmp6)) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse IPv6 address '%s'", items[9]);
            goto error;
        }
        mm_bearer_ip_config_set_address (config, items[9]);
        mm_bearer_ip_config_set_prefix (config, 64);

        /* If the address is a link-local one, then SLAAC or DHCP must be used
         * to get the real prefix and address.  Change the method to DHCP to
         * indicate this to clients.
         */
        if (IN6_IS_ADDR_LINKLOCAL (&tmp6))
            mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_DHCP);
    } else {
        /* No IPv6 given, but DNS will be available, try with DHCP */
        mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_DHCP);
    }

    /* DNS server */
    if (g_strcmp0 (items[11], "::") != 0) {
        memset (&tmp6, 0, sizeof (tmp6));
        if (inet_pton (AF_INET6, items[11], &tmp6) == 1 &&
            !IN6_IS_ADDR_UNSPECIFIED (&tmp6)) {
            dns[0] = items[11];
            dns[1] = NULL;
            mm_bearer_ip_config_set_dns (config, (const gchar **) dns);
        } else {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "Couldn't parse DNS address '%s'", items[11]);
            goto error;
        }
    }

    return config;

error:
    g_object_unref (config);
    return NULL;
}

#define IPDPADDR_TAG "%IPDPADDR: "

gboolean
mm_icera_parse_ipdpaddr_response (const gchar *response,
                                  guint expected_cid,
                                  MMBearerIpConfig **out_ip4_config,
                                  MMBearerIpConfig **out_ip6_config,
                                  GError **error)
{
    MMBearerIpConfig *ip4_config = NULL;
    MMBearerIpConfig *ip6_config = NULL;
    GError *local = NULL;
    gboolean success = FALSE;
    char **items;
    guint num_items, i;
    guint num;

    g_return_val_if_fail (out_ip4_config, FALSE);
    g_return_val_if_fail (out_ip6_config, FALSE);

    if (!response || !g_str_has_prefix (response, IPDPADDR_TAG)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing %%IPDPADDR prefix");
        return FALSE;
    }

    /* %IPDPADDR: <cid>,<ip>,<gw>,<dns1>,<dns2>[,<nbns1>,<nbns2>[,<??>,<netmask>,<gw>]]
     * %IPDPADDR: <cid>,<ip>,<gw>,<dns1>,<dns2>,<nbns1>,<nbns2>,<netmask>,<gw>
     * %IPDPADDR: <cid>,<ip>,<gw>,<dns1>,<dns2>,<nbns1>,<nbns2>,<??>,<gw>,<ip6>,::,<ip6_dns1>,::,::,::,::,::
     *
     * Sierra USB305: %IPDPADDR: 2, 21.93.217.11, 21.93.217.10, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0
     * K3805-Z: %IPDPADDR: 2, 21.93.217.11, 21.93.217.10, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0, 255.0.0.0, 255.255.255.0, 21.93.217.10,
     * Nokia 21M: %IPDPADDR: 2, 33.196.7.127, 33.196.7.128, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0, 255.0.0.0, 33.196.7.128, fe80::f:9135:5901, ::, fd00:976a::9, ::, ::, ::, ::, ::
     * Nokia 21M: %IPDPADDR: 3, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, 0.0.0.0, fe80::2e:437b:7901, ::, fd00:976a::9, ::, ::, ::, ::, ::
     */
    response = mm_strip_tag (response, IPDPADDR_TAG);
    items = g_strsplit_set (response, ",", 0);

    /* Strip any spaces on elements; inet_pton() doesn't like them */
    num_items = g_strv_length (items);
    for (i = 0; i < num_items; i++)
        items[i] = g_strstrip (items[i]);

    if (num_items < 7) {
        g_set_error_literal (error,
                             MM_CORE_ERROR,
                             MM_CORE_ERROR_FAILED,
                             "Malformed IPDPADDR response (not enough items)");
        goto out;
    }

    /* Validate context ID */
    if (!mm_get_uint_from_str (items[0], &num) ||
        num != expected_cid) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "Unknown CID in IPDPADDR response (got %d, expected %d)",
                     (guint) num,
                     expected_cid);
        goto out;
    }

    ip4_config = parse_ipdpaddr_v4 ((const gchar **) items, num_items, &local);
    if (local) {
        g_propagate_error (error, local);
        goto out;
    }

    ip6_config = parse_ipdpaddr_v6 ((const gchar **) items, num_items, &local);
    if (local) {
        g_propagate_error (error, local);
        goto out;
    }

    success = TRUE;

out:
    g_strfreev (items);

    *out_ip4_config = ip4_config;
    *out_ip6_config = ip6_config;
    return success;
}

/*****************************************************************************/
/* %IPDPCFG? response parser.
 * Modifies the input list of profiles in place
 *
 * AT%IPDPCFG?
 *   %IPDPCFG: 1,0,0,,,0
 *   %IPDPCFG: 2,0,0,,,0
 *   %IPDPCFG: 3,0,2,"user","pass",0
 *   %IPDPCFG: 4,0,0,,,0
 *   OK
 */

gboolean
mm_icera_parse_ipdpcfg_query_response (const gchar  *str,
                                       GList        *profiles,
                                       gpointer      log_object,
                                       GError      **error)
{
    g_autoptr(GRegex)     r = NULL;
    g_autoptr(GError)     inner_error = NULL;
    g_autoptr(GMatchInfo) match_info  = NULL;
    guint                 n_updates = 0;
    guint                 n_profiles;

    n_profiles = g_list_length (profiles);

    r = g_regex_new ("%IPDPCFG:\\s*(\\d+),(\\d+),(\\d+),([^,]*),([^,]*),(\\d+)",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, NULL);
    g_assert (r != NULL);

    g_regex_match_full (r, str, strlen (str), 0, 0, &match_info, &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    /* Parse the results */
    while (g_match_info_matches (match_info)) {
        guint                cid;
        guint                auth;
        MMBearerAllowedAuth  allowed_auth;
        g_autofree gchar    *user = NULL;
        g_autofree gchar    *password = NULL;
        GList               *l;

        if (!mm_get_uint_from_match_info (match_info, 1, &cid)) {
            mm_obj_warn (log_object, "couldn't parse cid from %%IPDPCFG line");
            goto next;
        }

        if (!mm_get_uint_from_match_info (match_info, 3, &auth)) {
            mm_obj_warn (log_object, "couldn't parse auth from %%IPDPCFG line");
            goto next;
        }

        switch (auth) {
            case 0:
                allowed_auth = MM_BEARER_ALLOWED_AUTH_NONE;
                break;
            case 1:
                allowed_auth = MM_BEARER_ALLOWED_AUTH_PAP;
                break;
            case 2:
                allowed_auth = MM_BEARER_ALLOWED_AUTH_CHAP;
                break;
            default:
                mm_obj_warn (log_object, "unexpected icera auth setting: %u", auth);
                goto next;
        }

        user = mm_get_string_unquoted_from_match_info (match_info, 4);
        password = mm_get_string_unquoted_from_match_info (match_info, 5);

        mm_obj_dbg (log_object, "found icera auth settings for profile with id '%u'", cid);

        /* Find profile and update in place */
        for (l = profiles; l; l = g_list_next (l)) {
            MM3gppProfile *iter = l->data;

            if (mm_3gpp_profile_get_profile_id (iter) == (gint) cid) {
                n_updates++;
                mm_3gpp_profile_set_allowed_auth (iter, allowed_auth);
                mm_3gpp_profile_set_user (iter, user);
                mm_3gpp_profile_set_password (iter, password);
                break;
            }
        }
        if (!l)
            mm_obj_warn (log_object, "couldn't update auth settings in profile with id '%d': not found", cid);

    next:
        g_match_info_next (match_info, NULL);
    }

    if (n_updates != n_profiles)
        mm_obj_warn (log_object, "couldn't update auth settings in all profiles: %u/%u updated",
                     n_updates, n_profiles);

    return TRUE;
}
