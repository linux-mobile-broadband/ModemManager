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
 * Copyright (C) 2016 Trimble Navigation Limited
 * Author: Matthew Stanger <matthew_stanger@trimble.com>
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
#include "mm-broadband-bearer-cinterion.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-modem-helpers-cinterion.h"
#include "mm-daemon-enums-types.h"

G_DEFINE_TYPE (MMBroadbandBearerCinterion, mm_broadband_bearer_cinterion, MM_TYPE_BROADBAND_BEARER)

/*****************************************************************************/
/* Common enums and structs */

#define FIRST_USB_INTERFACE 1
#define SECOND_USB_INTERFACE 2

typedef enum {
    BearerCinterionAuthUnknown   = -1,
    BearerCinterionAuthNone      =  0,
    BearerCinterionAuthPap       =  1,
    BearerCinterionAuthChap      =  2,
    BearerCinterionAuthMsChapV2  =  3,
} BearerCinterionAuthType;

typedef enum {
    Connect3gppContextStepInit = 0,
    Connect3gppContextStepAuth,
    Connect3gppContextStepPdpCtx,
    Connect3gppContextStepStartSwwan,
    Connect3gppContextStepValidateConnection,
    Connect3gppContextStepIpConfig,
    Connect3gppContextStepFinalizeBearer,
} Connect3gppContextStep;

typedef enum {
    Disconnect3gppContextStepStopSwwan = 0,
    Disconnect3gppContextStepConnectionStatus,
    Disconnect3gppContextStepFinish
} Disconnect3gppContextStep;

typedef struct {
    MMBroadbandBearerCinterion *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPort *data;
    Connect3gppContextStep connect;
    MMBearerIpConfig *ipv4_config;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
} Connect3gppContext;

typedef struct {
    MMBroadbandBearerCinterion *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPort *data;
    Disconnect3gppContextStep disconnect;
    GSimpleAsyncResult *result;
} Disconnect3gppContext;

struct _MMBroadbandBearerCinterionPrivate {
    guint network_disconnect_pending_id;/* Flag for network-initiated disconnect */
    guint pdp_cid;/* Mapping for PDP Context to network (SWWAN) interface */
};

/*****************************************************************************/
/* Common 3GPP Function Declarations */
static void connect_3gpp_context_step (Connect3gppContext *ctx);
static void disconnect_3gpp_context_step (Disconnect3gppContext *ctx);
static void connect_3gpp_context_complete_and_free (Connect3gppContext *ctx);
static void disconnect_3gpp_context_complete_and_free (Disconnect3gppContext *ctx);

/*****************************************************************************/
/* Common - Helper Functions*/

static void
pdp_cid_map (MMBroadbandBearerCinterion *self, const gchar *bearer_interface)
{
    /* Map PDP context from the current Bearer. USB0 -> 3rd context, USB1 -> 1st context.
     * Note - Cinterion told me specifically to map the contexts to the interfaces this way, though
     * I've seen that SWWAN appear's to work fine with any PDP to any interface */

    if (g_strcmp0 (bearer_interface, "0a") == 0)
        self->priv->pdp_cid = 3;
    else if (g_strcmp0 (bearer_interface, "0c") == 0)
        self->priv->pdp_cid = 1;
    else
        /* Shouldn't be able to create a bearer for SWWAN and not be able to
         * find the net interface. Otherwise connects/disconnects will get wrecked */
        g_assert_not_reached ();
}

static gint
verify_connection_state_from_swwan_response (GList *result, GError **error)
{
    /* Returns 0 if SWWAN is connected, 1 if not connected, -1 on error
     * for the bearer's target interface */

    if (g_list_length(result) != 0) {
        int first_result = GPOINTER_TO_INT(result->data);

        /* Received an 'OK'(0) response */
        if (first_result == 0)
            return 1;
        /* 1 || 3 result is the CID, given when that context is activated.
         * TODO: Rework for dual sim connections. */
        else if (first_result == 1 || first_result ==3)
            return 0;
        else {
            for (; result; result = g_list_next(result))
                mm_err ("Unknown SWWAN response data:%i", GPOINTER_TO_INT(result->data));

            g_set_error (error,
                         MM_CORE_ERROR,
                         MM_CORE_ERROR_INVALID_ARGS,
                         "Unparsable SWWAN response format.");

            return -1;
        }
     }
     else {
        mm_err ("Unable to parse zero length SWWAN response.");

        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_INVALID_ARGS,
                     "Unparsable SWWAN response format.");

        return -1;
     }
}


