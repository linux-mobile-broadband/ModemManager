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
 * Copyright (C) 2010 Guido Guenther <agx@sigxcpu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-log-object.h"

#define SUPPORT_CHECKED_TAG "3gpp-ussd-support-checked-tag"
#define SUPPORTED_TAG       "3gpp-ussd-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/

void
mm_iface_modem_3gpp_ussd_bind_simple_status (MMIfaceModem3gppUssd *self,
                                             MMSimpleStatus *status)
{
    /* Nothing shown in simple status */
}

/*****************************************************************************/

static gboolean
ensure_enabled (MMBaseModem *self,
                GError **error)
{
    MMModemState modem_state;

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
        return FALSE;

    case MM_MODEM_STATE_INITIALIZING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_WRONG_STATE,
                     "Cannot perform USSD action: "
                     "device not enabled yet");
        return FALSE;

    case MM_MODEM_STATE_DISABLING:
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_WRONG_STATE,
                     "Cannot perform USSD action: "
                     "currently being disabled");
        return FALSE;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        break;

    default:
        g_assert_not_reached ();
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gppUssd *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gppUssd *self;
} HandleCancelContext;

static void
handle_cancel_context_free (HandleCancelContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static void
handle_cancel_ready (MMIfaceModem3gppUssd *self,
                     GAsyncResult *res,
                     HandleCancelContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->cancel_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem3gpp_ussd_complete_cancel (ctx->skeleton, ctx->invocation);

    handle_cancel_context_free (ctx);
}

static void
handle_cancel_auth_ready (MMBaseModem *self,
                          GAsyncResult *res,
                          HandleCancelContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error) ||
        !ensure_enabled (self, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_cancel_context_free (ctx);
        return;
    }

    g_assert (MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->cancel != NULL);
    g_assert (MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->cancel_finish != NULL);

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->cancel (
        MM_IFACE_MODEM_3GPP_USSD (self),
        (GAsyncReadyCallback)handle_cancel_ready,
        ctx);
}

static gboolean
handle_cancel (MmGdbusModem3gppUssd *skeleton,
               GDBusMethodInvocation *invocation,
               MMIfaceModem3gppUssd *self)
{
    HandleCancelContext *ctx;

    ctx = g_new (HandleCancelContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_USSD,
                             (GAsyncReadyCallback)handle_cancel_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gppUssd *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gppUssd *self;
    gchar *command;
} HandleRespondContext;

static void
handle_respond_context_free (HandleRespondContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->command);
    g_free (ctx);
}

static void
handle_respond_ready (MMIfaceModem3gppUssd *self,
                      GAsyncResult *res,
                      HandleRespondContext *ctx)
{
    GError *error = NULL;
    gchar *reply;

    reply = MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->send_finish (self, res, &error);
    if (!reply)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem3gpp_ussd_complete_respond (ctx->skeleton,
                                                  ctx->invocation,
                                                  reply);
        g_free (reply);
    }
    handle_respond_context_free (ctx);
}

static void
handle_respond_auth_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           HandleRespondContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error) ||
        !ensure_enabled (self, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_respond_context_free (ctx);
        return;
    }

    g_assert (MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->send != NULL);
    g_assert (MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->send_finish != NULL);

    switch (mm_gdbus_modem3gpp_ussd_get_state (ctx->skeleton)) {
    case MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE:
    case MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot respond USSD: "
                                               "no active session");
        break;

    case MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->send (
            MM_IFACE_MODEM_3GPP_USSD (self),
            ctx->command,
            (GAsyncReadyCallback)handle_respond_ready,
            ctx);
        return;

    case MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN:
    default:
        /* We should never have a DBus request when in UNKNOWN state */
        g_assert_not_reached ();
        break;
    }

    handle_respond_context_free (ctx);
}

