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
 * Copyright (C) 2008 - 2010 Ericsson AB
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Lanedo GmbH
 *
 * Author: Per Hallsmark <per.hallsmark@ericsson.com>
 *         Bjorn Runaker <bjorn.runaker@ericsson.com>
 *         Torgny Johansson <torgny.johansson@ericsson.com>
 *         Jonas Sjöquist <jonas.sjoquist@ericsson.com>
 *         Dan Williams <dcbw@redhat.com>
 *         Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-mbm.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-mbm.h"
#include "mm-daemon-enums-types.h"

G_DEFINE_TYPE (MMBroadbandBearerMbm, mm_broadband_bearer_mbm, MM_TYPE_BROADBAND_BEARER);

struct _MMBroadbandBearerMbmPrivate {
    gpointer connect_pending;
    guint connect_pending_id;
    gulong connect_cancellable_id;
};

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearerMbm *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    guint cid;
    GCancellable *cancellable;
    MMPort *data;
    GSimpleAsyncResult *result;
    guint poll_count;
} Dial3gppContext;

static void
dial_3gpp_context_complete_and_free (Dial3gppContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->data)
        g_object_unref (ctx->data);
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
dial_3gpp_context_complete_and_free_if_cancelled (Dial3gppContext *ctx)
{
    GError *error = NULL;

    if (!dial_3gpp_context_set_error_if_cancelled (ctx, &error))
        return FALSE;

    g_simple_async_result_take_error (ctx->result, error);
    dial_3gpp_context_complete_and_free (ctx);
    return TRUE;
}

static MMPort *
dial_3gpp_finish (MMBroadbandBearer *self,
                  GAsyncResult *res,
                  GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return MM_PORT (g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res))));
}

static void
report_connection_status (MMBaseBearer *bearer,
                          MMBearerConnectionStatus status)
{
    MMBroadbandBearerMbm *self = MM_BROADBAND_BEARER_MBM (bearer);
    Dial3gppContext *ctx;

    g_assert (status == MM_BEARER_CONNECTION_STATUS_CONNECTED ||
              status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED);

    /* Recover context (if any) and remove both cancellation and timeout (if any)*/
    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;

    /* Connection status reported but no connection attempt? */
    if (!ctx) {
        g_assert (self->priv->connect_pending_id == 0);

        mm_dbg ("Received spontaneous *E2NAP (%s)",
                mm_bearer_connection_status_get_string (status));

        if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED) {
            /* If no connection attempt on-going, make sure we mark ourselves as
             * disconnected */
            MM_BASE_BEARER_CLASS (mm_broadband_bearer_mbm_parent_class)->report_connection_status (
                bearer,
                status);
        }
        return;
    }

    if (self->priv->connect_pending_id) {
        g_source_remove (self->priv->connect_pending_id);
        self->priv->connect_pending_id = 0;
    }

    if (self->priv->connect_cancellable_id) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;
    }

    /* Reporting connected */
    if (status == MM_BEARER_CONNECTION_STATUS_CONNECTED) {
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   g_object_ref (ctx->data),
                                                   (GDestroyNotify)g_object_unref);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Reporting disconnected */
    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Call setup failed");
    dial_3gpp_context_complete_and_free (ctx);
}

static void
connect_cancelled_cb (GCancellable *cancellable,
                      MMBroadbandBearerMbm *self)
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
    dial_3gpp_context_complete_and_free (ctx);
}

static gboolean poll_timeout_cb (MMBroadbandBearerMbm *self);

static void
poll_ready (MMBaseModem *modem,
            GAsyncResult *res,
            MMBroadbandBearerMbm *self)
{
    Dial3gppContext *ctx;
    GError *error = NULL;
    const gchar *response;
    guint state;

    /* Try to recover the connection context. If none found, it means the
     * context was already completed and we have nothing else to do. */
    ctx = self->priv->connect_pending;

    /* Balance refcount with the extra ref we passed to command_full() */
    g_object_unref (self);

    if (!ctx) {
        mm_dbg ("Connection context was finished already by an unsolicited message");

        /* Run _finish() to finalize the async call, even if we don't care
         * the result */
        mm_base_modem_at_command_full_finish (modem, res, NULL);
        return;
    }

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (response
        && sscanf (response, "*ENAP: %d", &state) == 1
        && state == 1) {
        /* Success!  Connected... */
        self->priv->connect_pending = NULL;

        if (ctx && self->priv->connect_cancellable_id) {
            g_cancellable_disconnect (ctx->cancellable,
                                      self->priv->connect_cancellable_id);
            self->priv->connect_cancellable_id = 0;
        }

        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   g_object_ref (ctx->data),
                                                   (GDestroyNotify)g_object_unref);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    self->priv->connect_pending_id = g_timeout_add_seconds (1,
                                                            (GSourceFunc)poll_timeout_cb,
                                                            self);
}

