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
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "libqcdm/src/commands.h"

#include "mm-iface-modem.h"
#include "mm-iface-modem-cdma.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-log-object.h"

#define SUBSYSTEM_CDMA1X "cdma1x"
#define SUBSYSTEM_EVDO "evdo"

/*****************************************************************************/
/* Private data context */

#define PRIVATE_TAG "iface-modem-cdma-private-tag"
static GQuark private_quark;

typedef struct {
    gboolean activation_ongoing;
} Private;

static void
private_free (Private *priv)
{
    g_slice_free (Private, priv);
}

static Private *
get_private (MMIfaceModemCdma *self)
{
    Private *priv;

    if (G_UNLIKELY (!private_quark))
        private_quark = g_quark_from_static_string (PRIVATE_TAG);

    priv = g_object_get_qdata (G_OBJECT (self), private_quark);
    if (!priv) {
        priv = g_slice_new0 (Private);
        g_object_set_qdata_full (G_OBJECT (self), private_quark, priv, (GDestroyNotify)private_free);
    }

    return priv;
}

/*****************************************************************************/

void
mm_iface_modem_cdma_bind_simple_status (MMIfaceModemCdma *self,
                                        MMSimpleStatus *status)
{
    MmGdbusModemCdma *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    g_object_bind_property (skeleton, "cdma1x-registration-state",
                            status, MM_SIMPLE_PROPERTY_CDMA_CDMA1X_REGISTRATION_STATE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "sid",
                            status, MM_SIMPLE_PROPERTY_CDMA_SID,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "nid",
                            status, MM_SIMPLE_PROPERTY_CDMA_NID,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "evdo-registration-state",
                            status, MM_SIMPLE_PROPERTY_CDMA_EVDO_REGISTRATION_STATE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemCdma *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemCdma *self;
    gchar *carrier;
} HandleActivateContext;

static void
handle_activate_context_free (HandleActivateContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->carrier);
    g_free (ctx);
}

static void
handle_activate_ready (MMIfaceModemCdma *self,
                       GAsyncResult *res,
                       HandleActivateContext *ctx)
{
    Private *priv;
    GError  *error = NULL;

    priv = get_private (self);
    priv->activation_ongoing = FALSE;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_finish (self, res,&error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_cdma_complete_activate (ctx->skeleton, ctx->invocation);

    handle_activate_context_free (ctx);
}

static void
handle_activate_auth_ready (MMBaseModem *self,
                            GAsyncResult *res,
                            HandleActivateContext *ctx)
{
    Private      *priv;
    MMModemState  modem_state;
    GError       *error = NULL;

    priv = get_private (MM_IFACE_MODEM_CDMA (self));

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_activate_context_free (ctx);
        return;
    }

    /* Fail if we have already an activation ongoing */
    if (priv->activation_ongoing) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_IN_PROGRESS,
                                               "An activation operation is already in progress");
        handle_activate_context_free (ctx);
        return;
    }

    /* If we're already activated, nothing to do */
    if (mm_gdbus_modem_cdma_get_activation_state (ctx->skeleton) == MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED) {
        mm_obj_dbg (self, "already activated");
        mm_gdbus_modem_cdma_complete_activate (ctx->skeleton, ctx->invocation);
        handle_activate_context_free (ctx);
        return;
    }

    /* If activating OTA is not implemented, report an error */
    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate ||
        !MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot perform OTA activation: "
                                               "operation not supported");
        handle_activate_context_free (ctx);
        return;
    }

    /* Error if carrier code is empty */
    if (!ctx->carrier || !ctx->carrier[0]) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_INVALID_ARGS,
                                               "Cannot perform OTA activation: "
                                               "invalid empty carrier code");
        handle_activate_context_free (ctx);
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
                                               "Cannot perform OTA activation: "
                                               "device not fully initialized yet");
        handle_activate_context_free (ctx);
        return;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        priv->activation_ongoing = TRUE;
        MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate (
            MM_IFACE_MODEM_CDMA (self),
            ctx->carrier,
            (GAsyncReadyCallback)handle_activate_ready,
            ctx);
        return;

    case MM_MODEM_STATE_DISABLING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform OTA activation: "
                                               "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform OTA activation: "
                                               "not enabled yet");
        break;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform OTA activation: "
                                               "modem is connected");
        break;

    default:
        g_assert_not_reached ();
    }

    handle_activate_context_free (ctx);
}

static gboolean
handle_activate (MmGdbusModemCdma *skeleton,
                 GDBusMethodInvocation *invocation,
                 const gchar *carrier,
                 MMIfaceModemCdma *self)
{
    HandleActivateContext *ctx;

    ctx = g_new (HandleActivateContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->carrier = g_strdup (carrier);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_activate_auth_ready,
                             ctx);

    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModemCdma *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModemCdma *self;
    GVariant *dictionary;
} HandleActivateManualContext;

