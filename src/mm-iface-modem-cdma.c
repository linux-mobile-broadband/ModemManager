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

#include "libqcdm/src/commands.h"

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

typedef struct _RunAllRegistrationChecksContext RunAllRegistrationChecksContext;
static void registration_check_step (RunAllRegistrationChecksContext *ctx);

typedef enum {
    REGISTRATION_CHECK_STEP_FIRST,

    REGISTRATION_CHECK_STEP_QCDM_CALL_MANAGER_STATE,
    REGISTRATION_CHECK_STEP_QCDM_HDR_STATE,
    REGISTRATION_CHECK_STEP_QCDM_LAST,

    REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS,
    REGISTRATION_CHECK_STEP_AT_CDMA1X_SERVING_SYSTEM,
    REGISTRATION_CHECK_STEP_AT_LAST,

    REGISTRATION_CHECK_STEP_SPECIFIC_STATE,
    REGISTRATION_CHECK_STEP_LAST,
} RegistrationCheckStep;

struct _RunAllRegistrationChecksContext {
    MMIfaceModemCdma *self;
    GSimpleAsyncResult *result;
    RegistrationCheckStep step;
    MMModemCdmaRegistrationState cdma1x_state;
    MMModemCdmaRegistrationState evdo_state;
    gboolean cdma1x_supported;
    gboolean evdo_supported;

    guint call_manager_system_mode;
    guint call_manager_operating_mode;

    guint8 hdr_session_state;
    guint8 hdr_almp_state;
    guint8 hdr_hybrid_mode;

    guint cdma1x_class;
    guint cdma1x_band;
    guint cdma1x_sid;
};

