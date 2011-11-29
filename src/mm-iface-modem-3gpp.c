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

#include <mm-gdbus-modem.h>
#include <mm-enums-types.h>
#include <mm-errors-types.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem.h"
#include "mm-log.h"

typedef struct {
    MmGdbusModem3gpp *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp *self;
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
dbus_call_context_new (MmGdbusModem3gpp *skeleton,
                       GDBusMethodInvocation *invocation,
                       MMIfaceModem3gpp *self)
{
    DbusCallContext *ctx;

    ctx = g_new (DbusCallContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    return ctx;
}

/*****************************************************************************/

static void
register_in_network_ready (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish (self,
                                                                               res,
                                                                               &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem3gpp_complete_register (ctx->skeleton,
                                               ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_register (MmGdbusModem3gpp *skeleton,
                 GDBusMethodInvocation *invocation,
                 const gchar *arg_network_id,
                 MMIfaceModem3gpp *self)
{
    MMModemState modem_state;

    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network != NULL);
    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish != NULL);

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
        /* We should never have a UNKNOWN->REGISTERED transition */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: device locked");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network (
            self,
            arg_network_id,
            (GAsyncReadyCallback)register_in_network_ready,
            dbus_call_context_new (skeleton,
                                   invocation,
                                   self));
        break;

    case MM_MODEM_STATE_DISABLING:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "not yet enabled");
        break;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "modem is connected");
        break;
    }

    return TRUE;
}

static gboolean
handle_scan (MmGdbusModem3gpp *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModem3gpp *self)
{
    return FALSE; /* Currently unhandled */
}

/*****************************************************************************/

typedef struct {
    GSimpleAsyncResult *result;
    gboolean cs_done;
    GError *cs_reg_error;
    GError *ps_reg_error;
} RunAllRegistrationChecksContext;

static void
run_all_registration_checks_context_complete_and_free (RunAllRegistrationChecksContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_clear_error (&ctx->cs_reg_error);
    g_clear_error (&ctx->ps_reg_error);
    g_object_unref (ctx->result);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_run_all_registration_checks_finish (MMIfaceModem3gpp *self,
                                                        GAsyncResult *res,
                                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
run_ps_registration_check_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 RunAllRegistrationChecksContext *ctx)
{
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish (self, res, &ctx->ps_reg_error);

    /* If both CS and PS registration checks returned errors we fail */
    if (ctx->ps_reg_error &&
        (ctx->cs_reg_error || !ctx->cs_done))
        /* Prefer the PS error */
        g_simple_async_result_set_from_error (ctx->result, ctx->ps_reg_error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

static void
run_cs_registration_check_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 RunAllRegistrationChecksContext *ctx)
{
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check_finish (self, res, &ctx->cs_reg_error);

    if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish) {
        ctx->cs_done = TRUE;
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check (
            self,
            (GAsyncReadyCallback)run_ps_registration_check_ready,
            ctx);
        return;
    }

    /* All done */
    if (ctx->cs_reg_error)
        g_simple_async_result_set_from_error (ctx->result, ctx->cs_reg_error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

void
mm_iface_modem_3gpp_run_all_registration_checks (MMIfaceModem3gpp *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data)
{
    RunAllRegistrationChecksContext *ctx;

    ctx = g_new0 (RunAllRegistrationChecksContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_3gpp_run_all_registration_checks);

    if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check (
            self,
            (GAsyncReadyCallback)run_cs_registration_check_ready,
            ctx);
        return;
    }

    if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check (
            self,
            (GAsyncReadyCallback)run_ps_registration_check_ready,
            ctx);
        return;
    }

    /* Nothing to do :-/ all done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_update_registration_state (MMIfaceModem3gpp *self,
                                               MMModem3gppRegistrationState new_state)
{
    MMModem3gppRegistrationState previous_state;

    /* Only set new state if different */
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &previous_state,
                  NULL);
    if (new_state == previous_state)
        return;

    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, new_state,
                  NULL);

    /* TODO:
     * While connected we don't want registration status changes to change
     * the modem's state away from CONNECTED.
     */
    switch (new_state) {
    case MM_MODEM_3GPP_REGISTRATION_STATE_HOME:
    case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING:
        mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                     MM_MODEM_STATE_REGISTERED,
                                     MM_MODEM_STATE_REASON_NONE);
        break;
    case MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING:
        mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                     MM_MODEM_STATE_SEARCHING,
                                     MM_MODEM_STATE_REASON_NONE);
        break;
    case MM_MODEM_3GPP_REGISTRATION_STATE_IDLE:
    case MM_MODEM_3GPP_REGISTRATION_STATE_DENIED:
    case MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN:
        mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                     MM_MODEM_STATE_ENABLED,
                                     MM_MODEM_STATE_REASON_NONE);
        break;
    }
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION,
    ENABLING_STEP_SETUP_CS_REGISTRATION,
    ENABLING_STEP_SETUP_PS_REGISTRATION,
    ENABLING_STEP_RUN_ALL_REGISTRATION_CHECKS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModem3gpp *self;
    MMAtSerialPort *primary;
    EnablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModem3gpp *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModem3gpp *self,
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
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
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
mm_iface_modem_3gpp_enable_finish (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME)                                       \
    static void                                                         \
    NAME##_ready (MMIfaceModem3gpp *self,                               \
                  GAsyncResult *res,                                    \
                  EnablingContext *ctx)                                 \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            g_simple_async_result_take_error (ctx->result, error);      \
            enabling_context_complete_and_free (ctx);                   \
            return;                                                     \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_enabling_step (ctx);                                  \
    }

