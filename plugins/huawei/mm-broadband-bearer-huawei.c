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

G_DEFINE_TYPE (MMBroadbandBearerHuawei, mm_broadband_bearer_huawei, MM_TYPE_BROADBAND_BEARER)

struct _MMBroadbandBearerHuaweiPrivate {
    gpointer connect_pending;
    gpointer disconnect_pending;
};

/*****************************************************************************/
/* Connect 3GPP */

typedef enum {
    CONNECT_3GPP_CONTEXT_STEP_FIRST = 0,
    CONNECT_3GPP_CONTEXT_STEP_NDISDUP,
    CONNECT_3GPP_CONTEXT_STEP_DHCP,
    CONNECT_3GPP_CONTEXT_STEP_LAST
} Connect3gppContextStep;

typedef struct {
    MMBroadbandBearerHuawei *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMPort *data;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    Connect3gppContextStep step;
    guint check_count;
} Connect3gppContext;

static void
connect_3gpp_context_complete_and_free (Connect3gppContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_slice_free (Connect3gppContext, ctx);
}

static gboolean
connect_3gpp_finish (MMBroadbandBearer *self,
                     GAsyncResult *res,
                     MMPort **data,
                     MMBearerIpConfig **ipv4_config,
                     MMBearerIpConfig **ipv6_config,
                     GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    *data = g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    *ipv4_config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (*ipv4_config, MM_BEARER_IP_METHOD_DHCP);
    *ipv6_config = NULL;
    return TRUE;
}

static void connect_3gpp_context_step (Connect3gppContext *ctx);

static gboolean
connect_retry_dhcp_check_cb (MMBroadbandBearerHuawei *self)
{
    Connect3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    /* Retry same step */
    connect_3gpp_context_step (ctx);

    return FALSE;
}

