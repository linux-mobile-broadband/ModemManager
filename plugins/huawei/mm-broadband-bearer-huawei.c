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
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-huawei.h"
#include "mm-daemon-enums-types.h"

G_DEFINE_TYPE (MMBroadbandBearerHuawei, mm_broadband_bearer_huawei, MM_TYPE_BROADBAND_BEARER)

struct _MMBroadbandBearerHuaweiPrivate {
    gpointer connect_pending;
    gpointer disconnect_pending;
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
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPort *data;
    Connect3gppContextStep step;
    guint check_count;
    guint failed_ndisstatqry_count;
    MMBearerIpConfig *ipv4_config;
} Connect3gppContext;

static void
connect_3gpp_context_free (Connect3gppContext *ctx)
{
    g_object_unref (ctx->modem);

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
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void connect_3gpp_context_step (GTask *task);

static void
connect_dhcp_check_ready (MMBaseModem *modem,
                          GAsyncResult *res,
                          MMBroadbandBearerHuawei *self)
{
    GTask *task;
    Connect3gppContext *ctx;
    const gchar *response;
    GError *error = NULL;

    task = self->priv->connect_pending;
    g_assert (task != NULL);

    ctx = g_task_get_task_data (task);

    /* Balance refcount */
    g_object_unref (self);

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
            mm_obj_dbg (self, "unexpected response to ^DHCP command: %s", error->message);
        }
    }

    g_clear_error (&error);
    ctx->step++;
    connect_3gpp_context_step (task);
}

static gboolean
connect_retry_ndisstatqry_check_cb (MMBroadbandBearerHuawei *self)
{
    GTask *task;

    /* Recover context */
    task = self->priv->connect_pending;
    g_assert (task != NULL);

    /* Balance refcount */
    g_object_unref (self);

    /* Retry same step */
    connect_3gpp_context_step (task);

    return G_SOURCE_REMOVE;
}

static void
connect_ndisstatqry_check_ready (MMBaseModem *modem,
                                 GAsyncResult *res,
                                 MMBroadbandBearerHuawei *self)
{
    GTask *task;
    Connect3gppContext *ctx;
    const gchar *response;
    GError *error = NULL;
    gboolean ipv4_available = FALSE;
    gboolean ipv4_connected = FALSE;
    gboolean ipv6_available = FALSE;
    gboolean ipv6_connected = FALSE;

    task = self->priv->connect_pending;
    g_assert (task != NULL);

    ctx = g_task_get_task_data (task);

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
        mm_obj_dbg (self, "unexpected response to ^NDISSTATQRY command: %s (%u attempts so far)",
                    error->message, ctx->failed_ndisstatqry_count);
        g_error_free (error);
    }

    /* Connected in IPv4? */
    if (ipv4_available && ipv4_connected) {
        /* Success! */
        ctx->step++;
        connect_3gpp_context_step (task);
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
    GTask *task;
    Connect3gppContext *ctx;
    GError *error = NULL;

    task = self->priv->connect_pending;
    g_assert (task != NULL);

    ctx = g_task_get_task_data (task);

    /* Balance refcount */
    g_object_unref (self);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        /* Clear task */
        self->priv->connect_pending = NULL;
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go to next step */
    ctx->step++;
    connect_3gpp_context_step (task);
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
    case MM_BEARER_ALLOWED_AUTH_UNKNOWN:
    case MM_BEARER_ALLOWED_AUTH_MSCHAP:
    case MM_BEARER_ALLOWED_AUTH_EAP:
        return MM_BEARER_HUAWEI_AUTH_UNKNOWN;
    }
}

