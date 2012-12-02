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
    guint    connect_pending_id;
    gulong   connect_cancellable_id;

    gpointer disconnect_pending;
    guint    disconnect_pending_id;
};

/*****************************************************************************/
/* Dial 3GPP */

typedef struct {
    MMBroadbandBearerHuawei *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    guint cid;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    guint check_count;
} Dial3gppContext;

static Dial3gppContext *
dial_3gpp_context_new (MMBroadbandBearerHuawei *self,
                       MMBaseModem *modem,
                       MMAtSerialPort *primary,
                       guint cid,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    Dial3gppContext *ctx;

    ctx = g_slice_new0 (Dial3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             dial_3gpp_context_new);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->check_count = 0;

    return ctx;
}

static void
dial_3gpp_context_complete_and_free (Dial3gppContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_slice_free (Dial3gppContext, ctx);
}

static gboolean
dial_3gpp_context_set_error_if_cancelled (Dial3gppContext *ctx,
                                          GError **error)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_CANCELLED,
                 "Dial operation has been cancelled");
    return TRUE;
}

static gboolean
huawei_dial_3gpp_finish (MMBroadbandBearer *self,
                         GAsyncResult *res,
                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
connect_cancelled_cb (GCancellable *cancellable,
                      MMBroadbandBearerHuawei *self)
{
    GError *error = NULL;
    Dial3gppContext *ctx;

    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;

    g_source_remove (self->priv->connect_pending_id);
    self->priv->connect_pending_id = 0;

    self->priv->connect_cancellable_id = 0;

    /* Send disconnect command to make sure modem to keep in disconnection */
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   "^NDISDUP=1,0",
                                   3,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   NULL, /* Do not care the AT response */
                                   NULL);

    g_assert (dial_3gpp_context_set_error_if_cancelled (ctx, &error));

    g_simple_async_result_take_error (ctx->result, error);
    dial_3gpp_context_complete_and_free (ctx);
}

static gboolean check_connection_status_cb (MMBroadbandBearerHuawei *self);

static void
check_connection_status_ready (MMBaseModem *modem,
                               GAsyncResult *res,
                               MMBroadbandBearerHuawei *self)
{
    Dial3gppContext *ctx;
    const gchar *response;

    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    g_object_unref (self);

    response = mm_base_modem_at_command_full_finish (modem, res, NULL);
    if (response) {
        /* Success!  Connected... */
        self->priv->connect_pending = NULL;
        if (self->priv->connect_cancellable_id) {
            g_cancellable_disconnect (ctx->cancellable,
                                      self->priv->connect_cancellable_id);
            self->priv->connect_cancellable_id = 0;
        }

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    self->priv->connect_pending_id = g_timeout_add_seconds (1,
                                                            (GSourceFunc)check_connection_status_cb,
                                                            self);
}

static gboolean
check_connection_status_cb (MMBroadbandBearerHuawei *self)
{
    Dial3gppContext *ctx;

    self->priv->connect_pending_id = 0;

    /* Recover context */
    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    /* Try 30 times of 1 second timeout, too many means connection timeout, failed */
    if (ctx->check_count > 30) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;

        self->priv->connect_pending = NULL;

        g_simple_async_result_set_error (ctx->result,
                                         MM_MOBILE_EQUIPMENT_ERROR,
                                         MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                         "Connection attempt timed out");
        dial_3gpp_context_complete_and_free (ctx);
        return FALSE;
    }

    ctx->check_count++;
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   "^DHCP?",
                                   3,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)check_connection_status_ready,
                                   g_object_ref (ctx->self));
    return FALSE;
}

static void
huawei_dial_3gpp_ready (MMBaseModem *modem,
                        GAsyncResult *res,
                        MMBroadbandBearerHuawei *self)
{
    Dial3gppContext *ctx;
    GError *error = NULL;

    ctx = self->priv->connect_pending;
    g_assert (ctx != NULL);

    g_object_unref (self);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* We will now setup a timeout to check the status */
    self->priv->connect_pending_id = g_timeout_add_seconds (1,
                                                            (GSourceFunc)check_connection_status_cb,
                                                            self);

    self->priv->connect_cancellable_id = g_cancellable_connect (ctx->cancellable,
                                                                G_CALLBACK (connect_cancelled_cb),
                                                                self,
                                                                NULL);
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
huawei_dial_3gpp (MMBroadbandBearer *self,
                  MMBaseModem *modem,
                  MMAtSerialPort *primary,
                  MMPort *data,
                  guint cid,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    Dial3gppContext     *ctx;
    const gchar         *apn;
    const gchar         *user;
    const gchar         *passwd;
    MMBearerAllowedAuth  auth;
    gint                 encoded_auth = MM_BEARER_HUAWEI_AUTH_UNKNOWN;
    gchar               *command;

    g_assert (primary != NULL);

    ctx = dial_3gpp_context_new (MM_BROADBAND_BEARER_HUAWEI (self),
                                 modem,
                                 primary,
                                 cid,
                                 cancellable,
                                 callback,
                                 user_data);

    g_assert(ctx != NULL);
    g_assert (ctx->self->priv->connect_pending == NULL);
    g_assert (ctx->self->priv->disconnect_pending == NULL);

    ctx->self->priv->connect_pending = ctx;

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
                                    (GAsyncReadyCallback)huawei_dial_3gpp_ready,
                                    g_object_ref (ctx->self));
    g_free (command);
}

