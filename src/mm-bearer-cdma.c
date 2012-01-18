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
 * Copyright (C) 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-bearer-cdma.h"
#include "mm-base-modem-at.h"
#include "mm-utils.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMBearerCdma, mm_bearer_cdma, MM_TYPE_BEARER, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                               async_initable_iface_init));

enum {
    PROP_0,
    PROP_NUMBER,
    PROP_RM_PROTOCOL,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBearerCdmaPrivate {
    /* Number to dial */
    gchar *number;
    /* Protocol of the Rm interface */
    MMModemCdmaRmProtocol rm_protocol;

    /* Data port used when modem is connected */
    MMPort *port;
};

/*****************************************************************************/

MMModemCdmaRmProtocol
mm_bearer_cdma_get_rm_protocol (MMBearerCdma *self)
{
    return self->priv->rm_protocol;
}

/*****************************************************************************/
/* CONNECT
 * Connection procedure of a CDMA bearer involves several steps:
 * 1) Get data port from the modem. Default implementation will have only
 *    one single possible data port, but plugins may have more.
 * 2) If requesting specific RM, load current.
 *  2.1) If current RM different to the requested one, set the new one.
 * 3) Initiate call.
 */

typedef struct {
    MMBearer *bearer;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMPort *data;
    GSimpleAsyncResult *result;
    gchar *number;
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

        /* Keep data port around while connected */
        MM_BEARER_CDMA (ctx->bearer)->priv->port = g_object_ref (ctx->data);

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    g_simple_async_result_complete_in_idle (ctx->result);

    g_free (ctx->number);
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
connect_ready (MMBaseModem *modem,
               GAsyncResult *res,
               ConnectContext *ctx)
{
    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_finish (modem, res, &(ctx->error));
    if (ctx->error)
        mm_warn ("Couldn't connect: '%s'", ctx->error->message);
    /* else... Yuhu! */

    connect_context_complete_and_free (ctx);
}

static void
connect_context_dial (ConnectContext *ctx)
{
    gchar *command;

    /* Decide which number to dial, in the following order:
     *  (1) If a number given during Connect(), use it.
     *  (2) If a number given when creating the bearer, use that one. Wait, this is quite
     *      redundant, isn't it?
     *  (3) Otherwise, use the default one, #777
     */
    if (ctx->number)
        command = g_strconcat ("DT", ctx->number, NULL);
    else if (MM_BEARER_CDMA (ctx->bearer)->priv->number)
        command = g_strconcat ("DT", MM_BEARER_CDMA (ctx->bearer)->priv->number, NULL);
    else
        command = g_strdup ("DT#777");
    mm_base_modem_at_command_in_port (
        ctx->modem,
        ctx->primary,
        command,
        90,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)connect_ready,
        ctx);
    g_free (command);
}

static void
set_rm_protocol_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       ConnectContext *ctx)
{
     /* If cancelled, complete */
    if (connect_context_set_error_if_cancelled (ctx, &ctx->error)) {
        connect_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command_finish (self, res, &(ctx->error));
    if (ctx->error) {
        mm_warn ("Couldn't set RM protocol: '%s'", ctx->error->message);
        connect_context_complete_and_free (ctx);
        return;
    }

    /* Nothing else needed, go on with dialing */
    connect_context_dial (ctx);
}

static void
current_rm_protocol_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           ConnectContext *ctx)
{
    const gchar *result;
    guint current_index;
    MMModemCdmaRmProtocol current_rm;

    /* If cancelled, complete */
    if (connect_context_set_error_if_cancelled (ctx, &ctx->error)) {
        connect_context_complete_and_free (ctx);
        return;
    }

    result = mm_base_modem_at_command_finish (self, res, &(ctx->error));
    if (ctx->error) {
        mm_warn ("Couldn't query current RM protocol: '%s'", ctx->error->message);
        connect_context_complete_and_free (ctx);
        return;
    }

    result = mm_strip_tag (result, "+CRM:");
    current_index = (guint) atoi (result);
    current_rm = mm_cdma_get_rm_protocol_from_index (current_index, &ctx->error);
    if (ctx->error) {
        mm_warn ("Couldn't parse RM protocol reply (%s): '%s'",
                 result,
                 ctx->error->message);
        connect_context_complete_and_free (ctx);
        return;
    }

    if (current_rm != MM_BEARER_CDMA (ctx->bearer)->priv->rm_protocol) {
        guint new_index;
        gchar *command;

        mm_dbg ("Setting requested RM protocol...");

        new_index = (mm_cdma_get_index_from_rm_protocol (
                         MM_BEARER_CDMA (ctx->bearer)->priv->rm_protocol,
                         &ctx->error));
        if (ctx->error) {
            mm_warn ("Cannot set RM protocol: '%s'",
                     ctx->error->message);
            connect_context_complete_and_free (ctx);
            return;
        }

        command = g_strdup_printf ("+CRM=%u", new_index);
        mm_base_modem_at_command_in_port (
            ctx->modem,
            ctx->primary,
            command,
            3,
            FALSE,
            NULL, /* cancellable */
            (GAsyncReadyCallback)set_rm_protocol_ready,
            ctx);
        g_free (command);
        return;
    }

    /* Nothing else needed, go on with dialing */
    connect_context_dial (ctx);
}

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

    if (MM_BEARER_CDMA (self)->priv->port) {
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
    ctx->number = g_strdup (number);
    ctx->modem = modem;

    /* NOTE:
     * We don't currently support cancelling AT commands, so we'll just check
     * whether the operation is to be cancelled at each step. */
    ctx->cancellable = g_object_ref (cancellable);

    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    if (MM_BEARER_CDMA (self)->priv->rm_protocol != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
        /* Need to query current RM protocol */
        mm_dbg ("Querying current RM protocol set...");
        mm_base_modem_at_command_in_port (
            ctx->modem,
            ctx->primary,
            "+CRM?",
            3,
            FALSE,
            NULL, /* cancellable */
            (GAsyncReadyCallback)current_rm_protocol_ready,
            ctx);
        return;
    }

    /* Nothing else needed, go on with dialing */
    connect_context_dial (ctx);
}

