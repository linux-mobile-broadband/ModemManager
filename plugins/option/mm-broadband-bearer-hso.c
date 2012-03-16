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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-hso.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-utils.h"

G_DEFINE_TYPE (MMBroadbandBearerHso, mm_broadband_bearer_hso, MM_TYPE_BROADBAND_BEARER);

enum {
    PROP_0,
    PROP_USER,
    PROP_PASSWORD,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandBearerHsoPrivate {
    gchar *user;
    gchar *password;
    guint auth_idx;

    gpointer connect_pending;
    guint connect_pending_id;
    gulong connect_cancellable_id;
};

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearerHso *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    guint cid;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    guint auth_idx;
} Dial3gppContext;

static Dial3gppContext *
dial_3gpp_context_new (MMBroadbandBearerHso *self,
                       MMBaseModem *modem,
                       MMAtSerialPort *primary,
                       guint cid,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    Dial3gppContext *ctx;

    ctx = g_new0 (Dial3gppContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             dial_3gpp_context_new);
    ctx->cancellable = g_object_ref (cancellable);

    /* Always start with the index that worked last time
     * (will be 0 the first time)*/
    ctx->auth_idx = self->priv->auth_idx;

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
    g_free (ctx);
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
dial_3gpp_context_complete_and_free_if_cancelled (Dial3gppContext *ctx)
{
    GError *error = NULL;

    if (!dial_3gpp_context_set_error_if_cancelled (ctx, &error))
        return FALSE;

    g_simple_async_result_take_error (ctx->result, error);
    dial_3gpp_context_complete_and_free (ctx);
    return TRUE;
}

static gboolean
dial_3gpp_finish (MMBroadbandBearer *self,
                  GAsyncResult *res,
                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_broadband_bearer_hso_report_connection_status (MMBroadbandBearerHso *self,
                                                  MMBroadbandBearerHsoConnectionStatus status)
{
    Dial3gppContext *ctx;

    /* Recover context (if any) and remove both cancellation and timeout (if any)*/
    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;

    if (self->priv->connect_pending_id) {
        g_source_remove (self->priv->connect_pending_id);
        self->priv->connect_pending_id = 0;
    }

    if (self->priv->connect_cancellable_id) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;
    }

    switch (status) {
    case MM_BROADBAND_BEARER_HSO_CONNECTION_STATUS_UNKNOWN:
        g_warn_if_reached ();
        break;

    case MM_BROADBAND_BEARER_HSO_CONNECTION_STATUS_CONNECTED:
        if (!ctx)
            break;

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        dial_3gpp_context_complete_and_free (ctx);
        return;

    case MM_BROADBAND_BEARER_HSO_CONNECTION_STATUS_CONNECTION_FAILED:
        if (!ctx)
            break;

        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Call setup failed");
        dial_3gpp_context_complete_and_free (ctx);
        return;

    case MM_BROADBAND_BEARER_HSO_CONNECTION_STATUS_DISCONNECTED:
        if (!ctx) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Call setup failed");
            dial_3gpp_context_complete_and_free (ctx);
        } else {
            /* Just ensure we mark ourselves as being disconnected... */
            g_object_set (self,
                          MM_BEARER_STATUS, MM_BEARER_STATUS_DISCONNECTED,
                          NULL);
        }
        break;
    }
}

static void
connect_reset_ready (MMBaseModem *modem,
                     GAsyncResult *res,
                     Dial3gppContext *ctx)
{
    mm_base_modem_at_command_full_finish (modem, res, NULL);

    /* error should have already been set in the simple async result */
    dial_3gpp_context_complete_and_free (ctx);
}

static void
connect_reset (Dial3gppContext *ctx)
{
    gchar *command;

    /* Need to reset the connection attempt */
    command = g_strdup_printf ("AT_OWANCALL=%d,0,1",
                               ctx->cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)connect_reset_ready,
                                   ctx);
    g_free (command);
}

static gboolean
connect_timed_out_cb (MMBroadbandBearerHso *self)
{
    Dial3gppContext *ctx;

    /* Recover context and remove cancellation */
    ctx = self->priv->connect_pending;

    g_cancellable_disconnect (ctx->cancellable,
                              self->priv->connect_cancellable_id);

    self->priv->connect_pending = NULL;
    self->priv->connect_pending_id = 0;
    self->priv->connect_cancellable_id = 0;

    g_simple_async_result_set_error (ctx->result,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                     "Connection attempt timed out");
    connect_reset (ctx);

    return FALSE;
}

