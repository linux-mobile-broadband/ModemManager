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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-bearer-3gpp.h"
#include "mm-base-modem-at.h"
#include "mm-utils.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMBearer3gpp, mm_bearer_3gpp, MM_TYPE_BEARER);

enum {
    PROP_0,
    PROP_APN,
    PROP_IP_TYPE,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBearer3gppPrivate {
    /* APN of the PDP context */
    gchar *apn;
    /* IP type of the PDP context */
    gchar *ip_type;

    /* Data port used when modem is connected */
    MMPort *port;
    /* CID of the PDP context */
    guint cid;
};

/*****************************************************************************/

const gchar *
mm_bearer_3gpp_get_apn (MMBearer3gpp *self)
{
    return self->priv->apn;
}

const gchar *
mm_bearer_3gpp_get_ip_type (MMBearer3gpp *self)
{
    return self->priv->ip_type;
}

/*****************************************************************************/
/* CONNECT
 *
 * Connection procedure of a 3GPP bearer involves several steps:
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

typedef struct {
    MMBearer *bearer;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMPort *data;
    guint cid;
    guint max_cid;
    GSimpleAsyncResult *result;
    GError *error;
    GCancellable *cancellable;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    if (ctx->error) {
        /* On errors, close the data port */
        if (MM_IS_AT_SERIAL_PORT (ctx->data))
            mm_serial_port_close (MM_SERIAL_PORT (ctx->data));

        g_simple_async_result_take_error (ctx->result, ctx->error);
    } else {
        GVariant *ip_config;
        GVariantBuilder builder;
        MMBearerIpMethod ip_method;

        /* Port is connected; update the state */
        mm_port_set_connected (ctx->data, TRUE);
        mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (ctx->bearer),
                                       TRUE);
        mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (ctx->bearer),
                                       mm_port_get_device (ctx->data));

        /* If serial port, set PPP method */
        ip_method = (MM_IS_AT_SERIAL_PORT (ctx->data) ?
                     MM_BEARER_IP_METHOD_PPP :
                     MM_BEARER_IP_METHOD_DHCP);

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
        g_variant_builder_add (&builder, "{sv}", "method", g_variant_new_uint32 (ip_method));
        ip_config = g_variant_builder_end (&builder);
        mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (ctx->bearer), ip_config);
        mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (ctx->bearer), ip_config);

        /* Keep data port and CID around while connected */
        MM_BEARER_3GPP (ctx->bearer)->priv->cid = ctx->cid;
        MM_BEARER_3GPP (ctx->bearer)->priv->port = g_object_ref (ctx->data);

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    g_simple_async_result_complete_in_idle (ctx->result);

    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->bearer);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->result);
    g_free (ctx);
}

static gboolean
connect_context_set_error_if_cancelled (ConnectContext *ctx,
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
connect_finish (MMBearer *self,
                GAsyncResult *res,
                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
connect_report_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      ConnectContext *ctx)
{
    const gchar *result;

    /* If cancelled, complete */
    if (connect_context_set_error_if_cancelled (ctx, &ctx->error)) {
        connect_context_complete_and_free (ctx);
        return;
    }

    result = mm_base_modem_at_command_finish (modem, res, NULL);
    if (result &&
        g_str_has_prefix (result, "+CEER: ") &&
        strlen (result) > 7) {
        GError *rebuilt;

        rebuilt = g_error_new (ctx->error->domain,
                               ctx->error->code,
                               "%s", &result[7]);
        g_error_free (ctx->error);
        ctx->error = rebuilt;
    }

    /* Done with errors */
    connect_context_complete_and_free (ctx);
}

static void
connect_ready (MMBaseModem *modem,
               GAsyncResult *res,
               ConnectContext *ctx)
{
    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_finish (modem, res, &(ctx->error));
    if (ctx->error) {
        /* Try to get more information why it failed */
        mm_base_modem_at_command_in_port (
            modem,
            ctx->primary,
            "+CEER",
            3,
            FALSE,
            NULL, /* cancellable */
            (GAsyncReadyCallback)connect_report_ready,
            ctx);
        return;
    }

    /* Yuhu! */
    connect_context_complete_and_free (ctx);
}

static void
initialize_pdp_context_ready (MMBaseModem *self,
                              GAsyncResult *res,
                              ConnectContext *ctx)
{
    gchar *command;

    /* If cancelled, complete */
    if (connect_context_set_error_if_cancelled (ctx, &ctx->error)) {
        connect_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command_finish (self, res, &(ctx->error));
    if (ctx->error) {
        mm_warn ("Couldn't initialize PDP context with our APN: '%s'", ctx->error->message);
        connect_context_complete_and_free (ctx);
        return;
    }

    /* Use default *99 to connect */
    command = g_strdup_printf ("ATD*99***%d#", ctx->cid);
    mm_base_modem_at_command_in_port (
        ctx->modem,
        ctx->primary,
        command,
        60,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)connect_ready,
        ctx);
    g_free (command);
}

static void
find_cid_ready (MMBaseModem *self,
                GAsyncResult *res,
                ConnectContext *ctx)
{
    GVariant *result;
    gchar *command;
    GError *error = NULL;

    result = mm_base_modem_at_sequence_finish (self, res, NULL, &error);
    if (!result) {
        mm_warn ("Couldn't find best CID to use: '%s'", error->message);
        ctx->error = error;
        connect_context_complete_and_free (ctx);
        return;
    }

    /* If cancelled, complete. Normally, we would get the cancellation error
     * already when finishing the sequence, but we may still get cancelled
     * between last command result parsing in the sequence and the ready(). */
    if (connect_context_set_error_if_cancelled (ctx, &ctx->error)) {
        connect_context_complete_and_free (ctx);
        return;
    }

    /* Initialize PDP context with our APN */
    ctx->cid = g_variant_get_uint32 (result);
    command = g_strdup_printf ("+CGDCONT=%u,\"IP\",\"%s\"",
                               ctx->cid,
                               MM_BEARER_3GPP (ctx->bearer)->priv->apn);
    mm_base_modem_at_command_in_port (
        ctx->modem,
        ctx->primary,
        command,
        3,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)initialize_pdp_context_ready,
        ctx);
    g_free (command);
}

