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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
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

#include "mm-broadband-bearer-icera.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-daemon-enums-types.h"
#include "mm-modem-helpers-icera.h"

G_DEFINE_TYPE (MMBroadbandBearerIcera, mm_broadband_bearer_icera, MM_TYPE_BROADBAND_BEARER);

enum {
    PROP_0,
    PROP_DEFAULT_IP_METHOD,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandBearerIceraPrivate {
    MMBearerIpMethod default_ip_method;

    /* Connection related */
    gpointer connect_pending;
    guint connect_pending_id;
    gulong connect_cancellable_id;
    gulong connect_port_closed_id;

    /* Disconnection related */
    gpointer disconnect_pending;
    guint disconnect_pending_id;
};

/*****************************************************************************/
/* 3GPP IP config retrieval (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearerIcera *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    guint cid;
    GSimpleAsyncResult *result;
} GetIpConfig3gppContext;

static GetIpConfig3gppContext *
get_ip_config_3gpp_context_new (MMBroadbandBearerIcera *self,
                                MMBaseModem *modem,
                                MMPortSerialAt *primary,
                                guint cid,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    GetIpConfig3gppContext *ctx;

    ctx = g_new0 (GetIpConfig3gppContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;
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
        g_simple_async_result_take_error (ctx->result, error);
        goto out;
    }

    if (!mm_icera_parse_ipdpaddr_response (response,
                                           ctx->cid,
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

    ctx = get_ip_config_3gpp_context_new (MM_BROADBAND_BEARER_ICERA (self),
                                          MM_BASE_MODEM (modem),
                                          primary,
                                          cid,
                                          callback,
                                          user_data);

    if (ctx->self->priv->default_ip_method == MM_BEARER_IP_METHOD_STATIC) {
        gchar *command;

        command = g_strdup_printf ("%%IPDPADDR=%u", cid);
        mm_base_modem_at_command_full (MM_BASE_MODEM (modem),
                                       primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)ip_config_ready,
                                       ctx);
        g_free (command);
        return;
    }

    /* Otherwise, DHCP */
    if (ctx->self->priv->default_ip_method == MM_BEARER_IP_METHOD_DHCP) {
        MMBearerConnectResult *connect_result;
        MMBearerIpConfig *ipv4_config = NULL, *ipv6_config = NULL;

        if (ip_family & MM_BEARER_IP_FAMILY_IPV4 || ip_family & MM_BEARER_IP_FAMILY_IPV4V6) {
            ipv4_config = mm_bearer_ip_config_new ();
            mm_bearer_ip_config_set_method (ipv4_config, MM_BEARER_IP_METHOD_DHCP);
        }
        if (ip_family & MM_BEARER_IP_FAMILY_IPV6 || ip_family & MM_BEARER_IP_FAMILY_IPV4V6) {
            ipv6_config = mm_bearer_ip_config_new ();
            mm_bearer_ip_config_set_method (ipv6_config, MM_BEARER_IP_METHOD_DHCP);
        }
        g_assert (ipv4_config || ipv6_config);

        connect_result = mm_bearer_connect_result_new (MM_PORT (ctx->primary),
                                                       ipv4_config,
                                                       ipv6_config);
        g_clear_object (&ipv4_config);
        g_clear_object (&ipv6_config);

        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   connect_result,
                                                   (GDestroyNotify)mm_bearer_connect_result_unref);
        get_ip_config_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

/*****************************************************************************/
/* 3GPP disconnection */

typedef struct {
    MMBroadbandBearerIcera *self;
    GSimpleAsyncResult *result;
} Disconnect3gppContext;

static void
disconnect_3gpp_context_complete_and_free (Disconnect3gppContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer *self,
                        GAsyncResult *res,
                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
disconnect_3gpp_timed_out_cb (MMBroadbandBearerIcera *self)
{
    Disconnect3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->disconnect_pending;

    self->priv->disconnect_pending = NULL;
    self->priv->disconnect_pending_id = 0;

    g_simple_async_result_set_error (ctx->result,
                                     MM_SERIAL_ERROR,
                                     MM_SERIAL_ERROR_RESPONSE_TIMEOUT,
                                     "Disconnection attempt timed out");

    disconnect_3gpp_context_complete_and_free (ctx);
    return G_SOURCE_REMOVE;
}

static void
report_disconnect_status (MMBroadbandBearerIcera *self,
                          MMBearerConnectionStatus status)
{
    Disconnect3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->disconnect_pending;
    self->priv->disconnect_pending = NULL;
    g_assert (ctx != NULL);

    /* Cleanup timeout, if any */
    if (self->priv->disconnect_pending_id) {
        g_source_remove (self->priv->disconnect_pending_id);
        self->priv->disconnect_pending_id = 0;
    }

    /* Received 'CONNECTED' during a disconnection attempt? */
    if (status == MM_BEARER_CONNECTION_STATUS_CONNECTED) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Disconnection failed");
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Received 'DISCONNECTED' during a disconnection attempt? */
    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED ||
        status == MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED) {
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* No other status is expected by this implementation */
    g_assert_not_reached ();
}

static void
disconnect_ipdpact_ready (MMBaseModem *modem,
                          GAsyncResult *res,
                          MMBroadbandBearerIcera *self)
{
    Disconnect3gppContext *ctx;
    GError *error = NULL;

    /* Try to recover the disconnection context. If none found, it means the
     * context was already completed and we have nothing else to do. */
    ctx = self->priv->disconnect_pending;

    /* Balance refcount with the extra ref we passed to command_full() */
    g_object_unref (self);

    if (!ctx) {
        mm_dbg ("Disconnection context was finished already by an unsolicited message");

        /* Run _finish() to finalize the async call, even if we don't care
         * the result */
        mm_base_modem_at_command_full_finish (modem, res, NULL);
        return;
    }

    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        self->priv->disconnect_pending = NULL;
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Set a 60-second disconnection-failure timeout */
    self->priv->disconnect_pending_id = g_timeout_add_seconds (60,
                                                               (GSourceFunc)disconnect_3gpp_timed_out_cb,
                                                               self);
}

static void
disconnect_3gpp (MMBroadbandBearer *bearer,
                 MMBroadbandModem *modem,
                 MMPortSerialAt *primary,
                 MMPortSerialAt *secondary,
                 MMPort *data,
                 guint cid,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    MMBroadbandBearerIcera *self = MM_BROADBAND_BEARER_ICERA (bearer);
    gchar *command;
    Disconnect3gppContext *ctx;

    ctx = g_new0 (Disconnect3gppContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect_3gpp);

    /* The unsolicited response to %IPDPACT may come before the OK does.
     * We will keep the disconnection context in the bearer private data so
     * that it is accessible from the unsolicited message handler. Note
     * also that we do NOT pass the ctx to the GAsyncReadyCallback, as it
     * may not be valid any more when the callback is called (it may be
     * already completed in the unsolicited handling) */
    g_assert (ctx->self->priv->disconnect_pending == NULL);
    ctx->self->priv->disconnect_pending = ctx;

    command = g_strdup_printf ("%%IPDPACT=%d,0", cid);
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (modem),
        primary,
        command,
        60,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)disconnect_ipdpact_ready,
        g_object_ref (ctx->self)); /* we pass the bearer object! */
    g_free (command);
}

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearerIcera *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    guint cid;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    MMPort *data;
    guint authentication_retries;
    GError *saved_error;
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
    command = g_strdup_printf ("%%IPDPACT=%d,0", ctx->cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)connect_reset_ready,
                                   ctx);
    g_free (command);
}