static void
connect_cancelled_cb (GCancellable *cancellable,
                      MMBroadbandBearerHso *self)
{
    GError *error = NULL;
    Dial3gppContext *ctx;

    /* Recover context and remove timeout */
    ctx = self->priv->connect_pending;

    g_source_remove (self->priv->connect_pending_id);

    self->priv->connect_pending = NULL;
    self->priv->connect_pending_id = 0;
    self->priv->connect_cancellable_id = 0;

    g_assert (dial_3gpp_context_set_error_if_cancelled (ctx, &error));

    g_simple_async_result_take_error (ctx->result, error);
    connect_reset (ctx);
}

static void
activate_ready (MMBaseModem *modem,
                GAsyncResult *res,
                Dial3gppContext *ctx)
{
    GError *error = NULL;

    /* From now on, if we get cancelled, we'll need to run the connection
     * reset ourselves just in case */

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete (ctx->result);
        g_object_unref (ctx->result);
        return;
    }

    /* We will now setup a timeout and keep the context in the bearer's private.
     * Reports of modem being connected will arrive via unsolicited messages. */
    g_assert (ctx->self->priv->connect_pending == NULL);
    ctx->self->priv->connect_pending = ctx;
    ctx->self->priv->connect_pending_id = g_timeout_add_seconds (30,
                                                                 (GSourceFunc)connect_timed_out_cb,
                                                                 ctx->self);
    ctx->self->priv->connect_cancellable_id = g_cancellable_connect (ctx->cancellable,
                                                                     G_CALLBACK (connect_cancelled_cb),
                                                                     ctx->self,
                                                                     NULL);
}

static void authenticate (Dial3gppContext *ctx);

static void
authenticate_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    Dial3gppContext *ctx)
{
    gchar *command;

    /* If cancelled, complete */
    if (dial_3gpp_context_complete_and_free_if_cancelled (ctx))
        return;

    if (!mm_base_modem_at_command_full_finish (modem, res, NULL)) {
        /* Try the next auth command */
        ctx->auth_idx++;
        authenticate (ctx);
        return;
    }

    /* Store which auth command worked, for next attempts */
    ctx->self->priv->auth_idx = ctx->auth_idx;

    /* Success, activate the PDP context and start the data session */
    command = g_strdup_printf ("AT_OWANCALL=%d,1,1",
                               ctx->cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)activate_ready,
                                   ctx);
    g_free (command);
}

const gchar *auth_commands[] = {
	"$QCPDPP",
	/* Icera-based devices (GI0322/Quicksilver, iCON 505) don't implement
	 * $QCPDPP, but instead use _OPDPP with the same arguments.
	 */
	"_OPDPP",
	NULL
};

static void
authenticate (Dial3gppContext *ctx)
{
    gchar *command;

    if (!auth_commands[ctx->auth_idx]) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't run HSO authentication");
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Both user and password are required; otherwise firmware returns an error */
    if (!ctx->self->priv->user || !ctx->self->priv->password)
		command = g_strdup_printf ("%s=%d,0",
                                   auth_commands[ctx->auth_idx],
                                   ctx->cid);
    else
        command = g_strdup_printf ("%s=%d,1,\"%s\",\"%s\"",
                                   auth_commands[ctx->auth_idx],
                                   ctx->cid,
                                   ctx->self->priv->password,
                                   ctx->self->priv->user);

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)authenticate_ready,
                                   ctx);
    g_free (command);
}

static void
dial_3gpp (MMBroadbandBearer *self,
           MMBaseModem *modem,
           MMAtSerialPort *primary,
           guint cid,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    g_assert (primary != NULL);

    authenticate (dial_3gpp_context_new (MM_BROADBAND_BEARER_HSO (self),
                                         modem,
                                         primary,
                                         cid,
                                         cancellable,
                                         callback,
                                         user_data));
}

/*****************************************************************************/

