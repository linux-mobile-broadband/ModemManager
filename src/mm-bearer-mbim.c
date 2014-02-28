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
#include "mm-modem-helpers-mbim.h"
#include "mm-port-enums-types.h"
#include "mm-bearer-mbim.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMBearerMbim, mm_bearer_mbim, MM_TYPE_BEARER)

enum {
    PROP_0,
    PROP_SESSION_ID,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBearerMbimPrivate {
    /* The session ID for this bearer */
    guint32 session_id;

    MMPort *data;
};

/*****************************************************************************/

static gboolean
peek_ports (gpointer self,
            MbimDevice **o_device,
            MMPort **o_data,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMBaseModem *modem = NULL;

    g_object_get (G_OBJECT (self),
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (MM_IS_BASE_MODEM (modem));

    if (o_device) {
        MMPortMbim *port;

        port = mm_base_modem_peek_port_mbim (modem);
        if (!port) {
            g_simple_async_report_error_in_idle (G_OBJECT (self),
                                                 callback,
                                                 user_data,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_FAILED,
                                                 "Couldn't peek MBIM port");
            g_object_unref (modem);
            return FALSE;
        }

        *o_device = mm_port_mbim_peek_device (port);
    }

    if (o_data) {
        MMPort *port;

        /* Grab a data port */
        port = mm_base_modem_peek_best_data_port (modem, MM_PORT_TYPE_NET);
        if (!port) {
            g_simple_async_report_error_in_idle (G_OBJECT (self),
                                                 callback,
                                                 user_data,
                                                 MM_CORE_ERROR,
                                                 MM_CORE_ERROR_NOT_FOUND,
                                                 "No valid data port found to launch connection");
            g_object_unref (modem);
            return FALSE;
        }

        *o_data = port;
    }

    g_object_unref (modem);
    return TRUE;
}

/*****************************************************************************/
/* Connect */

typedef enum {
    CONNECT_STEP_FIRST,
    CONNECT_STEP_PACKET_SERVICE,
    CONNECT_STEP_PROVISIONED_CONTEXTS,
    CONNECT_STEP_CONNECT,
    CONNECT_STEP_IP_CONFIGURATION,
    CONNECT_STEP_LAST
} ConnectStep;

typedef struct {
    MMBearerMbim *self;
    MbimDevice *device;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMBearerProperties *properties;
    ConnectStep step;
    MMPort *data;
    MbimContextIpType ip_type;
    MMBearerConnectResult *connect_result;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->connect_result)
        mm_bearer_connect_result_unref (ctx->connect_result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->properties);
    g_object_unref (ctx->device);
    g_object_unref (ctx->self);
    g_slice_free (ConnectContext, ctx);
}

static MMBearerConnectResult *
connect_finish (MMBearer *self,
                GAsyncResult *res,
                GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void connect_context_step (ConnectContext *ctx);

static void
ip_configuration_query_ready (MbimDevice *device,
                              GAsyncResult *res,
                              ConnectContext *ctx)
{
    GError *error = NULL;
    MbimMessage *response;
    MbimIPConfigurationAvailableFlag ipv4configurationavailable;
    MbimIPConfigurationAvailableFlag ipv6configurationavailable;
    guint32 ipv4addresscount;
    MbimIPv4Element **ipv4address;
    guint32 ipv6addresscount;
    MbimIPv6Element **ipv6address;
    const MbimIPv4 *ipv4gateway;
    const MbimIPv6 *ipv6gateway;
    guint32 ipv4dnsservercount;
    MbimIPv4 *ipv4dnsserver;
    guint32 ipv6dnsservercount;
    MbimIPv6 *ipv6dnsserver;
    guint32 ipv4mtu;
    guint32 ipv6mtu;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_command_done_get_result (response, &error) &&
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
        gchar *str;
        GInetAddress *addr;
        MMBearerIpConfig *ipv4_config;
        MMBearerIpConfig *ipv6_config;

        /* IPv4 info */

        str = mbim_ip_configuration_available_flag_build_string_from_mask (ipv4configurationavailable);
        mm_dbg ("IPv4 configuration available: '%s'", str);
        g_free (str);

        if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_ADDRESS) {
            guint i;

            mm_dbg ("  IP addresses (%u)", ipv4addresscount);
            for (i = 0; i < ipv4addresscount; i++) {
                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv4address[i]->ipv4_address, G_SOCKET_FAMILY_IPV4);
                str = g_inet_address_to_string (addr);
                mm_dbg ("    IP [%u]: '%s/%u'", i, str, ipv4address[i]->on_link_prefix_length);
                g_free (str);
                g_object_unref (addr);
            }
        }

