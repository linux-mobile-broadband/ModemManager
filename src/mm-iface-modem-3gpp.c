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
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-log.h"

#define REGISTRATION_CHECK_TIMEOUT_SEC 30

#define SUBSYSTEM_3GPP "3gpp"

#define REGISTRATION_STATE_CONTEXT_TAG    "3gpp-registration-state-context-tag"
#define REGISTRATION_CHECK_CONTEXT_TAG    "3gpp-registration-check-context-tag"

static GQuark registration_state_context_quark;
static GQuark registration_check_context_quark;

/*****************************************************************************/

void
mm_iface_modem_3gpp_bind_simple_status (MMIfaceModem3gpp *self,
                                        MMSimpleStatus *status)
{
    MmGdbusModem3gpp *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    g_object_bind_property (skeleton, "registration-state",
                            status, MM_SIMPLE_PROPERTY_3GPP_REGISTRATION_STATE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "operator-code",
                            status, MM_SIMPLE_PROPERTY_3GPP_OPERATOR_CODE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "operator-name",
                            status, MM_SIMPLE_PROPERTY_3GPP_OPERATOR_NAME,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "subscription-state",
                            status, MM_SIMPLE_PROPERTY_3GPP_SUBSCRIPTION_STATE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    MMModem3gppRegistrationState cs;
    MMModem3gppRegistrationState ps;
    MMModem3gppRegistrationState eps;
    gboolean manual_registration;
    GCancellable *pending_registration_cancellable;
    gboolean reloading_registration_info;
} RegistrationStateContext;

static void
registration_state_context_free (RegistrationStateContext *ctx)
{
    if (ctx->pending_registration_cancellable) {
        g_cancellable_cancel (ctx->pending_registration_cancellable);
        g_object_unref (ctx->pending_registration_cancellable);
    }
    g_slice_free (RegistrationStateContext, ctx);
}

static RegistrationStateContext *
get_registration_state_context (MMIfaceModem3gpp *self)
{
    RegistrationStateContext *ctx;

    if (G_UNLIKELY (!registration_state_context_quark))
        registration_state_context_quark =  (g_quark_from_static_string (
                                                 REGISTRATION_STATE_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), registration_state_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_slice_new0 (RegistrationStateContext);
        ctx->cs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
        ctx->ps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
        ctx->eps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

        g_object_set_qdata_full (
            G_OBJECT (self),
            registration_state_context_quark,
            ctx,
            (GDestroyNotify)registration_state_context_free);
    }

    return ctx;
}

static MMModem3gppRegistrationState
get_consolidated_reg_state (RegistrationStateContext *ctx)
{
    /* Some devices (Blackberries for example) will respond to +CGREG, but
     * return ERROR for +CREG, probably because their firmware is just stupid.
     * So here we prefer the +CREG response, but if we never got a successful
     * +CREG response, we'll take +CGREG instead.
     */
    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return ctx->cs;
    if (ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return ctx->ps;
    if (ctx->eps == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        ctx->eps == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return ctx->eps;

    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return ctx->cs;
    if (ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return ctx->ps;
    if (ctx->eps == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return ctx->eps;

    /* If one state is DENIED and the others are UNKNOWN, use DENIED */
    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED &&
        ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN &&
        ctx->eps == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN)
        return ctx->cs;
    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN &&
        ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED &&
        ctx->eps == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN)
        return ctx->ps;
    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN &&
        ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN &&
        ctx->eps == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED)
        return ctx->eps;

    return ctx->cs;
}

/*****************************************************************************/

typedef struct {
    MMIfaceModem3gpp *self;
    MmGdbusModem3gpp *skeleton;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar *operator_id;
    GTimer *timer;
    guint max_registration_time;
} RegisterInNetworkContext;

static void
register_in_network_context_complete_and_free (RegisterInNetworkContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);

    if (ctx->timer)
        g_timer_destroy (ctx->timer);

    if (ctx->cancellable) {
        RegistrationStateContext *registration_state_context;

        /* Clear our cancellable if still around */
        registration_state_context = get_registration_state_context (ctx->self);
        if (registration_state_context->pending_registration_cancellable == ctx->cancellable)
            g_clear_object (&registration_state_context->pending_registration_cancellable);

        g_object_unref (ctx->cancellable);
    }

    g_free (ctx->operator_id);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_object_unref (ctx->self);
    g_slice_free (RegisterInNetworkContext, ctx);
}

static void
register_in_network_context_failed (RegisterInNetworkContext *ctx,
                                    GError *error)
{
    mm_iface_modem_3gpp_update_cs_registration_state (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);
    mm_iface_modem_3gpp_update_ps_registration_state (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);
    mm_iface_modem_3gpp_update_eps_registration_state (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);
    mm_iface_modem_3gpp_update_access_technologies (ctx->self, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
    mm_iface_modem_3gpp_update_location (ctx->self, 0, 0);

    g_simple_async_result_take_error (ctx->result, error);
}

gboolean
mm_iface_modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                                GAsyncResult *res,
                                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void run_registration_checks_ready (MMIfaceModem3gpp *self,
                                           GAsyncResult *res,
                                           RegisterInNetworkContext *ctx);

static gboolean
run_registration_checks_again (RegisterInNetworkContext *ctx)
{
    /* Get fresh registration state */
    mm_iface_modem_3gpp_run_registration_checks (
        ctx->self,
        (GAsyncReadyCallback)run_registration_checks_ready,
        ctx);
    return FALSE;
}

static void
run_registration_checks_ready (MMIfaceModem3gpp *self,
                               GAsyncResult *res,
                               RegisterInNetworkContext *ctx)
{
    GError *error = NULL;
    RegistrationStateContext *registration_state_context;
    MMModem3gppRegistrationState current_registration_state;

    mm_iface_modem_3gpp_run_registration_checks_finish (MM_IFACE_MODEM_3GPP (self), res, &error);
    if (error) {
        mm_dbg ("3GPP registration check failed: '%s'", error->message);
        register_in_network_context_failed (ctx, error);
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    registration_state_context = get_registration_state_context (ctx->self);
    current_registration_state = get_consolidated_reg_state (registration_state_context);

    /* If we got a final state and it's denied, we can assume the registration is
     * finished */
    if (current_registration_state == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED) {
        mm_dbg ("Registration denied");
        register_in_network_context_failed (
            ctx,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED));
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* If we got registered, end registration checks */
    if (current_registration_state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        current_registration_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        /* Request immediate access tech update */
        mm_iface_modem_refresh_access_technologies (MM_IFACE_MODEM (ctx->self));
        mm_dbg ("Modem is currently registered in a 3GPP network");
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* Don't spend too much time waiting to get registered */
    if (g_timer_elapsed (ctx->timer, NULL) > ctx->max_registration_time) {
        mm_dbg ("3GPP registration check timed out");
        register_in_network_context_failed (
            ctx,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT));
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* If we're still waiting for automatic registration to complete or
     * fail, check again in a few seconds.
     *
     * This 3s timeout will catch results from automatic registrations as
     * well.
     */
    mm_dbg ("Modem not yet registered in a 3GPP network... will recheck soon");
    g_timeout_add_seconds (3, (GSourceFunc)run_registration_checks_again, ctx);
}

static void
register_in_network_ready (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           RegisterInNetworkContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish (self, res, &error)) {
        /* Propagate error when trying to lock to network */
        register_in_network_context_failed (ctx, error);
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* Now try to gather current registration status until we're registered or
     * the time goes off */
    mm_iface_modem_3gpp_run_registration_checks (
        self,
        (GAsyncReadyCallback)run_registration_checks_ready,
        ctx);
}

void
mm_iface_modem_3gpp_register_in_network (MMIfaceModem3gpp *self,
                                         const gchar *operator_id,
                                         guint max_registration_time,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    RegisterInNetworkContext *ctx;
    const gchar *current_operator_code;
    RegistrationStateContext *registration_state_context;
    GError *error = NULL;

    ctx = g_slice_new0 (RegisterInNetworkContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_3gpp_register_in_network);
    ctx->operator_id = (operator_id && operator_id[0]) ? g_strdup (operator_id) : NULL;
    ctx->max_registration_time = max_registration_time;
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* Validate input MCC/MNC */
    if (ctx->operator_id && !mm_3gpp_parse_operator_id (ctx->operator_id, NULL, NULL, &error)) {
        g_assert (error != NULL);
        g_simple_async_result_take_error (ctx->result, error);
        register_in_network_context_complete_and_free (ctx);
        return;
    }

    /* Get registration state context */
    registration_state_context = get_registration_state_context (self);

    /* (Try to) cancel previous registration request */
    if (registration_state_context->pending_registration_cancellable) {
        g_cancellable_cancel (registration_state_context->pending_registration_cancellable);
        g_clear_object (&registration_state_context->pending_registration_cancellable);
    }

    current_operator_code = mm_gdbus_modem3gpp_get_operator_code (ctx->skeleton);

    /* Manual registration requested? */
    if (ctx->operator_id) {
        /* If already registered with the requested operator, we're done */
        if (current_operator_code &&
            g_str_equal (current_operator_code, ctx->operator_id)) {
            registration_state_context->manual_registration = TRUE;
            mm_dbg ("Already registered in selected network '%s'...",
                    current_operator_code);
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
            register_in_network_context_complete_and_free (ctx);
            return;
        }

        /* Manual registration to a new operator required */
        mm_dbg ("Launching manual network registration (%s)...",
                ctx->operator_id);
        registration_state_context->manual_registration = TRUE;
    }
    /* Automatic registration requested? */
    else {
        /* If the modem is already registered and the last time it was asked
         * automatic registration, we're done */
        if (current_operator_code &&
            !registration_state_context->manual_registration) {
            mm_dbg ("Already registered in network '%s',"
                    " automatic registration not launched...",
                    current_operator_code);
            g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
            register_in_network_context_complete_and_free (ctx);
            return;
        }

        /* Automatic registration to a new operator requested */
        mm_dbg ("Launching automatic network registration...");
        registration_state_context->manual_registration = FALSE;
    }

    ctx->cancellable = g_cancellable_new ();

    /* Keep an accessible reference to the cancellable, so that we can cancel
     * previous request when needed */
    registration_state_context->pending_registration_cancellable =
        g_object_ref (ctx->cancellable);

    ctx->timer = g_timer_new ();
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network (
        self,
        ctx->operator_id,
        ctx->cancellable,
        (GAsyncReadyCallback)register_in_network_ready,
        ctx);
}

typedef struct {
    MmGdbusModem3gpp *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp *self;
    gchar *operator_id;
} HandleRegisterContext;

static void
handle_register_context_free (HandleRegisterContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->operator_id);
    g_free (ctx);
}

static void
handle_register_ready (MMIfaceModem3gpp *self,
                       GAsyncResult *res,
                       HandleRegisterContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish (self, res,&error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem3gpp_complete_register (ctx->skeleton, ctx->invocation);

    handle_register_context_free (ctx);
}

static void
handle_register_auth_ready (MMBaseModem *self,
                            GAsyncResult *res,
                            HandleRegisterContext *ctx)
{
    MMModemState modem_state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_register_context_free (ctx);
        return;
    }

    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network != NULL);
    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish != NULL);

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_FAILED:
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        /* We should never have such request (interface wasn't exported yet) */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_INITIALIZING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "device not fully initialized yet");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        mm_iface_modem_3gpp_register_in_network (MM_IFACE_MODEM_3GPP (self),
                                                 ctx->operator_id,
                                                 60,
                                                 (GAsyncReadyCallback)handle_register_ready,
                                                 ctx);
        return;

    case MM_MODEM_STATE_DISABLING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "not yet enabled");
        break;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "modem is connected");
        break;
    }

    handle_register_context_free (ctx);
}

