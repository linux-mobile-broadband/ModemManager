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
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-serial-enums-types.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandBearer, mm_broadband_bearer, MM_TYPE_BEARER, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                               async_initable_iface_init));

typedef enum {
    CONNECTION_TYPE_NONE,
    CONNECTION_TYPE_3GPP,
    CONNECTION_TYPE_CDMA,
} ConnectionType;

struct _MMBroadbandBearerPrivate {
    /*-- Common stuff --*/
    /* Data port used when modem is connected */
    MMPort *port;
    /* Current connection type */
    ConnectionType connection_type;

    /*-- 3GPP specific --*/
    /* CID of the PDP context */
    guint cid;
};

/*****************************************************************************/

guint
mm_broadband_bearer_get_3gpp_cid (MMBroadbandBearer *self)
{
    return self->priv->cid;
}

/*****************************************************************************/
/* Detailed connect result, used in both CDMA and 3GPP sequences */
typedef struct {
    MMPort *data;
    MMBearerIpConfig *ipv4_config;
    MMBearerIpConfig *ipv6_config;
} DetailedConnectResult;

static void
detailed_connect_result_free (DetailedConnectResult *result)
{
    if (result->ipv4_config)
        g_object_unref (result->ipv4_config);
    if (result->ipv6_config)
        g_object_unref (result->ipv6_config);
    if (result->data)
        g_object_unref (result->data);
    g_slice_free (DetailedConnectResult, result);
}

static DetailedConnectResult *
detailed_connect_result_new (MMPort *data,
                             MMBearerIpConfig *ipv4_config,
                             MMBearerIpConfig *ipv6_config)
{
    DetailedConnectResult *result;

    result = g_slice_new0 (DetailedConnectResult);
    if (data)
        result->data = g_object_ref (data);
    if (ipv4_config)
        result->ipv4_config = g_object_ref (ipv4_config);
    if (ipv6_config)
        result->ipv6_config = g_object_ref (ipv6_config);
    return result;
}

/*****************************************************************************/
/* Detailed connect context, used in both CDMA and 3GPP sequences */
typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMPort *data;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;

    /* 3GPP-specific */
    guint cid;
    guint max_cid;
} DetailedConnectContext;

static gboolean
detailed_connect_finish (MMBroadbandBearer *self,
                         GAsyncResult *res,
                         MMPort **data,
                         MMBearerIpConfig **ipv4_config,
                         MMBearerIpConfig **ipv6_config,
                         GError **error)
{
    DetailedConnectResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    *data = (result->data ? g_object_ref (result->data) : NULL);
    *ipv4_config = (result->ipv4_config ? g_object_ref (result->ipv4_config) : NULL);
    *ipv6_config = (result->ipv6_config ? g_object_ref (result->ipv6_config) : NULL);
    return TRUE;
}

static void
detailed_connect_context_complete_and_free (DetailedConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static gboolean
detailed_connect_context_set_error_if_cancelled (DetailedConnectContext *ctx,
                                                 GError **error)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_CANCELLED,
                 "Connection setup operation has been cancelled");
    return TRUE;
}

static gboolean
detailed_connect_context_complete_and_free_if_cancelled (DetailedConnectContext *ctx)
{
    GError *error = NULL;

    if (!detailed_connect_context_set_error_if_cancelled (ctx, &error))
        return FALSE;

    g_simple_async_result_take_error (ctx->result, error);
    detailed_connect_context_complete_and_free (ctx);
    return TRUE;
}

static DetailedConnectContext *
detailed_connect_context_new (MMBroadbandBearer *self,
                              MMBroadbandModem *modem,
                              MMAtSerialPort *primary,
                              MMAtSerialPort *secondary,
                              MMPort *data,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    DetailedConnectContext *ctx;

    ctx = g_new0 (DetailedConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->secondary = (secondary ? g_object_ref (secondary) : NULL);
    ctx->data = g_object_ref (data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             detailed_connect_context_new);
    /* NOTE:
     * We don't currently support cancelling AT commands, so we'll just check
     * whether the operation is to be cancelled at each step. */
    ctx->cancellable = g_object_ref (cancellable);
    return ctx;
}

/*****************************************************************************/
/* CDMA CONNECT
 *
 * CDMA connection procedure of a bearer involves several steps:
 * 1) Get data port from the modem. Default implementation will have only
 *    one single possible data port, but plugins may have more.
 * 2) If requesting specific RM, load current.
 *  2.1) If current RM different to the requested one, set the new one.
 * 3) Initiate call.
 */

static void
dial_cdma_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 DetailedConnectContext *ctx)
{
    GError *error = NULL;
    MMBearerIpConfig *config;

    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_warn ("Couldn't connect: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* else... Yuhu! */

    /* Generic CDMA connections are done over PPP always */
    g_assert (MM_IS_AT_SERIAL_PORT (ctx->data));
    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config, MM_BEARER_IP_METHOD_PPP);

    /* Assume only IPv4 is given */
    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        detailed_connect_result_new (ctx->data, config, NULL),
        (GDestroyNotify)detailed_connect_result_free);
    detailed_connect_context_complete_and_free (ctx);

    g_object_unref (config);
}

static void
cdma_connect_context_dial (DetailedConnectContext *ctx)
{
    gchar *command;
    const gchar *number;

    number = mm_bearer_properties_get_number (mm_bearer_peek_config (MM_BEARER (ctx->self)));

    /* If a number was given when creating the bearer, use that one.
     * Otherwise, use the default one, #777
     */
    if (number)
        command = g_strconcat ("DT", number, NULL);
    else
        command = g_strdup ("DT#777");

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   90,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)dial_cdma_ready,
                                   ctx);
    g_free (command);
}