static void
handle_activate_manual_context_free (HandleActivateManualContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_variant_unref (ctx->dictionary);
    g_free (ctx);
}

static void
handle_activate_manual_ready (MMIfaceModemCdma *self,
                              GAsyncResult *res,
                              HandleActivateManualContext *ctx)
{
    Private *priv;
    GError  *error = NULL;

    priv = get_private (self);
    priv->activation_ongoing = FALSE;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_manual_finish (self, res,&error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_cdma_complete_activate_manual (ctx->skeleton, ctx->invocation);

    handle_activate_manual_context_free (ctx);
}

static void
handle_activate_manual_auth_ready (MMBaseModem *self,
                                   GAsyncResult *res,
                                   HandleActivateManualContext *ctx)
{
    MMCdmaManualActivationProperties *properties;
    Private                          *priv;
    MMModemState                      modem_state;
    GError                           *error = NULL;

    priv = get_private (MM_IFACE_MODEM_CDMA (self));

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_activate_manual_context_free (ctx);
        return;
    }

    /* Fail if we have already an activation ongoing */
    if (priv->activation_ongoing) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_IN_PROGRESS,
                                               "An activation operation is already in progress");
        handle_activate_manual_context_free (ctx);
        return;
    }

    /* If we're already activated, nothing to do */
    if (mm_gdbus_modem_cdma_get_activation_state (ctx->skeleton) == MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED) {
        mm_obj_dbg (self, "already activated");
        mm_gdbus_modem_cdma_complete_activate_manual (ctx->skeleton, ctx->invocation);
        handle_activate_manual_context_free (ctx);
        return;
    }

    /* If manual activation is not implemented, report an error */
    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_manual ||
        !MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_manual_finish) {
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot perform manual activation: "
                                               "operation not supported");
        handle_activate_manual_context_free (ctx);
        return;
    }

    /* Parse input properties */
    properties = mm_cdma_manual_activation_properties_new_from_dictionary (ctx->dictionary, &error);
    if (!properties) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_activate_manual_context_free (ctx);
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
                                               "Cannot perform manual activation: "
                                               "device not fully initialized yet");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        priv->activation_ongoing = TRUE;
        MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->activate_manual (
            MM_IFACE_MODEM_CDMA (self),
            properties,
            (GAsyncReadyCallback)handle_activate_manual_ready,
            ctx);
        g_object_unref (properties);
        return;

    case MM_MODEM_STATE_DISABLING:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform manual activation: "
                                               "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform manual activation: "
                                               "not enabled yet");
        break;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot perform manual activation: "
                                               "modem is connected");
        break;

    default:
        g_assert_not_reached ();
    }

    g_object_unref (properties);
    handle_activate_manual_context_free (ctx);
}

