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
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-firmware.h"
#include "mm-log.h"

#define SUPPORT_CHECKED_TAG "firmware-support-checked-tag"
#define SUPPORTED_TAG       "firmware-supported-tag"

static GQuark support_checked_quark;
static GQuark supported_quark;

/*****************************************************************************/

void
mm_iface_modem_firmware_bind_simple_status (MMIfaceModemFirmware *self,
                                             MMSimpleStatus *status)
{
}

/*****************************************************************************/
/* Handle the 'List' method from DBus */

typedef struct {
    MMIfaceModemFirmware *self;
    MmGdbusModemFirmware *skeleton;
    GDBusMethodInvocation *invocation;
    GList *list;
    MMFirmwareProperties *current;
} HandleListContext;

static void
handle_list_context_free (HandleListContext *ctx)
{
    if (ctx->list)
        g_list_free_full (ctx->list, (GDestroyNotify)g_object_unref);
    if (ctx->current)
        g_object_unref (ctx->current);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleListContext, ctx);
}

static void
load_current_ready (MMIfaceModemFirmware *self,
                    GAsyncResult *res,
                    HandleListContext *ctx)
{
    GVariantBuilder builder;
    GList *l;
    GError *error = NULL;

    /* reported current may be NULL and we don't treat it as error */
    ctx->current = MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->load_current_finish (self, res, &error);
    if (error) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_list_context_free (ctx);
        return;
    }

    /* Build array of dicts */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
    for (l = ctx->list; l; l = g_list_next (l))
        g_variant_builder_add_value (
            &builder,
            mm_firmware_properties_get_dictionary (MM_FIRMWARE_PROPERTIES (l->data)));

    mm_gdbus_modem_firmware_complete_list (
        ctx->skeleton,
        ctx->invocation,
        (ctx->current ? mm_firmware_properties_get_unique_id (ctx->current) : ""),
        g_variant_builder_end (&builder));
    handle_list_context_free (ctx);
}

static void
load_list_ready (MMIfaceModemFirmware *self,
                 GAsyncResult *res,
                 HandleListContext *ctx)
{
    GError *error = NULL;

    ctx->list = MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->load_list_finish (self, res, &error);
    if (error) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_list_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->load_current (MM_IFACE_MODEM_FIRMWARE (self),
                                                                (GAsyncReadyCallback)load_current_ready,
                                                                ctx);
}

static void
list_auth_ready (MMBaseModem *self,
                 GAsyncResult *res,
                 HandleListContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_list_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->load_list (MM_IFACE_MODEM_FIRMWARE (self),
                                                             (GAsyncReadyCallback)load_list_ready,
                                                             ctx);
}

static gboolean
handle_list (MmGdbusModemFirmware *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModemFirmware *self)
{
    HandleListContext *ctx;

    g_assert (MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->load_list != NULL);
    g_assert (MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->load_list_finish != NULL);
    g_assert (MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->load_current != NULL);
    g_assert (MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->load_current_finish != NULL);

    ctx = g_slice_new (HandleListContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)list_auth_ready,
                             ctx);

    return TRUE;
}

/*****************************************************************************/
/* Handle the 'Select' method from DBus */

typedef struct {
    MMIfaceModemFirmware *self;
    MmGdbusModemFirmware *skeleton;
    GDBusMethodInvocation *invocation;
    gchar *name;
} HandleSelectContext;

static void
handle_select_context_free (HandleSelectContext *ctx)
{
    g_free (ctx->name);
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_slice_free (HandleSelectContext, ctx);
}

static void
change_current_ready (MMIfaceModemFirmware *self,
                      GAsyncResult *res,
                      HandleSelectContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->change_current_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_firmware_complete_select (ctx->skeleton, ctx->invocation);
    handle_select_context_free (ctx);
}

static void
select_auth_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   HandleSelectContext *ctx)
{
    GError *error = NULL;

    if (!mm_base_modem_authorize_finish (self, res, &error)) {
        g_dbus_method_invocation_take_error (ctx->invocation, error);
        handle_select_context_free (ctx);
        return;
    }

    MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->change_current (MM_IFACE_MODEM_FIRMWARE (self),
                                                                  ctx->name,
                                                                  (GAsyncReadyCallback)change_current_ready,
                                                                  ctx);
}

static gboolean
handle_select (MmGdbusModemFirmware *skeleton,
               GDBusMethodInvocation *invocation,
               const gchar *name,
               MMIfaceModemFirmware *self)
{
    HandleSelectContext *ctx;

    g_assert (MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->change_current != NULL);
    g_assert (MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->change_current_finish != NULL);

    ctx = g_slice_new (HandleSelectContext);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    ctx->name = g_strdup (name);

    mm_base_modem_authorize (MM_BASE_MODEM (self),
                             invocation,
                             MM_AUTHORIZATION_DEVICE_CONTROL,
                             (GAsyncReadyCallback)select_auth_ready,
                             ctx);

    return TRUE;
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
    MMIfaceModemFirmware *self;
    MmGdbusModemFirmware *skeleton;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
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
check_support_ready (MMIfaceModemFirmware *self,
                     GAsyncResult *res,
                     InitializationContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (self)->check_support_finish (self,
                                                                             res,
                                                                             &error)) {
        if (error) {
            /* This error shouldn't be treated as critical */
            mm_dbg ("Firmware support check failed: '%s'", error->message);
            g_error_free (error);
        }
    } else {
        /* Firmware is supported! */
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
    /* Don't run new steps if we're cancelled */
    if (initialization_context_complete_and_free_if_cancelled (ctx))
        return;

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

            if (MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (ctx->self)->check_support &&
                MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (ctx->self)->check_support_finish) {
                MM_IFACE_MODEM_FIRMWARE_GET_INTERFACE (ctx->self)->check_support (
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
                                             "Firmware interface not available");
            initialization_context_complete_and_free (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-list",
                          G_CALLBACK (handle_list),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-select",
                          G_CALLBACK (handle_select),
                          ctx->self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem_firmware (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                     MM_GDBUS_MODEM_FIRMWARE (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_firmware_initialize_finish (MMIfaceModemFirmware *self,
                                           GAsyncResult *res,
                                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_firmware_initialize (MMIfaceModemFirmware *self,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    InitializationContext *ctx;
    MmGdbusModemFirmware *skeleton = NULL;

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_FIRMWARE_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_firmware_skeleton_new ();
        g_object_set (self,
                      MM_IFACE_MODEM_FIRMWARE_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_firmware_initialize);
    ctx->step = INITIALIZATION_STEP_FIRST;
    ctx->skeleton = skeleton;

    interface_initialization_step (ctx);
}

void
mm_iface_modem_firmware_shutdown (MMIfaceModemFirmware *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_FIRMWARE (self));

    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem_firmware (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_FIRMWARE_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_firmware_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_FIRMWARE_DBUS_SKELETON,
                              "Firmware DBus skeleton",
                              "DBus skeleton for the Firmware interface",
                              MM_GDBUS_TYPE_MODEM_FIRMWARE_SKELETON,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_firmware_get_type (void)
{
    static GType iface_modem_firmware_type = 0;

    if (!G_UNLIKELY (iface_modem_firmware_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModemFirmware), /* class_size */
            iface_modem_firmware_init,     /* base_init */
            NULL,                           /* base_finalize */
        };

        iface_modem_firmware_type = g_type_register_static (G_TYPE_INTERFACE,
                                                            "MMIfaceModemFirmware",
                                                            &info,
                                                            0);

        g_type_interface_add_prerequisite (iface_modem_firmware_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_firmware_type;
}