static void
set_rm_protocol_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       DetailedConnectContext *ctx)
{
    GError *error = NULL;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't set RM protocol: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

static void
current_rm_protocol_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           DetailedConnectContext *ctx)
{
    const gchar *result;
    GError *error = NULL;
    guint current_index;
    MMModemCdmaRmProtocol current_rm;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    result = mm_base_modem_at_command_full_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't query current RM protocol: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    result = mm_strip_tag (result, "+CRM:");
    current_index = (guint) atoi (result);
    current_rm = mm_cdma_get_rm_protocol_from_index (current_index, &error);
    if (error) {
        mm_warn ("Couldn't parse RM protocol reply (%s): '%s'",
                 result,
                 error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    if (current_rm != mm_bearer_properties_get_rm_protocol (mm_bearer_peek_config (MM_BEARER (self)))) {
        guint new_index;
        gchar *command;

        mm_dbg ("Setting requested RM protocol...");

        new_index = (mm_cdma_get_index_from_rm_protocol (
                         mm_bearer_properties_get_rm_protocol (mm_bearer_peek_config (MM_BEARER (self))),
                         &error));
        if (error) {
            mm_warn ("Cannot set RM protocol: '%s'",
                     error->message);
            g_simple_async_result_take_error (ctx->result, error);
            detailed_connect_context_complete_and_free (ctx);
            return;
        }

        command = g_strdup_printf ("+CRM=%u", new_index);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       3,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)set_rm_protocol_ready,
                                       ctx);
        g_free (command);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

static void
connect_cdma (MMBroadbandBearer *self,
              MMBroadbandModem *modem,
              MMAtSerialPort *primary,
              MMAtSerialPort *secondary, /* unused by us */
              MMPort *data,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    DetailedConnectContext *ctx;
    MMPort *real_data;

    g_assert (primary != NULL);

    if (MM_IS_AT_SERIAL_PORT (data))
        real_data = data;
    else {
        mm_dbg ("Ignoring 'net' interface in CDMA connection");
        real_data = MM_PORT (primary);
    }

    ctx = detailed_connect_context_new (self,
                                        modem,
                                        primary,
                                        NULL,
                                        real_data,
                                        cancellable,
                                        callback,
                                        user_data);

    if (mm_bearer_properties_get_rm_protocol (
            mm_bearer_peek_config (MM_BEARER (self))) !=
        MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
        /* Need to query current RM protocol */
        mm_dbg ("Querying current RM protocol set...");
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "+CRM?",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)current_rm_protocol_ready,
                                       ctx);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
    GError *saved_error;
} Dial3gppContext;

static Dial3gppContext *
dial_3gpp_context_new (MMBroadbandBearer *self,
                       MMBaseModem *modem,
                       MMAtSerialPort *primary,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    Dial3gppContext *ctx;

    ctx = g_new0 (Dial3gppContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
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
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
    g_object_unref (ctx->cancellable);
    g_simple_async_result_complete (ctx->result);
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
extended_error_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      Dial3gppContext *ctx)
{
    const gchar *result;

    /* If cancelled, complete */
    if (dial_3gpp_context_complete_and_free_if_cancelled (ctx))
        return;

    result = mm_base_modem_at_command_full_finish (modem, res, NULL);
    if (result &&
        g_str_has_prefix (result, "+CEER: ") &&
        strlen (result) > 7) {
        g_simple_async_result_set_error (ctx->result,
                                         ctx->saved_error->domain,
                                         ctx->saved_error->code,
                                         "%s", &result[7]);
        g_error_free (ctx->saved_error);
    } else
        g_simple_async_result_take_error (ctx->result,
                                          ctx->saved_error);

    ctx->saved_error = NULL;

    /* Done with errors */
    dial_3gpp_context_complete_and_free (ctx);
}

static void
atd_ready (MMBaseModem *modem,
           GAsyncResult *res,
           Dial3gppContext *ctx)
{
    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_full_finish (modem, res, &ctx->saved_error);

    if (ctx->saved_error) {
        /* Try to get more information why it failed */
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "+CEER",
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)extended_error_ready,
                                       ctx);
        return;
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    dial_3gpp_context_complete_and_free (ctx);
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
    gchar *command;
    Dial3gppContext *ctx;

    g_assert (primary != NULL);

    ctx = dial_3gpp_context_new (self,
                                 modem,
                                 primary,
                                 cancellable,
                                 callback,
                                 user_data);

    /* Use default *99 to connect */
    command = g_strdup_printf ("ATD*99***%d#", cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   60,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)atd_ready,
                                   ctx);
    g_free (command);
}

/*****************************************************************************/
/* 3GPP CONNECT
 *
 * 3GPP connection procedure of a bearer involves several steps:
 * 1) Get data port from the modem. Default implementation will have only
 *    one single possible data port, but plugins may have more.
 * 2) Decide which PDP context to use
 *   2.1) Look for an already existing PDP context with the same APN.
 *   2.2) If none found with the same APN, try to find a PDP context without any
 *        predefined APN.
 *   2.3) If none found, look for the highest available CID, and use that one.
 * 3) Activate PDP context.
 * 4) Initiate call.
 */

