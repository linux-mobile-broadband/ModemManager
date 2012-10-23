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

    /* Disconnection related */
    gpointer disconnect_pending;
    guint disconnect_pending_id;
};

/*****************************************************************************/
/* 3GPP IP config retrieval (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearerIcera *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    guint cid;
    GSimpleAsyncResult *result;
} GetIpConfig3gppContext;

static GetIpConfig3gppContext *
get_ip_config_3gpp_context_new (MMBroadbandBearerIcera *self,
                                MMBaseModem *modem,
                                MMAtSerialPort *primary,
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
    MMBearerIpConfig *ip_config;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    /* No IPv6 for now */
    ip_config = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *ipv4_config = g_object_ref (ip_config);
    *ipv6_config = NULL;
    return TRUE;
}

#define IPDPADDR_TAG "%IPDPADDR: "

static void
ip_config_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 GetIpConfig3gppContext *ctx)
{
    MMBearerIpConfig *ip_config = NULL;
    const gchar *response;
    GError *error = NULL;
    gchar **items;
    gchar *dns[3] = { 0 };
    guint i;
    guint dns_i;

    response = mm_base_modem_at_command_full_finish (MM_BASE_MODEM (modem), res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        get_ip_config_context_complete_and_free (ctx);
        return;
    }

    /* TODO: use a regex to parse this */

    /* Check result */
    if (!g_str_has_prefix (response, IPDPADDR_TAG)) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get IP config: invalid response '%s'",
                                         response);
        get_ip_config_context_complete_and_free (ctx);
        return;
    }

    /* %IPDPADDR: <cid>,<ip>,<gw>,<dns1>,<dns2>[,<nbns1>,<nbns2>[,<??>,<netmask>,<gw>]]
     *
     * Sierra USB305: %IPDPADDR: 2, 21.93.217.11, 21.93.217.10, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0
     * K3805-Z: %IPDPADDR: 2, 21.93.217.11, 21.93.217.10, 10.177.0.34, 10.161.171.220, 0.0.0.0, 0.0.0.0, 255.0.0.0, 255.255.255.0, 21.93.217.10,
     */
    response = mm_strip_tag (response, IPDPADDR_TAG);
    items = g_strsplit (response, ", ", 0);

    ip_config = mm_bearer_ip_config_new ();

    for (i = 0, dns_i = 0; items[i]; i++) {
        if (i == 0) { /* CID */
            gint num;

            if (!mm_get_int_from_str (items[i], &num) ||
                num != ctx->cid) {
                error = g_error_new (MM_CORE_ERROR,
                                     MM_CORE_ERROR_FAILED,
                                     "Unknown CID in IPDPADDR response ("
                                     "got %d, expected %d)",
                                     (guint) num,
                                     ctx->cid);
                break;
            }
        } else if (i == 1) { /* IP address */
            guint32 tmp = 0;

            if (!inet_pton (AF_INET, items[i], &tmp)) {
                mm_warn ("Couldn't parse IP address '%s'", items[i]);
                g_clear_object (&ip_config);
                break;
            }

            mm_bearer_ip_config_set_method (ip_config, MM_BEARER_IP_METHOD_STATIC);
            mm_bearer_ip_config_set_address (ip_config,  items[i]);
        } else if (i == 2) { /* Gateway */
            guint32 tmp = 0;

            if (!inet_pton (AF_INET, items[i], &tmp)) {
                mm_warn ("Couldn't parse gateway address '%s'", items[i]);
                g_clear_object (&ip_config);
                break;
            }

            if (tmp)
                mm_bearer_ip_config_set_gateway (ip_config, items[i]);
        } else if (i == 3 || i == 4) { /* DNS entries */
            guint32 tmp = 0;

            if (!inet_pton (AF_INET, items[i], &tmp)) {
                mm_warn ("Couldn't parse DNS address '%s'", items[i]);
                g_clear_object (&ip_config);
                break;
            }

            if (tmp)
                dns[dns_i++] = items[i];
        } else if (i == 8) { /* Netmask */
            guint32 tmp = 0;

            if (!inet_pton (AF_INET, items[i], &tmp)) {
                mm_warn ("Couldn't parse netmask '%s'", items[i]);
                g_clear_object (&ip_config);
                break;
            }

            mm_bearer_ip_config_set_prefix (ip_config, mm_netmask_to_cidr (items[i]));
        } else if (i == 9) { /* Duplicate Gateway */
            if (!mm_bearer_ip_config_get_gateway (ip_config)) {
                guint32 tmp = 0;

                if (!inet_pton (AF_INET, items[i], &tmp)) {
                    mm_warn ("Couldn't parse (duplicate) gateway address '%s'", items[i]);
                    g_clear_object (&ip_config);
                    break;
                }

                if (tmp)
                    mm_bearer_ip_config_set_gateway (ip_config, items[i]);
            }
        }
    }

    if (!ip_config) {
        if (error)
            g_simple_async_result_take_error (ctx->result, error);
        else
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't get IP config: couldn't parse response '%s'",
                                             response);
    } else {
        /* If we got DNS entries, set them in the IP config */
        if (dns[0])
            mm_bearer_ip_config_set_dns (ip_config, (const gchar **)dns);

        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   ip_config,
                                                   (GDestroyNotify)g_object_unref);
    }

    get_ip_config_context_complete_and_free (ctx);
    g_strfreev (items);
}

