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

#define LOCATION_CONTEXT_TAG "location-context-tag"

static GQuark location_context_quark;

/*****************************************************************************/

void
mm_iface_modem_location_bind_simple_status (MMIfaceModemLocation *self,
                                            MMCommonSimpleProperties *status)
{
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemLocation *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemLocation *self;
} DbusCallContext;

static void
dbus_call_context_free (DbusCallContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static DbusCallContext *
dbus_call_context_new (MmGdbusModemLocation *skeleton,
                       GDBusMethodInvocation *invocation,
                       MMIfaceModemLocation *self)
{
    DbusCallContext *ctx;

    ctx = g_new (DbusCallContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    return ctx;
}

/*****************************************************************************/

typedef struct {
    gboolean interface_enabled;
} LocationContext;

static LocationContext *
get_location_context (MMIfaceModemLocation *self)
{
    LocationContext *ctx;

    if (G_UNLIKELY (!location_context_quark))
        location_context_quark =  (g_quark_from_static_string (
                                       LOCATION_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), location_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_new0 (LocationContext, 1);

        g_object_set_qdata_full (
            G_OBJECT (self),
            location_context_quark,
            ctx,
            (GDestroyNotify)g_free);
    }

    return ctx;
}

/*****************************************************************************/

static void
enable_location_gathering_ready (MMIfaceModemLocation *self,
                                 GAsyncResult *res,
                                 DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering_finish (self,
                                                                                         res,
                                                                                         &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem_location_set_enabled (ctx->skeleton, TRUE);
        mm_gdbus_modem_location_complete_enable (ctx->skeleton,
                                                 ctx->invocation);
    }

    dbus_call_context_free (ctx);
}

static void
disable_location_gathering_ready (MMIfaceModemLocation *self,
                                  GAsyncResult *res,
                                  DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering_finish (self,
                                                                                          res,
                                                                                          &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem_location_set_enabled (ctx->skeleton, FALSE);
        mm_gdbus_modem_location_complete_enable (ctx->skeleton,
                                                 ctx->invocation);
    }

    dbus_call_context_free (ctx);
}

static gboolean
handle_enable (MmGdbusModemLocation *skeleton,
               GDBusMethodInvocation *invocation,
               gboolean enable,
               gboolean signal_location,
               MMIfaceModemLocation *self)
{
    LocationContext *ctx;

    ctx = get_location_context (self);

    /* Enabling */
    if (enable) {
        mm_dbg ("Enabling location gathering%s...",
                signal_location ? " (with signaling)" : "");

        /* Update the new signal location value */
        if (mm_gdbus_modem_location_get_signals_location (skeleton) != signal_location) {
            mm_dbg ("%s location signaling",
                    signal_location ? "Enabling" : "Disabling");
            mm_gdbus_modem_location_set_signals_location (skeleton, signal_location);
            if (!signal_location)
                /* If disabling, cleanup currently available value */
                mm_gdbus_modem_location_set_location (skeleton, NULL);
        }

        /* If already enabled, just done */
        if (mm_gdbus_modem_location_get_enabled (skeleton))
            mm_gdbus_modem_location_complete_enable (skeleton, invocation);
        /* Plugins can run custom actions to enable location gathering */
        else if (ctx->interface_enabled &&
                 MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering &&
                 MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering_finish)
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering (
                self,
                (GAsyncReadyCallback)enable_location_gathering_ready,
                dbus_call_context_new (skeleton,
                                       invocation,
                                       self));
        else {
            /* If no plugin-specific setup needed or interface not yet enabled, just done */
            mm_gdbus_modem_location_set_enabled (skeleton, TRUE);
            mm_gdbus_modem_location_complete_enable (skeleton, invocation);
        }
    }
    /* Disabling */
    else {
        mm_dbg ("Disabling location gathering...");

        /* If already disabled, just done */
        if (!mm_gdbus_modem_location_get_enabled (skeleton))
            mm_gdbus_modem_location_complete_enable (skeleton, invocation);
        /* Plugins can run custom actions to disable location gathering */
        else if (ctx->interface_enabled &&
                 MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering &&
                 MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering_finish)
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering (
                self,
                (GAsyncReadyCallback)disable_location_gathering_ready,
                dbus_call_context_new (skeleton,
                                       invocation,
                                       self));
        else {
            /* If no plugin-specific setup needed, or interface not yet enabled, just done */
            mm_gdbus_modem_location_set_enabled (skeleton, FALSE);
            mm_gdbus_modem_location_complete_enable (skeleton, invocation);
        }
    }

    return TRUE;
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

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_GATHERING,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModemLocation *self;
    MMAtSerialPort *primary;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemLocation *skeleton;
};

static DisablingContext *
disabling_context_new (MMIfaceModemLocation *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disabling_context_new);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    return ctx;
}

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_location_disable_finish (MMIfaceModemLocation *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disabling_location_gathering_ready (MMIfaceModemLocation *self,
                                    GAsyncResult *res,
                                    DisablingContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->disable_location_gathering_finish (self,
                                                                                          res,
                                                                                          &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        disabling_context_complete_and_free (ctx);
        return;
    }

    mm_gdbus_modem_location_set_enabled (ctx->skeleton, FALSE);

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

static void
interface_disabling_step (DisablingContext *ctx)
{
    LocationContext *location_context;

    location_context = get_location_context (ctx->self);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_GATHERING:
        if (mm_gdbus_modem_location_get_enabled (ctx->skeleton) &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->disable_location_gathering &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->disable_location_gathering_finish) {
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->disable_location_gathering (
                ctx->self,
                (GAsyncReadyCallback)disabling_location_gathering_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        location_context->interface_enabled = FALSE;
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_location_disable (MMIfaceModemLocation *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    interface_disabling_step (disabling_context_new (self,
                                                     callback,
                                                     user_data));
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_ENABLE_GATHERING,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModemLocation *self;
    MMAtSerialPort *primary;
    EnablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemLocation *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModemLocation *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    EnablingContext *ctx;

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enabling_context_new);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_LOCATION_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    return ctx;
}

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_location_enable_finish (MMIfaceModemLocation *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
enabling_location_gathering_ready (MMIfaceModemLocation *self,
                                   GAsyncResult *res,
                                   EnablingContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_LOCATION_GET_INTERFACE (self)->enable_location_gathering_finish (self,
                                                                                         res,
                                                                                         &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
interface_enabling_step (EnablingContext *ctx)
{
    LocationContext *location_context;

    location_context = get_location_context (ctx->self);

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_GATHERING:
        if (mm_gdbus_modem_location_get_enabled (ctx->skeleton) &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->enable_location_gathering &&
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->enable_location_gathering_finish) {
            MM_IFACE_MODEM_LOCATION_GET_INTERFACE (ctx->self)->enable_location_gathering (
                ctx->self,
                (GAsyncReadyCallback)enabling_location_gathering_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        location_context->interface_enabled = TRUE;
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        enabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_location_enable (MMIfaceModemLocation *self,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    interface_enabling_step (enabling_context_new (self,
                                                   callback,
                                                   user_data));
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
        mm_gdbus_modem_location_set_signals_location (skeleton, FALSE);
        mm_gdbus_modem_location_set_location (skeleton, NULL);

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
