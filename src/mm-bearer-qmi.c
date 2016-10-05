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
 * Copyright (C) 2015 Azimut Electronics
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
#include "mm-bearer-qmi.h"
#include "mm-modem-helpers-qmi.h"
#include "mm-port-enums-types.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMBearerQmi, mm_bearer_qmi, MM_TYPE_BASE_BEARER)

#define GLOBAL_PACKET_DATA_HANDLE 0xFFFFFFFF

struct _MMBearerQmiPrivate {
    /* State kept while connected */
    QmiClientWds *client_ipv4;
    guint packet_service_status_ipv4_indication_id;

    QmiClientWds *client_ipv6;
    guint packet_service_status_ipv6_indication_id;

    MMPort *data;
    guint32 packet_data_handle_ipv4;
    guint32 packet_data_handle_ipv6;
};

/*****************************************************************************/
/* Stats */

typedef enum {
    RELOAD_STATS_CONTEXT_STEP_FIRST,
    RELOAD_STATS_CONTEXT_STEP_IPV4,
    RELOAD_STATS_CONTEXT_STEP_IPV6,
    RELOAD_STATS_CONTEXT_STEP_LAST,
} ReloadStatsContextStep;

typedef struct {
    guint64 rx_bytes;
    guint64 tx_bytes;
} ReloadStatsResult;

typedef struct {
    MMBearerQmi *self;
    GSimpleAsyncResult *result;
    QmiMessageWdsGetPacketStatisticsInput *input;
    ReloadStatsContextStep step;
    ReloadStatsResult stats;
} ReloadStatsContext;

static gboolean
reload_stats_finish (MMBaseBearer *bearer,
                     guint64 *rx_bytes,
                     guint64 *tx_bytes,
                     GAsyncResult *res,
                     GError **error)
{
    ReloadStatsResult *stats;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    stats = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    if (rx_bytes)
        *rx_bytes = stats->rx_bytes;
    if (tx_bytes)
        *tx_bytes = stats->tx_bytes;
    return TRUE;
}

static void
reload_stats_context_complete_and_free (ReloadStatsContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    qmi_message_wds_get_packet_statistics_input_unref (ctx->input);
    g_slice_free (ReloadStatsContext, ctx);
}

static void reload_stats_context_step (ReloadStatsContext *ctx);

static void
get_packet_statistics_ready (QmiClientWds *client,
                             GAsyncResult *res,
                             ReloadStatsContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsGetPacketStatisticsOutput *output;
    guint64 tx_bytes_ok = 0;
    guint64 rx_bytes_ok = 0;

    output = qmi_client_wds_get_packet_statistics_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (ctx->result, error);
        reload_stats_context_complete_and_free (ctx);
        return;
    }

    if (!qmi_message_wds_get_packet_statistics_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get packet statistics: ");
        g_simple_async_result_take_error (ctx->result, error);
        qmi_message_wds_get_packet_statistics_output_unref (output);
        reload_stats_context_complete_and_free (ctx);
        return;
    }

    qmi_message_wds_get_packet_statistics_output_get_tx_bytes_ok (output, &tx_bytes_ok, NULL);
    qmi_message_wds_get_packet_statistics_output_get_rx_bytes_ok (output, &rx_bytes_ok, NULL);
    ctx->stats.rx_bytes += rx_bytes_ok;
    ctx->stats.tx_bytes += tx_bytes_ok;

    qmi_message_wds_get_packet_statistics_output_unref (output);

    /* Go on */
    ctx->step++;
    reload_stats_context_step (ctx);
}

static void
reload_stats_context_step (ReloadStatsContext *ctx)
{
    switch (ctx->step) {
    case RELOAD_STATS_CONTEXT_STEP_FIRST:
        /* Fall through */
        ctx->step++;
    case RELOAD_STATS_CONTEXT_STEP_IPV4:
        if (ctx->self->priv->client_ipv4) {
            qmi_client_wds_get_packet_statistics (QMI_CLIENT_WDS (ctx->self->priv->client_ipv4),
                                                  ctx->input,
                                                  10,
                                                  NULL,
                                                  (GAsyncReadyCallback)get_packet_statistics_ready,
                                                  ctx);
            return;
        }
        ctx->step++;
        /* Fall through */
    case RELOAD_STATS_CONTEXT_STEP_IPV6:
        if (ctx->self->priv->client_ipv6) {
            qmi_client_wds_get_packet_statistics (QMI_CLIENT_WDS (ctx->self->priv->client_ipv6),
                                                  ctx->input,
                                                  10,
                                                  NULL,
                                                  (GAsyncReadyCallback)get_packet_statistics_ready,
                                                  ctx);
            return;
        }
        ctx->step++;
        /* Fall through */
    case RELOAD_STATS_CONTEXT_STEP_LAST:
        g_simple_async_result_set_op_res_gpointer (ctx->result, &ctx->stats, NULL);
        reload_stats_context_complete_and_free (ctx);
        return;
    }
}

static void
reload_stats (MMBaseBearer *self,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    ReloadStatsContext *ctx;

    ctx = g_slice_new0 (ReloadStatsContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             reload_stats);
    ctx->input = qmi_message_wds_get_packet_statistics_input_new ();
    qmi_message_wds_get_packet_statistics_input_set_mask (
        ctx->input,
        (QMI_WDS_PACKET_STATISTICS_MASK_FLAG_TX_BYTES_OK |
         QMI_WDS_PACKET_STATISTICS_MASK_FLAG_RX_BYTES_OK),
        NULL);
    ctx->step = RELOAD_STATS_CONTEXT_STEP_FIRST;

    reload_stats_context_step (ctx);
}

/*****************************************************************************/
/* Connect */

static void common_setup_cleanup_unsolicited_events (MMBearerQmi *self,
                                                     QmiClientWds *client,
                                                     gboolean enable,
                                                     guint *indication_id);

typedef enum {
    CONNECT_STEP_FIRST,
    CONNECT_STEP_OPEN_QMI_PORT,
    CONNECT_STEP_IP_METHOD,
    CONNECT_STEP_IPV4,
    CONNECT_STEP_WDS_CLIENT_IPV4,
    CONNECT_STEP_IP_FAMILY_IPV4,
    CONNECT_STEP_ENABLE_INDICATIONS_IPV4,
    CONNECT_STEP_START_NETWORK_IPV4,
    CONNECT_STEP_GET_CURRENT_SETTINGS_IPV4,
    CONNECT_STEP_IPV6,
    CONNECT_STEP_WDS_CLIENT_IPV6,
    CONNECT_STEP_IP_FAMILY_IPV6,
    CONNECT_STEP_ENABLE_INDICATIONS_IPV6,
    CONNECT_STEP_START_NETWORK_IPV6,
    CONNECT_STEP_GET_CURRENT_SETTINGS_IPV6,
    CONNECT_STEP_LAST
} ConnectStep;