static gboolean
handle_activate_manual (MmGdbusModemCdma *skeleton,
                        GDBusMethodInvocation *invocation,
                        GVariant *dictionary,
                        MMIfaceModemCdma *self)
{
    HandleActivateManualContext *ctx;

    ctx = g_new (HandleActivateManualContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->dictionary = g_variant_ref (dictionary);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)handle_activate_manual_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/
/* Register in the CDMA network.
 * Note that the registration in the CDMA network is usually automatic; so this
 * method will really just try to ensure the modem is registered. If not
 * registered, it will wait until it is, up to N seconds.
 */

gboolean
mm_iface_modem_cdma_register_in_network_finish (MMIfaceModemCdma *self,
                                                GAsyncResult *res,
                                                GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
register_in_network_ready (MMIfaceModemCdma *self,
                           GAsyncResult *res,
                           GTask *task)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->register_in_network_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_iface_modem_cdma_register_in_network (MMIfaceModemCdma *self,
                                         guint max_registration_time,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GTask *task;

    task = g_task_new (self, NULL, callback, user_data);

    MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->register_in_network (
        self,
        max_registration_time,
        (GAsyncReadyCallback)register_in_network_ready,
        task);
}

/*****************************************************************************/

#define REGISTRATION_CHECK_TIMEOUT_SEC 30
#define REGISTRATION_CHECK_CONTEXT_TAG "cdma-registration-check-context-tag"

static GQuark registration_check_context_quark;

typedef struct _RunRegistrationChecksContext RunRegistrationChecksContext;
static void registration_check_step (GTask *task);

typedef enum {
    REGISTRATION_CHECK_STEP_FIRST,
    REGISTRATION_CHECK_STEP_SETUP_REGISTRATION_CHECKS,

    REGISTRATION_CHECK_STEP_QCDM_CALL_MANAGER_STATE,
    REGISTRATION_CHECK_STEP_QCDM_HDR_STATE,
    REGISTRATION_CHECK_STEP_QCDM_CDMA1X_SERVING_SYSTEM,
    REGISTRATION_CHECK_STEP_QCDM_LAST,

    REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS,
    REGISTRATION_CHECK_STEP_AT_CDMA1X_SERVING_SYSTEM,
    REGISTRATION_CHECK_STEP_AT_LAST,

    REGISTRATION_CHECK_STEP_DETAILED_REGISTRATION_STATE,
    REGISTRATION_CHECK_STEP_LAST,
} RegistrationCheckStep;

struct _RunRegistrationChecksContext {
    RegistrationCheckStep step;
    MMModemCdmaRegistrationState cdma1x_state;
    MMModemCdmaRegistrationState evdo_state;
    gboolean cdma1x_supported;
    gboolean evdo_supported;

    gboolean skip_qcdm_call_manager_step;
    gboolean skip_qcdm_hdr_step;
    gboolean skip_at_cdma_service_status_step;
    gboolean skip_at_cdma1x_serving_system_step;
    gboolean skip_detailed_registration_state;

    guint call_manager_system_mode;
    guint call_manager_operating_mode;

    guint8 hdr_session_state;
    guint8 hdr_almp_state;
    guint8 hdr_hybrid_mode;

    guint cdma1x_class;
    guint cdma1x_band;
    guint cdma1x_sid;
    guint cdma1x_nid;
};

gboolean
mm_iface_modem_cdma_run_registration_checks_finish (MMIfaceModemCdma *self,
                                                    GAsyncResult *res,
                                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_registration_checks_ready (MMIfaceModemCdma *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    RunRegistrationChecksContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_registration_checks_finish (
            self,
            res,
            &ctx->skip_qcdm_call_manager_step,
            &ctx->skip_qcdm_hdr_step,
            &ctx->skip_at_cdma_service_status_step,
            &ctx->skip_at_cdma1x_serving_system_step,
            &ctx->skip_detailed_registration_state,
            &error)) {
        /* Make it fatal */
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    registration_check_step (task);
}

static void
get_call_manager_state_ready (MMIfaceModemCdma *self,
                              GAsyncResult *res,
                              GTask *task)
{
    RunRegistrationChecksContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_call_manager_state_finish (
            self,
            res,
            &ctx->call_manager_system_mode,
            &ctx->call_manager_operating_mode,
            &error)) {
        mm_obj_dbg (self, "could not get call manager state: %s", error->message);
        g_error_free (error);
        /* Fallback to AT-based check */
        ctx->step = REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS;
        registration_check_step (task);
        return;
    }

    /* If no CDMA service, just finish checks */
    if (ctx->call_manager_operating_mode != QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_ONLINE) {
        ctx->step = REGISTRATION_CHECK_STEP_LAST;
        registration_check_step (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    registration_check_step (task);
}

static void
get_hdr_state_ready (MMIfaceModemCdma *self,
                     GAsyncResult *res,
                     GTask *task)
{
    RunRegistrationChecksContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_hdr_state_finish (
            self,
            res,
            &ctx->hdr_hybrid_mode,
            &ctx->hdr_session_state,
            &ctx->hdr_almp_state,
            &error)) {
        mm_obj_dbg (self, "could not get HDR state: %s", error->message);
        g_error_free (error);
        /* Fallback to AT-based check */
        ctx->step = REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS;
        registration_check_step (task);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    registration_check_step (task);
}

static void
parse_qcdm_results (GTask *task)
{
    MMIfaceModemCdma             *self;
    RunRegistrationChecksContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    mm_obj_dbg (self, "QCDM CM System Mode: %d", ctx->call_manager_system_mode);
    mm_obj_dbg (self, "QCDM HDR Hybrid Mode: %d", ctx->hdr_hybrid_mode);
    mm_obj_dbg (self, "QCDM HDR Session State: %d", ctx->hdr_session_state);
    mm_obj_dbg (self, "QCDM HDR ALMP State: %d", ctx->hdr_almp_state);

    /* Set QCDM-obtained registration info */
    switch (ctx->call_manager_system_mode) {
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_CDMA:
        ctx->cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        if (   ctx->hdr_hybrid_mode
            && ctx->hdr_session_state == QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_OPEN
            && (   ctx->hdr_almp_state == QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_IDLE
                || ctx->hdr_almp_state == QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_CONNECTED))
            ctx->evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_HDR:
        ctx->evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        break;
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_AMPS:
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_NO_SERVICE:
    case QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_WCDMA:
    default:
        break;
    }

    if (ctx->cdma1x_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN ||
        ctx->evdo_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN)
        /* Jump to get detailed registration state */
        ctx->step = REGISTRATION_CHECK_STEP_DETAILED_REGISTRATION_STATE;
    else
        /* If no CDMA service, just finish checks */
        ctx->step = REGISTRATION_CHECK_STEP_LAST;

    registration_check_step (task);
}

static void
get_service_status_ready (MMIfaceModemCdma *self,
                          GAsyncResult *res,
                          GTask *task)
{
    RunRegistrationChecksContext *ctx;
    GError *error = NULL;
    gboolean has_service = FALSE;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_service_status_finish (self,
                                                                              res,
                                                                              &has_service,
                                                                              &error)) {
        mm_obj_warn (self, "could not get service status: %s", error->message);
        g_task_return_error (task, error);
        g_object_unref (task);
        return;
    }

    if (!has_service) {
        /* There is no CDMA service at all, end registration checks */
        mm_obj_dbg (self, "no CDMA service found");
        ctx->step = REGISTRATION_CHECK_STEP_LAST;
    } else
        /* If we do have service, go on to next step */
        ctx->step++;

    registration_check_step (task);
}

static void
get_cdma1x_serving_system_ready (MMIfaceModemCdma *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    RunRegistrationChecksContext *ctx;
    GError *error = NULL;

    ctx = g_task_get_task_data (task);

    /* Note: used for *both* AT and QCDM serving system checks */

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system_finish (
            self,
            res,
            &ctx->cdma1x_class,
            &ctx->cdma1x_band,
            &ctx->cdma1x_sid,
            &ctx->cdma1x_nid,
            &error)) {
        /* Treat as fatal all errors except for no-network */
        if (!g_error_matches (error,
                              MM_MOBILE_EQUIPMENT_ERROR,
                              MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK)) {
            mm_obj_warn (self, "could not get serving system: %s", error->message);
            g_task_return_error (task, error);
            g_object_unref (task);
            return;
        }

        ctx->cdma1x_sid = MM_MODEM_CDMA_SID_UNKNOWN;
        ctx->cdma1x_nid = MM_MODEM_CDMA_NID_UNKNOWN;
    }

    /* TODO: not sure why we also take class/band here */

    /* Go on to next step */
    ctx->step++;
    registration_check_step (task);
}

static void
parse_at_results (GTask *task)
{
    MMIfaceModemCdma             *self;
    RunRegistrationChecksContext *ctx;

    self = g_task_get_source_object (task);
    ctx  = g_task_get_task_data (task);

    /* 99999 means unknown/no service */
    if (ctx->cdma1x_sid == MM_MODEM_CDMA_SID_UNKNOWN &&
        ctx->cdma1x_nid == MM_MODEM_CDMA_NID_UNKNOWN) {
        /* Not registered in CDMA network, end registration checks */
        mm_obj_dbg (self, "no registered in any CDMA network");
        ctx->step = REGISTRATION_CHECK_STEP_LAST;
    } else {
        /* We're registered on the CDMA 1x network (at least) */
        ctx->cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED;
        /* Jump to get detailed registration state */
        ctx->step = REGISTRATION_CHECK_STEP_DETAILED_REGISTRATION_STATE;
    }

    registration_check_step (task);
}

static void
get_detailed_registration_state_ready (MMIfaceModemCdma *self,
                                       GAsyncResult *res,
                                       GTask *task)
{
    RunRegistrationChecksContext *ctx;
    GError *error = NULL;
    MMModemCdmaRegistrationState detailed_cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    MMModemCdmaRegistrationState detailed_evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;

    ctx = g_task_get_task_data (task);

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_detailed_registration_state_finish (
            self,
            res,
            &detailed_cdma1x_state,
            &detailed_evdo_state,
            &error)) {
        /* This error is NOT fatal. If we get an error here, we'll just fallback
         * to the non-detailed values we already got. */
        mm_obj_dbg (self, "could not get more detailed registration state: %s", error->message);
    } else {
        ctx->cdma1x_state = detailed_cdma1x_state;
        ctx->evdo_state = detailed_evdo_state;
    }

    /* Go on to next step */
    ctx->step++;
    registration_check_step (task);
}

static void
registration_check_step (GTask *task)
{
    MMIfaceModemCdma *self;
    RunRegistrationChecksContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case REGISTRATION_CHECK_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case REGISTRATION_CHECK_STEP_SETUP_REGISTRATION_CHECKS:
        /* Allow implementations to run an initial setup check. This setup allows
         * to specify which of the next steps will be completely skipped. Useful
         * when implementations have a best get_detailed_registration_state()
         * so that they just need that to be run. */
        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_registration_checks &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_registration_checks_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_registration_checks (
                self,
                (GAsyncReadyCallback)setup_registration_checks_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case REGISTRATION_CHECK_STEP_QCDM_CALL_MANAGER_STATE:
        mm_obj_dbg (self, "starting QCDM-based registration checks...");
        if (!ctx->skip_qcdm_call_manager_step &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_call_manager_state &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_call_manager_state_finish) {
            /* Start by trying to get the call manager state. */
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_call_manager_state (
                self,
                (GAsyncReadyCallback)get_call_manager_state_ready,
                task);
            return;
        }
        /* Fallback to AT-based check */
        mm_obj_dbg (self, "  skipping all QCDM-based checks and falling back to AT-based checks");
        ctx->step = REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS;
        registration_check_step (task);
        return;

    case REGISTRATION_CHECK_STEP_QCDM_HDR_STATE:
        if (ctx->evdo_supported &&
            !ctx->skip_qcdm_hdr_step &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_hdr_state &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_hdr_state_finish) {
            /* Get HDR (EVDO) state. */
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_hdr_state (
                self,
                (GAsyncReadyCallback)get_hdr_state_ready,
                task);
            return;
        }
        mm_obj_dbg (self, "  skipping HDR check");
        ctx->step++;
        /* fall through */

    case REGISTRATION_CHECK_STEP_QCDM_CDMA1X_SERVING_SYSTEM:
        /* We only care about SID/NID here; nothing to do with registration
         * state.
         */
        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system (
                self,
                (GAsyncReadyCallback)get_cdma1x_serving_system_ready,
                task);
            return;
        }
        mm_obj_dbg (self, "  skipping CDMA1x serving system check");
        ctx->step++;
        /* fall through */

    case REGISTRATION_CHECK_STEP_QCDM_LAST:
        /* When we get all QCDM results, parse them */
        parse_qcdm_results (task);
        return;

    case REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS:
        mm_obj_dbg (self, "starting AT-based registration checks");
        /* If we don't have means to get service status, just assume we do have
         * CDMA service and keep on */
        if (!ctx->skip_at_cdma_service_status_step &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_service_status &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_service_status_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_service_status (
                self,
                (GAsyncReadyCallback)get_service_status_ready,
                task);
            return;
        }
        mm_obj_dbg (self, "  skipping CDMA service status check, assuming with service");
        ctx->step++;
        /* fall through */

    case REGISTRATION_CHECK_STEP_AT_CDMA1X_SERVING_SYSTEM:
        /* Now that we have some sort of service, check if the the device is
         * registered on the network.
         */

        /* Some devices key the AT+CSS? response off the 1X state, but if the
         * device has EVDO service but no 1X service, then reading AT+CSS? will
         * error out too early.  Let subclasses that know that their AT+CSS?
         * response is wrong in this case handle more specific registration
         * themselves; if they do, they'll set these callbacks to NULL..
         */
        if (!ctx->skip_at_cdma1x_serving_system_step &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system (
                self,
                (GAsyncReadyCallback)get_cdma1x_serving_system_ready,
                task);
            return;
        }
        mm_obj_dbg (self, "  skipping CDMA1x Serving System check");
        ctx->step++;
        /* fall through */

    case REGISTRATION_CHECK_STEP_AT_LAST:
        /* When we get all AT results, parse them */
        parse_at_results (task);
        return;

    case REGISTRATION_CHECK_STEP_DETAILED_REGISTRATION_STATE:
        mm_obj_dbg (self, "starting detailed registration state check");
        /* We let classes implementing this interface to look for more detailed
         * registration info. */
        if (!ctx->skip_detailed_registration_state &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_detailed_registration_state &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_detailed_registration_state_finish) {
            /* We pass the CDMA1x/EVDO registration states we got up to now.
             * If the implementation can't improve the detail, it must either
             * return the values it already got as input, or issue an error,
             * and we'll assume it couldn't get any better value. */
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_detailed_registration_state (
                self,
                ctx->cdma1x_state,
                ctx->evdo_state,
                (GAsyncReadyCallback)get_detailed_registration_state_ready,
                task);
            return;
        }
        mm_obj_dbg (self, "  skipping detailed registration state check");
        ctx->step++;
        /* fall through */

    case REGISTRATION_CHECK_STEP_LAST:
        /* We are done without errors! */
        mm_obj_dbg (self, "all CDMA registration state checks done");
        mm_iface_modem_cdma_update_cdma1x_registration_state (self,
                                                              ctx->cdma1x_state,
                                                              ctx->cdma1x_sid,
                                                              ctx->cdma1x_nid);
        mm_iface_modem_cdma_update_evdo_registration_state (self,
                                                            ctx->evdo_state);

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

static void
custom_run_registration_checks_ready (MMIfaceModemCdma *self,
                                      GAsyncResult *res,
                                      GTask *task)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->run_registration_checks_finish (self, res, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
    g_object_unref (task);
}

void
mm_iface_modem_cdma_run_registration_checks (MMIfaceModemCdma *self,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    GTask *task;
    gboolean cdma1x_supported;
    gboolean evdo_supported;

    task = g_task_new (self, NULL, callback, user_data);

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED,   &evdo_supported,
                  MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED, &cdma1x_supported,
                  NULL);

    mm_obj_dbg (self, "running registration checks (CDMA1x: '%s', EV-DO: '%s')",
                cdma1x_supported ? "yes" : "no",
                evdo_supported ? "yes" : "no");

    if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->run_registration_checks &&
        MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->run_registration_checks_finish) {
        /* Plugins implementing full custom registration checks shouldn't implement
         * sub-steps of the generic registration check sequence */
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_registration_checks != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_registration_checks_finish != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_call_manager_state != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_call_manager_state_finish != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_hdr_state != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_hdr_state_finish != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_service_status != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_service_status_finish != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system_finish != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_detailed_registration_state != NULL);
        g_warn_if_fail (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_detailed_registration_state_finish != NULL);

        MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->run_registration_checks (
            self,
            cdma1x_supported,
            evdo_supported,
            (GAsyncReadyCallback)custom_run_registration_checks_ready,
            task);
    } else {
        RunRegistrationChecksContext *ctx;

        ctx = g_new0 (RunRegistrationChecksContext, 1);
        ctx->cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        ctx->evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
        ctx->call_manager_system_mode = QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_NO_SERVICE;
        ctx->hdr_session_state = QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSED;
        ctx->hdr_almp_state = QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INACTIVE;
        ctx->hdr_hybrid_mode = 0;
        ctx->evdo_supported = evdo_supported;
        ctx->cdma1x_supported = cdma1x_supported;

        g_task_set_task_data (task, ctx, g_free);

        registration_check_step (task);
    }
}

