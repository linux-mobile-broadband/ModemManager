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
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc.
 */

#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-location.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-profile-manager.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-error-helpers.h"
#include "mm-log.h"
#include "mm-log-helpers.h"
#include "mm-iface-op-lock.h"

#define SUBSYSTEM_3GPP "3gpp"

/* When comparing EPS bearer settings take into account that:
 *  -  'password' may not always be readable.
 *  -  'apn-type' may not always be supported.
 *  -  'access-type-preference' may not always be reported.
 *  -  'profile-id' will not be known in the requested settings
 *  -  'profile-name' might not be known in the requested settings
 *  -  we ignore settings not applicable to profiles, like 'allow-roaming' or
 *     'rm-protocol'.
 *  -  we apply very loose matching for all fields.
 */
#define MM_BEARER_PROPERTIES_CMP_FLAGS_EPS              \
    (MM_BEARER_PROPERTIES_CMP_FLAGS_LOOSE |             \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PROFILE_ID |     \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PROFILE_NAME |   \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_PASSWORD |       \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_ALLOW_ROAMING |  \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_APN_TYPE |       \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_RM_PROTOCOL |    \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_ACCESS_TYPE_PREFERENCE | \
     MM_BEARER_PROPERTIES_CMP_FLAGS_NO_ROAMING_ALLOWANCE)

G_DEFINE_INTERFACE (MMIfaceModem3gpp, mm_iface_modem_3gpp, MM_TYPE_IFACE_MODEM)

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "iface-modem-3gpp-private-tag"
static GQuark private_quark;

typedef struct {
    /* Interface enabled or disabled */
    gboolean iface_enabled;
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
    /* Packet service state */
    gboolean packet_service_state_update_supported;
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

#define GET_NETWORK_SUPPORTED(domain,DOMAIN)                                         \
    static gboolean                                                                  \
    get_##domain##_network_supported (MMIfaceModem3gpp *self)                        \
    {                                                                                \
        gboolean supported = FALSE;                                                  \
                                                                                     \
        g_object_get (self,                                                          \
                      MM_IFACE_MODEM_3GPP_##DOMAIN##_NETWORK_SUPPORTED, &supported,  \
                      NULL);                                                         \
        return supported;                                                            \
    }

GET_NETWORK_SUPPORTED (cs,  CS)
GET_NETWORK_SUPPORTED (ps,  PS)
GET_NETWORK_SUPPORTED (eps, EPS)
GET_NETWORK_SUPPORTED (5gs, 5GS)

/*****************************************************************************/
/* Helper method to wait for a final packet service state */

typedef struct {
    MMModem3gppPacketServiceState final_state;
    gulong                        state_changed_id;
    guint                         timeout_id;
    gulong                        cancellable_id;
} WaitForPacketServiceStateContext;

MMModem3gppPacketServiceState
mm_iface_modem_3gpp_wait_for_packet_service_state_finish (MMIfaceModem3gpp  *self,
                                                          GAsyncResult      *res,
                                                          GError           **error)
{
    GError *inner_error = NULL;
    gssize  value;

    value = g_task_propagate_int (G_TASK (res), &inner_error);
    if (inner_error) {
        g_propagate_error (error, inner_error);
        return MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN;
    }
    if (value == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN)
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                     "Unknown packet service state");
    return (MMModem3gppPacketServiceState)value;
}

static void
wait_for_packet_service_state_context_complete (GTask                         *task,
                                                MMModem3gppPacketServiceState  state,
                                                GError                        *error)
{
    MMIfaceModem3gpp                 *self;
    WaitForPacketServiceStateContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    g_assert (ctx->state_changed_id);
    if (g_signal_handler_is_connected (self, ctx->state_changed_id))
        g_signal_handler_disconnect (self, ctx->state_changed_id);
    ctx->state_changed_id = 0;

    g_assert (ctx->timeout_id);
    g_source_remove (ctx->timeout_id);
    ctx->timeout_id = 0;

    if (!g_task_return_error_if_cancelled (task)) {
        if (ctx->cancellable_id) {
            g_cancellable_disconnect (g_task_get_cancellable (task), ctx->cancellable_id);
            ctx->cancellable_id = 0;
        }
        if (error)
            g_task_return_error (task, error);
        else
            g_task_return_int (task, state);
    }
    g_object_unref (task);
}

static void
packet_service_wait_cancelled (GCancellable *cancellable,
                               GTask        *task)
{
    MMIfaceModem3gpp                 *self;
    WaitForPacketServiceStateContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_obj_dbg (self, "wait for packet service state '%s': cancelled",
                (ctx->final_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN) ?
                "any" : mm_modem_3gpp_packet_service_state_get_string (ctx->final_state));

    /* Given that the cancellable is the same one as in the task, we can complete the operation here
     * without specifying an exact error. The task will itself be completed with a cancelled error. */
    g_assert (g_task_get_cancellable (task) == cancellable);
    wait_for_packet_service_state_context_complete (task, MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN, NULL);
}

static gboolean
packet_service_wait_timeout (GTask *task)
{
    MMIfaceModem3gpp                 *self;
    WaitForPacketServiceStateContext *ctx;
    GError                           *error;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    mm_obj_dbg (self, "wait for packet service state '%s': timed out",
                (ctx->final_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN) ?
                "any" : mm_modem_3gpp_packet_service_state_get_string (ctx->final_state));

    error = g_error_new (MM_CORE_ERROR,
                         MM_CORE_ERROR_RETRY,
                         "Too much time waiting to get to a final packet service state");
    wait_for_packet_service_state_context_complete (task, MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN, error);
    return G_SOURCE_REMOVE;
}

static void
packet_service_state_changed (MMIfaceModem3gpp *self,
                              GParamSpec       *spec,
                              GTask            *task)
{
    WaitForPacketServiceStateContext *ctx;
    MMModem3gppPacketServiceState     state = MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PACKET_SERVICE_STATE, &state,
                  NULL);

    /* Ignore unknown state explicitly during a wait operation */
    if (state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN)
        return;

    ctx = g_task_get_task_data (task);

    /* If we want a specific final state and this is not the one we were
     * looking for, then skip */
    if ((ctx->final_state != MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN) &&
        (state != ctx->final_state))
        return;

    mm_obj_dbg (self, "wait for packet service state '%s': finished",
                (ctx->final_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN) ?
                "any" : mm_modem_3gpp_packet_service_state_get_string (ctx->final_state));

    /* Done! */
    wait_for_packet_service_state_context_complete (task, state, NULL);
}

void
mm_iface_modem_3gpp_wait_for_packet_service_state (MMIfaceModem3gpp              *self,
                                                   MMModem3gppPacketServiceState  final_state,
                                                   GCancellable                  *cancellable,
                                                   GAsyncReadyCallback            callback,
                                                   gpointer                       user_data)
{
    MMModem3gppPacketServiceState     state = MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN;
    WaitForPacketServiceStateContext *ctx;
    GTask                            *task;

    task = g_task_new (self, cancellable, callback, user_data);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PACKET_SERVICE_STATE, &state,
                  NULL);

    /* Is this the state we actually wanted? */
    if (final_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN ||
        (state != MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN && state == final_state)) {
        g_task_return_int (task, state);
        g_object_unref (task);
        return;
    }

    /* Otherwise, we'll need to wait for the exact one we want */
    ctx = g_new0 (WaitForPacketServiceStateContext, 1);
    ctx->final_state = final_state;

    g_task_set_task_data (task, ctx, g_free);

    /* Ownership of the task will be shared among the signal handler, the timeout,
     * and the cancellable. As soon as one of them is triggered, it should cancel the
     * other two. */

    /* Want to get notified when packet service state changes */
    ctx->state_changed_id = g_signal_connect (self,
                                              "notify::" MM_IFACE_MODEM_3GPP_PACKET_SERVICE_STATE,
                                              G_CALLBACK (packet_service_state_changed),
                                              task);
    /* But we don't want to wait forever */
    ctx->timeout_id = g_timeout_add_seconds (10,
                                             (GSourceFunc)packet_service_wait_timeout,
                                             task);

    /* And we want it to be cancellable */
    if (cancellable) {
        ctx->cancellable_id = g_cancellable_connect (cancellable,
                                                     (GCallback) packet_service_wait_cancelled,
                                                     task,
                                                     NULL);
        /* Do nothing if already cancelled, packet_service_wait_cancelled() will already be called */
        if (!ctx->cancellable_id)
            return;
    }

    mm_obj_dbg (self, "wait for packet service state '%s': started",
                (final_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN) ?
                "any" : mm_modem_3gpp_packet_service_state_get_string (final_state));
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

static MMModem3gppPacketServiceState
get_consolidated_packet_service_state (MMIfaceModem3gpp *self)
{
    Private *priv;

    priv = get_private (self);
    g_assert (!priv->packet_service_state_update_supported);

    /* If registered in any of PS, EPS or 5GS, then packet service domain is
     * implicitly attached. */
    if (mm_modem_3gpp_registration_state_is_registered (priv->state_ps) ||
        mm_modem_3gpp_registration_state_is_registered (priv->state_eps) ||
        mm_modem_3gpp_registration_state_is_registered (priv->state_5gs))
        return MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED;

    if (mm_modem_3gpp_registration_state_is_registered (priv->state_cs))
        return MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED;

    /* If not registered in any of CS, PS, EPS or 5GS, then packet service
     * domain is detached. */
    return MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED;
}

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

#define REG_STATE_IS_UNKNOWN_IDLE_DENIED(state)                           \
    (state == MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN ||                 \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_IDLE ||                    \
     state == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED)

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

#undef REG_STATE_IS_UNKNOWN_IDLE_DENIED

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
    gboolean          force_registration;
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

    mm_iface_modem_3gpp_update_cs_registration_state      (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE, TRUE);
    mm_iface_modem_3gpp_update_ps_registration_state      (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE, TRUE);
    mm_iface_modem_3gpp_update_eps_registration_state     (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE, TRUE);
    mm_iface_modem_3gpp_update_5gs_registration_state     (ctx->self, MM_MODEM_3GPP_REGISTRATION_STATE_IDLE, TRUE);
    mm_iface_modem_3gpp_apply_deferred_registration_state (ctx->self);

    mm_iface_modem_3gpp_update_access_technologies (ctx->self, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
    mm_iface_modem_3gpp_update_location            (ctx->self, 0, 0, 0);

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
        mm_obj_info (self, "3GPP registration check failed: %s", error->message);
        register_in_network_context_complete_failed (task, error);
        return;
    }

    current_registration_state = get_consolidated_reg_state (ctx->self);

    /* If we got a final state and it's denied, we can assume the registration is
     * finished */
    if (current_registration_state == MM_MODEM_3GPP_REGISTRATION_STATE_DENIED) {
        mm_obj_info (self, "registration denied");
        register_in_network_context_complete_failed (
            task,
            mm_mobile_equipment_error_for_code (MM_MOBILE_EQUIPMENT_ERROR_NETWORK_NOT_ALLOWED, self));
        return;
    }

    /* If we got registered, end registration checks */
    if (mm_modem_3gpp_registration_state_is_registered (current_registration_state)) {
        /* Request immediate access tech and signal update: we may have changed
         * from home to roaming or viceversa, both registered states, so there
         * wouldn't be an explicit refresh triggered from the modem interface as
         * the modem never got un-registered during the sequence. */
        mm_iface_modem_refresh_signal (MM_IFACE_MODEM (ctx->self));
        mm_obj_info (self, "currently registered in a 3GPP network");
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    /* Don't spend too much time waiting to get registered */
    if (g_timer_elapsed (ctx->timer, NULL) > ctx->max_registration_time) {
        mm_obj_info (self, "3GPP registration check timed out");
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

    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->register_in_network_finish (self, res, &error)) {
        /* Propagate error when trying to lock to network */
        register_in_network_context_complete_failed (task, error);
        return;
    }

    /* Now try to gather current registration status until we're registered or
     * the time goes off */
    run_registration_checks (task);
}