static void
get_ip_config_3gpp_ready (MMBroadbandModem *modem,
                          GAsyncResult *res,
                          DetailedConnectContext *ctx)
{
    MMBearerIpConfig *ipv4_config = NULL;
    MMBearerIpConfig *ipv6_config = NULL;
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->get_ip_config_3gpp_finish (ctx->self,
                                                                               res,
                                                                               &ipv4_config,
                                                                               &ipv6_config,
                                                                               &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        detailed_connect_result_new (ctx->data, ipv4_config, ipv6_config),
        (GDestroyNotify)detailed_connect_result_free);
    detailed_connect_context_complete_and_free (ctx);

    if (ipv4_config)
        g_object_unref (ipv4_config);
    if (ipv6_config)
        g_object_unref (ipv6_config);
}

static void
dial_3gpp_ready (MMBroadbandModem *modem,
                 GAsyncResult *res,
                 DetailedConnectContext *ctx)
{
    MMBearerIpConfig *config;
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->dial_3gpp_finish (ctx->self,
                                                                      res,
                                                                      &error)) {
        /* Clear CID when it failed to connect. */
        ctx->self->priv->cid = 0;
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    if (MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->get_ip_config_3gpp &&
        MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->get_ip_config_3gpp_finish) {
        /* Launch specific IP config retrieval */
        MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->get_ip_config_3gpp (
            ctx->self,
            MM_BROADBAND_MODEM (ctx->modem),
            ctx->primary,
            ctx->secondary,
            ctx->data,
            ctx->cid,
            (GAsyncReadyCallback)get_ip_config_3gpp_ready,
            ctx);
        return;
    }

    /* Yuhu! */

    /* If no specific IP retrieval requested, set the default implementation
     * (PPP if data port is AT, DHCP otherwise) */
    config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (config,
                                    (MM_IS_AT_SERIAL_PORT (ctx->data) ?
                                     MM_BEARER_IP_METHOD_PPP :
                                     MM_BEARER_IP_METHOD_DHCP));

    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        detailed_connect_result_new (ctx->data, config, NULL),
        (GDestroyNotify)detailed_connect_result_free);
    detailed_connect_context_complete_and_free (ctx);

    g_object_unref (config);
}

static void
initialize_pdp_context_ready (MMBaseModem *modem,
                              GAsyncResult *res,
                              DetailedConnectContext *ctx)
{
    GError *error = NULL;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        mm_warn ("Couldn't initialize PDP context with our APN: '%s'",
                 error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* Keep CID around after initializing the PDP context in order to
     * handle corresponding unsolicited PDP activation responses. */
    ctx->self->priv->cid = ctx->cid;
    MM_BROADBAND_BEARER_GET_CLASS (ctx->self)->dial_3gpp (ctx->self,
                                                          ctx->modem,
                                                          ctx->primary,
                                                          ctx->data,
                                                          ctx->cid,
                                                          ctx->cancellable,
                                                          (GAsyncReadyCallback)dial_3gpp_ready,
                                                          ctx);
}

static void
find_cid_ready (MMBaseModem *modem,
                GAsyncResult *res,
                DetailedConnectContext *ctx)
{
    GVariant *result;
    gchar *command;
    GError *error = NULL;
    const gchar *pdp_type;

    result = mm_base_modem_at_sequence_full_finish (modem, res, NULL, &error);
    if (!result) {
        mm_warn ("Couldn't find best CID to use: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* If cancelled, complete. Normally, we would get the cancellation error
     * already when finishing the sequence, but we may still get cancelled
     * between last command result parsing in the sequence and the ready(). */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    /* Initialize PDP context with our APN */
    pdp_type = mm_3gpp_get_pdp_type_from_ip_family (mm_bearer_properties_get_ip_type (mm_bearer_peek_config (MM_BEARER (ctx->self))));
    if (!pdp_type) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_INVALID_ARGS,
                                         "Invalid PDP type requested");
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    ctx->cid = g_variant_get_uint32 (result);
    command = g_strdup_printf ("+CGDCONT=%u,\"%s\",\"%s\"",
                               ctx->cid,
                               pdp_type,
                               mm_bearer_properties_get_apn (mm_bearer_peek_config (MM_BEARER (ctx->self))));
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)initialize_pdp_context_ready,
                                   ctx);
    g_free (command);
}

static gboolean
parse_cid_range (MMBaseModem *modem,
                 DetailedConnectContext *ctx,
                 const gchar *command,
                 const gchar *response,
                 gboolean last_command,
                 const GError *error,
                 GVariant **result,
                 GError **result_error)
{
    GError *inner_error = NULL;
    GRegex *r;
    GMatchInfo *match_info;
    guint cid = 0;

    /* If cancelled, set result error */
    if (detailed_connect_context_set_error_if_cancelled (ctx, result_error))
        return FALSE;

    if (error) {
        mm_dbg ("Unexpected +CGDCONT error: '%s'", error->message);
        mm_dbg ("Defaulting to CID=1");
        *result = g_variant_new_uint32 (1);
        return TRUE;
    }

    if (!g_str_has_prefix (response, "+CGDCONT:")) {
        mm_dbg ("Unexpected +CGDCONT response: '%s'", response);
        mm_dbg ("Defaulting to CID=1");
        *result = g_variant_new_uint32 (1);
        return TRUE;
    }

    r = g_regex_new ("\\+CGDCONT:\\s*\\((\\d+)-(\\d+)\\),\\(?\"(\\S+)\"",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, &inner_error);
    if (r) {
        g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
        cid = 0;
        while (!inner_error &&
               cid == 0 &&
               g_match_info_matches (match_info)) {
            gchar *pdp_type;

            pdp_type = g_match_info_fetch (match_info, 3);

            if (mm_3gpp_get_ip_family_from_pdp_type (pdp_type) ==
                mm_bearer_properties_get_ip_type (mm_bearer_peek_config (MM_BEARER (ctx->self)))) {
                gchar *max_cid_range_str;
                guint max_cid_range;

                max_cid_range_str = g_match_info_fetch (match_info, 2);
                max_cid_range = (guint)atoi (max_cid_range_str);

                if (ctx->max_cid < max_cid_range)
                    cid = ctx->max_cid + 1;
                else
                    cid = ctx->max_cid;

                g_free (max_cid_range_str);
            }

            g_free (pdp_type);
            g_match_info_next (match_info, &inner_error);
        }

        g_match_info_free (match_info);
        g_regex_unref (r);
    }

    if (inner_error) {
        mm_dbg ("Unexpected error matching +CGDCONT response: '%s'", inner_error->message);
        g_error_free (inner_error);
    }

    if (cid == 0) {
        mm_dbg ("Defaulting to CID=1");
        cid = 1;
    } else
        mm_dbg ("Using CID %u", cid);

    *result = g_variant_new_uint32 (cid);
    return TRUE;
}