/*****************************************************************************/
/* Connect - Helper Functions*/

static gint
cinterion_parse_auth_type (MMBearerAllowedAuth mm_auth)
{
    switch (mm_auth) {
    case MM_BEARER_ALLOWED_AUTH_NONE:
        return BearerCinterionAuthNone;
    case MM_BEARER_ALLOWED_AUTH_PAP:
        return BearerCinterionAuthPap;
    case MM_BEARER_ALLOWED_AUTH_CHAP:
        return BearerCinterionAuthChap;
    case MM_BEARER_ALLOWED_AUTH_MSCHAPV2:
        return BearerCinterionAuthMsChapV2;
    default:
        return BearerCinterionAuthUnknown;
    }
}

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



static void
pdp_cid_connect (Connect3gppContext *ctx)
{
    const gchar *bearer_interface;

    /* For E: INTERFACE=usb0 -> E: ID_USB_INTERFACE_NUM=0a
     * For E: INTERFACE=usb1 -> E: ID_USB_INTERFACE_NUM=0c */

    /* Look up the net port to associate with this bearer */
    bearer_interface = mm_kernel_device_get_property (mm_port_peek_kernel_device (ctx->data),
                                   "ID_USB_INTERFACE_NUM");

    pdp_cid_map (ctx->self, bearer_interface);

    return;
}

static void
get_cmd_write_response_ctx_connect (MMBaseModem *modem,
                                    GAsyncResult *res,
                                    Connect3gppContext *ctx)
{
    /* Only use this to parse responses that respond 'Ok' or 'ERROR' */
    GError *error = NULL;

    /* Check to see if the command had an error */
    mm_base_modem_at_command_finish (modem, res, &error);

    /* We expect either OK or an error */
    if (error != NULL) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    g_clear_error (&error);

    /*GOTO next step */
    ctx->connect++;
    connect_3gpp_context_step (ctx);
}

static void
get_swwan_read_response_ctx_connect (MMBaseModem *modem,
                                     GAsyncResult *res,
                                     Connect3gppContext *ctx)
{
    const gchar *response;
    GError *error = NULL;
    GList *response_parsed = NULL;

    /* Get the SWWAN read response */
    response = mm_base_modem_at_command_finish (modem, res, &error);

    /* Error if parsing SWWAN read response fails */
    if (!mm_cinterion_parse_swwan_response (response, &response_parsed, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /*Check parsed SWWAN reponse to see if we are now connected */
    if (verify_connection_state_from_swwan_response(response_parsed, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    g_list_free(response_parsed);
    g_clear_error (&error);

    /* GOTO next step */
    ctx->connect++;
    connect_3gpp_context_step (ctx);
}

static void
setup_ip_settings (Connect3gppContext *ctx)
{
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

    /* Default to automatic/DHCP addressing */
    ctx->ipv4_config = mm_bearer_ip_config_new ();
    mm_bearer_ip_config_set_method (ctx->ipv4_config, MM_BEARER_IP_METHOD_DHCP);
}

static gchar *
build_cinterion_auth_string (Connect3gppContext *ctx)
{
    const gchar         *user = NULL;
    const gchar         *passwd = NULL;
    MMBearerAllowedAuth  auth;
    gint                 encoded_auth = BearerCinterionAuthUnknown;
    gchar               *command = NULL;

    user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    passwd = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));

    /* Normal use case is no user & pass so return as quick as possible */
    if (!user && !passwd)
        return NULL;

    auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    encoded_auth = cinterion_parse_auth_type (auth);

    /* Default to no authentication if not specified */
    if (encoded_auth == BearerCinterionAuthUnknown) {
        encoded_auth = BearerCinterionAuthNone;
        mm_dbg ("Unable to detect authentication type. Defaulting to:%i", encoded_auth);
    }

    /* TODO: Haven't tested this as I can't get a hold of a SIM w/ this feature atm.
     * Write Command
     * AT^SGAUTH=<cid>[, <auth_type>[, <passwd>, <user>]]
     * Response(s)
     * OK
     * ERROR
     * +CME ERROR: <err>
     */

    command = g_strdup_printf ("^SGAUTH=%i,%i,%s,%s",
                               ctx->self->priv->pdp_cid,
                               encoded_auth,
                               passwd,
                               user);

    return command;
}

static gchar *
build_cinterion_pdp_context_string (Connect3gppContext *ctx)
{
    const gchar         *apn = NULL;
    gchar               *command;

    apn = mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));

    /* TODO: Get IP type if protocol was specified. Hardcoded to IPV4 for now */

    command = g_strdup_printf ("+CGDCONT=%i,\"IP\",\"%s\"",
                               ctx->self->priv->pdp_cid,
                               apn == NULL ? "" : apn);

    return command;
}