typedef struct {
    MMBearerQmi *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    ConnectStep step;
    MMPort *data;
    MMPortQmi *qmi;
    gchar *user;
    gchar *password;
    gchar *apn;
    QmiWdsAuthentication auth;
    gboolean no_ip_family_preference;
    gboolean default_ip_family_set;

    MMBearerIpMethod ip_method;

    gboolean ipv4;
    gboolean running_ipv4;
    QmiClientWds *client_ipv4;
    guint packet_service_status_ipv4_indication_id;
    guint32 packet_data_handle_ipv4;
    MMBearerIpConfig *ipv4_config;
    GError *error_ipv4;

    gboolean ipv6;
    gboolean running_ipv6;
    QmiClientWds *client_ipv6;
    guint packet_service_status_ipv6_indication_id;
    guint32 packet_data_handle_ipv6;
    MMBearerIpConfig *ipv6_config;
    GError *error_ipv6;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_free (ctx->apn);
    g_free (ctx->user);
    g_free (ctx->password);

    if (ctx->packet_service_status_ipv4_indication_id) {
        common_setup_cleanup_unsolicited_events (ctx->self,
                                                 ctx->client_ipv4,
                                                 FALSE,
                                                 &ctx->packet_service_status_ipv4_indication_id);
    }
    if (ctx->packet_service_status_ipv6_indication_id) {
        common_setup_cleanup_unsolicited_events (ctx->self,
                                                 ctx->client_ipv6,
                                                 FALSE,
                                                 &ctx->packet_service_status_ipv6_indication_id);
    }

    g_clear_error (&ctx->error_ipv4);
    g_clear_error (&ctx->error_ipv6);
    g_clear_object (&ctx->client_ipv4);
    g_clear_object (&ctx->client_ipv6);
    g_clear_object (&ctx->ipv4_config);
    g_clear_object (&ctx->ipv6_config);
    g_object_unref (ctx->data);
    g_object_unref (ctx->qmi);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->self);
    g_slice_free (ConnectContext, ctx);
}

static MMBearerConnectResult *
connect_finish (MMBaseBearer *self,
                GAsyncResult *res,
                GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void connect_context_step (ConnectContext *ctx);

static void
start_network_ready (QmiClientWds *client,
                     GAsyncResult *res,
                     ConnectContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsStartNetworkOutput *output;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);
    g_assert (!(ctx->running_ipv4 && ctx->running_ipv6));

    output = qmi_client_wds_start_network_finish (client, res, &error);
    if (output &&
        !qmi_message_wds_start_network_output_get_result (output, &error)) {
        /* No-effect errors should be ignored. The modem will keep the
         * connection active as long as there is a WDS client which requested
         * to start the network. If ModemManager crashed while a connection was
         * active, we would be leaving an unreleased WDS client around and the
         * modem would just keep connected. */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_error_free (error);
            error = NULL;
            if (ctx->running_ipv4)
                ctx->packet_data_handle_ipv4 = GLOBAL_PACKET_DATA_HANDLE;
            else
                ctx->packet_data_handle_ipv6 = GLOBAL_PACKET_DATA_HANDLE;

            /* Fall down to a successful connection */
        } else {
            mm_info ("error: couldn't start network: %s", error->message);
            if (g_error_matches (error,
                                 QMI_PROTOCOL_ERROR,
                                 QMI_PROTOCOL_ERROR_CALL_FAILED)) {
                QmiWdsCallEndReason cer;
                QmiWdsVerboseCallEndReasonType verbose_cer_type;
                gint16 verbose_cer_reason;

                if (qmi_message_wds_start_network_output_get_call_end_reason (
                        output,
                        &cer,
                        NULL))
                    mm_info ("call end reason (%u): '%s'",
                             cer,
                             qmi_wds_call_end_reason_get_string (cer));

                if (qmi_message_wds_start_network_output_get_verbose_call_end_reason (
                        output,
                        &verbose_cer_type,
                        &verbose_cer_reason,
                        NULL))
                    mm_info ("verbose call end reason (%u,%d): [%s] %s",
                             verbose_cer_type,
                             verbose_cer_reason,
                             qmi_wds_verbose_call_end_reason_type_get_string (verbose_cer_type),
                             qmi_wds_verbose_call_end_reason_get_string (verbose_cer_type, verbose_cer_reason));
            }
        }
    }

    if (error) {
        if (ctx->running_ipv4)
            ctx->error_ipv4 = error;
        else
            ctx->error_ipv6 = error;
    } else {
        if (ctx->running_ipv4)
            qmi_message_wds_start_network_output_get_packet_data_handle (output, &ctx->packet_data_handle_ipv4, NULL);
        else
            qmi_message_wds_start_network_output_get_packet_data_handle (output, &ctx->packet_data_handle_ipv6, NULL);
    }

    if (output)
        qmi_message_wds_start_network_output_unref (output);

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static QmiMessageWdsStartNetworkInput *
build_start_network_input (ConnectContext *ctx)
{
    QmiMessageWdsStartNetworkInput *input;
    gboolean has_user, has_password;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);
    g_assert (!(ctx->running_ipv4 && ctx->running_ipv6));

    input = qmi_message_wds_start_network_input_new ();

    if (ctx->apn && ctx->apn[0])
        qmi_message_wds_start_network_input_set_apn (input, ctx->apn, NULL);

    has_user     = (ctx->user     && ctx->user[0]);
    has_password = (ctx->password && ctx->password[0]);

    /* Need to add auth info? */
    if (has_user || has_password || ctx->auth != QMI_WDS_AUTHENTICATION_NONE) {
        /* We define a valid auth preference if we have either user or password, or a explicit
         * request for one to be set. If no explicit one was given, default to PAP. */
        qmi_message_wds_start_network_input_set_authentication_preference (
            input,
            (ctx->auth != QMI_WDS_AUTHENTICATION_NONE) ? ctx->auth : QMI_WDS_AUTHENTICATION_PAP,
            NULL);

        if (has_user)
            qmi_message_wds_start_network_input_set_username (input, ctx->user, NULL);
        if (has_password)
            qmi_message_wds_start_network_input_set_password (input, ctx->password, NULL);
    }

    /* Only add the IP family preference TLV if explicitly requested a given
     * family. This TLV may be newer than the Start Network command itself, so
     * we'll just allow the case where none is specified. Also, don't add this
     * TLV if we already set a default IP family preference with "WDS Set IP
     * Family" */
    if (!ctx->no_ip_family_preference &&
        !ctx->default_ip_family_set) {
        qmi_message_wds_start_network_input_set_ip_family_preference (
            input,
            (ctx->running_ipv6 ? QMI_WDS_IP_FAMILY_IPV6 : QMI_WDS_IP_FAMILY_IPV4),
            NULL);
    }

    return input;
}

