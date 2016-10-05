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
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 * Copyright (C) 2012 Huawei Technologies Co., Ltd
 *
 * Author: Franko fang <huanahu@huawei.com>
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <ModemManager.h>
#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-huawei.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-huawei.h"
#include "mm-daemon-enums-types.h"

G_DEFINE_TYPE (MMBroadbandBearerHuawei, mm_broadband_bearer_huawei, MM_TYPE_BROADBAND_BEARER)

struct _MMBroadbandBearerHuaweiPrivate {
    gpointer connect_pending;
    gpointer disconnect_pending;
    /* Tag for the post task for network-initiated disconnect */
    guint network_disconnect_pending_id;
};

/*****************************************************************************/

static MMPortSerialAt *
get_dial_port (MMBroadbandModemHuawei *modem,
               MMPort                 *data,
               MMPortSerialAt         *primary)
{
    MMPortSerialAt *dial_port;

    /* See if we have a cdc-wdm AT port for the interface */
    dial_port = (mm_broadband_modem_huawei_peek_port_at_for_data (
                     MM_BROADBAND_MODEM_HUAWEI (modem), data));
    if (dial_port)
        return g_object_ref (dial_port);

    /* Otherwise, fallback to using the primary port for dialing */
    return g_object_ref (primary);
}

/*****************************************************************************/
/* Connect 3GPP */

typedef enum {
    CONNECT_3GPP_CONTEXT_STEP_FIRST = 0,
    CONNECT_3GPP_CONTEXT_STEP_NDISDUP,
    CONNECT_3GPP_CONTEXT_STEP_NDISSTATQRY,
    CONNECT_3GPP_CONTEXT_STEP_IP_CONFIG,
    CONNECT_3GPP_CONTEXT_STEP_LAST
} Connect3gppContextStep;

typedef struct {
    MMBroadbandBearerHuawei *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPort *data;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    Connect3gppContextStep step;
    guint check_count;
    guint failed_ndisstatqry_count;
    MMBearerIpConfig *ipv4_config;
} Connect3gppContext;

static void
connect_3gpp_context_complete_and_free (Connect3gppContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);

    g_clear_object (&ctx->ipv4_config);
    g_clear_object (&ctx->data);
    g_clear_object (&ctx->primary);

    g_slice_free (Connect3gppContext, ctx);
}