        if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY) {
            addr = g_inet_address_new_from_bytes ((guint8 *)ipv4gateway, G_SOCKET_FAMILY_IPV4);
            str = g_inet_address_to_string (addr);
            mm_dbg ("  Gateway: '%s'", str);
            g_free (str);
            g_object_unref (addr);
        }

        if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_DNS) {
            guint i;

            mm_dbg ("  DNS addresses (%u)", ipv4dnsservercount);
            for (i = 0; i < ipv4dnsservercount; i++) {
                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv4dnsserver[i], G_SOCKET_FAMILY_IPV4);
                str = g_inet_address_to_string (addr);
                mm_dbg ("    DNS [%u]: '%s'", i, str);
                g_free (str);
                g_object_unref (addr);
            }
        }

        if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_MTU) {
            mm_dbg ("  MTU: '%u'", ipv4mtu);
        }

        /* IPv6 info */

        str = mbim_ip_configuration_available_flag_build_string_from_mask (ipv6configurationavailable);
        mm_dbg ("IPv6 configuration available: '%s'", str);
        g_free (str);

        if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_ADDRESS) {
            guint i;

            mm_dbg ("  IP addresses (%u)", ipv6addresscount);
            for (i = 0; i < ipv6addresscount; i++) {
                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv6address[i]->ipv6_address, G_SOCKET_FAMILY_IPV6);
                str = g_inet_address_to_string (addr);
                mm_dbg ("    IP [%u]: '%s/%u'", i, str, ipv6address[i]->on_link_prefix_length);
                g_free (str);
                g_object_unref (addr);
            }
        }

        if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY) {
            addr = g_inet_address_new_from_bytes ((guint8 *)ipv6gateway, G_SOCKET_FAMILY_IPV6);
            str = g_inet_address_to_string (addr);
            mm_dbg ("  Gateway: '%s'", str);
            g_free (str);
            g_object_unref (addr);
        }

        if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_DNS) {
            guint i;

            mm_dbg ("  DNS addresses (%u)", ipv6dnsservercount);
            for (i = 0; i < ipv6dnsservercount; i++) {
                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv6dnsserver[i], G_SOCKET_FAMILY_IPV6);
                str = g_inet_address_to_string (addr);
                mm_dbg ("    DNS [%u]: '%s'", i, str);
                g_free (str);
                g_object_unref (addr);
            }
        }

        if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_MTU) {
            mm_dbg ("  MTU: '%u'", ipv6mtu);
        }

        /* Build connection results */

        /* Build IPv4 config */
        if (ctx->ip_type == MBIM_CONTEXT_IP_TYPE_IPV4 ||
            ctx->ip_type == MBIM_CONTEXT_IP_TYPE_IPV4V6 ||
            ctx->ip_type == MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6) {
            ipv4_config = mm_bearer_ip_config_new ();

            /* We assume that if we have IP and DNS, we can setup static */
            if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_ADDRESS &&
                ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_DNS &&
                ipv4addresscount > 0 &&
                ipv4dnsservercount > 0) {
                gchar **strarr;
                guint i;

                mm_bearer_ip_config_set_method (ipv4_config, MM_BEARER_IP_METHOD_STATIC);

                /* IP address, pick the first one */
                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv4address[0]->ipv4_address, G_SOCKET_FAMILY_IPV4);
                str = g_inet_address_to_string (addr);
                mm_bearer_ip_config_set_address (ipv4_config, str);
                g_free (str);
                g_object_unref (addr);

                /* Netmask */
                mm_bearer_ip_config_set_prefix (ipv4_config, ipv4address[0]->on_link_prefix_length);

                /* Gateway */
                if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY) {
                    addr = g_inet_address_new_from_bytes ((guint8 *)ipv4gateway, G_SOCKET_FAMILY_IPV4);
                    str = g_inet_address_to_string (addr);
                    mm_bearer_ip_config_set_gateway (ipv4_config, str);
                    g_free (str);
                    g_object_unref (addr);
                }

                /* DNS */
                strarr = g_new0 (gchar *, ipv4dnsservercount + 1);
                for (i = 0; i < ipv4dnsservercount; i++) {
                    addr = g_inet_address_new_from_bytes ((guint8 *)&ipv4dnsserver[i], G_SOCKET_FAMILY_IPV4);
                    strarr[i] = g_inet_address_to_string (addr);
                    g_object_unref (addr);
                }
                mm_bearer_ip_config_set_dns (ipv4_config, (const gchar **)strarr);
                g_strfreev (strarr);
            } else
                mm_bearer_ip_config_set_method (ipv4_config, MM_BEARER_IP_METHOD_DHCP);

            /* MTU */
            if (ipv4configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_MTU)
                mm_bearer_ip_config_set_mtu (ipv4_config, ipv4mtu);
        } else
            ipv4_config = NULL;

        /* Build IPv6 config */
        if (ctx->ip_type == MBIM_CONTEXT_IP_TYPE_IPV6 ||
            ctx->ip_type == MBIM_CONTEXT_IP_TYPE_IPV4V6 ||
            ctx->ip_type == MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6) {
            ipv6_config = mm_bearer_ip_config_new ();

            /* We assume that if we have IP and DNS, we can setup static */
            if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_ADDRESS &&
                ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_DNS &&
                ipv6addresscount > 0 &&
                ipv6dnsservercount > 0) {
                gchar **strarr;
                guint i;

                mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_STATIC);

                /* IP address, pick the first one */
                addr = g_inet_address_new_from_bytes ((guint8 *)&ipv6address[0]->ipv6_address, G_SOCKET_FAMILY_IPV6);
                str = g_inet_address_to_string (addr);
                mm_bearer_ip_config_set_address (ipv6_config, str);
                g_free (str);

                /* If the address is a link-local one, then SLAAC or DHCP must be used
                 * to get the real prefix and address.  Change the method to DHCP to
                 * indicate this to clients.
                 */
                if (g_inet_address_get_is_link_local (addr))
                    mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_DHCP);

                g_object_unref (addr);

                /* Netmask */
                mm_bearer_ip_config_set_prefix (ipv6_config, ipv6address[0]->on_link_prefix_length);

                /* Gateway */
                if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_GATEWAY) {
                    addr = g_inet_address_new_from_bytes ((guint8 *)ipv6gateway, G_SOCKET_FAMILY_IPV6);
                    str = g_inet_address_to_string (addr);
                    mm_bearer_ip_config_set_gateway (ipv6_config, str);
                    g_free (str);
                    g_object_unref (addr);
                }

                /* DNS */
                strarr = g_new0 (gchar *, ipv6dnsservercount + 1);
                for (i = 0; i < ipv6dnsservercount; i++) {
                    addr = g_inet_address_new_from_bytes ((guint8 *)&ipv6dnsserver[i], G_SOCKET_FAMILY_IPV6);
                    strarr[i] = g_inet_address_to_string (addr);
                    g_object_unref (addr);
                }
                mm_bearer_ip_config_set_dns (ipv6_config, (const gchar **)strarr);
                g_strfreev (strarr);
            } else
                mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_DHCP);

            /* MTU */
            if (ipv6configurationavailable & MBIM_IP_CONFIGURATION_AVAILABLE_FLAG_MTU)
                mm_bearer_ip_config_set_mtu (ipv6_config, ipv6mtu);
        } else
            ipv6_config = NULL;

        /* Store result */
        ctx->connect_result = mm_bearer_connect_result_new (ctx->data,
                                                            ipv4_config,
                                                            ipv6_config);

        if (ipv4_config)
            g_object_unref (ipv4_config);
        if (ipv6_config)
            g_object_unref (ipv6_config);
        mbim_ipv4_element_array_free (ipv4address);
        mbim_ipv6_element_array_free (ipv6address);
        g_free (ipv4dnsserver);
        g_free (ipv6dnsserver);
    }

    if (response)
        mbim_message_unref (response);

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
connect_set_ready (MbimDevice *device,
                   GAsyncResult *res,
                   ConnectContext *ctx)
{
    GError *error = NULL;
    MbimMessage *response;
    guint32 session_id;
    MbimActivationState activation_state;
    MbimContextIpType ip_type;
    guint32 nw_error;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        (mbim_message_command_done_get_result (response, &error) ||
         error->code == MBIM_STATUS_ERROR_FAILURE)) {
        GError *inner_error = NULL;

        if (mbim_message_connect_response_parse (
                response,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                &ip_type,
                NULL, /* context_type */
                &nw_error,
                &inner_error)) {
            if (nw_error) {
                if (error)
                    g_error_free (error);
                error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error);
            } else {
                ctx->ip_type = ip_type;
                mm_dbg ("Session ID '%u': %s (IP type: %s)",
                        session_id,
                        mbim_activation_state_get_string (activation_state),
                        mbim_context_ip_type_get_string (ip_type));
            }
        } else {
            /* Prefer the error from the result to the parsing error */
            if (!error)
                error = inner_error;
            else
                g_error_free (inner_error);
        }
    }

    if (response)
        mbim_message_unref (response);

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
provisioned_contexts_query_ready (MbimDevice *device,
                                  GAsyncResult *res,
                                  ConnectContext *ctx)
{
    GError *error = NULL;
    MbimMessage *response;
    guint32 provisioned_contexts_count;
    MbimProvisionedContextElement **provisioned_contexts;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_command_done_get_result (response, &error) &&
        mbim_message_provisioned_contexts_response_parse (
            response,
            &provisioned_contexts_count,
            &provisioned_contexts,
            &error)) {
        guint32 i;

        mm_dbg ("Provisioned contexts found (%u):", provisioned_contexts_count);
        for (i = 0; i < provisioned_contexts_count; i++) {
            MbimProvisionedContextElement *el = provisioned_contexts[i];
            gchar *uuid_str;

            uuid_str = mbim_uuid_get_printable (&el->context_type);
            mm_dbg ("[%u] context type: %s", el->context_id, mbim_context_type_get_string (mbim_uuid_to_context_type (&el->context_type)));
            mm_dbg ("             uuid: %s", uuid_str);
            mm_dbg ("    access string: %s", el->access_string ? el->access_string : "");
            mm_dbg ("         username: %s", el->user_name ? el->user_name : "");
            mm_dbg ("         password: %s", el->password ? el->password : "");
            mm_dbg ("      compression: %s", mbim_compression_get_string (el->compression));
            mm_dbg ("             auth: %s", mbim_auth_protocol_get_string (el->auth_protocol));
            g_free (uuid_str);
        }

        mbim_provisioned_context_element_array_free (provisioned_contexts);
    } else {
        mm_dbg ("Error listing provisioned contexts: %s", error->message);
        g_error_free (error);
    }

    if (response)
        mbim_message_unref (response);

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
packet_service_set_ready (MbimDevice *device,
                          GAsyncResult *res,
                          ConnectContext *ctx)
{
    GError *error = NULL;
    MbimMessage *response;
    guint32 nw_error;
    MbimPacketServiceState packet_service_state;
    MbimDataClass highest_available_data_class;
    guint64 uplink_speed;
    guint64 downlink_speed;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        (mbim_message_command_done_get_result (response, &error) ||
         error->code == MBIM_STATUS_ERROR_FAILURE)) {
        GError *inner_error = NULL;

        if (mbim_message_packet_service_response_parse (
                response,
                &nw_error,
                &packet_service_state,
                &highest_available_data_class,
                &uplink_speed,
                &downlink_speed,
                &inner_error)) {
            if (nw_error) {
                if (error)
                    g_error_free (error);
                error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error);
            } else {
                gchar *str;

                str = mbim_data_class_build_string_from_mask (highest_available_data_class);
                mm_dbg ("Packet service update:");
                mm_dbg ("         state: '%s'", mbim_packet_service_state_get_string (packet_service_state));
                mm_dbg ("    data class: '%s'", str);
                mm_dbg ("        uplink: '%" G_GUINT64_FORMAT "' bps", uplink_speed);
                mm_dbg ("      downlink: '%" G_GUINT64_FORMAT "' bps", downlink_speed);
                g_free (str);
            }
        } else {
            /* Prefer the error from the result to the parsing error */
            if (!error)
                error = inner_error;
            else
                g_error_free (inner_error);
        }
    }

    if (response)
        mbim_message_unref (response);

    if (error) {
        /* Don't make NoDeviceSupport errors fatal; just try to keep on the
         * connection sequence even with this error. */
        if (g_error_matches (error, MBIM_STATUS_ERROR, MBIM_STATUS_ERROR_NO_DEVICE_SUPPORT)) {
            mm_dbg ("Device doesn't support packet service attach");
            g_error_free (error);
        } else {
            /* All other errors are fatal */
            g_simple_async_result_take_error (ctx->result, error);
            connect_context_complete_and_free (ctx);
            return;
        }
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
connect_context_step (ConnectContext *ctx)
{
    MbimMessage *message;

    /* If cancelled, complete */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "Connection setup operation has been cancelled");
        connect_context_complete_and_free (ctx);
        return;
    }

    switch (ctx->step) {
    case CONNECT_STEP_FIRST:
        /* Fall down */
        ctx->step++;

    case CONNECT_STEP_PACKET_SERVICE: {
        GError *error = NULL;

        mm_dbg ("Activating packet service...");
        message = (mbim_message_packet_service_set_new (
                       MBIM_PACKET_SERVICE_ACTION_ATTACH,
                       &error));
        if (!message) {
            g_simple_async_result_take_error (ctx->result, error);
            connect_context_complete_and_free (ctx);
            return;
        }

        mbim_device_command (ctx->device,
                             message,
                             30,
                             NULL,
                             (GAsyncReadyCallback)packet_service_set_ready,
                             ctx);
        mbim_message_unref (message);
        return;
    }

    case CONNECT_STEP_PROVISIONED_CONTEXTS:
        mm_dbg ("Listing provisioned contexts...");
        message = mbim_message_provisioned_contexts_query_new (NULL);
        mbim_device_command (ctx->device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)provisioned_contexts_query_ready,
                             ctx);
        mbim_message_unref (message);
        return;

    case CONNECT_STEP_CONNECT: {
        const gchar *apn;
        const gchar *user;
        const gchar *password;
        MbimAuthProtocol auth;
        MbimContextIpType ip_type;
        MMBearerIpFamily ip_family;
        GError *error = NULL;

        /* Setup parameters to use */

        apn = mm_bearer_properties_get_apn (ctx->properties);
        user = mm_bearer_properties_get_user (ctx->properties);
        password = mm_bearer_properties_get_password (ctx->properties);

        if (!user && !password) {
            auth = MBIM_AUTH_PROTOCOL_NONE;
        } else {
            MMBearerAllowedAuth bearer_auth;
            bearer_auth = mm_bearer_properties_get_allowed_auth (ctx->properties);
            if (bearer_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
                mm_dbg ("Using default (PAP) authentication method");
                auth = MBIM_AUTH_PROTOCOL_PAP;
            } else if (bearer_auth & MM_BEARER_ALLOWED_AUTH_PAP) {
                auth = MBIM_AUTH_PROTOCOL_PAP;
            } else if (bearer_auth & MM_BEARER_ALLOWED_AUTH_CHAP) {
                auth = MBIM_AUTH_PROTOCOL_CHAP;
            } else if (bearer_auth & MM_BEARER_ALLOWED_AUTH_MSCHAPV2) {
                auth = MBIM_AUTH_PROTOCOL_MSCHAPV2;
            } else if (bearer_auth & MM_BEARER_ALLOWED_AUTH_NONE) {
                auth = MBIM_AUTH_PROTOCOL_NONE;
            } else {
                gchar *str;

                str = mm_bearer_allowed_auth_build_string_from_mask (bearer_auth);
                g_simple_async_result_set_error (
                    ctx->result,
                    MM_CORE_ERROR,
                    MM_CORE_ERROR_UNSUPPORTED,
                    "Cannot use any of the specified authentication methods (%s)",
                    str);
                g_free (str);
                connect_context_complete_and_free (ctx);
                return;
            }
        }

        ip_family = mm_bearer_properties_get_ip_type (ctx->properties);
        if (ip_family == MM_BEARER_IP_FAMILY_NONE ||
            ip_family == MM_BEARER_IP_FAMILY_ANY) {
            gchar * str;

            ip_family = mm_bearer_get_default_ip_family (MM_BEARER (ctx->self));
            str = mm_bearer_ip_family_build_string_from_mask (ip_family);
            mm_dbg ("No specific IP family requested, defaulting to %s", str);
            g_free (str);
        }

        if (ip_family == MM_BEARER_IP_FAMILY_IPV4)
            ip_type = MBIM_CONTEXT_IP_TYPE_IPV4;
        else if (ip_family == MM_BEARER_IP_FAMILY_IPV6)
            ip_type = MBIM_CONTEXT_IP_TYPE_IPV6;
        else if (ip_family == MM_BEARER_IP_FAMILY_IPV4V6)
            ip_type = MBIM_CONTEXT_IP_TYPE_IPV4V6;
        else if (ip_family == (MM_BEARER_IP_FAMILY_IPV4 | MM_BEARER_IP_FAMILY_IPV6))
            ip_type = MBIM_CONTEXT_IP_TYPE_IPV4_AND_IPV6;
        else if (ip_family == MM_BEARER_IP_FAMILY_NONE ||
                 ip_family == MM_BEARER_IP_FAMILY_ANY)
            /* A valid default IP family should have been specified */
            g_assert_not_reached ();
        else  {
            gchar * str;

            str = mm_bearer_ip_family_build_string_from_mask (ip_family);
            g_simple_async_result_set_error (
                ctx->result,
                MM_CORE_ERROR,
                MM_CORE_ERROR_UNSUPPORTED,
                "Unsupported IP type configuration: '%s'",
                str);
            g_free (str);
            connect_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Launching connection with APN '%s'...", apn);
        message = (mbim_message_connect_set_new (
                       ctx->self->priv->session_id,
                       MBIM_ACTIVATION_COMMAND_ACTIVATE,
                       apn ? apn : "",
                       user ? user : "",
                       password ? password : "",
                       MBIM_COMPRESSION_NONE,
                       auth,
                       ip_type,
                       mbim_uuid_from_context_type (MBIM_CONTEXT_TYPE_INTERNET),
                       &error));
        if (!message) {
            g_simple_async_result_take_error (ctx->result, error);
            connect_context_complete_and_free (ctx);
            return;
        }

        mbim_device_command (ctx->device,
                             message,
                             60,
                             NULL,
                             (GAsyncReadyCallback)connect_set_ready,
                             ctx);
        mbim_message_unref (message);
        return;
    }

    case CONNECT_STEP_IP_CONFIGURATION: {
        GError *error = NULL;

        mm_dbg ("Querying IP configuration...");
        message = (mbim_message_ip_configuration_query_new (
                       ctx->self->priv->session_id,
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
                       &error));
        if (!message) {
            g_simple_async_result_take_error (ctx->result, error);
            connect_context_complete_and_free (ctx);
            return;
        }

        mbim_device_command (ctx->device,
                             message,
                             60,
                             NULL,
                             (GAsyncReadyCallback)ip_configuration_query_ready,
                             ctx);
        mbim_message_unref (message);
        return;
    }

    case CONNECT_STEP_LAST:
        /* Port is connected; update the state */
        mm_port_set_connected (MM_PORT (ctx->data), TRUE);

        /* Keep the data port */
        g_assert (ctx->self->priv->data == NULL);
        ctx->self->priv->data = g_object_ref (ctx->data);

        /* Set operation result */
        g_simple_async_result_set_op_res_gpointer (
            ctx->result,
            mm_bearer_connect_result_ref (ctx->connect_result),
            (GDestroyNotify)mm_bearer_connect_result_unref);
        connect_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

static void
_connect (MMBearer *self,
          GCancellable *cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    ConnectContext *ctx;
    MMPort *data;
    MbimDevice *device;
    MMBaseModem *modem  = NULL;
    const gchar *apn;

    if (!peek_ports (self, &device, &data, callback, user_data))
        return;

    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem);

    /* Check whether we have an APN */
    apn = mm_bearer_properties_get_apn (mm_bearer_peek_config (MM_BEARER (self)));

    /* Is this a 3GPP only modem and no APN was given? If so, error */
    if (mm_iface_modem_is_3gpp_only (MM_IFACE_MODEM (modem)) && !apn) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_INVALID_ARGS,
            "3GPP connection logic requires APN setting");
        g_object_unref (modem);
        return;
    }

    g_object_unref (modem);

    mm_dbg ("Launching connection with data port (%s/%s)",
            mm_port_subsys_get_string (mm_port_get_subsys (data)),
            mm_port_get_device (data));

    ctx = g_slice_new0 (ConnectContext);
    ctx->self = g_object_ref (self);
    ctx->device = g_object_ref (device);;
    ctx->data = g_object_ref (data);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->step = CONNECT_STEP_FIRST;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             _connect);

    g_object_get (self,
                  MM_BEARER_CONFIG, &ctx->properties,
                  NULL);

    /* Run! */
    connect_context_step (ctx);
}