static void
qmi_inet4_ntop (guint32 address, char *buf, const gsize buflen)
{
    struct in_addr a = { .s_addr = GUINT32_TO_BE (address) };

    g_assert (buflen >= INET_ADDRSTRLEN);

    /* We can ignore inet_ntop() return value if 'buf' is
     * at least INET_ADDRSTRLEN in size. */
    memset (buf, 0, buflen);
    g_assert (inet_ntop (AF_INET, &a, buf, buflen));
}

static MMBearerIpConfig *
get_ipv4_config (MMBearerQmi *self,
                 MMBearerIpMethod ip_method,
                 QmiMessageWdsGetCurrentSettingsOutput *output,
                 guint32 mtu)
{
    MMBearerIpConfig *config;
    char buf[INET_ADDRSTRLEN];
    char buf2[INET_ADDRSTRLEN];
    const gchar *dns[3] = { 0 };
    guint dns_idx = 0;
    guint32 addr = 0;
    GError *error = NULL;
    guint32 prefix = 0;

    /* IPv4 subnet mask */
    if (!qmi_message_wds_get_current_settings_output_get_ipv4_gateway_subnet_mask (output, &addr, &error)) {
        mm_warn ("Failed to read IPv4 netmask (%s)", error->message);
        g_clear_error (&error);
        return NULL;
    }
    qmi_inet4_ntop (addr, buf, sizeof (buf));
    prefix = mm_netmask_to_cidr (buf);

    /* IPv4 address */
    if (!qmi_message_wds_get_current_settings_output_get_ipv4_address (output, &addr, &error)) {
        mm_warn ("IPv4 family but no IPv4 address (%s)", error->message);
        g_clear_error (&error);
        return NULL;
    }

    mm_info ("QMI IPv4 Settings:");

    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config, ip_method);

    /* IPv4 address */
    qmi_inet4_ntop (addr, buf, sizeof (buf));
    mm_bearer_ip_config_set_address (config, buf);
    mm_bearer_ip_config_set_prefix (config, prefix);
    mm_info ("    Address: %s/%d", buf, prefix);

    /* IPv4 gateway address */
    if (qmi_message_wds_get_current_settings_output_get_ipv4_gateway_address (output, &addr, &error)) {
        qmi_inet4_ntop (addr, buf, sizeof (buf));
        mm_bearer_ip_config_set_gateway (config, buf);
        mm_info ("    Gateway: %s", buf);
    } else {
        mm_info ("    Gateway: failed (%s)", error->message);
        g_clear_error (&error);
    }

    /* IPv4 DNS #1 */
    if (qmi_message_wds_get_current_settings_output_get_primary_ipv4_dns_address (output, &addr, &error)) {
        qmi_inet4_ntop (addr, buf, sizeof (buf));
        dns[dns_idx++] = buf;
        mm_info ("    DNS #1: %s", buf);
    } else {
        mm_info ("    DNS #1: failed (%s)", error->message);
        g_clear_error (&error);
    }

    /* IPv4 DNS #2 */
    if (qmi_message_wds_get_current_settings_output_get_secondary_ipv4_dns_address (output, &addr, &error)) {
        qmi_inet4_ntop (addr, buf2, sizeof (buf2));
        dns[dns_idx++] = buf2;
        mm_info ("    DNS #2: %s", buf2);
    } else {
        mm_info ("    DNS #2: failed (%s)", error->message);
        g_clear_error (&error);
    }

    if (dns_idx > 0)
        mm_bearer_ip_config_set_dns (config, (const gchar **) &dns);

    if (mtu) {
        mm_bearer_ip_config_set_mtu (config, mtu);
        mm_info ("       MTU: %d", mtu);
    }

    return config;
}

static void
qmi_inet6_ntop (GArray *array, char *buf, const gsize buflen)
{
    struct in6_addr a;
    guint32 i;

    g_assert (array);
    g_assert (array->len == 8);
    g_assert (buflen >= INET6_ADDRSTRLEN);

    for (i = 0; i < array->len; i++)
        a.s6_addr16[i] = GUINT16_TO_BE (g_array_index (array, guint16, i));

    /* We can ignore inet_ntop() return value if 'buf' is
     * at least INET6_ADDRSTRLEN in size. */
    memset (buf, 0, buflen);
    g_assert (inet_ntop (AF_INET6, &a, buf, buflen));
}

static MMBearerIpConfig *
get_ipv6_config (MMBearerQmi *self,
                 MMBearerIpMethod ip_method,
                 QmiMessageWdsGetCurrentSettingsOutput *output,
                 guint32 mtu)
{
    MMBearerIpConfig *config;
    char buf[INET6_ADDRSTRLEN];
    char buf2[INET6_ADDRSTRLEN];
    const gchar *dns[3] = { 0 };
    guint dns_idx = 0;
    GArray *array;
    GError *error = NULL;
    guint8 prefix = 0;

    /* If the message has an IPv6 address, create an IPv6 bearer config */
    if (!qmi_message_wds_get_current_settings_output_get_ipv6_address (output, &array, &prefix, &error)) {
        mm_warn ("IPv6 family but no IPv6 address (%s)", error->message);
        g_clear_error (&error);
        return NULL;
    }

    mm_info ("QMI IPv6 Settings:");

    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config, ip_method);

    /* IPv6 address */
    qmi_inet6_ntop (array, buf, sizeof (buf));

    mm_bearer_ip_config_set_address (config, buf);
    mm_bearer_ip_config_set_prefix (config, prefix);
    mm_info ("    Address: %s/%d", buf, prefix);

    /* IPv6 gateway address */
    if (qmi_message_wds_get_current_settings_output_get_ipv6_gateway_address (output, &array, &prefix, &error)) {
        qmi_inet6_ntop (array, buf, sizeof (buf));
        mm_bearer_ip_config_set_gateway (config, buf);
        mm_info ("    Gateway: %s/%d", buf, prefix);
    } else {
        mm_info ("    Gateway: failed (%s)", error->message);
        g_clear_error (&error);
    }

    /* IPv6 DNS #1 */
    if (qmi_message_wds_get_current_settings_output_get_ipv6_primary_dns_address (output, &array, &error)) {
        qmi_inet6_ntop (array, buf, sizeof (buf));
        dns[dns_idx++] = buf;
        mm_info ("    DNS #1: %s", buf);
    } else {
        mm_info ("    DNS #1: failed (%s)", error->message);
        g_clear_error (&error);
    }

    /* IPv6 DNS #2 */
    if (qmi_message_wds_get_current_settings_output_get_ipv6_secondary_dns_address (output, &array, &error)) {
        qmi_inet6_ntop (array, buf2, sizeof (buf2));
        dns[dns_idx++] = buf2;
        mm_info ("    DNS #2: %s", buf2);
    } else {
        mm_info ("    DNS #2: failed (%s)", error->message);
        g_clear_error (&error);
    }

    if (dns_idx > 0)
        mm_bearer_ip_config_set_dns (config, (const gchar **) &dns);

    if (mtu) {
        mm_bearer_ip_config_set_mtu (config, mtu);
        mm_info ("       MTU: %d", mtu);
    }

    return config;
}