static void
initial_registration_checks_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult     *res,
                                   GTask            *task)
{
    Private                      *priv;
    RegisterInNetworkContext     *ctx;
    GError                       *error = NULL;
    const gchar                  *current_operator_code;
    MMModem3gppRegistrationState  reg_state;

    priv = get_private (self);
    ctx = g_task_get_task_data (task);

    if (!mm_iface_modem_3gpp_run_registration_checks_finish (self, res, &error)) {
        mm_obj_info (self, "Initial 3GPP registration check failed: %s", error->message);
        g_error_free (error);
        /* Just continue as if nothing happened */
    }

    current_operator_code = mm_gdbus_modem3gpp_get_operator_code (ctx->skeleton);
    reg_state = mm_gdbus_modem3gpp_get_registration_state (ctx->skeleton);

    /* Manual registration requested? */
    if (ctx->operator_id) {
        /* If already registered manually with the requested operator, we're done */
        if (!ctx->force_registration &&
            (g_strcmp0 (current_operator_code, ctx->operator_id) == 0) &&
            mm_modem_3gpp_registration_state_is_registered (reg_state) &&
            priv->manual_registration) {
            mm_obj_info (self, "already registered manually in selected network '%s', manual registration not launched...",
                        current_operator_code);
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }

        /* Manual registration to a new operator required */
        mm_obj_info (self, "launching manual network registration in '%s'...", ctx->operator_id);
        g_free (priv->manual_registration_operator_id);
        priv->manual_registration_operator_id = g_strdup (ctx->operator_id);
        priv->manual_registration = TRUE;
    }
    /* Automatic registration requested? */
    else {
        /* If the modem is already registered and the last time it was asked
         * automatic registration, we're done */
        if (!ctx->force_registration &&
            mm_modem_3gpp_registration_state_is_registered (reg_state) &&
            !priv->manual_registration) {
            mm_obj_info (self, "already registered automatically in network '%s',"
                        " automatic registration not launched...",
                        current_operator_code ? current_operator_code : "unknown");
            g_task_return_boolean (task, TRUE);
            g_object_unref (task);
            return;
        }

        /* Automatic registration to a new operator requested */
        mm_obj_info (self, "launching automatic network registration...");
        g_clear_pointer (&priv->manual_registration_operator_id, g_free);
        priv->manual_registration = FALSE;
    }

    ctx->cancellable = g_cancellable_new ();

    /* Keep an accessible reference to the cancellable, so that we can cancel
     * previous request when needed */
    priv->pending_registration_cancellable = g_object_ref (ctx->cancellable);

    ctx->timer = g_timer_new ();
    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->register_in_network (
        self,
        ctx->operator_id,
        ctx->cancellable,
        (GAsyncReadyCallback)register_in_network_ready,
        task);
}

void
mm_iface_modem_3gpp_register_in_network (MMIfaceModem3gpp    *self,
                                         const gchar         *operator_id,
                                         gboolean             force_registration,
                                         guint                max_registration_time,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
    Private                  *priv;
    RegisterInNetworkContext *ctx;
    GTask                    *task;
    GError                   *error = NULL;

    priv = get_private (self);

    ctx = g_slice_new0 (RegisterInNetworkContext);
    ctx->self = g_object_ref (self);
    ctx->force_registration = force_registration;
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
    if (ctx->operator_id && !mm_3gpp_parse_operator_id (ctx->operator_id, NULL, NULL, NULL, &error)) {
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

    /* Query initial registration state here in order to avoid re-registering. */
    mm_iface_modem_3gpp_run_registration_checks (
        self,
        (GAsyncReadyCallback)initial_registration_checks_ready,
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
    MmGdbusModem3gpp      *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp      *self;
    gchar                 *operator_id;
} HandleRegisterContext;

static void
handle_register_context_free (HandleRegisterContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->operator_id);
    g_slice_free (HandleRegisterContext, ctx);
}

static void
handle_register_ready (MMIfaceModem3gpp      *self,
                       GAsyncResult          *res,
                       HandleRegisterContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_3gpp_register_in_network_finish (self, res, &error)) {
        if (ctx->operator_id && ctx->operator_id[0])
            mm_obj_warn (self, "failed registering modem in '%s': %s", ctx->operator_id, error->message);
        else
            mm_obj_warn (self, "failed registering modem: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        if (ctx->operator_id && ctx->operator_id[0])
            mm_obj_info (self, "modem registered in '%s'", ctx->operator_id);
        else
            mm_obj_info (self, "modem registered");
        mm_gdbus_modem3gpp_complete_register (ctx->skeleton, ctx->invocation);
    }

    handle_register_context_free (ctx);
}

static void
handle_register_auth_ready (MMIfaceAuth           *auth,
                            GAsyncResult          *res,
                            HandleRegisterContext *ctx)
{
    MMIfaceModem3gpp *self = MM_IFACE_MODEM_3GPP (auth);
    MMModemState  modem_state = MM_MODEM_STATE_UNKNOWN;
    GError       *error = NULL;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_register_context_free (ctx);
        return;
    }

    g_assert (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->register_in_network != NULL);
    g_assert (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->register_in_network_finish != NULL);

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_FAILED:
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_INITIALIZING:
    case MM_MODEM_STATE_LOCKED:
    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                                        "Device not yet enabled");
        handle_register_context_free (ctx);
        return;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        if (ctx->operator_id && ctx->operator_id[0])
            mm_obj_info (self, "processing user request to register modem in '%s'...", ctx->operator_id);
        else
            mm_obj_info (self, "processing user request to register modem...");
        mm_iface_modem_3gpp_register_in_network (MM_IFACE_MODEM_3GPP (self),
                                                 ctx->operator_id,
                                                 FALSE, /* if already registered with same settings, do nothing */
                                                 60,
                                                 (GAsyncReadyCallback)handle_register_ready,
                                                 ctx);
        return;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE,
                                                        "Operation not allowed while modem is connected");
        handle_register_context_free (ctx);
        return;

    default:
        g_assert_not_reached ();
    }
}

static gboolean
handle_register (MmGdbusModem3gpp      *skeleton,
                 GDBusMethodInvocation *invocation,
                 const gchar           *operator_id,
                 MMIfaceModem3gpp      *self)
{
    HandleRegisterContext *ctx;

    ctx = g_slice_new0 (HandleRegisterContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->operator_id = g_strdup (operator_id);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_register_auth_ready,
                             ctx);

    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp      *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp      *self;
} HandleScanContext;

static void
handle_scan_context_free (HandleScanContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleScanContext, ctx);
}

static GVariant *
build_scan_networks_result (MMIfaceModem3gpp *self,
                            GList            *info_list)
{
    GList *l;
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

    for (l = info_list; l; l = g_list_next (l)) {
        MM3gppNetworkInfo *info = l->data;
        g_autofree gchar  *access_tech_str = NULL;

        if (!info->operator_code) {
            g_warn_if_reached ();
            continue;
        }

        /* log results as INFO */
        access_tech_str = mm_modem_access_technology_build_string_from_mask (info->access_tech);
        mm_obj_info (self, "  mccmnc: %s, status: %s, access tech: %s, long name: %s, short name: %s",
                     info->operator_code,
                     mm_modem_3gpp_network_availability_get_string (info->status),
                     access_tech_str,
                     info->operator_long ? info->operator_long : "n/a",
                     info->operator_short ? info->operator_short : "n/a");

        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
        g_variant_builder_add (&builder, "{sv}", "operator-code", g_variant_new_string (info->operator_code));
        g_variant_builder_add (&builder, "{sv}", "status", g_variant_new_uint32 (info->status));
        g_variant_builder_add (&builder, "{sv}", "access-technology", g_variant_new_uint32 (info->access_tech));
        if (info->operator_long)
            g_variant_builder_add (&builder, "{sv}", "operator-long", g_variant_new_string (info->operator_long));
        if (info->operator_short)
            g_variant_builder_add (&builder, "{sv}", "operator-short", g_variant_new_string (info->operator_short));
        g_variant_builder_close (&builder);
    }

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

static void
handle_scan_ready (MMIfaceModem3gpp  *self,
                   GAsyncResult      *res,
                   HandleScanContext *ctx)
{
    GError              *error = NULL;
    GList               *info_list;
    g_autoptr(GVariant)  dict_array = NULL;

    info_list = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->scan_networks_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "failed scanning networks: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_scan_context_free (ctx);
        return;
    }

    mm_obj_info (self, "network scan performed: %u found", g_list_length (info_list));
    dict_array = build_scan_networks_result (self, info_list);
    mm_gdbus_modem3gpp_complete_scan (ctx->skeleton, ctx->invocation, dict_array);
    mm_3gpp_network_info_list_free (info_list);
    handle_scan_context_free (ctx);
}

static void
handle_scan_auth_ready (MMIfaceAuth       *auth,
                        GAsyncResult      *res,
                        HandleScanContext *ctx)
{
    MMIfaceModem3gpp *self = MM_IFACE_MODEM_3GPP (auth);
    GError *error = NULL;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_scan_context_free (ctx);
        return;
    }

    /* If scanning is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->scan_networks ||
        !MM_IFACE_MODEM_3GPP_GET_IFACE (self)->scan_networks_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Cannot scan networks: operation not supported");
        handle_scan_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_ENABLED)) {
        handle_scan_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->scan_networks (
        MM_IFACE_MODEM_3GPP (self),
        (GAsyncReadyCallback)handle_scan_ready,
        ctx);
}

static gboolean
handle_scan (MmGdbusModem3gpp      *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModem3gpp      *self)
{
    HandleScanContext *ctx;

    ctx = g_slice_new0 (HandleScanContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
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

    uemode = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_eps_ue_mode_operation_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "failed reloading EPS UE mode of operation after update to '%s': %s",
                     mm_modem_3gpp_eps_ue_mode_operation_get_string (ctx->mode),
                     error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    if (uemode != ctx->mode) {
        mm_obj_info (self, "requested (%s) and reloaded (%s) EPS UE mode of operation don't match",
                     mm_modem_3gpp_eps_ue_mode_operation_get_string (ctx->mode),
                     mm_modem_3gpp_eps_ue_mode_operation_get_string (uemode));
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "EPS UE mode of operation wasn't updated");
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

    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_eps_ue_mode_operation_finish (self, res, &error)) {
        mm_obj_warn (self, "failed setting EPS UE mode of operation to '%s': %s",
                     mm_modem_3gpp_eps_ue_mode_operation_get_string (ctx->mode),
                     error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_eps_ue_mode_operation &&
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_eps_ue_mode_operation_finish) {
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_eps_ue_mode_operation (
            self,
            (GAsyncReadyCallback)after_set_load_eps_ue_mode_operation_ready,
            ctx);
        return;
    }

    /* Assume we're ok */
    mm_obj_info (self, "EPS UE mode of operation set to '%s'",
                 mm_modem_3gpp_eps_ue_mode_operation_get_string (ctx->mode));
    mm_gdbus_modem3gpp_complete_set_eps_ue_mode_operation (ctx->skeleton, ctx->invocation);
    handle_set_eps_ue_mode_operation_context_free (ctx);
}