/*****************************************************************************/
/* Disconnect */

typedef enum {
    DISCONNECT_STEP_FIRST,
    DISCONNECT_STEP_DISCONNECT,
    DISCONNECT_STEP_LAST
} DisconnectStep;

typedef struct {
    MMBearerMbim *self;
    MbimDevice *device;
    GSimpleAsyncResult *result;
    MMPort *data;
    DisconnectStep step;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->self);
    g_slice_free (DisconnectContext, ctx);
}

static gboolean
disconnect_finish (MMBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void disconnect_context_step (DisconnectContext *ctx);

static void
disconnect_set_ready (MbimDevice *device,
                      GAsyncResult *res,
                      DisconnectContext *ctx)
{
    GError *error = NULL;
    MbimMessage *response;
    guint32 session_id;
    MbimActivationState activation_state;
    guint32 nw_error;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        (mbim_message_command_done_get_result (response, &error) ||
         error->code == MBIM_STATUS_ERROR_FAILURE)) {
        GError *inner_error = NULL;

        if (mbim_message_connect_response_parse (
                response,
                &session_id,
                &activation_state,
                NULL, /* voice_call_state */
                NULL, /* ip_type */
                NULL, /* context_type */
                &nw_error,
                &inner_error)) {
            if (nw_error) {
                if (error)
                    g_error_free (error);
                error = mm_mobile_equipment_error_from_mbim_nw_error (nw_error);
            } else
                mm_dbg ("Session ID '%u': %s",
                        session_id,
                        mbim_activation_state_get_string (activation_state));
        } else {
            /* Prefer the error from the result to the parsing error */
            if (!error)
                error = inner_error;
            else
                g_error_free (inner_error);
        }
    }

    if (response)
        mbim_message_unref (response);

    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_context_complete_and_free (ctx);
        return;
    }

    /* Keep on */
    ctx->step++;
    disconnect_context_step (ctx);
}