/*****************************************************************************/
/* DISCONNECT */

typedef struct {
    MMBearer *bearer;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMPort *data;
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
        /* Clear data port */
        g_clear_object (&(MM_BEARER_CDMA (ctx->bearer)->priv->port));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    g_simple_async_result_complete_in_idle (ctx->result);

    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->bearer);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->result);
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
        } else
            mm_dbg ("Port flashing failed (not fatal): %s", error->message);
    }

    disconnect_context_complete_and_free (ctx);
}

static void
disconnect (MMBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    DisconnectContext *ctx;
    MMBaseModem *modem = NULL;

    if (!MM_BEARER_CDMA (self)->priv->port) {
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
    ctx->data = g_object_ref (MM_BEARER_CDMA (self)->priv->port);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (modem));
    ctx->bearer = g_object_ref (self);
    ctx->modem = modem;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_ready,
                          ctx);
}

/*****************************************************************************/

gchar *
mm_bearer_cdma_new_unique_path (void)
{
    static guint id = 0;

    return g_strdup_printf (MM_DBUS_BEARER_CDMA_PREFIX "/%d", id++);
}

/*****************************************************************************/

typedef struct _InitAsyncContext InitAsyncContext;
static void interface_initialization_step (InitAsyncContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_RM_PROTOCOL,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitAsyncContext {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMBearerCdma *self;
    MMBaseModem *modem;
    InitializationStep step;
    MMAtSerialPort *port;
};

static void
init_async_context_free (InitAsyncContext *ctx,
                         gboolean close_port)
{
    if (close_port)
        mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx);
}