static gboolean
parse_cid_range (MMBaseModem *self,
                 ConnectContext *ctx,
                 const gchar *command,
                 const gchar *response,
                 const GError *error,
                 GVariant **result,
                 GError **result_error)
{
    GError *inner_error = NULL;
    GRegex *r;
    GMatchInfo *match_info;
    guint cid = 0;

    /* If cancelled, set result error */
    if (connect_context_set_error_if_cancelled (ctx, result_error))
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

            /* TODO: What about PDP contexts of type "IPV6"? */
            if (g_str_equal (pdp_type, "IP")) {
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
parse_pdp_list (MMBaseModem *self,
                ConnectContext *ctx,
                const gchar *command,
                const gchar *response,
                const GError *error,
                GVariant **result,
                GError **result_error)
{
    GError *inner_error = NULL;
    GList *pdp_list;
    GList *l;
    guint cid;

    /* If cancelled, set result error */
    if (connect_context_set_error_if_cancelled (ctx, result_error))
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

    pdp_list = mm_3gpp_parse_pdp_query_response (response, &inner_error);
    if (!pdp_list) {
        /* No predefined PDP contexts found */
        mm_dbg ("No PDP contexts found");
        return FALSE;
    }

    cid = 0;
    mm_dbg ("Found '%u' PDP contexts", g_list_length (pdp_list));
    for (l = pdp_list; l; l = g_list_next (l)) {
        MM3gppPdpContext *pdp = l->data;

        mm_dbg ("  PDP context [cid=%u] [type='%s'] [apn='%s']",
                pdp->cid,
                pdp->pdp_type ? pdp->pdp_type : "",
                pdp->apn ? pdp->apn : "");
        if (g_str_equal (pdp->pdp_type, "IP")) {
            /* PDP with no APN set? we may use that one if not exact match found */
            if (!pdp->apn || !pdp->apn[0]) {
                mm_dbg ("Found PDP context with CID %u and no APN",
                        pdp->cid);
                cid = pdp->cid;
            } else if (g_str_equal (pdp->apn, MM_BEARER_3GPP (ctx->bearer)->priv->apn)) {
                /* Found a PDP context with the same CID, we'll use it. */
                mm_dbg ("Found PDP context with CID %u for APN '%s'",
                        pdp->cid, pdp->apn);
                cid = pdp->cid;
                /* In this case, stop searching */
                break;
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
connect (MMBearer *self,
         const gchar *number,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    ConnectContext *ctx;
    MMAtSerialPort *primary;
    MMPort *data;
    MMBaseModem *modem = NULL;

    if (number && number[0])
        mm_warn ("Ignoring number to use when connecting 3GPP bearer: '%s'", number);

    if (MM_BEARER_3GPP (self)->priv->port) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: this bearer is already connected");
        return;
    }

    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);

    /* We will launch the ATD call in the primary port */
    primary = mm_base_modem_get_port_primary (modem);
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
    data = mm_base_modem_get_best_data_port (modem);
    if (!data) {
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
     if (MM_IS_AT_SERIAL_PORT (data)) {
         GError *error = NULL;

         if (!mm_serial_port_open (MM_SERIAL_PORT (data), &error)) {
             g_prefix_error (&error, "Couldn't connect: cannot keep data port open.");
             g_simple_async_report_take_gerror_in_idle (
                 G_OBJECT (self),
                 callback,
                 user_data,
                 error);
             g_object_unref (modem);
             g_object_unref (data);
             return;
         }
    }

    ctx = g_new0 (ConnectContext, 1);
    ctx->primary = g_object_ref (primary);
    ctx->data = g_object_ref (data);
    ctx->bearer = g_object_ref (self);
    ctx->modem = modem;

    /* NOTE:
     * We don't currently support cancelling AT commands, so we'll just check
     * whether the operation is to be cancelled at each step. */
    ctx->cancellable = g_object_ref (cancellable);

    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    mm_dbg ("Looking for best CID...");
    mm_base_modem_at_sequence_in_port (
        ctx->modem,
        ctx->primary,
        find_cid_sequence,
        ctx, /* also passed as response processor context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)find_cid_ready,
        ctx);
}

/*****************************************************************************/
/* DISCONNECT */

typedef struct {
    MMBearer *bearer;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMPort *data;
    gchar *cgact_command;
    gboolean cgact_sent;
    GSimpleAsyncResult *result;
    GError *error;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    if (ctx->error) {
        g_simple_async_result_take_error (ctx->result, ctx->error);
    } else {
        /* If properly disconnected, close the data port */
        if (MM_IS_AT_SERIAL_PORT (ctx->data))
            mm_serial_port_close (MM_SERIAL_PORT (ctx->data));

        /* Port is disconnected; update the state */
        mm_port_set_connected (ctx->data, FALSE);
        mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (ctx->bearer), FALSE);
        mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (ctx->bearer), NULL);
        mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (ctx->bearer), NULL);
        mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (ctx->bearer), NULL);
        /* Clear data port and CID */
        MM_BEARER_3GPP (ctx->bearer)->priv->cid = 0;
        g_clear_object (&(MM_BEARER_3GPP (ctx->bearer)->priv->port));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    g_simple_async_result_complete_in_idle (ctx->result);

    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    g_object_unref (ctx->bearer);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->result);
    g_free (ctx->cgact_command);
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
cgact_primary_ready (MMBaseModem *modem,
                     GAsyncResult *res,
                     DisconnectContext *ctx)
{
    /* Ignore errors for now */
    mm_base_modem_at_command_finish (MM_BASE_MODEM (modem), res, NULL);

    disconnect_context_complete_and_free (ctx);
}

static void
primary_flash_ready (MMSerialPort *port,
                     GError *error,
                     DisconnectContext *ctx)
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
            ctx->error = g_error_copy (error);
            disconnect_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Port flashing failed (not fatal): %s", error->message);
    }

    /* Don't bother doing the CGACT again if it was done on a secondary port */
    if (!ctx->cgact_sent) {
        disconnect_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command_in_port (
        ctx->modem,
        ctx->primary,
        ctx->cgact_command,
        3,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)cgact_primary_ready,
        ctx);
}

static void
cgact_secondary_ready (MMBaseModem *modem,
                       GAsyncResult *res,
                       DisconnectContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (modem), res, &error);
    if (!error)
        ctx->cgact_sent = TRUE;
    else
        g_error_free (error);

    mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_ready,
                          ctx);
}