static gboolean
handle_register (MmGdbusModem3gpp *skeleton,
                 GDBusMethodInvocation *invocation,
                 const gchar *operator_id,
                 MMIfaceModem3gpp *self)
{
    HandleRegisterContext *ctx;

    ctx = g_new (HandleRegisterContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->operator_id = g_strdup (operator_id);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_register_auth_ready,
                             ctx);

    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp *self;
} HandleScanContext;

static void
handle_scan_context_free (HandleScanContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static GVariant *
scan_networks_build_result (GList *info_list)
{
    GList *l;
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

    for (l = info_list; l; l = g_list_next (l)) {
        MM3gppNetworkInfo *info = l->data;

        if (!info->operator_code) {
            g_warn_if_reached ();
            continue;
        }

        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));

        g_variant_builder_add (&builder, "{sv}",
                               "operator-code", g_variant_new_string (info->operator_code));
        g_variant_builder_add (&builder, "{sv}",
                               "status", g_variant_new_uint32 (info->status));
        g_variant_builder_add (&builder, "{sv}",
                               "access-technology", g_variant_new_uint32 (info->access_tech));
        if (info->operator_long)
            g_variant_builder_add (&builder, "{sv}",
                                   "operator-long", g_variant_new_string (info->operator_long));
        if (info->operator_short)
            g_variant_builder_add (&builder, "{sv}",
                                   "operator-short", g_variant_new_string (info->operator_short));
        g_variant_builder_close (&builder);
    }

    return g_variant_ref (g_variant_builder_end (&builder));
}

