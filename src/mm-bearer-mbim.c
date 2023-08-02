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

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-modem-helpers-mbim.h"
#include "mm-port-enums-types.h"
#include "mm-bearer-mbim.h"
#include "mm-log-object.h"
#include "mm-context.h"

G_DEFINE_TYPE (MMBearerMbim, mm_bearer_mbim, MM_TYPE_BASE_BEARER)

struct _MMBearerMbimPrivate {
    MMPortMbim *mbim;
    MMPort     *data;
    MMPort     *link;
    guint32     session_id;
};

/*****************************************************************************/

static gboolean
peek_ports (gpointer              self,
            MMPortMbim          **o_mbim,
            MMPort              **o_data,
            GAsyncReadyCallback   callback,
            gpointer              user_data)
{
    g_autoptr(MMBaseModem) modem = NULL;

    g_object_get (G_OBJECT (self),
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    if (o_mbim) {
        MMPortMbim *port;

        port = mm_broadband_modem_mbim_peek_port_mbim (MM_BROADBAND_MODEM_MBIM (modem));
        if (!port) {
            g_task_report_new_error (self,
                                     callback,
                                     user_data,
                                     peek_ports,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Couldn't peek MBIM port");
            return FALSE;
        }

        *o_mbim = port;
    }

    if (o_data) {
        MMPort *port;

        /* Grab a data port */
        port = mm_base_modem_peek_best_data_port (modem, MM_PORT_TYPE_NET);
        if (!port) {
            g_task_report_new_error (self,
                                     callback,
                                     user_data,
                                     peek_ports,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_NOT_FOUND,
                                     "No valid data port found to launch connection");
            return FALSE;
        }

        *o_data = port;
    }

    return TRUE;
}

/*****************************************************************************/
/* Stats */

typedef struct {
    guint64 rx_bytes;
    guint64 tx_bytes;
} ReloadStatsResult;

static gboolean
reload_stats_finish (MMBaseBearer  *bearer,
                     guint64       *rx_bytes,
                     guint64       *tx_bytes,
                     GAsyncResult  *res,
                     GError       **error)
{
    ReloadStatsResult *stats;

    stats = g_task_propagate_pointer (G_TASK (res), error);
    if (!stats)
        return FALSE;

    if (rx_bytes)
        *rx_bytes = stats->rx_bytes;
    if (tx_bytes)
        *tx_bytes = stats->tx_bytes;

    g_free (stats);
    return TRUE;
}

static void
packet_statistics_query_ready (MbimDevice   *device,
                               GAsyncResult *res,
                               GTask        *task)
{
    GError                 *error = NULL;
    g_autoptr(MbimMessage)  response = NULL;
    guint64                 in_octets = 0;
    guint64                 out_octets = 0;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_packet_statistics_response_parse (
            response,
            NULL, /* in_discards */
            NULL, /* in_errors */
            &in_octets, /* in_octets */
            NULL, /* in_packets */
            &out_octets, /* out_octets */
            NULL, /* out_packets */
            NULL, /* out_errors */
            NULL, /* out_discards */
            &error)) {
        /* Store results */
        ReloadStatsResult *stats;

        stats = g_new (ReloadStatsResult, 1);
        stats->rx_bytes = in_octets;
        stats->tx_bytes = out_octets;
        g_task_return_pointer (task, stats, g_free);
    } else if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_OPERATION_NOT_ALLOWED)) {
        g_clear_error (&error);
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED, "operation not allowed");
    } else
        g_task_return_error (task, error);

    g_object_unref (task);
}

