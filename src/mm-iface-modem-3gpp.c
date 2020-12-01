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

#define SUBSYSTEM_3GPP "3gpp"

/* When comparing EPS bearer settings take into account that PASSWORD may not always
 * be readable, and apply very loose matching for all fields. Also, some implementations
 * may allow configuring roaming allowance in the initial EPS bearer, but that is also
 * not common. */
#define MM_BEARER_PROPERTIES_CMP_FLAGS_EPS              \
    (MM_BEARER_PROPERTIES_CMP_FLAGS_LOOSE |             \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PASSWORD |       \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_ALLOW_ROAMING |  \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_RM_PROTOCOL)

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "iface-modem-3gpp-private-tag"
static GQuark private_quark;

typedef struct {
    /* Registration state */
    MMModem3gppRegistrationState  state_cs;
    MMModem3gppRegistrationState  state_ps;
    MMModem3gppRegistrationState  state_eps;
    MMModem3gppRegistrationState  state_5gs;
    gboolean                      manual_registration;
    gchar                        *manual_registration_operator_id;
    GCancellable                 *pending_registration_cancellable;
    gboolean                      reloading_registration_info;
    /* Registration checks */
    guint    check_timeout_source;
    gboolean check_running;
} Private;

static void
private_free (Private *priv)
{
    g_free (priv->manual_registration_operator_id);
    if (priv->pending_registration_cancellable) {
        g_cancellable_cancel (priv->pending_registration_cancellable);
        g_object_unref (priv->pending_registration_cancellable);
    }
    if (priv->check_timeout_source)
        g_source_remove (priv->check_timeout_source);
    g_slice_free (Private, priv);
}

static Private *
get_private (MMIfaceModem3gpp *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);
        priv->state_cs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
        priv->state_ps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
        priv->state_eps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
        priv->state_5gs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

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

    g_object_unref (skeleton);
}

/*****************************************************************************/

#define REG_STATE_IS_REGISTERED(state)                                    \
    (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||                    \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING ||                 \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME_SMS_ONLY ||           \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_SMS_ONLY ||        \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME_CSFB_NOT_PREFERRED || \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_CSFB_NOT_PREFERRED)

#define REG_STATE_IS_UNKNOWN_IDLE_DENIED(state)                           \
    (state == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN ||                 \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_IDLE ||                    \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED)