static MMBearerConnectResult *
connect_3gpp_finish (MMBroadbandBearer *self,
                     GAsyncResult *res,
                     GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void connect_3gpp_context_step (Connect3gppContext *ctx);

static void
connect_dhcp_check_ready (MMBaseModem *modem,
                          GAsyncResult *res,
                          MMBroadbandBearerHuawei *self)
{
    Connect3gppContext *ctx;
    const gchar *response;
    GError *error = NULL;

    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    /* Default to automatic/DHCP addressing */
    ctx->ipv4_config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (ctx->ipv4_config, MM_BEARER_IP_METHOD_DHCP);

    /* Cache IPv4 details if available, otherwise clients will have to use DHCP */
    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (response) {
        guint address = 0;
        guint prefix = 0;
        guint gateway = 0;
        guint dns1 = 0;
        guint dns2 = 0;

        if (mm_huawei_parse_dhcp_response (response,
                                           &address,
                                           &prefix,
                                           &gateway,
                                           &dns1,
                                           &dns2,
                                           &error)) {
            GInetAddress *addr;
            gchar *strarr[3] = { NULL, NULL, NULL };
            guint n = 0;
            gchar *str;

            mm_bearer_ip_config_set_method (ctx->ipv4_config, MM_BEARER_IP_METHOD_STATIC);

            addr = g_inet_address_new_from_bytes ((guint8 *)&address, G_SOCKET_FAMILY_IPV4);
            str = g_inet_address_to_string (addr);
            mm_bearer_ip_config_set_address (ctx->ipv4_config, str);
            g_free (str);
            g_object_unref (addr);

            /* Netmask */
            mm_bearer_ip_config_set_prefix (ctx->ipv4_config, prefix);

            /* Gateway */
            addr = g_inet_address_new_from_bytes ((guint8 *)&gateway, G_SOCKET_FAMILY_IPV4);
            str = g_inet_address_to_string (addr);
            mm_bearer_ip_config_set_gateway (ctx->ipv4_config, str);
            g_free (str);
            g_object_unref (addr);

            /* DNS */
            if (dns1) {
                addr = g_inet_address_new_from_bytes ((guint8 *)&dns1, G_SOCKET_FAMILY_IPV4);
                strarr[n++] = g_inet_address_to_string (addr);
                g_object_unref (addr);
            }
            if (dns2) {
                addr = g_inet_address_new_from_bytes ((guint8 *)&dns2, G_SOCKET_FAMILY_IPV4);
                strarr[n++] = g_inet_address_to_string (addr);
                g_object_unref (addr);
            }
            mm_bearer_ip_config_set_dns (ctx->ipv4_config, (const gchar **)strarr);
            g_free (strarr[0]);
            g_free (strarr[1]);
        } else {
            mm_dbg ("Unexpected response to ^DHCP command: %s", error->message);
        }
    }

    g_clear_error (&error);
    ctx->step++;
    connect_3gpp_context_step (ctx);
}

static gboolean
connect_retry_ndisstatqry_check_cb (MMBroadbandBearerHuawei *self)
{
    Connect3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    /* Retry same step */
    connect_3gpp_context_step (ctx);

    return G_SOURCE_REMOVE;
}

static void
connect_ndisstatqry_check_ready (MMBaseModem *modem,
                                 GAsyncResult *res,
                                 MMBroadbandBearerHuawei *self)
{
    Connect3gppContext *ctx;
    const gchar *response;
    GError *error = NULL;
    gboolean ipv4_available = FALSE;
    gboolean ipv4_connected = FALSE;
    gboolean ipv6_available = FALSE;
    gboolean ipv6_connected = FALSE;

    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!response ||
        !mm_huawei_parse_ndisstatqry_response (response,
                                               &ipv4_available,
                                               &ipv4_connected,
                                               &ipv6_available,
                                               &ipv6_connected,
                                               &error)) {
        ctx->failed_ndisstatqry_count++;
        mm_dbg ("Unexpected response to ^NDISSTATQRY command: %s (Attempts so far: %u)",
                error->message, ctx->failed_ndisstatqry_count);
        g_error_free (error);
    }

    /* Connected in IPv4? */
    if (ipv4_available && ipv4_connected) {
        /* Success! */
        ctx->step++;
        connect_3gpp_context_step (ctx);
        return;
    }

    /* Setup timeout to retry the same step */
    g_timeout_add_seconds (1,
                           (GSourceFunc)connect_retry_ndisstatqry_check_cb,
                           g_object_ref (self));
}

static void
connect_ndisdup_ready (MMBaseModem *modem,
                       GAsyncResult *res,
                       MMBroadbandBearerHuawei *self)
{
    Connect3gppContext *ctx;
    GError *error = NULL;

    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        /* Clear context */
        self->priv->connect_pending = NULL;
        g_simple_async_result_take_error (ctx->result, error);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Go to next step */
    ctx->step++;
    connect_3gpp_context_step (ctx);
}

typedef enum {
    MM_BEARER_HUAWEI_AUTH_UNKNOWN   = -1,
    MM_BEARER_HUAWEI_AUTH_NONE      =  0,
    MM_BEARER_HUAWEI_AUTH_PAP       =  1,
    MM_BEARER_HUAWEI_AUTH_CHAP      =  2,
    MM_BEARER_HUAWEI_AUTH_MSCHAPV2  =  3,
} MMBearerHuaweiAuthPref;