static void
handle_set_eps_ue_mode_operation_auth_ready (MMIfaceAuth                        *auth,
                                             GAsyncResult                       *res,
                                             HandleSetEpsUeModeOperationContext *ctx)
{
    MMIfaceModem3gpp *self = MM_IFACE_MODEM_3GPP (auth);
    GError *error = NULL;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
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
    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_eps_ue_mode_operation ||
        !MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_eps_ue_mode_operation_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Cannot set UE mode of operation for EPS: operation not supported");
        handle_set_eps_ue_mode_operation_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to set EPS UE mode of operation to '%s'...",
                 mm_modem_3gpp_eps_ue_mode_operation_get_string (ctx->mode));

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_eps_ue_mode_operation (
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

    ctx = g_slice_new0 (HandleSetEpsUeModeOperationContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);
    ctx->mode       = mode;

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_eps_ue_mode_operation_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef enum {
    HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_FIRST,
    HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_DOWN,
    HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_UPDATE,
    HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_PREVIOUS_POWER,
    HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_RELOAD,
    HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST,
} HandleSetInitialEpsBearerSettingsStep;

typedef struct {
    HandleSetInitialEpsBearerSettingsStep  step;
    MmGdbusModem3gpp                      *skeleton;
    GDBusMethodInvocation                 *invocation;
    gssize                                 operation_id;
    MMIfaceModem3gpp                      *self;
    GVariant                              *dictionary;
    MMBearerProperties                    *config;
    MMModemPowerState                      previous_power_state;
    GError                                *saved_error;
} HandleSetInitialEpsBearerSettingsContext;

static void
handle_set_initial_eps_bearer_settings_context_free (HandleSetInitialEpsBearerSettingsContext *ctx)
{
    if (ctx->operation_id >= 0)
        mm_iface_op_lock_unlock (MM_IFACE_OP_LOCK (ctx->self), ctx->operation_id);

    g_assert (!ctx->saved_error);
    g_clear_object (&ctx->config);
    g_variant_unref (ctx->dictionary);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetInitialEpsBearerSettingsContext, ctx);
}

static void handle_set_initial_eps_bearer_settings_step (HandleSetInitialEpsBearerSettingsContext *ctx);

static void
handle_set_initial_eps_bearer_settings_reload_ready (MMIfaceModem3gpp                         *self,
                                                     GAsyncResult                             *res,
                                                     HandleSetInitialEpsBearerSettingsContext *ctx)
{
    g_autoptr(MMBearerProperties) new_config = NULL;
    g_autoptr(GVariant)           dictionary = NULL;

    g_assert (!ctx->saved_error);

    new_config = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_settings_finish (self, res, &ctx->saved_error);
    if (ctx->saved_error)
        mm_obj_warn (self, "failed reloading initial EPS bearer settings after update: %s", ctx->saved_error->message);
    else if (!mm_bearer_properties_cmp (new_config, ctx->config, MM_BEARER_PROPERTIES_CMP_FLAGS_EPS)) {
        mm_obj_warn (self, "requested and reloaded initial EPS bearer settings don't match");
        mm_obj_info (self, "reloaded initial EPS bearer settings:");
        mm_log_bearer_properties (self, MM_LOG_LEVEL_INFO, "  ", new_config);
        ctx->saved_error = g_error_new_literal (MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                "Initial EPS bearer settings were not updated");
    } else {
        dictionary = mm_bearer_properties_get_dictionary (new_config);
        mm_gdbus_modem3gpp_set_initial_eps_bearer_settings (ctx->skeleton, dictionary);
    }

    ctx->step++;
    handle_set_initial_eps_bearer_settings_step (ctx);
}

static void
handle_set_initial_eps_bearer_settings_previous_power_ready (MMIfaceModem                             *self,
                                                             GAsyncResult                             *res,
                                                             HandleSetInitialEpsBearerSettingsContext *ctx)
{
    g_autoptr(GError) error = NULL;

    if (!mm_iface_modem_set_power_state_finish (self, res, NULL, &error)) {
        mm_obj_warn (self, "failed to restore power state after updating initial EPS bearer settings: %s", error->message);
        if (!ctx->saved_error)
            ctx->saved_error = g_steal_pointer (&error);
    } else {
        mm_obj_dbg (self, "modem power state updated: %s -> %s",
                    mm_modem_power_state_get_string (MM_MODEM_POWER_STATE_LOW),
                    mm_modem_power_state_get_string (ctx->previous_power_state));
    }

    /* Jump to last if there is any error */
    if (ctx->saved_error)
        ctx->step = HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST;
    else
        ctx->step++;
    handle_set_initial_eps_bearer_settings_step (ctx);
}

static void
handle_set_initial_eps_bearer_settings_update_ready (MMIfaceModem3gpp                         *self,
                                                     GAsyncResult                             *res,
                                                     HandleSetInitialEpsBearerSettingsContext *ctx)
{
    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_initial_eps_bearer_settings_finish (self, res, &ctx->saved_error)) {
        mm_obj_warn (self, "failed setting initial EPS bearer settings: %s", ctx->saved_error->message);
        /* process profile manager updates right away on error */
        mm_iface_modem_3gpp_profile_manager_update_ignore_stop (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self));
        /* we continue the steps in order to power up after the failure if needed */
    } else {
        /* delay processing profile manager updates on success */
        mm_iface_modem_3gpp_profile_manager_update_ignore_stop_delayed (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (self));
        mm_obj_info (self, "initial EPS bearer settings updated");
    }

    ctx->step++;
    handle_set_initial_eps_bearer_settings_step (ctx);
}

static void
handle_set_initial_eps_bearer_settings_power_down_ready (MMIfaceModem                             *self,
                                                         GAsyncResult                             *res,
                                                         HandleSetInitialEpsBearerSettingsContext *ctx)
{
    if (!mm_iface_modem_set_power_state_finish (self, res, &ctx->previous_power_state, &ctx->saved_error)) {
        /* Jump to last if there is any error */
        mm_obj_warn (self, "failed to power down modem before updating initial EPS bearer settings: %s", ctx->saved_error->message);
        ctx->step = HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST;
    } else {
        /* If we could not load the previous power state, assume it was 'on', which is the most likely
         * case on these type of failures. */
        if (ctx->previous_power_state == MM_MODEM_POWER_STATE_UNKNOWN) {
            mm_obj_warn (ctx->self, "power state before the initial EPS bearer settings is unknown, will assume 'on'");
            ctx->previous_power_state = MM_MODEM_POWER_STATE_ON;
        }

        mm_obj_dbg (self, "modem power state updated: %s -> %s",
                    mm_modem_power_state_get_string (ctx->previous_power_state),
                    mm_modem_power_state_get_string (MM_MODEM_POWER_STATE_LOW));
        ctx->step++;
    }

    handle_set_initial_eps_bearer_settings_step (ctx);
}

static void
handle_set_initial_eps_bearer_settings_step (HandleSetInitialEpsBearerSettingsContext *ctx)
{
    switch (ctx->step) {
        case HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_FIRST:
            ctx->step++;
            /* fall through */

        case HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_POWER_DOWN:
            mm_obj_msg (ctx->self, "set initial EPS bearer settings state (%d/%d): power down",
                        ctx->step, HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST);
            mm_iface_modem_set_power_state (
                MM_IFACE_MODEM (ctx->self),
                MM_MODEM_POWER_STATE_LOW,
                (GAsyncReadyCallback)handle_set_initial_eps_bearer_settings_power_down_ready,
                ctx);
            return;

        case HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_UPDATE:
            mm_obj_msg (ctx->self, "set initial EPS bearer settings state (%d/%d): update",
                        ctx->step, HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST);
            mm_iface_modem_3gpp_profile_manager_update_ignore_start (MM_IFACE_MODEM_3GPP_PROFILE_MANAGER (ctx->self));
            MM_IFACE_MODEM_3GPP_GET_IFACE (ctx->self)->set_initial_eps_bearer_settings (
                ctx->self,
                ctx->config,
                (GAsyncReadyCallback)handle_set_initial_eps_bearer_settings_update_ready,
                ctx);
            return;

        case HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_PREVIOUS_POWER:
            mm_obj_msg (ctx->self, "set initial EPS bearer settings state (%d/%d): recover previous power state",
                        ctx->step, HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST);
            mm_iface_modem_set_power_state (
                MM_IFACE_MODEM (ctx->self),
                ctx->previous_power_state,
                (GAsyncReadyCallback)handle_set_initial_eps_bearer_settings_previous_power_ready,
                ctx);
            return;

        case HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_RELOAD:
            mm_obj_msg (ctx->self, "set initial EPS bearer settings state (%d/%d): reload",
                        ctx->step, HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST);
            if (MM_IFACE_MODEM_3GPP_GET_IFACE (ctx->self)->load_initial_eps_bearer_settings &&
                MM_IFACE_MODEM_3GPP_GET_IFACE (ctx->self)->load_initial_eps_bearer_settings_finish) {
                MM_IFACE_MODEM_3GPP_GET_IFACE (ctx->self)->load_initial_eps_bearer_settings (
                    ctx->self,
                    (GAsyncReadyCallback)handle_set_initial_eps_bearer_settings_reload_ready,
                    ctx);
                return;
            }
            /* Otherwise, assume we're ok */

            ctx->step++;
            /* fall through */

        case HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST:
            if (ctx->saved_error) {
                mm_obj_msg (ctx->self, "set initial EPS bearer settings state (%d/%d): failed",
                            ctx->step, HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST);
                mm_dbus_method_invocation_take_error (ctx->invocation, g_steal_pointer (&ctx->saved_error));
            } else {
                mm_obj_msg (ctx->self, "set initial EPS bearer settings state (%d/%d): all done",
                            ctx->step, HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_LAST);
                mm_gdbus_modem3gpp_complete_set_initial_eps_bearer_settings (ctx->skeleton, ctx->invocation);
            }
            handle_set_initial_eps_bearer_settings_context_free (ctx);
            return;

        default:
            g_assert_not_reached ();
    }
}