static gboolean
parse_pdp_list (MMBaseModem *modem,
                DetailedConnectContext *ctx,
                const gchar *command,
                const gchar *response,
                gboolean last_command,
                const GError *error,
                GVariant **result,
                GError **result_error)
{
    GError *inner_error = NULL;
    GList *pdp_list;
    GList *l;
    guint cid;

    /* If cancelled, set result error */
    if (detailed_connect_context_set_error_if_cancelled (ctx, result_error))
        return FALSE;

    ctx->max_cid = 0;

    /* Some Android phones don't support querying existing PDP contexts,
     * but will accept setting the APN.  So if CGDCONT? isn't supported,
     * just ignore that error and hope for the best. (bgo #637327)
     */
    if (g_error_matches (error,
                         MM_MOBILE_EQUIPMENT_ERROR,
                         MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED)) {
        mm_dbg ("Querying PDP context list is unsupported");
        return FALSE;
    }

    pdp_list = mm_3gpp_parse_cgdcont_read_response (response, &inner_error);
    if (!pdp_list) {
        /* No predefined PDP contexts found */
        mm_dbg ("No PDP contexts found");
        return FALSE;
    }

    cid = 0;

    /* Show all found PDP contexts in debug log */
    mm_dbg ("Found '%u' PDP contexts", g_list_length (pdp_list));
    for (l = pdp_list; l; l = g_list_next (l)) {
        MM3gppPdpContext *pdp = l->data;

        mm_dbg ("  PDP context [cid=%u] [type='%s'] [apn='%s']",
                pdp->cid,
                mm_bearer_ip_family_get_string (pdp->pdp_type),
                pdp->apn ? pdp->apn : "");
    }

    /* Look for the exact PDP context we want */
    for (l = pdp_list; l; l = g_list_next (l)) {
        MM3gppPdpContext *pdp = l->data;

        if (pdp->pdp_type == mm_bearer_properties_get_ip_type (mm_bearer_peek_config (MM_BEARER (ctx->self)))) {
            /* PDP with no APN set? we may use that one if not exact match found */
            if (!pdp->apn || !pdp->apn[0]) {
                mm_dbg ("Found PDP context with CID %u and no APN",
                        pdp->cid);
                cid = pdp->cid;
            } else {
                const gchar *apn;

                apn = mm_bearer_properties_get_apn (mm_bearer_peek_config (MM_BEARER (ctx->self)));
                if (apn &&
                    g_str_equal (pdp->apn, apn)) {
                    /* Found a PDP context with the same CID and PDP type, we'll use it. */
                    mm_dbg ("Found PDP context with CID %u and PDP type %s for APN '%s'",
                            pdp->cid, mm_bearer_ip_family_get_string (pdp->pdp_type), pdp->apn);
                    cid = pdp->cid;
                    /* In this case, stop searching */
                    break;
                }
            }
        }

        if (ctx->max_cid < pdp->cid)
            ctx->max_cid = pdp->cid;
    }
    mm_3gpp_pdp_context_list_free (pdp_list);

    if (cid > 0) {
        *result = g_variant_new_uint32 (cid);
        return TRUE;
    }

    return FALSE;
}

static const MMBaseModemAtCommand find_cid_sequence[] = {
    { "+CGDCONT?",  3, FALSE, (MMBaseModemAtResponseProcessor)parse_pdp_list  },
    { "+CGDCONT=?", 3, TRUE,  (MMBaseModemAtResponseProcessor)parse_cid_range },
    { NULL }
};

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
    DetailedConnectContext *ctx;

    g_assert (primary != NULL);

    ctx = detailed_connect_context_new (self,
                                        modem,
                                        primary,
                                        secondary,
                                        data,
                                        cancellable,
                                        callback,
                                        user_data);

    mm_dbg ("Looking for best CID...");
    mm_base_modem_at_sequence_full (ctx->modem,
                                    ctx->primary,
                                    find_cid_sequence,
                                    ctx, /* also passed as response processor context */
                                    NULL, /* response_processor_context_free */
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback)find_cid_ready,
                                    ctx);
}

/*****************************************************************************/
/* CONNECT */

typedef struct {
    MMPort *data;
    MMBearerIpConfig *ipv4_config;
    MMBearerIpConfig *ipv6_config;
} ConnectResult;