static gint
huawei_parse_auth_type (MMBearerAllowedAuth mm_auth)
{
    switch (mm_auth) {
    case MM_BEARER_ALLOWED_AUTH_NONE:
        return MM_BEARER_HUAWEI_AUTH_NONE;
    case MM_BEARER_ALLOWED_AUTH_PAP:
        return MM_BEARER_HUAWEI_AUTH_PAP;
    case MM_BEARER_ALLOWED_AUTH_CHAP:
        return MM_BEARER_HUAWEI_AUTH_CHAP;
    case MM_BEARER_ALLOWED_AUTH_MSCHAPV2:
        return MM_BEARER_HUAWEI_AUTH_MSCHAPV2;
    default:
        return MM_BEARER_HUAWEI_AUTH_UNKNOWN;
    }
}

static void
connect_3gpp_context_step (Connect3gppContext *ctx)
{
    /* Check for cancellation */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        /* Clear context */
        ctx->self->priv->connect_pending = NULL;

        /* If we already sent the connetion command, send the disconnection one */
        if (ctx->step > CONNECT_3GPP_CONTEXT_STEP_NDISDUP)
            mm_base_modem_at_command_full (ctx->modem,
                                           ctx->primary,
                                           "^NDISDUP=1,0",
                                           3,
                                           FALSE,
                                           FALSE,
                                           NULL,
                                           NULL, /* Do not care the AT response */
                                           NULL);

        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_CANCELLED,
                                         "Huawei connection operation has been cancelled");
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Network-initiated disconnect should not be outstanding at this point,
     * because it interferes with the connect attempt.
     */
    g_assert (ctx->self->priv->network_disconnect_pending_id == 0);

    switch (ctx->step) {
    case CONNECT_3GPP_CONTEXT_STEP_FIRST: {
        MMBearerIpFamily ip_family;

        ip_family = mm_bearer_properties_get_ip_type (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
        if (ip_family == MM_BEARER_IP_FAMILY_NONE ||
            ip_family == MM_BEARER_IP_FAMILY_ANY) {
            gchar *ip_family_str;

            ip_family = mm_base_bearer_get_default_ip_family (MM_BASE_BEARER (ctx->self));
            ip_family_str = mm_bearer_ip_family_build_string_from_mask (ip_family);
            mm_dbg ("No specific IP family requested, defaulting to %s",
                    ip_family_str);
            g_free (ip_family_str);
        }

        if (ip_family != MM_BEARER_IP_FAMILY_IPV4) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Only IPv4 is supported by this modem");
            connect_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Store the context */
        ctx->self->priv->connect_pending = ctx;

        ctx->step++;
        /* Fall down to the next step */
    }

    case CONNECT_3GPP_CONTEXT_STEP_NDISDUP: {
        const gchar         *apn;
        const gchar         *user;
        const gchar         *passwd;
        MMBearerAllowedAuth  auth;
        gint                 encoded_auth = MM_BEARER_HUAWEI_AUTH_UNKNOWN;
        gchar               *command;

        apn = mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
        user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
        passwd = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
        auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
        encoded_auth = huawei_parse_auth_type (auth);

        /* Default to no authentication if not specified */
        if ((encoded_auth = huawei_parse_auth_type (auth)) == MM_BEARER_HUAWEI_AUTH_UNKNOWN)
            encoded_auth = MM_BEARER_HUAWEI_AUTH_NONE;

        if (!user && !passwd)
            command = g_strdup_printf ("AT^NDISDUP=1,1,\"%s\"",
                                       apn == NULL ? "" : apn);
        else if (encoded_auth == MM_BEARER_HUAWEI_AUTH_NONE)
            command = g_strdup_printf ("AT^NDISDUP=1,1,\"%s\",\"%s\",\"%s\"",
                                       apn == NULL ? "" : apn,
                                       user == NULL ? "" : user,
                                       passwd == NULL ? "" : passwd);
        else
            command = g_strdup_printf ("AT^NDISDUP=1,1,\"%s\",\"%s\",\"%s\",%d",
                                       apn == NULL ? "" : apn,
                                       user == NULL ? "" : user,
                                       passwd == NULL ? "" : passwd,
                                       encoded_auth);

        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)connect_ndisdup_ready,
                                       g_object_ref (ctx->self));
        g_free (command);
        return;
    }

    case CONNECT_3GPP_CONTEXT_STEP_NDISSTATQRY:
        /* Wait for dial up timeout, retries for 60 times
         * (1s between the retries, so it means 1 minute).
         * If too many retries, failed
         */
        if (ctx->check_count > 60) {
            /* Clear context */
            ctx->self->priv->connect_pending = NULL;
            g_simple_async_result_set_error (ctx->result,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                             "Connection attempt timed out");
            connect_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Give up if too many unexpected responses to NIDSSTATQRY are encountered. */
        if (ctx->failed_ndisstatqry_count > 10) {
            /* Clear context */
            ctx->self->priv->connect_pending = NULL;
            g_simple_async_result_set_error (ctx->result,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED,
                                             "Connection attempt not supported.");
            connect_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Check if connected */
        ctx->check_count++;
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "^NDISSTATQRY?",
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)connect_ndisstatqry_check_ready,
                                       g_object_ref (ctx->self));
        return;

    case CONNECT_3GPP_CONTEXT_STEP_IP_CONFIG:
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "^DHCP?",
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)connect_dhcp_check_ready,
                                       g_object_ref (ctx->self));
        return;

    case CONNECT_3GPP_CONTEXT_STEP_LAST:
        /* Clear context */
        ctx->self->priv->connect_pending = NULL;

        /* Setup result */
        {
            if (ctx->ipv4_config) {
                g_simple_async_result_set_op_res_gpointer (
                    ctx->result,
                    mm_bearer_connect_result_new (ctx->data, ctx->ipv4_config, NULL),
                    (GDestroyNotify)mm_bearer_connect_result_unref);
            }
        }

        connect_3gpp_context_complete_and_free (ctx);
        return;
    }
}