static void
handle_scan_ready (MMIfaceModem3gpp *self,
                   GAsyncResult *res,
                   HandleScanContext *ctx)
{
    GError *error = NULL;
    GList *info_list;

    info_list = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks_finish (self, res, &error);
    if (error)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        GVariant *dict_array;

        dict_array = scan_networks_build_result (info_list);
        mm_gdbus_modem3gpp_complete_scan (ctx->skeleton,
                                          ctx->invocation,
                                          dict_array);
        g_variant_unref (dict_array);
    }

    mm_3gpp_network_info_list_free (info_list);
    handle_scan_context_free (ctx);
}

static void
handle_scan_auth_ready (MMBaseModem *self,
                        GAsyncResult *res,
                        HandleScanContext *ctx)
{
    MMModemState modem_state;
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_scan_context_free (ctx);
        return;
    }

    /* If scanning is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks ||
        !MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot scan networks: operation not supported");
        handle_scan_context_free (ctx);
        return;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_FAILED:
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        /* We should never have such request (interface wasn't exported yet) */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_INITIALIZING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot scan networks: "
                                               "device not fully initialized yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot scan networks: not enabled yet");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks (
            MM_IFACE_MODEM_3GPP (self),
            (GAsyncReadyCallback)handle_scan_ready,
            ctx);
        return;
    }

    handle_scan_context_free (ctx);
}

