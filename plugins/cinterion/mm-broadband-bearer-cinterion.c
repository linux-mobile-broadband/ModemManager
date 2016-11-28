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
/* Auth helpers */

typedef enum {
    BEARER_CINTERION_AUTH_UNKNOWN   = -1,
    BEARER_CINTERION_AUTH_NONE      =  0,
    BEARER_CINTERION_AUTH_PAP       =  1,
    BEARER_CINTERION_AUTH_CHAP      =  2,
    BEARER_CINTERION_AUTH_MSCHAPV2  =  3,
} BearerCinterionAuthType;

static BearerCinterionAuthType
parse_auth_type (MMBearerAllowedAuth mm_auth)
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

static gchar *
build_auth_string (MMBearerProperties *config,
                   gint                usb_interface_config_index)
{
    const gchar             *user;
    const gchar             *passwd;
    MMBearerAllowedAuth      auth;
    BearerCinterionAuthType  encoded_auth = BEARER_CINTERION_AUTH_UNKNOWN;

    user = mm_bearer_properties_get_user (config);
    passwd = mm_bearer_properties_get_password (config);

    /* Normal use case is no user & pass so return as quick as possible */
    if (!user && !passwd)
        return NULL;

    auth = mm_bearer_properties_get_allowed_auth (config);
    encoded_auth = parse_auth_type (auth);

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
    return g_strdup_printf ("^SGAUTH=%u,%d,%s,%s",
                            usb_interface_configs[usb_interface_config_index].pdp_context,
                            encoded_auth,
                            passwd,
                            user);
}

/*****************************************************************************/
/* Connect 3GPP */

typedef enum {
    CONNECT_3GPP_CONTEXT_STEP_FIRST = 0,
    CONNECT_3GPP_CONTEXT_STEP_AUTH,
    CONNECT_3GPP_CONTEXT_STEP_PDP_CTX,
    CONNECT_3GPP_CONTEXT_STEP_START_SWWAN,
    CONNECT_3GPP_CONTEXT_STEP_VALIDATE_CONNECTION,
    CONNECT_3GPP_CONTEXT_STEP_IP_CONFIG,
    CONNECT_3GPP_CONTEXT_STEP_LAST,
} Connect3gppContextStep;