static void
reload_stats (MMBaseBearer        *self,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    MMPortMbim             *mbim;
    GTask                  *task;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_ports (self, &mbim, NULL, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    message = (mbim_message_packet_statistics_query_new (NULL));
    mbim_device_command (mm_port_mbim_peek_device (mbim),
                         message,
                         5,
                         NULL,
                         (GAsyncReadyCallback)packet_statistics_query_ready,
                         task);
}

/*****************************************************************************/
/* Disconnection message builder.
 */

static MbimMessage *
build_disconnect_message (MMBearerMbim *self,
                          MMPortMbim   *port,
                          guint32       session_id)
{
    if (mbim_device_check_ms_mbimex_version (mm_port_mbim_peek_device (port), 3, 0))
        return mbim_message_ms_basic_connect_v3_connect_set_new (session_id,
                                                                 MBIM_ACTIVATION_COMMAND_DEACTIVATE,
                                                                 MBIM_COMPRESSION_NONE,
                                                                 MBIM_AUTH_PROTOCOL_NONE,
                                                                 MBIM_CONTEXT_IP_TYPE_DEFAULT,
                                                                 mbim_uuid_from_context_type (MBIM_CONTEXT_TYPE_INTERNET),
                                                                 MBIM_ACCESS_MEDIA_TYPE_UNKNOWN,
                                                                 "", /* access string */
                                                                 "", /* user name */
                                                                 "", /* password */
                                                                 NULL, /* unnamed ies */
                                                                 NULL);

    return mbim_message_connect_set_new (session_id,
                                         MBIM_ACTIVATION_COMMAND_DEACTIVATE,
                                         "", /* access string */
                                         "", /* user name */
                                         "", /* password */
                                         MBIM_COMPRESSION_NONE,
                                         MBIM_AUTH_PROTOCOL_NONE,
                                         MBIM_CONTEXT_IP_TYPE_DEFAULT,
                                         mbim_uuid_from_context_type (MBIM_CONTEXT_TYPE_INTERNET),
                                         NULL);
}

/*****************************************************************************/
/* Connect */

#define WAIT_LINK_PORT_TIMEOUT_MS 2500

typedef enum {
    CONNECT_STEP_FIRST,
    CONNECT_STEP_LOAD_PROFILE_SETTINGS,
    CONNECT_STEP_SETUP_LINK,
    CONNECT_STEP_SETUP_LINK_MAIN_UP,
    CONNECT_STEP_CHECK_DISCONNECTED,
    CONNECT_STEP_ENSURE_DISCONNECTED,
    CONNECT_STEP_CONNECT,
    CONNECT_STEP_IP_CONFIGURATION,
    CONNECT_STEP_LAST
} ConnectStep;

typedef struct {
    MMPortMbim            *mbim;
    MMBroadbandModemMbim  *modem;
    ConnectStep            step;
    MMPort                *data;
    MMBearerConnectResult *connect_result;
    MbimMessage           *abort_on_failure;
    /* settings to use */
    gint                   profile_id;
    gchar                 *apn;
    MbimContextType        context_type;
    gchar                 *user;
    gchar                 *password;
    MbimAuthProtocol       auth;
    MbimContextIpType      requested_ip_type;
    MbimContextIpType      activated_ip_type;
    /* multiplex support */
    guint                  session_id;
    gchar                 *link_prefix_hint;
    gchar                 *link_name;
    MMPort                *link;
} ConnectContext;

static void
connect_context_free (ConnectContext *ctx)
{
    if (ctx->abort_on_failure) {
        mbim_device_command (mm_port_mbim_peek_device (ctx->mbim),
                             ctx->abort_on_failure,
                             MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
                             NULL, NULL, NULL);
        mbim_message_unref (ctx->abort_on_failure);
    }

    if (ctx->link_name) {
        mm_port_mbim_cleanup_link (ctx->mbim, ctx->link_name, NULL, NULL);
        g_free (ctx->link_name);
    }
    g_clear_object (&ctx->link);
    g_free (ctx->link_prefix_hint);

    g_free (ctx->apn);
    g_free (ctx->user);
    g_free (ctx->password);

    g_clear_pointer (&ctx->connect_result, (GDestroyNotify)mm_bearer_connect_result_unref);

    g_clear_object (&ctx->data);
    g_object_unref (ctx->mbim);
    g_object_unref (ctx->modem);

    g_slice_free (ConnectContext, ctx);
}

static MMBearerConnectResult *
connect_finish (MMBaseBearer  *self,
                GAsyncResult  *res,
                GError       **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void connect_context_step (GTask *task);

static void
ip_configuration_query_ready (MbimDevice   *device,
                              GAsyncResult *res,
                              GTask        *task)
{
    MMBearerMbim                     *self;
    ConnectContext                   *ctx;
    GError                           *error = NULL;
    g_autoptr(MbimMessage)            response = NULL;
    MbimIPConfigurationAvailableFlag  ipv4configurationavailable;
    MbimIPConfigurationAvailableFlag  ipv6configurationavailable;
    guint32                           ipv4addresscount;
    g_autoptr(MbimIPv4ElementArray)   ipv4address = NULL;
    guint32                           ipv6addresscount;
    g_autoptr(MbimIPv6ElementArray)   ipv6address = NULL;
    const MbimIPv4                   *ipv4gateway;
    const MbimIPv6                   *ipv6gateway;
    guint32                           ipv4dnsservercount;
    g_autofree MbimIPv4              *ipv4dnsserver = NULL;
    guint32                           ipv6dnsservercount;
    g_autofree MbimIPv6              *ipv6dnsserver = NULL;
    guint32                           ipv4mtu;
    guint32                           ipv6mtu;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_ip_configuration_response_parse (
            response,
            NULL, /* sessionid */
            &ipv4configurationavailable,
            &ipv6configurationavailable,
            &ipv4addresscount,
            &ipv4address,
            &ipv6addresscount,
            &ipv6address,
            &ipv4gateway,
            &ipv6gateway,
            &ipv4dnsservercount,
            &ipv4dnsserver,
            &ipv6dnsservercount,
            &ipv6dnsserver,
            &ipv4mtu,
            &ipv6mtu,
            &error)) {
        g_autofree gchar            *ipv4configurationavailable_str = NULL;
        g_autofree gchar            *ipv6configurationavailable_str = NULL;
        g_autoptr(MMBearerIpConfig)  ipv4_config = NULL;
        g_autoptr(MMBearerIpConfig)  ipv6_config = NULL;
        guint64                      uplink_speed = 0;
        guint64                      downlink_speed = 0;

        /* IPv4 info */

        ipv4configurationavailable_str = mbim_ip_configuration_available_flag_build_string_from_mask (ipv4configurationavailable);
        mm_obj_dbg (self, "IPv4 configuration available: '%s'", ipv4configurationavailable_str);

        if ((ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_ADDRESS) && ipv4addresscount) {
            guint i;

            mm_obj_dbg (self, "  IP addresses (%u)", ipv4addresscount);
            for (i = 0; i < ipv4addresscount; i++) {
                g_autoptr(GInetAddress)  addr = NULL;
                g_autofree gchar        *str = NULL;

                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv4address[i]->ipv4_address, G_SOCKET_FAMILY_IPV4);
                str = g_inet_address_to_string (addr);
                mm_obj_dbg (self, "    IP [%u]: '%s/%u'", i, str, ipv4address[i]->on_link_prefix_length);
            }
        }

        if ((ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY) && ipv4gateway) {
            g_autoptr(GInetAddress)  addr = NULL;
            g_autofree gchar        *str = NULL;

            addr = g_inet_address_new_from_bytes ((guint8 *)ipv4gateway, G_SOCKET_FAMILY_IPV4);
            str = g_inet_address_to_string (addr);
            mm_obj_dbg (self, "  gateway: '%s'", str);
        }

        if ((ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_DNS) && ipv4dnsservercount) {
            guint i;

            mm_obj_dbg (self, "  DNS addresses (%u)", ipv4dnsservercount);
            for (i = 0; i < ipv4dnsservercount; i++) {
                g_autoptr(GInetAddress)  addr = NULL;

                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv4dnsserver[i], G_SOCKET_FAMILY_IPV4);
                if (!g_inet_address_get_is_any (addr)) {
                    g_autofree gchar *str = NULL;

                    str = g_inet_address_to_string (addr);
                    mm_obj_dbg (self, "    DNS [%u]: '%s'", i, str);
                }
            }
        }

        if ((ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_MTU) && ipv4mtu)
            mm_obj_dbg (self, "  MTU: '%u'", ipv4mtu);

        /* IPv6 info */

        ipv6configurationavailable_str = mbim_ip_configuration_available_flag_build_string_from_mask (ipv6configurationavailable);
        mm_obj_dbg (self, "IPv6 configuration available: '%s'", ipv6configurationavailable_str);

        if ((ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_ADDRESS) && ipv6addresscount) {
            guint i;

            mm_obj_dbg (self, "  IP addresses (%u)", ipv6addresscount);
            for (i = 0; i < ipv6addresscount; i++) {
                g_autoptr(GInetAddress)  addr = NULL;
                g_autofree gchar        *str = NULL;

                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv6address[i]->ipv6_address, G_SOCKET_FAMILY_IPV6);
                str = g_inet_address_to_string (addr);
                mm_obj_dbg (self, "    IP [%u]: '%s/%u'", i, str, ipv6address[i]->on_link_prefix_length);
            }
        }

        if ((ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY) && ipv6gateway) {
            g_autoptr(GInetAddress)  addr = NULL;
            g_autofree gchar        *str = NULL;

            addr = g_inet_address_new_from_bytes ((guint8 *)ipv6gateway, G_SOCKET_FAMILY_IPV6);
            str = g_inet_address_to_string (addr);
            mm_obj_dbg (self, "  gateway: '%s'", str);
        }

        if ((ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_DNS) && ipv6dnsservercount) {
            guint i;

            mm_obj_dbg (self, "  DNS addresses (%u)", ipv6dnsservercount);
            for (i = 0; i < ipv6dnsservercount; i++) {
                g_autoptr(GInetAddress)  addr = NULL;

                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv6dnsserver[i], G_SOCKET_FAMILY_IPV6);
                if (!g_inet_address_get_is_any (addr)) {
                    g_autofree gchar *str = NULL;

                    str = g_inet_address_to_string (addr);
                    mm_obj_dbg (self, "    DNS [%u]: '%s'", i, str);
                }
            }
        }

        if ((ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_MTU) && ipv6mtu)
            mm_obj_dbg (self, "  MTU: '%u'", ipv6mtu);

        /* Build connection results */

        /* Build IPv4 config */
        if (ctx->requested_ip_type == MBIM_CONTEXT_IP_TYPE_IPV4 ||
            ctx->requested_ip_type == MBIM_CONTEXT_IP_TYPE_IPV4V6 ||
            ctx->requested_ip_type == MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6) {
            gboolean address_set = FALSE;

            ipv4_config = mm_bearer_ip_config_new ();

            /* We assume that if we have an IP we can use static configuration.
             * Not all modems or providers will return DNS servers or even a
             * gateway, and not all modems support DHCP either. The IP management
             * daemon/script just has to deal with this...
             */
            if ((ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_ADDRESS) && (ipv4addresscount > 0)) {
                g_autoptr(GInetAddress)  addr = NULL;
                g_autofree gchar        *str = NULL;

                mm_bearer_ip_config_set_method (ipv4_config, MM_BEARER_IP_METHOD_STATIC);

                /* IP address, pick the first one */
                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv4address[0]->ipv4_address, G_SOCKET_FAMILY_IPV4);
                str = g_inet_address_to_string (addr);
                mm_bearer_ip_config_set_address (ipv4_config, str);
                address_set = TRUE;

                /* Netmask */
                mm_bearer_ip_config_set_prefix (ipv4_config, ipv4address[0]->on_link_prefix_length);

                /* Gateway */
                if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY) {
                    g_autoptr(GInetAddress)  gw_addr = NULL;
                    g_autofree gchar        *gw_str = NULL;

                    gw_addr = g_inet_address_new_from_bytes ((guint8 *)ipv4gateway, G_SOCKET_FAMILY_IPV4);
                    gw_str = g_inet_address_to_string (gw_addr);
                    mm_bearer_ip_config_set_gateway (ipv4_config, gw_str);
                }
            } else
                mm_bearer_ip_config_set_method (ipv4_config, MM_BEARER_IP_METHOD_DHCP);

            /* DNS */
            if ((ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_DNS) && (ipv4dnsservercount > 0)) {
                g_auto(GStrv) strarr = NULL;
                guint         i;
                guint         n;

                strarr = g_new0 (gchar *, ipv4dnsservercount + 1);
                for (i = 0, n = 0; i < ipv4dnsservercount; i++) {
                    g_autoptr(GInetAddress) addr = NULL;

                    addr = g_inet_address_new_from_bytes ((guint8 *)&ipv4dnsserver[i], G_SOCKET_FAMILY_IPV4);
                    if (!g_inet_address_get_is_any (addr))
                        strarr[n++] = g_inet_address_to_string (addr);
                }
                mm_bearer_ip_config_set_dns (ipv4_config, (const gchar **)strarr);
            }

            /* MTU */
            if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_MTU)
                mm_bearer_ip_config_set_mtu (ipv4_config, ipv4mtu);

            /* We requested IPv4, but it wasn't reported as activated. If there is no IP address
             * provided by the modem, we assume the IPv4 bearer wasn't truly activated */
            if (!address_set &&
                ctx->activated_ip_type != MBIM_CONTEXT_IP_TYPE_IPV4 &&
                ctx->activated_ip_type != MBIM_CONTEXT_IP_TYPE_IPV4V6 &&
                ctx->activated_ip_type != MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6) {
                mm_obj_dbg (self, "IPv4 requested but no IPv4 activated and no IPv4 address set: ignoring");
                g_clear_object (&ipv4_config);
            }
        }

        /* Build IPv6 config */
        if (ctx->requested_ip_type == MBIM_CONTEXT_IP_TYPE_IPV6 ||
            ctx->requested_ip_type == MBIM_CONTEXT_IP_TYPE_IPV4V6 ||
            ctx->requested_ip_type == MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6) {
            gboolean address_set = FALSE;

            ipv6_config = mm_bearer_ip_config_new ();

            if ((ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_ADDRESS) && (ipv6addresscount > 0)) {
                g_autoptr(GInetAddress)  addr = NULL;
                g_autofree gchar        *str = NULL;

                /* IP address, pick the first one */
                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv6address[0]->ipv6_address, G_SOCKET_FAMILY_IPV6);
                str = g_inet_address_to_string (addr);
                mm_bearer_ip_config_set_address (ipv6_config, str);
                address_set = TRUE;

                /* If the address is a link-local one, then SLAAC or DHCP must be used
                 * to get the real prefix and address.
                 * If the address is a global one, then the modem did SLAAC already and
                 * there is no need to run host SLAAC.
                 */
                if (g_inet_address_get_is_link_local (addr))
                    mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_DHCP);
                else
                    mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_STATIC);

                /* Netmask */
                mm_bearer_ip_config_set_prefix (ipv6_config, ipv6address[0]->on_link_prefix_length);

                /* Gateway */
                if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY) {
                    g_autoptr(GInetAddress)  gw_addr = NULL;
                    g_autofree gchar        *gw_str = NULL;

                    gw_addr = g_inet_address_new_from_bytes ((guint8 *)ipv6gateway, G_SOCKET_FAMILY_IPV6);
                    gw_str = g_inet_address_to_string (gw_addr);
                    mm_bearer_ip_config_set_gateway (ipv6_config, gw_str);
                }
            } else {
                /* If no address is given, this is likely a bug in the modem firmware, because even in the
                 * case of needing to run host SLAAC, a link-local IPv6 address must be given. Either way,
                 * go on requesting the need of host SLAAC, and let the network decide whether our SLAAC
                 * Router Solicitation messages with an unexpected link-local address are accepted or not. */
                mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_DHCP);
            }

            if ((ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_DNS) && (ipv6dnsservercount > 0)) {
                g_auto(GStrv) strarr = NULL;
                guint         i;
                guint         n;

                /* DNS */
                strarr = g_new0 (gchar *, ipv6dnsservercount + 1);
                for (i = 0, n = 0; i < ipv6dnsservercount; i++) {
                    g_autoptr(GInetAddress) addr = NULL;

                    addr = g_inet_address_new_from_bytes ((guint8 *)&ipv6dnsserver[i], G_SOCKET_FAMILY_IPV6);
                    if (!g_inet_address_get_is_any (addr))
                        strarr[n++] = g_inet_address_to_string (addr);
                }
                mm_bearer_ip_config_set_dns (ipv6_config, (const gchar **)strarr);
            }

            /* MTU */
            if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_MTU)
                mm_bearer_ip_config_set_mtu (ipv6_config, ipv6mtu);

            /* We requested IPv6, but it wasn't reported as activated. If there is no IPv6 address
             * provided by the modem, we assume the IPv6 bearer wasn't truly activated */
            if (!address_set &&
                ctx->activated_ip_type != MBIM_CONTEXT_IP_TYPE_IPV6 &&
                ctx->activated_ip_type != MBIM_CONTEXT_IP_TYPE_IPV4V6 &&
                ctx->activated_ip_type != MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6) {
                mm_obj_dbg (self, "IPv6 requested but no IPv6 activated and no IPv6 address set: ignoring");
                g_clear_object (&ipv6_config);
            }
        }

        /* Store result */
        ctx->connect_result = mm_bearer_connect_result_new (ctx->link ? ctx->link : ctx->data,
                                                            ipv4_config,
                                                            ipv6_config);
        mm_bearer_connect_result_set_multiplexed (ctx->connect_result, !!ctx->link);

        if (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN)
            mm_bearer_connect_result_set_profile_id (ctx->connect_result, ctx->profile_id);

        /* Propagate speeds from modem object */
        mm_broadband_modem_mbim_get_speeds (ctx->modem, &uplink_speed, &downlink_speed);
        mm_bearer_connect_result_set_uplink_speed (ctx->connect_result, uplink_speed);
        mm_bearer_connect_result_set_downlink_speed (ctx->connect_result, downlink_speed);
    }

    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (task);
}

