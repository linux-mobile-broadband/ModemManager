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
/* WWAN interface mapping */

typedef struct {
    guint swwan_index;
    guint usb_iface_num;
    guint pdp_context;
} UsbInterfaceConfig;

/* Map SWWAN index, USB interface number and preferred PDP context.
 *
 * The expected USB interface mapping is:
 *   INTERFACE=usb0 -> ID_USB_INTERFACE_NUM=0a
 *   INTERFACE=usb1 -> ID_USB_INTERFACE_NUM=0c
 *
 * The preferred PDP context CIDs are:
 *   INTERFACE=usb0 -> PDP CID #3
 *   INTERFACE=usb1 -> PDP CID #1
 *
 * The PDP context mapping is as suggested by Cinterion, although it looks like
 * this isn't strictly enforced by the modem; i.e. SWWAN could work fine with
 * any PDP context vs SWWAN interface mapping.
 */
static const UsbInterfaceConfig usb_interface_configs[] = {
    {
        .swwan_index   = 1,
        .usb_iface_num = 0x0a,
        .pdp_context   = 3
    },
    {
        .swwan_index   = 2,
        .usb_iface_num = 0x0c,
        .pdp_context   = 1
    },
};

static gint
get_usb_interface_config_index (MMPort  *data,
                                GError **error)
{
    guint usb_iface_num;
    guint i;

    usb_iface_num = mm_kernel_device_get_property_as_int_hex (mm_port_peek_kernel_device (data), "ID_USB_INTERFACE_NUM");

    for (i = 0; i < G_N_ELEMENTS (usb_interface_configs); i++) {
        if (usb_interface_configs[i].usb_iface_num == usb_iface_num)
            return (gint) i;
    }

    g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                 "Unsupported WWAN interface: unexpected interface number: 0x%02x", usb_iface_num);
    return -1;
}

/*****************************************************************************/
/* Common enums and structs */

typedef enum {
    BEARER_CINTERION_AUTH_UNKNOWN   = -1,
    BEARER_CINTERION_AUTH_NONE      =  0,
    BEARER_CINTERION_AUTH_PAP       =  1,
    BEARER_CINTERION_AUTH_CHAP      =  2,
    BEARER_CINTERION_AUTH_MSCHAPV2  =  3,
} BearerCinterionAuthType;

typedef enum {
    CONNECT_3GPP_CONTEXT_STEP_INIT = 0,
    CONNECT_3GPP_CONTEXT_STEP_AUTH,
    CONNECT_3GPP_CONTEXT_STEP_PDP_CTX,
    CONNECT_3GPP_CONTEXT_STEP_START_SWWAN,
    CONNECT_3GPP_CONTEXT_STEP_VALIDATE_CONNECTION,
    CONNECT_3GPP_CONTEXT_STEP_IP_CONFIG,
    CONNECT_3GPP_CONTEXT_STEP_FINALIZE_BEARER,
} Connect3gppContextStep;

typedef enum {
    DISCONNECT_3GPP_CONTEXT_STEP_STOP_SWWAN = 0,
    DISCONNECT_3GPP_CONTEXT_STEP_CONNECTION_STATUS,
    DISCONNECT_3GPP_CONTEXT_STEP_FINISH,
} Disconnect3gppContextStep;;

typedef struct {
    MMBroadbandBearerCinterion *self;
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPort *data;
    gint usb_interface_config_index;
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
    gint usb_interface_config_index;
    Disconnect3gppContextStep disconnect;
    GSimpleAsyncResult *result;
} Disconnect3gppContext;

struct _MMBroadbandBearerCinterionPrivate {
    /* Flag for network-initiated disconnect */
    guint network_disconnect_pending_id;
};

/*****************************************************************************/
/* Common 3GPP Function Declarations */
static void connect_3gpp_context_step (Connect3gppContext *ctx);
static void disconnect_3gpp_context_step (Disconnect3gppContext *ctx);
static void connect_3gpp_context_complete_and_free (Connect3gppContext *ctx);
static void disconnect_3gpp_context_complete_and_free (Disconnect3gppContext *ctx);

/*****************************************************************************/
/* Common - Helper Functions*/

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