static void
connect_result_free (ConnectResult *result)
{
    if (result->ipv4_config)
        g_object_unref (result->ipv4_config);
    if (result->ipv6_config)
        g_object_unref (result->ipv6_config);
    g_object_unref (result->data);
    g_free (result);
}

typedef struct {
    MMBroadbandBearer *self;
    GSimpleAsyncResult *result;
    MMPort *suggested_data;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->suggested_data);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
connect_finish (MMBearer *self,
                GAsyncResult *res,
                MMPort **data,
                MMBearerIpConfig **ipv4_config,
                MMBearerIpConfig **ipv6_config,
                GError **error)
{
    ConnectResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = (ConnectResult *) g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *data = MM_PORT (g_object_ref (result->data));
    *ipv4_config = (result->ipv4_config ? g_object_ref (result->ipv4_config) : NULL);
    *ipv6_config = (result->ipv6_config ? g_object_ref (result->ipv6_config) : NULL);

    return TRUE;
}

static void
connect_succeeded (ConnectContext *ctx,
                   ConnectionType connection_type,
                   MMPort *data,
                   MMBearerIpConfig *ipv4_config,
                   MMBearerIpConfig *ipv6_config)
{
    ConnectResult *result;
    MMPort *real_data;

    if (data) {
        if (data != ctx->suggested_data)
            mm_dbg ("Suggested to use port '%s/%s' for connection, but using '%s/%s' instead",
                    mm_port_subsys_get_string (mm_port_get_subsys (ctx->suggested_data)),
                    mm_port_get_device (ctx->suggested_data),
                    mm_port_subsys_get_string (mm_port_get_subsys (data)),
                    mm_port_get_device (data));
        real_data = data;
    } else
        real_data = g_object_ref (ctx->suggested_data);

    /* Port is connected; update the state */
    mm_port_set_connected (real_data, TRUE);

    /* Keep connected port and type of connection */
    ctx->self->priv->port = g_object_ref (real_data);
    ctx->self->priv->connection_type = connection_type;

    /* Build result */
    result = g_new0 (ConnectResult, 1);
    result->data = real_data;
    result->ipv4_config = ipv4_config;
    result->ipv6_config = ipv6_config;

    /* Set operation result */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               result,
                                               (GDestroyNotify)connect_result_free);

    connect_context_complete_and_free (ctx);
}

static void
connect_failed (ConnectContext *ctx,
                GError *error)
{
    /* On errors, close the data port */
    if (MM_IS_AT_SERIAL_PORT (ctx->suggested_data))
        mm_serial_port_close (MM_SERIAL_PORT (ctx->suggested_data));

    g_simple_async_result_take_error (ctx->result, error);
    connect_context_complete_and_free (ctx);
}

static void
connect_cdma_ready (MMBroadbandBearer *self,
                    GAsyncResult *res,
                    ConnectContext *ctx)
{
    GError *error = NULL;
    MMBearerIpConfig *ipv4_config = NULL;
    MMBearerIpConfig *ipv6_config = NULL;
    MMPort *data = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->connect_cdma_finish (self,
                                                                    res,
                                                                    &data,
                                                                    &ipv4_config,
                                                                    &ipv6_config,
                                                                    &error))
        connect_failed (ctx, error);
    else
        connect_succeeded (ctx, CONNECTION_TYPE_CDMA, data, ipv4_config, ipv6_config);
}

static void
connect_3gpp_ready (MMBroadbandBearer *self,
                    GAsyncResult *res,
                    ConnectContext *ctx)
{
    GError *error = NULL;
    MMBearerIpConfig *ipv4_config = NULL;
    MMBearerIpConfig *ipv6_config = NULL;
    MMPort *data = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->connect_3gpp_finish (self,
                                                                    res,
                                                                    &data,
                                                                    &ipv4_config,
                                                                    &ipv6_config,
                                                                    &error))
        connect_failed (ctx, error);
    else
        connect_succeeded (ctx, CONNECTION_TYPE_3GPP, data, ipv4_config, ipv6_config);
}

static void
connect (MMBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    MMBaseModem *modem = NULL;
    MMAtSerialPort *primary;
    MMPort *suggested_data;
    ConnectContext *ctx;

    /* Don't try to connect if already connected */
    if (MM_BROADBAND_BEARER (self)->priv->port) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: this bearer is already connected");
        return;
    }

    /* Get the owner modem object */
    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);

    /* We will launch the ATD call in the primary port... */
    primary = mm_base_modem_peek_port_primary (modem);
    if (!primary) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: couldn't get primary port");
        g_object_unref (modem);
        return;
    }

    /* ...only if not already connected */
    if (mm_port_get_connected (MM_PORT (primary))) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: primary AT port is already connected");
        g_object_unref (modem);
        return;
    }

    /* Look for best data port, NULL if none available. */
    suggested_data = mm_base_modem_peek_best_data_port (modem);
    if (!suggested_data) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: all available data ports already connected");
        g_object_unref (modem);
        return;
    }

    /* If data port is AT, we need to ensure it's open during the whole
     * connection. For the case where the primary port is used as data port,
     * which is actually always right now, this is already ensured because the
     * primary port is kept open as long as the modem is enabled, but anyway
     * there's no real problem in keeping an open count here as well. */
    if (MM_IS_AT_SERIAL_PORT (suggested_data)) {
        GError *error = NULL;

        if (!mm_serial_port_open (MM_SERIAL_PORT (suggested_data), &error)) {
            g_prefix_error (&error, "Couldn't connect: cannot keep data port open.");
            g_simple_async_report_take_gerror_in_idle (
                G_OBJECT (self),
                callback,
                user_data,
                error);
            g_object_unref (modem);
            return;
        }
    }

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_new0 (ConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->suggested_data = g_object_ref (suggested_data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    /* If the modem has 3GPP capabilities, launch 3GPP-based connection */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (modem))) {
        MM_BROADBAND_BEARER_GET_CLASS (self)->connect_3gpp (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            suggested_data,
            cancellable,
            (GAsyncReadyCallback) connect_3gpp_ready,
            ctx);
        g_object_unref (modem);
        return;
    }

    /* Otherwise, launch CDMA-specific connection */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (modem))) {
        MM_BROADBAND_BEARER_GET_CLASS (self)->connect_cdma (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            suggested_data,
            cancellable,
            (GAsyncReadyCallback) connect_cdma_ready,
            ctx);
        g_object_unref (modem);
        return;
    }

    g_assert_not_reached ();
}