static void
connect_set_ready (MbimDevice   *device,
                   GAsyncResult *res,
                   GTask        *task)
{
    MMBearerMbim           *self;
    ConnectContext         *ctx;
    GError                 *error = NULL;
    g_autoptr(GError)       inner_error = NULL;
    g_autoptr(MbimMessage)  response = NULL;
    guint32                 session_id;
    MbimActivationState     activation_state;
    guint32                 nw_error;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* always parse, because on failure we also check the NwError */
    mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_v3_connect_response_parse (
                response,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                &ctx->activated_ip_type,
                NULL, /* context_type */
                &nw_error,
                NULL, /* media_preference */
                NULL, /* access_string */
                NULL, /* unnamed_ies */
                &inner_error))
            g_prefix_error (&inner_error, "Failed processing MBIMEx v3.0 connect response: ");
        else
            mm_obj_dbg (self, "processed MBIMEx v3.0 connect response");
    } else {
        if (!mbim_message_connect_response_parse (
                response,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                &ctx->activated_ip_type,
                NULL, /* context_type */
                &nw_error,
                &inner_error))
            g_prefix_error (&inner_error, "Failed processing connect response: ");
        else
            mm_obj_dbg (self, "processed connect response");
    }

    /* Prefer the error from the result to the parsing error */
    if (inner_error) {
        if (!error)
            error = g_steal_pointer (&inner_error);
    } else {
        /* Report the IP type we asked for and the one returned by the modem */
        mm_obj_dbg (self, "session ID '%u': %s (requested IP type: %s, activated IP type: %s, nw error: %s)",
                    session_id,
                    mbim_activation_state_get_string (activation_state),
                    mbim_context_ip_type_get_string (ctx->requested_ip_type),
                    mbim_context_ip_type_get_string (ctx->activated_ip_type),
                    mbim_nw_error_get_string (nw_error));
        /* If the response reports an ACTIVATED state, we're good even if
         * there is a nw_error set (e.g. asking for IPv4v6 may return a
         * 'pdp-type-ipv4-only-allowed' nw_error).
         * If the nw_error is not set (MBIM_NW_ERROR_NONE), we prefer to
         * return any operation error (e.g. 'OperationNotAllowed') instead
         * of MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN ('Unknown error'). */
        if (nw_error != MBIM_NW_ERROR_NONE &&
            activation_state != MBIM_ACTIVATION_STATE_ACTIVATED &&
            activation_state != MBIM_ACTIVATION_STATE_ACTIVATING) {
            g_clear_error (&error);
            error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error, self);
        }
    }

    if (error) {
        /* A timeout when attempting to activate the request will require us to
         * explicitly abort the operation */
        if (g_error_matches (error, MBIM_CORE_ERROR, MBIM_CORE_ERROR_TIMEOUT))
            ctx->abort_on_failure = build_disconnect_message (self, ctx->mbim, ctx->session_id);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Keep on. From now on, any additional command failure will require an
     * explicit disconnection */
    ctx->abort_on_failure = build_disconnect_message (self, ctx->mbim, ctx->session_id);
    ctx->step++;
    connect_context_step (task);
}