static void
run_all_registration_checks_context_complete_and_free (RunAllRegistrationChecksContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

gboolean
mm_iface_modem_cdma_run_all_registration_checks_finish (MMIfaceModemCdma *self,
                                                        GAsyncResult *res,
                                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
get_call_manager_state_ready (MMIfaceModemCdma *self,
                              GAsyncResult *res,
                              RunAllRegistrationChecksContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_call_manager_state_finish (
            self,
            res,
            &ctx->call_manager_system_mode,
            &ctx->call_manager_operating_mode,
            &error)) {
        mm_dbg ("Could not get call manager state: %s", error->message);
        g_error_free (error);
        /* Fallback to AT-based check */
        ctx->step = REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS;
        registration_check_step (ctx);
        return;
    }

    /* If no CDMA service, just finish checks */
    if (ctx->call_manager_operating_mode != QCDM_CMD_CM_SUBSYS_STATE_INFO_OPERATING_MODE_ONLINE) {
        ctx->step = REGISTRATION_CHECK_STEP_LAST;
        registration_check_step (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    registration_check_step (ctx);
}

static void
get_hdr_state_ready (MMIfaceModemCdma *self,
                     GAsyncResult *res,
                     RunAllRegistrationChecksContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_hdr_state_finish (
            self,
            res,
            &ctx->hdr_session_state,
            &ctx->hdr_almp_state,
            &ctx->hdr_hybrid_mode,
            &error)) {
        mm_dbg ("Could not get HDR state: %s", error->message);
        g_error_free (error);
        /* Fallback to AT-based check */
        ctx->step = REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS;
        registration_check_step (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    registration_check_step (ctx);
}

static void
parse_qcdm_results (RunAllRegistrationChecksContext *ctx)
{
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
        /* Jump to get specific registration state */
        ctx->step = REGISTRATION_CHECK_STEP_SPECIFIC_STATE;
    else
        /* If no CDMA service, just finish checks */
        ctx->step = REGISTRATION_CHECK_STEP_LAST;

    registration_check_step (ctx);
}

static void
get_service_status_ready (MMIfaceModemCdma *self,
                          GAsyncResult *res,
                          RunAllRegistrationChecksContext *ctx)
{
    GError *error = NULL;
    gboolean has_service = FALSE;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_service_status_finish (self,
                                                                              res,
                                                                              &has_service,
                                                                              &error)) {
        mm_warn ("Could not get service status: %s", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        run_all_registration_checks_context_complete_and_free (ctx);
        return;
    }

    if (!has_service)
        /* There is no CDMA service at all, end registration checks */
        /* If no CDMA service, just finish checks */
        ctx->step = REGISTRATION_CHECK_STEP_LAST;
    else
        /* If we do have service, go on to next step */
        ctx->step++;

    registration_check_step (ctx);
}

static void
get_cdma1x_serving_system_ready (MMIfaceModemCdma *self,
                                 GAsyncResult *res,
                                 RunAllRegistrationChecksContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->get_cdma1x_serving_system_finish (
            self,
            res,
            &ctx->cdma1x_class,
            &ctx->cdma1x_band,
            &ctx->cdma1x_sid,
            &error)) {
        mm_warn ("Could not get serving system: %s", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        run_all_registration_checks_context_complete_and_free (ctx);
        return;
    }

    /* 99999 means unknown/no service */
    if (ctx->cdma1x_sid == 99999)
        /* There is no CDMA service, end registration checks */
        ctx->step = REGISTRATION_CHECK_STEP_LAST;
    else
        /* Go on to next step */
        ctx->step++;

    registration_check_step (ctx);
}

static void
registration_check_step (RunAllRegistrationChecksContext *ctx)
{
    switch (ctx->step) {
    case REGISTRATION_CHECK_STEP_FIRST:
        /* If QCDM-based checks available start with those first. Otherwise start
         * with AT-based checks directly. */
        if (!MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_call_manager_state ||
            !MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_call_manager_state_finish ||
            !MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_hdr_state ||
            !MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_hdr_state_finish) {
            /* Fallback to AT-based check */
            ctx->step = REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS;
            registration_check_step (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case REGISTRATION_CHECK_STEP_QCDM_CALL_MANAGER_STATE:
        /* Start by trying to get the call manager state. */
        MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_call_manager_state (
            ctx->self,
            (GAsyncReadyCallback)get_call_manager_state_ready,
            ctx);
            return;

    case REGISTRATION_CHECK_STEP_QCDM_HDR_STATE:
        /* Get HDR state. */
        MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_hdr_state (
            ctx->self,
            (GAsyncReadyCallback)get_hdr_state_ready,
            ctx);
        return;

    case REGISTRATION_CHECK_STEP_QCDM_LAST:
        /* When we get all QCDM results, parse them */
        parse_qcdm_results (ctx);
        return;

    case REGISTRATION_CHECK_STEP_AT_CDMA_SERVICE_STATUS:
        /* If we don't have means to get service status, just assume we do have
         * CDMA service and keep on */
        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_service_status &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_service_status_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_service_status (
                ctx->self,
                (GAsyncReadyCallback)get_service_status_ready,
                ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case REGISTRATION_CHECK_STEP_AT_CDMA1X_SERVING_SYSTEM:
        /* Plugins may fully disable querying the serving system, for example if
         * they already know that AT+CSS? will return undesired errors. */
        if (MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_cdma1x_serving_system &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_cdma1x_serving_system_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->get_cdma1x_serving_system (
                ctx->self,
                (GAsyncReadyCallback)get_cdma1x_serving_system_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case REGISTRATION_CHECK_STEP_AT_LAST:
        /* Fall down to next step */
        ctx->step++;

    case REGISTRATION_CHECK_STEP_SPECIFIC_STATE:
        /* Fall down to next step */
        ctx->step++;

    case REGISTRATION_CHECK_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        run_all_registration_checks_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_cdma_run_all_registration_checks (MMIfaceModemCdma *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data)
{
    RunAllRegistrationChecksContext *ctx;

    ctx = g_new0 (RunAllRegistrationChecksContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_cdma_run_all_registration_checks);
    ctx->cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    ctx->evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    ctx->call_manager_system_mode = QCDM_CMD_CM_SUBSYS_STATE_INFO_SYSTEM_MODE_NO_SERVICE;
    ctx->hdr_session_state = QCDM_CMD_HDR_SUBSYS_STATE_INFO_SESSION_STATE_CLOSED;
    ctx->hdr_almp_state = QCDM_CMD_HDR_SUBSYS_STATE_INFO_ALMP_STATE_INACTIVE;
    ctx->hdr_hybrid_mode = 0;

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_EVDO_NETWORK_SUPPORTED, &ctx->evdo_supported,
                  MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED, &ctx->cdma1x_supported,
                  NULL);

    mm_dbg ("Running registration checks (CDMA1x: '%s', EVDO: '%s')",
            ctx->cdma1x_supported ? "yes" : "no",
            ctx->evdo_supported ? "yes" : "no");

    registration_check_step (ctx);
}

/*****************************************************************************/

#define ALL_CDMA_ACCESS_TECHNOLOGIES_MASK           \
    (ALL_CDMA_CDMA1X_ACCESS_TECHNOLOGIES_MASK |     \
     ALL_CDMA_EVDO_ACCESS_TECHNOLOGIES_MASK)

#define ALL_CDMA_EVDO_ACCESS_TECHNOLOGIES_MASK      \
    (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 |             \
     MM_MODEM_ACCESS_TECHNOLOGY_EVDOA |             \
     MM_MODEM_ACCESS_TECHNOLOGY_EVDOB)

#define ALL_CDMA_CDMA1X_ACCESS_TECHNOLOGIES_MASK    \
    (MM_MODEM_ACCESS_TECHNOLOGY_1XRTT)

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
            mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                         MM_MODEM_STATE_REGISTERED,
                                         MM_MODEM_STATE_REASON_NONE);
            /* TODO: report proper EVDO revision (0/A/B) */
            mm_iface_modem_update_access_tech (MM_IFACE_MODEM (self),
                                               MM_MODEM_ACCESS_TECHNOLOGY_EVDO0,
                                               ALL_CDMA_EVDO_ACCESS_TECHNOLOGIES_MASK);
            break;
        case MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN:
            mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                         MM_MODEM_STATE_DISABLED,
                                         MM_MODEM_STATE_REASON_NONE);
            mm_iface_modem_update_access_tech (MM_IFACE_MODEM (self),
                                               0,
                                               ALL_CDMA_EVDO_ACCESS_TECHNOLOGIES_MASK);
            break;
        }
    }

    g_object_unref (skeleton);
}

void
mm_iface_modem_cdma_update_cdma1x_registration_state (MMIfaceModemCdma *self,
                                                      MMModemCdmaRegistrationState state)
{
    MmGdbusModemCdma *skeleton = NULL;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_CDMA_CDMA1X_NETWORK_SUPPORTED, &supported,
                  MM_IFACE_MODEM_CDMA_DBUS_SKELETON, &skeleton,
                  NULL);

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
            mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                         MM_MODEM_STATE_REGISTERED,
                                         MM_MODEM_STATE_REASON_NONE);
            mm_iface_modem_update_access_tech (MM_IFACE_MODEM (self),
                                               MM_MODEM_ACCESS_TECHNOLOGY_1XRTT,
                                               ALL_CDMA_CDMA1X_ACCESS_TECHNOLOGIES_MASK);
            break;
        case MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN:
            mm_iface_modem_update_state (MM_IFACE_MODEM (self),
                                         MM_MODEM_STATE_DISABLED,
                                         MM_MODEM_STATE_REASON_NONE);
            mm_iface_modem_update_access_tech (MM_IFACE_MODEM (self),
                                               0,
                                               ALL_CDMA_CDMA1X_ACCESS_TECHNOLOGIES_MASK);
            break;
        }
    }

    g_object_unref (skeleton);
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
    ENABLING_STEP_RUN_ALL_REGISTRATION_CHECKS,
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
run_all_registration_checks_ready (MMIfaceModemCdma *self,
                                   GAsyncResult *res,
                                   EnablingContext *ctx)
{
    GError *error = NULL;

    if (!mm_iface_modem_cdma_run_all_registration_checks_finish (self,
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
    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_RUN_ALL_REGISTRATION_CHECKS:
        mm_iface_modem_cdma_run_all_registration_checks (ctx->self,
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
    INITIALIZATION_STEP_MEID,
    INITIALIZATION_STEP_ESN,
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

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY)                                \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModemCdma *self,                        \
                         GAsyncResult *res,                             \
                         InitializationContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
        gchar *val;                                                     \
                                                                        \
        val = MM_IFACE_MODEM_CDMA_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_modem_cdma_set_##NAME (ctx->skeleton, val);            \
        g_free (val);                                                   \
                                                                        \
        if (error) {                                                    \
            mm_warn ("couldn't load %s: '%s'", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (ctx);                            \
    }

STR_REPLY_READY_FN (meid, "MEID")
STR_REPLY_READY_FN (esn, "ESN")

static void
interface_initialization_step (InitializationContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_MEID:
        /* MEID value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem_cdma_get_meid (ctx->skeleton) &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->load_meid &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->load_meid_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->load_meid (
                ctx->self,
                (GAsyncReadyCallback)load_meid_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_ESN:
        /* ESN value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem_cdma_get_esn (ctx->skeleton) &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->load_esn &&
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->load_esn_finish) {
            MM_IFACE_MODEM_CDMA_GET_INTERFACE (ctx->self)->load_esn (
                ctx->self,
                (GAsyncReadyCallback)load_esn_ready,
                ctx);
            return;
        }
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
        mm_gdbus_modem_cdma_set_sid (skeleton, MM_IFACE_MODEM_CDMA_SID_UNKNOWN);
        mm_gdbus_modem_cdma_set_nid (skeleton, MM_IFACE_MODEM_CDMA_NID_UNKNOWN);

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
                            "EVDO Registration State",
                            "Registration state of the modem in the EVDO network",
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
                               "EVDO network supported",
                               "Whether the modem works in the EVDO network",
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