typedef struct {
    MMBroadbandBearerCinterion *self;
    MMBaseModem                *modem;
    MMPortSerialAt             *primary;
    MMPort                     *data;
    gint                        usb_interface_config_index;
    Connect3gppContextStep      step;
    MMBearerIpConfig           *ipv4_config;
    GCancellable               *cancellable;
    GSimpleAsyncResult         *result;
} Connect3gppContext;

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
connect_3gpp_finish (MMBroadbandBearer  *self,
                     GAsyncResult       *res,
                     GError            **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return mm_bearer_connect_result_ref (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void connect_3gpp_context_step (Connect3gppContext *ctx);

static void
swwan_connect_check_status_ready (MMBaseModem        *modem,
                                  GAsyncResult       *res,
                                  Connect3gppContext *ctx)
{
    const gchar  *response;
    GError       *error = NULL;
    MMSwwanState  state;

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (!response) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    state = mm_cinterion_parse_swwan_response (response,
                                               usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                                               &error);
    if (state == MM_SWWAN_STATE_UNKNOWN) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    if (state == MM_SWWAN_STATE_DISCONNECTED) {
        g_simple_async_result_set_error (ctx->result, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                         "CID %u is reported disconnected",
                                         usb_interface_configs[ctx->usb_interface_config_index].pdp_context);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    g_assert (state == MM_SWWAN_STATE_CONNECTED);

    /* Go to next step */
    ctx->step++;
    connect_3gpp_context_step (ctx);
}

static void
common_connect_operation_ready (MMBaseModem        *modem,
                                GAsyncResult       *res,
                                Connect3gppContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* Go to next step */
    ctx->step++;
    connect_3gpp_context_step (ctx);
}

static void
handle_cancel_connect (Connect3gppContext *ctx)
{
    gchar *command;

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

    g_simple_async_result_set_error (ctx->result, MM_CORE_ERROR, MM_CORE_ERROR_CANCELLED,
                                     "Connection operation has been cancelled");
    connect_3gpp_context_complete_and_free (ctx);
}

static void
connect_3gpp_context_step (Connect3gppContext *ctx)
{
    /* Check for cancellation */
    if (g_cancellable_is_cancelled (ctx->cancellable)) {
        handle_cancel_connect (ctx);
        return;
    }

    switch (ctx->step) {
    case CONNECT_3GPP_CONTEXT_STEP_FIRST: {
        MMBearerIpFamily ip_family;

        /* Only IPv4 supported by this bearer implementation for now */
        ip_family = mm_bearer_properties_get_ip_type (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));
        if (ip_family == MM_BEARER_IP_FAMILY_NONE || ip_family == MM_BEARER_IP_FAMILY_ANY) {
            gchar *ip_family_str;

            ip_family = mm_base_bearer_get_default_ip_family (MM_BASE_BEARER (ctx->self));
            ip_family_str = mm_bearer_ip_family_build_string_from_mask (ip_family);
            mm_dbg ("No specific IP family requested, defaulting to %s", ip_family_str);
            g_free (ip_family_str);
        }

        if (ip_family != MM_BEARER_IP_FAMILY_IPV4) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Only IPv4 is supported by this modem");
            connect_3gpp_context_complete_and_free (ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;
    }
    case CONNECT_3GPP_CONTEXT_STEP_AUTH: {
        gchar *command;

        command = build_auth_string (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)),
                                     ctx->usb_interface_config_index);

        if (command) {
            mm_dbg ("cinterion connect step 1/6: authenticating...");
            /* Send SGAUTH write, if User & Pass are provided.
             * advance to next state by callback */
            mm_base_modem_at_command_full (ctx->modem,
                                           ctx->primary,
                                           command,
                                           10,
                                           FALSE,
                                           FALSE,
                                           NULL,
                                           (GAsyncReadyCallback) common_connect_operation_ready,
                                           ctx);
            g_free (command);
            return;
        }

        /* Fall down to next step */
        mm_dbg ("cinterion connect step 1/6: authentication not required");
        ctx->step++;
    }
    case CONNECT_3GPP_CONTEXT_STEP_PDP_CTX: {
        gchar       *command;
        const gchar *apn;

        apn = mm_bearer_properties_get_apn (mm_base_bearer_peek_config (MM_BASE_BEARER (ctx->self)));

        mm_dbg ("cinterion connect step 2/6: configuring PDP context %u with APN '%s'",
                usb_interface_configs[ctx->usb_interface_config_index].pdp_context, apn);

        /* TODO: Get IP type if protocol was specified. Hardcoded to IPV4 for now */
        command = g_strdup_printf ("+CGDCONT=%u,\"IP\",\"%s\"",
                                   usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                                   apn ? apn : "");
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       5,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback) common_connect_operation_ready,
                                       ctx);
        g_free (command);
        return;
    }
    case CONNECT_3GPP_CONTEXT_STEP_START_SWWAN: {
        gchar *command;

        mm_dbg ("cinterion connect step 3/6: starting SWWAN interface %u connection...",
                usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        command = g_strdup_printf ("^SWWAN=1,%u,%u",
                                   usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                                   usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       90,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback) common_connect_operation_ready,
                                       ctx);
        g_free (command);
        return;
    }
    case CONNECT_3GPP_CONTEXT_STEP_VALIDATE_CONNECTION:
        mm_dbg ("cinterion connect step 4/6: checking SWWAN interface %u status...",
                usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "^SWWAN?",
                                       5,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback) swwan_connect_check_status_ready,
                                       ctx);
        return;

    case CONNECT_3GPP_CONTEXT_STEP_IP_CONFIG:
        mm_dbg ("cinterion connect step 5/6: creating IP config...");
        /* Default to automatic/DHCP addressing */
        ctx->ipv4_config = mm_bearer_ip_config_new ();
        mm_bearer_ip_config_set_method (ctx->ipv4_config, MM_BEARER_IP_METHOD_DHCP);
        /* Fall down to next step */
        ctx->step++;

    case CONNECT_3GPP_CONTEXT_STEP_LAST:
        mm_dbg ("cinterion connect step 6/6: finished");
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   mm_bearer_connect_result_new (ctx->data, ctx->ipv4_config, NULL),
                                                   (GDestroyNotify) mm_bearer_connect_result_unref);
        connect_3gpp_context_complete_and_free (ctx);
        return;
    }
}

static void
connect_3gpp (MMBroadbandBearer   *self,
              MMBroadbandModem    *modem,
              MMPortSerialAt      *primary,
              MMPortSerialAt      *secondary,
              GCancellable        *cancellable,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
    Connect3gppContext *ctx;
    MMPort             *port;
    gint                usb_interface_config_index;
    GError             *error = NULL;

    g_assert (primary != NULL);

    /* Get a net port to setup the connection on */
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
    ctx->step = CONNECT_3GPP_CONTEXT_STEP_FIRST;

    /* Run! */
    connect_3gpp_context_step (ctx);
}

/*****************************************************************************/
/* Disconnect 3GPP */

typedef enum {
    DISCONNECT_3GPP_CONTEXT_STEP_FIRST,
    DISCONNECT_3GPP_CONTEXT_STEP_STOP_SWWAN,
    DISCONNECT_3GPP_CONTEXT_STEP_CONNECTION_STATUS,
    DISCONNECT_3GPP_CONTEXT_STEP_LAST,
} Disconnect3gppContextStep;

typedef struct {
    MMBroadbandBearerCinterion *self;
    MMBaseModem                *modem;
    MMPortSerialAt             *primary;
    MMPort                     *data;
    gint                        usb_interface_config_index;
    Disconnect3gppContextStep   step;
    GSimpleAsyncResult         *result;
} Disconnect3gppContext;