static MMModem3gppRegistrationState
get_consolidated_reg_state (MMIfaceModem3gpp *self)
{
    Private *priv;

    priv = get_private (self);

    /* Some devices (Blackberries for example) will respond to +CGREG, but
     * return ERROR for +CREG, probably because their firmware is just stupid.
     * So here we prefer the +CREG response, but if we never got a successful
     * +CREG response, we'll take +CGREG instead.
     */
    if (priv->state_cs == MM_MODEM_3GPP_REGISTRATION_STATE_HOME || priv->state_cs == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return priv->state_cs;
    if (priv->state_ps == MM_MODEM_3GPP_REGISTRATION_STATE_HOME || priv->state_ps == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return priv->state_ps;
    if (priv->state_eps == MM_MODEM_3GPP_REGISTRATION_STATE_HOME || priv->state_eps == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return priv->state_eps;
    if (priv->state_5gs == MM_MODEM_3GPP_REGISTRATION_STATE_HOME || priv->state_5gs == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return priv->state_5gs;

    /* Searching? */
    if (priv->state_cs  == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING ||
        priv->state_ps  == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING ||
        priv->state_eps == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING ||
        priv->state_5gs == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING;

    /* If at least one state is DENIED and the others are UNKNOWN or IDLE, use DENIED */
    if ((priv->state_cs == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED ||
         priv->state_ps == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED ||
         priv->state_eps == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED ||
         priv->state_5gs == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED) &&
        REG_STATE_IS_UNKNOWN_IDLE_DENIED (priv->state_cs) &&
        REG_STATE_IS_UNKNOWN_IDLE_DENIED (priv->state_ps) &&
        REG_STATE_IS_UNKNOWN_IDLE_DENIED (priv->state_eps) &&
        REG_STATE_IS_UNKNOWN_IDLE_DENIED (priv->state_5gs))
        return MM_MODEM_3GPP_REGISTRATION_STATE_DENIED;

    /* Emergency services? */
    if (priv->state_cs  == MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY ||
        priv->state_ps  == MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY ||
        priv->state_eps == MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY ||
        priv->state_5gs == MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY)
        return MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY;

    /* Support for additional registration states reported when on LTE/5GNR.
     *
     * For example, we may see the modem registered in LTE (EPS==HOME), and we
     * may get "SMS only" reported for CS.
     *
     * We give these states a very low priority w.r.t. the other ones as they
     * are really likely never used (i.e. we would get as consolidated the LTE
     * registration state, not the CS fall back state).
     *
     * We also warn in that case, because ideally we should always report the
     * LTE registration state first, not this one.
     */
    if (priv->state_cs == MM_MODEM_3GPP_REGISTRATION_STATE_HOME_SMS_ONLY ||
        priv->state_cs == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_SMS_ONLY ||
        priv->state_cs == MM_MODEM_3GPP_REGISTRATION_STATE_HOME_CSFB_NOT_PREFERRED ||
        priv->state_cs == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_CSFB_NOT_PREFERRED) {
        mm_obj_warn (self, "3GPP CSFB registration state is consolidated: %s",
                     mm_modem_3gpp_registration_state_get_string (priv->state_cs));
        return priv->state_cs;
    }

    /* Idle? */
    if (priv->state_cs  == MM_MODEM_3GPP_REGISTRATION_STATE_IDLE ||
        priv->state_ps  == MM_MODEM_3GPP_REGISTRATION_STATE_IDLE ||
        priv->state_eps == MM_MODEM_3GPP_REGISTRATION_STATE_IDLE ||
        priv->state_5gs == MM_MODEM_3GPP_REGISTRATION_STATE_IDLE)
        return MM_MODEM_3GPP_REGISTRATION_STATE_IDLE;

    return MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
}

/*****************************************************************************/

typedef struct {
    MMIfaceModem3gpp *self;
    MmGdbusModem3gpp *skeleton;
    GCancellable     *cancellable;
    gchar            *operator_id;
    GTimer           *timer;
    guint             max_registration_time;
} RegisterInNetworkContext;

static void
register_in_network_context_free (RegisterInNetworkContext *ctx)
{
    if (ctx->timer)
        g_timer_destroy (ctx->timer);

    if (ctx->cancellable) {
        Private *priv;

        /* Clear our cancellable if still around */
        priv = get_private (ctx->self);
        if (priv->pending_registration_cancellable == ctx->cancellable)
            g_clear_object (&priv->pending_registration_cancellable);
        g_object_unref (ctx->cancellable);
    }

    g_free (ctx->operator_id);
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_object_unref (ctx->self);
    g_slice_free (RegisterInNetworkContext, ctx);
}

static void
register_in_network_context_complete_failed (GTask  *task,
                                             GError *error)
{
    RegisterInNetworkContext *ctx;

    ctx = g_task_get_task_data (task);

    mm_iface_modem_3gpp_update_cs_registration_state (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);
    mm_iface_modem_3gpp_update_ps_registration_state (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);
    mm_iface_modem_3gpp_update_eps_registration_state (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);
    mm_iface_modem_3gpp_update_access_technologies (ctx->self, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
    mm_iface_modem_3gpp_update_location (ctx->self, 0, 0, 0);

    g_task_return_error (task, error);
    g_object_unref (task);
}

gboolean
mm_iface_modem_3gpp_register_in_network_finish (MMIfaceModem3gpp  *self,
                                                GAsyncResult      *res,
                                                GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static gboolean run_registration_checks (GTask *task);

static void
run_registration_checks_ready (MMIfaceModem3gpp *self,
                               GAsyncResult     *res,
                               GTask            *task)
{
    RegisterInNetworkContext     *ctx;
    GError                       *error = NULL;
    MMModem3gppRegistrationState  current_registration_state;

    ctx = g_task_get_task_data (task);

    mm_iface_modem_3gpp_run_registration_checks_finish (MM_IFACE_MODEM_3GPP (self), res, &error);
    if (error) {
        mm_obj_dbg (self, "3GPP registration check failed: %s", error->message);
        register_in_network_context_complete_failed (task, error);
        return;
    }

    current_registration_state = get_consolidated_reg_state (ctx->self);

    /* If we got a final state and it's denied, we can assume the registration is
     * finished */
    if (current_registration_state == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED) {
        mm_obj_dbg (self, "registration denied");
        register_in_network_context_complete_failed (
            task,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED, self));
        return;
    }

    /* If we got registered, end registration checks */
    if (REG_STATE_IS_REGISTERED (current_registration_state)) {
        /* Request immediate access tech and signal update: we may have changed
         * from home to roaming or viceversa, both registered states, so there
         * wouldn't be an explicit refresh triggered from the modem interface as
         * the modem never got un-registered during the sequence. */
        mm_iface_modem_refresh_signal (MM_IFACE_MODEM (ctx->self));
        mm_obj_dbg (self, "currently registered in a 3GPP network");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Don't spend too much time waiting to get registered */
    if (g_timer_elapsed (ctx->timer, NULL) > ctx->max_registration_time) {
        mm_obj_dbg (self, "3GPP registration check timed out");
        register_in_network_context_complete_failed (
            task,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT, self));
        return;
    }

    /* If we're still waiting for automatic registration to complete or
     * fail, check again in a few seconds.
     *
     * This 3s timeout will catch results from automatic registrations as
     * well.
     */
    mm_obj_dbg (self, "not yet registered in a 3GPP network... will recheck soon");
    g_timeout_add_seconds (3, (GSourceFunc)run_registration_checks, task);
}

static gboolean
run_registration_checks (GTask *task)
{
    RegisterInNetworkContext *ctx;

    ctx = g_task_get_task_data (task);

    /* Get fresh registration state */
    mm_iface_modem_3gpp_run_registration_checks (
        ctx->self,
        (GAsyncReadyCallback)run_registration_checks_ready,
        task);
    return G_SOURCE_REMOVE;
}

static void
register_in_network_ready (MMIfaceModem3gpp *self,
                           GAsyncResult     *res,
                           GTask            *task)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish (self, res, &error)) {
        /* Propagate error when trying to lock to network */
        register_in_network_context_complete_failed (task, error);
        return;
    }

    /* Now try to gather current registration status until we're registered or
     * the time goes off */
    run_registration_checks (task);
}

void
mm_iface_modem_3gpp_register_in_network (MMIfaceModem3gpp    *self,
                                         const gchar         *operator_id,
                                         gboolean             force_registration,
                                         guint                max_registration_time,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
    RegisterInNetworkContext     *ctx;
    const gchar                  *current_operator_code;
    GError                       *error = NULL;
    GTask                        *task;
    Private                      *priv;
    MMModem3gppRegistrationState  reg_state;

    priv = get_private (self);

    ctx = g_slice_new0 (RegisterInNetworkContext);
    ctx->self = g_object_ref (self);
    ctx->operator_id = (operator_id && operator_id[0]) ? g_strdup (operator_id) : NULL;
    ctx->max_registration_time = max_registration_time;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)register_in_network_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    /* Validate input MCC/MNC */
    if (ctx->operator_id && !mm_3gpp_parse_operator_id (ctx->operator_id, NULL, NULL, &error)) {
        g_assert (error != NULL);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* (Try to) cancel previous registration request */
    if (priv->pending_registration_cancellable) {
        g_cancellable_cancel (priv->pending_registration_cancellable);
        g_clear_object (&priv->pending_registration_cancellable);
    }

    current_operator_code = mm_gdbus_modem3gpp_get_operator_code (ctx->skeleton);
    reg_state = mm_gdbus_modem3gpp_get_registration_state (ctx->skeleton);

    /* Manual registration requested? */
    if (ctx->operator_id) {
        /* If already registered manually with the requested operator, we're done */
        if (!force_registration &&
            (g_strcmp0 (current_operator_code, ctx->operator_id) == 0) &&
            REG_STATE_IS_REGISTERED (reg_state) &&
            priv->manual_registration) {
            mm_obj_dbg (self, "already registered manually in selected network '%s', manual registration not launched...",
                        current_operator_code);
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }

        /* Manual registration to a new operator required */
        mm_obj_dbg (self, "launching manual network registration (%s)...", ctx->operator_id);
        g_free (priv->manual_registration_operator_id);
        priv->manual_registration_operator_id = g_strdup (ctx->operator_id);
        priv->manual_registration = TRUE;
    }
    /* Automatic registration requested? */
    else {
        /* If the modem is already registered and the last time it was asked
         * automatic registration, we're done */
        if (!force_registration &&
            (current_operator_code || REG_STATE_IS_REGISTERED (reg_state)) &&
            !priv->manual_registration) {
            mm_obj_dbg (self, "already registered automatically in network '%s',"
                        " automatic registration not launched...",
                        current_operator_code);
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }

        /* Automatic registration to a new operator requested */
        mm_obj_dbg (self, "launching automatic network registration...");
        g_clear_pointer (&priv->manual_registration_operator_id, g_free);
        priv->manual_registration = FALSE;
    }

    ctx->cancellable = g_cancellable_new ();

    /* Keep an accessible reference to the cancellable, so that we can cancel
     * previous request when needed */
    priv->pending_registration_cancellable = g_object_ref (ctx->cancellable);

    ctx->timer = g_timer_new ();
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network (
        self,
        ctx->operator_id,
        ctx->cancellable,
        (GAsyncReadyCallback)register_in_network_ready,
        task);
}

/*****************************************************************************/
/* Request to reregister using the last settings */

#define REREGISTER_IN_NETWORK_TIMEOUT 120

gboolean
mm_iface_modem_3gpp_reregister_in_network_finish (MMIfaceModem3gpp  *self,
                                                  GAsyncResult      *res,
                                                  GError           **error)
{
    return mm_iface_modem_3gpp_register_in_network_finish (self, res, error);
}

void
mm_iface_modem_3gpp_reregister_in_network (MMIfaceModem3gpp    *self,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
    Private *priv;

    /* Relaunch registration using the last used settings */
    priv = get_private (self);
    mm_iface_modem_3gpp_register_in_network (self,
                                             priv->manual_registration_operator_id,
                                             TRUE, /* if already registered with same settings, force re-registration */
                                             REREGISTER_IN_NETWORK_TIMEOUT,
                                             callback,
                                             user_data);
}


/*****************************************************************************/

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

    if (!mm_iface_modem_3gpp_register_in_network_finish (self, res, &error))
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
                                                 FALSE, /* if already registered with same settings, do nothing */
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

    default:
        g_assert_not_reached ();
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

    default:
        g_assert_not_reached ();
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

typedef struct {
    MmGdbusModem3gpp              *skeleton;
    GDBusMethodInvocation         *invocation;
    MMIfaceModem3gpp              *self;
    MMModem3gppEpsUeModeOperation  mode;
} HandleSetEpsUeModeOperationContext;

static void
handle_set_eps_ue_mode_operation_context_free (HandleSetEpsUeModeOperationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetEpsUeModeOperationContext, ctx);
}

static void
after_set_load_eps_ue_mode_operation_ready (MMIfaceModem3gpp                   *self,
                                            GAsyncResult                       *res,
                                            HandleSetEpsUeModeOperationContext *ctx)
{
    MMModem3gppEpsUeModeOperation  uemode;
    GError                        *error = NULL;

    uemode = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_eps_ue_mode_operation_finish (self, res, &error);
    if (error) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    if (uemode != ctx->mode) {
        g_dbus_method_invocation_return_error_literal (ctx->invocation,
                                                       MM_CORE_ERROR,
                                                       MM_CORE_ERROR_FAILED,
                                                       "UE mode of operation for EPS wasn't updated");
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    mm_gdbus_modem3gpp_set_eps_ue_mode_operation (ctx->skeleton, uemode);
    mm_gdbus_modem3gpp_complete_set_eps_ue_mode_operation (ctx->skeleton, ctx->invocation);
    handle_set_eps_ue_mode_operation_context_free (ctx);
}

static void
handle_set_eps_ue_mode_operation_ready (MMIfaceModem3gpp                   *self,
                                        GAsyncResult                       *res,
                                        HandleSetEpsUeModeOperationContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->set_eps_ue_mode_operation_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_eps_ue_mode_operation &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_eps_ue_mode_operation_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_eps_ue_mode_operation (
            self,
            (GAsyncReadyCallback)after_set_load_eps_ue_mode_operation_ready,
            ctx);
        return;
    }

    /* Assume we're ok */
    mm_gdbus_modem3gpp_complete_set_eps_ue_mode_operation (ctx->skeleton, ctx->invocation);
    handle_set_eps_ue_mode_operation_context_free (ctx);
}

static void
handle_set_eps_ue_mode_operation_auth_ready (MMBaseModem                        *self,
                                             GAsyncResult                       *res,
                                             HandleSetEpsUeModeOperationContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    /* Check if we already are in the requested mode */
    if (mm_gdbus_modem3gpp_get_eps_ue_mode_operation (ctx->skeleton) == ctx->mode) {
        /* Nothing to do */
        mm_gdbus_modem3gpp_complete_set_eps_ue_mode_operation (ctx->skeleton, ctx->invocation);
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    /* If UE mode update is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->set_eps_ue_mode_operation ||
        !MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->set_eps_ue_mode_operation_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set UE mode of operation for EPS: operation not supported");
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->set_eps_ue_mode_operation (
        MM_IFACE_MODEM_3GPP (self),
        ctx->mode,
        (GAsyncReadyCallback)handle_set_eps_ue_mode_operation_ready,
        ctx);
}

static gboolean
handle_set_eps_ue_mode_operation (MmGdbusModem3gpp      *skeleton,
                                  GDBusMethodInvocation *invocation,
                                  guint                  mode,
                                  MMIfaceModem3gpp      *self)
{
    HandleSetEpsUeModeOperationContext *ctx;

    ctx = g_slice_new (HandleSetEpsUeModeOperationContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);
    ctx->mode       = mode;

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_eps_ue_mode_operation_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp      *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp      *self;
    GVariant              *dictionary;
    MMBearerProperties    *config;
} HandleSetInitialEpsBearerSettingsContext;

static void
handle_set_initial_eps_bearer_settings_context_free (HandleSetInitialEpsBearerSettingsContext *ctx)
{
    g_clear_object (&ctx->config);
    g_variant_unref (ctx->dictionary);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetInitialEpsBearerSettingsContext, ctx);
}

static void
log_initial_eps_bearer_settings (MMIfaceModem3gpp   *self,
                                 MMBearerProperties *properties)
{
    const gchar         *apn;
    MMBearerAllowedAuth  allowed_auth;
    const gchar         *user;
    const gchar         *password;
    MMBearerIpFamily     ip_family;

    apn = mm_bearer_properties_get_apn (properties);
    if (apn)
        mm_obj_dbg (self, "  APN: '%s'", apn);

    allowed_auth = mm_bearer_properties_get_allowed_auth (properties);
    if (allowed_auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN) {
        g_autofree gchar *allowed_auth_str = NULL;

        allowed_auth_str = mm_bearer_allowed_auth_build_string_from_mask (allowed_auth);
        mm_obj_dbg (self, "  allowed auth: '%s'", allowed_auth_str);
    }

    user = mm_bearer_properties_get_user (properties);
    if (user)
        mm_obj_dbg (self, "  user: '%s'", user);

    password = mm_bearer_properties_get_password (properties);
    if (password)
        mm_obj_dbg (self, "  password: '%s'", password);

    ip_family = mm_bearer_properties_get_ip_type (properties);
    if (ip_family != MM_BEARER_IP_FAMILY_NONE) {
        g_autofree gchar *ip_family_str = NULL;

        ip_family_str = mm_bearer_ip_family_build_string_from_mask (ip_family);
        mm_obj_dbg (self, "  ip family: '%s'", ip_family_str);
    }
}

static void
after_set_load_initial_eps_bearer_settings_ready (MMIfaceModem3gpp                         *self,
                                                  GAsyncResult                             *res,
                                                  HandleSetInitialEpsBearerSettingsContext *ctx)
{
    GError             *error = NULL;
    MMBearerProperties *new_config;

    new_config = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_settings_finish (self, res, &error);
    if (error) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    mm_obj_dbg (self, "Updated initial EPS bearer settings:");
    log_initial_eps_bearer_settings (self, new_config);

    if (!mm_bearer_properties_cmp (new_config, ctx->config, MM_BEARER_PROPERTIES_CMP_FLAGS_EPS)) {
        mm_obj_dbg (self, "Requested initial EPS bearer settings:");
        log_initial_eps_bearer_settings (self, ctx->config);
        g_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                       "Initial EPS bearer settings were not updated");
    } else {
        GVariant *dictionary;

        dictionary = mm_bearer_properties_get_dictionary (new_config);
        mm_gdbus_modem3gpp_set_initial_eps_bearer_settings (ctx->skeleton, dictionary);
        if (dictionary)
            g_variant_unref (dictionary);
        mm_gdbus_modem3gpp_complete_set_initial_eps_bearer_settings (ctx->skeleton, ctx->invocation);
    }

    handle_set_initial_eps_bearer_settings_context_free (ctx);
    g_object_unref (new_config);
}

static void
set_initial_eps_bearer_settings_ready (MMIfaceModem3gpp                         *self,
                                       GAsyncResult                             *res,
                                       HandleSetInitialEpsBearerSettingsContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->set_initial_eps_bearer_settings_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_settings &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_settings_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_settings (
            self,
            (GAsyncReadyCallback)after_set_load_initial_eps_bearer_settings_ready,
            ctx);
        return;
    }

    /* Assume we're ok */
    mm_gdbus_modem3gpp_complete_set_initial_eps_bearer_settings (ctx->skeleton, ctx->invocation);
    handle_set_initial_eps_bearer_settings_context_free (ctx);
}

static void
set_initial_eps_bearer_settings_auth_ready (MMBaseModem                              *self,
                                            GAsyncResult                             *res,
                                            HandleSetInitialEpsBearerSettingsContext *ctx)
{
    GError             *error = NULL;
    MMBearerProperties *old_config = NULL;
    GVariant           *old_dictionary;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    /* If UE mode update is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->set_initial_eps_bearer_settings ||
        !MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->set_initial_eps_bearer_settings_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot set initial EPS bearer settings: operation not supported");
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    ctx->config = mm_bearer_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!ctx->config) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    old_dictionary = mm_gdbus_modem3gpp_get_initial_eps_bearer_settings (ctx->skeleton);
    if (old_dictionary)
        old_config = mm_bearer_properties_new_from_dictionary (old_dictionary, NULL);

    if (old_config && mm_bearer_properties_cmp (ctx->config, old_config, MM_BEARER_PROPERTIES_CMP_FLAGS_EPS)) {
        mm_gdbus_modem3gpp_complete_set_initial_eps_bearer_settings (ctx->skeleton, ctx->invocation);
        handle_set_initial_eps_bearer_settings_context_free (ctx);
    } else {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->set_initial_eps_bearer_settings (
            MM_IFACE_MODEM_3GPP (self),
            ctx->config,
            (GAsyncReadyCallback)set_initial_eps_bearer_settings_ready,
            ctx);
    }

    g_clear_object (&old_config);
}

static gboolean
handle_set_initial_eps_bearer_settings (MmGdbusModem3gpp      *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        GVariant              *dictionary,
                                        MMIfaceModem3gpp      *self)
{
    HandleSetInitialEpsBearerSettingsContext *ctx;

    ctx = g_slice_new0 (HandleSetInitialEpsBearerSettingsContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)set_initial_eps_bearer_settings_auth_ready,
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
    gboolean is_cs_supported = FALSE;
    gboolean is_ps_supported = FALSE;
    gboolean is_eps_supported = FALSE;
    gboolean is_5gs_supported = FALSE;

    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_registration_checks != NULL);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED,  &is_cs_supported,
                  MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED,  &is_ps_supported,
                  MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &is_eps_supported,
                  MM_IFACE_MODEM_3GPP_5GS_NETWORK_SUPPORTED, &is_5gs_supported,
                  NULL);

    mm_obj_dbg (self, "running registration checks (CS: '%s', PS: '%s', EPS: '%s', 5GS: '%s')",
                is_cs_supported ? "yes" : "no",
                is_ps_supported ? "yes" : "no",
                is_eps_supported ? "yes" : "no",
                is_5gs_supported ? "yes" : "no");

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_registration_checks (self,
                                                                       is_cs_supported,
                                                                       is_ps_supported,
                                                                       is_eps_supported,
                                                                       is_5gs_supported,
                                                                       callback,
                                                                       user_data);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp *skeleton;
    gboolean operator_code_loaded;
    gboolean operator_name_loaded;
} ReloadCurrentRegistrationInfoContext;

static void
reload_current_registration_info_context_free (ReloadCurrentRegistrationInfoContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_slice_free (ReloadCurrentRegistrationInfoContext, ctx);
}

gboolean
mm_iface_modem_3gpp_reload_current_registration_info_finish (MMIfaceModem3gpp *self,
                                                             GAsyncResult *res,
                                                             GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void reload_current_registration_info_context_step (GTask *task);

static void
load_operator_name_ready (MMIfaceModem3gpp *self,
                          GAsyncResult *res,
                          GTask *task)
{
    ReloadCurrentRegistrationInfoContext *ctx;
    GError *error = NULL;
    gchar *str;

    ctx = g_task_get_task_data (task);

    str = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load operator name: %s", error->message);
        g_error_free (error);
    }

    if (ctx->skeleton)
        mm_gdbus_modem3gpp_set_operator_name (ctx->skeleton, str);
    g_free (str);

    ctx->operator_name_loaded = TRUE;
    reload_current_registration_info_context_step (task);
}

static void
load_operator_code_ready (MMIfaceModem3gpp *self,
                          GAsyncResult *res,
                          GTask *task)
{
    ReloadCurrentRegistrationInfoContext *ctx;
    GError *error = NULL;
    gchar *str;
    guint16 mcc = 0;
    guint16 mnc = 0;

    ctx = g_task_get_task_data (task);

    str = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load operator code: %s", error->message);
    } else if (!mm_3gpp_parse_operator_id (str, &mcc, &mnc, &error)) {
        mm_obj_dbg (self, "unexpected operator code string '%s': %s", str, error->message);
        g_clear_pointer (&str, g_free);
    }
    g_clear_error (&error);

    if (ctx->skeleton)
        mm_gdbus_modem3gpp_set_operator_code (ctx->skeleton, str);

    /* If we also implement the location interface, update the 3GPP location */
    if (mcc && MM_IS_IFACE_MODEM_LOCATION (self))
        mm_iface_modem_location_3gpp_update_mcc_mnc (MM_IFACE_MODEM_LOCATION (self), mcc, mnc);

    g_free (str);

    ctx->operator_code_loaded = TRUE;
    reload_current_registration_info_context_step (task);
}

static void
reload_current_registration_info_context_step (GTask *task)
{
    MMIfaceModem3gpp *self;
    ReloadCurrentRegistrationInfoContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    if (!ctx->operator_code_loaded) {
        /* Launch operator code update */
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code (
            self,
            (GAsyncReadyCallback)load_operator_code_ready,
            task);
        return;
    }

    if (!ctx->operator_name_loaded) {
        /* Launch operator name update */
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name (
            self,
            (GAsyncReadyCallback)load_operator_name_ready,
            task);
        return;
    }

    /* If all are loaded, all done */
    g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_iface_modem_3gpp_reload_current_registration_info (MMIfaceModem3gpp *self,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data)
{
    ReloadCurrentRegistrationInfoContext *ctx;
    GTask *task;

    ctx = g_slice_new0 (ReloadCurrentRegistrationInfoContext);

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)reload_current_registration_info_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
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

    reload_current_registration_info_context_step (task);
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

/*****************************************************************************/

void
mm_iface_modem_3gpp_update_access_technologies (MMIfaceModem3gpp        *self,
                                                MMModemAccessTechnology  access_tech)
{
    Private                      *priv;
    MMModem3gppRegistrationState  state;

    priv = get_private (self);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  NULL);

    /* Even if registration state didn't change, report access technology,
     * but only if something valid to report */
    if (REG_STATE_IS_REGISTERED (state) || priv->reloading_registration_info) {
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
                                     gulong            location_area_code,
                                     gulong            tracking_area_code,
                                     gulong            cell_id)
{
    Private                      *priv;
    MMModem3gppRegistrationState  state;

    priv = get_private (self);

    if (!MM_IS_IFACE_MODEM_LOCATION (self))
        return;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  NULL);

    /* Even if registration state didn't change, report access technology or
     * location updates, but only if something valid to report. For the case
     * where we're registering (loading current registration info after a state
     * change to registered), we also allow LAC/CID updates. */
    if (REG_STATE_IS_REGISTERED (state) || priv->reloading_registration_info) {
        if (location_area_code || tracking_area_code || cell_id)
            mm_iface_modem_location_3gpp_update_lac_tac_ci (MM_IFACE_MODEM_LOCATION (self),
                                                            location_area_code,
                                                            tracking_area_code,
                                                            cell_id);
    } else
        mm_iface_modem_location_3gpp_clear (MM_IFACE_MODEM_LOCATION (self));
}

/*****************************************************************************/

static void
update_registration_reload_current_registration_info_ready (MMIfaceModem3gpp *self,
                                                            GAsyncResult     *res,
                                                            gpointer          user_data)
{
    Private                      *priv;
    MMModem3gppRegistrationState  new_state;

    priv = get_private (self);

    new_state = GPOINTER_TO_UINT (user_data);

    mm_obj_info (self, "3GPP registration state changed (registering -> %s)",
                 mm_modem_3gpp_registration_state_get_string (new_state));
    mm_obj_dbg (self, "consolidated registration state: cs '%s', ps '%s', eps '%s', 5gs '%s' --> '%s'",
                mm_modem_3gpp_registration_state_get_string (priv->state_cs),
                mm_modem_3gpp_registration_state_get_string (priv->state_ps),
                mm_modem_3gpp_registration_state_get_string (priv->state_eps),
                mm_modem_3gpp_registration_state_get_string (priv->state_5gs),
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

    priv->reloading_registration_info = FALSE;
}

static void
update_non_registered_state (MMIfaceModem3gpp             *self,
                             MMModem3gppRegistrationState  old_state,
                             MMModem3gppRegistrationState  new_state)
{
    /* Not registered neither in home nor roaming network */
    mm_iface_modem_3gpp_clear_current_operator (self);

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
update_registration_state (MMIfaceModem3gpp             *self,
                           MMModem3gppRegistrationState  new_state,
                           gboolean                      deferrable)
{
    Private                      *priv;
    MMModem3gppRegistrationState  old_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

    priv = get_private (self);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &old_state,
                  NULL);

    /* Only set new state if different */
    if (new_state == old_state)
        return;

    if (REG_STATE_IS_REGISTERED (new_state)) {
        MMModemState modem_state;

        /* If already reloading registration info, skip it */
        if (priv->reloading_registration_info)
            return;

        /* If the modem isn't already enabled, this registration state update
         * could be due to a previously scheduled initial registration check
         * when the modem was being enabled.  We need to ignore it as otherwise
         * it may cause an incorrect transition of the registration state and
         * modem state when the modem is being disabled or still going through
         * enable steps */
        modem_state = MM_MODEM_STATE_UNKNOWN;
        g_object_get (self,
                      MM_IFACE_MODEM_STATE, &modem_state,
                      NULL);
        if (modem_state < MM_MODEM_STATE_ENABLED) {
            mm_obj_dbg (self, "3GPP registration state change ignored as modem isn't enabled");
            return;
        }

        mm_obj_info (self, "3GPP registration state changed (%s -> registering)",
                     mm_modem_3gpp_registration_state_get_string (old_state));

        /* Reload current registration info. ONLY update the state to REGISTERED
         * after having loaded operator code/name/subscription state */
        priv->reloading_registration_info = TRUE;
        mm_iface_modem_3gpp_reload_current_registration_info (
            self,
            (GAsyncReadyCallback)update_registration_reload_current_registration_info_ready,
            GUINT_TO_POINTER (new_state));
        return;
    }

    mm_obj_info (self, "3GPP registration state changed (%s -> %s)",
                 mm_modem_3gpp_registration_state_get_string (old_state),
                 mm_modem_3gpp_registration_state_get_string (new_state));
    mm_obj_dbg (self, "consolidated registration state: cs '%s', ps '%s', eps '%s', 5gs '%s' --> '%s'",
                mm_modem_3gpp_registration_state_get_string (priv->state_cs),
                mm_modem_3gpp_registration_state_get_string (priv->state_ps),
                mm_modem_3gpp_registration_state_get_string (priv->state_eps),
                mm_modem_3gpp_registration_state_get_string (priv->state_5gs),
                mm_modem_3gpp_registration_state_get_string (new_state));

    update_non_registered_state (self, old_state, new_state);
}

void
mm_iface_modem_3gpp_update_cs_registration_state (MMIfaceModem3gpp             *self,
                                                  MMModem3gppRegistrationState  state)
{
    Private  *priv;
    gboolean  supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    priv = get_private (self);
    priv->state_cs = state;
    update_registration_state (self, get_consolidated_reg_state (self), TRUE);
}

void
mm_iface_modem_3gpp_update_ps_registration_state (MMIfaceModem3gpp             *self,
                                                  MMModem3gppRegistrationState  state)
{
    Private  *priv;
    gboolean  supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    priv = get_private (self);
    priv->state_ps = state;
    update_registration_state (self, get_consolidated_reg_state (self), TRUE);
}

void
mm_iface_modem_3gpp_update_eps_registration_state (MMIfaceModem3gpp             *self,
                                                   MMModem3gppRegistrationState  state)
{
    Private  *priv;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    priv = get_private (self);
    priv->state_eps = state;
    update_registration_state (self, get_consolidated_reg_state (self), TRUE);
}

void
mm_iface_modem_3gpp_update_5gs_registration_state (MMIfaceModem3gpp             *self,
                                                   MMModem3gppRegistrationState  state)
{
    Private  *priv;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_5GS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    priv = get_private (self);
    priv->state_5gs = state;
    update_registration_state (self, get_consolidated_reg_state (self), TRUE);
}

/*****************************************************************************/
/* Periodic registration checks */

#define REGISTRATION_CHECK_TIMEOUT_SEC 30

static void
periodic_registration_checks_ready (MMIfaceModem3gpp *self,
                                    GAsyncResult     *res)
{
    Private *priv;
    GError  *error = NULL;

    priv = get_private (self);

    mm_iface_modem_3gpp_run_registration_checks_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "couldn't refresh 3GPP registration status: %s", error->message);
        g_error_free (error);
    }

    priv->check_running = FALSE;
}

static gboolean
periodic_registration_check (MMIfaceModem3gpp *self)
{
    Private *priv;

    priv = get_private (self);

    /* Only launch a new one if not one running already */
    if (!priv->check_running) {
        priv->check_running = TRUE;
        mm_iface_modem_3gpp_run_registration_checks (
            self,
            (GAsyncReadyCallback)periodic_registration_checks_ready,
            NULL);
    }
    return G_SOURCE_CONTINUE;
}

static void
periodic_registration_check_disable (MMIfaceModem3gpp *self)
{
    Private *priv;

    priv = get_private (self);

    /* Do nothing if already disabled */
    if (!priv->check_timeout_source)
        return;

    g_source_remove (priv->check_timeout_source);
    priv->check_timeout_source = 0;

    mm_obj_dbg (self, "periodic 3GPP registration checks disabled");
}

static void
periodic_registration_check_enable (MMIfaceModem3gpp *self)
{
    Private *priv;

    priv = get_private (self);

    /* Do nothing if already enabled */
    if (priv->check_timeout_source)
        return;

    /* Create context and keep it as object data */
    mm_obj_dbg (self, "periodic 3GPP registration checks enabled");
    priv->check_timeout_source = g_timeout_add_seconds (REGISTRATION_CHECK_TIMEOUT_SEC,
                                                        (GSourceFunc)periodic_registration_check,
                                                        self);
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_update_pco_list (MMIfaceModem3gpp *self,
                                     const GList *pco_list)
{
    MmGdbusModem3gpp *skeleton = NULL;
    GVariantBuilder builder;
    GVariant *variant;
    const GList *iter;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ubay)"));
    for (iter = pco_list; iter; iter = g_list_next (iter)) {
        g_variant_builder_add_value (&builder,
                                     mm_pco_to_variant (MM_PCO (iter->data)));
    }
    variant = g_variant_ref_sink (g_variant_builder_end (&builder));
    mm_gdbus_modem3gpp_set_pco (skeleton, variant);
    g_variant_unref (variant);
    g_object_unref (skeleton);
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_update_initial_eps_bearer (MMIfaceModem3gpp   *self,
                                               MMBearerProperties *properties)
{
    MmGdbusModem3gpp *skeleton = NULL;
    MMBaseBearer     *old_bearer = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON,      &skeleton,
                  MM_IFACE_MODEM_3GPP_INITIAL_EPS_BEARER, &old_bearer,
                  NULL);
    g_assert (skeleton);

    /* skip update? */
    if ((!old_bearer && !properties) ||
        (old_bearer && properties &&
         mm_bearer_properties_cmp (properties,
                                   mm_base_bearer_peek_config (MM_BASE_BEARER (old_bearer)),
                                   MM_BEARER_PROPERTIES_CMP_FLAGS_EPS)))
        goto out;

    if (properties) {
        MMBaseBearer *new_bearer;

        mm_obj_dbg (self, "updating initial EPS bearer...");
        g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->create_initial_eps_bearer);
        new_bearer = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->create_initial_eps_bearer (self, properties);
        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_INITIAL_EPS_BEARER, new_bearer,
                      NULL);
        mm_gdbus_modem3gpp_set_initial_eps_bearer (skeleton, mm_base_bearer_get_path (new_bearer));
        g_object_unref (new_bearer);
    } else {
        mm_obj_dbg (self, "clearing initial EPS bearer...");
        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_INITIAL_EPS_BEARER, NULL,
                      NULL);
        mm_gdbus_modem3gpp_set_initial_eps_bearer (skeleton, NULL);
    }

out:
    g_clear_object (&old_bearer);
    g_object_unref (skeleton);
}