/*****************************************************************************/
/* Detailed disconnect context, used in both CDMA and 3GPP sequences */

typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMPort *data;
    GSimpleAsyncResult *result;

    /* 3GPP-specific */
    gchar *cgact_command;
    gboolean cgact_sent;
} DetailedDisconnectContext;

static gboolean
detailed_disconnect_finish (MMBroadbandBearer *self,
                            GAsyncResult *res,
                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
detailed_disconnect_context_complete_and_free (DetailedDisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->cgact_command)
        g_free (ctx->cgact_command);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static DetailedDisconnectContext *
detailed_disconnect_context_new (MMBroadbandBearer *self,
                                 MMBroadbandModem *modem,
                                 MMAtSerialPort *primary,
                                 MMAtSerialPort *secondary,
                                 MMPort *data,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    DetailedDisconnectContext *ctx;

    ctx = g_new0 (DetailedDisconnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->secondary = (secondary ? g_object_ref (secondary) : NULL);
    ctx->data = g_object_ref (data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             detailed_disconnect_context_new);
    return ctx;
}

/*****************************************************************************/
/* CDMA DISCONNECT */

static void
primary_flash_cdma_ready (MMSerialPort *port,
                          GError *error,
                          DetailedDisconnectContext *ctx)
{
    if (error) {
        /* Ignore "NO CARRIER" response when modem disconnects and any flash
         * failures we might encounter. Other errors are hard errors.
         */
        if (!g_error_matches (error,
                              MM_CONNECTION_ERROR,
                              MM_CONNECTION_ERROR_NO_CARRIER) &&
            !g_error_matches (error,
                              MM_SERIAL_ERROR,
                              MM_SERIAL_ERROR_FLASH_FAILED)) {
            /* Fatal */
            g_simple_async_result_set_from_error (ctx->result, error);
            detailed_disconnect_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Port flashing failed (not fatal): %s", error->message);
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    detailed_disconnect_context_complete_and_free (ctx);
}

static void
disconnect_cdma (MMBroadbandBearer *self,
                 MMBroadbandModem *modem,
                 MMAtSerialPort *primary,
                 MMAtSerialPort *secondary,
                 MMPort *data,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    DetailedDisconnectContext *ctx;

    g_assert (primary != NULL);

    ctx = detailed_disconnect_context_new (self,
                                           modem,
                                           primary,
                                           secondary,
                                           data,
                                           callback,
                                           user_data);

    /* Just flash the primary port */
    mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_cdma_ready,
                          ctx);
}

/*****************************************************************************/
/* 3GPP DISCONNECT */

static void
cgact_primary_ready (MMBaseModem *modem,
                     GAsyncResult *res,
                     DetailedDisconnectContext *ctx)
{

    GError *error = NULL;

    /* Ignore errors for now */
    mm_base_modem_at_command_full_finish (MM_BASE_MODEM (modem), res, &error);
    if (error) {
        mm_dbg ("PDP context deactivation failed (not fatal): %s", error->message);
        g_error_free (error);
    }

    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    detailed_disconnect_context_complete_and_free (ctx);
}

static void
primary_flash_3gpp_ready (MMSerialPort *port,
                          GError *error,
                          DetailedDisconnectContext *ctx)
{
    if (error) {
        /* Ignore "NO CARRIER" response when modem disconnects and any flash
         * failures we might encounter. Other errors are hard errors.
         */
        if (!g_error_matches (error,
                              MM_CONNECTION_ERROR,
                              MM_CONNECTION_ERROR_NO_CARRIER) &&
            !g_error_matches (error,
                              MM_SERIAL_ERROR,
                              MM_SERIAL_ERROR_FLASH_FAILED)) {
            /* Fatal */
            g_simple_async_result_set_from_error (ctx->result, error);
            detailed_disconnect_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Port flashing failed (not fatal): %s", error->message);
    }

    /* Don't bother doing the CGACT again if it was done on a secondary port
     * or if not needed */
    if (ctx->cgact_sent) {
        mm_dbg ("PDP disconnection already sent in secondary port");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        detailed_disconnect_context_complete_and_free (ctx);
        return;
    }

    /* We don't want to try to send CGACT to the still connected port, so
     * if the primary AT port is actually the data port, set it as
     * disconnected here already. */
    if ((gpointer)ctx->primary == (gpointer)ctx->data)
        /* Port is disconnected; update the state */
        mm_port_set_connected (ctx->data, FALSE);

    mm_dbg ("Sending PDP context deactivation in primary port...");
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   ctx->cgact_command,
                                   3,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)cgact_primary_ready,
                                   ctx);
}

static void
cgact_secondary_ready (MMBaseModem *modem,
                       GAsyncResult *res,
                       DetailedDisconnectContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_full_finish (MM_BASE_MODEM (modem), res, &error);
    if (!error)
        ctx->cgact_sent = TRUE;
    else
        g_error_free (error);

    mm_dbg ("Flash primary port...");
    mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_3gpp_ready,
                          ctx);
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
    DetailedDisconnectContext *ctx;

    g_assert (primary != NULL);

    ctx = detailed_disconnect_context_new (self,
                                           modem,
                                           primary,
                                           secondary,
                                           data,
                                           callback,
                                           user_data);

    /* If no specific CID was used, disable all PDP contexts */
    ctx->cgact_command = (cid >= 0 ?
                          g_strdup_printf ("+CGACT=0,%d", cid) :
                          g_strdup_printf ("+CGACT=0"));

    /* If the primary port is connected (with PPP) then try sending the PDP
     * context deactivation on the secondary port because not all modems will
     * respond to flashing (since either the modem or the kernel's serial
     * driver doesn't support it).
     */
    if (ctx->secondary &&
        mm_port_get_connected (MM_PORT (ctx->primary))) {
        mm_dbg ("Sending PDP context deactivation in secondary port...");
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->secondary,
                                       ctx->cgact_command,
                                       3,
                                       FALSE,
                                       FALSE, /* raw */
                                       NULL, /* cancellable */
                                       (GAsyncReadyCallback)cgact_secondary_ready,
                                       ctx);
        return;
    }

    /* If no secondary port, go on to flash the primary port */
    mm_dbg ("Flash primary port...");
    mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_3gpp_ready,
                          ctx);
}