static gboolean
handle_scan (MmGdbusModem3gpp *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModem3gpp *self)
{
    HandleScanContext *ctx;

    ctx = g_new (HandleScanContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_scan_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

gboolean
mm_iface_modem_3gpp_run_registration_checks_finish (MMIfaceModem3gpp *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_registration_checks_finish != NULL);
    return MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_registration_checks_finish (self, res, error);
}

void
mm_iface_modem_3gpp_run_registration_checks (MMIfaceModem3gpp *self,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    gboolean cs_supported = FALSE;
    gboolean ps_supported = FALSE;
    gboolean eps_supported = FALSE;

    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_registration_checks != NULL);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                  MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                  MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &eps_supported,
                  NULL);

    mm_dbg ("Running registration checks (CS: '%s', PS: '%s', EPS: '%s')",
            cs_supported ? "yes" : "no",
            ps_supported ? "yes" : "no",
            eps_supported ? "yes" : "no");

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_registration_checks (self,
                                                                       cs_supported,
                                                                       ps_supported,
                                                                       eps_supported,
                                                                       callback,
                                                                       user_data);
}

/*****************************************************************************/

typedef struct {
    MMIfaceModem3gpp *self;
    MmGdbusModem3gpp *skeleton;
    GSimpleAsyncResult *result;
    gboolean operator_code_loaded;
    gboolean operator_name_loaded;
    gboolean subscription_state_loaded;
} ReloadCurrentRegistrationInfoContext;

static void
reload_current_registration_info_context_complete_and_free (ReloadCurrentRegistrationInfoContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_object_unref (ctx->self);
    g_slice_free (ReloadCurrentRegistrationInfoContext, ctx);
}

gboolean
mm_iface_modem_3gpp_reload_current_registration_info_finish (MMIfaceModem3gpp *self,
                                                             GAsyncResult *res,
                                                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void reload_current_registration_info_context_step (ReloadCurrentRegistrationInfoContext *ctx);

static void
load_operator_name_ready (MMIfaceModem3gpp *self,
                          GAsyncResult *res,
                          ReloadCurrentRegistrationInfoContext *ctx)
{
    GError *error = NULL;
    gchar *str;

    str = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't load Operator Name: '%s'", error->message);
        g_error_free (error);
    }

    if (ctx->skeleton)
        mm_gdbus_modem3gpp_set_operator_name (ctx->skeleton, str);
    g_free (str);

    ctx->operator_name_loaded = TRUE;
    reload_current_registration_info_context_step (ctx);
}

static gboolean
parse_mcc_mnc (const gchar *mccmnc,
               guint *mcc_out,
               guint *mnc_out)
{
    guint mccmnc_len;
    gchar mcc[4] = { 0, 0, 0, 0 };
    gchar mnc[4] = { 0, 0, 0, 0 };

    mccmnc_len = (mccmnc ? strlen (mccmnc) : 0);
    if (mccmnc_len != 5 &&
        mccmnc_len != 6) {
        mm_dbg ("Unexpected MCC/MNC string '%s'", mccmnc);
        return FALSE;
    }

    memcpy (mcc, mccmnc, 3);
    /* Not all modems report 6-digit MNCs */
    memcpy (mnc, mccmnc + 3, 2);
    if (mccmnc_len == 6)
        mnc[2] = mccmnc[5];

    *mcc_out = atoi (mcc);
    *mnc_out = atoi (mnc);

    return TRUE;
}

static void
load_operator_code_ready (MMIfaceModem3gpp *self,
                          GAsyncResult *res,
                          ReloadCurrentRegistrationInfoContext *ctx)
{
    GError *error = NULL;
    gchar *str;

    str = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't load Operator Code: '%s'", error->message);
        g_error_free (error);
    }

    if (ctx->skeleton)
        mm_gdbus_modem3gpp_set_operator_code (ctx->skeleton, str);

    /* If we also implement the location interface, update the 3GPP location */
    if (str && MM_IS_IFACE_MODEM_LOCATION (self)) {
        guint mcc = 0;
        guint mnc = 0;

        if (parse_mcc_mnc (str, &mcc, &mnc))
            mm_iface_modem_location_3gpp_update_mcc_mnc (MM_IFACE_MODEM_LOCATION (self), mcc, mnc);
    }
    g_free (str);

    ctx->operator_code_loaded = TRUE;
    reload_current_registration_info_context_step (ctx);
}

static void
load_subscription_state_ready (MMIfaceModem3gpp *self,
                               GAsyncResult *res,
                               ReloadCurrentRegistrationInfoContext *ctx)
{
    GError *error = NULL;
    MMModem3gppSubscriptionState subscription_state = MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN;

    subscription_state = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_subscription_state_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't load Subscription State: '%s'", error->message);
        g_error_free (error);
    }

    if (ctx->skeleton)
        mm_gdbus_modem3gpp_set_subscription_state (ctx->skeleton, subscription_state);

    ctx->subscription_state_loaded = TRUE;
    reload_current_registration_info_context_step (ctx);
}