static void
handle_cancel_connect (Connect3gppContext *ctx)
{
    gchar               *command;

    /* 3rd context -> 1st wwan adapt / 1st context -> 2nd wwan adapt */
    command = g_strdup_printf ("^SWWAN=%s,%i,%i",
                               "0",
                               ctx->self->priv->pdp_cid,
                               ctx->self->priv->pdp_cid == 3  ? FIRST_USB_INTERFACE : SECOND_USB_INTERFACE);

    /* Disconnect, may not succeed. Will not check response on cancel */
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   NULL,
                                   NULL);

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Cinterion connection operation has been cancelled");
    connect_3gpp_context_complete_and_free (ctx);

}

static void
create_cinterion_bearer (Connect3gppContext *ctx)
{
    if (ctx->ipv4_config) {
        g_simple_async_result_set_op_res_gpointer (
            ctx->result,
            mm_bearer_connect_result_new (ctx->data, ctx->ipv4_config, NULL),
            (GDestroyNotify)mm_bearer_connect_result_unref);
    }
    else {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_WRONG_STATE,
                                         "Cinterion connection failed to set IP protocol");
    }
}

/*****************************************************************************/
/* Connect - AT Command Wrappers*/

static void
send_swwan_read_command_ctx_connect (Connect3gppContext *ctx)
{
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   "^SWWAN?",
                                   5,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)get_swwan_read_response_ctx_connect,
                                   ctx);
}


static void
send_write_command_ctx_connect (Connect3gppContext *ctx,
                                gchar **command)
{
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   *command,
                                   10,/*Seen it take 5 seconds :0 */
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)get_cmd_write_response_ctx_connect,
                                   ctx);
}

static void
send_swwan_connect_command_ctx_connect (Connect3gppContext *ctx)
{
    /* 3rd context -> 1st wwan adapt / 1st context -> 2nd wwan adapt */
    gchar               *command;
    command = g_strdup_printf ("^SWWAN=%s,%i,%i",
                               "1",
                               ctx->self->priv->pdp_cid,
                               ctx->self->priv->pdp_cid == 3  ? FIRST_USB_INTERFACE : SECOND_USB_INTERFACE);

    send_write_command_ctx_connect(ctx, &command);

    g_free (command);
}

static void
send_swwan_disconnect_command_ctx_connect (Connect3gppContext *ctx)
{
    /* 3rd context -> 1st wwan adapt / 1st context -> 2nd wwan adapt */
    gchar               *command;
    command = g_strdup_printf ("^SWWAN=%s,%i,%i",
                               "0",
                               ctx->self->priv->pdp_cid,
                               ctx->self->priv->pdp_cid == 3  ? FIRST_USB_INTERFACE : SECOND_USB_INTERFACE);

    send_write_command_ctx_connect(ctx, &command);

    g_free (command);
}

/*****************************************************************************/
/* Connect - Bearer */