VOID_REPLY_READY_FN (setup_cs_registration)
VOID_REPLY_READY_FN (setup_ps_registration)

static void
run_all_registration_checks_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   EnablingContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_3gpp_run_all_registration_checks_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

VOID_REPLY_READY_FN (setup_unsolicited_registration)

static void
interface_enabling_step (EnablingContext *ctx)
{
    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_unsolicited_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_CS_REGISTRATION:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_cs_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_PS_REGISTRATION:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_ps_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_RUN_ALL_REGISTRATION_CHECKS:
        mm_iface_modem_3gpp_run_all_registration_checks (
            ctx->self,
            (GAsyncReadyCallback)run_all_registration_checks_ready,
            ctx);
        return;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        enabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_enable (MMIfaceModem3gpp *self,
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
    INITIALIZATION_STEP_IMEI,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem3gpp *self;
    MMAtSerialPort *port;
    MmGdbusModem3gpp *skeleton;
    GSimpleAsyncResult *result;
    InitializationStep step;
};

static InitializationContext *
initialization_context_new (MMIfaceModem3gpp *self,
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
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
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
load_imei_ready (MMIfaceModem3gpp *self,
                 GAsyncResult *res,
                 InitializationContext *ctx)
{
    GError *error = NULL;
    gchar *imei;

    imei = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_imei_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_imei (ctx->skeleton, imei);
    g_free (imei);

    if (error) {
        mm_warn ("couldn't load IMEI: '%s'", error->message);
        g_error_free (error);
    }

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

    case INITIALIZATION_STEP_IMEI:
        /* IMEI value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem3gpp_get_imei (ctx->skeleton) &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei (
                ctx->self,
                (GAsyncReadyCallback)load_imei_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-register",
                          G_CALLBACK (handle_register),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-scan",
                          G_CALLBACK (handle_scan),
                          ctx->self);


        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem3gpp (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                MM_GDBUS_MODEM3GPP (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_3gpp_initialize_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM_3GPP (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_3gpp_initialize (MMIfaceModem3gpp *self,
                                MMAtSerialPort *port,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MmGdbusModem3gpp *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_3GPP (self));

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem3gpp_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem3gpp_set_imei (skeleton, NULL);
        mm_gdbus_modem3gpp_set_operator_code (skeleton, NULL);
        mm_gdbus_modem3gpp_set_operator_name (skeleton, NULL);
        mm_gdbus_modem3gpp_set_enabled_facility_locks (skeleton, MM_MODEM_3GPP_FACILITY_NONE);

        /* Bind our RegistrationState property */
        g_object_bind_property (self, MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                                skeleton, "registration-state",
                                G_BINDING_DEFAULT);

        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON, skeleton,
                      MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,
                      NULL);
    }

    /* Perform async initialization here */
    interface_initialization_step (initialization_context_new (self,
                                                               port,
                                                               callback,
                                                               user_data));
    g_object_unref (skeleton);
    return;
}

void
mm_iface_modem_3gpp_shutdown (MMIfaceModem3gpp *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_3GPP (self));

    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem3gpp (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_3gpp_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_3GPP_DBUS_SKELETON,
                              "3GPP DBus skeleton",
                              "DBus skeleton for the 3GPP interface",
                              MM_GDBUS_TYPE_MODEM3GPP_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_enum (MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                            "RegistrationState",
                            "Registration state of the modem",
                            MM_TYPE_MODEM_3GPP_REGISTRATION_STATE,
                            MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,
                            G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_3gpp_get_type (void)
{
    static GType iface_modem_3gpp_type = 0;

    if (!G_UNLIKELY (iface_modem_3gpp_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem3gpp), /* class_size */
            iface_modem_3gpp_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_3gpp_type = g_type_register_static (G_TYPE_INTERFACE,
                                                        "MMIfaceModem3gpp",
                                                        &info,
                                                        0);

        g_type_interface_add_prerequisite (iface_modem_3gpp_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_3gpp_type;
}