/*****************************************************************************/

void
mm_iface_modem_cdma_update_access_technologies (MMIfaceModemCdma *self,
                                                MMModemAccessTechnology access_tech)
{
    MMModemCdmaRegistrationState cdma1x_state;
    MMModemCdmaRegistrationState evdo_state;

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE, &cdma1x_state,
                  MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE, &evdo_state,
                  NULL);

    /* Even if registration state didn't change, report access technology,
     * but only if something valid to report */
    if (cdma1x_state == MM_MODEM_CDMA_REGISTRATION_STATE_HOME ||
        cdma1x_state == MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING ||
        cdma1x_state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED ||
        evdo_state == MM_MODEM_CDMA_REGISTRATION_STATE_HOME ||
        evdo_state == MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING ||
        evdo_state == MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED) {
        if (access_tech != MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN)
            mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                       access_tech,
                                                       MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK);
    } else
        mm_iface_modem_update_access_technologies (MM_IFACE_MODEM (self),
                                                   MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN,
                                                   MM_IFACE_MODEM_CDMA_ALL_ACCESS_TECHNOLOGIES_MASK);
}

void
mm_iface_modem_cdma_update_evdo_registration_state (MMIfaceModemCdma *self,
                                                    MMModemCdmaRegistrationState state)
{
    MmGdbusModemCdma *skeleton = NULL;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED, &supported,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (supported) {
        /* The property in the interface is bound to the property
         * in the skeleton, so just updating here is enough */
        g_object_set (self,
                      MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE, state,
                      NULL);

        switch (state) {
        case MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED:
        case MM_MODEM_CDMA_REGISTRATION_STATE_HOME:
        case MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING:
            mm_iface_modem_update_subsystem_state (MM_IFACE_MODEM (self),
                                                   SUBSYSTEM_EVDO,
                                                   MM_MODEM_STATE_REGISTERED,
                                                   MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
            break;
        case MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN:
            mm_iface_modem_update_subsystem_state (MM_IFACE_MODEM (self),
                                                   SUBSYSTEM_EVDO,
                                                   MM_MODEM_STATE_ENABLED,
                                                   MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
            break;
        default:
            g_assert_not_reached ();
            break;
        }
    }

    g_object_unref (skeleton);
}

void
mm_iface_modem_cdma_update_cdma1x_registration_state (MMIfaceModemCdma *self,
                                                      MMModemCdmaRegistrationState state,
                                                      guint sid,
                                                      guint nid)
{
    MmGdbusModemCdma *skeleton = NULL;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED, &supported,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (supported) {
        /* The property in the interface is bound to the property
         * in the skeleton, so just updating here is enough */
        g_object_set (self,
                      MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE, state,
                      NULL);

        switch (state) {
        case MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED:
        case MM_MODEM_CDMA_REGISTRATION_STATE_HOME:
        case MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING:
            mm_gdbus_modem_cdma_set_sid (skeleton, sid);
            mm_gdbus_modem_cdma_set_nid (skeleton, nid);
            mm_iface_modem_update_subsystem_state (MM_IFACE_MODEM (self),
                                                   SUBSYSTEM_CDMA1X,
                                                   MM_MODEM_STATE_REGISTERED,
                                                   MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
            break;
        case MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN:
            if (mm_gdbus_modem_cdma_get_sid (skeleton) != MM_MODEM_CDMA_SID_UNKNOWN)
                mm_gdbus_modem_cdma_set_sid (skeleton, MM_MODEM_CDMA_SID_UNKNOWN);
            if (mm_gdbus_modem_cdma_get_nid (skeleton) != MM_MODEM_CDMA_NID_UNKNOWN)
                mm_gdbus_modem_cdma_set_nid (skeleton, MM_MODEM_CDMA_NID_UNKNOWN);

            mm_iface_modem_update_subsystem_state (MM_IFACE_MODEM (self),
                                                   SUBSYSTEM_CDMA1X,
                                                   MM_MODEM_STATE_ENABLED,
                                                   MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
            break;
        default:
            g_assert_not_reached ();
            break;
        }
    }

    g_object_unref (skeleton);
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
periodic_registration_checks_ready (MMIfaceModemCdma *self,
                                    GAsyncResult *res)
{
    RegistrationCheckContext *ctx;
    GError *error = NULL;

    mm_iface_modem_cdma_run_registration_checks_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "couldn't refresh CDMA registration status: %s", error->message);
        g_error_free (error);
    }

    /* Remove the running tag */
    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);
    if (ctx)
        ctx->running = FALSE;
}

static gboolean
periodic_registration_check (MMIfaceModemCdma *self)
{
    RegistrationCheckContext *ctx;

    /* Only launch a new one if not one running already */
    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);
    if (!ctx->running) {
        ctx->running = TRUE;
        mm_iface_modem_cdma_run_registration_checks (
            self,
            (GAsyncReadyCallback)periodic_registration_checks_ready,
            NULL);
    }
    return G_SOURCE_CONTINUE;
}

static void
periodic_registration_check_disable (MMIfaceModemCdma *self)
{
    if (G_UNLIKELY (!registration_check_context_quark))
        registration_check_context_quark = (g_quark_from_static_string (
                                                REGISTRATION_CHECK_CONTEXT_TAG));

    /* Overwriting the data will free the previous context */
    g_object_set_qdata (G_OBJECT (self),
                        registration_check_context_quark,
                        NULL);

    mm_obj_dbg (self, "periodic CDMA registration checks disabled");
}

static void
periodic_registration_check_enable (MMIfaceModemCdma *self)
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
    mm_obj_dbg (self, "periodic CDMA registration checks enabled");
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

static GVariant *
build_empty_dictionary (void)
{
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
    return g_variant_builder_end (&builder);
}

void
mm_iface_modem_cdma_update_activation_state (MMIfaceModemCdma *self,
                                             MMModemCdmaActivationState activation_state,
                                             const GError *activation_error)
{
    MmGdbusModemCdma *skeleton = NULL;
    MMCdmaActivationError error = MM_CDMA_ACTIVATION_ERROR_NONE;

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return;

    if (activation_error) {
        mm_obj_dbg (self, "activation failed: %s", activation_error->message);
        if (activation_error->domain == MM_CDMA_ACTIVATION_ERROR)
            error = activation_error->code;
        else {
            mm_obj_warn (self, "error given is not an activation error");
            error = MM_CDMA_ACTIVATION_ERROR_UNKNOWN;
        }
    }

    /* Flush current change before signaling the state change,
     * so that clients get the proper state already in the
     * state-changed callback */
    mm_gdbus_modem_cdma_set_activation_state (skeleton, activation_state);
    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (skeleton));
    /* We don't know what changed, so just return an empty dictionary for now */
    mm_gdbus_modem_cdma_emit_activation_state_changed (skeleton,
                                                       activation_state,
                                                       error,
                                                       build_empty_dictionary ());

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (GTask *task);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    DisablingStep step;
    MmGdbusModemCdma *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_cdma_disable_finish (MMIfaceModemCdma *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModemCdma *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->disable_unsolicited_events_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "couldn't disable unsolicited events: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
cleanup_unsolicited_events_ready (MMIfaceModemCdma *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
    if (error) {
        mm_obj_dbg (self, "couldn't cleanup unsolicited events: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
interface_disabling_step (GTask *task)
{
    MMIfaceModemCdma *self;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS:
        periodic_registration_check_disable (self);
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->disable_unsolicited_events &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->disable_unsolicited_events_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->disable_unsolicited_events (
                self,
                (GAsyncReadyCallback)disable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->cleanup_unsolicited_events &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->cleanup_unsolicited_events_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->cleanup_unsolicited_events (
                self,
                (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_LAST:
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
mm_iface_modem_cdma_disable (MMIfaceModemCdma *self,
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
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &ctx->skeleton,
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
    ENABLING_STEP_PERIODIC_REGISTRATION_CHECKS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModemCdma *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_cdma_enable_finish (MMIfaceModemCdma *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModemCdma *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "setting up unsolicited events failed: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
enable_unsolicited_events_ready (MMIfaceModemCdma *self,
                                GAsyncResult *res,
                                 GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error);
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
interface_enabling_step (GTask *task)
{
    MMIfaceModemCdma *self;
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
        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_unsolicited_events &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_unsolicited_events_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->setup_unsolicited_events (
                self,
                (GAsyncReadyCallback)setup_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->enable_unsolicited_events &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->enable_unsolicited_events_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->enable_unsolicited_events (
                self,
                (GAsyncReadyCallback)enable_unsolicited_events_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_PERIODIC_REGISTRATION_CHECKS:
        periodic_registration_check_enable (self);
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
mm_iface_modem_cdma_enable (MMIfaceModemCdma *self,
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
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &ctx->skeleton,
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
    INITIALIZATION_STEP_MEID,
    INITIALIZATION_STEP_ESN,
    INITIALIZATION_STEP_ACTIVATION_STATE,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModemCdma *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY)                                \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModemCdma *self,                        \
                         GAsyncResult *res,                             \
                         GTask *task)                                   \
    {                                                                   \
        InitializationContext *ctx;                                     \
        GError *error = NULL;                                           \
        gchar *val;                                                     \
                                                                        \
        ctx = g_task_get_task_data (task);                              \
                                                                        \
        val = MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_modem_cdma_set_##NAME (ctx->skeleton, val);            \
        g_free (val);                                                   \
                                                                        \
        if (error) {                                                    \
            mm_obj_warn (self, "couldn't load %s: %s", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (task);                           \
    }

STR_REPLY_READY_FN (meid, "MEID")
STR_REPLY_READY_FN (esn, "ESN")

static void
load_activation_state_ready (MMIfaceModemCdma *self,
                             GAsyncResult *res,
                             GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;
    MMModemCdmaActivationState state;

    ctx = g_task_get_task_data (task);

    state = MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_activation_state_finish (self, res, &error);
    mm_gdbus_modem_cdma_set_activation_state (ctx->skeleton, state);

    if (error) {
        mm_obj_warn (self, "couldn't load activation state: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModemCdma *self;
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

    case INITIALIZATION_STEP_MEID:
        /* MEID value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem_cdma_get_meid (ctx->skeleton) &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_meid &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_meid_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_meid (
                self,
                (GAsyncReadyCallback)load_meid_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_ESN:
        /* ESN value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem_cdma_get_esn (ctx->skeleton) &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_esn &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_esn_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_esn (
                self,
                (GAsyncReadyCallback)load_esn_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_ACTIVATION_STATE:
        /* Initial activation state is meant to be loaded only once during the
         * whole lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (mm_gdbus_modem_cdma_get_activation_state (ctx->skeleton) == MM_MODEM_CDMA_ACTIVATION_STATE_UNKNOWN &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_activation_state &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_activation_state_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_activation_state (
                self,
                (GAsyncReadyCallback)load_activation_state_ready,
                task);
            return;
        }
        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-activate",
                          G_CALLBACK (handle_activate),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-activate-manual",
                          G_CALLBACK (handle_activate_manual),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_cdma (MM_GDBUS_OBJECT_SKELETON (self),
                                                 MM_GDBUS_MODEM_CDMA (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_cdma_initialize_finish (MMIfaceModemCdma *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_cdma_initialize (MMIfaceModemCdma *self,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemCdma *skeleton = NULL;
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_cdma_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem_cdma_set_meid (skeleton, NULL);
        mm_gdbus_modem_cdma_set_esn (skeleton, NULL);
        mm_gdbus_modem_cdma_set_sid (skeleton, MM_MODEM_CDMA_SID_UNKNOWN);
        mm_gdbus_modem_cdma_set_nid (skeleton, MM_MODEM_CDMA_NID_UNKNOWN);
        mm_gdbus_modem_cdma_set_activation_state (skeleton, MM_MODEM_CDMA_ACTIVATION_STATE_UNKNOWN);

        /* Bind our Registration State properties */
        g_object_bind_property (self, MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE,
                                skeleton, "cdma1x-registration-state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        g_object_bind_property (self, MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE,
                                skeleton, "evdo-registration-state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_CDMA_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */

    ctx = g_new0 (InitializationContext, 1);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    interface_initialization_step (task);
}

void
mm_iface_modem_cdma_shutdown (MMIfaceModemCdma *self)
{
    /* Remove RegistrationCheckContext object to make sure any pending
     * invocation of periodic_registration_check is cancelled before the
     * DBus skeleton is removed. */
    if (G_LIKELY (registration_check_context_quark))
        g_object_set_qdata (G_OBJECT (self),
                            registration_check_context_quark,
                            NULL);

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

    g_object_interface_install_property
        (g_iface,
         g_param_spec_enum (MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE,
                            "CDMA1x Registration State",
                            "Registration state of the modem in the CDMA1x network",
                            MM_TYPE_MODEM_CDMA_REGISTRATION_STATE,
                            MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
                            G_PARAM_READWRITE));


    g_object_interface_install_property
        (g_iface,
         g_param_spec_enum (MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE,
                            "EV-DO Registration State",
                            "Registration state of the modem in the EV-DO network",
                            MM_TYPE_MODEM_CDMA_REGISTRATION_STATE,
                            MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
                            G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED,
                               "CDMA1x network supported",
                               "Whether the modem works in the CDMA1x network",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED,
                               "EV-DO network supported",
                               "Whether the modem works in the EV-DO network",
                               TRUE,
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