static void
connect_dhcp_check_ready (MMBaseModem *modem,
                          GAsyncResult *res,
                          MMBroadbandBearerHuawei *self)
{
    Connect3gppContext *ctx;

    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    if (!mm_base_modem_at_command_full_finish (modem, res, NULL)) {
        /* Setup timeout to retry the same step */
        g_timeout_add_seconds (1,
                               (GSourceFunc)connect_retry_dhcp_check_cb,
                               g_object_ref (self));
        return;
    }

    /* Success! */
    ctx->step++;
    connect_3gpp_context_step (ctx);
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

    switch (ctx->step) {
    case CONNECT_3GPP_CONTEXT_STEP_FIRST: {
        MMBearerIpFamily ip_type;

        ip_type = mm_bearer_properties_get_ip_type (mm_bearer_peek_config (MM_BEARER (ctx->self)));
        if (ip_type != MM_BEARER_IP_FAMILY_IPV4) {
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

        apn = mm_bearer_properties_get_apn (mm_bearer_peek_config (MM_BEARER (ctx->self)));
        user = mm_bearer_properties_get_user (mm_bearer_peek_config (MM_BEARER (ctx->self)));
        passwd = mm_bearer_properties_get_password (mm_bearer_peek_config (MM_BEARER (ctx->self)));
        auth = mm_bearer_properties_get_allowed_auth (mm_bearer_peek_config (MM_BEARER (ctx->self)));
        encoded_auth = huawei_parse_auth_type (auth);

        command = g_strdup_printf ("AT^NDISDUP=1,1,\"%s\",\"%s\",\"%s\",%d",
                                   apn == NULL ? "" : apn,
                                   user == NULL ? "" : user,
                                   passwd == NULL ? "" : passwd,
                                   encoded_auth == MM_BEARER_HUAWEI_AUTH_UNKNOWN ? MM_BEARER_HUAWEI_AUTH_NONE : encoded_auth);
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

    case CONNECT_3GPP_CONTEXT_STEP_DHCP:
        /* If too many retries (1s of wait between the retries), failed */
        if (ctx->check_count > 30) {
            /* Clear context */
            ctx->self->priv->connect_pending = NULL;
            g_simple_async_result_set_error (ctx->result,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                             "Connection attempt timed out");
            connect_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Check if connected */
        ctx->check_count++;
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
        /* Set data port as result */
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   g_object_ref (ctx->data),
                                                   g_object_unref);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }
}

static void
connect_3gpp (MMBroadbandBearer *self,
              MMBroadbandModem *modem,
              MMAtSerialPort *primary,
              MMAtSerialPort *secondary,
              MMPort *data,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    Connect3gppContext  *ctx;

    g_assert (primary != NULL);

    /* Setup connection context */
    ctx = g_slice_new0 (Connect3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->data = g_object_ref (data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect_3gpp);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->step = CONNECT_3GPP_CONTEXT_STEP_FIRST;

    g_assert (ctx->self->priv->connect_pending == NULL);
    g_assert (ctx->self->priv->disconnect_pending == NULL);

    /* Run! */
    connect_3gpp_context_step (ctx);
}

/*****************************************************************************/
/* Disconnect 3GPP */

typedef enum {
    DISCONNECT_3GPP_CONTEXT_STEP_FIRST = 0,
    DISCONNECT_3GPP_CONTEXT_STEP_NDISDUP,
    DISCONNECT_3GPP_CONTEXT_STEP_DHCP,
    DISCONNECT_3GPP_CONTEXT_STEP_LAST
} Disconnect3gppContextStep;

typedef struct {
    MMBroadbandBearerHuawei *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    GSimpleAsyncResult *result;
    Disconnect3gppContextStep step;
    guint check_count;
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
disconnect_retry_dhcp_check_cb (MMBroadbandBearerHuawei *self)
{
    Disconnect3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->disconnect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    /* Retry same step */
    disconnect_3gpp_context_step (ctx);
    return FALSE;
}

static void
disconnect_dhcp_check_ready (MMBaseModem *modem,
                             GAsyncResult *res,
                             MMBroadbandBearerHuawei *self)
{
    Disconnect3gppContext *ctx;

    ctx = self->priv->disconnect_pending;
    g_assert (ctx != NULL);

    /* Balance refcount */
    g_object_unref (self);

    /* If any response give, we're still connected */
    if (mm_base_modem_at_command_full_finish (modem, res, NULL)) {
        /* Setup timeout to retry the same step */
        g_timeout_add_seconds (1,
                               (GSourceFunc)disconnect_retry_dhcp_check_cb,
                               g_object_ref (self));
        return;
    }

    /* Success! */
    ctx->step++;
    disconnect_3gpp_context_step (ctx);
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

    case DISCONNECT_3GPP_CONTEXT_STEP_DHCP:
        /* If too many retries (1s of wait between the retries), failed */
        if (ctx->check_count > 10) {
            /* Clear context */
            ctx->self->priv->disconnect_pending = NULL;
            g_simple_async_result_set_error (ctx->result,
                                             MM_MOBILE_EQUIPMENT_ERROR,
                                             MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                             "Disconnection attempt timed out");
            disconnect_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Check if disconnected */
        ctx->check_count++;
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "^DHCP?",
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)disconnect_dhcp_check_ready,
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
                 MMAtSerialPort *primary,
                 MMAtSerialPort *secondary,
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
    ctx->primary = g_object_ref (primary);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect_3gpp);
    ctx->step = DISCONNECT_3GPP_CONTEXT_STEP_FIRST;

    g_assert (ctx->self->priv->connect_pending == NULL);
    g_assert (ctx->self->priv->disconnect_pending == NULL);

    /* Start! */
    disconnect_3gpp_context_step (ctx);
}

/*****************************************************************************/

MMBearer *
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
    mm_bearer_export (MM_BEARER (bearer));

    return MM_BEARER (bearer);
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
        MM_BEARER_MODEM, modem,
        MM_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_huawei_init (MMBroadbandBearerHuawei *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER_HUAWEI,
                                              MMBroadbandBearerHuaweiPrivate);
}

static void
mm_broadband_bearer_huawei_class_init (MMBroadbandBearerHuaweiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerHuaweiPrivate));

    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