static void
ensure_disconnected_ready (MbimDevice   *device,
                           GAsyncResult *res,
                           GTask        *task)
{
    ConnectContext         *ctx;
    g_autoptr(MbimMessage)  response = NULL;

    ctx = g_task_get_task_data (task);

    /* Ignore all errors, just go on */
    response = mbim_device_command_finish (device, res, NULL);

    /* Keep on */
    ctx->step++;
    connect_context_step (task);
}

static void
check_disconnected_ready (MbimDevice   *device,
                          GAsyncResult *res,
                          GTask        *task)
{
    MMBearerMbim           *self;
    ConnectContext         *ctx;
    g_autoptr(MbimMessage)  response = NULL;
    guint32                 session_id;
    MbimActivationState     activation_state = MBIM_ACTIVATION_STATE_UNKNOWN;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, NULL);
    if (response && mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, NULL)) {
        if (mbim_device_check_ms_mbimex_version (device, 3, 0))
            mbim_message_ms_basic_connect_v3_connect_response_parse (
                response,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                NULL, /* ip_type */
                NULL, /* context_type */
                NULL, /* nw_error */
                NULL, /* media_preference */
                NULL, /* access_string */
                NULL, /* unnamed_ies */
                NULL);
        else
            mbim_message_connect_response_parse (
                response,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                NULL, /* ip_type */
                NULL, /* context_type */
                NULL, /* nw_error */
                NULL);
    }

    if (activation_state != MBIM_ACTIVATION_STATE_UNKNOWN)
        mm_obj_dbg (self, "session ID '%u': %s", session_id, mbim_activation_state_get_string (activation_state));

    /* Some modem (e.g. Huawei ME936) reports MBIM_ACTIVATION_STATE_UNKNOWN
     * when being queried for the activation state before an IP session has
     * been activated once. Here we expect a modem would at least tell the
     * truth when the session has been activated, so we proceed to deactivate
     * the session only the modem indicates the session has been activated or
     * is being activated.
     */
    if (activation_state == MBIM_ACTIVATION_STATE_ACTIVATED || activation_state == MBIM_ACTIVATION_STATE_ACTIVATING)
        ctx->step = CONNECT_STEP_ENSURE_DISCONNECTED;
    else
        ctx->step = CONNECT_STEP_CONNECT;

    connect_context_step (task);
}