static void
connect_3gpp_context_step (Connect3gppContext *ctx)
{
    /* Check for cancellation */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        handle_cancel_connect(ctx);
        return;
    }

    mm_dbg ("Connect Step:%i", ctx->connect);

    /* Network-initiated disconnect should not be outstanding at this point,
       because it interferes with the connect attempt.*/
    g_assert (ctx->self->priv->network_disconnect_pending_id == 0);

    switch (ctx->connect) {
    case Connect3gppContextStepInit:

        /* Insure no connection is currently
         * active with the bearer we're creating.*/
        send_swwan_disconnect_command_ctx_connect (ctx);

        return;
    case Connect3gppContextStepAuth: {

        gchar *command = NULL;

        command = build_cinterion_auth_string (ctx);

        mm_dbg ("Auth String:%s", command);

        /* Send SGAUTH write, if User & Pass are provided.
         * advance to next state by callback */
        if (command != NULL) {
            mm_dbg ("Sending auth");

            send_write_command_ctx_connect (ctx, &command);
            g_free (command);
            return;
        }

        /* GOTO next step - Fall down below */
        ctx->connect++;
    }
    case Connect3gppContextStepPdpCtx: {
        gchar *command = NULL;

        command = build_cinterion_pdp_context_string (ctx);

        /*Set the PDP context with cgdcont.*/
        send_write_command_ctx_connect (ctx, &command);

        g_free (command);
        return;
    }
    case Connect3gppContextStepStartSwwan:

        send_swwan_connect_command_ctx_connect (ctx);

        return;
    case Connect3gppContextStepValidateConnection:

        send_swwan_read_command_ctx_connect (ctx);

        return;
    case Connect3gppContextStepIpConfig:

        setup_ip_settings (ctx);

        /* GOTO next step - Fall down below */
        ctx->connect++;
    case Connect3gppContextStepFinalizeBearer:
        /* Setup bearer */
        create_cinterion_bearer (ctx);

        /* Clear context */
        ctx->self->priv->pdp_cid = 0;

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
    MMPort *port;

    g_assert (primary != NULL);

    /* Get a Net port to setup the connection on */
    port = mm_base_modem_peek_best_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!port) {
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
    ctx->data = g_object_ref (port);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect_3gpp);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->primary = g_object_ref (primary);

    /* Maps Bearer-> Net Interface-> PDP Context */
    pdp_cid_connect (ctx);

    /* Initialize */
    ctx->connect = Connect3gppContextStepInit;

    /* Run! */
    connect_3gpp_context_step (ctx);
}


/*****************************************************************************/
/* Disconnect - Helper Functions*/

static void
pdp_cid_disconnect(Disconnect3gppContext *ctx)
{
    const gchar *bearer_interface;

    /* For E: INTERFACE=usb0 -> E: ID_USB_INTERFACE_NUM=0a
     * For E: INTERFACE=usb1 -> E: ID_USB_INTERFACE_NUM=0c */

    /* Look up the net port to associate with this bearer */
    bearer_interface = mm_kernel_device_get_property (mm_port_peek_kernel_device (ctx->data),
                                   "ID_USB_INTERFACE_NUM");

    pdp_cid_map (ctx->self, bearer_interface);

    return;
}

static void
get_cmd_write_response_ctx_disconnect (MMBaseModem *modem,
                                       GAsyncResult *res,
                                       Disconnect3gppContext *ctx)
{
    /* We don't bother to check error or response here since, ctx flow's
     * next step checks it */

    /* GOTO next step */
    ctx->disconnect++;
    disconnect_3gpp_context_step (ctx);
}

static void
get_swwan_read_response_ctx_disconnect (MMBaseModem *modem,
                                        GAsyncResult *res,
                                        Disconnect3gppContext *ctx)
{
    const gchar *response;
    GError *error = NULL;
    GList *response_parsed = NULL;

    /* Get the SWWAN response */
    response = mm_base_modem_at_command_finish (modem, res, &error);

    /* Return if the SWWAN read threw an error or parsing it fails */
    if (!mm_cinterion_parse_swwan_response (response, &response_parsed, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Check parsed SWWAN reponse to see if we are now disconnected */
    if (verify_connection_state_from_swwan_response (response_parsed, &error) != 1) {

        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Disconnection attempt failed");

        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }

    g_list_free (response_parsed);
    g_clear_error (&error);

    /* GOTO next step */
    ctx->disconnect++;
    disconnect_3gpp_context_step (ctx);
}

/*****************************************************************************/
/* Disconnect - AT Command Wrappers*/

static void
send_swwan_disconnect_command_ctx_disconnect (Disconnect3gppContext *ctx)
{
    /* 3rd context -> 1st wwan adapt / 1st context -> 2nd wwan adapt */
    gchar               *command;
    command = g_strdup_printf ("^SWWAN=%s,%i,%i",
                               "0",
                               ctx->self->priv->pdp_cid,
                               ctx->self->priv->pdp_cid == 3  ? FIRST_USB_INTERFACE : SECOND_USB_INTERFACE);

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   10,/*Seen it take 5 seconds :0 */
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)get_cmd_write_response_ctx_disconnect,
                                   ctx);

    g_free (command);
}