static void
connect_3gpp_context_step (GTask *task)
{
    MMBroadbandBearerHuawei *self;
    Connect3gppContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    /* Check for cancellation */
    if (g_cancellable_is_cancelled (g_task_get_cancellable (task))) {
        /* Clear task */
        self->priv->connect_pending = NULL;

        /* If we already sent the connetion command, send the disconnection one */
        if (ctx->step > CONNECT_3GPP_CONTEXT_STEP_NDISDUP)
            mm_base_modem_at_command_full (ctx->modem,
                                           ctx->primary,
                                           "^NDISDUP=1,0",
                                           MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT,
                                           FALSE,
                                           FALSE,
                                           NULL,
                                           NULL, /* Do not care the AT response */
                                           NULL);

        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                 "Huawei connection operation has been cancelled");
        g_object_unref (task);
        return;
    }

    switch (ctx->step) {
    case CONNECT_3GPP_CONTEXT_STEP_FIRST: {
        MMBearerIpFamily ip_family;

        ip_family = mm_bearer_properties_get_ip_type (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
        if (ip_family == MM_BEARER_IP_FAMILY_NONE ||
            ip_family == MM_BEARER_IP_FAMILY_ANY) {
            gchar *ip_family_str;

            ip_family = mm_base_bearer_get_default_ip_family (MM_BASE_BEARER (self));
            ip_family_str = mm_bearer_ip_family_build_string_from_mask (ip_family);
            mm_obj_dbg (self, "no specific IP family requested, defaulting to %s", ip_family_str);
            g_free (ip_family_str);
        }

        if (ip_family != MM_BEARER_IP_FAMILY_IPV4) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "Only IPv4 is supported by this modem");
            g_object_unref (task);
            return;
        }

        /* Store the task */
        self->priv->connect_pending = task;

        ctx->step++;
    } /* fall through */

    case CONNECT_3GPP_CONTEXT_STEP_NDISDUP: {
        const gchar         *apn;
        const gchar         *user;
        const gchar         *passwd;
        MMBearerAllowedAuth  auth;
        gint                 encoded_auth = MM_BEARER_HUAWEI_AUTH_UNKNOWN;
        gchar               *command;

        apn = mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
        user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
        passwd = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
        auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (self)));
        encoded_auth = huawei_parse_auth_type (auth);

        /* Default to no authentication if not specified */
        if (encoded_auth == MM_BEARER_HUAWEI_AUTH_UNKNOWN)
            encoded_auth = MM_BEARER_HUAWEI_AUTH_NONE;

        if (!user && !passwd)
            command = g_strdup_printf ("AT^NDISDUP=1,1,\"%s\"",
                                       apn == NULL ? "" : apn);
        else {
            if (encoded_auth == MM_BEARER_HUAWEI_AUTH_NONE) {
                encoded_auth = MM_BEARER_HUAWEI_AUTH_CHAP;
                mm_obj_dbg (self, "using default (CHAP) authentication method");
            }
            command = g_strdup_printf ("AT^NDISDUP=1,1,\"%s\",\"%s\",\"%s\",%d",
                                       apn == NULL ? "" : apn,
                                       user == NULL ? "" : user,
                                       passwd == NULL ? "" : passwd,
                                       encoded_auth);
        }

        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)connect_ndisdup_ready,
                                       g_object_ref (self));
        g_free (command);
        return;
    }

    case CONNECT_3GPP_CONTEXT_STEP_NDISSTATQRY:
        /* Wait for dial up timeout, retries for 180 times
         * (1s between the retries, so it means 3 minutes).
         * If too many retries, failed
         */
        if (ctx->check_count > MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT) {
            /* Clear context */
            self->priv->connect_pending = NULL;
            g_task_return_new_error (task,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                     "Connection attempt timed out");
            g_object_unref (task);
            return;
        }

        /* Give up if too many unexpected responses to NIDSSTATQRY are encountered. */
        if (ctx->failed_ndisstatqry_count > 10) {
            /* Clear context */
            self->priv->connect_pending = NULL;
            g_task_return_new_error (task,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED,
                                     "Connection attempt not supported.");
            g_object_unref (task);
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
                                       g_object_ref (self));
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
                                       g_object_ref (self));
        return;

    case CONNECT_3GPP_CONTEXT_STEP_LAST:
        /* Clear context */
        self->priv->connect_pending = NULL;

        /* Setup result */
        g_task_return_pointer (
            task,
            mm_bearer_connect_result_new (ctx->data, ctx->ipv4_config, NULL),
            (GDestroyNotify)mm_bearer_connect_result_unref);

        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
connect_3gpp (MMBroadbandBearer *_self,
              MMBroadbandModem *modem,
              MMPortSerialAt *primary,
              MMPortSerialAt *secondary,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    MMBroadbandBearerHuawei *self = MM_BROADBAND_BEARER_HUAWEI (_self);
    Connect3gppContext *ctx;
    GTask *task;
    MMPort *data;

    g_assert (primary != NULL);

    /* We need a net data port */
    data = mm_base_modem_peek_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!data) {
        g_task_report_new_error (self,
                                 callback,
                                 user_data,
                                 connect_3gpp,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "No valid data port found to launch connection");
        return;
    }

    /* Setup connection context */
    ctx = g_slice_new0 (Connect3gppContext);
    ctx->modem = g_object_ref (modem);
    ctx->data = g_object_ref (data);
    ctx->step = CONNECT_3GPP_CONTEXT_STEP_FIRST;

    g_assert (self->priv->connect_pending == NULL);
    g_assert (self->priv->disconnect_pending == NULL);

    /* Get correct dial port to use */
    ctx->primary = get_dial_port (MM_BROADBAND_MODEM_HUAWEI (ctx->modem), ctx->data, primary);


    /* Default to automatic/DHCP addressing */
    ctx->ipv4_config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (ctx->ipv4_config, MM_BEARER_IP_METHOD_DHCP);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)connect_3gpp_context_free);
    g_task_set_check_cancellable (task, FALSE);

    /* Run! */
    connect_3gpp_context_step (task);
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
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    Disconnect3gppContextStep step;
    guint check_count;
    guint failed_ndisstatqry_count;
} Disconnect3gppContext;

