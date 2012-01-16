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
 */

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-log.h"

/*****************************************************************************/

void
mm_iface_modem_location_bind_simple_status (MMIfaceModemLocation *self,
                                            MMCommonSimpleProperties *status)
{
}

/*****************************************************************************/

static gboolean
handle_enable (MmGdbusModemLocation *object,
               GDBusMethodInvocation *invocation,
               gboolean enable,
               gboolean signal_location,
               MMIfaceModemLocation *self)
{
    return FALSE;
}

/*****************************************************************************/

static gboolean
handle_get_location (MmGdbusModemLocation *object,
                     GDBusMethodInvocation *invocation,
                     MMIfaceModemLocation *self)
{
    return FALSE;
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CAPABILITIES,
    INITIALIZATION_STEP_VALIDATE_CAPABILITIES,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModemLocation *self;
    MMAtSerialPort *port;
    MmGdbusModemLocation *skeleton;
    GSimpleAsyncResult *result;
    InitializationStep step;
    MMModemLocationSource capabilities;
};

static InitializationContext *
initialization_context_new (MMIfaceModemLocation *self,
                            MMAtSerialPort *port,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    InitializationContext *ctx;

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (port);
    ctx->capabilities = MM_MODEM_LOCATION_SOURCE_NONE;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_context_new);
    ctx->step = INITIALIZATION_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static void
load_capabilities_ready (MMIfaceModemLocation *self,
                         GAsyncResult *res,
                         InitializationContext *ctx)
{
    GError *error = NULL;

    ctx->capabilities = MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->load_capabilities_finish (self, res, &error);
    if (error) {
        mm_warn ("couldn't load location capabilities: '%s'", error->message);
        g_error_free (error);
    }

    mm_gdbus_modem_location_set_capabilities (ctx->skeleton, ctx->capabilities);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization_step (InitializationContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CAPABILITIES:
        /* Location capabilities value is meant to be loaded only once during
         * the whole lifetime of the modem. Therefore, if we already have it
         * loaded, don't try to load it again. */
        if (!mm_gdbus_modem_location_get_capabilities (ctx->skeleton) &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->load_capabilities &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->load_capabilities_finish) {
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->load_capabilities (
                ctx->self,
                (GAsyncReadyCallback)load_capabilities_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_VALIDATE_CAPABILITIES:
        /* If the modem doesn't support any location capabilities, we won't export
         * the interface. We just report an UNSUPPORTED error. */
        if (ctx->capabilities == MM_MODEM_LOCATION_SOURCE_NONE) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "The modem doesn't have location capabilities");
            initialization_context_complete_and_free (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-enable",
                          G_CALLBACK (handle_enable),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-get-location",
                          G_CALLBACK (handle_get_location),
                          ctx->self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_location (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                     MM_GDBUS_MODEM_LOCATION (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_location_initialize_finish (MMIfaceModemLocation *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM_LOCATION (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_location_initialize (MMIfaceModemLocation *self,
                                    MMAtSerialPort *port,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    MmGdbusModemLocation *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_LOCATION (self));

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_location_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem_location_set_capabilities (skeleton, MM_MODEM_LOCATION_SOURCE_NONE);
        mm_gdbus_modem_location_set_enabled (skeleton, TRUE);
        mm_gdbus_modem_location_set_location (skeleton, NULL);
        mm_gdbus_modem_location_set_signals_location (skeleton, FALSE);

        g_object_set (self,
                      MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, skeleton,
                      NULL);
    }


    /* Perform async initialization here */
    interface_initialization_step (initialization_context_new (self,
                                                               port,
                                                               callback,
                                                               user_data));
    g_object_unref (skeleton);
}

void
mm_iface_modem_location_shutdown (MMIfaceModemLocation *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_LOCATION (self));

    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_location (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_location_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_LOCATION_DBUS_SKELETON,
                              "Location DBus skeleton",
                              "DBus skeleton for the Location interface",
                              MM_GDBUS_TYPE_MODEM_LOCATION_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_location_get_type (void)
{
    static GType iface_modem_location_type = 0;

    if (!G_UNLIKELY (iface_modem_location_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemLocation), /* class_size */
            iface_modem_location_init,     /* base_init */
            NULL,                          /* base_finalize */
        };

        iface_modem_location_type = g_type_register_static (G_TYPE_INTERFACE,
                                                            "MMIfaceModemLocation",
                                                            &info,
                                                            0);

        g_type_interface_add_prerequisite (iface_modem_location_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_location_type;
}