static void
reload_initial_eps_bearer_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult     *res)
{
    g_autoptr(MMBearerProperties) properties = NULL;
    g_autoptr(GError)             error = NULL;

    properties = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_finish (self, res, &error);
    if (!properties) {
        mm_obj_dbg (self, "couldn't load initial default bearer properties: %s", error->message);
        return;
    }

    mm_iface_modem_3gpp_update_initial_eps_bearer (self, properties);
}

void
mm_iface_modem_3gpp_reload_initial_eps_bearer (MMIfaceModem3gpp *self)
{
    gboolean eps_supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &eps_supported,
                  NULL);

    if (eps_supported &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer (
            self,
            (GAsyncReadyCallback)reload_initial_eps_bearer_ready,
            NULL);
    }
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (GTask *task);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_INITIAL_EPS_BEARER,
    DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS,
    DISABLING_STEP_DISABLE_UNSOLICITED_REGISTRATION_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_REGISTRATION_STATE,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    DisablingStep step;
    MmGdbusModem *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_disable_finish (MMIfaceModem3gpp *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME,DISPLAY)                               \
    static void                                                         \
    NAME##_ready (MMIfaceModem3gpp *self,                               \
                  GAsyncResult *res,                                    \
                  GTask *task)                                          \
    {                                                                   \
        DisablingContext *ctx;                                          \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            mm_obj_dbg (self, "couldn't %s: %s", DISPLAY, error->message);      \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx = g_task_get_task_data (task);                              \
        ctx->step++;                                                    \
        interface_disabling_step (task);                                \
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
interface_disabling_step (GTask *task)
{
    MMIfaceModem3gpp *self;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_INITIAL_EPS_BEARER:
        mm_iface_modem_3gpp_update_initial_eps_bearer (self, NULL);
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS:
        /* Disable periodic registration checks, if they were set */
        periodic_registration_check_disable (self);
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_REGISTRATION_EVENTS: {
        gboolean cs_supported = FALSE;
        gboolean ps_supported = FALSE;
        gboolean eps_supported = FALSE;

        g_object_get (self,
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                      MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &eps_supported,
                      NULL);

        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->disable_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->disable_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->disable_unsolicited_registration_events (
                self,
                cs_supported,
                ps_supported,
                eps_supported,
                (GAsyncReadyCallback)disable_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
    } /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->cleanup_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->cleanup_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->cleanup_unsolicited_registration_events (
                self,
                (GAsyncReadyCallback)cleanup_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->cleanup_unsolicited_events (
                self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_REGISTRATION_STATE:
        update_registration_state (self, MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN, FALSE);
        mm_iface_modem_3gpp_update_access_technologies (self, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_3gpp_update_location (self, 0, 0, 0);
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        g_assert_not_reached ();
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_disable (MMIfaceModem3gpp *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    DisablingContext *ctx;
    GTask *task;

    ctx = g_new0 (DisablingContext, 1);
    ctx->step = DISABLING_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)disabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_disabling_step (task);
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (GTask *task);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_UNSOLICITED_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION_EVENTS,
    ENABLING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS,
    ENABLING_STEP_INITIAL_EPS_BEARER,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModem3gpp *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_enable_finish (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "setting up unsolicited events failed: %s", error->message);
        g_error_free (error);

        /* If we get an error setting up unsolicited events, don't even bother trying to
         * enable them. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS + 1;
        interface_enabling_step (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (task);
}

static void
enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "enabling unsolicited events failed: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
setup_unsolicited_registration_events_ready (MMIfaceModem3gpp *self,
                                             GAsyncResult *res,
                                             GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_registration_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "setting up unsolicited registration events failed: %s", error->message);
        g_error_free (error);

        /* If we get an error setting up unsolicited events, don't even bother trying to
         * enable them. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS + 1;
        interface_enabling_step (task);
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (self);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (task);
}

static void
enable_unsolicited_registration_events_ready (MMIfaceModem3gpp *self,
                                              GAsyncResult *res,
                                              GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_registration_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "enabling unsolicited registration events failed: %s", error->message);
        g_error_free (error);
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (self);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
load_initial_eps_bearer_ready (MMIfaceModem3gpp *self,
                               GAsyncResult     *res,
                               GTask            *task)
{
    MMBearerProperties *properties;
    EnablingContext    *ctx;
    GError             *error = NULL;

    ctx = g_task_get_task_data (task);

    properties = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_finish (self, res, &error);
    if (!properties) {
        mm_obj_dbg (self, "couldn't load initial default bearer properties: %s", error->message);
        g_error_free (error);
        goto out;
    }

    mm_iface_modem_3gpp_update_initial_eps_bearer (self, properties);
    g_object_unref (properties);

out:
    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (task);
}

static void
interface_enabling_step (GTask *task)
{
    MMIfaceModem3gpp *self;
    EnablingContext *ctx;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_unsolicited_registration_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS: {
        gboolean cs_supported = FALSE;
        gboolean ps_supported = FALSE;
        gboolean eps_supported = FALSE;

        g_object_get (self,
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                      MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &eps_supported,
                      NULL);

        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_registration_events (
                self,
                cs_supported,
                ps_supported,
                eps_supported,
                (GAsyncReadyCallback)enable_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
    } /* fall through */

    case ENABLING_STEP_INITIAL_EPS_BEARER: {
        gboolean eps_supported = FALSE;

        g_object_get (self,
                      MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED, &eps_supported,
                      NULL);

        if (eps_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer (
                self,
                (GAsyncReadyCallback)load_initial_eps_bearer_ready,
                task);
            return;
        }
        ctx->step++;
    } /* fall through */

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
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
    GTask *task;

    ctx = g_new0 (EnablingContext, 1);
    ctx->step = ENABLING_STEP_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)enabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_enabling_step (task);
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_IMEI,
    INITIALIZATION_STEP_ENABLED_FACILITY_LOCKS,
    INITIALIZATION_STEP_EPS_UE_MODE_OPERATION,
    INITIALIZATION_STEP_EPS_INITIAL_BEARER_SETTINGS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModem3gpp *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
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
load_initial_eps_bearer_settings_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult     *res,
                                        GTask            *task)
{
    InitializationContext *ctx;
    MMBearerProperties    *config;
    GError                *error = NULL;

    ctx = g_task_get_task_data (task);

    config = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_settings_finish (self, res, &error);
    if (!config) {
        mm_obj_warn (self, "couldn't load initial EPS bearer settings: %s", error->message);
        g_error_free (error);
    } else {
        GVariant *dictionary;

        dictionary = mm_bearer_properties_get_dictionary (config);
        mm_gdbus_modem3gpp_set_initial_eps_bearer_settings (ctx->skeleton, dictionary);
        g_object_unref (config);
        if (dictionary)
            g_variant_unref (dictionary);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_eps_ue_mode_operation_ready (MMIfaceModem3gpp *self,
                                  GAsyncResult     *res,
                                  GTask            *task)
{
    InitializationContext         *ctx;
    MMModem3gppEpsUeModeOperation  uemode;
    GError                        *error = NULL;

    ctx = g_task_get_task_data (task);

    uemode = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_eps_ue_mode_operation_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_eps_ue_mode_operation (ctx->skeleton, uemode);

    if (error) {
        mm_obj_warn (self, "couldn't load UE mode of operation for EPS: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_enabled_facility_locks_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;
    MMModem3gppFacility facilities;

    ctx = g_task_get_task_data (task);

    facilities = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_enabled_facility_locks_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_enabled_facility_locks (ctx->skeleton, facilities);

    if (error) {
        mm_obj_warn (self, "couldn't load facility locks: %s", error->message);
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
    interface_initialization_step (task);
}

static void
load_imei_ready (MMIfaceModem3gpp *self,
                 GAsyncResult *res,
                 GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;
    gchar *imei;

    ctx = g_task_get_task_data (task);

    imei = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_imei_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_imei (ctx->skeleton, imei);
    g_free (imei);

    if (error) {
        mm_obj_warn (self, "couldn't load IMEI: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModem3gpp *self;
    InitializationContext *ctx;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_IMEI:
        /* IMEI value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem3gpp_get_imei (ctx->skeleton) &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_imei &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_imei_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_imei (
                self,
                (GAsyncReadyCallback)load_imei_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_ENABLED_FACILITY_LOCKS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_enabled_facility_locks &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_enabled_facility_locks_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_enabled_facility_locks (
                self,
                (GAsyncReadyCallback)load_enabled_facility_locks_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_EPS_UE_MODE_OPERATION:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_eps_ue_mode_operation &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_eps_ue_mode_operation_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_eps_ue_mode_operation (
                self,
                (GAsyncReadyCallback)load_eps_ue_mode_operation_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_EPS_INITIAL_BEARER_SETTINGS:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_settings &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_settings_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_initial_eps_bearer_settings (
                self,
                (GAsyncReadyCallback)load_initial_eps_bearer_settings_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-register",
                          G_CALLBACK (handle_register),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-scan",
                          G_CALLBACK (handle_scan),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-eps-ue-mode-operation",
                          G_CALLBACK (handle_set_eps_ue_mode_operation),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-initial-eps-bearer-settings",
                          G_CALLBACK (handle_set_initial_eps_bearer_settings),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem3gpp (MM_GDBUS_OBJECT_SKELETON (self),
                                                MM_GDBUS_MODEM3GPP (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_3gpp_initialize_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_3gpp_initialize (MMIfaceModem3gpp *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MmGdbusModem3gpp *skeleton = NULL;
    InitializationContext *ctx;
    GTask *task;

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
        mm_gdbus_modem3gpp_set_pco (skeleton, NULL);
        mm_gdbus_modem3gpp_set_initial_eps_bearer (skeleton, NULL);

        /* Bind our RegistrationState property */
        g_object_bind_property (self, MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                                skeleton, "registration-state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON, skeleton,
                      NULL);
    }

    ctx = g_new0 (InitializationContext, 1);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    /* Perform async initialization here */
    interface_initialization_step (task);
}

void
mm_iface_modem_3gpp_shutdown (MMIfaceModem3gpp *self)
{
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
         g_param_spec_boolean (MM_IFACE_MODEM_3GPP_5GS_NETWORK_SUPPORTED,
                               "5GS network supported",
                               "Whether the modem works in the 5GS network",
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

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_3GPP_INITIAL_EPS_BEARER,
                              "Initial EPS bearer",
                              "Initial EPS bearer setup during registration",
                              MM_TYPE_BASE_BEARER,
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