static void
connect_3gpp (MMBroadbandBearer *self,
              MMBroadbandModem *modem,
              MMPortSerialAt *primary,
              MMPortSerialAt *secondary,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    Connect3gppContext  *ctx;
    MMPort *data;

    g_assert (primary != NULL);

    /* We need a net data port */
    data = mm_base_modem_peek_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!data) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_NOT_FOUND,
                                             "No valid data port found to launch connection");
        return;
    }

    /* Setup connection context */
    ctx = g_slice_new0 (Connect3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->data = g_object_ref (data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect_3gpp);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->step = CONNECT_3GPP_CONTEXT_STEP_FIRST;

    g_assert (ctx->self->priv->connect_pending == NULL);
    g_assert (ctx->self->priv->disconnect_pending == NULL);

    /* Get correct dial port to use */
    ctx->primary = get_dial_port (MM_BROADBAND_MODEM_HUAWEI (ctx->modem), ctx->data, primary);

    /* Run! */
    connect_3gpp_context_step (ctx);
}

/*****************************************************************************/
/* Disconnect 3GPP */

typedef enum {
    DISCONNECT_3GPP_CONTEXT_STEP_FIRST = 0,
    DISCONNECT_3GPP_CONTEXT_STEP_NDISDUP,
    DISCONNECT_3GPP_CONTEXT_STEP_NDISSTATQRY,
    DISCONNECT_3GPP_CONTEXT_STEP_LAST
} Disconnect3gppContextStep;

typedef struct {
    MMBroadbandBearerHuawei *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    GSimpleAsyncResult *result;
    Disconnect3gppContextStep step;
    guint check_count;
    guint failed_ndisstatqry_count;
} Disconnect3gppContext;