static void
get_current_settings_ready (QmiClientWds *client,
                            GAsyncResult *res,
                            ConnectContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsGetCurrentSettingsOutput *output;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);

    output = qmi_client_wds_get_current_settings_finish (client, res, &error);
    if (!output ||
        !qmi_message_wds_get_current_settings_output_get_result (output, &error)) {
        /* Never treat this as a hard connection error; not all devices support
         * "WDS Get Current Settings" */
        mm_info ("error: couldn't get current settings: %s", error->message);
        g_error_free (error);
    } else {
        QmiWdsIpFamily ip_family = QMI_WDS_IP_FAMILY_UNSPECIFIED;
        guint32 mtu = 0;
        GArray *array;

        if (!qmi_message_wds_get_current_settings_output_get_ip_family (output, &ip_family, &error)) {
            mm_dbg (" IP Family: failed (%s); assuming IPv4", error->message);
            g_clear_error (&error);
            ip_family = QMI_WDS_IP_FAMILY_IPV4;
        }
        mm_dbg (" IP Family: %s",
                (ip_family == QMI_WDS_IP_FAMILY_IPV4) ? "IPv4" :
                   (ip_family == QMI_WDS_IP_FAMILY_IPV6) ? "IPv6" : "unknown");

        if (!qmi_message_wds_get_current_settings_output_get_mtu (output, &mtu, &error)) {
            mm_dbg ("       MTU: failed (%s)", error->message);
            g_clear_error (&error);
        }

        if (ip_family == QMI_WDS_IP_FAMILY_IPV4)
            ctx->ipv4_config = get_ipv4_config (ctx->self, ctx->ip_method, output, mtu);
        else if (ip_family == QMI_WDS_IP_FAMILY_IPV6)
            ctx->ipv6_config = get_ipv6_config (ctx->self, ctx->ip_method, output, mtu);

        /* Domain names */
        if (qmi_message_wds_get_current_settings_output_get_domain_name_list (output, &array, &error)) {
            GString *s = g_string_sized_new (array ? (array->len * 20) : 1);
            guint i;

            for (i = 0; array && (i < array->len); i++) {
                if (s->len)
                    g_string_append (s, ", ");
                g_string_append (s, g_array_index (array, const char *, i));
            }
            mm_dbg ("   Domains: %s", s->str);
            g_string_free (s, TRUE);
        } else {
            mm_dbg ("   Domains: failed (%s)", error ? error->message : "unknown");
            g_clear_error (&error);
        }
    }

    if (output)
        qmi_message_wds_get_current_settings_output_unref (output);

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
get_current_settings (ConnectContext *ctx, QmiClientWds *client)
{
    QmiMessageWdsGetCurrentSettingsInput *input;
    QmiWdsGetCurrentSettingsRequestedSettings requested;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);

    requested = QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_DNS_ADDRESS |
                QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_GRANTED_QOS |
                QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_IP_ADDRESS |
                QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_GATEWAY_INFO |
                QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_MTU |
                QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_DOMAIN_NAME_LIST |
                QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_IP_FAMILY;

    input = qmi_message_wds_get_current_settings_input_new ();
    qmi_message_wds_get_current_settings_input_set_requested_settings (input, requested, NULL);
    qmi_client_wds_get_current_settings (client,
                                         input,
                                         10,
                                         ctx->cancellable,
                                         (GAsyncReadyCallback)get_current_settings_ready,
                                         ctx);
    qmi_message_wds_get_current_settings_input_unref (input);
}