/*****************************************************************************/
/* 3GPP disconnect */

typedef struct {
    MMBroadbandBearerHuawei *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    GSimpleAsyncResult *result;
    guint check_count;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_slice_free (DisconnectContext, ctx);
}

static gboolean
huawei_disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean check_disconnect_status_cb (MMBroadbandBearerHuawei *self);

static void
check_disconnect_status_ready (MMBaseModem *modem,
                               GAsyncResult *res,
                               MMBroadbandBearerHuawei *self)
{
    DisconnectContext *ctx;
    const gchar *response;

    /* Balance refcount with the extra ref we passed to command_full() */
    g_object_unref (self);

    ctx = self->priv->disconnect_pending;
    g_assert (ctx != NULL);

    response = mm_base_modem_at_command_full_finish (modem, res, NULL);
    if (!response) {
       /* Success!  Disconnected... */
        self->priv->disconnect_pending = NULL;
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disconnect_context_complete_and_free (ctx);
        return;
    }

    self->priv->disconnect_pending_id = g_timeout_add_seconds (1,
                                                               (GSourceFunc)check_disconnect_status_cb,
                                                               self);
}

static gboolean
check_disconnect_status_cb (MMBroadbandBearerHuawei *self)
{
    DisconnectContext *ctx;

    self->priv->disconnect_pending_id = 0;

    /* Recover context */
    ctx = self->priv->disconnect_pending;

    /* Try 10 times of 1 second timeout, too many means failed */
    if (ctx->check_count > 10) {
        self->priv->disconnect_pending = NULL;
        g_simple_async_result_set_error (ctx->result,
                                         MM_MOBILE_EQUIPMENT_ERROR,
                                         MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                         "Disconnect attempt timed out");
        disconnect_context_complete_and_free (ctx);
        return FALSE;
    }

    ctx->check_count++;
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   "^DHCP?",
                                   3,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)check_disconnect_status_ready,
                                   g_object_ref (ctx->self));
    return FALSE;
}

static void
huawei_disconnect_3gpp_ready (MMBaseModem *modem,
                              GAsyncResult *res,
                              MMBroadbandBearerHuawei *self)
{
    DisconnectContext *ctx;
    GError *error = NULL;

    /* Balance refcount with the extra ref we passed to command_full() */
    g_object_unref (self);

    ctx = self->priv->disconnect_pending;
    g_assert (ctx != NULL);

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_context_complete_and_free (ctx);
        return;
    }

    /* We will now setup a timeout to poll for the status */
    self->priv->disconnect_pending_id = g_timeout_add_seconds (1,
                                                               (GSourceFunc)check_disconnect_status_cb,
                                                               self);
}

static void
huawei_disconnect_3gpp (MMBroadbandBearer *self,
                        MMBroadbandModem *modem,
                        MMAtSerialPort *primary,
                        MMAtSerialPort *secondary,
                        MMPort *data,
                        guint cid,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    DisconnectContext *ctx;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (DisconnectContext);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             huawei_disconnect_3gpp);
    ctx->check_count = 0;

    g_assert (ctx->self->priv->connect_pending == NULL);
    g_assert (ctx->self->priv->disconnect_pending == NULL);

    ctx->self->priv->disconnect_pending = ctx;

    mm_base_modem_at_command_full (MM_BASE_MODEM (modem),
                                   primary,
                                   "^NDISDUP=1,0",
                                   3,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)huawei_disconnect_3gpp_ready,
                                   g_object_ref (ctx->self));
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

    broadband_bearer_class->dial_3gpp = huawei_dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = huawei_dial_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = huawei_disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = huawei_disconnect_3gpp_finish;
}
