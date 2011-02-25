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
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <string.h>
#include <dbus/dbus-glib.h>
#include "mm-modem-cdma.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-marshal.h"
#include "mm-auth-provider.h"

static void impl_modem_cdma_get_signal_quality (MMModemCdma *modem, DBusGMethodInvocation *context);
static void impl_modem_cdma_get_esn (MMModemCdma *modem, DBusGMethodInvocation *context);
static void impl_modem_cdma_get_serving_system (MMModemCdma *modem, DBusGMethodInvocation *context);
static void impl_modem_cdma_get_registration_state (MMModemCdma *modem, DBusGMethodInvocation *context);
static void impl_modem_cdma_activate (MMModemCdma *modem, DBusGMethodInvocation *context);
static void impl_modem_cdma_activate_manual (MMModemCdma *modem, DBusGMethodInvocation *context);

#include "mm-modem-cdma-glue.h"

enum {
    SIGNAL_QUALITY,
    REGISTRATION_STATE_CHANGED,
    ACTIVATION_STATE_CHANGED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/*****************************************************************************/

static void
str_call_done (MMModem *modem, const char *result, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, result);
}

static void
str_call_not_supported (MMModemCdma *self,
                        MMModemStringFn callback,
                        gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_string_new (MM_MODEM (self), callback, user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");

    mm_callback_info_schedule (info);
}

static void
uint_op_not_supported (MMModem *self,
                       MMModemUIntFn callback,
                       gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (self, callback, user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");
    mm_callback_info_schedule (info);
}

static void
uint_call_done (MMModem *modem, guint32 result, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, result);
}

static void
serving_system_call_done (MMModemCdma *self,
                          guint32 class,
                          unsigned char band,
                          guint32 sid,
                          GError *error,
                          gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else {
        GValueArray *array;
        GValue value = { 0, };
        char band_str[2] = { 0, 0 };

        array = g_value_array_new (3);

        /* Band Class */
        g_value_init (&value, G_TYPE_UINT);
        g_value_set_uint (&value, class);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        /* Band */
        g_value_init (&value, G_TYPE_STRING);
        band_str[0] = band;
        g_value_set_string (&value, band_str);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        /* SID */
        g_value_init (&value, G_TYPE_UINT);
        g_value_set_uint (&value, sid);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        dbus_g_method_return (context, array);
    }
}

static void
serving_system_invoke (MMCallbackInfo *info)
{
    MMModemCdmaServingSystemFn callback = (MMModemCdmaServingSystemFn) info->callback;

    callback (MM_MODEM_CDMA (info->modem), 0, 0, 0, info->error, info->user_data);
}

static void
serving_system_call_not_supported (MMModemCdma *self,
                                   MMModemCdmaServingSystemFn callback,
                                   gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (self), serving_system_invoke, G_CALLBACK (callback), user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");

    mm_callback_info_schedule (info);
}

void
mm_modem_cdma_get_serving_system (MMModemCdma *self,
                                  MMModemCdmaServingSystemFn callback,
                                  gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_CDMA (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_CDMA_GET_INTERFACE (self)->get_serving_system)
        MM_MODEM_CDMA_GET_INTERFACE (self)->get_serving_system (self, callback, user_data);
    else
        serving_system_call_not_supported (self, callback, user_data);
}

static void
impl_modem_cdma_get_serving_system (MMModemCdma *modem,
                                    DBusGMethodInvocation *context)
{
    mm_modem_cdma_get_serving_system (modem, serving_system_call_done, context);
}

void
mm_modem_cdma_get_esn (MMModemCdma *self,
                       MMModemStringFn callback,
                       gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_CDMA (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_CDMA_GET_INTERFACE (self)->get_esn)
        MM_MODEM_CDMA_GET_INTERFACE (self)->get_esn (self, callback, user_data);
    else
        str_call_not_supported (self, callback, user_data);
}

static void
esn_auth_cb (MMAuthRequest *req,
             GObject *owner,
             DBusGMethodInvocation *context,
             gpointer user_data)
{
    MMModemCdma *self = MM_MODEM_CDMA (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise get the ESN */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_cdma_get_esn (self, str_call_done, context);
}

static void
impl_modem_cdma_get_esn (MMModemCdma *self, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to get the ESN */
    if (!mm_modem_auth_request (MM_MODEM (self),
                                MM_AUTHORIZATION_DEVICE_INFO,
                                context,
                                esn_auth_cb,
                                NULL,
                                NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

void
mm_modem_cdma_get_signal_quality (MMModemCdma *self,
                                  MMModemUIntFn callback,
                                  gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_CDMA (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_CDMA_GET_INTERFACE (self)->get_signal_quality)
        MM_MODEM_CDMA_GET_INTERFACE (self)->get_signal_quality (self, callback, user_data);
    else
        uint_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void
impl_modem_cdma_get_signal_quality (MMModemCdma *modem, DBusGMethodInvocation *context)
{
    mm_modem_cdma_get_signal_quality (modem, uint_call_done, context);
}

void
mm_modem_cdma_emit_signal_quality_changed (MMModemCdma *self, guint32 quality)
{
    g_return_if_fail (MM_IS_MODEM_CDMA (self));

    g_signal_emit (self, signals[SIGNAL_QUALITY], 0, quality);
}

void mm_modem_cdma_activate(MMModemCdma *self, MMModemUIntFn callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_CDMA (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_CDMA_GET_INTERFACE (self)->activate)
        MM_MODEM_CDMA_GET_INTERFACE (self)->activate(self, callback, user_data);
    else
        uint_op_not_supported (MM_MODEM (self), callback, user_data);
}

static void impl_modem_cdma_activate(MMModemCdma *modem,
                                     DBusGMethodInvocation *context)
{
    mm_modem_cdma_activate (modem, uint_call_done, context);
}


void mm_modem_cdma_activate_manual(MMModemCdma *self, MMModemUIntFn callback,
                                   gpointer user_data) {
    g_return_if_fail (MM_IS_MODEM_CDMA (self));
    g_return_if_fail (callback != NULL);
    if (MM_MODEM_CDMA_GET_INTERFACE (self)->activate_manual)
        MM_MODEM_CDMA_GET_INTERFACE (self)->activate_manual(self, callback, user_data);
    else
        uint_op_not_supported (MM_MODEM (self), callback, user_data);
}
static void impl_modem_cdma_activate_manual (MMModemCdma *modem,
                                             DBusGMethodInvocation *context) {
    mm_modem_cdma_activate_manual(modem, uint_call_done, context);
}

/*****************************************************************************/

static void
get_registration_state_call_done (MMModemCdma *self,
                                  MMModemCdmaRegistrationState cdma_1x_reg_state,
                                  MMModemCdmaRegistrationState evdo_reg_state,
                                  GError *error,
                                  gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, cdma_1x_reg_state, evdo_reg_state);
}

void
mm_modem_cdma_get_registration_state (MMModemCdma *self,
                                      MMModemCdmaRegistrationStateFn callback,
                                      gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_CDMA (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_CDMA_GET_INTERFACE (self)->get_registration_state)
        MM_MODEM_CDMA_GET_INTERFACE (self)->get_registration_state (self, callback, user_data);
    else {
        GError *error;

        error = g_error_new_literal (MM_MODEM_ERROR,
                                     MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                     "Operation not supported");

        callback (self,
                  MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
                  MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN,
                  error,
                  user_data);

        g_error_free (error);
    }
}

static void
impl_modem_cdma_get_registration_state (MMModemCdma *modem, DBusGMethodInvocation *context)
{
    mm_modem_cdma_get_registration_state (modem, get_registration_state_call_done, context);
}

void
mm_modem_cdma_emit_registration_state_changed (MMModemCdma *self,
                                               MMModemCdmaRegistrationState cdma_1x_new_state,
                                               MMModemCdmaRegistrationState evdo_new_state)
{
    g_return_if_fail (MM_IS_MODEM_CDMA (self));

    g_signal_emit (self, signals[REGISTRATION_STATE_CHANGED], 0, cdma_1x_new_state, evdo_new_state);
}

/*****************************************************************************/

#define DBUS_TYPE_G_MAP_OF_VARIANT (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))

static void
mm_modem_cdma_init (gpointer g_iface)
{
    GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_CDMA_MEID,
                              "MEID",
                              "MEID",
                              NULL,
                              G_PARAM_READABLE));

    /* Signals */
    signals[SIGNAL_QUALITY] =
        g_signal_new ("signal-quality",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemCdma, signal_quality),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    signals[REGISTRATION_STATE_CHANGED] =
        g_signal_new ("registration-state-changed",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemCdma, registration_state_changed),
                      NULL, NULL,
                      mm_marshal_VOID__UINT_UINT,
                      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

    signals[ACTIVATION_STATE_CHANGED] =
        g_signal_new ("activation-state-changed",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemCdma, registration_state_changed),
                      NULL, NULL,
                      mm_marshal_VOID__UINT_UINT_BOXED,
                      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, DBUS_TYPE_G_MAP_OF_VARIANT);

    initialized = TRUE;
}

GType
mm_modem_cdma_get_type (void)
{
    static GType modem_type = 0;

    if (!G_UNLIKELY (modem_type)) {
        const GTypeInfo modem_info = {
            sizeof (MMModemCdma), /* class_size */
            mm_modem_cdma_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        modem_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "MMModemCdma",
                                             &modem_info, 0);

        g_type_interface_add_prerequisite (modem_type, MM_TYPE_MODEM);

        dbus_g_object_type_install_info (modem_type, &dbus_glib_mm_modem_cdma_object_info);
    }

    return modem_type;
}
