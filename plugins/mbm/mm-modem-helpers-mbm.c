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

#include "mm-modem-helpers.h"
#include "mm-modem-helpers-mbm.h"

/*****************************************************************************/
/* *E2IPCFG response parser */

static gboolean
validate_address (int family, const char *addr)
{
    struct in6_addr tmp6 = IN6ADDR_ANY_INIT;

    if (inet_pton (family, addr, (void *) &tmp6) != 1)
{
g_message ("%s: famil '%s'", __func__, addr);
        return FALSE;
}
    if ((family == AF_INET6) && IN6_IS_ADDR_UNSPECIFIED (&tmp6))
        return FALSE;
    return TRUE;
}

#define E2IPCFG_TAG "*E2IPCFG"

gboolean
mm_mbm_parse_e2ipcfg_response (const gchar *response,
                               MMBearerIpConfig **out_ip4_config,
                               MMBearerIpConfig **out_ip6_config,
                               GError **error)
{
    MMBearerIpConfig **ip_config = NULL;
    gboolean got_address = FALSE, got_gw = FALSE, got_dns = FALSE;
    GRegex *r;
    GMatchInfo *match_info = NULL;
    GError *match_error = NULL;
    gchar *dns[3] = { 0 };
    guint dns_idx = 0;
    int family = AF_INET;
    MMBearerIpMethod method = MM_BEARER_IP_METHOD_STATIC;

    g_return_val_if_fail (out_ip4_config, FALSE);
    g_return_val_if_fail (out_ip6_config, FALSE);

    if (!response || !g_str_has_prefix (response, E2IPCFG_TAG)) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing " E2IPCFG_TAG " prefix");
        return FALSE;
    }

    response = mm_strip_tag (response, "*E2IPCFG: ");

    if (strchr (response, ':')) {
        family = AF_INET6;
        ip_config = out_ip6_config;
        method = MM_BEARER_IP_METHOD_DHCP;
    } else if (strchr (response, '.')) {
        family = AF_INET;
        ip_config = out_ip4_config;
        method = MM_BEARER_IP_METHOD_STATIC;
    } else {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Failed to detect " E2IPCFG_TAG " address family");
        return FALSE;
    }

    /* *E2IPCFG: (1,<IP>)(2,<gateway>)(3,<DNS>)(3,<DNS>)
     * 
     * *E2IPCFG: (1,"46.157.32.246")(2,"46.157.32.243")(3,"193.213.112.4")(3,"130.67.15.198")
     * *E2IPCFG: (1,"fe80:0000:0000:0000:0000:0000:e537:1801")(3,"2001:4600:0004:0fff:0000:0000:0000:0054")(3,"2001:4600:0004:1fff:0000:0000:0000:0054")
     * *E2IPCFG: (1,"fe80:0000:0000:0000:0000:0027:b7fe:9401")(3,"fd00:976a:0000:0000:0000:0000:0000:0009")
     */
    r = g_regex_new ("\\((\\d),\"([0-9a-fA-F.:]+)\"\\)", 0, 0, NULL);
    g_assert (r != NULL);

    if (!g_regex_match_full (r, response, -1, 0, 0, &match_info, &match_error)) {
        if (match_error) {
            g_propagate_error (error, match_error);
            g_prefix_error (error, "Could not parse " E2IPCFG_TAG " results: ");
        } else {
            g_set_error_literal (error,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't match " E2IPCFG_TAG " reply");
        }
        goto done;
    }

    *ip_config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (*ip_config, method);
    while (g_match_info_matches (match_info)) {
        char *id = g_match_info_fetch (match_info, 1);
        char *str = g_match_info_fetch (match_info, 2);

        switch (atoi (id)) {
        case 1:
            if (validate_address (family, str)) {
                mm_bearer_ip_config_set_address (*ip_config, str);
                mm_bearer_ip_config_set_prefix (*ip_config, (family == AF_INET6) ? 64 : 28);
                got_address = TRUE;
            }
            break;
        case 2:
            if ((family == AF_INET) && validate_address (family, str)) {
                mm_bearer_ip_config_set_gateway (*ip_config, str);
                got_gw = TRUE;
            }
            break;
        case 3:
            if (validate_address (family, str)) {
                dns[dns_idx++] = g_strdup (str);
                got_dns = TRUE;
            }
            break;
        default:
            break;
        }
        g_free (id);
        g_free (str);
        g_match_info_next (match_info, NULL);
    }

    if (got_dns) {
        mm_bearer_ip_config_set_dns (*ip_config, (const gchar **) dns);
        g_free (dns[0]);
        g_free (dns[1]);
    }

    if (!got_address || (family == AF_INET && !got_gw)) {
        g_object_unref (*ip_config);
        *ip_config = NULL;
        g_set_error_literal (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                             "Got incomplete IP configuration from " E2IPCFG_TAG);
    }

done:
    if (match_info)
        g_match_info_free (match_info);
    g_regex_unref (r);
    return !!*ip_config;
}