static gboolean
connect_timed_out_cb (MMBroadbandBearerIcera *self)
{
    Dial3gppContext *ctx;

    /* Recover context and remove it from the private info */
    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;

    /* Remove cancellation, if found */
    if (self->priv->connect_cancellable_id) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;
    }

    /* Remove closed port watch, if found */
    if (ctx && self->priv->connect_port_closed_id) {
        g_signal_handler_disconnect (ctx->primary, self->priv->connect_port_closed_id);
        self->priv->connect_port_closed_id = 0;
    }

    /* Cleanup timeout ID */
    self->priv->connect_pending_id = 0;

    /* If we were cancelled, prefer that error */
    if (ctx->saved_error) {
        g_simple_async_result_take_error (ctx->result, ctx->saved_error);
        ctx->saved_error = NULL;
    } else
        g_simple_async_result_set_error (ctx->result,
                                         MM_MOBILE_EQUIPMENT_ERROR,
                                         MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                         "Connection attempt timed out");

    /* It's probably pointless to try to reset this here, but anyway... */
    connect_reset (ctx);

    return G_SOURCE_REMOVE;
}

static void
connect_cancelled_cb (GCancellable *cancellable,
                      MMBroadbandBearerIcera *self)
{
    Dial3gppContext *ctx;

    /* Recover context but DON'T remove it from the private info */
    ctx = self->priv->connect_pending;

    /* Remove the cancellable
     * NOTE: we shouldn't remove the timeout yet. We still need to wait
     * to get connected before running the explicit connection reset */
    self->priv->connect_cancellable_id = 0;

    /* Store cancelled error */
    g_assert (dial_3gpp_context_set_error_if_cancelled (ctx, &ctx->saved_error));

    /* We cannot reset right here, we need to wait for the connection
     * attempt to finish */
}