static gboolean
cmp_properties (MMBearer *self,
                MMBearerProperties *properties)
{
    MMBroadbandBearerHso *hso = MM_BROADBAND_BEARER_HSO (self);

    return ((mm_broadband_bearer_get_allow_roaming (MM_BROADBAND_BEARER (self)) ==
             mm_bearer_properties_get_allow_roaming (properties)) &&
            (!g_strcmp0 (mm_broadband_bearer_get_ip_type (MM_BROADBAND_BEARER (self)),
                         mm_bearer_properties_get_ip_type (properties))) &&
            (!g_strcmp0 (mm_broadband_bearer_get_3gpp_apn (MM_BROADBAND_BEARER (self)),
                         mm_bearer_properties_get_apn (properties))) &&
            (!g_strcmp0 (hso->priv->user,
                         mm_bearer_properties_get_user (properties))) &&
            (!g_strcmp0 (hso->priv->password,
                         mm_bearer_properties_get_password (properties))));
}

static MMBearerProperties *
expose_properties (MMBearer *self)
{
    MMBroadbandBearerHso *hso = MM_BROADBAND_BEARER_HSO (self);
    MMBearerProperties *properties;

    properties = mm_bearer_properties_new ();
    mm_bearer_properties_set_apn (properties,
                                  mm_broadband_bearer_get_3gpp_apn (MM_BROADBAND_BEARER (self)));
    mm_bearer_properties_set_ip_type (properties,
                                      mm_broadband_bearer_get_ip_type (MM_BROADBAND_BEARER (self)));
    mm_bearer_properties_set_allow_roaming (properties,
                                            mm_broadband_bearer_get_allow_roaming (MM_BROADBAND_BEARER (self)));
    mm_bearer_properties_set_user (properties, hso->priv->user);
    mm_bearer_properties_set_password (properties, hso->priv->user);
    return properties;
}

/*****************************************************************************/

MMBearer *
mm_broadband_bearer_hso_new_finish (GAsyncResult *res,
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
mm_broadband_bearer_hso_new (MMBroadbandModemHso *modem,
                             MMBearerProperties *properties,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_HSO,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM, modem,
        MM_BROADBAND_BEARER_3GPP_APN,      mm_bearer_properties_get_apn (properties),
        MM_BROADBAND_BEARER_IP_TYPE,       mm_bearer_properties_get_ip_type (properties),
        MM_BROADBAND_BEARER_ALLOW_ROAMING, mm_bearer_properties_get_allow_roaming (properties),
        MM_BROADBAND_BEARER_HSO_USER,      mm_bearer_properties_get_user (properties),
        MM_BROADBAND_BEARER_HSO_PASSWORD,  mm_bearer_properties_get_password (properties),
        NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandBearerHso *self = MM_BROADBAND_BEARER_HSO (object);

    switch (prop_id) {
    case PROP_USER:
        g_free (self->priv->user);
        self->priv->user = g_value_dup_string (value);
        break;
    case PROP_PASSWORD:
        g_free (self->priv->password);
        self->priv->password = g_value_dup_string (value);
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
    MMBroadbandBearerHso *self = MM_BROADBAND_BEARER_HSO (object);

    switch (prop_id) {
    case PROP_USER:
        g_value_set_string (value, self->priv->user);
        break;
    case PROP_PASSWORD:
        g_value_set_string (value, self->priv->password);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_bearer_hso_init (MMBroadbandBearerHso *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER_HSO,
                                              MMBroadbandBearerHsoPrivate);
}

static void
finalize (GObject *object)
{
    MMBroadbandBearerHso *self = MM_BROADBAND_BEARER_HSO (object);

    g_free (self->priv->user);
    g_free (self->priv->password);

    G_OBJECT_CLASS (mm_broadband_bearer_hso_parent_class)->finalize (object);
}

static void
mm_broadband_bearer_hso_class_init (MMBroadbandBearerHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;

    bearer_class->cmp_properties = cmp_properties;
    bearer_class->expose_properties = expose_properties;

    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;

    properties[PROP_USER] =
        g_param_spec_string (MM_BROADBAND_BEARER_HSO_USER,
                             "User",
                             "Username to use to authenticate the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_USER, properties[PROP_USER]);

    properties[PROP_PASSWORD] =
        g_param_spec_string (MM_BROADBAND_BEARER_HSO_PASSWORD,
                             "Password",
                             "Password to use to authenticate the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_PASSWORD, properties[PROP_PASSWORD]);
}