static void
disconnect_3gpp_context_complete_and_free (Disconnect3gppContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_slice_free (Disconnect3gppContext, ctx);
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void disconnect_3gpp_context_step (Disconnect3gppContext *ctx);

static gboolean
disconnect_retry_ndisstatqry_check_cb (MMBroadbandBearerHuawei *self)
{
    Disconnect3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->disconnect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    /* Retry same step */
    disconnect_3gpp_context_step (ctx);
    return G_SOURCE_REMOVE;
}

static void
disconnect_ndisstatqry_check_ready (MMBaseModem *modem,
                                    GAsyncResult *res,
                                    MMBroadbandBearerHuawei *self)
{
    Disconnect3gppContext *ctx;
    const gchar *response;
    GError *error = NULL;
    gboolean ipv4_available = FALSE;
    gboolean ipv4_connected = FALSE;
    gboolean ipv6_available = FALSE;
    gboolean ipv6_connected = FALSE;

    ctx = self->priv->disconnect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!response ||
        !mm_huawei_parse_ndisstatqry_response (response,
                                               &ipv4_available,
                                               &ipv4_connected,
                                               &ipv6_available,
                                               &ipv6_connected,
                                               &error)) {
        ctx->failed_ndisstatqry_count++;
        mm_dbg ("Unexpected response to ^NDISSTATQRY command: %s (Attempts so far: %u)",
                error->message, ctx->failed_ndisstatqry_count);
        g_error_free (error);
    }

    /* Disconnected IPv4? */
    if (ipv4_available && !ipv4_connected) {
        /* Success! */
        ctx->step++;
        disconnect_3gpp_context_step (ctx);
        return;
    }

    /* Setup timeout to retry the same step */
    g_timeout_add_seconds (1,
                           (GSourceFunc)disconnect_retry_ndisstatqry_check_cb,
                           g_object_ref (self));
}

static void
disconnect_ndisdup_ready (MMBaseModem *modem,
                          GAsyncResult *res,
                          MMBroadbandBearerHuawei *self)
{
    Disconnect3gppContext *ctx;
    GError *error = NULL;

    ctx = self->priv->disconnect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        /* Clear context */
        self->priv->disconnect_pending = NULL;
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Go to next step */
    ctx->step++;
    disconnect_3gpp_context_step (ctx);
}

static void
disconnect_3gpp_context_step (Disconnect3gppContext *ctx)
{
    switch (ctx->step) {
    case DISCONNECT_3GPP_CONTEXT_STEP_FIRST:
        /* Store the context */
        ctx->self->priv->disconnect_pending = ctx;

        /* We ignore any pending network-initiated disconnection in order to prevent it
         * from interfering with the client-initiated disconnection, as we would like to
         * proceed with the latter anyway. */
        if (ctx->self->priv->network_disconnect_pending_id != 0) {
            g_source_remove (ctx->self->priv->network_disconnect_pending_id);
            ctx->self->priv->network_disconnect_pending_id = 0;
        }

        ctx->step++;
        /* Fall down to the next step */

    case DISCONNECT_3GPP_CONTEXT_STEP_NDISDUP:
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "^NDISDUP=1,0",
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)disconnect_ndisdup_ready,
                                       g_object_ref (ctx->self));
        return;

    case DISCONNECT_3GPP_CONTEXT_STEP_NDISSTATQRY:
        /* If too many retries (1s of wait between the retries), failed */
        if (ctx->check_count > 60) {
            /* Clear context */
            ctx->self->priv->disconnect_pending = NULL;
            g_simple_async_result_set_error (ctx->result,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                             "Disconnection attempt timed out");
            disconnect_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Give up if too many unexpected responses to NIDSSTATQRY are encountered. */
        if (ctx->failed_ndisstatqry_count > 10) {
            /* Clear context */
            ctx->self->priv->disconnect_pending = NULL;
            g_simple_async_result_set_error (ctx->result,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED,
                                             "Disconnection attempt not supported.");
            disconnect_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Check if disconnected */
        ctx->check_count++;
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "^NDISSTATQRY?",
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)disconnect_ndisstatqry_check_ready,
                                       g_object_ref (ctx->self));
        return;

    case DISCONNECT_3GPP_CONTEXT_STEP_LAST:
        /* Clear context */
        ctx->self->priv->disconnect_pending = NULL;
        /* Set data port as result */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }
}

static void
disconnect_3gpp (MMBroadbandBearer *self,
                 MMBroadbandModem *modem,
                 MMPortSerialAt *primary,
                 MMPortSerialAt *secondary,
                 MMPort *data,
                 guint cid,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    Disconnect3gppContext *ctx;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (Disconnect3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect_3gpp);
    ctx->step = DISCONNECT_3GPP_CONTEXT_STEP_FIRST;

    g_assert (ctx->self->priv->connect_pending == NULL);
    g_assert (ctx->self->priv->disconnect_pending == NULL);

    /* Get correct dial port to use */
    ctx->primary = get_dial_port (MM_BROADBAND_MODEM_HUAWEI (ctx->modem), data, primary);

    /* Start! */
    disconnect_3gpp_context_step (ctx);
}

