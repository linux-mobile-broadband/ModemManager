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

#include <stdlib.h>
#include <string.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-3gpp-ussd.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

#define SUPPORT_CHECKED_TAG "3gpp-ussd-support-checked-tag"
#define SUPPORTED_TAG       "3gpp-ussd-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/

void
mm_iface_modem_3gpp_ussd_bind_simple_status (MMIfaceModem3gppUssd *self,
                                             MMCommonSimpleProperties *status)
{
    /* Nothing shown in simple status */
}

/*****************************************************************************/

gchar *
mm_iface_modem_3gpp_ussd_encode (MMIfaceModem3gppUssd *self,
                                 const gchar *command,
                                 guint *scheme)
{
    return MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->encode (self, command, scheme);
}

gchar *
mm_iface_modem_3gpp_ussd_decode (MMIfaceModem3gppUssd *self,
                                 const gchar *reply)
{
    return MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->decode (self, reply);
}

/*****************************************************************************/

void
mm_iface_modem_3gpp_ussd_update_state (MMIfaceModem3gppUssd *self,
                                       MMModem3gppUssdSessionState new_state)
{
    MmGdbusModem3gppUssd *skeleton = NULL;
    MMModem3gppUssdSessionState old_state;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &skeleton,
                  NULL);

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

    mm_gdbus_modem3gpp_ussd_set_network_request (skeleton,
                                                 network_request);
    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_UNSOLICITED_RESULT_CODES,
    DISABLING_STEP_CLEANUP_UNSOLICITED_RESULT_CODES,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModem3gppUssd *self;
    MMAtSerialPort *primary;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModem3gppUssd *skeleton;
};

static DisablingContext *
disabling_context_new (MMIfaceModem3gppUssd *self,
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
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &ctx->skeleton,
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
mm_iface_modem_3gpp_ussd_disable_finish (MMIfaceModem3gppUssd *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disable_unsolicited_result_codes_ready (MMIfaceModem3gppUssd *self,
                                        GAsyncResult *res,
                                        DisablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->disable_unsolicited_result_codes_finish (self,
                                                                                            res,
                                                                                            &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Couldn't disable unsolicited result codes: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

static void
cleanup_unsolicited_result_codes_ready (MMIfaceModem3gppUssd *self,
                                        GAsyncResult *res,
                                        DisablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->cleanup_unsolicited_result_codes_finish (self,
                                                                                            res,
                                                                                            &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Couldn't cleanup unsolicited result codes: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_UNSOLICITED_RESULT_CODES:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (ctx->self)->disable_unsolicited_result_codes (
            ctx->self,
            (GAsyncReadyCallback)disable_unsolicited_result_codes_ready,
            ctx);
        return;

    case DISABLING_STEP_CLEANUP_UNSOLICITED_RESULT_CODES:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (ctx->self)->cleanup_unsolicited_result_codes (
            ctx->self,
            (GAsyncReadyCallback)cleanup_unsolicited_result_codes_ready,
            ctx);
        return;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_ussd_disable (MMIfaceModem3gppUssd *self,
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
    ENABLING_STEP_SETUP_UNSOLICITED_RESULT_CODES,
    ENABLING_STEP_ENABLE_UNSOLICITED_RESULT_CODES,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModem3gppUssd *self;
    MMAtSerialPort *primary;
    EnablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModem3gppUssd *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModem3gppUssd *self,
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
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &ctx->skeleton,
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
mm_iface_modem_3gpp_ussd_enable_finish (MMIfaceModem3gppUssd *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
setup_unsolicited_result_codes_ready (MMIfaceModem3gppUssd *self,
                                      GAsyncResult *res,
                                      EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->setup_unsolicited_result_codes_finish (self,
                                                                                          res,
                                                                                          &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Couldn't setup unsolicited result codes: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
enable_unsolicited_result_codes_ready (MMIfaceModem3gppUssd *self,
                                       GAsyncResult *res,
                                       EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->enable_unsolicited_result_codes_finish (self,
                                                                                           res,
                                                                                           &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Couldn't enable unsolicited result codes: '%s'", error->message);
        g_error_free (error);
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

    case ENABLING_STEP_SETUP_UNSOLICITED_RESULT_CODES:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (ctx->self)->setup_unsolicited_result_codes (
            ctx->self,
            (GAsyncReadyCallback)setup_unsolicited_result_codes_ready,
            ctx);
        return;

    case ENABLING_STEP_ENABLE_UNSOLICITED_RESULT_CODES:
        MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (ctx->self)->enable_unsolicited_result_codes (
            ctx->self,
            (GAsyncReadyCallback)enable_unsolicited_result_codes_ready,
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
mm_iface_modem_3gpp_ussd_enable (MMIfaceModem3gppUssd *self,
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
    INITIALIZATION_STEP_CHECK_SUPPORT,
    INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem3gppUssd *self;
    MMAtSerialPort *port;
    MmGdbusModem3gppUssd *skeleton;
    GSimpleAsyncResult *result;
    InitializationStep step;
};

static InitializationContext *
initialization_context_new (MMIfaceModem3gppUssd *self,
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
                  MM_IFACE_MODEM_3GPP_USSD_DBUS_SKELETON, &ctx->skeleton,
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
check_support_ready (MMIfaceModem3gppUssd *self,
                     GAsyncResult *res,
                     InitializationContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (self)->check_support_finish (self,
                                                                              res,
                                                                              &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_dbg ("USSD support check failed: '%s'", error->message);
            g_error_free (error);
        }
    } else {
        /* USSD is supported! */
        g_object_set_qdata (G_OBJECT (self),
                            supported_quark,
                            GUINT_TO_POINTER (TRUE));
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
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!support_checked_quark))
            support_checked_quark = (g_quark_from_static_string (
                                         SUPPORT_CHECKED_TAG));
        if (G_UNLIKELY (!supported_quark))
            supported_quark = (g_quark_from_static_string (
                                   SUPPORTED_TAG));

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CHECK_SUPPORT:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   support_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                support_checked_quark,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support it */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                supported_quark,
                                GUINT_TO_POINTER (FALSE));

            if (MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (ctx->self)->check_support &&
                MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (ctx->self)->check_support_finish) {
                MM_IFACE_MODEM_3GPP_USSD_GET_INTERFACE (ctx->self)->check_support (
                    ctx->self,
                    (GAsyncReadyCallback)check_support_ready,
                    ctx);
                return;
            }

            /* If there is no implementation to check support, assume we DON'T
             * support it. */
        }

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_FAIL_IF_UNSUPPORTED:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   supported_quark))) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "USSD not supported");
            initialization_context_complete_and_free (ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem3gpp_ussd (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                     MM_GDBUS_MODEM3GPP_USSD (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_3gpp_ussd_initialize_finish (MMIfaceModem3gppUssd *self,
                                            GAsyncResult *res,
                                            GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM_3GPP_USSD (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_3gpp_ussd_initialize (MMIfaceModem3gppUssd *self,
                                     MMAtSerialPort *port,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
    MmGdbusModem3gppUssd *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_3GPP_USSD (self));

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
    interface_initialization_step (initialization_context_new (self,
                                                               port,
                                                               callback,
                                                               user_data));
    g_object_unref (skeleton);
    return;
}

void
mm_iface_modem_3gpp_ussd_shutdown (MMIfaceModem3gppUssd *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_3GPP_USSD (self));

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