static void
get_ip_config_3gpp (MMBroadbandBearer *self,
                    MMBroadbandModem *modem,
                    MMAtSerialPort *primary,
                    MMAtSerialPort *secondary,
                    MMPort *data,
                    guint cid,
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

        command = g_strdup_printf ("%%IPDPADDR=%d", cid);
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
        MMBearerIpConfig *ip_config;

        ip_config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (ip_config, MM_BEARER_IP_METHOD_DHCP);
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   ip_config,
                                                   (GDestroyNotify)g_object_unref);
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
    return FALSE;
}

static void
report_disconnect_status (MMBroadbandBearerIcera *self,
                          MMBroadbandBearerIceraConnectionStatus status)
{
    Disconnect3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->disconnect_pending;
    self->priv->disconnect_pending = NULL;

    /* Cleanup timeout, if any */
    if (self->priv->disconnect_pending_id) {
        g_source_remove (self->priv->disconnect_pending_id);
        self->priv->disconnect_pending_id = 0;
    }

    switch (status) {
    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_UNKNOWN:
        g_warn_if_reached ();
        break;

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_CONNECTED:
        if (!ctx)
            break;

        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Disconnection failed");
        disconnect_3gpp_context_complete_and_free (ctx);
        return;

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_CONNECTION_FAILED:
        if (!ctx)
            break;

        /* Well, this actually means disconnection, right? */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_DISCONNECTED:
        if (!ctx) {
            mm_dbg ("Received spontaneous %%IPDPACT disconnect");
            mm_bearer_report_disconnection (MM_BEARER (self));
            break;
        }

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }
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

    mm_base_modem_at_command_full_finish (MM_BASE_MODEM (modem), res, &error);
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
                 MMAtSerialPort *primary,
                 MMAtSerialPort *secondary,
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
    MMAtSerialPort *primary;
    guint cid;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    guint authentication_retries;
    GError *saved_error;
} Dial3gppContext;

static Dial3gppContext *
dial_3gpp_context_new (MMBroadbandBearerIcera *self,
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
    return ctx;
}