static void
reload_current_registration_info_context_step (ReloadCurrentRegistrationInfoContext *ctx)
{
    if (!ctx->operator_code_loaded) {
        /* Launch operator code update */
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_operator_code (
            ctx->self,
            (GAsyncReadyCallback)load_operator_code_ready,
            ctx);
        return;
    }

    if (!ctx->operator_name_loaded) {
        /* Launch operator name update */
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_operator_name (
            ctx->self,
            (GAsyncReadyCallback)load_operator_name_ready,
            ctx);
        return;
    }

    if (!ctx->subscription_state_loaded) {
        /* Launch subscription state update */
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_subscription_state (
            ctx->self,
            (GAsyncReadyCallback)load_subscription_state_ready,
            ctx);
        return;
    }

    /* If all are loaded, all done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    reload_current_registration_info_context_complete_and_free (ctx);
}

void
mm_iface_modem_3gpp_reload_current_registration_info (MMIfaceModem3gpp *self,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data)
{
    ReloadCurrentRegistrationInfoContext *ctx;

    ctx = g_slice_new0 (ReloadCurrentRegistrationInfoContext);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_3gpp_reload_current_registration_info);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        reload_current_registration_info_context_complete_and_free (ctx);
        return;
    }

    ctx->operator_code_loaded = !(MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code &&
                                  MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code_finish);
    if (ctx->operator_code_loaded) {
        mm_gdbus_modem3gpp_set_operator_code (ctx->skeleton, NULL);
        if (MM_IS_IFACE_MODEM_LOCATION (self))
            mm_iface_modem_location_3gpp_update_mcc_mnc (MM_IFACE_MODEM_LOCATION (self), 0, 0);
    }

    ctx->operator_name_loaded = !(MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name &&
                                  MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name_finish);
    if (ctx->operator_name_loaded)
        mm_gdbus_modem3gpp_set_operator_name (ctx->skeleton, NULL);

    ctx->subscription_state_loaded = !(MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_subscription_state &&
                                       MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_subscription_state_finish);
    if (ctx->subscription_state_loaded)
        mm_gdbus_modem3gpp_set_subscription_state (ctx->skeleton, MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN);

    reload_current_registration_info_context_step (ctx);
}

void
mm_iface_modem_3gpp_clear_current_operator (MMIfaceModem3gpp *self)
{
    MmGdbusModem3gpp *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    mm_gdbus_modem3gpp_set_operator_code (skeleton, NULL);
    mm_gdbus_modem3gpp_set_operator_name (skeleton, NULL);
    if (MM_IS_IFACE_MODEM_LOCATION (self))
        mm_iface_modem_location_3gpp_update_mcc_mnc (MM_IFACE_MODEM_LOCATION (self), 0, 0);
}

static void
clear_subscription_state (MMIfaceModem3gpp *self)
{
    MmGdbusModem3gpp *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;
    mm_gdbus_modem3gpp_set_subscription_state (skeleton, MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN);
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_update_access_technologies (MMIfaceModem3gpp *self,
                                                MMModemAccessTechnology access_tech)
{
    MMModem3gppRegistrationState state;
    RegistrationStateContext *ctx;

    ctx = get_registration_state_context (self);
    g_assert (ctx);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  NULL);

    /* Even if registration state didn't change, report access technology,
     * but only if something valid to report */
    if (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING ||
        ctx->reloading_registration_info) {
        if (access_tech != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                       access_tech,
                                                       MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
    } else
        mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                   MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,
                                                   MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK);
}

void
mm_iface_modem_3gpp_update_location (MMIfaceModem3gpp *self,
                                     gulong location_area_code,
                                     gulong cell_id)
{
    MMModem3gppRegistrationState state;
    RegistrationStateContext *ctx;

    ctx = get_registration_state_context (self);
    g_assert (ctx);

    if (!MM_IS_IFACE_MODEM_LOCATION (self))
        return;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  NULL);

    /* Even if registration state didn't change, report access technology or
     * location updates, but only if something valid to report. For the case
     * where we're registering (loading current registration info after a state
     * change to registered), we also allow LAC/CID updates. */
    if (ctx->reloading_registration_info ||
        state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        if (location_area_code > 0 && cell_id > 0)
            mm_iface_modem_location_3gpp_update_lac_ci (MM_IFACE_MODEM_LOCATION (self),
                                                        location_area_code,
                                                        cell_id);
    } else
        mm_iface_modem_location_3gpp_clear (MM_IFACE_MODEM_LOCATION (self));
}

/*****************************************************************************/

static void
update_registration_reload_current_registration_info_ready (MMIfaceModem3gpp *self,
                                                            GAsyncResult *res,
                                                            gpointer user_data)
{
    MMModem3gppRegistrationState new_state;
    RegistrationStateContext *ctx;

    new_state = GPOINTER_TO_UINT (user_data);

    mm_info ("Modem %s: 3GPP Registration state changed (registering -> %s)",
             g_dbus_object_get_object_path (G_DBUS_OBJECT (self)),
             mm_modem_3gpp_registration_state_get_string (new_state));

    /* The property in the interface is bound to the property
     * in the skeleton, so just updating here is enough */
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, new_state,
                  NULL);

    mm_iface_modem_update_subsystem_state (MM_IFACE_MODEM (self),
                                           SUBSYSTEM_3GPP,
                                           MM_MODEM_STATE_REGISTERED,
                                           MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);

    ctx = get_registration_state_context (self);
    ctx->reloading_registration_info = FALSE;
}