static void
disconnect (MMBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    DisconnectContext *ctx;
    MMBaseModem *modem = NULL;

    if (!MM_BEARER_3GPP (self)->priv->port) {
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

    ctx = g_new0 (DisconnectContext, 1);
    ctx->data = g_object_ref (MM_BEARER_3GPP (self)->priv->port);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (modem));
    ctx->secondary = mm_base_modem_get_port_secondary (modem);
    if (ctx->secondary)
        g_object_ref (ctx->secondary);
    ctx->bearer = g_object_ref (self);
    ctx->modem = modem;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    /* If no specific CID was used, disable all PDP contexts */
    ctx->cgact_command =
        (MM_BEARER_3GPP (self)->priv->cid >= 0 ?
         g_strdup_printf ("+CGACT=0,%d", MM_BEARER_3GPP (self)->priv->cid) :
         g_strdup_printf ("+CGACT=0"));

    /* If the primary port is connected (with PPP) then try sending the PDP
     * context deactivation on the secondary port because not all modems will
     * respond to flashing (since either the modem or the kernel's serial
     * driver doesn't support it).
     */
    if (ctx->secondary &&
        mm_port_get_connected (MM_PORT (ctx->primary))) {
        mm_base_modem_at_command_in_port (
            ctx->modem,
            ctx->secondary,
            ctx->cgact_command,
            3,
            FALSE,
            NULL, /* cancellable */
            (GAsyncReadyCallback)cgact_secondary_ready,
            ctx);
        return;
    }

    mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_ready,
                          ctx);
}