/*****************************************************************************/
/* DISCONNECT */

typedef struct {
    MMBroadbandBearer *self;
    GSimpleAsyncResult *result;
    MMPort *data;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
disconnect_finish (MMBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
reset_bearer_connection (MMBroadbandBearer *self)
{
    if (self->priv->port) {
        /* If properly disconnected, close the data port */
        if (MM_IS_AT_SERIAL_PORT (self->priv->port))
            mm_serial_port_close (MM_SERIAL_PORT (self->priv->port));

        /* Port is disconnected; update the state. Note: implementations may
         * already have set the port as disconnected (e.g the 3GPP one) */
        mm_port_set_connected (self->priv->port, FALSE);

        /* Clear data port */
        g_clear_object (&self->priv->port);
    }

    /* Reset current connection type */
    self->priv->connection_type = CONNECTION_TYPE_NONE;
}

static void
disconnect_succeeded (DisconnectContext *ctx)
{
    /* Cleanup all connection related data */
    reset_bearer_connection (ctx->self);

    /* Set operation result */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    disconnect_context_complete_and_free (ctx);
}

static void
disconnect_failed (DisconnectContext *ctx,
                   GError *error)
{
    g_simple_async_result_take_error (ctx->result, error);
    disconnect_context_complete_and_free (ctx);
}

static void
disconnect_cdma_ready (MMBroadbandBearer *self,
                       GAsyncResult *res,
                       DisconnectContext *ctx)
{
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_cdma_finish (self,
                                                                       res,
                                                                       &error))
        disconnect_failed (ctx, error);
    else
        disconnect_succeeded (ctx);
}

static void
disconnect_3gpp_ready (MMBroadbandBearer *self,
                       GAsyncResult *res,
                       DisconnectContext *ctx)
{
    GError *error = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp_finish (self,
                                                                       res,
                                                                       &error))
        disconnect_failed (ctx, error);
    else {
        /* Clear CID if we got any set */
        if (ctx->self->priv->cid)
            ctx->self->priv->cid = 0;
        disconnect_succeeded (ctx);
    }
}

static void
disconnect (MMBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    MMAtSerialPort *primary;
    MMBaseModem *modem = NULL;
    DisconnectContext *ctx;

    if (!MM_BROADBAND_BEARER (self)->priv->port) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect: this bearer is not connected");
        return;
    }

    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);

    /* We need the primary port to disconnect... */
    primary = mm_base_modem_peek_port_primary (modem);
    if (!primary) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect: couldn't get primary port");
        g_object_unref (modem);
        return;
    }

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_new0 (DisconnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->data = g_object_ref (MM_BROADBAND_BEARER (self)->priv->port);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect);

    switch (MM_BROADBAND_BEARER (self)->priv->connection_type) {
    case CONNECTION_TYPE_3GPP:
            MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_3gpp (
                MM_BROADBAND_BEARER (self),
                MM_BROADBAND_MODEM (modem),
                primary,
                mm_base_modem_peek_port_secondary (modem),
                MM_BROADBAND_BEARER (self)->priv->port,
                MM_BROADBAND_BEARER (self)->priv->cid,
                (GAsyncReadyCallback) disconnect_3gpp_ready,
                ctx);
        break;

    case CONNECTION_TYPE_CDMA:
        MM_BROADBAND_BEARER_GET_CLASS (self)->disconnect_cdma (
            MM_BROADBAND_BEARER (self),
            MM_BROADBAND_MODEM (modem),
            primary,
            mm_base_modem_peek_port_secondary (modem),
            MM_BROADBAND_BEARER (self)->priv->port,
            (GAsyncReadyCallback) disconnect_cdma_ready,
            ctx);
        break;

    case CONNECTION_TYPE_NONE:
        g_assert_not_reached ();
    }

    g_object_unref (modem);
}

/*****************************************************************************/