static gboolean
handle_respond (MmGdbusModem3gppUssd *skeleton,
                GDBusMethodInvocation *invocation,
                const gchar *command,
                MMIfaceModem3gppUssd *self)
{
    HandleRespondContext *ctx;

    ctx = g_new (HandleRespondContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->command = g_strdup (command);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_USSD,
                             (GAsyncReadyCallback)handle_respond_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gppUssd *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gppUssd *self;
    gchar *command;
} HandleInitiateContext;

static void
handle_initiate_context_free (HandleInitiateContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx->command);
    g_free (ctx);
}

static void
handle_initiate_ready (MMIfaceModem3gppUssd *self,
                       GAsyncResult *res,
                       HandleInitiateContext *ctx)
{
    GError *error = NULL;
    gchar *reply;

    reply = MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->send_finish (self, res, &error);
    if (!reply)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem3gpp_ussd_complete_initiate (ctx->skeleton,
                                                   ctx->invocation,
                                                   reply);
        g_free (reply);
    }
    handle_initiate_context_free (ctx);
}

static void
handle_initiate_auth_ready (MMBaseModem *self,
                            GAsyncResult *res,
                            HandleInitiateContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error) ||
        !ensure_enabled (self, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_initiate_context_free (ctx);
        return;
    }

    g_assert (MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->send != NULL);
    g_assert (MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->send_finish != NULL);

    switch (mm_gdbus_modem3gpp_ussd_get_state (ctx->skeleton)) {
    case MM_MODEM_3GPP_USSD_SESSION_STATE_ACTIVE:
    case MM_MODEM_3GPP_USSD_SESSION_STATE_USER_RESPONSE:
        g_dbus_method_invocation_return_error (ctx->invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot initiate USSD: "
                                               "a session is already active");
        break;

    case MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->send (
            MM_IFACE_MODEM_3GPP_USSD (self),
            ctx->command,
            (GAsyncReadyCallback)handle_initiate_ready,
            ctx);
        return;

    case MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN:
    default:
        /* We should never have a DBus request when in UNKNOWN state */
        g_assert_not_reached ();
        break;
    }

    handle_initiate_context_free (ctx);
}

static gboolean
handle_initiate (MmGdbusModem3gppUssd *skeleton,
                 GDBusMethodInvocation *invocation,
                 const gchar *command,
                 MMIfaceModem3gppUssd *self)
{
    HandleInitiateContext *ctx;

    ctx = g_new (HandleInitiateContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->command = g_strdup (command);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_USSD,
                             (GAsyncReadyCallback)handle_initiate_auth_ready,
                             ctx);
    return TRUE;
}

/*****************************************************************************/

gchar *
mm_iface_modem_3gpp_ussd_encode (MMIfaceModem3gppUssd *self,
                                 const gchar *command,
                                 guint *scheme,
                                 GError **error)
{
    return MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->encode (self, command, scheme, error);
}

gchar *
mm_iface_modem_3gpp_ussd_decode (MMIfaceModem3gppUssd *self,
                                 const gchar *reply,
                                 GError **error)
{
    return MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->decode (self, reply, error);
}

/*****************************************************************************/

MMModem3gppUssdSessionState
mm_iface_modem_3gpp_ussd_get_state (MMIfaceModem3gppUssd *self)
{
    MmGdbusModem3gppUssd        *skeleton = NULL;
    MMModem3gppUssdSessionState  state;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &skeleton,
                  NULL);

    if (!skeleton)
        return MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN;

    state = (MMModem3gppUssdSessionState) mm_gdbus_modem3gpp_ussd_get_state (skeleton);
    g_object_unref (skeleton);
    return state;
}

void
mm_iface_modem_3gpp_ussd_update_state (MMIfaceModem3gppUssd *self,
                                       MMModem3gppUssdSessionState new_state)
{
    MmGdbusModem3gppUssd *skeleton = NULL;
    MMModem3gppUssdSessionState old_state;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &skeleton,
                  NULL);

    if (!skeleton)
      return;

    old_state = (MMModem3gppUssdSessionState) mm_gdbus_modem3gpp_ussd_get_state (skeleton);

    if (old_state != new_state)
        mm_gdbus_modem3gpp_ussd_set_state (skeleton, new_state);

    g_object_unref (skeleton);
}