static gboolean
poll_timeout_cb (MMBroadbandBearerMbm *self)
{
    Dial3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->connect_pending;

    /* Too many retries... */
    if (ctx->poll_count > 50) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);

        self->priv->connect_pending = NULL;
        self->priv->connect_pending_id = 0;
        self->priv->connect_cancellable_id = 0;

        g_simple_async_result_set_error (ctx->result,
                                         MM_MOBILE_EQUIPMENT_ERROR,
                                         MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                         "Connection attempt timed out");
        dial_3gpp_context_complete_and_free (ctx);
        return G_SOURCE_REMOVE;
    }

    ctx->poll_count++;
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   "AT*ENAP?",
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)poll_ready,
                                   g_object_ref (ctx->self)); /* we pass the bearer object! */
    self->priv->connect_pending_id = 0;
    return G_SOURCE_REMOVE;
}

static void
activate_ready (MMBaseModem *modem,
                GAsyncResult *res,
                MMBroadbandBearerMbm *self)
{
    Dial3gppContext *ctx;
    GError *error = NULL;

    /* Try to recover the connection context. If none found, it means the
     * context was already completed and we have nothing else to do. */
    ctx = self->priv->connect_pending;

    /* Balance refcount with the extra ref we passed to command_full() */
    g_object_unref (self);

    if (!ctx) {
        mm_dbg ("Connection context was finished already by an unsolicited message");

        /* Run _finish() to finalize the async call, even if we don't care
         * the result */
        mm_base_modem_at_command_full_finish (modem, res, NULL);
        return;
    }

    /* From now on, if we get cancelled, we'll need to run the connection
     * reset ourselves just in case */

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        self->priv->connect_pending = NULL;
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* We will now setup a timeout to poll for the status */
    self->priv->connect_pending_id = g_timeout_add_seconds (1,
                                                            (GSourceFunc)poll_timeout_cb,
                                                            self);

    self->priv->connect_cancellable_id = g_cancellable_connect (ctx->cancellable,
                                                                G_CALLBACK (connect_cancelled_cb),
                                                                self,
                                                                NULL);
}

static void
activate (Dial3gppContext *ctx)
{
    gchar *command;

    /* The unsolicited response to ENAP may come before the OK does.
     * We will keep the connection context in the bearer private data so
     * that it is accessible from the unsolicited message handler. Note
     * also that we do NOT pass the ctx to the GAsyncReadyCallback, as it
     * may not be valid any more when the callback is called (it may be
     * already completed in the unsolicited handling) */
    g_assert (ctx->self->priv->connect_pending == NULL);
    ctx->self->priv->connect_pending = ctx;

    /* Success, activate the PDP context and start the data session */
    command = g_strdup_printf ("AT*ENAP=1,%d",
                               ctx->cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)activate_ready,
                                   g_object_ref (ctx->self)); /* we pass the bearer object! */
    g_free (command);
}

static void
authenticate_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    Dial3gppContext *ctx)
{
    GError *error = NULL;

    /* If cancelled, complete */
    if (dial_3gpp_context_complete_and_free_if_cancelled (ctx))
        return;

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    activate (ctx);
}

static void
authenticate (Dial3gppContext *ctx)
{
    const gchar *user;
    const gchar *password;

    user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    password = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));

    /* Both user and password are required; otherwise firmware returns an error */
    if (user || password) {
        gchar *command;
        gchar *encoded_user;
        gchar *encoded_password;

        encoded_user = mm_broadband_modem_take_and_convert_to_current_charset (MM_BROADBAND_MODEM (ctx->modem),
                                                                               g_strdup (user));
        encoded_password = mm_broadband_modem_take_and_convert_to_current_charset (MM_BROADBAND_MODEM (ctx->modem),
                                                                                   g_strdup (password));

        command = g_strdup_printf ("AT*EIAAUW=%d,1,\"%s\",\"%s\"",
                                   ctx->cid,
                                   encoded_user ? encoded_user : "",
                                   encoded_password ? encoded_password : "");
        g_free (encoded_user);
        g_free (encoded_password);

        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)authenticate_ready,
                                       ctx);
        g_free (command);
        return;
    }

    mm_dbg ("Authentication not needed");
    activate (ctx);
}