static void
main_interface_up_ready (MMPortNet    *link,
                         GAsyncResult *res,
                         GTask        *task)
{
    ConnectContext *ctx;
    GError         *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_port_net_link_setup_finish (link, res, &error)) {
        g_prefix_error (&error, "Couldn't bring main interface up: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (task);
}

static void
wait_link_port_ready (MMBaseModem  *modem,
                      GAsyncResult *res,
                      GTask        *task)
{
    ConnectContext *ctx;
    GError         *error = NULL;

    ctx = g_task_get_task_data (task);

    ctx->link = mm_base_modem_wait_link_port_finish (modem, res, &error);
    if (!ctx->link) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (task);
}

static void
setup_link_ready (MMPortMbim    *mbim,
                  GAsyncResult  *res,
                  GTask         *task)
{
    MMBearerMbim           *self;
    ConnectContext         *ctx;
    GError                 *error = NULL;
    g_autoptr(MMBaseModem)  modem  = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    ctx->link_name = mm_port_mbim_setup_link_finish (mbim, res, &ctx->session_id, &error);
    if (!ctx->link_name) {
        g_prefix_error (&error, "failed to create net link for device: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* From now on link_name will be set, and we'll use that to know
     * whether we should cleanup the link upon a connection failure */
    mm_obj_msg (self, "net link %s created (session id %u)", ctx->link_name, ctx->session_id);

    /* Wait for the data port with the given interface name, which will be
     * added asynchronously */
    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem);

    mm_base_modem_wait_link_port (modem,
                                  "net",
                                  ctx->link_name,
                                  WAIT_LINK_PORT_TIMEOUT_MS,
                                  (GAsyncReadyCallback) wait_link_port_ready,
                                  task);
}

static gboolean
load_settings_from_profile (MMBearerMbim    *self,
                            ConnectContext  *ctx,
                            MM3gppProfile   *profile,
                            MMBearerApnType  default_apn_type,
                            GError         **error)
{
    MMBearerAllowedAuth     bearer_auth;
    MMBearerApnType         apn_type;
    GError                 *inner_error = NULL;
    g_autoptr(MMBaseModem)  modem  = NULL;

    g_object_get (self, MM_BASE_BEARER_MODEM,  &modem, NULL);
    g_assert (modem);

    /* APN settings */
    ctx->apn = g_strdup (mm_3gpp_profile_get_apn (profile));
    apn_type = mm_3gpp_profile_get_apn_type (profile);
    if (apn_type == MM_BEARER_APN_TYPE_NONE) {
        if (default_apn_type == MM_BEARER_APN_TYPE_NONE) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                         "APN type in profile is not initialized");
            return FALSE;
        }
        apn_type = default_apn_type;
    }
    ctx->context_type = mm_bearer_apn_type_to_mbim_context_type (
                            apn_type,
                            mm_broadband_modem_mbim_is_context_type_ext_supported (MM_BROADBAND_MODEM_MBIM (modem)),
                            self,
                            &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return FALSE;
    }

    /* Auth settings */
    ctx->user = g_strdup (mm_3gpp_profile_get_user (profile));
    ctx->password = g_strdup (mm_3gpp_profile_get_password (profile));
    if (!ctx->user && !ctx->password) {
        ctx->auth = MBIM_AUTH_PROTOCOL_NONE;
    } else {
        bearer_auth = mm_3gpp_profile_get_allowed_auth (profile);
        ctx->auth = mm_bearer_allowed_auth_to_mbim_auth_protocol (bearer_auth, self, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
    }

    /* This IP type reading is applicable only when the profile comes
     * from the input bearer properties, as there is no IP type stored
     * in the device profiles. Therefore, only read it if it hasn't been
     * read yet */
    if (ctx->requested_ip_type == MBIM_CONTEXT_IP_TYPE_DEFAULT) {
        MMBearerIpFamily ip_type;

        ip_type = mm_3gpp_profile_get_ip_type (profile);
        mm_3gpp_normalize_ip_family (&ip_type);
        ctx->requested_ip_type = mm_bearer_ip_family_to_mbim_context_ip_type (ip_type, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
    }

    return TRUE;
}

static void
get_profile_ready (MMIfaceModem3gppProfileManager *modem,
                   GAsyncResult                   *res,
                   GTask                          *task)
{
    MMBearerMbim             *self;
    ConnectContext           *ctx;
    GError                   *error = NULL;
    g_autoptr(MM3gppProfile)  profile = NULL;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    profile = mm_iface_modem_3gpp_profile_manager_get_profile_finish (modem, res, &error);
    if (!profile) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!load_settings_from_profile (self, ctx, profile, MM_BEARER_APN_TYPE_NONE, &error)) {
        g_prefix_error (&error, "Couldn't load settings from profile: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (task);
}

static void
connect_context_step (GTask *task)
{
    MMBearerMbim           *self;
    ConnectContext         *ctx;
    g_autoptr(MbimMessage)  message = NULL;

    /* If cancelled, complete */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case CONNECT_STEP_FIRST:
        ctx->step++;
        /* Fall through */

    case CONNECT_STEP_LOAD_PROFILE_SETTINGS:
        if (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN) {
            mm_obj_dbg (self, "loading connection settings from profile '%d'...", ctx->profile_id);
            mm_iface_modem_3gpp_profile_manager_get_profile (
                MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (ctx->modem),
                ctx->profile_id,
                (GAsyncReadyCallback)get_profile_ready,
                task);
            return;
        }
        ctx->step++;
        /* Fall through */

    case CONNECT_STEP_SETUP_LINK:
        /* if a link prefix hint is available, it's because we should be doing
         * multiplexing */
        if (ctx->link_prefix_hint) {
            mm_obj_dbg (self, "setting up new multiplexed link...");
            mm_port_mbim_setup_link (ctx->mbim,
                                     ctx->data,
                                     ctx->link_prefix_hint,
                                     (GAsyncReadyCallback) setup_link_ready,
                                     task);
            return;
        }
        ctx->step++;
        /* fall through */

    case CONNECT_STEP_SETUP_LINK_MAIN_UP:
        /* if the connection is done through a new link, we need to ifup the main interface */
        if (ctx->link) {
            mm_obj_dbg (self, "bringing main interface %s up...", mm_port_get_device (ctx->data));
            mm_port_net_link_setup (MM_PORT_NET (ctx->data),
                                    TRUE,
                                    0, /* ignore */
                                    g_task_get_cancellable (task),
                                    (GAsyncReadyCallback) main_interface_up_ready,
                                    task);
            return;
        }
        ctx->step++;
        /* fall through */

    case CONNECT_STEP_CHECK_DISCONNECTED: {
        MbimDevice *device;

        mm_obj_dbg (self, "checking if session %u is disconnected...", ctx->session_id);

        device = mm_port_mbim_peek_device (ctx->mbim);
        if (mbim_device_check_ms_mbimex_version (device, 3, 0))
            message = mbim_message_ms_basic_connect_v3_connect_query_new (ctx->session_id, NULL);
        else
            message = mbim_message_connect_query_new (
                          ctx->session_id,
                          MBIM_ACTIVATION_STATE_UNKNOWN,
                          MBIM_VOICE_CALL_STATE_NONE,
                          MBIM_CONTEXT_IP_TYPE_DEFAULT,
                          mbim_uuid_from_context_type (MBIM_CONTEXT_TYPE_INTERNET),
                          0,
                          NULL);

        mbim_device_command (device,
                             message,
                             10,
                             g_task_get_cancellable (task),
                             (GAsyncReadyCallback)check_disconnected_ready,
                             task);
        return;
    }

    case CONNECT_STEP_ENSURE_DISCONNECTED:
        mm_obj_dbg (self, "ensuring session %u is disconnected...", ctx->session_id);
        message = build_disconnect_message (self, ctx->mbim, ctx->session_id);
        mbim_device_command (mm_port_mbim_peek_device (ctx->mbim),
                             message,
                             MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
                             g_task_get_cancellable (task),
                             (GAsyncReadyCallback)ensure_disconnected_ready,
                             task);
        return;

    case CONNECT_STEP_CONNECT: {
        MbimDevice *device;

        mm_obj_dbg (self, "launching %s connection in session %u...",
                    mbim_context_ip_type_get_string (ctx->requested_ip_type), ctx->session_id);

        device = mm_port_mbim_peek_device (ctx->mbim);
        if (mbim_device_check_ms_mbimex_version (device, 3, 0))
            message = mbim_message_ms_basic_connect_v3_connect_set_new (
                          ctx->session_id,
                          MBIM_ACTIVATION_COMMAND_ACTIVATE,
                          MBIM_COMPRESSION_NONE,
                          ctx->auth,
                          ctx->requested_ip_type,
                          mbim_uuid_from_context_type (ctx->context_type),
                          MBIM_ACCESS_MEDIA_TYPE_UNKNOWN,
                          ctx->apn ? ctx->apn : "",
                          ctx->user ? ctx->user : "",
                          ctx->password ? ctx->password : "",
                          NULL, /* unnamed ies */
                          NULL);
        else
            message = mbim_message_connect_set_new (
                          ctx->session_id,
                          MBIM_ACTIVATION_COMMAND_ACTIVATE,
                          ctx->apn ? ctx->apn : "",
                          ctx->user ? ctx->user : "",
                          ctx->password ? ctx->password : "",
                          MBIM_COMPRESSION_NONE,
                          ctx->auth,
                          ctx->requested_ip_type,
                          mbim_uuid_from_context_type (ctx->context_type),
                          NULL);

        mbim_device_command (mm_port_mbim_peek_device (ctx->mbim),
                             message,
                             MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                             g_task_get_cancellable (task),
                             (GAsyncReadyCallback)connect_set_ready,
                             task);
        return;
    }

    case CONNECT_STEP_IP_CONFIGURATION:
        mm_obj_dbg (self, "querying IP configuration...");
        message = mbim_message_ip_configuration_query_new (
                      ctx->session_id,
                      MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_NONE, /* ipv4configurationavailable */
                      MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_NONE, /* ipv6configurationavailable */
                      0, /* ipv4addresscount */
                      NULL, /* ipv4address */
                      0, /* ipv6addresscount */
                      NULL, /* ipv6address */
                      NULL, /* ipv4gateway */
                      NULL, /* ipv6gateway */
                      0, /* ipv4dnsservercount */
                      NULL, /* ipv4dnsserver */
                      0, /* ipv6dnsservercount */
                      NULL, /* ipv6dnsserver */
                      0, /* ipv4mtu */
                      0, /* ipv6mtu */
                      NULL);
        mbim_device_command (mm_port_mbim_peek_device (ctx->mbim),
                             message,
                             60,
                             g_task_get_cancellable (task),
                             (GAsyncReadyCallback)ip_configuration_query_ready,
                             task);
        return;

    case CONNECT_STEP_LAST:
        /* Cleanup the abort message so that we don't
         * run it */
        g_clear_pointer (&ctx->abort_on_failure, mbim_message_unref);

        /* Port is connected; update the state */
        mm_port_set_connected (ctx->link ? ctx->link : ctx->data, TRUE);

        /* Keep connection related data */
        g_assert (self->priv->mbim == NULL);
        self->priv->mbim = g_object_ref (ctx->mbim);
        g_assert (self->priv->data == NULL);
        self->priv->data = ctx->data ? g_object_ref (ctx->data) : NULL;
        g_assert (self->priv->link == NULL);
        self->priv->link = ctx->link ? g_object_ref (ctx->link) : NULL;
        g_assert (!self->priv->session_id);
        self->priv->session_id = ctx->session_id;

        /* reset the link name to avoid cleaning up the link on context free */
        g_clear_pointer (&ctx->link_name, g_free);

        /* Set operation result */
        g_task_return_pointer (
            task,
            mm_bearer_connect_result_ref (ctx->connect_result),
            (GDestroyNotify)mm_bearer_connect_result_unref);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

static gboolean
load_settings_from_bearer (MMBearerMbim        *self,
                           ConnectContext      *ctx,
                           MMBearerProperties  *properties,
                           GError             **error)
{
    MMBearerMultiplexSupport  multiplex;
    gboolean                  multiplex_supported = TRUE;
    guint                     current_multiplexed_bearers;
    guint                     max_multiplexed_bearers;
    const gchar              *data_port_driver;

    if (!mm_broadband_modem_get_active_multiplexed_bearers (MM_BROADBAND_MODEM (ctx->modem),
                                                            &current_multiplexed_bearers,
                                                            &max_multiplexed_bearers,
                                                            error))
        return FALSE;

    /* Check multiplex support in the kernel and the device */
    data_port_driver = mm_kernel_device_get_driver (mm_port_peek_kernel_device (ctx->data));
    if (!g_strcmp0 (data_port_driver, "mhi_net") || !max_multiplexed_bearers)
        multiplex_supported = FALSE;

    /* If no multiplex setting given by the user, assume none */
    multiplex = mm_bearer_properties_get_multiplex (properties);
    if (multiplex == MM_BEARER_MULTIPLEX_SUPPORT_UNKNOWN) {
        if (mm_context_get_test_multiplex_requested ())
            multiplex = MM_BEARER_MULTIPLEX_SUPPORT_REQUESTED;
        else
            multiplex = MM_BEARER_MULTIPLEX_SUPPORT_NONE;
    }

    /* If multiplex unsupported, either abort or default to none */
    if (!multiplex_supported) {
        if (multiplex == MM_BEARER_MULTIPLEX_SUPPORT_REQUIRED) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                         "Multiplexing required but not supported");
            return FALSE;
        }
        if (multiplex == MM_BEARER_MULTIPLEX_SUPPORT_REQUESTED) {
            mm_obj_dbg (self, "Multiplexing unsupported");
            multiplex = MM_BEARER_MULTIPLEX_SUPPORT_NONE;
        }
    }

    /* Go on with multiplexing enabled */
    if (multiplex == MM_BEARER_MULTIPLEX_SUPPORT_REQUESTED ||
        multiplex == MM_BEARER_MULTIPLEX_SUPPORT_REQUIRED) {
        g_assert (multiplex_supported);

        if (current_multiplexed_bearers == max_multiplexed_bearers) {
            g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                         "Maximum number of multiplexed bearers reached");
            return FALSE;
        }

        /* the link prefix hint given must be modem-specific */
        ctx->link_prefix_hint = g_strdup_printf ("mbimmux%u.", mm_base_modem_get_dbus_id (MM_BASE_MODEM (ctx->modem)));
    }

    /* If profile id is given, we'll load all settings from the stored profile,
     * so ignore any other setting received in the bearer properties */
    ctx->profile_id = mm_bearer_properties_get_profile_id (properties);
    if (ctx->profile_id != MM_3GPP_PROFILE_ID_UNKNOWN) {
        MMBearerIpFamily  ip_type;
        GError           *inner_error = NULL;

        /* If we're loading settings from a profile, still read the ip-type
         * from the user input, as that is not stored in the profile */
        ip_type = mm_bearer_properties_get_ip_type (properties);
        mm_3gpp_normalize_ip_family (&ip_type);
        ctx->requested_ip_type = mm_bearer_ip_family_to_mbim_context_ip_type (ip_type, &inner_error);
        if (inner_error) {
            g_propagate_error (error, inner_error);
            return FALSE;
        }
        return TRUE;
    }

    /* Use the implicit profile settings in the bearer properties.
     * If not loading from a stored profile, initialize the APN type to 'internet'
     * (TYPE_DEFAULT) by default, which is what we've done until now. */
    if (!load_settings_from_profile (self,
                                     ctx,
                                     mm_bearer_properties_peek_3gpp_profile (properties),
                                     MM_BEARER_APN_TYPE_DEFAULT,
                                     error))
        return FALSE;

    /* Is this a 3GPP only modem and no APN or profile id was given? If so, error */
    if (mm_iface_modem_is_3gpp_only (MM_IFACE_MODEM (ctx->modem)) && !ctx->apn) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                     "3GPP connection logic requires APN or profile id setting");
        return FALSE;
    }

    return TRUE;
}