void
mm_iface_modem_3gpp_ussd_update_network_notification (MMIfaceModem3gppUssd *self,
                                                      const gchar *network_notification)
{
    MmGdbusModem3gppUssd *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &skeleton,
                  NULL);

    if (!skeleton)
      return;

    mm_gdbus_modem3gpp_ussd_set_network_notification (skeleton,
                                                      network_notification);
    g_object_unref (skeleton);
}

void
mm_iface_modem_3gpp_ussd_update_network_request (MMIfaceModem3gppUssd *self,
                                                 const gchar *network_request)
{
    MmGdbusModem3gppUssd *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &skeleton,
                  NULL);

    if (!skeleton)
      return;

    mm_gdbus_modem3gpp_ussd_set_network_request (skeleton,
                                                 network_request);
    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (GTask *task);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    DisablingStep step;
    MmGdbusModem3gppUssd *skeleton;
};

static void
disabling_context_free (DisablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_ussd_disable_finish (MMIfaceModem3gppUssd *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
disable_unsolicited_events_ready (MMIfaceModem3gppUssd *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->disable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "couldn't disable unsolicited USSD events: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_disabling_step (task);
}

static void
cleanup_unsolicited_events_ready (MMIfaceModem3gppUssd *self,
                                  GAsyncResult *res,
                                  GTask *task)
{
    DisablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->cleanup_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "couldn't cleanup unsolicited USSD events: %s", error->message);
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
    MMIfaceModem3gppUssd *self;
    DisablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->disable_unsolicited_events (
            self,
            (GAsyncReadyCallback)disable_unsolicited_events_ready,
            task);
        return;

    case DISABLING_STEP_CLEANUP_UNSOLICITED_EVENTS:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->cleanup_unsolicited_events (
            self,
            (GAsyncReadyCallback)cleanup_unsolicited_events_ready,
            task);
        return;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self),
                                               MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_ussd_disable (MMIfaceModem3gppUssd *self,
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
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &ctx->skeleton,
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
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    EnablingStep step;
    MmGdbusModem3gppUssd *skeleton;
};

static void
enabling_context_free (EnablingContext *ctx)
{
    if (ctx->skeleton)
        g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_ussd_enable_finish (MMIfaceModem3gppUssd *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
setup_unsolicited_events_ready (MMIfaceModem3gppUssd *self,
                                GAsyncResult *res,
                                GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->setup_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "couldn't setup unsolicited USSD events: %s", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_enabling_step (task);
}

static void
enable_unsolicited_events_ready (MMIfaceModem3gppUssd *self,
                                 GAsyncResult *res,
                                 GTask *task)
{
    EnablingContext *ctx;
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_obj_dbg (self, "couldn't enable unsolicited USSD events: %s", error->message);
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
    MMIfaceModem3gppUssd *self;
    EnablingContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        ctx->step++;
        /* fall through */

    case ENABLING_STEP_SETUP_UNSOLICITED_EVENTS:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->setup_unsolicited_events (
            self,
            (GAsyncReadyCallback)setup_unsolicited_events_ready,
            task);
        return;

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->enable_unsolicited_events (
            self,
            (GAsyncReadyCallback)enable_unsolicited_events_ready,
            task);
        return;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        mm_iface_modem_3gpp_ussd_update_state (MM_IFACE_MODEM_3GPP_USSD (self),
                                               MM_MODEM_3GPP_USSD_SESSION_STATE_IDLE);
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_ussd_enable (MMIfaceModem3gppUssd *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    EnablingContext *ctx;
    GTask *task;

    ctx = g_new0 (EnablingContext, 1);
    ctx->step = ENABLING_STEP_FIRST;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)enabling_context_free);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &ctx->skeleton,
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
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MmGdbusModem3gppUssd *skeleton;
    InitializationStep step;
};

static void
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static void
check_support_ready (MMIfaceModem3gppUssd *self,
                     GAsyncResult *res,
                     GTask *task)
{
    InitializationContext *ctx;
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->check_support_finish (self,
                                                                              res,
                                                                              &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_obj_dbg (self, "USSD support check failed: %s", error->message);
            g_error_free (error);
        }
    } else {
        /* USSD is supported! */
        g_object_set_qdata (G_OBJECT (self),
                            supported_quark,
                            GUINT_TO_POINTER (TRUE));
    }

    /* Go on to next step */
    ctx = g_task_get_task_data (task);
    ctx->step++;
    interface_initialization_step (task);
}

static void
interface_initialization_step (GTask *task)
{
    MMIfaceModem3gppUssd *self;
    InitializationContext *ctx;

    self = g_task_get_source_object (task);
    ctx = g_task_get_task_data (task);

    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!support_checked_quark))
            support_checked_quark = (g_quark_from_static_string (
                                         SUPPORT_CHECKED_TAG));
        if (G_UNLIKELY (!supported_quark))
            supported_quark = (g_quark_from_static_string (
                                   SUPPORTED_TAG));

        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_CHECK_SUPPORT:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                   support_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (self),
                                support_checked_quark,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support it */
            g_object_set_qdata (G_OBJECT (self),
                                supported_quark,
                                GUINT_TO_POINTER (FALSE));

            if (MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->check_support &&
                MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->check_support_finish) {
                MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->check_support (
                    self,
                    (GAsyncReadyCallback)check_support_ready,
                    task);
                return;
            }

            /* If there is no implementation to check support, assume we DON'T
             * support it. */
        }

        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                   supported_quark))) {
            g_task_return_new_error (task,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNSUPPORTED,
                                     "USSD not supported");
            g_object_unref (task);
            return;
        }

        ctx->step++;
        /* fall through */

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-initiate",
                          G_CALLBACK (handle_initiate),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-respond",
                          G_CALLBACK (handle_respond),
                          self);
        g_signal_connect (ctx->skeleton,
                          "handle-cancel",
                          G_CALLBACK (handle_cancel),
                          self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem3gpp_ussd (MM_GDBUS_OBJECT_SKELETON (self),
                                                     MM_GDBUS_MODEM3GPP_USSD (ctx->skeleton));

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;

    default:
        break;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_3gpp_ussd_initialize_finish (MMIfaceModem3gppUssd *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

void
mm_iface_modem_3gpp_ussd_initialize (MMIfaceModem3gppUssd *self,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModem3gppUssd *skeleton = NULL;
    GTask *task;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem3gpp_ussd_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem3gpp_ussd_set_state (skeleton, MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN);
        mm_gdbus_modem3gpp_ussd_set_network_notification (skeleton, NULL);
        mm_gdbus_modem3gpp_ussd_set_network_request (skeleton, NULL);

        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */

    ctx = g_new0 (InitializationContext, 1);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)initialization_context_free);

    interface_initialization_step (task);
}

void
mm_iface_modem_3gpp_ussd_shutdown (MMIfaceModem3gppUssd *self)
{
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem3gpp_ussd (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_3gpp_ussd_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON,
                              "3GPP DBus skeleton",
                              "DBus skeleton for the 3GPP interface",
                              MM_GDBUS_TYPE_MODEM3GPP_USSD_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_3gpp_ussd_get_type (void)
{
    static GType iface_modem_3gpp_ussd_type = 0;

    if (!G_UNLIKELY (iface_modem_3gpp_ussd_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem3gppUssd), /* class_size */
            iface_modem_3gpp_ussd_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_3gpp_ussd_type = g_type_register_static (G_TYPE_INTERFACE,
                                                        "MMIfaceModem3gppUssd",
                                                        &info,
                                                        0);

        g_type_interface_add_prerequisite (iface_modem_3gpp_ussd_type, MM_TYPE_IFACE_MODEM_3GPP);
    }

    return iface_modem_3gpp_ussd_type;
}