/*****************************************************************************/

gchar *
mm_bearer_3gpp_new_unique_path (void)
{
    static guint id = 0;

    return g_strdup_printf (MM_DBUS_BEARER_3GPP_PREFIX "/%d", id++);
}

MMBearer *
mm_bearer_3gpp_new_finish (MMIfaceModem3gpp *modem,
                           GAsyncResult *res,
                           GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return MM_BEARER (g_object_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res))));
}

void
mm_bearer_3gpp_new (MMIfaceModem3gpp *modem,
                    MMCommonBearerProperties *properties,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMBearer3gpp *bearer;
    gchar *path;

    result = g_simple_async_result_new (G_OBJECT (modem),
                                        callback,
                                        user_data,
                                        mm_bearer_3gpp_new);

    /* Check mandatory properties */
    if (!mm_common_bearer_properties_get_apn (properties)) {
        g_simple_async_result_set_error (
            result,
            MM_CORE_ERROR,
            MM_CORE_ERROR_INVALID_ARGS,
            "Invalid input properties: 3GPP bearer requires 'apn'");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Create the object */
    bearer = g_object_new (MM_TYPE_BEARER_3GPP,
                           MM_BEARER_3GPP_APN, mm_common_bearer_properties_get_apn (properties),
                           MM_BEARER_3GPP_IP_TYPE, mm_common_bearer_properties_get_ip_type (properties),
                           MM_BEARER_ALLOW_ROAMING, mm_common_bearer_properties_get_allow_roaming (properties),
                           NULL);

    /* Set modem and path ONLY after having checked input properties, so that
     * we don't export invalid bearers. */
    path = mm_bearer_3gpp_new_unique_path ();
    g_object_set (bearer,
                  MM_BEARER_PATH,  path,
                  MM_BEARER_MODEM, modem,
                  NULL);
    g_free (path);

    g_simple_async_result_set_op_res_gpointer (result,
                                               bearer,
                                               (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete_in_idle (result);
    g_object_unref (result);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBearer3gpp *self = MM_BEARER_3GPP (object);

    switch (prop_id) {
    case PROP_APN:
        g_free (self->priv->apn);
        self->priv->apn = g_value_dup_string (value);
        break;
    case PROP_IP_TYPE:
        g_free (self->priv->ip_type);
        self->priv->ip_type = g_value_dup_string (value);
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
    MMBearer3gpp *self = MM_BEARER_3GPP (object);

    switch (prop_id) {
    case PROP_APN:
        g_value_set_string (value, self->priv->apn);
        break;
    case PROP_IP_TYPE:
        g_value_set_string (value, self->priv->ip_type);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_bearer_3gpp_init (MMBearer3gpp *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_3GPP,
                                              MMBearer3gppPrivate);
}

static void
finalize (GObject *object)
{
    MMBearer3gpp *self = MM_BEARER_3GPP (object);

    g_free (self->priv->apn);
    g_free (self->priv->ip_type);

    G_OBJECT_CLASS (mm_bearer_3gpp_parent_class)->finalize (object);
}

static void
mm_bearer_3gpp_class_init (MMBearer3gppClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearer3gppPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    bearer_class->connect = connect;
    bearer_class->connect_finish = connect_finish;
    bearer_class->disconnect = disconnect;
    bearer_class->disconnect_finish = disconnect_finish;

    properties[PROP_APN] =
        g_param_spec_string (MM_BEARER_3GPP_APN,
                             "APN",
                             "Access Point Name to use in the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_APN, properties[PROP_APN]);

    properties[PROP_IP_TYPE] =
        g_param_spec_string (MM_BEARER_3GPP_IP_TYPE,
                             "IP type",
                             "IP setup to use in the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_IP_TYPE, properties[PROP_IP_TYPE]);
}