static void
_connect (MMBaseBearer        *self,
          GCancellable        *cancellable,
          GAsyncReadyCallback  callback,
          gpointer             user_data)
{
    ConnectContext                *ctx;
    MMPort                        *data;
    MMPortMbim                    *mbim;
    GTask                         *task;
    GError                        *error = NULL;
    g_autoptr(MMBaseModem)         modem  = NULL;
    g_autoptr(MMBearerProperties)  properties = NULL;

    if (!peek_ports (self, &mbim, &data, callback, user_data))
        return;

    task = g_task_new (self, cancellable, callback, user_data);

    g_object_get (self,
                  MM_BASE_BEARER_MODEM,  &modem,
                  MM_BASE_BEARER_CONFIG, &properties,
                  NULL);
    g_assert (modem);

    ctx = g_slice_new0 (ConnectContext);
    ctx->modem = MM_BROADBAND_MODEM_MBIM (g_object_ref (modem));
    ctx->mbim = g_object_ref (mbim);
    ctx->data = g_object_ref (data);
    ctx->step = CONNECT_STEP_FIRST;
    ctx->requested_ip_type = MBIM_CONTEXT_IP_TYPE_DEFAULT;
    ctx->activated_ip_type = MBIM_CONTEXT_IP_TYPE_DEFAULT;
    g_task_set_task_data (task, ctx, (GDestroyNotify)connect_context_free);

    if (!load_settings_from_bearer (MM_BEARER_MBIM (self), ctx, properties, &error)) {
        g_prefix_error (&error, "Invalid bearer properties: ");
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "launching %sconnection with data port (%s/%s)",
                ctx->link_prefix_hint ? "multiplexed " : "",
                mm_port_subsys_get_string (mm_port_get_subsys (data)),
                mm_port_get_device (data));

    /* Run! */
    connect_context_step (task);
}