static void
set_initial_eps_bearer_settings_auth_ready (MMIfaceOpLock                            *_self,
                                            GAsyncResult                             *res,
                                            HandleSetInitialEpsBearerSettingsContext *ctx)
{
    MMBaseModem                   *self = MM_BASE_MODEM (_self);
    gboolean                       force = FALSE;
    GError                        *error = NULL;
    GVariant                      *old_dictionary;
    g_autoptr(MMBearerProperties)  old_config = NULL;

    ctx->operation_id = mm_iface_op_lock_authorize_and_lock_finish (_self, res, &error);
    if (ctx->operation_id < 0) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    /* If UE mode update is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_initial_eps_bearer_settings ||
        !MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_initial_eps_bearer_settings_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Operation not supported");
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    ctx->config = mm_bearer_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!ctx->config) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    force = mm_bearer_properties_get_force (ctx->config);
    mm_obj_info (self, "processing user request to set initial EPS bearer settings%s...", force ? " (forced)" : "");

    mm_log_bearer_properties (self, MM_LOG_LEVEL_INFO, "  ", ctx->config);

    old_dictionary = mm_gdbus_modem3gpp_get_initial_eps_bearer_settings (ctx->skeleton);
    if (old_dictionary)
        old_config = mm_bearer_properties_new_from_dictionary (old_dictionary, NULL);

    if (!force && old_config && mm_bearer_properties_cmp (ctx->config, old_config, MM_BEARER_PROPERTIES_CMP_FLAGS_EPS)) {
        mm_obj_info (self, "skipped setting initial EPS bearer settings: same configuration provided");
        mm_gdbus_modem3gpp_complete_set_initial_eps_bearer_settings (ctx->skeleton, ctx->invocation);
        handle_set_initial_eps_bearer_settings_context_free (ctx);
        return;
    }

    /* Launch procedure */
    ctx->step = HANDLE_SET_INITIAL_EPS_BEARER_SETTINGS_STEP_FIRST;
    handle_set_initial_eps_bearer_settings_step (ctx);
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
    ctx->previous_power_state = MM_MODEM_POWER_STATE_UNKNOWN;
    ctx->operation_id = -1;

    mm_iface_op_lock_authorize_and_lock (MM_IFACE_OP_LOCK (self),
                                         invocation,
                                         MM_AUTHORIZATION_DEVICE_CONTROL,
                                         MM_OPERATION_PRIORITY_DEFAULT,
                                         "set-initial-eps-bearer-settings",
                                         (GAsyncReadyCallback)set_initial_eps_bearer_settings_auth_ready,
                                         ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp      *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp      *self;
    GVariant              *dictionary;
    MMModem3gppFacility    facility;
    gchar                 *facility_str;
    guint8                 slot;
    gchar                 *control_key;
} HandleDisableFacilityLockContext;

static void
handle_disable_facility_lock_context_free (HandleDisableFacilityLockContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_variant_unref (ctx->dictionary);
    g_free (ctx->control_key);
    g_free (ctx->facility_str);
    g_slice_free (HandleDisableFacilityLockContext, ctx);
}

static void
update_lock_info_ready (MMIfaceModem                     *modem,
                        GAsyncResult                     *res,
                        HandleDisableFacilityLockContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_update_lock_info_finish (modem, res, &error);
    if (error) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_disable_facility_lock_context_free (ctx);
        return;
    }

    mm_gdbus_modem3gpp_complete_disable_facility_lock (ctx->skeleton, ctx->invocation);
    handle_disable_facility_lock_context_free (ctx);
}

static void
handle_disable_facility_lock_ready (MMIfaceModem3gpp                 *self,
                                    GAsyncResult                     *res,
                                    HandleDisableFacilityLockContext *ctx)
{
    MMModem3gppFacility  facilities;
    GError              *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_facility_lock_finish (self, res, &error)) {
        mm_obj_warn (self, "failed disabling facility lock '%s': %s",
                     ctx->facility_str, error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_disable_facility_lock_context_free (ctx);
        return;
    }

    mm_obj_info (self, "facility lock '%s' disabled", ctx->facility_str);

    /* Update the Enabled Facility Locks property in the DBus interface */
    facilities = mm_gdbus_modem3gpp_get_enabled_facility_locks (ctx->skeleton);
    facilities &= ~ctx->facility;
    mm_gdbus_modem3gpp_set_enabled_facility_locks (ctx->skeleton, facilities);

    /* Recheck lock status after unlock code has been sent  */
    mm_iface_modem_update_lock_info (MM_IFACE_MODEM (self),
                                     MM_MODEM_LOCK_UNKNOWN, /* ask */
                                     (GAsyncReadyCallback)update_lock_info_ready,
                                     ctx);
}

static void
disable_facility_lock_auth_ready (MMIfaceAuth                      *auth,
                                  GAsyncResult                     *res,
                                  HandleDisableFacilityLockContext *ctx)
{
    MMIfaceModem3gpp *self = MM_IFACE_MODEM_3GPP (auth);
    GError *error = NULL;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_disable_facility_lock_context_free (ctx);
        return;
    }

    /* If disable facility locks is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_facility_lock ||
        !MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_facility_lock_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Operation not supported");
        handle_disable_facility_lock_context_free (ctx);
        return;
    }

    /* Parse properties dictionary */
    if (!g_variant_is_of_type (ctx->dictionary, G_VARIANT_TYPE ("(us)"))) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                        "Invalid parameters");
        handle_disable_facility_lock_context_free (ctx);
        return;
    }

    /* Only modems with single slot or single configuration for all slots are supported */
    ctx->slot = 1;

    g_variant_get (ctx->dictionary, "(us)", &ctx->facility, &ctx->control_key);

    /* Only four facility locks can be disabled:
     * - MM_MODEM_3GPP_FACILITY_NET_PERS (network personalization)
     * - MM_MODEM_3GPP_FACILITY_NET_SUB_PERS (network subset personalization)
     * - MM_MODEM_3GPP_FACILITY_PROVIDER_PERS (service provider personalization)
     * - MM_MODEM_3GPP_FACILITY_CORP_PERS (corporate personalization)
     */
    if (ctx->facility != (ctx->facility & (MM_MODEM_3GPP_FACILITY_NET_PERS |
                                           MM_MODEM_3GPP_FACILITY_NET_SUB_PERS |
                                           MM_MODEM_3GPP_FACILITY_PROVIDER_PERS |
                                           MM_MODEM_3GPP_FACILITY_CORP_PERS))) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                        "Invalid type of facility lock to disable or empty key");
        handle_disable_facility_lock_context_free (ctx);
        return;
    }

    ctx->facility_str = mm_modem_3gpp_facility_build_string_from_mask (ctx->facility);
    mm_obj_info (self, "processing user request to disable facility lock '%s'...", ctx->facility_str);
    mm_obj_info (self, "  control key: %s", mm_log_str_personal_info (ctx->control_key));
    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_facility_lock (
        MM_IFACE_MODEM_3GPP (self),
        ctx->facility,
        ctx->slot,
        ctx->control_key,
        (GAsyncReadyCallback)handle_disable_facility_lock_ready,
        ctx);
}

static gboolean
handle_disable_facility_lock (MmGdbusModem3gpp      *skeleton,
                              GDBusMethodInvocation *invocation,
                              GVariant              *dictionary,
                              MMIfaceModem3gpp      *self)
{
    HandleDisableFacilityLockContext *ctx;

    ctx = g_slice_new0 (HandleDisableFacilityLockContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)disable_facility_lock_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Set Packet Service State (internal) */

gboolean
mm_iface_modem_3gpp_set_packet_service_state_finish (MMIfaceModem3gpp  *self,
                                                     GAsyncResult      *res,
                                                     GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
set_packet_service_state_ready (MMIfaceModem3gpp *self,
                                GAsyncResult     *res,
                                GTask            *task)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_packet_service_state_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_iface_modem_3gpp_set_packet_service_state (MMIfaceModem3gpp              *self,
                                              MMModem3gppPacketServiceState  packet_service_state,
                                              GAsyncReadyCallback            callback,
                                              gpointer                       user_data)
{
    GTask *task;

    g_assert (packet_service_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED ||
              packet_service_state == MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED);

    task = g_task_new (self, NULL, callback, user_data);

    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_packet_service_state ||
        !MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_packet_service_state_finish) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                 "Explicit packet service attach/detach operation not supported");
        g_object_unref (task);
        return;
    }

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_packet_service_state (
        self,
        packet_service_state,
        (GAsyncReadyCallback)set_packet_service_state_ready,
        task);
}

/*****************************************************************************/
/* Set Packet Service State */

typedef struct {
    MMIfaceModem3gpp               *self;
    MmGdbusModem3gpp               *skeleton;
    GDBusMethodInvocation          *invocation;
    MMModem3gppPacketServiceState   packet_service_state;
} HandlePacketServiceStateContext;

static void
handle_set_packet_service_state_context_free (HandlePacketServiceStateContext *ctx)
{
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->self);
    g_slice_free (HandlePacketServiceStateContext,ctx);
}

static void
internal_set_packet_service_state_ready (MMIfaceModem3gpp                *self,
                                         GAsyncResult                    *res,
                                         HandlePacketServiceStateContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_3gpp_set_packet_service_state_finish (self, res, &error)) {
        mm_obj_warn (self, "failed setting packet service state to '%s': %s",
                     mm_modem_3gpp_packet_service_state_get_string (ctx->packet_service_state),
                     error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    } else {
        mm_obj_info (self, "packet service state set to '%s'",
                     mm_modem_3gpp_packet_service_state_get_string (ctx->packet_service_state));
        mm_gdbus_modem3gpp_complete_set_packet_service_state (ctx->skeleton, ctx->invocation);
    }
    handle_set_packet_service_state_context_free (ctx);
}

static void
set_packet_service_state_auth_ready (MMIfaceAuth                     *auth,
                                     GAsyncResult                    *res,
                                     HandlePacketServiceStateContext *ctx)
{
    MMIfaceModem3gpp *self = MM_IFACE_MODEM_3GPP (auth);
    GError *error = NULL;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_packet_service_state_context_free (ctx);
        return;
    }

    if (mm_iface_modem_abort_invocation_if_state_not_reached (MM_IFACE_MODEM (self),
                                                              ctx->invocation,
                                                              MM_MODEM_STATE_ENABLED)) {
        handle_set_packet_service_state_context_free (ctx);
        return;
    }

    if ((ctx->packet_service_state != MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED) &&
        (ctx->packet_service_state != MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED)) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS,
                                                        "Invalid packet service state requested");
        handle_set_packet_service_state_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to set packet service state to '%s'...",
                 mm_modem_3gpp_packet_service_state_get_string (ctx->packet_service_state));
    mm_iface_modem_3gpp_set_packet_service_state (ctx->self,
                                                  ctx->packet_service_state,
                                                  (GAsyncReadyCallback)internal_set_packet_service_state_ready,
                                                  ctx);
}

static gboolean
handle_set_packet_service_state (MmGdbusModem3gpp              *skeleton,
                                 GDBusMethodInvocation         *invocation,
                                 MMModem3gppPacketServiceState  packet_service_state,
                                 MMIfaceModem3gpp              *self)
{
    HandlePacketServiceStateContext *ctx;

    ctx = g_slice_new0 (HandlePacketServiceStateContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->packet_service_state = packet_service_state;

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)set_packet_service_state_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp           *skeleton;
    GDBusMethodInvocation      *invocation;
    gssize                      operation_id;
    MMIfaceModem3gpp           *self;
    GVariant                   *dictionary;
    MMNr5gRegistrationSettings *settings;
} HandleSetNr5gRegistrationSettingsContext;

static void
handle_set_nr5g_registration_settings_context_free (HandleSetNr5gRegistrationSettingsContext *ctx)
{
    if (ctx->operation_id >= 0)
        mm_iface_op_lock_unlock (MM_IFACE_OP_LOCK (ctx->self), ctx->operation_id);

    g_clear_object (&ctx->settings);
    g_variant_unref (ctx->dictionary);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSetNr5gRegistrationSettingsContext, ctx);
}