static void
disconnect_context_step (DisconnectContext *ctx)
{
    switch (ctx->step) {
    case DISCONNECT_STEP_FIRST:
        /* Fall down */
        ctx->step++;

    case DISCONNECT_STEP_DISCONNECT: {
        MbimMessage *message;
        GError *error = NULL;

        message = (mbim_message_connect_set_new (
                       ctx->self->priv->session_id,
                       MBIM_ACTIVATION_COMMAND_DEACTIVATE,
                       "",
                       "",
                       "",
                       MBIM_COMPRESSION_NONE,
                       MBIM_AUTH_PROTOCOL_NONE,
                       MBIM_CONTEXT_IP_TYPE_DEFAULT,
                       mbim_uuid_from_context_type (MBIM_CONTEXT_TYPE_INTERNET),
                       &error));
        if (!message) {
            g_simple_async_result_take_error (ctx->result, error);
            disconnect_context_complete_and_free (ctx);
            return;
        }

        mbim_device_command (ctx->device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)disconnect_set_ready,
                             ctx);
        mbim_message_unref (message);
        return;
    }

    case DISCONNECT_STEP_LAST:
        /* Port is disconnected; update the state */
        mm_port_set_connected (ctx->self->priv->data, FALSE);
        g_clear_object (&ctx->self->priv->data);

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disconnect_context_complete_and_free (ctx);
        return;
    }
}