/*****************************************************************************/
/* Disconnect */

typedef enum {
    DISCONNECT_STEP_FIRST,
    DISCONNECT_STEP_DISCONNECT,
    DISCONNECT_STEP_LAST
} DisconnectStep;

typedef struct {
    MMPortMbim     *mbim;
    guint           session_id;
    DisconnectStep  step;
} DisconnectContext;

static void
disconnect_context_free (DisconnectContext *ctx)
{
    g_object_unref (ctx->mbim);
    g_slice_free (DisconnectContext, ctx);
}

static gboolean
disconnect_finish (MMBaseBearer  *self,
                   GAsyncResult  *res,
                   GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
reset_bearer_connection (MMBearerMbim *self)
{
    if (self->priv->data) {
        mm_port_set_connected (self->priv->data, FALSE);
        g_clear_object (&self->priv->data);
    }

    if (self->priv->link) {
        g_assert (self->priv->mbim);
        /* Link is disconnected; update the state */
        mm_port_set_connected (self->priv->link, FALSE);
        mm_port_mbim_cleanup_link (self->priv->mbim,
                                   mm_port_get_device (self->priv->link),
                                   NULL,
                                   NULL);
        g_clear_object (&self->priv->link);
    }
    self->priv->session_id = 0;
    g_clear_object (&self->priv->mbim);
}

static void disconnect_context_step (GTask *task);

static void
disconnect_set_ready (MbimDevice   *device,
                      GAsyncResult *res,
                      GTask        *task)
{
    MMBearerMbim           *self;
    DisconnectContext      *ctx;
    GError                 *error = NULL;
    g_autoptr(MbimMessage)  response = NULL;
    guint32                 session_id;
    MbimActivationState     activation_state;
    guint32                 nw_error;
    g_autoptr(GError)       inner_error = NULL;
    gboolean                result = FALSE;
    gboolean                parsed_result = FALSE;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response)
        goto out;

    result = mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error);

    /* Parse the response only for the cases we need to */
    if (result ||
        g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_FAILURE) ||
        g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_CONTEXT_NOT_ACTIVATED)) {
        if (mbim_device_check_ms_mbimex_version (device, 3, 0))
            parsed_result = mbim_message_ms_basic_connect_v3_connect_response_parse (
                                response,
                                &session_id,
                                &activation_state,
                                NULL, /* voice_call_state */
                                NULL, /* ip_type */
                                NULL, /* context_type */
                                &nw_error,
                                NULL, /* media_preference */
                                NULL, /* access_string */
                                NULL, /* unnamed_ies */
                                &inner_error);
        else
            parsed_result = mbim_message_connect_response_parse (
                                response,
                                &session_id,
                                &activation_state,
                                NULL, /* voice_call_state */
                                NULL, /* ip_type */
                                NULL, /* context_type */
                                &nw_error,
                                &inner_error);
    }

    /* Now handle different response / error cases */

    if (result && parsed_result) {
        g_assert (!error);
        g_assert (!inner_error);
        mm_obj_dbg (self, "session ID '%u': %s", session_id, mbim_activation_state_get_string (activation_state));
        /* success */
        goto out;
    }

    if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_CONTEXT_NOT_ACTIVATED)) {
        if (parsed_result)
            mm_obj_dbg (self, "context not activated: session ID '%u' already disconnected", session_id);
        else
            mm_obj_dbg (self, "context not activated: already disconnected");

        g_clear_error (&error);
        g_clear_error (&inner_error);
        /* success */
        goto out;
    }

    if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_SIM_NOT_INSERTED)) {
        g_clear_error (&error);
        g_clear_error (&inner_error);
        mm_obj_dbg (self, "SIM card not inserted: already disconnected");
        /* success */
        goto out;
    }

    if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_FAILURE) && parsed_result && nw_error != 0) {
        g_assert (!inner_error);
        g_error_free (error);
        error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error, self);
        /* error out with nw_error error */
        goto out;
    }

    /* Give precedence to original error over parsing error */
    if (!error && inner_error)
        error = g_steal_pointer (&inner_error);

out:
    if (error) {
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Keep on */
    ctx->step++;
    disconnect_context_step (task);
}

static void
disconnect_context_step (GTask *task)
{
    MMBearerMbim      *self;
    DisconnectContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISCONNECT_STEP_FIRST:
        ctx->step++;
        /* Fall through */

    case DISCONNECT_STEP_DISCONNECT: {
        g_autoptr(MbimMessage) message = NULL;

        message = build_disconnect_message (self, ctx->mbim, ctx->session_id);
        mbim_device_command (mm_port_mbim_peek_device (ctx->mbim),
                             message,
                             MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
                             NULL,
                             (GAsyncReadyCallback)disconnect_set_ready,
                             task);
        return;
    }

    case DISCONNECT_STEP_LAST:
        /* Port is disconnected; update the state */
        reset_bearer_connection (self);

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
disconnect (MMBaseBearer        *_self,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
    MMBearerMbim      *self = MM_BEARER_MBIM (_self);
    DisconnectContext *ctx;
    GTask             *task;

    task = g_task_new (self, NULL, callback, user_data);

    if ((!self->priv->data && !self->priv->link) ||
        !self->priv->mbim) {
        mm_obj_dbg (self, "no need to disconnect: MBIM bearer is already disconnected");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "launching disconnection on data port (%s/%s)",
                mm_port_subsys_get_string (mm_port_get_subsys (self->priv->data)),
                mm_port_get_device (self->priv->data));

    ctx = g_slice_new0 (DisconnectContext);
    ctx->mbim = g_object_ref (self->priv->mbim);
    ctx->session_id = self->priv->session_id;
    ctx->step = DISCONNECT_STEP_FIRST;

    g_task_set_task_data (task, ctx, (GDestroyNotify)disconnect_context_free);

    /* Run! */
    disconnect_context_step (task);
}

/*****************************************************************************/

guint32
mm_bearer_mbim_get_session_id (MMBearerMbim *self)
{
    g_return_val_if_fail (MM_IS_BEARER_MBIM (self), 0);

    return self->priv->session_id;
}

/*****************************************************************************/

static void
report_connection_status (MMBaseBearer             *self,
                          MMBearerConnectionStatus  status,
                          const GError             *connection_error)
{
    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED)
        /* Cleanup all connection related data */
        reset_bearer_connection (MM_BEARER_MBIM (self));

    /* Chain up parent's report_connection_status() */
    MM_BASE_BEARER_CLASS (mm_bearer_mbim_parent_class)->report_connection_status (self, status, connection_error);
}