static void
disconnect_3gpp_context_free (Disconnect3gppContext *ctx)
{
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_slice_free (Disconnect3gppContext, ctx);
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void disconnect_3gpp_context_step (GTask *task);

static gboolean
disconnect_retry_ndisstatqry_check_cb (MMBroadbandBearerHuawei *self)
{
    GTask *task;

    /* Recover context */
    task = self->priv->disconnect_pending;
    g_assert (task != NULL);

    /* Balance refcount */
    g_object_unref (self);

    /* Retry same step */
    disconnect_3gpp_context_step (task);
    return G_SOURCE_REMOVE;
}

static void
disconnect_ndisstatqry_check_ready (MMBaseModem *modem,
                                    GAsyncResult *res,
                                    MMBroadbandBearerHuawei *self)
{
    GTask *task;
    Disconnect3gppContext *ctx;
    const gchar *response;
    GError *error = NULL;
    gboolean ipv4_available = FALSE;
    gboolean ipv4_connected = FALSE;
    gboolean ipv6_available = FALSE;
    gboolean ipv6_connected = FALSE;

    task = self->priv->disconnect_pending;
    g_assert (task != NULL);

    ctx = g_task_get_task_data (task);

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
        mm_obj_dbg (self, "unexpected response to ^NDISSTATQRY command: %s (%u attempts so far)",
                    error->message, ctx->failed_ndisstatqry_count);
        g_error_free (error);
    }

    /* Disconnected IPv4? */
    if (ipv4_available && !ipv4_connected) {
        /* Success! */
        ctx->step++;
        disconnect_3gpp_context_step (task);
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
    GTask *task;
    Disconnect3gppContext *ctx;

    task = self->priv->disconnect_pending;
    g_assert (task != NULL);

    ctx = g_task_get_task_data (task);

    /* Balance refcount */
    g_object_unref (self);

    /* Running NDISDUP=1,0 on an already disconnected bearer/context will
     * return ERROR! Ignore errors in the NDISDUP disconnection command,
     * because we're anyway going to check the bearer/context status
     * afterwards.  */
    mm_base_modem_at_command_full_finish (modem, res, NULL);

    /* Go to next step */
    ctx->step++;
    disconnect_3gpp_context_step (task);
}

static void
disconnect_3gpp_context_step (GTask *task)
{
    MMBroadbandBearerHuawei *self;
    Disconnect3gppContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISCONNECT_3GPP_CONTEXT_STEP_FIRST:
        /* Store the task */
        self->priv->disconnect_pending = task;
        ctx->step++;
        /* fall through */

    case DISCONNECT_3GPP_CONTEXT_STEP_NDISDUP:
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "^NDISDUP=1,0",
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)disconnect_ndisdup_ready,
                                       g_object_ref (self));
        return;

    case DISCONNECT_3GPP_CONTEXT_STEP_NDISSTATQRY:
        /* If too many retries (1s of wait between the retries), failed */
        if (ctx->check_count > MM_BASE_BEARER_DEFAULT_DISCONNECTION_TIMEOUT) {
            /* Clear task */
            self->priv->disconnect_pending = NULL;
            g_task_return_new_error (task,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                     "Disconnection attempt timed out");
            g_object_unref (task);
            return;
        }

        /* Give up if too many unexpected responses to NIDSSTATQRY are encountered. */
        if (ctx->failed_ndisstatqry_count > 10) {
            /* Clear task */
            self->priv->disconnect_pending = NULL;
            g_task_return_new_error (task,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED,
                                     "Disconnection attempt not supported.");
            g_object_unref (task);
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
                                       g_object_ref (self));
        return;

    case DISCONNECT_3GPP_CONTEXT_STEP_LAST:
        /* Clear task */
        self->priv->disconnect_pending = NULL;
        /* Set data port as result */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }
}

static void
disconnect_3gpp (MMBroadbandBearer *_self,
                 MMBroadbandModem *modem,
                 MMPortSerialAt *primary,
                 MMPortSerialAt *secondary,
                 MMPort *data,
                 guint cid,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    MMBroadbandBearerHuawei *self = MM_BROADBAND_BEARER_HUAWEI (_self);
    Disconnect3gppContext *ctx;
    GTask *task;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (Disconnect3gppContext);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->step = DISCONNECT_3GPP_CONTEXT_STEP_FIRST;

    g_assert (self->priv->connect_pending == NULL);
    g_assert (self->priv->disconnect_pending == NULL);

    /* Get correct dial port to use */
    ctx->primary = get_dial_port (MM_BROADBAND_MODEM_HUAWEI (ctx->modem), data, primary);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)disconnect_3gpp_context_free);

    /* Start! */
    disconnect_3gpp_context_step (task);
}

/*****************************************************************************/

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

    mm_obj_dbg (self, "received spontaneous ^NDISSTAT (%s)", mm_bearer_connection_status_get_string (status));

    /* Ignore 'CONNECTED' */
    if (status == MM_BEARER_CONNECTION_STATUS_CONNECTED)
        return;

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

    base_bearer_class->report_connection_status = report_connection_status;
    base_bearer_class->load_connection_status = NULL;
    base_bearer_class->load_connection_status_finish = NULL;

    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