static void
set_ip_family_ready (QmiClientWds *client,
                     GAsyncResult *res,
                     ConnectContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsSetIpFamilyOutput *output;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);
    g_assert (!(ctx->running_ipv4 && ctx->running_ipv6));

    output = qmi_client_wds_set_ip_family_finish (client, res, &error);
    if (output) {
        qmi_message_wds_set_ip_family_output_get_result (output, &error);
        qmi_message_wds_set_ip_family_output_unref (output);
    }

    if (error) {
        /* Ensure we add the IP family preference TLV */
        mm_dbg ("Couldn't set IP family preference: '%s'", error->message);
        g_error_free (error);
        ctx->default_ip_family_set = FALSE;
    } else {
        /* No need to add IP family preference */
        ctx->default_ip_family_set = TRUE;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
packet_service_status_indication_cb (QmiClientWds *client,
                                     QmiIndicationWdsPacketServiceStatusOutput *output,
                                     MMBearerQmi *self)
{
    QmiWdsConnectionStatus connection_status;

    if (qmi_indication_wds_packet_service_status_output_get_connection_status (
            output,
            &connection_status,
            NULL,
            NULL)) {
        MMBearerConnectionStatus bearer_status = mm_base_bearer_get_status (MM_BASE_BEARER (self));

        if (connection_status == QMI_WDS_CONNECTION_STATUS_DISCONNECTED &&
            bearer_status != MM_BEARER_CONNECTION_STATUS_DISCONNECTED &&
            bearer_status != MM_BEARER_CONNECTION_STATUS_DISCONNECTING) {
            QmiWdsCallEndReason cer;
            QmiWdsVerboseCallEndReasonType verbose_cer_type;
            gint16 verbose_cer_reason;

            if (qmi_indication_wds_packet_service_status_output_get_call_end_reason (
                    output,
                    &cer,
                    NULL))
                mm_info ("bearer call end reason (%u): '%s'",
                         cer,
                         qmi_wds_call_end_reason_get_string (cer));

            if (qmi_indication_wds_packet_service_status_output_get_verbose_call_end_reason (
                    output,
                    &verbose_cer_type,
                    &verbose_cer_reason,
                    NULL))
                mm_info ("bearer verbose call end reason (%u,%d): [%s] %s",
                         verbose_cer_type,
                         verbose_cer_reason,
                         qmi_wds_verbose_call_end_reason_type_get_string (verbose_cer_type),
                         qmi_wds_verbose_call_end_reason_get_string (verbose_cer_type, verbose_cer_reason));

            mm_base_bearer_report_connection_status (MM_BASE_BEARER (self), MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
        }
    }
}

static void
common_setup_cleanup_unsolicited_events (MMBearerQmi *self,
                                         QmiClientWds *client,
                                         gboolean enable,
                                         guint *indication_id)
{
    if (!client)
        return;

    /* Connect/Disconnect "Packet Service Status" indications */
    if (enable) {
        g_assert (*indication_id == 0);
        *indication_id =
            g_signal_connect (client,
                              "packet-service-status",
                              G_CALLBACK (packet_service_status_indication_cb),
                              self);
    } else {
        g_assert (*indication_id != 0);
        g_signal_handler_disconnect (client, *indication_id);
        *indication_id = 0;
    }
}

static void
qmi_port_allocate_client_ready (MMPortQmi *qmi,
                                GAsyncResult *res,
                                ConnectContext *ctx)
{
    GError *error = NULL;

    g_assert (ctx->running_ipv4 || ctx->running_ipv6);
    g_assert (!(ctx->running_ipv4 && ctx->running_ipv6));

    if (!mm_port_qmi_allocate_client_finish (qmi, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    if (ctx->running_ipv4)
        ctx->client_ipv4 = QMI_CLIENT_WDS (mm_port_qmi_get_client (qmi,
                                                                   QMI_SERVICE_WDS,
                                                                   MM_PORT_QMI_FLAG_WDS_IPV4));
    else
        ctx->client_ipv6 = QMI_CLIENT_WDS (mm_port_qmi_get_client (qmi,
                                                                   QMI_SERVICE_WDS,
                                                                   MM_PORT_QMI_FLAG_WDS_IPV6));

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
qmi_port_open_ready (MMPortQmi *qmi,
                     GAsyncResult *res,
                     ConnectContext *ctx)
{
    GError *error = NULL;

    if (!mm_port_qmi_open_finish (qmi, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_context_complete_and_free (ctx);
        return;
    }

    /* Keep on */
    ctx->step++;
    connect_context_step (ctx);
}

static void
connect_context_step (ConnectContext *ctx)
{
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

        g_assert (ctx->ipv4 || ctx->ipv6);

        /* Fall down */
        ctx->step++;

    case CONNECT_STEP_OPEN_QMI_PORT:
        if (!mm_port_qmi_is_open (ctx->qmi)) {
            mm_port_qmi_open (ctx->qmi,
                              TRUE,
                              ctx->cancellable,
                              (GAsyncReadyCallback)qmi_port_open_ready,
                              ctx);
            return;
        }

        /* If already open, just fall down */
        ctx->step++;

    case CONNECT_STEP_IP_METHOD:
        /* Once the QMI port is open, we decide the IP method we're going
         * to request. If the LLP is raw-ip, we force Static IP, because not
         * all DHCP clients support the raw-ip interfaces; otherwise default
         * to DHCP as always. */
        if (mm_port_qmi_llp_is_raw_ip (ctx->qmi))
            ctx->ip_method = MM_BEARER_IP_METHOD_STATIC;
        else
            ctx->ip_method = MM_BEARER_IP_METHOD_DHCP;

        mm_dbg ("Defaulting to use %s IP method", mm_bearer_ip_method_get_string (ctx->ip_method));

        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_IPV4:
        /* If no IPv4 setup needed, jump to IPv6 */
        if (!ctx->ipv4) {
            ctx->step = CONNECT_STEP_IPV6;
            connect_context_step (ctx);
            return;
        }

        /* Start IPv4 setup */
        mm_dbg ("Running IPv4 connection setup");
        ctx->running_ipv4 = TRUE;
        ctx->running_ipv6 = FALSE;
        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_WDS_CLIENT_IPV4: {
        QmiClient *client;

        client = mm_port_qmi_get_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         MM_PORT_QMI_FLAG_WDS_IPV4);
        if (!client) {
            mm_dbg ("Allocating IPv4-specific WDS client");
            mm_port_qmi_allocate_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         MM_PORT_QMI_FLAG_WDS_IPV4,
                                         ctx->cancellable,
                                         (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                         ctx);
            return;
        }

        ctx->client_ipv4 = QMI_CLIENT_WDS (client);
        /* Just fall down */
        ctx->step++;
    }

    case CONNECT_STEP_IP_FAMILY_IPV4:
        /* If client is new enough, select IP family */
        if (!ctx->no_ip_family_preference &&
            qmi_client_check_version (QMI_CLIENT (ctx->client_ipv4), 1, 9)) {
            QmiMessageWdsSetIpFamilyInput *input;

            mm_dbg ("Setting default IP family to: IPv4");
            input = qmi_message_wds_set_ip_family_input_new ();
            qmi_message_wds_set_ip_family_input_set_preference (input, QMI_WDS_IP_FAMILY_IPV4, NULL);
            qmi_client_wds_set_ip_family (ctx->client_ipv4,
                                          input,
                                          10,
                                          ctx->cancellable,
                                          (GAsyncReadyCallback)set_ip_family_ready,
                                          ctx);
            qmi_message_wds_set_ip_family_input_unref (input);
            return;
        }

        ctx->default_ip_family_set = FALSE;

        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_ENABLE_INDICATIONS_IPV4:
        common_setup_cleanup_unsolicited_events (ctx->self,
                                                 ctx->client_ipv4,
                                                 TRUE,
                                                 &ctx->packet_service_status_ipv4_indication_id);
        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_START_NETWORK_IPV4: {
        QmiMessageWdsStartNetworkInput *input;

        mm_dbg ("Starting IPv4 connection...");
        input = build_start_network_input (ctx);
        qmi_client_wds_start_network (ctx->client_ipv4,
                                      input,
                                      45,
                                      ctx->cancellable,
                                      (GAsyncReadyCallback)start_network_ready,
                                      ctx);
        qmi_message_wds_start_network_input_unref (input);
        return;
    }

    case CONNECT_STEP_GET_CURRENT_SETTINGS_IPV4: {
        /* Retrieve and print IP configuration */
        if (ctx->packet_data_handle_ipv4) {
            mm_dbg ("Getting IPv4 configuration...");
            get_current_settings (ctx, ctx->client_ipv4);
            return;
        }
        /* Fall through */
        ctx->step++;
    }

    case CONNECT_STEP_IPV6:
        /* If no IPv6 setup needed, jump to last */
        if (!ctx->ipv6) {
            ctx->step = CONNECT_STEP_LAST;
            connect_context_step (ctx);
            return;
        }

        /* Start IPv6 setup */
        mm_dbg ("Running IPv6 connection setup");
        ctx->running_ipv4 = FALSE;
        ctx->running_ipv6 = TRUE;
        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_WDS_CLIENT_IPV6: {
        QmiClient *client;

        client = mm_port_qmi_get_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         MM_PORT_QMI_FLAG_WDS_IPV6);
        if (!client) {
            mm_dbg ("Allocating IPv6-specific WDS client");
            mm_port_qmi_allocate_client (ctx->qmi,
                                         QMI_SERVICE_WDS,
                                         MM_PORT_QMI_FLAG_WDS_IPV6,
                                         ctx->cancellable,
                                         (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                         ctx);
            return;
        }

        ctx->client_ipv6 = QMI_CLIENT_WDS (client);
        /* Just fall down */
        ctx->step++;
    }

    case CONNECT_STEP_IP_FAMILY_IPV6:

        g_assert (ctx->no_ip_family_preference == FALSE);

        /* If client is new enough, select IP family */
        if (qmi_client_check_version (QMI_CLIENT (ctx->client_ipv6), 1, 9)) {
            QmiMessageWdsSetIpFamilyInput *input;

            mm_dbg ("Setting default IP family to: IPv6");
            input = qmi_message_wds_set_ip_family_input_new ();
            qmi_message_wds_set_ip_family_input_set_preference (input, QMI_WDS_IP_FAMILY_IPV6, NULL);
            qmi_client_wds_set_ip_family (ctx->client_ipv6,
                                          input,
                                          10,
                                          ctx->cancellable,
                                          (GAsyncReadyCallback)set_ip_family_ready,
                                          ctx);
            qmi_message_wds_set_ip_family_input_unref (input);
            return;
        }

        ctx->default_ip_family_set = FALSE;

        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_ENABLE_INDICATIONS_IPV6:
        common_setup_cleanup_unsolicited_events (ctx->self,
                                                 ctx->client_ipv6,
                                                 TRUE,
                                                 &ctx->packet_service_status_ipv6_indication_id);
        /* Just fall down */
        ctx->step++;

    case CONNECT_STEP_START_NETWORK_IPV6: {
        QmiMessageWdsStartNetworkInput *input;

        mm_dbg ("Starting IPv6 connection...");
        input = build_start_network_input (ctx);
        qmi_client_wds_start_network (ctx->client_ipv6,
                                      input,
                                      45,
                                      ctx->cancellable,
                                      (GAsyncReadyCallback)start_network_ready,
                                      ctx);
        qmi_message_wds_start_network_input_unref (input);
        return;
    }

    case CONNECT_STEP_GET_CURRENT_SETTINGS_IPV6: {
        /* Retrieve and print IP configuration */
        if (ctx->packet_data_handle_ipv6) {
            mm_dbg ("Getting IPv6 configuration...");
            get_current_settings (ctx, ctx->client_ipv6);
            return;
        }
        /* Fall through */
        ctx->step++;
    }

    case CONNECT_STEP_LAST:
        /* If one of IPv4 or IPv6 succeeds, we're connected */
        if (ctx->packet_data_handle_ipv4 || ctx->packet_data_handle_ipv6) {
            /* Port is connected; update the state */
            mm_port_set_connected (MM_PORT (ctx->data), TRUE);

            /* Keep connection related data */
            g_assert (ctx->self->priv->data == NULL);
            ctx->self->priv->data = g_object_ref (ctx->data);

            g_assert (ctx->self->priv->packet_data_handle_ipv4 == 0);
            g_assert (ctx->self->priv->client_ipv4 == NULL);
            if (ctx->packet_data_handle_ipv4) {
                ctx->self->priv->packet_data_handle_ipv4 = ctx->packet_data_handle_ipv4;
                ctx->self->priv->packet_service_status_ipv4_indication_id = ctx->packet_service_status_ipv4_indication_id;
                ctx->packet_service_status_ipv4_indication_id = 0;
                ctx->self->priv->client_ipv4 = g_object_ref (ctx->client_ipv4);
            }

            g_assert (ctx->self->priv->packet_data_handle_ipv6 == 0);
            g_assert (ctx->self->priv->client_ipv6 == NULL);
            if (ctx->packet_data_handle_ipv6) {
                ctx->self->priv->packet_data_handle_ipv6 = ctx->packet_data_handle_ipv6;
                ctx->self->priv->packet_service_status_ipv6_indication_id = ctx->packet_service_status_ipv6_indication_id;
                ctx->packet_service_status_ipv6_indication_id = 0;
                ctx->self->priv->client_ipv6 = g_object_ref (ctx->client_ipv6);
            }

            /* Set operation result */
            g_simple_async_result_set_op_res_gpointer (
                ctx->result,
                mm_bearer_connect_result_new (ctx->data, ctx->ipv4_config, ctx->ipv6_config),
                (GDestroyNotify)mm_bearer_connect_result_unref);
        } else {
            GError *error;

            /* No connection, set error. If both set, IPv4 error preferred */
            if (ctx->error_ipv4) {
                error = ctx->error_ipv4;
                ctx->error_ipv4 = NULL;
            } else {
                error = ctx->error_ipv6;
                ctx->error_ipv6 = NULL;
            }

            g_simple_async_result_take_error (ctx->result, error);
        }

        connect_context_complete_and_free (ctx);
        return;
    }
}

static void
_connect (MMBaseBearer *self,
          GCancellable *cancellable,
          GAsyncReadyCallback callback,
          gpointer user_data)
{
    MMBearerProperties *properties = NULL;
    ConnectContext *ctx;
    MMBaseModem *modem  = NULL;
    MMPort *data;
    MMPortQmi *qmi;
    GError *error = NULL;
    const gchar *apn;

    g_object_get (self,
                  MM_BASE_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem);

    /* Grab a data port */
    data = mm_base_modem_get_best_data_port (modem, MM_PORT_TYPE_NET);
    if (!data) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_NOT_FOUND,
            "No valid data port found to launch connection");
        g_object_unref (modem);
        return;
    }

    /* Each data port has a single QMI port associated */
    qmi = mm_base_modem_get_port_qmi_for_data (modem, data, &error);
    if (!qmi) {
        g_simple_async_report_take_gerror_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            error);
        g_object_unref (data);
        g_object_unref (modem);
        return;
    }

    /* Check whether we have an APN */
    apn = mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));

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

    /* Is this a 3GPP2 only modem and APN was given? If so, error */
    if (mm_iface_modem_is_cdma_only (MM_IFACE_MODEM (modem)) && apn) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_INVALID_ARGS,
            "3GPP2 doesn't support APN setting");
        g_object_unref (modem);
        return;
    }

    g_object_unref (modem);

    mm_dbg ("Launching connection with QMI port (%s/%s) and data port (%s/%s)",
            mm_port_subsys_get_string (mm_port_get_subsys (MM_PORT (qmi))),
            mm_port_get_device (MM_PORT (qmi)),
            mm_port_subsys_get_string (mm_port_get_subsys (data)),
            mm_port_get_device (data));

    ctx = g_slice_new0 (ConnectContext);
    ctx->self = g_object_ref (self);
    ctx->qmi = qmi;
    ctx->data = data;
    ctx->cancellable = g_object_ref (cancellable);
    ctx->step = CONNECT_STEP_FIRST;
    ctx->ip_method = MM_BEARER_IP_METHOD_UNKNOWN;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    g_object_get (self,
                  MM_BASE_BEARER_CONFIG, &properties,
                  NULL);

    if (properties) {
        MMBearerAllowedAuth auth;
        MMBearerIpFamily ip_family;

        ctx->apn = g_strdup (mm_bearer_properties_get_apn (properties));
        ctx->user = g_strdup (mm_bearer_properties_get_user (properties));
        ctx->password = g_strdup (mm_bearer_properties_get_password (properties));

        ip_family = mm_bearer_properties_get_ip_type (properties);
        if (ip_family == MM_BEARER_IP_FAMILY_NONE ||
            ip_family == MM_BEARER_IP_FAMILY_ANY) {
            gchar *ip_family_str;

            ip_family = mm_base_bearer_get_default_ip_family (self);
            ip_family_str = mm_bearer_ip_family_build_string_from_mask (ip_family);
            mm_dbg ("No specific IP family requested, defaulting to %s",
                    ip_family_str);
            ctx->no_ip_family_preference = TRUE;
            g_free (ip_family_str);
        }

        if (ip_family & MM_BEARER_IP_FAMILY_IPV4)
            ctx->ipv4 = TRUE;
        if (ip_family & MM_BEARER_IP_FAMILY_IPV6)
            ctx->ipv6 = TRUE;
        if (ip_family & MM_BEARER_IP_FAMILY_IPV4V6) {
            ctx->ipv4 = TRUE;
            ctx->ipv6 = TRUE;
        }

        if (!ctx->ipv4 && !ctx->ipv6) {
            gchar *str;

            str = mm_bearer_ip_family_build_string_from_mask (ip_family);
            g_simple_async_result_set_error (
                ctx->result,
                MM_CORE_ERROR,
                MM_CORE_ERROR_UNSUPPORTED,
                "Unsupported IP type requested: '%s'",
                str);
            g_free (str);
            connect_context_complete_and_free (ctx);
            return;
        }

        auth = mm_bearer_properties_get_allowed_auth (properties);
        g_object_unref (properties);

        if (auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
            /* We'll default to PAP later if needed */
            ctx->auth = QMI_WDS_AUTHENTICATION_NONE;
        } else if (auth & (MM_BEARER_ALLOWED_AUTH_PAP |
                           MM_BEARER_ALLOWED_AUTH_CHAP |
                           MM_BEARER_ALLOWED_AUTH_NONE)) {
            /* Only PAP and/or CHAP or NONE are supported */
            ctx->auth = mm_bearer_allowed_auth_to_qmi_authentication (auth);
        } else {
            gchar *str;

            str = mm_bearer_allowed_auth_build_string_from_mask (auth);
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

    /* Run! */
    connect_context_step (ctx);
}

/*****************************************************************************/
/* Disconnect */

typedef enum {
    DISCONNECT_STEP_FIRST,
    DISCONNECT_STEP_STOP_NETWORK_IPV4,
    DISCONNECT_STEP_STOP_NETWORK_IPV6,
    DISCONNECT_STEP_LAST
} DisconnectStep;

typedef struct {
    MMBearerQmi *self;
    GSimpleAsyncResult *result;
    MMPort *data;
    DisconnectStep step;

    gboolean running_ipv4;
    QmiClientWds *client_ipv4;
    guint32 packet_data_handle_ipv4;
    GError *error_ipv4;

    gboolean running_ipv6;
    QmiClientWds *client_ipv6;
    guint32 packet_data_handle_ipv6;
    GError *error_ipv6;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->error_ipv4)
        g_error_free (ctx->error_ipv4);
    if (ctx->error_ipv6)
        g_error_free (ctx->error_ipv6);
    if (ctx->client_ipv4)
        g_object_unref (ctx->client_ipv4);
    if (ctx->client_ipv6)
        g_object_unref (ctx->client_ipv6);
    g_object_unref (ctx->data);
    g_object_unref (ctx->self);
    g_slice_free (DisconnectContext, ctx);
}