static void
forced_close_cb (MMPortSerial *port,
                 MMBroadbandBearerIcera *self)
{
    /* Just treat the forced close event as any other unsolicited message */
    mm_base_bearer_report_connection_status (MM_BASE_BEARER (self),
                                             MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED);
}

static void
ier_query_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 Dial3gppContext *ctx)
{
    const gchar *response;
    GError *activation_error = NULL;

    response = mm_base_modem_at_command_full_finish (modem, res, NULL);
    if (response) {
        gint nw_activation_err;

        response = mm_strip_tag (response, "%IER:");
        if (sscanf (response, "%*d,%*d,%d", &nw_activation_err)) {
            /* 3GPP TS 24.008 Annex G error codes:
             * 27 - Unknown or missing access point name
             * 33 - Requested service option not subscribed
             */
            if (nw_activation_err == 27 || nw_activation_err == 33)
                activation_error = mm_mobile_equipment_error_for_code (
                    MM_MOBILE_EQUIPMENT_ERROR_GPRS_SERVICE_OPTION_NOT_SUBSCRIBED);
        }
    }

    if (activation_error)
        g_simple_async_result_take_error (ctx->result, activation_error);
    else
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Call setup failed");
    dial_3gpp_context_complete_and_free (ctx);
}

static void
report_connect_status (MMBroadbandBearerIcera *self,
                       MMBearerConnectionStatus status)
{
    Dial3gppContext *ctx;

    g_assert (status == MM_BEARER_CONNECTION_STATUS_CONNECTED ||
              status == MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED ||
              status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED);

    /* Recover context and remove it from the private info */
    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;
    g_assert (ctx != NULL);

    /* Cleanup cancellable, timeout and port closed watch, if any */
    if (self->priv->connect_pending_id) {
        g_source_remove (self->priv->connect_pending_id);
        self->priv->connect_pending_id = 0;
    }

    if (self->priv->connect_cancellable_id) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;
    }

    if (self->priv->connect_port_closed_id) {
        g_signal_handler_disconnect (ctx->primary, self->priv->connect_port_closed_id);
        self->priv->connect_port_closed_id = 0;
    }

    /* Received 'CONNECTED' during a connection attempt? */
    if (status == MM_BEARER_CONNECTION_STATUS_CONNECTED) {
        /* If we wanted to get cancelled before, do it now */
        if (ctx->saved_error) {
            /* Keep error */
            g_simple_async_result_take_error (ctx->result, ctx->saved_error);
            ctx->saved_error = NULL;
            /* Cancel connection */
            connect_reset (ctx);
            return;
        }

        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   g_object_ref (ctx->data),
                                                   (GDestroyNotify)g_object_unref);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* If we wanted to get cancelled before and now we couldn't connect,
     * use the cancelled error and return */
    if (ctx->saved_error) {
        g_simple_async_result_take_error (ctx->result, ctx->saved_error);
        ctx->saved_error = NULL;
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Received CONNECTION_FAILED during a connection attempt? */
    if (status == MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED) {
        /* Try to gather additional info about the connection failure */
        mm_base_modem_at_command_full (
            ctx->modem,
            ctx->primary,
            "%IER?",
            60,
            FALSE,
            FALSE, /* raw */
            NULL, /* cancellable */
            (GAsyncReadyCallback)ier_query_ready,
            ctx);
        return;
    }

    /* Otherwise, received 'DISCONNECTED' during a connection attempt? */
    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Call setup failed");
    dial_3gpp_context_complete_and_free (ctx);
}

