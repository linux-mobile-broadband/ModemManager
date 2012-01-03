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
 * Copyright (C) 2011 Google, Inc.
 */

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-cdma.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

/*****************************************************************************/

void
mm_iface_modem_cdma_bind_simple_status (MMIfaceModemCdma *self,
                                        MMCommonSimpleProperties *status)
{
    MmGdbusModemCdma *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &skeleton,
                  NULL);

    /* TODO: Bind here properties to be reported during GetStatus() in the
     * simple interface */

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemCdma *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemCdma *self;
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
dbus_call_context_new (MmGdbusModemCdma *skeleton,
                       GDBusMethodInvocation *invocation,
                       MMIfaceModemCdma *self)
{
    DbusCallContext *ctx;

    ctx = g_new (DbusCallContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    return ctx;
}

/*****************************************************************************/

gboolean
mm_iface_modem_cdma_activate_finish (MMIfaceModemCdma *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
activate_ready (MMIfaceModemCdma *self,
                GAsyncResult *res,
                GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_finish (self,
                                                                    res,
                                                                    &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_iface_modem_cdma_activate (MMIfaceModemCdma *self,
                              const gchar *carrier,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_assert (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate != NULL);
    g_assert (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_finish != NULL);

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_cdma_activate);

    MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate (
        self,
        carrier,
        (GAsyncReadyCallback)activate_ready,
        result);
}

static void
handle_activate_ready (MMIfaceModemCdma *self,
                       GAsyncResult *res,
                       DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_cdma_activate_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_cdma_complete_activate (ctx->skeleton,
                                               ctx->invocation,
                                               MM_MODEM_CDMA_ACTIVATION_ERROR_NONE
);

    dbus_call_context_free (ctx);
}

static gboolean
handle_activate (MmGdbusModemCdma *skeleton,
                 GDBusMethodInvocation *invocation,
                 const gchar *carrier,
                 MMIfaceModemCdma *self)
{
    MMModemState modem_state;

    /* If activating OTA is not implemented, report an error */
    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate ||
        !MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot perform OTA activation: "
                                               "operation not supported");
        return TRUE;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
        /* We should never have such request in UNKNOWN state */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform OTA activation: "
                                               "device locked");
        break;


    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        mm_iface_modem_cdma_activate (self,
                                      carrier,
                                      (GAsyncReadyCallback)handle_activate_ready,
                                      dbus_call_context_new (skeleton,
                                                             invocation,
                                                             self));

    case MM_MODEM_STATE_DISABLING:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform OTA activation: "
                                               "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform OTA activation: "
                                               "not enabled yet");
        break;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform OTA activation: "
                                               "modem is connected");
        break;
    }

    return TRUE;
}

/*****************************************************************************/

gboolean
mm_iface_modem_cdma_activate_manual_finish (MMIfaceModemCdma *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
activate_manual_ready (MMIfaceModemCdma *self,
                       GAsyncResult *res,
                       GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_manual_finish (self,
                                                                           res,
                                                                           &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_iface_modem_cdma_activate_manual (MMIfaceModemCdma *self,
                                     GVariant *properties,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_assert (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_manual != NULL);
    g_assert (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_manual_finish != NULL);

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_cdma_activate_manual);

    MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_manual (
        self,
        properties,
        (GAsyncReadyCallback)activate_manual_ready,
        result);
}

static void
handle_activate_manual_ready (MMIfaceModemCdma *self,
                              GAsyncResult *res,
                              DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_cdma_activate_manual_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_cdma_complete_activate_manual (ctx->skeleton,
                                                      ctx->invocation);

    dbus_call_context_free (ctx);
}

static gboolean
handle_activate_manual (MmGdbusModemCdma *skeleton,
                        GDBusMethodInvocation *invocation,
                        GVariant *properties,
                        MMIfaceModemCdma *self)
{
    MMModemState modem_state;

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
        /* We should never have such request in UNKNOWN state */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform manual activation: "
                                               "device locked");
        break;


    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        mm_iface_modem_cdma_activate_manual (self,
                                             properties,
                                             (GAsyncReadyCallback)handle_activate_manual_ready,
                                             dbus_call_context_new (skeleton,
                                                                    invocation,
                                                                    self));

    case MM_MODEM_STATE_DISABLING:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform manual activation: "
                                               "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform manual activation: "
                                               "not enabled yet");
        break;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform manual activation: "
                                               "modem is connected");
        break;
    }

    return TRUE;
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModemCdma *self;
    MMAtSerialPort *primary;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemCdma *skeleton;
};

static DisablingContext *
disabling_context_new (MMIfaceModemCdma *self,
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
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &ctx->skeleton,
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
mm_iface_modem_cdma_disable_finish (MMIfaceModemCdma *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_cdma_disable (MMIfaceModemCdma *self,
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
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModemCdma *self;
    MMAtSerialPort *primary;
    EnablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemCdma *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModemCdma *self,
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
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &ctx->skeleton,
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
mm_iface_modem_cdma_enable_finish (MMIfaceModemCdma *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
interface_enabling_step (EnablingContext *ctx)
{
    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        enabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_cdma_enable (MMIfaceModemCdma *self,
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
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModemCdma *self;
    MMAtSerialPort *port;
    MmGdbusModemCdma *skeleton;
    GSimpleAsyncResult *result;
    InitializationStep step;
};

static InitializationContext *
initialization_context_new (MMIfaceModemCdma *self,
                            MMAtSerialPort *port,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    InitializationContext *ctx;

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (port);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_context_new);
    ctx->step = INITIALIZATION_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &ctx->skeleton,
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
interface_initialization_step (InitializationContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-activate",
                          G_CALLBACK (handle_activate),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-activate-manual",
                          G_CALLBACK (handle_activate_manual),
                          ctx->self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_cdma (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                 MM_GDBUS_MODEM_CDMA (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_cdma_initialize_finish (MMIfaceModemCdma *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM_CDMA (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_cdma_initialize (MMIfaceModemCdma *self,
                                MMAtSerialPort *port,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MmGdbusModemCdma *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_CDMA (self));

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_cdma_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem_cdma_set_meid (skeleton, NULL);
        mm_gdbus_modem_cdma_set_esn (skeleton, NULL);
        mm_gdbus_modem_cdma_set_sid (skeleton, 0);
        mm_gdbus_modem_cdma_set_nid (skeleton, 0);
        mm_gdbus_modem_cdma_set_cdma1x_registration_state (skeleton,
                                                           MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
        mm_gdbus_modem_cdma_set_evdo_registration_state (skeleton,
                                                         MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

        g_object_set (self,
                      MM_IFACE_MODEM_CDMA_DBUS_SKELETON, skeleton,
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
mm_iface_modem_cdma_shutdown (MMIfaceModemCdma *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_CDMA (self));

    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_cdma (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_cdma_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_CDMA_DBUS_SKELETON,
                              "CDMA DBus skeleton",
                              "DBus skeleton for the CDMA interface",
                              MM_GDBUS_TYPE_MODEM_CDMA_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_cdma_get_type (void)
{
    static GType iface_modem_cdma_type = 0;

    if (!G_UNLIKELY (iface_modem_cdma_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemCdma), /* class_size */
            iface_modem_cdma_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_cdma_type = g_type_register_static (G_TYPE_INTERFACE,
                                                        "MMIfaceModemCdma",
                                                        &info,
                                                        0);

        g_type_interface_add_prerequisite (iface_modem_cdma_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_cdma_type;
}