MMBearer *
mm_bearer_cdma_new_finish (GAsyncResult *res,
                           GError **error)
{
    gchar *path;
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Set path ONLY after having created and initialized the object, so that we
     * don't export invalid bearers. */
    path = mm_bearer_cdma_new_unique_path ();
    g_object_set (bearer,
                  MM_BEARER_PATH, path,
                  NULL);
    g_free (path);

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

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        /* We should possibly take this error as fatal. If we were told to use a
         * specific Rm protocol, we must be able to check if it is supported. */
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        MMModemCdmaRmProtocol min = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
        MMModemCdmaRmProtocol max = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;

        if (mm_cdma_parse_crm_range_response (response,
                                              &min, &max,
                                              &error)) {
            /* Check if value within the range */
            if (ctx->self->priv->rm_protocol >= min &&
                ctx->self->priv->rm_protocol <= max) {
                /* Fine, go on with next step */
                ctx->step++;
                interface_initialization_step (ctx);
            }

            g_assert (error == NULL);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested RM protocol '%s' is not supported",
                                 mm_modem_cdma_rm_protocol_get_string (
                                     ctx->self->priv->rm_protocol));
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

    case INITIALIZATION_STEP_RM_PROTOCOL:
        /* If a specific RM protocol is given, we need to check whether it is
         * supported. */
        if (ctx->self->priv->rm_protocol != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
            mm_base_modem_at_command_in_port (
                ctx->modem,
                ctx->port,
                "+CRM=?",
                3,
                TRUE, /* getting range, so reply can be cached */
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
    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, FALSE);
        return;
    }

    interface_initialization_step (ctx);
}

void
mm_bearer_cdma_new (MMIfaceModemCdma *modem,
                    MMCommonBearerProperties *properties,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BEARER_CDMA,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM, modem,
        MM_BEARER_CDMA_NUMBER, mm_common_bearer_properties_get_number (properties),
        MM_BEARER_CDMA_RM_PROTOCOL, mm_common_bearer_properties_get_rm_protocol (properties),
        MM_BEARER_ALLOW_ROAMING, mm_common_bearer_properties_get_allow_roaming (properties),
        NULL);
}

/*****************************************************************************/

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBearerCdma *self = MM_BEARER_CDMA (object);

    switch (prop_id) {
    case PROP_NUMBER:
        g_free (self->priv->number);
        self->priv->number = g_value_dup_string (value);
        break;
    case PROP_RM_PROTOCOL:
        self->priv->rm_protocol = g_value_get_enum (value);
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
    MMBearerCdma *self = MM_BEARER_CDMA (object);

    switch (prop_id) {
    case PROP_NUMBER:
        g_value_set_string (value, self->priv->number);
        break;
    case PROP_RM_PROTOCOL:
        g_value_set_enum (value, self->priv->rm_protocol);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_bearer_cdma_init (MMBearerCdma *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BEARER_CDMA,
                                              MMBearerCdmaPrivate);
    self->priv->rm_protocol = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
    iface->init_async = initable_init_async;
    iface->init_finish = initable_init_finish;
}

static void
mm_bearer_cdma_class_init (MMBearerCdmaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBearerCdmaPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    bearer_class->connect = connect;
    bearer_class->connect_finish = connect_finish;
    bearer_class->disconnect = disconnect;
    bearer_class->disconnect_finish = disconnect_finish;

    properties[PROP_NUMBER] =
        g_param_spec_string (MM_BEARER_CDMA_NUMBER,
                             "Number to dial",
                             "Number to dial when launching the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_NUMBER, properties[PROP_NUMBER]);

    properties[PROP_RM_PROTOCOL] =
        g_param_spec_enum (MM_BEARER_CDMA_RM_PROTOCOL,
                           "Rm Protocol",
                           "Protocol to use in the Rm interface",
                           MM_TYPE_MODEM_CDMA_RM_PROTOCOL,
                           MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_RM_PROTOCOL, properties[PROP_RM_PROTOCOL]);
}