/*****************************************************************************/

static gboolean
network_disconnect_3gpp_delayed (MMBroadbandBearerHuawei *self)
{
    mm_dbg ("Disconnect bearer '%s' on network request.",
            mm_base_bearer_get_path (MM_BASE_BEARER (self)));

    self->priv->network_disconnect_pending_id = 0;
    mm_base_bearer_report_connection_status (MM_BASE_BEARER (self),
                                             MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
    return G_SOURCE_REMOVE;
}

static void
report_connection_status (MMBaseBearer *bearer,
                          MMBearerConnectionStatus status)
{
    MMBroadbandBearerHuawei *self = MM_BROADBAND_BEARER_HUAWEI (bearer);

    g_assert (status == MM_BEARER_CONNECTION_STATUS_CONNECTED ||
              status == MM_BEARER_CONNECTION_STATUS_DISCONNECTING ||
              status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED);

    /* When a pending connection / disconnection attempt is in progress, we use
     * ^NDISSTATQRY? to check the connection status and thus temporarily ignore
     * ^NDISSTAT unsolicited messages */
    if (self->priv->connect_pending || self->priv->disconnect_pending)
        return;

    mm_dbg ("Received spontaneous ^NDISSTAT (%s)",
            mm_bearer_connection_status_get_string (status));

    /* Ignore 'CONNECTED' */
    if (status == MM_BEARER_CONNECTION_STATUS_CONNECTED)
        return;

    /* We already use ^NDISSTATQRY? to poll the connection status, so only
     * handle network-initiated disconnection here. */
    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTING) {
        /* MM_BEARER_CONNECTION_STATUS_DISCONNECTING is used to indicate that the
         * reporting of disconnection should be delayed. See MMBroadbandModemHuawei's
         * bearer_report_connection_status for details. */
        if (mm_base_bearer_get_status (bearer) == MM_BEARER_STATUS_CONNECTED &&
            self->priv->network_disconnect_pending_id == 0) {
            mm_dbg ("Delay network-initiated disconnection of bearer '%s'",
                    mm_base_bearer_get_path (MM_BASE_BEARER (self)));
            self->priv->network_disconnect_pending_id = (g_timeout_add_seconds (
                                                             4,
                                                             (GSourceFunc) network_disconnect_3gpp_delayed,
                                                             self));
        }
        return;
    }

    /* Report disconnected right away */
    MM_BASE_BEARER_CLASS (mm_broadband_bearer_huawei_parent_class)->report_connection_status (
        bearer,
        MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_huawei_new_finish (GAsyncResult *res,
                                       GError **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}

static void
dispose (GObject *object)
{
    MMBroadbandBearerHuawei *self = MM_BROADBAND_BEARER_HUAWEI (object);

    if (self->priv->network_disconnect_pending_id != 0) {
        g_source_remove (self->priv->network_disconnect_pending_id);
        self->priv->network_disconnect_pending_id = 0;
    }

    G_OBJECT_CLASS (mm_broadband_bearer_huawei_parent_class)->dispose (object);
}

void
mm_broadband_bearer_huawei_new (MMBroadbandModemHuawei *modem,
                                MMBearerProperties *config,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_HUAWEI,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_huawei_init (MMBroadbandBearerHuawei *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_BEARER_HUAWEI,
                                              MMBroadbandBearerHuaweiPrivate);
}

static void
mm_broadband_bearer_huawei_class_init (MMBroadbandBearerHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerHuaweiPrivate));

    object_class->dispose = dispose;

    base_bearer_class->report_connection_status = report_connection_status;
    base_bearer_class->load_connection_status = NULL;
    base_bearer_class->load_connection_status_finish = NULL;

    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