static void
disconnect (MMBearer *_self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMBearerMbim *self = MM_BEARER_MBIM (_self);
    MbimDevice *device;
    DisconnectContext *ctx;

    if (!self->priv->data) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect MBIM bearer: this bearer is not connected");
        return;
    }

    if (!peek_ports (self, &device, NULL, callback, user_data))
        return;

    mm_dbg ("Launching disconnection on data port (%s/%s)",
            mm_port_subsys_get_string (mm_port_get_subsys (self->priv->data)),
            mm_port_get_device (self->priv->data));

    ctx = g_slice_new0 (DisconnectContext);
    ctx->self = g_object_ref (self);
    ctx->device = g_object_ref (device);
    ctx->data = g_object_ref (self->priv->data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect);
    ctx->step = DISCONNECT_STEP_FIRST;

    /* Run! */
    disconnect_context_step (ctx);
}

/*****************************************************************************/

guint32
mm_bearer_mbim_get_session_id (MMBearerMbim *self)
{
    g_return_val_if_fail (MM_IS_BEARER_MBIM (self), 0);

    return self->priv->session_id;
}

/*****************************************************************************/

MMBearer *
mm_bearer_mbim_new (MMBroadbandModemMbim *modem,
                    MMBearerProperties *config,
                    guint32 session_id)
{
    MMBearer *bearer;

    /* The Mbim bearer inherits from MMBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new() here */
    bearer = g_object_new (MM_TYPE_BEARER_MBIM,
                           MM_BEARER_MODEM,           modem,
                           MM_BEARER_CONFIG,          config,
                           MM_BEARER_MBIM_SESSION_ID, (guint)session_id,
                           NULL);

    /* Only export valid bearers */
    mm_bearer_export (bearer);

    return bearer;
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBearerMbim *self = MM_BEARER_MBIM (object);

    switch (prop_id) {
    case PROP_SESSION_ID:
        self->priv->session_id = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBearerMbim *self = MM_BEARER_MBIM (object);

    switch (prop_id) {
    case PROP_SESSION_ID:
        g_value_set_uint (value, self->priv->session_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}


static void
mm_bearer_mbim_init (MMBearerMbim *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BEARER_MBIM,
                                              MMBearerMbimPrivate);
}

static void
dispose (GObject *object)
{
    MMBearerMbim *self = MM_BEARER_MBIM (object);

    g_clear_object (&self->priv->data);

    G_OBJECT_CLASS (mm_bearer_mbim_parent_class)->dispose (object);
}

static void
mm_bearer_mbim_class_init (MMBearerMbimClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerMbimPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;
    object_class->get_property = get_property;
    object_class->set_property = set_property;

    bearer_class->connect = _connect;
    bearer_class->connect_finish = connect_finish;
    bearer_class->disconnect = disconnect;
    bearer_class->disconnect_finish = disconnect_finish;

    properties[PROP_SESSION_ID] =
        g_param_spec_uint (MM_BEARER_MBIM_SESSION_ID,
                           "Session ID",
                           "Session ID to use with this bearer",
                           0,
                           255,
                           0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_SESSION_ID, properties[PROP_SESSION_ID]);
}