static void
activate_ready (MMBaseModem *modem,
                GAsyncResult *res,
                MMBroadbandBearerIcera *self)
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

    /* Errors on the dial command are fatal */
    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        self->priv->connect_pending = NULL;
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* We will now setup a timeout and keep the context in the bearer's private.
     * Reports of modem being connected will arrive via unsolicited messages.
     * This timeout should be long enough. Actually... ideally should never get
     * reached. */
    self->priv->connect_pending_id = g_timeout_add_seconds (60,
                                                            (GSourceFunc)connect_timed_out_cb,
                                                            self);

    /* From now on, if we get cancelled, we'll still need to wait for the connection
     * attempt to finish before resetting it */
    self->priv->connect_cancellable_id = g_cancellable_connect (ctx->cancellable,
                                                                G_CALLBACK (connect_cancelled_cb),
                                                                self,
                                                                NULL);

    /* If we get the port closed, we treat as a connect error */
    self->priv->connect_port_closed_id = g_signal_connect (ctx->primary,
                                                           "forced-close",
                                                           G_CALLBACK (forced_close_cb),
                                                           self);
}

static void authenticate (Dial3gppContext *ctx);

static gboolean
retry_authentication_cb (Dial3gppContext *ctx)
{
    authenticate (ctx);
    return G_SOURCE_REMOVE;
}

static void
authenticate_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    Dial3gppContext *ctx)
{
    GError *error = NULL;
    gchar *command;

    /* If cancelled, complete */
    if (dial_3gpp_context_complete_and_free_if_cancelled (ctx))
        return;

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        /* Retry configuring the context. It sometimes fails with a 583
         * error ["a profile (CID) is currently active"] if a connect
         * is attempted too soon after a disconnect. */
        if (++ctx->authentication_retries < 3) {
            mm_dbg ("Authentication failed: '%s'; retrying...", error->message);
            g_error_free (error);
            g_timeout_add_seconds (1, (GSourceFunc)retry_authentication_cb, ctx);
            return;
        }

        /* Return an error */
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* The unsolicited response to %IPDPACT may come before the OK does.
     * We will keep the connection context in the bearer private data so
     * that it is accessible from the unsolicited message handler. Note
     * also that we do NOT pass the ctx to the GAsyncReadyCallback, as it
     * may not be valid any more when the callback is called (it may be
     * already completed in the unsolicited handling) */
    g_assert (ctx->self->priv->connect_pending == NULL);
    ctx->self->priv->connect_pending = ctx;

    command = g_strdup_printf ("%%IPDPACT=%d,1", ctx->cid);
    mm_base_modem_at_command_full (
        ctx->modem,
        ctx->primary,
        command,
        60,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)activate_ready,
        g_object_ref (ctx->self)); /* we pass the bearer object! */
    g_free (command);
}

static void
authenticate (Dial3gppContext *ctx)
{
    gchar *command;
    const gchar *user;
    const gchar *password;
    MMBearerAllowedAuth allowed_auth;

    user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    password = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    allowed_auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));

    /* Both user and password are required; otherwise firmware returns an error */
    if (!user || !password || allowed_auth == MM_BEARER_ALLOWED_AUTH_NONE) {
        mm_dbg ("Not using authentication");
        command = g_strdup_printf ("%%IPDPCFG=%d,0,0,\"\",\"\"", ctx->cid);
    } else {
        gchar *quoted_user;
        gchar *quoted_password;
        guint icera_auth;

        if (allowed_auth == MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
            mm_dbg ("Using default (PAP) authentication method");
            icera_auth = 1;
        } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_PAP) {
            mm_dbg ("Using PAP authentication method");
            icera_auth = 1;
        } else if (allowed_auth & MM_BEARER_ALLOWED_AUTH_CHAP) {
            mm_dbg ("Using CHAP authentication method");
            icera_auth = 2;
        } else {
            gchar *str;

            str = mm_bearer_allowed_auth_build_string_from_mask (allowed_auth);
            g_simple_async_result_set_error (
                ctx->result,
                MM_CORE_ERROR,
                MM_CORE_ERROR_UNSUPPORTED,
                "Cannot use any of the specified authentication methods (%s)",
                str);
            g_free (str);
            dial_3gpp_context_complete_and_free (ctx);
            return;
        }

        quoted_user = mm_port_serial_at_quote_string (user);
        quoted_password = mm_port_serial_at_quote_string (password);
        command = g_strdup_printf ("%%IPDPCFG=%d,0,%u,%s,%s",
                                   ctx->cid, icera_auth, quoted_user, quoted_password);
        g_free (quoted_user);
        g_free (quoted_password);
    }

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   60,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)authenticate_ready,
                                   ctx);
    g_free (command);
}