static void
dial_3gpp_context_complete_and_free (Dial3gppContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
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

    return FALSE;
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
    g_cancellable_disconnect (ctx->cancellable,
                              self->priv->connect_cancellable_id);
    self->priv->connect_cancellable_id = 0;

    /* Store cancelled error */
    g_assert (dial_3gpp_context_set_error_if_cancelled (ctx, &ctx->saved_error));

    /* We cannot reset right here, we need to wait for the connection
     * attempt to finish */
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
                       MMBroadbandBearerIceraConnectionStatus status)
{
    Dial3gppContext *ctx;

    /* Recover context and remove it from the private info */
    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;

    /* Cleanup cancellable and timeout, if any */
    if (self->priv->connect_pending_id) {
        g_source_remove (self->priv->connect_pending_id);
        self->priv->connect_pending_id = 0;
    }

    if (ctx && self->priv->connect_cancellable_id) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;
    }

    switch (status) {
    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_UNKNOWN:
        break;

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_CONNECTED:
        if (!ctx)
            /* We may get this if the timeout for the connection attempt is
             * reached before the unsolicited response. We should probably
             * keep the CID around to request explicit disconnection in this
             * case. */
            break;

        /* If we wanted to get cancelled before, do it now */
        if (ctx->saved_error) {
            /* Keep error */
            g_simple_async_result_take_error (ctx->result, ctx->saved_error);
            ctx->saved_error = NULL;
            /* Cancel connection */
            connect_reset (ctx);
            return;
        }

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        dial_3gpp_context_complete_and_free (ctx);
        return;

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_CONNECTION_FAILED:
        if (!ctx)
            break;

        /* If we wanted to get cancelled before and now we couldn't connect,
         * use the cancelled error and return */
        if (ctx->saved_error) {
            g_simple_async_result_take_error (ctx->result, ctx->saved_error);
            ctx->saved_error = NULL;
            dial_3gpp_context_complete_and_free (ctx);
            return;
        }

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

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_DISCONNECTED:
        if (ctx) {
            /* If we wanted to get cancelled before and now we couldn't connect,
             * use the cancelled error and return */
            if (ctx->saved_error) {
                g_simple_async_result_take_error (ctx->result, ctx->saved_error);
                ctx->saved_error = NULL;
                dial_3gpp_context_complete_and_free (ctx);
                return;
            }

            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Call setup failed");
            dial_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Just ensure we mark ourselves as being disconnected... */
        mm_bearer_report_disconnection (MM_BEARER (self));
        return;
    }

    g_warn_if_reached ();
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
}

static void
deactivate_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  Dial3gppContext *ctx)
{
    gchar *command;

    /*
     * Ignore any error here; %IPDPACT=ctx,0 will produce an error 767
     * if the context is not, in fact, connected. This is annoying but
     * harmless.
     */
    mm_base_modem_at_command_full_finish (modem, res, NULL);

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

static void authenticate (Dial3gppContext *ctx);

static gboolean
retry_authentication_cb (Dial3gppContext *ctx)
{
    authenticate (ctx);
    return FALSE;
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

    /*
     * Deactivate the context we want to use before we try to activate
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
authenticate (Dial3gppContext *ctx)
{
    gchar *command;
    const gchar *user;
    const gchar *password;
    MMBearerAllowedAuth allowed_auth;

    user = mm_bearer_properties_get_user (mm_bearer_peek_config (MM_BEARER (ctx->self)));
    password = mm_bearer_properties_get_password (mm_bearer_peek_config (MM_BEARER (ctx->self)));
    allowed_auth = mm_bearer_properties_get_allowed_auth (mm_bearer_peek_config (MM_BEARER (ctx->self)));

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

        quoted_user = mm_at_serial_port_quote_string (user);
        quoted_password = mm_at_serial_port_quote_string (password);
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
dial_3gpp (MMBroadbandBearer *self,
           MMBaseModem *modem,
           MMAtSerialPort *primary,
           MMPort *data, /* unused by us */
           guint cid,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    g_assert (primary != NULL);

    authenticate (dial_3gpp_context_new (MM_BROADBAND_BEARER_ICERA (self),
                                         modem,
                                         primary,
                                         cid,
                                         cancellable,
                                         callback,
                                         user_data));
}

/*****************************************************************************/

void
mm_broadband_bearer_icera_report_connection_status (MMBroadbandBearerIcera *self,
                                                    MMBroadbandBearerIceraConnectionStatus status)
{
    if (self->priv->connect_pending)
        report_connect_status (self, status);

    if (self->priv->disconnect_pending)
        report_disconnect_status (self, status);
}

/*****************************************************************************/

MMBearer *
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
    mm_bearer_export (MM_BEARER (bearer));

    return MM_BEARER (bearer);
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
        MM_BEARER_MODEM, modem,
        MM_BEARER_CONFIG, config,
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
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER_ICERA,
                                              MMBroadbandBearerIceraPrivate);

    /* Defaults */
    self->priv->default_ip_method = MM_BEARER_IP_METHOD_STATIC;
}

static void
mm_broadband_bearer_icera_class_init (MMBroadbandBearerIceraClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerIceraPrivate));

    object_class->get_property = get_property;
    object_class->set_property = set_property;
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