static void
dial_3gpp (MMBroadbandBearer *self,
           MMBaseModem *modem,
           MMPortSerialAt *primary,
           guint cid,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    Dial3gppContext *ctx;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (Dial3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             dial_3gpp);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->poll_count = 0;

    /* We need a net data port */
    ctx->data = mm_base_modem_get_best_data_port (modem, MM_PORT_TYPE_NET);
    if (!ctx->data) {
        g_simple_async_result_set_error (
            ctx->result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_NOT_FOUND,
            "No valid data port found to launch connection");
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    authenticate (ctx);
}

/*****************************************************************************/
/* 3GPP IP config retrieval (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearerMbm *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMBearerIpFamily family;
    GSimpleAsyncResult *result;
} GetIpConfig3gppContext;

static GetIpConfig3gppContext *
get_ip_config_3gpp_context_new (MMBroadbandBearerMbm *self,
                                MMBaseModem *modem,
                                MMPortSerialAt *primary,
                                MMBearerIpFamily family,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    GetIpConfig3gppContext *ctx;

    ctx = g_new0 (GetIpConfig3gppContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->family = family;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             get_ip_config_3gpp_context_new);
    return ctx;
}

static void
get_ip_config_context_complete_and_free (GetIpConfig3gppContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
get_ip_config_3gpp_finish (MMBroadbandBearer *self,
                           GAsyncResult *res,
                           MMBearerIpConfig **ipv4_config,
                           MMBearerIpConfig **ipv6_config,
                           GError **error)
{
    MMBearerConnectResult *configs;
    MMBearerIpConfig *ipv4, *ipv6;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    configs = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    g_assert (configs);

    ipv4 = mm_bearer_connect_result_peek_ipv4_config (configs);
    ipv6 = mm_bearer_connect_result_peek_ipv6_config (configs);
    g_assert (ipv4 || ipv6);
    if (ipv4_config && ipv4)
        *ipv4_config = g_object_ref (ipv4);
    if (ipv6_config && ipv6)
        *ipv6_config = g_object_ref (ipv6);

    return TRUE;
}

static void
ip_config_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 GetIpConfig3gppContext *ctx)
{
    MMBearerIpConfig *ipv4_config = NULL;
    MMBearerIpConfig *ipv6_config = NULL;
    const gchar *response;
    GError *error = NULL;
    MMBearerConnectResult *connect_result;

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_error_free (error);

        /* Fall back to DHCP configuration; early devices don't support *E2IPCFG */
        if (ctx->family == MM_BEARER_IP_FAMILY_IPV4 || ctx->family == MM_BEARER_IP_FAMILY_IPV4V6) {
            ipv4_config = mm_bearer_ip_config_new ();
            mm_bearer_ip_config_set_method (ipv4_config, MM_BEARER_IP_METHOD_DHCP);
        }
        if (ctx->family == MM_BEARER_IP_FAMILY_IPV6 || ctx->family == MM_BEARER_IP_FAMILY_IPV4V6) {
            ipv6_config = mm_bearer_ip_config_new ();
            mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_DHCP);
        }
    } else {
        if (!mm_mbm_parse_e2ipcfg_response (response,
                                            &ipv4_config,
                                            &ipv6_config,
                                            &error)) {
            g_simple_async_result_take_error (ctx->result, error);
            goto out;
        }

        if (!ipv4_config && !ipv6_config) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't get IP config: couldn't parse response '%s'",
                                             response);
            goto out;
        }
    }

    connect_result = mm_bearer_connect_result_new (MM_PORT (ctx->primary),
                                                   ipv4_config,
                                                   ipv6_config);
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               connect_result,
                                               (GDestroyNotify)mm_bearer_connect_result_unref);

out:
    g_clear_object (&ipv4_config);
    g_clear_object (&ipv6_config);
    get_ip_config_context_complete_and_free (ctx);
}

static void
get_ip_config_3gpp (MMBroadbandBearer *self,
                    MMBroadbandModem *modem,
                    MMPortSerialAt *primary,
                    MMPortSerialAt *secondary,
                    MMPort *data,
                    guint cid,
                    MMBearerIpFamily ip_family,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GetIpConfig3gppContext *ctx;

    ctx = get_ip_config_3gpp_context_new (MM_BROADBAND_BEARER_MBM (self),
                                          MM_BASE_MODEM (modem),
                                          primary,
                                          ip_family,
                                          callback,
                                          user_data);

    mm_base_modem_at_command_full (MM_BASE_MODEM (modem),
                                   primary,
                                   "*E2IPCFG?",
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)ip_config_ready,
                                   ctx);
}

/*****************************************************************************/
/* 3GPP disconnect */

typedef struct {
    MMBroadbandBearerMbm *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    GSimpleAsyncResult *result;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disconnect_enap_ready (MMBaseModem *modem,
                       GAsyncResult *res,
                       DisconnectContext *ctx)
{
    GError *error = NULL;

    /* Ignore errors for now */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_dbg ("Disconnection failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    disconnect_context_complete_and_free (ctx);
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
    DisconnectContext *ctx;

    g_assert (primary != NULL);

    ctx = g_new0 (DisconnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect_3gpp);

    mm_base_modem_at_command_full (MM_BASE_MODEM (modem),
                                   primary,
                                   "*ENAP=0",
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)disconnect_enap_ready,
                                   ctx);
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_mbm_new_finish (GAsyncResult *res,
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
mm_broadband_bearer_mbm_new (MMBroadbandModemMbm *modem,
                             MMBearerProperties *config,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_MBM,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_mbm_init (MMBroadbandBearerMbm *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_BEARER_MBM,
                                              MMBroadbandBearerMbmPrivate);
}

static void
mm_broadband_bearer_mbm_class_init (MMBroadbandBearerMbmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerMbmPrivate));

    base_bearer_class->report_connection_status = report_connection_status;
    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;
    broadband_bearer_class->get_ip_config_3gpp = get_ip_config_3gpp;
    broadband_bearer_class->get_ip_config_3gpp_finish = get_ip_config_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