/*****************************************************************************/

#if defined WITH_SUSPEND_RESUME

static MMBearerConnectionStatus
reload_connection_status_finish (MMBaseBearer  *self,
                                 GAsyncResult  *res,
                                 GError       **error)
{
    gint val;

    val = g_task_propagate_int (G_TASK (res), error);
    if (val < 0)
        return MM_BEARER_CONNECTION_STATUS_UNKNOWN;

    return (MMBearerConnectionStatus) val;
}

static void
reload_connection_status_ready (MbimDevice   *device,
                                GAsyncResult *res,
                                GTask        *task)
{
    MMBearerMbim             *self;
    guint32                   session_id;
    MbimActivationState       activation_state;
    MMBearerConnectionStatus  bearer_connection_status = MM_BEARER_CONNECTION_STATUS_UNKNOWN;
    GError                   *error = NULL;
    g_autoptr(MbimMessage)    response = NULL;

    self = g_task_get_source_object (task);

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_prefix_error (&error, "Cannot load session ID '%u' status: ",
                        mm_bearer_mbim_get_session_id (MM_BEARER_MBIM (self)));
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_v3_connect_response_parse (
                response,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                NULL, /* ip_type */
                NULL, /* context_type */
                NULL, /* nw_error */
                NULL, /* media_preference */
                NULL, /* access_string */
                NULL, /* unnamed_ies */
                &error))
            g_prefix_error (&error, "Failed processing MBIMEx v3.0 connect response: ");
        else
            mm_obj_dbg (self, "processed MBIMEx v3.0 connect response");
    } else {
        if (!mbim_message_connect_response_parse (
                response,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                NULL, /* ip_type */
                NULL, /* context_type */
                NULL, /* nw_error */
                &error))
            g_prefix_error (&error, "Failed processing connect response: ");
        else
            mm_obj_dbg (self, "processed connect response");
    }

    if (error) {
        g_prefix_error (&error, "Cannot load session ID '%u' status: ",
                        mm_bearer_mbim_get_session_id (MM_BEARER_MBIM (self)));
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    mm_obj_dbg (self, "session ID '%u': %s", session_id, mbim_activation_state_get_string (activation_state));

    switch (activation_state) {
    case MBIM_ACTIVATION_STATE_ACTIVATED:
    case MBIM_ACTIVATION_STATE_ACTIVATING:
        /* for the purposes of the sync operation, it's fine to map ACTIVATING
         * to CONNECTED, as we're really going to ignore that state in the actual
         * processing of the logic. */
        bearer_connection_status = MM_BEARER_CONNECTION_STATUS_CONNECTED;
        break;
    case MBIM_ACTIVATION_STATE_DEACTIVATING:
        bearer_connection_status = MM_BEARER_CONNECTION_STATUS_DISCONNECTING;
        break;
    case MBIM_ACTIVATION_STATE_DEACTIVATED:
        bearer_connection_status = MM_BEARER_CONNECTION_STATUS_DISCONNECTED;
        break;
    case MBIM_ACTIVATION_STATE_UNKNOWN:
    default:
        break;
    }

    if (bearer_connection_status == MM_BEARER_CONNECTION_STATUS_UNKNOWN)
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Cannot load session ID '%u' status",
                                 mm_bearer_mbim_get_session_id (MM_BEARER_MBIM (self)));
    else
        g_task_return_int (task, bearer_connection_status);
    g_object_unref (task);
}

static void
reload_connection_status (MMBaseBearer        *self,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    MMPortMbim             *mbim;
    GTask                  *task = NULL;
    g_autoptr(MbimMessage)  message = NULL;

    if (!peek_ports (self, &mbim, NULL, callback, user_data))
        return;

    task = g_task_new (self, NULL, callback, user_data);
    message = mbim_message_connect_query_new (mm_bearer_mbim_get_session_id (MM_BEARER_MBIM (self)),
                                              MBIM_ACTIVATION_STATE_UNKNOWN,
                                              MBIM_VOICE_CALL_STATE_NONE,
                                              MBIM_CONTEXT_IP_TYPE_DEFAULT,
                                              mbim_uuid_from_context_type (MBIM_CONTEXT_TYPE_INTERNET),
                                              0,
                                              NULL);
    mbim_device_command (mm_port_mbim_peek_device (mbim),
                         message,
                         10,
                         NULL,
                         (GAsyncReadyCallback)reload_connection_status_ready,
                         task);
}

#endif /* WITH_SUSPEND_RESUME */

/*****************************************************************************/

MMBaseBearer *
mm_bearer_mbim_new (MMBroadbandModemMbim *modem,
                    MMBearerProperties   *config)
{
    MMBaseBearer *bearer;

    /* The Mbim bearer inherits from MMBaseBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new() here */
    bearer = g_object_new (MM_TYPE_BEARER_MBIM,
                           MM_BASE_BEARER_MODEM,  modem,
                           MM_BASE_BEARER_CONFIG, config,
                           NULL);

    /* Only export valid bearers */
    mm_base_bearer_export (bearer);

    return bearer;
}

static void
mm_bearer_mbim_init (MMBearerMbim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_BEARER_MBIM, MMBearerMbimPrivate);
}

static void
dispose (GObject *object)
{
    MMBearerMbim *self = MM_BEARER_MBIM (object);

    reset_bearer_connection (self);

    G_OBJECT_CLASS (mm_bearer_mbim_parent_class)->dispose (object);
}

static void
mm_bearer_mbim_class_init (MMBearerMbimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerMbimPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;

    base_bearer_class->connect = _connect;
    base_bearer_class->connect_finish = connect_finish;
    base_bearer_class->disconnect = disconnect;
    base_bearer_class->disconnect_finish = disconnect_finish;
    base_bearer_class->report_connection_status = report_connection_status;
    base_bearer_class->reload_stats = reload_stats;
    base_bearer_class->reload_stats_finish = reload_stats_finish;
    base_bearer_class->load_connection_status = NULL;
    base_bearer_class->load_connection_status_finish = NULL;
#if defined WITH_SUSPEND_RESUME
    base_bearer_class->reload_connection_status = reload_connection_status;
    base_bearer_class->reload_connection_status_finish = reload_connection_status_finish;
#endif
}