static gboolean
disconnect_finish (MMBaseBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
reset_bearer_connection (MMBearerQmi *self,
                         gboolean reset_ipv4,
                         gboolean reset_ipv6)
{
    if (reset_ipv4) {
        self->priv->packet_data_handle_ipv4 = 0;
        g_clear_object (&self->priv->client_ipv4);
    }

    if (reset_ipv6) {
        self->priv->packet_data_handle_ipv6 = 0;
        g_clear_object (&self->priv->client_ipv6);
    }

    if (!self->priv->packet_data_handle_ipv4 &&
        !self->priv->packet_data_handle_ipv6) {
        if (self->priv->data) {
            /* Port is disconnected; update the state */
            mm_port_set_connected (self->priv->data, FALSE);
            g_clear_object (&self->priv->data);
        }
    }
}

static void disconnect_context_step (DisconnectContext *ctx);

static void
stop_network_ready (QmiClientWds *client,
                    GAsyncResult *res,
                    DisconnectContext *ctx)
{
    GError *error = NULL;
    QmiMessageWdsStopNetworkOutput *output;

    output = qmi_client_wds_stop_network_finish (client, res, &error);
    if (output &&
        !qmi_message_wds_stop_network_output_get_result (output, &error)) {
        /* No effect error, we're already disconnected */
        if (g_error_matches (error,
                             QMI_PROTOCOL_ERROR,
                             QMI_PROTOCOL_ERROR_NO_EFFECT)) {
            g_error_free (error);
            error = NULL;
        }
    }

    if (error) {
        if (ctx->running_ipv4)
            ctx->error_ipv4 = error;
        else
            ctx->error_ipv6 = error;
    } else {
        /* Clear internal status */
        reset_bearer_connection (ctx->self,
                                 ctx->running_ipv4,
                                 ctx->running_ipv6);
    }

    if (output)
        qmi_message_wds_stop_network_output_unref (output);

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

    case DISCONNECT_STEP_STOP_NETWORK_IPV4:
        if (ctx->packet_data_handle_ipv4) {
            QmiMessageWdsStopNetworkInput *input;

            common_setup_cleanup_unsolicited_events (ctx->self,
                                                     ctx->client_ipv4,
                                                     FALSE,
                                                     &ctx->self->priv->packet_service_status_ipv4_indication_id);

            input = qmi_message_wds_stop_network_input_new ();
            qmi_message_wds_stop_network_input_set_packet_data_handle (input, ctx->packet_data_handle_ipv4, NULL);

            ctx->running_ipv4 = TRUE;
            ctx->running_ipv6 = FALSE;
            qmi_client_wds_stop_network (ctx->client_ipv4,
                                         input,
                                         30,
                                         NULL,
                                         (GAsyncReadyCallback)stop_network_ready,
                                         ctx);
            return;
        }

        /* Fall down */
        ctx->step++;

    case DISCONNECT_STEP_STOP_NETWORK_IPV6:
        if (ctx->packet_data_handle_ipv6) {
            QmiMessageWdsStopNetworkInput *input;

            common_setup_cleanup_unsolicited_events (ctx->self,
                                                     ctx->client_ipv6,
                                                     FALSE,
                                                     &ctx->self->priv->packet_service_status_ipv6_indication_id);

            input = qmi_message_wds_stop_network_input_new ();
            qmi_message_wds_stop_network_input_set_packet_data_handle (input, ctx->packet_data_handle_ipv6, NULL);

            ctx->running_ipv4 = FALSE;
            ctx->running_ipv6 = TRUE;
            qmi_client_wds_stop_network (ctx->client_ipv6,
                                         input,
                                         30,
                                         NULL,
                                         (GAsyncReadyCallback)stop_network_ready,
                                         ctx);
            return;
        }

        /* Fall down */
        ctx->step++;

    case DISCONNECT_STEP_LAST:
        if (!ctx->error_ipv4 && !ctx->error_ipv6)
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        else {
            GError *error;

            /* If both set, IPv4 error preferred */
            if (ctx->error_ipv4) {
                error = ctx->error_ipv4;
                ctx->error_ipv4 = NULL;
            } else {
                error = ctx->error_ipv6;
                ctx->error_ipv6 = NULL;
            }

            g_simple_async_result_take_error (ctx->result, error);
        }

        disconnect_context_complete_and_free (ctx);
        return;
    }
}