static BearerCinterionAuthType
cinterion_parse_auth_type (MMBearerAllowedAuth mm_auth)
{
    switch (mm_auth) {
    case MM_BEARER_ALLOWED_AUTH_NONE:
        return BEARER_CINTERION_AUTH_NONE;
    case MM_BEARER_ALLOWED_AUTH_PAP:
        return BEARER_CINTERION_AUTH_PAP;
    case MM_BEARER_ALLOWED_AUTH_CHAP:
        return BEARER_CINTERION_AUTH_CHAP;
    case MM_BEARER_ALLOWED_AUTH_MSCHAPV2:
        return BEARER_CINTERION_AUTH_MSCHAPV2;
    default:
        return BEARER_CINTERION_AUTH_UNKNOWN;
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
    const gchar             *user = NULL;
    const gchar             *passwd = NULL;
    MMBearerAllowedAuth      auth;
    BearerCinterionAuthType  encoded_auth = BEARER_CINTERION_AUTH_UNKNOWN;
    gchar                   *command = NULL;

    user = mm_bearer_properties_get_user (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    passwd = mm_bearer_properties_get_password (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));

    /* Normal use case is no user & pass so return as quick as possible */
    if (!user && !passwd)
        return NULL;

    auth = mm_bearer_properties_get_allowed_auth (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
    encoded_auth = cinterion_parse_auth_type (auth);

    /* Default to no authentication if not specified */
    if (encoded_auth == BEARER_CINTERION_AUTH_UNKNOWN) {
        encoded_auth = BEARER_CINTERION_AUTH_NONE;
        mm_dbg ("Unable to detect authentication type. Defaulting to 'none'");
    }

    /* TODO: Haven't tested this as I can't get a hold of a SIM w/ this feature atm.
     * Write Command
     * AT^SGAUTH=<cid>[, <auth_type>[, <passwd>, <user>]]
     * Response(s)
     * OK
     * ERROR
     * +CME ERROR: <err>
     */
    command = g_strdup_printf ("^SGAUTH=%u,%d,%s,%s",
                               usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
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

    command = g_strdup_printf ("+CGDCONT=%u,\"IP\",\"%s\"",
                               usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                               apn ? apn : "");

    return command;
}

static void
handle_cancel_connect (Connect3gppContext *ctx)
{
    gchar               *command;

    /* 3rd context -> 1st wwan adapt / 1st context -> 2nd wwan adapt */
    command = g_strdup_printf ("^SWWAN=0,%u,%u",
                               usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                               usb_interface_configs[ctx->usb_interface_config_index].swwan_index);

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
    command = g_strdup_printf ("^SWWAN=1,%u,%u",
                               usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                               usb_interface_configs[ctx->usb_interface_config_index].swwan_index);

    send_write_command_ctx_connect(ctx, &command);

    g_free (command);
}

static void
send_swwan_disconnect_command_ctx_connect (Connect3gppContext *ctx)
{
    /* 3rd context -> 1st wwan adapt / 1st context -> 2nd wwan adapt */
    gchar               *command;
    command = g_strdup_printf ("^SWWAN=0,%u,%u",
                               usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                               usb_interface_configs[ctx->usb_interface_config_index].swwan_index);

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
    case CONNECT_3GPP_CONTEXT_STEP_INIT:

        /* Insure no connection is currently
         * active with the bearer we're creating.*/
        send_swwan_disconnect_command_ctx_connect (ctx);

        return;
    case CONNECT_3GPP_CONTEXT_STEP_AUTH: {

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
    case CONNECT_3GPP_CONTEXT_STEP_PDP_CTX: {
        gchar *command = NULL;

        command = build_cinterion_pdp_context_string (ctx);

        /*Set the PDP context with cgdcont.*/
        send_write_command_ctx_connect (ctx, &command);

        g_free (command);
        return;
    }
    case CONNECT_3GPP_CONTEXT_STEP_START_SWWAN:

        send_swwan_connect_command_ctx_connect (ctx);

        return;
    case CONNECT_3GPP_CONTEXT_STEP_VALIDATE_CONNECTION:

        send_swwan_read_command_ctx_connect (ctx);

        return;
    case CONNECT_3GPP_CONTEXT_STEP_IP_CONFIG:

        setup_ip_settings (ctx);

        /* GOTO next step - Fall down below */
        ctx->connect++;
    case CONNECT_3GPP_CONTEXT_STEP_FINALIZE_BEARER:
        /* Setup bearer */
        create_cinterion_bearer (ctx);

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
    gint usb_interface_config_index;
    GError *error = NULL;

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

    /* Validate configuration */
    usb_interface_config_index = get_usb_interface_config_index (port, &error);
    if (usb_interface_config_index < 0) {
        g_simple_async_report_take_gerror_in_idle (G_OBJECT (self),
                                                   callback,
                                                   user_data,
                                                   error);
        return;
    }

    /* Setup connection context */
    ctx = g_slice_new0 (Connect3gppContext);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->data = g_object_ref (port);
    ctx->usb_interface_config_index = usb_interface_config_index;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect_3gpp);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->primary = g_object_ref (primary);

    /* Initialize */
    ctx->connect = CONNECT_3GPP_CONTEXT_STEP_INIT;

    /* Run! */
    connect_3gpp_context_step (ctx);
}


/*****************************************************************************/
/* Disconnect - Helper Functions*/

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
    command = g_strdup_printf ("^SWWAN=0,%u,%u",
                               usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                               usb_interface_configs[ctx->usb_interface_config_index].swwan_index);

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
    case DISCONNECT_3GPP_CONTEXT_STEP_STOP_SWWAN:

        /* Has call back to next state */
        send_swwan_disconnect_command_ctx_disconnect (ctx);

        return;
    case DISCONNECT_3GPP_CONTEXT_STEP_CONNECTION_STATUS:

         /* Has call back to next state */
         send_swwan_read_command_ctx_disconnect (ctx);

         return;

    case DISCONNECT_3GPP_CONTEXT_STEP_FINISH:

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
    gint usb_interface_config_index;
    GError *error = NULL;

    g_assert (primary != NULL);
    g_assert (data != NULL);

    /* Validate configuration */
    usb_interface_config_index = get_usb_interface_config_index (data, &error);
    if (usb_interface_config_index < 0) {
        g_simple_async_report_take_gerror_in_idle (G_OBJECT (self),
                                                   callback,
                                                   user_data,
                                                   error);
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
    ctx->primary = g_object_ref (primary);
    ctx->data = g_object_ref (data);
    ctx->usb_interface_config_index = usb_interface_config_index;

    /* Initialize */
    ctx->disconnect = DISCONNECT_3GPP_CONTEXT_STEP_STOP_SWWAN;

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