static void
after_set_load_nr5g_registration_settings_ready (MMIfaceModem3gpp                         *self,
                                                 GAsyncResult                             *res,
                                                 HandleSetNr5gRegistrationSettingsContext *ctx)
{
    GError                                *error = NULL;
    g_autoptr(MMNr5gRegistrationSettings)  new_settings = NULL;
    g_autoptr(GVariant)                    dictionary = NULL;

    new_settings = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_nr5g_registration_settings_finish (self, res, &error);
    if (error) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    mm_obj_info (self, "5GNR registration settings updated");

    if (!mm_nr5g_registration_settings_cmp (new_settings, ctx->settings)) {
        mm_obj_info (self, "requested and reloaded 5GNR registration settings don't match");
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                                        "5GNR registration settings were not updated");
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    dictionary = mm_nr5g_registration_settings_get_dictionary (new_settings);
    mm_gdbus_modem3gpp_set_nr5g_registration_settings (ctx->skeleton, dictionary);
    mm_gdbus_modem3gpp_complete_set_nr5g_registration_settings (ctx->skeleton, ctx->invocation);
    handle_set_nr5g_registration_settings_context_free (ctx);
}

static void
set_nr5g_registration_settings_ready (MMIfaceModem3gpp                         *self,
                                      GAsyncResult                             *res,
                                      HandleSetNr5gRegistrationSettingsContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_nr5g_registration_settings_finish (self, res, &error)) {
        mm_obj_warn (self, "failed setting 5GNR registration settings: %s", error->message);
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_nr5g_registration_settings &&
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_nr5g_registration_settings_finish) {
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_nr5g_registration_settings (
            self,
            (GAsyncReadyCallback)after_set_load_nr5g_registration_settings_ready,
            ctx);
        return;
    }

    /* Assume we're ok */
    mm_obj_info (self, "5GNR registration settings updated");
    mm_gdbus_modem3gpp_complete_set_nr5g_registration_settings (ctx->skeleton, ctx->invocation);
    handle_set_nr5g_registration_settings_context_free (ctx);
}

