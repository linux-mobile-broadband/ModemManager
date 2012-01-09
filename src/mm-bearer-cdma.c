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
};

/*****************************************************************************/

MMModemCdmaRmProtocol
mm_bearer_cdma_get_rm_protocol (MMBearerCdma *self)
{
    return self->priv->rm_protocol;
}

/*****************************************************************************/
/* CONNECT */

static gboolean
connect_finish (MMBearer *self,
                GAsyncResult *res,
                GError **error)
{
    return FALSE;
}

static void
connect (MMBearer *self,
         const gchar *number,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
}

/*****************************************************************************/
/* DISCONNECT */

static gboolean
disconnect_finish (MMBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return FALSE;
}

static void
disconnect (MMBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
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
            GEnumClass *enum_class;
            GEnumValue *value;

            /* Check if value within the range */
            if (ctx->self->priv->rm_protocol >= min &&
                ctx->self->priv->rm_protocol <= max) {
                /* Fine, go on with next step */
                ctx->step++;
                interface_initialization_step (ctx);
            }

            enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_CDMA_RM_PROTOCOL));
            value = g_enum_get_value (enum_class, ctx->self->priv->rm_protocol);
            g_assert (error == NULL);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested RM protocol '%s' is not supported",
                                 value->value_nick);
            g_type_class_unref (enum_class);
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