static void
disconnect (MMBaseBearer *_self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMBearerQmi *self = MM_BEARER_QMI (_self);
    DisconnectContext *ctx;

    if ((!self->priv->packet_data_handle_ipv4 && !self->priv->packet_data_handle_ipv6) ||
        (!self->priv->client_ipv4 && !self->priv->client_ipv6) ||
        !self->priv->data) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect QMI bearer: this bearer is not connected");
        return;
    }

    ctx = g_slice_new0 (DisconnectContext);
    ctx->self = g_object_ref (self);
    ctx->data = g_object_ref (self->priv->data);
    ctx->client_ipv4 = self->priv->client_ipv4 ? g_object_ref (self->priv->client_ipv4) : NULL;
    ctx->packet_data_handle_ipv4 = self->priv->packet_data_handle_ipv4;
    ctx->client_ipv6 = self->priv->client_ipv6 ? g_object_ref (self->priv->client_ipv6) : NULL;
    ctx->packet_data_handle_ipv6 = self->priv->packet_data_handle_ipv6;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect);
    ctx->step = DISCONNECT_STEP_FIRST;

    /* Run! */
    disconnect_context_step (ctx);
}

/*****************************************************************************/

static void
report_connection_status (MMBaseBearer *self,
                          MMBearerConnectionStatus status)
{
    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED)
        /* Cleanup all connection related data */
        reset_bearer_connection (MM_BEARER_QMI (self), TRUE, TRUE);

    /* Chain up parent's report_connection_status() */
    MM_BASE_BEARER_CLASS (mm_bearer_qmi_parent_class)->report_connection_status (self, status);
}