static void
send_swwan_read_command_ctx_disconnect (Disconnect3gppContext *ctx)
{
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   "^SWWAN?",
                                   5,
                                   FALSE,
                                   FALSE,
                                   NULL,
                                   (GAsyncReadyCallback)get_swwan_read_response_ctx_disconnect,
                                   ctx);
}

/*****************************************************************************/
/* Disconnect - Bearer */

static void
disconnect_3gpp_context_complete_and_free (Disconnect3gppContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_clear_object (&ctx->data);
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

static void
disconnect_3gpp_context_step (Disconnect3gppContext *ctx)
{
    mm_dbg ("Disconnect Step:%i", ctx->disconnect);

    switch (ctx->disconnect) {
    case Disconnect3gppContextStepStopSwwan:

        /* Has call back to next state */
        send_swwan_disconnect_command_ctx_disconnect (ctx);

        return;
    case Disconnect3gppContextStepConnectionStatus:

         /* Has call back to next state */
         send_swwan_read_command_ctx_disconnect (ctx);

         return;

    case Disconnect3gppContextStepFinish:

        ctx->self->priv->pdp_cid = 0;

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
    MMPort *port;

    g_assert (primary != NULL);

    /* Note: Not sure how else to get active data port? Can this be done without adding this
     * function to mm-base-modem.c?
     * TODO: Dual SIM how do we know which interface to grab/disconnect if two are active? */
    /* Get the Net port to be torn down */
    port = mm_base_modem_peek_current_data_port (MM_BASE_MODEM (modem), MM_PORT_TYPE_NET);
    if (!port) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_NOT_FOUND,
                                             "No valid data port found to tear down.");
        return;
    }

    /* Setup connection context */
    ctx = g_slice_new0 (Disconnect3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect_3gpp);
    ctx->data = g_object_ref (port);
    ctx->primary = g_object_ref (primary);

    /* Maps Bearer->Net Interface-> PDP Context
     * We can't disconnect if we don't know which context to disconnect from. */
    pdp_cid_disconnect (ctx);

    /* Initialize */
    ctx->disconnect = Disconnect3gppContextStepStopSwwan;

    /* Start */
    disconnect_3gpp_context_step (ctx);
}

/*****************************************************************************/
/* Setup and Init Bearers */

MMBaseBearer *
mm_broadband_bearer_cinterion_new_finish (GAsyncResult *res,
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
    MMBroadbandBearerCinterion *self = MM_BROADBAND_BEARER_CINTERION (object);

    if (self->priv->network_disconnect_pending_id != 0) {
        g_source_remove (self->priv->network_disconnect_pending_id);
        self->priv->network_disconnect_pending_id = 0;
    }

    G_OBJECT_CLASS (mm_broadband_bearer_cinterion_parent_class)->dispose (object);
}

void
mm_broadband_bearer_cinterion_new (MMBroadbandModemCinterion *modem,
                                   MMBearerProperties *config,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_CINTERION,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM, modem,
        MM_BASE_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_cinterion_init (MMBroadbandBearerCinterion *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                              MM_TYPE_BROADBAND_BEARER_CINTERION,
                                              MMBroadbandBearerCinterionPrivate);
}

static void
mm_broadband_bearer_cinterion_class_init (MMBroadbandBearerCinterionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerCinterionPrivate));

    object_class->dispose = dispose;

    broadband_bearer_class->connect_3gpp = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish = connect_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