static void
set_nr5g_registration_settings_auth_ready (MMIfaceOpLock                            *_self,
                                           GAsyncResult                             *res,
                                           HandleSetNr5gRegistrationSettingsContext *ctx)
{
    MMBaseModem                           *self = MM_BASE_MODEM (_self);
    GError                                *error = NULL;
    GVariant                              *old_dictionary;
    g_autoptr(MMNr5gRegistrationSettings)  old_settings = NULL;
    MMModem3gppDrxCycle                    new_drx_cycle;
    MMModem3gppMicoMode                    new_mico_mode;

    ctx->operation_id = mm_iface_op_lock_authorize_and_lock_finish (_self, res, &error);
    if (ctx->operation_id < 0) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    /* If 5GNR registration settings update is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_nr5g_registration_settings ||
        !MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_nr5g_registration_settings_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Operation not supported");
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    ctx->settings = mm_nr5g_registration_settings_new_from_dictionary (ctx->dictionary, &error);
    if (!ctx->settings) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    new_drx_cycle = mm_nr5g_registration_settings_get_drx_cycle (ctx->settings);
    if (new_drx_cycle == MM_MODEM_3GPP_DRX_CYCLE_UNSUPPORTED) {
        g_set_error (&error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Invalid value for DRX cycle: %s",
                     mm_modem_3gpp_drx_cycle_get_string (new_drx_cycle));
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    new_mico_mode = mm_nr5g_registration_settings_get_mico_mode (ctx->settings);
    if (new_mico_mode == MM_MODEM_3GPP_MICO_MODE_UNSUPPORTED) {
        g_set_error (&error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS, "Invalid value for MICO mode: %s",
                     mm_modem_3gpp_mico_mode_get_string (new_mico_mode));
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    mm_obj_info (self, "processing user request to set 5GNR registration settings...");

    old_dictionary = mm_gdbus_modem3gpp_get_nr5g_registration_settings (ctx->skeleton);
    if (old_dictionary)
        old_settings = mm_nr5g_registration_settings_new_from_dictionary (old_dictionary, NULL);

    if (old_settings && mm_nr5g_registration_settings_cmp (ctx->settings, old_settings)) {
        mm_obj_info (self, "skipped setting 5GNR registration settings: same configuration provided");
        mm_gdbus_modem3gpp_complete_set_nr5g_registration_settings (ctx->skeleton, ctx->invocation);
        handle_set_nr5g_registration_settings_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_nr5g_registration_settings (
        MM_IFACE_MODEM_3GPP (self),
        ctx->settings,
        (GAsyncReadyCallback)set_nr5g_registration_settings_ready,
        ctx);
}

static gboolean
handle_set_nr5g_registration_settings (MmGdbusModem3gpp      *skeleton,
                                       GDBusMethodInvocation *invocation,
                                       GVariant              *dictionary,
                                       MMIfaceModem3gpp      *self)
{
    HandleSetNr5gRegistrationSettingsContext *ctx;

    ctx = g_slice_new0 (HandleSetNr5gRegistrationSettingsContext);
    ctx->skeleton   = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self       = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);
    ctx->operation_id = -1;

    mm_iface_op_lock_authorize_and_lock (MM_IFACE_OP_LOCK (self),
                                         invocation,
                                         MM_AUTHORIZATION_DEVICE_CONTROL,
                                         MM_OPERATION_PRIORITY_DEFAULT,
                                         "set-nr5g-registration-settings",
                                         (GAsyncReadyCallback)set_nr5g_registration_settings_auth_ready,
                                         ctx);
    return TRUE;
}

/*****************************************************************************/

gboolean
mm_iface_modem_3gpp_run_registration_checks_finish (MMIfaceModem3gpp  *self,
                                                    GAsyncResult      *res,
                                                    GError           **error)
{
    g_assert (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->run_registration_checks_finish != NULL);
    return MM_IFACE_MODEM_3GPP_GET_IFACE (self)->run_registration_checks_finish (self, res, error);
}

void
mm_iface_modem_3gpp_run_registration_checks (MMIfaceModem3gpp    *self,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
    gboolean is_cs_supported;
    gboolean is_ps_supported;
    gboolean is_eps_supported;
    gboolean is_5gs_supported;

    g_assert (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->run_registration_checks != NULL);

    is_cs_supported  = get_cs_network_supported  (self);
    is_ps_supported  = get_ps_network_supported  (self);
    is_eps_supported = get_eps_network_supported (self);
    is_5gs_supported = get_5gs_network_supported (self);

    mm_obj_dbg (self, "running registration checks (CS: '%s', PS: '%s', EPS: '%s', 5GS: '%s')",
                is_cs_supported ? "yes" : "no",
                is_ps_supported ? "yes" : "no",
                is_eps_supported ? "yes" : "no",
                is_5gs_supported ? "yes" : "no");

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->run_registration_checks (self,
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

    str = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_operator_name_finish (self, res, &error);
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

    ctx = g_task_get_task_data (task);

    str = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_operator_code_finish (self, res, &error);
    if (error) {
        mm_obj_warn (self, "couldn't load operator code: %s", error->message);
    } else if (!mm_3gpp_parse_operator_id (str, NULL, NULL, NULL, &error)) {
        mm_obj_warn (self, "unexpected operator code string '%s': %s", str, error->message);
        g_clear_pointer (&str, g_free);
    }
    g_clear_error (&error);

    if (ctx->skeleton)
        mm_gdbus_modem3gpp_set_operator_code (ctx->skeleton, str);

    /* If we also implement the location interface, update the 3GPP location */
    if (str && MM_IS_IFACE_MODEM_LOCATION (self))
        mm_iface_modem_location_3gpp_update_operator_code (MM_IFACE_MODEM_LOCATION (self), str);

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
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_operator_code (
            self,
            (GAsyncReadyCallback)load_operator_code_ready,
            task);
        return;
    }

    if (!ctx->operator_name_loaded) {
        /* Launch operator name update */
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_operator_name (
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

    ctx->operator_code_loaded = !(MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_operator_code &&
                                  MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_operator_code_finish);
    if (ctx->operator_code_loaded) {
        mm_gdbus_modem3gpp_set_operator_code (ctx->skeleton, NULL);
        if (MM_IS_IFACE_MODEM_LOCATION (self))
            mm_iface_modem_location_3gpp_update_operator_code (MM_IFACE_MODEM_LOCATION (self), NULL);
    }

    ctx->operator_name_loaded = !(MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_operator_name &&
                                  MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_operator_name_finish);
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
        mm_iface_modem_location_3gpp_update_operator_code (MM_IFACE_MODEM_LOCATION (self), NULL);
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_update_access_technologies (MMIfaceModem3gpp        *self,
                                                MMModemAccessTechnology  access_tech)
{
    Private                      *priv;
    MMModem3gppRegistrationState  state;

    priv = get_private (self);
    if (!priv->iface_enabled)
        return;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  NULL);

    /* Even if registration state didn't change, report access technology,
     * but only if something valid to report */
    if (mm_modem_3gpp_registration_state_is_registered (state) || priv->reloading_registration_info) {
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
    if (!priv->iface_enabled)
        return;

    if (!MM_IS_IFACE_MODEM_LOCATION (self))
        return;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  NULL);

    /* Even if registration state didn't change, report access technology or
     * location updates, but only if something valid to report. For the case
     * where we're registering (loading current registration info after a state
     * change to registered), we also allow LAC/CID updates. */
    if (mm_modem_3gpp_registration_state_is_registered (state) || priv->reloading_registration_info) {
        if (location_area_code || tracking_area_code || cell_id)
            mm_iface_modem_location_3gpp_update_lac_tac_ci (MM_IFACE_MODEM_LOCATION (self),
                                                            location_area_code,
                                                            tracking_area_code,
                                                            cell_id);
    } else
        mm_iface_modem_location_3gpp_clear (MM_IFACE_MODEM_LOCATION (self));
}

/*****************************************************************************/

static void update_packet_service_state (MMIfaceModem3gpp              *self,
                                         MMModem3gppPacketServiceState  new_state);

static void
update_registration_reload_current_registration_info_ready (MMIfaceModem3gpp *self,
                                                            GAsyncResult     *res,
                                                            gpointer          user_data)
{
    Private                      *priv;
    MMModem3gppRegistrationState  new_state;

    priv = get_private (self);
    if (!priv->iface_enabled)
        return;

    new_state = GPOINTER_TO_UINT (user_data);

    /* Update packet service state if we don't support external updates */
    if (!priv->packet_service_state_update_supported)
        update_packet_service_state (self, get_consolidated_packet_service_state (self));

    mm_obj_msg (self, "3GPP registration state changed (registering -> %s)",
                mm_modem_3gpp_registration_state_get_string (new_state));
    mm_obj_info (self, "consolidated registration state: cs '%s', ps '%s', eps '%s', 5gs '%s' --> '%s'",
                mm_modem_3gpp_registration_state_get_string (priv->state_cs),
                mm_modem_3gpp_registration_state_get_string (priv->state_ps),
                mm_modem_3gpp_registration_state_get_string (priv->state_eps),
                mm_modem_3gpp_registration_state_get_string (priv->state_5gs),
                mm_modem_3gpp_registration_state_get_string (new_state));

    /* The properties in the interface are bound to the properties
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
    Private *priv;

    priv = get_private (self);
    if (!priv->iface_enabled)
        return;

    /* Not registered neither in home nor roaming network */
    mm_iface_modem_3gpp_clear_current_operator (self);

    /* No packet service if we're not registered. This change is done
     * also when the device itself supports reporting the packet service
     * state updates. */
    update_packet_service_state (self, MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED);

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
                           MMModem3gppRegistrationState  new_state)
{
    Private                      *priv;
    MMModem3gppRegistrationState  old_state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

    priv = get_private (self);
    if (!priv->iface_enabled)
        return;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &old_state,
                  NULL);

    /* Only set new state if different */
    if (new_state == old_state) {
        MMModem3gppPacketServiceState old_packet_service_state = MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN;

        /* If packet service updates are expected, we can ignore the packet service state as that
         * info won't be used to build a consolidated packet service state */
        if (priv->packet_service_state_update_supported)
            return;

        /* If packet service updates are not expected, also check whether there are changes
         * in the consolidate packet service state */
        g_object_get (self,
                      MM_IFACE_MODEM_3GPP_PACKET_SERVICE_STATE, &old_packet_service_state,
                      NULL);
        if (old_packet_service_state == get_consolidated_packet_service_state (self))
            return;
    }

    if (mm_modem_3gpp_registration_state_is_registered (new_state)) {
        MMModemState modem_state;

        /* When moving to registered state, clear network rejection */
        mm_iface_modem_3gpp_update_network_rejection (self,
                                                      MM_NETWORK_ERROR_NONE,
                                                      NULL,
                                                      NULL,
                                                      MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

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

        mm_obj_msg (self, "3GPP registration state changed (%s -> registering)",
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

    mm_obj_msg (self, "3GPP registration state changed (%s -> %s)",
                mm_modem_3gpp_registration_state_get_string (old_state),
                mm_modem_3gpp_registration_state_get_string (new_state));
    mm_obj_info (self, "consolidated registration state: cs '%s', ps '%s', eps '%s', 5gs '%s' --> '%s'",
                mm_modem_3gpp_registration_state_get_string (priv->state_cs),
                mm_modem_3gpp_registration_state_get_string (priv->state_ps),
                mm_modem_3gpp_registration_state_get_string (priv->state_eps),
                mm_modem_3gpp_registration_state_get_string (priv->state_5gs),
                mm_modem_3gpp_registration_state_get_string (new_state));

    update_non_registered_state (self, old_state, new_state);
}

#define UPDATE_REGISTRATION_STATE(domain)                                                             \
    void                                                                                              \
    mm_iface_modem_3gpp_update_##domain##_registration_state (MMIfaceModem3gpp             *self,     \
                                                              MMModem3gppRegistrationState  state,    \
                                                              gboolean                      deferred) \
    {                                                                                                 \
        Private  *priv;                                                                               \
                                                                                                      \
        if (!get_##domain##_network_supported (self))                                                 \
            return;                                                                                   \
                                                                                                      \
        priv = get_private (self);                                                                    \
        if (!priv->iface_enabled)                                                                       \
            return;                                                                                   \
        priv->state_##domain = state;                                                                 \
                                                                                                      \
        if (!deferred)                                                                                \
            mm_iface_modem_3gpp_apply_deferred_registration_state (self);                             \
    }

UPDATE_REGISTRATION_STATE (cs)
UPDATE_REGISTRATION_STATE (ps)
UPDATE_REGISTRATION_STATE (eps)
UPDATE_REGISTRATION_STATE (5gs)

void
mm_iface_modem_3gpp_apply_deferred_registration_state (MMIfaceModem3gpp *self)
{
    update_registration_state (self, get_consolidated_reg_state (self));
}

/*****************************************************************************/
/* Packet service state as reported by the device */

static void
update_packet_service_state (MMIfaceModem3gpp              *self,
                             MMModem3gppPacketServiceState  new_state)
{
    MMModem3gppPacketServiceState  old_state = MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN;
    Private                       *priv;

    priv = get_private (self);
    if (!priv->iface_enabled)
        return;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PACKET_SERVICE_STATE, &old_state,
                  NULL);

    /* Only set new state if different */
    if (old_state == new_state)
        return;

    mm_obj_msg (self, "3GPP packet service state changed (%s -> %s)",
                mm_modem_3gpp_packet_service_state_get_string (old_state),
                mm_modem_3gpp_packet_service_state_get_string (new_state));

    /* The properties in the interface are bound to the properties
     * in the skeleton, so just updating here is enough */
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_PACKET_SERVICE_STATE, new_state,
                  NULL);
}

void
mm_iface_modem_3gpp_update_packet_service_state (MMIfaceModem3gpp              *self,
                                                 MMModem3gppPacketServiceState  new_state)
{
    Private *priv;

    priv = get_private (self);
    priv->packet_service_state_update_supported = TRUE;
    update_packet_service_state (self, new_state);
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
    GVariantBuilder   builder;
    GVariant         *variant;
    const GList      *iter;
    Private          *priv;

    priv = get_private (self);
    if (!priv->iface_enabled)
        return;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ubay)"));
    for (iter = pco_list; iter; iter = g_list_next (iter)) {
        g_autoptr(GVariant) pco_variant = NULL;

        pco_variant = mm_pco_to_variant (MM_PCO (iter->data));
        g_variant_builder_add_value (&builder, pco_variant);
    }
    variant = g_variant_ref_sink (g_variant_builder_end (&builder));
    mm_gdbus_modem3gpp_set_pco (skeleton, variant);
    g_variant_unref (variant);
    g_object_unref (skeleton);
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_update_network_rejection (MMIfaceModem3gpp       *self,
                                              MMNetworkError          error,
                                              const gchar            *operator_id,
                                              const gchar            *operator_name,
                                              MMModemAccessTechnology access_technology)
{
    MmGdbusModem3gpp              *skeleton = NULL;
    Private                       *priv;
    g_autoptr(MMNetworkRejection)  new_code = NULL;
    g_autoptr(GVariant)            dictionary = NULL;
    g_autofree gchar              *access_tech_str = NULL;
    const gchar                   *nw_rejection_error = NULL;

    priv = get_private (self);
    if (!priv->iface_enabled)
        return;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (!error) {
        mm_gdbus_modem3gpp_set_network_rejection (skeleton, NULL);
        return;
    }

    access_tech_str = mm_modem_access_technology_build_string_from_mask (access_technology);
    nw_rejection_error = mm_network_error_get_string (error);

    mm_obj_warn (self, "Network rejection received: reason '%s' (%u), "
                 "operator id '%s', operator name '%s', access technology '%s'",
                 nw_rejection_error ? nw_rejection_error : "unknown", error,
                 operator_id ? operator_id : "none",
                 operator_name ? operator_name : "none",
                 access_tech_str);

    new_code = mm_network_rejection_new ();
    mm_network_rejection_set_error (new_code, error);
    mm_network_rejection_set_operator_id (new_code, operator_id);
    mm_network_rejection_set_operator_name (new_code, operator_name);
    mm_network_rejection_set_access_technology (new_code, access_technology);

    dictionary = mm_network_rejection_get_dictionary (new_code);
    mm_gdbus_modem3gpp_set_network_rejection (skeleton, dictionary);
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_update_initial_eps_bearer (MMIfaceModem3gpp   *self,
                                               MMBearerProperties *properties)
{
    g_autoptr(MmGdbusModem3gppSkeleton) skeleton = NULL;
    g_autoptr(MMBaseBearer)             old_bearer = NULL;
    g_autoptr(MMBaseBearer)             new_bearer = NULL;

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
        return;

    if (!properties) {
        mm_obj_dbg (self, "clearing initial EPS bearer...");
        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_INITIAL_EPS_BEARER, NULL,
                      NULL);
        mm_gdbus_modem3gpp_set_initial_eps_bearer (MM_GDBUS_MODEM3GPP (skeleton), NULL);
        return;
    }

    mm_obj_dbg (self, "updating initial EPS bearer...");
    g_assert (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->create_initial_eps_bearer);
    new_bearer = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->create_initial_eps_bearer (self, properties);
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_INITIAL_EPS_BEARER, new_bearer,
                  NULL);
    mm_gdbus_modem3gpp_set_initial_eps_bearer (MM_GDBUS_MODEM3GPP (skeleton),
                                               mm_base_bearer_get_path (new_bearer));
}

static void
reload_initial_eps_bearer_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult     *res)
{
    g_autoptr(MMBearerProperties) properties = NULL;
    g_autoptr(GError)             error = NULL;

    properties = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_finish (self, res, &error);
    if (!properties) {
        mm_obj_dbg (self, "couldn't load initial default bearer properties: %s", error->message);
        return;
    }

    mm_iface_modem_3gpp_update_initial_eps_bearer (self, properties);
}

void
mm_iface_modem_3gpp_reload_initial_eps_bearer (MMIfaceModem3gpp *self)
{
    if (get_eps_network_supported (self) &&
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer &&
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_finish) {
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer (
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
    DisablingStep  step;
    MmGdbusModem  *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    g_clear_object (&ctx->skeleton);
    g_slice_free (DisablingContext, ctx);
}

gboolean
mm_iface_modem_3gpp_disable_finish (MMIfaceModem3gpp  *self,
                                    GAsyncResult      *res,
                                    GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME,DISPLAY)                               \
    static void                                                         \
    NAME##_ready (MMIfaceModem3gpp *self,                               \
                  GAsyncResult     *res,                                \
                  GTask            *task)                               \
    {                                                                   \
        DisablingContext  *ctx;                                         \
        g_autoptr(GError)  error = NULL;                                \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->NAME##_finish (self, res, &error); \
        if (error)                                                      \
            mm_obj_dbg (self, "couldn't %s: %s", DISPLAY, error->message);      \
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
    Private          *priv;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    priv = get_private (self);
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

    case DISABLING_STEP_DISABLE_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_registration_events (
                self,
                get_cs_network_supported (self),
                get_ps_network_supported (self),
                get_eps_network_supported (self),
                (GAsyncReadyCallback)disable_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->cleanup_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->cleanup_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->cleanup_unsolicited_registration_events (
                self,
                (GAsyncReadyCallback)cleanup_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->cleanup_unsolicited_events (
                self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_REGISTRATION_STATE:
        update_packet_service_state (self, MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN);
        update_registration_state (self, MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN);
        mm_iface_modem_3gpp_update_access_technologies (self, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_iface_modem_3gpp_update_location (self, 0, 0, 0);
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_LAST:
        /* Interface state is assumed enabled until the very end of the disabling sequence,
         * so that updates are taken into account and not ignored. */
        priv->iface_enabled = FALSE;
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
mm_iface_modem_3gpp_disable (MMIfaceModem3gpp    *self,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
    DisablingContext *ctx;
    GTask            *task;

    ctx = g_slice_new0 (DisablingContext);
    ctx->step = DISABLING_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)disabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
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
    EnablingStep      step;
    MmGdbusModem3gpp *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    g_clear_object (&ctx->skeleton);
    g_slice_free (EnablingContext, ctx);
}

gboolean
mm_iface_modem_3gpp_enable_finish (MMIfaceModem3gpp  *self,
                                   GAsyncResult      *res,
                                   GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                GAsyncResult     *res,
                                GTask            *task)
{
    EnablingContext   *ctx;
    g_autoptr(GError)  error = NULL;

    ctx = g_task_get_task_data (task);

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "setting up unsolicited events failed: %s", error->message);

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
                                 GAsyncResult     *res,
                                 GTask            *task)
{
    EnablingContext   *ctx;
    g_autoptr(GError)  error = NULL;

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_events_finish (self, res, &error);
    if (error)
        mm_obj_dbg (self, "enabling unsolicited events failed: %s", error->message);

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
setup_unsolicited_registration_events_ready (MMIfaceModem3gpp *self,
                                             GAsyncResult     *res,
                                             GTask            *task)
{
    EnablingContext   *ctx;
    g_autoptr(GError)  error = NULL;

    ctx = g_task_get_task_data (task);

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->setup_unsolicited_registration_events_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "setting up unsolicited registration events failed: %s", error->message);
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (self);

        /* If we get an error setting up unsolicited events, don't even bother trying to
         * enable them. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS + 1;
        interface_enabling_step (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (task);
}

static void
enable_unsolicited_registration_events_ready (MMIfaceModem3gpp *self,
                                              GAsyncResult     *res,
                                              GTask            *task)
{
    EnablingContext   *ctx;
    g_autoptr(GError)  error = NULL;

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_registration_events_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "enabling unsolicited registration events failed: %s", error->message);
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
    EnablingContext               *ctx;
    g_autoptr(MMBearerProperties)  properties = NULL;
    g_autoptr(GError)              error = NULL;

    ctx = g_task_get_task_data (task);

    properties = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_finish (self, res, &error);
    if (!properties)
        mm_obj_dbg (self, "couldn't load initial default bearer properties: %s", error->message);
    else
        mm_iface_modem_3gpp_update_initial_eps_bearer (self, properties);

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (task);
}

static void
interface_enabling_step (GTask *task)
{
    MMIfaceModem3gpp *self;
    Private          *priv;
    EnablingContext  *ctx;

    self = g_task_get_source_object (task);
    priv = get_private (self);
    ctx  = g_task_get_task_data (task);

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        priv->iface_enabled = FALSE;
        g_object_unref (task);
        return;
    }

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Interface state is assumed enabled from the very beginning of the enabling sequence,
         * so that updates are taken into account and not ignored. */
        priv->iface_enabled = TRUE;
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->setup_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->setup_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->setup_unsolicited_registration_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_registration_events (
                self,
                get_cs_network_supported (self),
                get_ps_network_supported (self),
                get_eps_network_supported (self),
                (GAsyncReadyCallback)enable_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_INITIAL_EPS_BEARER:
        if (get_eps_network_supported (self) &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer (
                self,
                (GAsyncReadyCallback)load_initial_eps_bearer_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

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
mm_iface_modem_3gpp_enable (MMIfaceModem3gpp    *self,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
    EnablingContext *ctx;
    GTask           *task;

    ctx = g_slice_new0 (EnablingContext);
    ctx->step = ENABLING_STEP_FIRST;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)enabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    if (!ctx->skeleton) {
        g_task_return_new_error (task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
                                 "Couldn't get interface skeleton");
        g_object_unref (task);
        return;
    }

    interface_enabling_step (task);
}

/*****************************************************************************/

#if defined WITH_SUSPEND_RESUME

typedef struct _SyncingContext SyncingContext;
static void interface_syncing_step (GTask *task);

typedef enum {
    SYNCING_STEP_FIRST,
    SYNCING_STEP_ENABLE_UNSOLICITED_EVENTS,
    SYNCING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS,
    SYNCING_STEP_REFRESH_3GPP_REGISTRATION,
    SYNCING_STEP_REFRESH_EPS_BEARER,
    SYNCING_STEP_LAST
} SyncingStep;

struct _SyncingContext {
    SyncingStep step;
};

gboolean
mm_iface_modem_3gpp_sync_finish (MMIfaceModem3gpp  *self,
                                 GAsyncResult      *res,
                                 GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

#undef VOID_SYNC_REPLY_READY_FN
#define VOID_SYNC_REPLY_READY_FN(NAME,DISPLAY)                          \
    static void                                                         \
    sync_##NAME##_ready (MMIfaceModem3gpp *self,                        \
                         GAsyncResult     *res,                         \
                         GTask            *task)                        \
    {                                                                   \
        DisablingContext  *ctx;                                         \
        g_autoptr(GError)  error = NULL;                                \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->NAME##_finish (self, res, &error); \
        if (error)                                                      \
            mm_obj_dbg (self, "couldn't %s: %s", DISPLAY, error->message);      \
                                                                        \
        /* Go on to next step */                                        \
        ctx = g_task_get_task_data (task);                              \
        ctx->step++;                                                    \
        interface_syncing_step (task);                                  \
    }

VOID_SYNC_REPLY_READY_FN (enable_unsolicited_events,
                     "enable unsolicited events")
VOID_SYNC_REPLY_READY_FN (enable_unsolicited_registration_events,
                     "enable unsolicited registration events")

static void
sync_eps_bearer_ready (MMIfaceModem3gpp *self,
                       GAsyncResult     *res,
                       GTask            *task)
{
    SyncingContext                *ctx;
    g_autoptr(MMBearerProperties)  properties  = NULL;
    g_autoptr(GError)              error = NULL;

    ctx = g_task_get_task_data (task);

    properties = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_finish (self, res, &error);
    if (!properties)
        mm_obj_dbg (self, "couldn't refresh initial EPS bearer status: %s", error->message);
    else
        mm_iface_modem_3gpp_update_initial_eps_bearer (self, properties);

    /* Go on to next step */
    ctx->step++;
    interface_syncing_step (task);
}

static void
sync_eps_bearer (MMIfaceModem3gpp    *self,
                 GAsyncReadyCallback  callback,
                 GTask               *task)
{
    SyncingContext *ctx;

    /* Refresh EPS bearer if supported */
    if (get_eps_network_supported (self) &&
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer &&
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_finish) {
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer (
            self,
            callback,
            task);
        return;
    }

    /* If EPS is unsupported, just go to the next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_syncing_step (task);
}

static void
sync_registration_ready (MMIfaceModem3gpp *self,
                         GAsyncResult     *res,
                         GTask            *task)
{
    SyncingContext     *ctx;
    g_autoptr (GError)  error = NULL;

    ctx = g_task_get_task_data (task);

    if (!mm_iface_modem_3gpp_run_registration_checks_finish (self, res, &error))
        mm_obj_dbg (self, "couldn't synchronize 3GPP registration: %s", error->message);

    /* Go on to next step */
    ctx->step++;
    interface_syncing_step(task);
}

static void
interface_syncing_step (GTask *task)
{
    MMIfaceModem3gpp *self;
    SyncingContext   *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case SYNCING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case SYNCING_STEP_ENABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)sync_enable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case SYNCING_STEP_ENABLE_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->enable_unsolicited_registration_events (
                self,
                get_cs_network_supported (self),
                get_ps_network_supported (self),
                get_eps_network_supported (self),
                (GAsyncReadyCallback)sync_enable_unsolicited_registration_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case SYNCING_STEP_REFRESH_3GPP_REGISTRATION:
        /*
         * Refresh registration info to verify that the modem is still registered.
         * Wait until registration checks are complete before going to the next step.
         */
        mm_iface_modem_3gpp_run_registration_checks (
            self,
            (GAsyncReadyCallback)sync_registration_ready,
            task);
        return;

    case SYNCING_STEP_REFRESH_EPS_BEARER:
        /*
         * Refresh EPS bearer and wait until complete.
         * We want to make sure that the modem is fully enabled again
         * when we refresh the mobile data connection bearers.
         */
        sync_eps_bearer (
            self,
            (GAsyncReadyCallback)sync_eps_bearer_ready,
            task);
        return;

    case SYNCING_STEP_LAST:
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
mm_iface_modem_3gpp_sync (MMIfaceModem3gpp    *self,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    SyncingContext *ctx;
    GTask          *task;

    /* Create SyncingContext */
    ctx = g_new0 (SyncingContext, 1);
    ctx->step = SYNCING_STEP_FIRST;

    /* Create sync steps task and execute it */
    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)g_free);
    interface_syncing_step (task);
}

typedef struct _TerseContext TerseContext;
static void interface_terse_step (GTask *task);

typedef enum {
    TERSE_STEP_FIRST,
    TERSE_STEP_DISABLE_UNSOLICITED_REGISTRATION_EVENTS,
    TERSE_STEP_DISABLE_UNSOLICITED_EVENTS,
    TERSE_STEP_LAST
} TerseStep;

struct _TerseContext {
    TerseStep step;
};

gboolean
mm_iface_modem_3gpp_terse_finish (MMIfaceModem3gpp  *self,
                                  GAsyncResult      *res,
                                  GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

#undef VOID_TERSE_REPLY_READY_FN
#define VOID_TERSE_REPLY_READY_FN(NAME,DISPLAY)                         \
    static void                                                         \
    terse_##NAME##_ready (MMIfaceModem3gpp *self,                       \
                  GAsyncResult     *res,                                \
                  GTask            *task)                               \
    {                                                                   \
        TerseContext  *ctx;                                             \
        g_autoptr(GError)  error = NULL;                                \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_IFACE (self)->NAME##_finish (self, res, &error); \
        if (error)                                                      \
            mm_obj_dbg (self, "couldn't %s: %s", DISPLAY, error->message);      \
                                                                        \
        /* Go on to next step */                                        \
        ctx = g_task_get_task_data (task);                              \
        ctx->step++;                                                    \
        interface_terse_step (task);                                    \
    }

VOID_TERSE_REPLY_READY_FN (disable_unsolicited_events,
                     "disable unsolicited events")
VOID_TERSE_REPLY_READY_FN (disable_unsolicited_registration_events,
                     "disable unsolicited registration events")

static void
interface_terse_step (GTask *task)
{
    MMIfaceModem3gpp *self;
    TerseContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case TERSE_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case TERSE_STEP_DISABLE_UNSOLICITED_REGISTRATION_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_registration_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_registration_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_registration_events (
                self,
                get_cs_network_supported (self),
                get_ps_network_supported (self),
                get_eps_network_supported (self),
                (GAsyncReadyCallback)terse_disable_unsolicited_registration_events_ready,
                task);
            mm_obj_msg (self, "terse state 3GPP (%d/%d): disable unsolicited registration events done",
                        ctx->step, TERSE_STEP_LAST);
            return;
        }
        ctx->step++;
        /* fall through */

    case TERSE_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)terse_disable_unsolicited_events_ready,
                task);
            mm_obj_msg (self, "terse state 3GPP (%d/%d): disable unsolicited events done",
                        ctx->step, TERSE_STEP_LAST);
            return;
        }
        ctx->step++;
        /* fall through */

    case TERSE_STEP_LAST:
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
mm_iface_modem_3gpp_terse (MMIfaceModem3gpp    *self,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    TerseContext *ctx;
    GTask          *task;

    /* Create SyncingContext */
    ctx = g_new0 (TerseContext, 1);
    ctx->step = TERSE_STEP_FIRST;

    /* Create sync steps task and execute it */
    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)g_free);
    interface_terse_step (task);
}

#endif

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (GTask *task);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_ENABLED_FACILITY_LOCKS,
    INITIALIZATION_STEP_IMEI,
    INITIALIZATION_STEP_TEST_LOCKED_OR_FAILED,
    INITIALIZATION_STEP_EPS_UE_MODE_OPERATION,
    INITIALIZATION_STEP_EPS_INITIAL_BEARER_SETTINGS,
    INITIALIZATION_STEP_NR5G_REGISTRATION_SETTINGS,
    INITIALIZATION_STEP_CONNECT_SIGNALS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModem3gpp   *skeleton;
    InitializationStep  step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_clear_object (&ctx->skeleton);
    g_slice_free (InitializationContext, ctx);
}

static void
sim_pin_lock_enabled_cb (MMBaseSim        *self,
                         gboolean          enabled,
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
load_nr5g_registration_settings_ready (MMIfaceModem3gpp *self,
                                       GAsyncResult     *res,
                                       GTask            *task)
{
    InitializationContext                 *ctx;
    g_autoptr(GError)                      error = NULL;
    g_autoptr(MMNr5gRegistrationSettings)  settings = NULL;

    ctx = g_task_get_task_data (task);

    settings = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_nr5g_registration_settings_finish (self, res, &error);
    if (!settings) {
        mm_obj_dbg (self, "couldn't load 5GNR registration settings: %s", error->message);
    } else {
        g_autoptr(GVariant) dictionary = NULL;

        dictionary = mm_nr5g_registration_settings_get_dictionary (settings);
        mm_gdbus_modem3gpp_set_nr5g_registration_settings (ctx->skeleton, dictionary);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_initial_eps_bearer_settings_ready (MMIfaceModem3gpp *self,
                                        GAsyncResult     *res,
                                        GTask            *task)
{
    InitializationContext         *ctx;
    g_autoptr(MMBearerProperties)  config = NULL;
    g_autoptr(GError)              error = NULL;

    ctx = g_task_get_task_data (task);

    config = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_settings_finish (self, res, &error);
    if (!config)
        mm_obj_dbg (self, "couldn't load initial EPS bearer settings: %s", error->message);
    else {
        g_autoptr(GVariant) dictionary = NULL;

        dictionary = mm_bearer_properties_get_dictionary (config);
        mm_gdbus_modem3gpp_set_initial_eps_bearer_settings (ctx->skeleton, dictionary);
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
    g_autoptr(GError)              error = NULL;

    ctx = g_task_get_task_data (task);

    uemode = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_eps_ue_mode_operation_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_eps_ue_mode_operation (ctx->skeleton, uemode);

    if (error)
        mm_obj_dbg (self, "couldn't load UE mode of operation for EPS: %s", error->message);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_enabled_facility_locks_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult     *res,
                                   GTask            *task)
{
    InitializationContext *ctx;
    MMModem3gppFacility    facilities;
    g_autoptr(GError)      error = NULL;

    ctx = g_task_get_task_data (task);

    facilities = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_enabled_facility_locks_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_enabled_facility_locks (ctx->skeleton, facilities);

    if (error)
        mm_obj_dbg (self, "couldn't load facility locks: %s", error->message);
    else {
        g_autoptr(MMBaseSim) sim = NULL;

        /* We loaded the initial list of facility locks; but we do need to update
         * the SIM PIN lock status when that changes. We'll connect to the signal
         * which notifies about such update. There is no need to ref self as the
         * SIM itself is an object which exists as long as self exists. */
        g_object_get (self, MM_IFACE_MODEM_SIM, &sim, NULL);

        if (sim)
            g_signal_connect (sim,
                              MM_BASE_SIM_PIN_LOCK_ENABLED,
                              G_CALLBACK (sim_pin_lock_enabled_cb),
                              ctx->skeleton);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
load_imei_ready (MMIfaceModem3gpp *self,
                 GAsyncResult     *res,
                 GTask            *task)
{
    InitializationContext *ctx;
    g_autoptr(GError)      error = NULL;
    g_autofree gchar      *imei = NULL;

    ctx = g_task_get_task_data (task);

    imei = MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_imei_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_imei (ctx->skeleton, imei);

    if (error)
        mm_obj_dbg (self, "couldn't load IMEI: %s", error->message);

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp      *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp      *self;
    GVariant              *data;
} HandleSetCarrierLockContext;

static void
handle_set_carrier_lock_context_free (HandleSetCarrierLockContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_variant_unref (ctx->data);
    g_free (ctx);
}

static void
handle_set_carrier_lock_ready (MMIfaceModem3gpp            *self,
                               GAsyncResult                *res,
                               HandleSetCarrierLockContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_carrier_lock_finish (self, res, &error))
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem3gpp_complete_set_carrier_lock (ctx->skeleton, ctx->invocation);
    handle_set_carrier_lock_context_free (ctx);
}

static void
handle_set_carrier_lock_auth_ready (MMIfaceAuth                 *auth,
                                    GAsyncResult                *res,
                                    HandleSetCarrierLockContext *ctx)
{
    MMIfaceModem3gpp *self = MM_IFACE_MODEM_3GPP (auth);
    GError           *error = NULL;
    const guint8     *data;
    gsize             data_size;

    if (!mm_iface_auth_authorize_finish (auth, res, &error)) {
        mm_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_set_carrier_lock_context_free (ctx);
        return;
    }

    /* If carrier lock is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_carrier_lock ||
        !MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_carrier_lock_finish) {
        mm_dbus_method_invocation_return_error_literal (ctx->invocation, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED,
                                                        "Cannot send set carrier lock request to modem: "
                                                        "operation not supported");
        handle_set_carrier_lock_context_free (ctx);
        return;
    }
    data = (const guint8 *) g_variant_get_fixed_array (ctx->data, &data_size, sizeof (guint8));

    MM_IFACE_MODEM_3GPP_GET_IFACE (self)->set_carrier_lock (
        ctx->self,
        data,
        data_size,
        (GAsyncReadyCallback)handle_set_carrier_lock_ready,
        ctx);
}

static gboolean
handle_set_carrier_lock (MmGdbusModem3gpp      *skeleton,
                         GDBusMethodInvocation *invocation,
                         GVariant              *data,
                         MMIfaceModem3gpp      *self)
{
    HandleSetCarrierLockContext *ctx;

    ctx = g_new0 (HandleSetCarrierLockContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->data = g_variant_ref (data);

    mm_iface_auth_authorize (MM_IFACE_AUTH (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_set_carrier_lock_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModem3gpp      *self;
    InitializationContext *ctx;
    MMModemState           modem_state;

    /* Don't run new steps if we're cancelled */
    if (g_task_return_error_if_cancelled (task)) {
        g_object_unref (task);
        return;
    }

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_ENABLED_FACILITY_LOCKS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_enabled_facility_locks &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_enabled_facility_locks_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_enabled_facility_locks (
                self,
                (GAsyncReadyCallback)load_enabled_facility_locks_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_IMEI:
        /* IMEI value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem3gpp_get_imei (ctx->skeleton) &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_imei &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_imei_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_imei (
                self,
                (GAsyncReadyCallback)load_imei_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_TEST_LOCKED_OR_FAILED:
        modem_state = MM_MODEM_STATE_UNKNOWN;
        g_object_get (self,
                      MM_IFACE_MODEM_STATE, &modem_state,
                      NULL);
        if (modem_state == MM_MODEM_STATE_LOCKED ||
            modem_state == MM_MODEM_STATE_FAILED) {
            /* Skip some steps and export the interface if modem is locked or failed */
            ctx->step = INITIALIZATION_STEP_LAST;
            interface_initialization_step (task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_EPS_UE_MODE_OPERATION:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_eps_ue_mode_operation &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_eps_ue_mode_operation_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_eps_ue_mode_operation (
                self,
                (GAsyncReadyCallback)load_eps_ue_mode_operation_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_EPS_INITIAL_BEARER_SETTINGS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_settings &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_settings_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_initial_eps_bearer_settings (
                self,
                (GAsyncReadyCallback)load_initial_eps_bearer_settings_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_NR5G_REGISTRATION_SETTINGS:
        if (MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_nr5g_registration_settings &&
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_nr5g_registration_settings_finish) {
            MM_IFACE_MODEM_3GPP_GET_IFACE (self)->load_nr5g_registration_settings (
                self,
                (GAsyncReadyCallback)load_nr5g_registration_settings_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_CONNECT_SIGNALS:
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
        g_signal_connect (ctx->skeleton,
                          "handle-set-packet-service-state",
                          G_CALLBACK (handle_set_packet_service_state),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-nr5g-registration-settings",
                          G_CALLBACK (handle_set_nr5g_registration_settings),
                          self);

        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* Always connect the signal to unlock modem */
        g_signal_connect (ctx->skeleton,
                          "handle-disable-facility-lock",
                          G_CALLBACK (handle_disable_facility_lock),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-carrier-lock",
                          G_CALLBACK (handle_set_carrier_lock),
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
mm_iface_modem_3gpp_initialize_finish (MMIfaceModem3gpp  *self,
                                       GAsyncResult      *res,
                                       GError           **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_3gpp_initialize (MMIfaceModem3gpp    *self,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    MmGdbusModem3gpp      *skeleton = NULL;
    InitializationContext *ctx;
    GTask                 *task;

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

        /* Bind our packet service state property */
        g_object_bind_property (self, MM_IFACE_MODEM_3GPP_PACKET_SERVICE_STATE,
                                skeleton, "packet-service-state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON, skeleton,
                      NULL);
    }

    ctx = g_slice_new0 (InitializationContext);
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

gchar *
mm_iface_modem_3gpp_get_manual_registration_operator_id (MMIfaceModem3gpp *self)
{
    Private *priv;

    priv = get_private (self);

    return g_strdup (priv->manual_registration_operator_id);
}

/*****************************************************************************/

static void
mm_iface_modem_3gpp_default_init (MMIfaceModem3gppInterface *iface)
{
    static gsize initialized = 0;

    if (!g_once_init_enter (&initialized))
        return;

    /* Properties */
    g_object_interface_install_property (
        iface,
        g_param_spec_object (MM_IFACE_MODEM_3GPP_DBUS_SKELETON,
                             "3GPP DBus skeleton",
                             "DBus skeleton for the 3GPP interface",
                             MM_GDBUS_TYPE_MODEM3GPP_SKELETON,
                             G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_enum (MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                           "RegistrationState",
                           "Registration state of the modem",
                           MM_TYPE_MODEM_3GPP_REGISTRATION_STATE,
                           MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,
                           G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_boolean (MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED,
                              "CS network supported",
                              "Whether the modem works in the CS network",
                              TRUE,
                              G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_boolean (MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED,
                              "PS network supported",
                              "Whether the modem works in the PS network",
                              TRUE,
                              G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_boolean (MM_IFACE_MODEM_3GPP_EPS_NETWORK_SUPPORTED,
                              "EPS network supported",
                              "Whether the modem works in the EPS network",
                              FALSE,
                              G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_boolean (MM_IFACE_MODEM_3GPP_5GS_NETWORK_SUPPORTED,
                              "5GS network supported",
                              "Whether the modem works in the 5GS network",
                              FALSE,
                              G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_flags (MM_IFACE_MODEM_3GPP_IGNORED_FACILITY_LOCKS,
                            "Ignored locks",
                            "Ignored facility locks",
                            MM_TYPE_MODEM_3GPP_FACILITY,
                            MM_MODEM_3GPP_FACILITY_NONE,
                            G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_object (MM_IFACE_MODEM_3GPP_INITIAL_EPS_BEARER,
                             "Initial EPS bearer",
                             "Initial EPS bearer setup during registration",
                             MM_TYPE_BASE_BEARER,
                             G_PARAM_READWRITE));

    g_object_interface_install_property (
        iface,
        g_param_spec_enum (MM_IFACE_MODEM_3GPP_PACKET_SERVICE_STATE,
                           "PacketServiceState",
                           "Packet service state of the modem",
                           MM_TYPE_MODEM_3GPP_PACKET_SERVICE_STATE,
                           MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN,
                           G_PARAM_READWRITE));

    g_once_init_leave (&initialized, 1);
}