/*****************************************************************************/

MMBaseBearer *
mm_bearer_qmi_new (MMBroadbandModemQmi *modem,
                   MMBearerProperties  *config)
{
    MMBaseBearer *bearer;

    /* The Qmi bearer inherits from MMBaseBearer (so it's not a MMBroadbandBearer)
     * and that means that the object is not async-initable, so we just use
     * g_object_new() here */
    bearer = g_object_new (MM_TYPE_BEARER_QMI,
                           MM_BASE_BEARER_MODEM, modem,
                           MM_BASE_BEARER_CONFIG, config,
                           NULL);

    /* Only export valid bearers */
    mm_base_bearer_export (bearer);

    return bearer;
}

static void
mm_bearer_qmi_init (MMBearerQmi *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BEARER_QMI,
                                              MMBearerQmiPrivate);
}

static void
dispose (GObject *object)
{
    MMBearerQmi *self = MM_BEARER_QMI (object);

    if (self->priv->packet_service_status_ipv4_indication_id) {
        common_setup_cleanup_unsolicited_events (self,
                                                 self->priv->client_ipv4,
                                                 FALSE,
                                                 &self->priv->packet_service_status_ipv4_indication_id);
    }
    if (self->priv->packet_service_status_ipv6_indication_id) {
        common_setup_cleanup_unsolicited_events (self,
                                                 self->priv->client_ipv6,
                                                 FALSE,
                                                 &self->priv->packet_service_status_ipv6_indication_id);
    }

    g_clear_object (&self->priv->data);
    g_clear_object (&self->priv->client_ipv4);
    g_clear_object (&self->priv->client_ipv6);

    G_OBJECT_CLASS (mm_bearer_qmi_parent_class)->dispose (object);
}

static void
mm_bearer_qmi_class_init (MMBearerQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerQmiPrivate));

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
}
