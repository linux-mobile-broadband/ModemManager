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
#include "mm-iface-modem-messaging.h"
#include "mm-sms-list.h"
#include "mm-log.h"

#define SUPPORT_CHECKED_TAG "messaging-support-checked-tag"
#define SUPPORTED_TAG       "messaging-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/

void
mm_iface_modem_messaging_bind_simple_status (MMIfaceModemMessaging *self,
                                            MMCommonSimpleProperties *status)
{
}

/*****************************************************************************/

gboolean
mm_iface_modem_messaging_take_part (MMIfaceModemMessaging *self,
                                    MMSmsPart *sms_part,
                                    gboolean received)
{
    MMSmsList *list = NULL;
    GError *error = NULL;
    gboolean added;

    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_SMS_LIST, &list,
                  NULL);
    g_assert (list != NULL);
    added = mm_sms_list_take_part (list, sms_part, received, &error);
    if (!added) {
        mm_dbg ("Couldn't take part in SMS list: '%s'", error->message);
        g_error_free (error);

        /* If part wasn't taken, we need to free the part ourselves */
        mm_sms_part_free (sms_part);
    }
    g_object_unref (list);

    return added;
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModemMessaging *self;
    MMAtSerialPort *primary;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemMessaging *skeleton;
};

static DisablingContext *
disabling_context_new (MMIfaceModemMessaging *self,
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
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &ctx->skeleton,
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
mm_iface_modem_messaging_disable_finish (MMIfaceModemMessaging *self,
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
        /* Clear SMS list */
        g_object_set (ctx->self,
                      MM_IFACE_MODEM_MESSAGING_SMS_LIST, NULL,
                      NULL);

        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_messaging_disable (MMIfaceModemMessaging *self,
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
    ENABLING_STEP_SETUP_SMS_FORMAT,
    ENABLING_STEP_LOAD_INITIAL_SMS_PARTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModemMessaging *self;
    MMAtSerialPort *primary;
    EnablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModemMessaging *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModemMessaging *self,
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
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &ctx->skeleton,
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
mm_iface_modem_messaging_enable_finish (MMIfaceModemMessaging *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
setup_sms_format_ready (MMIfaceModemMessaging *self,
                        GAsyncResult *res,
                        EnablingContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->setup_sms_format_finish (self,
                                                                                 res,
                                                                                 &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    mm_dbg ("SMS format correctly setup");

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
load_initial_sms_parts_ready (MMIfaceModemMessaging *self,
                              GAsyncResult *res,
                              EnablingContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->load_initial_sms_parts_finish (self,
                                                                                       res,
                                                                                       &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    mm_dbg ("Initial SMS parts correctly loaded");

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
interface_enabling_step (EnablingContext *ctx)
{
    switch (ctx->step) {
    case ENABLING_STEP_FIRST: {
        MMSmsList *list;

        list = mm_sms_list_new (MM_BASE_MODEM (ctx->self));
        g_object_set (ctx->self,
                      MM_IFACE_MODEM_MESSAGING_SMS_LIST, list,
                      NULL);
        g_object_unref (list);

        /* Fall down to next step */
        ctx->step++;
    }

    case ENABLING_STEP_SETUP_SMS_FORMAT:
        /* Allow setting SMS format to use */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_sms_format &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_sms_format_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->setup_sms_format (
                ctx->self,
                (GAsyncReadyCallback)setup_sms_format_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LOAD_INITIAL_SMS_PARTS:
        /* Allow loading the initial list of SMS parts */
        if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_initial_sms_parts &&
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_initial_sms_parts_finish) {
            MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->load_initial_sms_parts (
                ctx->self,
                (GAsyncReadyCallback)load_initial_sms_parts_ready,
                ctx);
            return;
        }
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
mm_iface_modem_messaging_enable (MMIfaceModemMessaging *self,
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
    MMIfaceModemMessaging *self;
    MMAtSerialPort *port;
    MmGdbusModemMessaging *skeleton;
    GSimpleAsyncResult *result;
    InitializationStep step;
};

static InitializationContext *
initialization_context_new (MMIfaceModemMessaging *self,
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
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &ctx->skeleton,
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
check_support_ready (MMIfaceModemMessaging *self,
                     GAsyncResult *res,
                     InitializationContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (self)->check_support_finish (self,
                                                                              res,
                                                                              &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_dbg ("Messaging support check failed: '%s'", error->message);
            g_error_free (error);
        }
    } else {
        /* Messaging is supported! */
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

            if (MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->check_support &&
                MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->check_support_finish) {
                MM_IFACE_MODEM_MESSAGING_GET_INTERFACE (ctx->self)->check_support (
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
                                             "Messaging not supported");
            initialization_context_complete_and_free (ctx);
            return;
        }

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_messaging (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                      MM_GDBUS_MODEM_MESSAGING (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_messaging_initialize_finish (MMIfaceModemMessaging *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM_MESSAGING (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_messaging_initialize (MMIfaceModemMessaging *self,
                                    MMAtSerialPort *port,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    MmGdbusModemMessaging *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_MESSAGING (self));

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_messaging_skeleton_new ();

        g_object_set (self,
                      MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, skeleton,
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
mm_iface_modem_messaging_shutdown (MMIfaceModemMessaging *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_MESSAGING (self));

    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_messaging (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_messaging_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_MESSAGING_DBUS_SKELETON,
                              "Messaging DBus skeleton",
                              "DBus skeleton for the Messaging interface",
                              MM_GDBUS_TYPE_MODEM_MESSAGING_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_MESSAGING_SMS_LIST,
                              "SMS list",
                              "List of SMS objects managed in the interface",
                              MM_TYPE_SMS_LIST,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_messaging_get_type (void)
{
    static GType iface_modem_messaging_type = 0;

    if (!G_UNLIKELY (iface_modem_messaging_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemMessaging), /* class_size */
            iface_modem_messaging_init,     /* base_init */
            NULL,                           /* base_finalize */
        };

        iface_modem_messaging_type = g_type_register_static (G_TYPE_INTERFACE,
                                                             "MMIfaceModemMessaging",
                                                             &info,
                                                             0);

        g_type_interface_add_prerequisite (iface_modem_messaging_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_messaging_type;
}