static void
update_non_registered_state (MMIfaceModem3gpp *self,
                             MMModem3gppRegistrationState old_state,
                             MMModem3gppRegistrationState new_state)
{
    /* Not registered neither in home nor roaming network */
    mm_iface_modem_3gpp_clear_current_operator (self);

    /* The subscription state can be computed in two ways: a) via PCO which is
     * sent by the carrier during registration or b) by looking at the
     * registration reject error code.  If b), we want to make sure we
     * preserve the subscription state */
    if (old_state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        old_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        clear_subscription_state (self);

    /* The property in the interface is bound to the property
     * in the skeleton, so just updating here is enough */
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, new_state,
                  NULL);

    mm_iface_modem_update_subsystem_state (
        MM_IFACE_MODEM (self),
        SUBSYSTEM_3GPP,
        (new_state == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING ?
         MM_MODEM_STATE_SEARCHING :
         MM_MODEM_STATE_ENABLED),
        MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
}

static void
update_registration_state (MMIfaceModem3gpp *self,
                           MMModem3gppRegistrationState new_state,
                           gboolean deferrable)
{
    MMModem3gppRegistrationState old_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
    RegistrationStateContext *ctx;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &old_state,
                  NULL);

    ctx = get_registration_state_context (self);
    g_assert (ctx);

    /* Only set new state if different */
    if (new_state == old_state)
        return;

    if (new_state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        new_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        /* If already reloading registration info, skip it */
        if (ctx->reloading_registration_info)
            return;

        mm_info ("Modem %s: 3GPP Registration state changed (%s -> registering)",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (self)),
                 mm_modem_3gpp_registration_state_get_string (old_state));

        /* Reload current registration info. ONLY update the state to REGISTERED
         * after having loaded operator code/name/subscription state */
        ctx->reloading_registration_info = TRUE;
        mm_iface_modem_3gpp_reload_current_registration_info (
            self,
            (GAsyncReadyCallback)update_registration_reload_current_registration_info_ready,
            GUINT_TO_POINTER (new_state));
        return;
    }

    mm_info ("Modem %s: 3GPP Registration state changed (%s -> %s)",
             g_dbus_object_get_object_path (G_DBUS_OBJECT (self)),
             mm_modem_3gpp_registration_state_get_string (old_state),
             mm_modem_3gpp_registration_state_get_string (new_state));

    update_non_registered_state (self, old_state, new_state);
}

void
mm_iface_modem_3gpp_update_cs_registration_state (MMIfaceModem3gpp *self,
                                                  MMModem3gppRegistrationState state)
{
    RegistrationStateContext *ctx;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    ctx = get_registration_state_context (self);
    ctx->cs = state;
    update_registration_state (self, get_consolidated_reg_state (ctx), TRUE);
}

void
mm_iface_modem_3gpp_update_ps_registration_state (MMIfaceModem3gpp *self,
                                                  MMModem3gppRegistrationState state)
{
    RegistrationStateContext *ctx;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    ctx = get_registration_state_context (self);
    ctx->ps = state;
    update_registration_state (self, get_consolidated_reg_state (ctx), TRUE);
}

void
mm_iface_modem_3gpp_update_eps_registration_state (MMIfaceModem3gpp *self,
                                                   MMModem3gppRegistrationState state)
{
    RegistrationStateContext *ctx;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    ctx = get_registration_state_context (self);
    ctx->eps = state;
    update_registration_state (self, get_consolidated_reg_state (ctx), TRUE);
}

void
mm_iface_modem_3gpp_update_subscription_state (MMIfaceModem3gpp *self,
                                               MMModem3gppSubscriptionState state)
{
    MmGdbusModem3gpp *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (skeleton) {
        mm_dbg ("Setting subscription state to: %s", mm_modem_3gpp_subscription_state_get_string (state));
        mm_gdbus_modem3gpp_set_subscription_state (skeleton, state);
        g_object_unref (skeleton);
    }
}

/*****************************************************************************/

typedef struct {
    guint timeout_source;
    gboolean running;
} RegistrationCheckContext;

static void
registration_check_context_free (RegistrationCheckContext *ctx)
{
    if (ctx->timeout_source)
        g_source_remove (ctx->timeout_source);
    g_free (ctx);
}

static void
periodic_registration_checks_ready (MMIfaceModem3gpp *self,
                                    GAsyncResult *res)
{
    RegistrationCheckContext *ctx;
    GError *error = NULL;

    mm_iface_modem_3gpp_run_registration_checks_finish (self, res, &error);
    if (error) {
        mm_dbg ("Couldn't refresh 3GPP registration status: '%s'", error->message);
        g_error_free (error);
    }

    /* Remove the running tag */
    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);
    if (ctx)
        ctx->running = FALSE;
}