static void
report_disconnection (MMBearer *self)
{
    /* Cleanup all connection related data */
    reset_bearer_connection (MM_BROADBAND_BEARER (self));

    /* Chain up parent's report_disconection() */
    MM_BEARER_CLASS (mm_broadband_bearer_parent_class)->report_disconnection (self);
}

/*****************************************************************************/

typedef struct _InitAsyncContext InitAsyncContext;
static void interface_initialization_step (InitAsyncContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CDMA_RM_PROTOCOL,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitAsyncContext {
    MMBroadbandBearer *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMBaseModem *modem;
    InitializationStep step;
    MMAtSerialPort *port;
};

static void
init_async_context_free (InitAsyncContext *ctx,
                         gboolean close_port)
{
    if (ctx->port) {
        if (close_port)
            mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
        g_object_unref (ctx->port);
    }
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx);
}

MMBearer *
mm_broadband_bearer_new_finish (GAsyncResult *res,
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

static gboolean
initable_init_finish (GAsyncInitable  *initable,
                      GAsyncResult    *result,
                      GError         **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void
crm_range_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 InitAsyncContext *ctx)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        /* We should possibly take this error as fatal. If we were told to use a
         * specific Rm protocol, we must be able to check if it is supported. */
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        MMModemCdmaRmProtocol min = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
        MMModemCdmaRmProtocol max = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;

        if (mm_cdma_parse_crm_test_response (response,
                                             &min, &max,
                                             &error)) {
            MMModemCdmaRmProtocol current;

            current = mm_bearer_properties_get_rm_protocol (mm_bearer_peek_config (MM_BEARER (ctx->self)));
            /* Check if value within the range */
            if (current >= min &&
                current <= max) {
                /* Fine, go on with next step */
                ctx->step++;
                interface_initialization_step (ctx);
            }

            g_assert (error == NULL);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested RM protocol '%s' is not supported",
                                 mm_modem_cdma_rm_protocol_get_string (current));
        }

        /* Failed, set as fatal as well */
        g_simple_async_result_take_error (ctx->result, error);
    }

    g_simple_async_result_complete (ctx->result);
    init_async_context_free (ctx, TRUE);
}

static void
interface_initialization_step (InitAsyncContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CDMA_RM_PROTOCOL:
        /* If a specific RM protocol is given, we need to check whether it is
         * supported. */
        if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->modem)) &&
            mm_bearer_properties_get_rm_protocol (
                mm_bearer_peek_config (MM_BEARER (ctx->self))) != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
            mm_base_modem_at_command_full (ctx->modem,
                                           ctx->port,
                                           "+CRM=?",
                                           3,
                                           TRUE, /* getting range, so reply can be cached */
                                           FALSE, /* raw */
                                           NULL, /* cancellable */
                                           (GAsyncReadyCallback)crm_range_ready,
                                           ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, TRUE);
        return;
    }

    g_assert_not_reached ();
}

static void
initable_init_async (GAsyncInitable *initable,
                     int io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    InitAsyncContext *ctx;
    GError *error = NULL;

    ctx = g_new0 (InitAsyncContext, 1);
    ctx->self = g_object_ref (initable);
    ctx->result = g_simple_async_result_new (G_OBJECT (initable),
                                             callback,
                                             user_data,
                                             initable_init_async);
    ctx->cancellable = (cancellable ?
                        g_object_ref (cancellable) :
                        NULL);

    g_object_get (initable,
                  MM_BEARER_MODEM, &ctx->modem,
                  NULL);

    ctx->port = mm_base_modem_get_port_primary (ctx->modem);
    if (!ctx->port) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get primary port");
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, FALSE);
        return;
    }

    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, FALSE);
        return;
    }

    interface_initialization_step (ctx);
}

void
mm_broadband_bearer_new (MMBroadbandModem *modem,
                         MMBearerProperties *properties,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM,  modem,
        MM_BEARER_CONFIG, properties,
        NULL);
}

static void
mm_broadband_bearer_init (MMBroadbandBearer *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER,
                                              MMBroadbandBearerPrivate);

    /* Set defaults */
    self->priv->connection_type = CONNECTION_TYPE_NONE;
}

static void
dispose (GObject *object)
{
    MMBroadbandBearer *self = MM_BROADBAND_BEARER (object);

    reset_bearer_connection (self);

    G_OBJECT_CLASS (mm_broadband_bearer_parent_class)->dispose (object);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
    iface->init_async = initable_init_async;
    iface->init_finish = initable_init_finish;
}

static void
mm_broadband_bearer_class_init (MMBroadbandBearerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerPrivate));

    /* Virtual methods */
    object_class->dispose = dispose;

    bearer_class->connect = connect;
    bearer_class->connect_finish = connect_finish;
    bearer_class->disconnect = disconnect;
    bearer_class->disconnect_finish = disconnect_finish;
    bearer_class->report_disconnection = report_disconnection;

    klass->connect_3gpp = connect_3gpp;
    klass->connect_3gpp_finish = detailed_connect_finish;
    klass->dial_3gpp = dial_3gpp;
    klass->dial_3gpp_finish = dial_3gpp_finish;

    klass->connect_cdma = connect_cdma;
    klass->connect_cdma_finish = detailed_connect_finish;

    klass->disconnect_3gpp = disconnect_3gpp;
    klass->disconnect_3gpp_finish = detailed_disconnect_finish;
    klass->disconnect_cdma = disconnect_cdma;
    klass->disconnect_cdma_finish = detailed_disconnect_finish;
}