static void
disconnect_3gpp_context_complete_and_free (Disconnect3gppContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->result);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_slice_free (Disconnect3gppContext, ctx);
}

static gboolean
disconnect_3gpp_finish (MMBroadbandBearer  *self,
                        GAsyncResult       *res,
                        GError            **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void disconnect_3gpp_context_step (Disconnect3gppContext *ctx);

static void
swwan_disconnect_check_status_ready (MMBaseModem           *modem,
                                     GAsyncResult          *res,
                                     Disconnect3gppContext *ctx)
{
    const gchar  *response;
    GError       *error = NULL;
    MMSwwanState  state;

    response = mm_base_modem_at_command_full_finish (modem, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }

    state = mm_cinterion_parse_swwan_response (response,
                                               usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                                               &error);

    if (state == MM_SWWAN_STATE_CONNECTED) {
        g_simple_async_result_set_error (ctx->result, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                         "CID %u is reported connected",
                                         usb_interface_configs[ctx->usb_interface_config_index].pdp_context);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }

    if (state == MM_SWWAN_STATE_UNKNOWN) {
        /* Assume disconnected */
        mm_dbg ("couldn't get CID %u status, assume disconnected: %s",
                usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                error->message);
        g_error_free (error);
    } else
        g_assert (state == MM_SWWAN_STATE_DISCONNECTED);

    /* Go on to next step */
    ctx->step++;
    disconnect_3gpp_context_step (ctx);
}

static void
swwan_disconnect_ready (MMBaseModem           *modem,
                        GAsyncResult          *res,
                        Disconnect3gppContext *ctx)
{
    /* We don't bother to check error or response here since, ctx flow's
     * next step checks it */
    mm_base_modem_at_command_full_finish (modem, res, NULL);

    /* Go on to next step */
    ctx->step++;
    disconnect_3gpp_context_step (ctx);
}

static void
disconnect_3gpp_context_step (Disconnect3gppContext *ctx)
{
    switch (ctx->step) {
    case DISCONNECT_3GPP_CONTEXT_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISCONNECT_3GPP_CONTEXT_STEP_STOP_SWWAN: {
        gchar *command;

        command = g_strdup_printf ("^SWWAN=0,%u,%u",
                                   usb_interface_configs[ctx->usb_interface_config_index].pdp_context,
                                   usb_interface_configs[ctx->usb_interface_config_index].swwan_index);

        mm_dbg ("cinterion disconnect step 1/3: disconnecting PDP CID %u...",
                usb_interface_configs[ctx->usb_interface_config_index].pdp_context);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       command,
                                       10,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)swwan_disconnect_ready,
                                       ctx);
        g_free (command);
        return;
    }
    case DISCONNECT_3GPP_CONTEXT_STEP_CONNECTION_STATUS:
        mm_dbg ("cinterion disconnect step 2/3: checking SWWAN interface %u status...",
                usb_interface_configs[ctx->usb_interface_config_index].swwan_index);
        mm_base_modem_at_command_full (ctx->modem,
                                       ctx->primary,
                                       "^SWWAN?",
                                       5,
                                       FALSE,
                                       FALSE,
                                       NULL,
                                       (GAsyncReadyCallback)swwan_disconnect_check_status_ready,
                                       ctx);
         return;

    case DISCONNECT_3GPP_CONTEXT_STEP_LAST:
        mm_dbg ("cinterion disconnect step 3/3: finished");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disconnect_3gpp_context_complete_and_free (ctx);
        return;
    }
}

static void
disconnect_3gpp (MMBroadbandBearer  *self,
                 MMBroadbandModem   *modem,
                 MMPortSerialAt     *primary,
                 MMPortSerialAt     *secondary,
                 MMPort             *data,
                 guint               cid,
                 GAsyncReadyCallback callback,
                 gpointer            user_data)
{
    Disconnect3gppContext *ctx;
    gint                   usb_interface_config_index;
    GError                *error = NULL;

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

    /* Input CID must match */
    g_warn_if_fail (cid == usb_interface_configs[usb_interface_config_index].pdp_context);

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
    ctx->step = DISCONNECT_3GPP_CONTEXT_STEP_FIRST;

    /* Start */
    disconnect_3gpp_context_step (ctx);
}

/*****************************************************************************/
/* Setup and Init Bearers */

MMBaseBearer *
mm_broadband_bearer_cinterion_new_finish (GAsyncResult  *res,
                                          GError       **error)
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
mm_broadband_bearer_cinterion_new (MMBroadbandModemCinterion *modem,
                                   MMBearerProperties        *config,
                                   GCancellable              *cancellable,
                                   GAsyncReadyCallback        callback,
                                   gpointer                   user_data)
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
}

static void
mm_broadband_bearer_cinterion_class_init (MMBroadbandBearerCinterionClass *klass)
{
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    broadband_bearer_class->connect_3gpp           = connect_3gpp;
    broadband_bearer_class->connect_3gpp_finish    = connect_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp        = disconnect_3gpp;
    broadband_bearer_class->disconnect_3gpp_finish = disconnect_3gpp_finish;
}