static gboolean
periodic_registration_check (MMIfaceModem3gpp *self)
{
    RegistrationCheckContext *ctx;

    /* Only launch a new one if not one running already */
    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);
    if (!ctx->running) {
        ctx->running = TRUE;
        mm_iface_modem_3gpp_run_registration_checks (
            self,
            (GAsyncReadyCallback)periodic_registration_checks_ready,
            NULL);
    }
    return TRUE;
}

static void
periodic_registration_check_disable (MMIfaceModem3gpp *self)
{
    if (G_UNLIKELY (!registration_check_context_quark))
        registration_check_context_quark = (g_quark_from_static_string (
                                                REGISTRATION_CHECK_CONTEXT_TAG));

    /* Overwriting the data will free the previous context */
    g_object_set_qdata (G_OBJECT (self),
                        registration_check_context_quark,
                        NULL);

    mm_dbg ("Periodic 3GPP registration checks disabled");
}

static void
periodic_registration_check_enable (MMIfaceModem3gpp *self)
{
    RegistrationCheckContext *ctx;

    if (G_UNLIKELY (!registration_check_context_quark))
        registration_check_context_quark = (g_quark_from_static_string (
                                                REGISTRATION_CHECK_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);

    /* If context is already there, we're already enabled */
    if (ctx)
        return;

    /* Create context and keep it as object data */
    mm_dbg ("Periodic 3GPP registration checks enabled");
    ctx = g_new0 (RegistrationCheckContext, 1);
    ctx->timeout_source = g_timeout_add_seconds (REGISTRATION_CHECK_TIMEOUT_SEC,
                                                 (GSourceFunc)periodic_registration_check,
                                                 self);
    g_object_set_qdata_full (G_OBJECT (self),
                             registration_check_context_quark,
                             ctx,
                             (GDestroyNotify)registration_check_context_free);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS,
    DISABLING_STEP_DISABLE_UNSOLICITED_REGISTRATION_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_REGISTRATION_STATE,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModem3gpp *self;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_disable_finish (MMIfaceModem3gpp *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME,DISPLAY)                               \
    static void                                                         \
    NAME##_ready (MMIfaceModem3gpp *self,                               \
                  GAsyncResult *res,                                    \
                  DisablingContext *ctx)                                \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            mm_dbg ("Couldn't %s: '%s'", DISPLAY, error->message);      \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_disabling_step (ctx);                                 \
    }

VOID_REPLY_READY_FN (cleanup_unsolicited_events,
                     "cleanup unsolicited events")
VOID_REPLY_READY_FN (disable_unsolicited_events,
                     "disable unsolicited events")
VOID_REPLY_READY_FN (cleanup_unsolicited_registration_events,
                     "cleanup unsolicited registration events")
VOID_REPLY_READY_FN (disable_unsolicited_registration_events,
                     "disable unsolicited registration events")

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS:
        /* Disable periodic registration checks, if they were set */
        periodic_registration_check_disable (ctx->self);
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_UNSOLICITED_REGISTRATION_EVENTS: {
        gboolean cs_supported = FALSE;
        gboolean ps_supported = FALSE;
        gboolean eps_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                      MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &eps_supported,
                      NULL);

        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_registration_events (
                ctx->self,
                cs_supported,
                ps_supported,
                eps_supported,
                (GAsyncReadyCallback)disable_unsolicited_registration_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }

    case DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration_events (
                ctx->self,
                (GAsyncReadyCallback)cleanup_unsolicited_registration_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_REGISTRATION_STATE:
        update_registration_state (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, FALSE);
        mm_iface_modem_3gpp_update_access_technologies (ctx->self, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_3gpp_update_location (ctx->self, 0, 0);
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
mm_iface_modem_3gpp_disable (MMIfaceModem3gpp *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_3gpp_disable);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        disabling_context_complete_and_free (ctx);
        return;
    }

    interface_disabling_step (ctx);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModem3gpp *self;
    EnablingStep step;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MmGdbusModem3gpp *skeleton;
};

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
enabling_context_complete_and_free_if_cancelled (EnablingContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface enabling cancelled");
    enabling_context_complete_and_free (ctx);
    return TRUE;
}