static void
deactivate_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  Dial3gppContext *ctx)
{
    /*
     * Ignore any error here; %IPDPACT=ctx,0 will produce an error 767
     * if the context is not, in fact, connected. This is annoying but
     * harmless.
     */
    mm_base_modem_at_command_full_finish (modem, res, NULL);

    authenticate (ctx);
}

static void
connect_deactivate (Dial3gppContext *ctx)
{
    gchar *command;

    /* Deactivate the context we want to use before we try to activate
     * it. This handles the case where ModemManager crashed while
     * connected and is now trying to reconnect. (Should some part of
     * the core or modem driver have made sure of this already?)
     */
    command = g_strdup_printf ("%%IPDPACT=%d,0", ctx->cid);
    mm_base_modem_at_command_full (
        ctx->modem,
        ctx->primary,
        command,
        60,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)deactivate_ready,
        ctx);
    g_free (command);
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

    connect_deactivate (ctx);
}

/*****************************************************************************/

static void
report_connection_status (MMBaseBearer *bearer,
                          MMBearerConnectionStatus status)
{
    MMBroadbandBearerIcera *self = MM_BROADBAND_BEARER_ICERA (bearer);

    /* Process pending connection attempt */
    if (self->priv->connect_pending) {
        report_connect_status (self, status);
        return;
    }

    /* Process pending disconnection attempt */
    if (self->priv->disconnect_pending) {
        report_disconnect_status (self, status);
        return;
    }

    mm_dbg ("Received spontaneous %%IPDPACT (%s)",
            mm_bearer_connection_status_get_string (status));

    /* Received a random 'DISCONNECTED'...*/
    if (status == MM_BEARER_CONNECTION_STATUS_DISCONNECTED ||
        status == MM_BEARER_CONNECTION_STATUS_CONNECTION_FAILED) {
        /* If no connection/disconnection attempt on-going, make sure we mark ourselves as
         * disconnected. Make sure we only pass 'DISCONNECTED' to the parent */
        MM_BASE_BEARER_CLASS (mm_broadband_bearer_icera_parent_class)->report_connection_status (
            bearer,
            MM_BEARER_CONNECTION_STATUS_DISCONNECTED);
    }
}

/*****************************************************************************/

MMBaseBearer *
mm_broadband_bearer_icera_new_finish (GAsyncResult *res,
                                      GError **error)
{
    GObject *source;
    GObject *bearer;

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
mm_broadband_bearer_icera_new (MMBroadbandModem *modem,
                               MMBearerIpMethod ip_method,
                               MMBearerProperties *config,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_ICERA,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        MM_BROADBAND_BEARER_ICERA_DEFAULT_IP_METHOD, ip_method,
        NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandBearerIcera *self = MM_BROADBAND_BEARER_ICERA (object);

    switch (prop_id) {
    case PROP_DEFAULT_IP_METHOD:
        self->priv->default_ip_method = g_value_get_enum (value);
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
    MMBroadbandBearerIcera *self = MM_BROADBAND_BEARER_ICERA (object);

    switch (prop_id) {
    case PROP_DEFAULT_IP_METHOD:
        g_value_set_enum (value, self->priv->default_ip_method);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_bearer_icera_init (MMBroadbandBearerIcera *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_BEARER_ICERA,
                                              MMBroadbandBearerIceraPrivate);

    /* Defaults */
    self->priv->default_ip_method = MM_BEARER_IP_METHOD_STATIC;
}

static void
mm_broadband_bearer_icera_class_init (MMBroadbandBearerIceraClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBaseBearerClass *base_bearer_class = MM_BASE_BEARER_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerIceraPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;

    base_bearer_class->report_connection_status = report_connection_status;
    base_bearer_class->load_connection_status = NULL;
    base_bearer_class->load_connection_status_finish = NULL;

    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;
    broadband_bearer_class->get_ip_config_3gpp = get_ip_config_3gpp;
    broadband_bearer_class->get_ip_config_3gpp_finish = get_ip_config_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;

    properties[PROP_DEFAULT_IP_METHOD] =
        g_param_spec_enum (MM_BROADBAND_BEARER_ICERA_DEFAULT_IP_METHOD,
                           "Default IP method",
                           "Default IP Method (static or DHCP) to use.",
                           MM_TYPE_BEARER_IP_METHOD,
                           MM_BEARER_IP_METHOD_STATIC,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_DEFAULT_IP_METHOD, properties[PROP_DEFAULT_IP_METHOD]);
}