gboolean
mm_iface_modem_3gpp_enable_finish (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                GAsyncResult *res,
                                EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Setting up unsolicited events failed: '%s'", error->message);
        g_error_free (error);

        /* If we get an error setting up unsolicited events, don't even bother trying to
         * enable them. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS + 1;
        interface_enabling_step (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Enabling unsolicited events failed: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_unsolicited_registration_events_ready (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_registration_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Setting up unsolicited registration events failed: '%s'", error->message);
        g_error_free (error);

        /* If we get an error setting up unsolicited events, don't even bother trying to
         * enable them. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS + 1;
        interface_enabling_step (ctx);
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (ctx->self);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
enable_unsolicited_registration_events_ready (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_registration_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Enabling unsolicited registration events failed: '%s'", error->message);
        g_error_free (error);
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (ctx->self);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
interface_enabling_step (EnablingContext *ctx)
{
    /* Don't run new steps if we're cancelled */
    if (enabling_context_complete_and_free_if_cancelled (ctx))
        return;

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events (
                ctx->self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration_events (
                ctx->self,
                (GAsyncReadyCallback)setup_unsolicited_registration_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS: {
        gboolean cs_supported = FALSE;
        gboolean ps_supported = FALSE;
        gboolean eps_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                      MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &eps_supported,
                      NULL);

        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_registration_events (
                ctx->self,
                cs_supported,
                ps_supported,
                eps_supported,
                (GAsyncReadyCallback)enable_unsolicited_registration_events_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }

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
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    EnablingContext *ctx;

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_3gpp_enable);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Couldn't get interface skeleton");
        enabling_context_complete_and_free (ctx);
        return;
    }

    interface_enabling_step (ctx);
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_IMEI,
    INITIALIZATION_STEP_ENABLED_FACILITY_LOCKS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem3gpp *self;
    MmGdbusModem3gpp *skeleton;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    InitializationStep step;
};

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
initialization_context_complete_and_free_if_cancelled (InitializationContext *ctx)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_CANCELLED,
                                     "Interface initialization cancelled");
    initialization_context_complete_and_free (ctx);
    return TRUE;
}

static void
sim_pin_lock_enabled_cb (MMBaseSim *self,
                         gboolean enabled,
                         MmGdbusModem3gpp *skeleton)
{
    MMModem3gppFacility facilities;

    facilities = mm_gdbus_modem3gpp_get_enabled_facility_locks (skeleton);
    if (enabled)
        facilities |= MM_MODEM_3GPP_FACILITY_SIM;
    else
        facilities &= ~MM_MODEM_3GPP_FACILITY_SIM;

    mm_gdbus_modem3gpp_set_enabled_facility_locks (skeleton, facilities);
}

static void
load_enabled_facility_locks_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   InitializationContext *ctx)
{
    GError *error = NULL;
    MMModem3gppFacility facilities;

    facilities = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_enabled_facility_locks_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_enabled_facility_locks (ctx->skeleton, facilities);

    if (error) {
        mm_warn ("couldn't load facility locks: '%s'", error->message);
        g_error_free (error);
    } else {
        MMBaseSim *sim = NULL;

        /* We loaded the initial list of facility locks; but we do need to update
         * the SIM PIN lock status when that changes. We'll connect to the signal
         * which notifies about such update. There is no need to ref self as the
         * SIM itself is an object which exists as long as self exists. */
        g_object_get (self,
                      MM_IFACE_MODEM_SIM, &sim,
                      NULL);
        g_signal_connect (sim,
                          MM_BASE_SIM_PIN_LOCK_ENABLED,
                          G_CALLBACK (sim_pin_lock_enabled_cb),
                          ctx->skeleton);
        g_object_unref (sim);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
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
    /* Don't run new steps if we're cancelled */
    if (initialization_context_complete_and_free_if_cancelled (ctx))
        return;

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

    case INITIALIZATION_STEP_ENABLED_FACILITY_LOCKS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_enabled_facility_locks &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_enabled_facility_locks_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_enabled_facility_locks (
                ctx->self,
                (GAsyncReadyCallback)load_enabled_facility_locks_ready,
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
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_3gpp_initialize (MMIfaceModem3gpp *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MmGdbusModem3gpp *skeleton = NULL;
    InitializationContext *ctx;

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
        mm_gdbus_modem3gpp_set_subscription_state (skeleton, MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN);

        /* Bind our RegistrationState property */
        g_object_bind_property (self, MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                                skeleton, "registration-state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON, skeleton,
                      NULL);

        /* If the modem is *only* LTE, we assume that CS network is not
         * supported */
        if (mm_iface_modem_is_3gpp_lte_only (MM_IFACE_MODEM (self))) {
            mm_dbg ("Modem is LTE-only, assuming CS network is not supported");
            g_object_set (self,
                          MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, FALSE,
                          NULL);
        }
    }

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_3gpp_initialize);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    /* Perform async initialization here */
    interface_initialization_step (ctx);
}

void
mm_iface_modem_3gpp_shutdown (MMIfaceModem3gpp *self)
{
    /* Remove RegistrationCheckContext object to make sure any pending
     * invocation of periodic_registration_check is cancelled before the
     * DBus skeleton is removed. */
    if (G_LIKELY (registration_check_context_quark))
        g_object_set_qdata (G_OBJECT (self),
                            registration_check_context_quark,
                            NULL);

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

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED,
                               "CS network supported",
                               "Whether the modem works in the CS network",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED,
                               "PS network supported",
                               "Whether the modem works in the PS network",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED,
                               "EPS network supported",
                               "Whether the modem works in the EPS network",
                               FALSE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_flags (MM_IFACE_MODEM_3GPP_IGNORED_FACILITY_LOCKS,
                             "Ignored locks",
                             "Ignored facility locks",
                             MM_TYPE_MODEM_3GPP_FACILITY,
                             MM_MODEM_3GPP_FACILITY_NONE,
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
