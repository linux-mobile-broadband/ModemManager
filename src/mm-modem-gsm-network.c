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
 * Copyright (C) 2010 Red Hat, Inc.
 */

#include <string.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-gsm-network.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-marshal.h"

static void impl_gsm_modem_register (MMModemGsmNetwork *modem,
                                     const char *network_id,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_scan (MMModemGsmNetwork *modem,
                                 DBusGMethodInvocation *context);

static void impl_gsm_modem_set_apn (MMModemGsmNetwork *modem,
                                    const char *apn,
                                    DBusGMethodInvocation *context);

static void impl_gsm_modem_get_signal_quality (MMModemGsmNetwork *modem,
                                               DBusGMethodInvocation *context);

static void impl_gsm_modem_set_band (MMModemGsmNetwork *modem,
                                     MMModemGsmBand band,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_get_band (MMModemGsmNetwork *modem,
                                     DBusGMethodInvocation *context);

static void impl_gsm_modem_set_allowed_mode (MMModemGsmNetwork *modem,
                                             MMModemGsmAllowedMode mode,
                                             DBusGMethodInvocation *context);

static void impl_gsm_modem_set_network_mode (MMModemGsmNetwork *modem,
                                             MMModemDeprecatedMode old_mode,
                                             DBusGMethodInvocation *context);

static void impl_gsm_modem_get_network_mode (MMModemGsmNetwork *modem,
                                             DBusGMethodInvocation *context);

static void impl_gsm_modem_get_reg_info (MMModemGsmNetwork *modem,
                                         DBusGMethodInvocation *context);

#include "mm-modem-gsm-network-glue.h"

/*****************************************************************************/

enum {
    SIGNAL_QUALITY,
    REGISTRATION_INFO,
    NETWORK_MODE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/*****************************************************************************/

MMModemGsmAllowedMode
mm_modem_gsm_network_old_mode_to_allowed (MMModemDeprecatedMode old_mode)
{
    /* Translate deprecated mode into new mode */
    switch (old_mode) {
    case MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_2G_PREFERRED:
        return MM_MODEM_GSM_ALLOWED_MODE_2G_PREFERRED;
    case MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_3G_PREFERRED:
        return MM_MODEM_GSM_ALLOWED_MODE_3G_PREFERRED;
    case MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_2G_ONLY:
        return MM_MODEM_GSM_ALLOWED_MODE_2G_ONLY;
    case MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_3G_ONLY:
        return MM_MODEM_GSM_ALLOWED_MODE_3G_ONLY;
    case MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_ANY:
    default:
        return MM_MODEM_GSM_ALLOWED_MODE_ANY;
    }
}

MMModemDeprecatedMode
mm_modem_gsm_network_act_to_old_mode (MMModemGsmAccessTech act)
{
    /* Translate new mode into old deprecated mode */
    if (act & MM_MODEM_GSM_ACCESS_TECH_GPRS)
        return MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_GPRS;
    else if (act & MM_MODEM_GSM_ACCESS_TECH_EDGE)
        return MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_EDGE;
    else if (act & MM_MODEM_GSM_ACCESS_TECH_UMTS)
        return MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_UMTS;
    else if (act & MM_MODEM_GSM_ACCESS_TECH_HSDPA)
        return MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_HSDPA;
    else if (act & MM_MODEM_GSM_ACCESS_TECH_HSUPA)
        return MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_HSUPA;
    else if (act & MM_MODEM_GSM_ACCESS_TECH_HSPA)
        return MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_HSPA;
    else if (act & MM_MODEM_GSM_ACCESS_TECH_HSPA_PLUS)
        return MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_HSPA;

    return MM_MODEM_GSM_NETWORK_DEPRECATED_MODE_ANY;
}

/*****************************************************************************/

static void
async_call_done (MMModem *modem, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context);
}

static void
async_call_not_supported (MMModemGsmNetwork *self,
                          MMModemFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
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
uint_call_not_supported (MMModemGsmNetwork *self,
                         MMModemUIntFn callback,
                         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");
    mm_callback_info_schedule (info);
}

static void
reg_info_call_done (MMModemGsmNetwork *self,
                    MMModemGsmNetworkRegStatus status,
                    const char *oper_code,
                    const char *oper_name,
                    GError *error,
                    gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else {
        GValueArray *array;
        GValue value = { 0, };

        array = g_value_array_new (3);

        /* Status */
        g_value_init (&value, G_TYPE_UINT);
        g_value_set_uint (&value, (guint32) status);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        /* Operator code */
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, oper_code);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        /* Operator name */
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, oper_name);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        dbus_g_method_return (context, array);
    }
}

static void
reg_info_invoke (MMCallbackInfo *info)
{
    MMModemGsmNetworkRegInfoFn callback = (MMModemGsmNetworkRegInfoFn) info->callback;

    callback (MM_MODEM_GSM_NETWORK (info->modem), 0, NULL, NULL, info->error, info->user_data);
}

static void
reg_info_call_not_supported (MMModemGsmNetwork *self,
                             MMModemGsmNetworkRegInfoFn callback,
                             gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (self), reg_info_invoke, G_CALLBACK (callback), user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");

    mm_callback_info_schedule (info);
}

static void
scan_call_done (MMModemGsmNetwork *self,
                GPtrArray *results,
                GError *error,
                gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, results);
}

static void
gsm_network_scan_invoke (MMCallbackInfo *info)
{
    MMModemGsmNetworkScanFn callback = (MMModemGsmNetworkScanFn) info->callback;

    callback (MM_MODEM_GSM_NETWORK (info->modem), NULL, info->error, info->user_data);
}

static void
scan_call_not_supported (MMModemGsmNetwork *self,
                         MMModemGsmNetworkScanFn callback,
                         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (self), gsm_network_scan_invoke, G_CALLBACK (callback), user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");

    mm_callback_info_schedule (info);
}

/*****************************************************************************/

void
mm_modem_gsm_network_register (MMModemGsmNetwork *self,
                               const char *network_id,
                               MMModemFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->do_register)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->do_register (self, network_id, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_scan (MMModemGsmNetwork *self,
                           MMModemGsmNetworkScanFn callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->scan)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->scan (self, callback, user_data);
    else
        scan_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_set_apn (MMModemGsmNetwork *self,
                              const char *apn,
                              MMModemFn callback,
                              gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (apn != NULL);
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_apn)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_apn (self, apn, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_get_signal_quality (MMModemGsmNetwork *self,
                                         MMModemUIntFn callback,
                                         gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_signal_quality)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_signal_quality (self, callback, user_data);
    else
        uint_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_set_band (MMModemGsmNetwork *self,
                               MMModemGsmBand band,
                               MMModemFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_band)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_band (self, band, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_get_band (MMModemGsmNetwork *self,
                               MMModemUIntFn callback,
                               gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_band)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_band (self, callback, user_data);
    else
        uint_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_set_allowed_mode (MMModemGsmNetwork *self,
                                       MMModemGsmAllowedMode mode,
                                       MMModemFn callback,
                                       gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_allowed_mode)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->set_allowed_mode (self, mode, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_get_registration_info (MMModemGsmNetwork *self,
                                            MMModemGsmNetworkRegInfoFn callback,
                                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_registration_info)
        MM_MODEM_GSM_NETWORK_GET_INTERFACE (self)->get_registration_info (self, callback, user_data);
    else
        reg_info_call_not_supported (self, callback, user_data);
}

void
mm_modem_gsm_network_signal_quality (MMModemGsmNetwork *self,
                                     guint32 quality)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));

    g_signal_emit (self, signals[SIGNAL_QUALITY], 0, quality);
}

void
mm_modem_gsm_network_registration_info (MMModemGsmNetwork *self,
                                        MMModemGsmNetworkRegStatus status,
                                        const char *oper_code,
                                        const char *oper_name)
{
    g_return_if_fail (MM_IS_MODEM_GSM_NETWORK (self));

    g_signal_emit (self, signals[REGISTRATION_INFO], 0, status,
                   oper_code ? oper_code : "",
                   oper_name ? oper_name : "");
}

/*****************************************************************************/

static void
impl_gsm_modem_register (MMModemGsmNetwork *modem,
                         const char *network_id,
                         DBusGMethodInvocation *context)
{
    const char *id;

    /* DBus does not support NULL strings, so the caller should pass an empty string
       for manual registration. */
    if (strlen (network_id) < 1)
        id = NULL;
    else
        id = network_id;

    mm_modem_gsm_network_register (modem, id, async_call_done, context);
}

static void
scan_auth_cb (MMAuthRequest *req,
              GObject *owner,
              DBusGMethodInvocation *context,
              gpointer user_data)
{
    MMModemGsmNetwork *self = MM_MODEM_GSM_NETWORK (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise get the IMEI */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_gsm_network_scan (self, scan_call_done, context);
}

static void
impl_gsm_modem_scan (MMModemGsmNetwork *modem,
                     DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to request a scan */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_CONTROL,
                                context,
                                scan_auth_cb,
                                NULL,
                                NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

static void
impl_gsm_modem_set_apn (MMModemGsmNetwork *modem,
                        const char *apn,
                        DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_set_apn (modem, apn, async_call_done, context);
}

static void
impl_gsm_modem_get_signal_quality (MMModemGsmNetwork *modem,
                                   DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_get_signal_quality (modem, uint_call_done, context);
}

static gboolean
check_for_single_value (guint32 value)
{
    gboolean found = FALSE;
    guint32 i;

    for (i = 1; i <= 32; i++) {
        if (value & 0x1) {
            if (found)
                return FALSE;  /* More than one bit set */
            found = TRUE;
        }
        value >>= 1;
    }

    return TRUE;
}

static void
impl_gsm_modem_set_band (MMModemGsmNetwork *modem,
                         MMModemGsmBand band,
                         DBusGMethodInvocation *context)
{
    if (!check_for_single_value (band)) {
        GError *error;

        error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                     "Invalid arguments (more than one value given)");
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    mm_modem_gsm_network_set_band (modem, band, async_call_done, context);
}

static void
impl_gsm_modem_get_band (MMModemGsmNetwork *modem,
                         DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_get_band (modem, uint_call_done, context);
}

static void
impl_gsm_modem_set_network_mode (MMModemGsmNetwork *modem,
                                 MMModemDeprecatedMode old_mode,
                                 DBusGMethodInvocation *context)
{
    if (!check_for_single_value (old_mode)) {
        GError *error;

        error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                     "Invalid arguments (more than one value given)");
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    mm_modem_gsm_network_set_allowed_mode (modem,
                                           mm_modem_gsm_network_old_mode_to_allowed (old_mode),
                                           async_call_done,
                                           context);
}

static void
impl_gsm_modem_set_allowed_mode (MMModemGsmNetwork *modem,
                                 MMModemGsmAllowedMode mode,
                                 DBusGMethodInvocation *context)
{
    if (mode > MM_MODEM_GSM_ALLOWED_MODE_LAST) {
        GError *error;

        error = g_error_new (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                             "Unknown allowed mode %d", mode);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    mm_modem_gsm_network_set_allowed_mode (modem, mode, async_call_done, context);
}

static void
impl_gsm_modem_get_network_mode (MMModemGsmNetwork *modem,
                                 DBusGMethodInvocation *context)
{
    MMModemGsmAccessTech act = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;

    /* DEPRECATED; it's now a property so it's quite easy to handle */
    g_object_get (G_OBJECT (modem),
                  MM_MODEM_GSM_NETWORK_ACCESS_TECHNOLOGY, &act,
                  NULL);
    dbus_g_method_return (context, mm_modem_gsm_network_act_to_old_mode (act));
}

static void
impl_gsm_modem_get_reg_info (MMModemGsmNetwork *modem,
                             DBusGMethodInvocation *context)
{
    mm_modem_gsm_network_get_registration_info (modem, reg_info_call_done, context);
}

/*****************************************************************************/

static void
mm_modem_gsm_network_init (gpointer g_iface)
{
    GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_GSM_NETWORK_ALLOWED_MODE,
                            "Allowed Mode",
                            "Allowed network access mode",
                            MM_MODEM_GSM_ALLOWED_MODE_ANY,
                            MM_MODEM_GSM_ALLOWED_MODE_LAST,
                            MM_MODEM_GSM_ALLOWED_MODE_ANY,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_GSM_NETWORK_ACCESS_TECHNOLOGY,
                            "Access Technology",
                            "Current access technology in use when connected to "
                            "a mobile network.",
                            MM_MODEM_GSM_ACCESS_TECH_UNKNOWN,
                            MM_MODEM_GSM_ACCESS_TECH_LAST,
                            MM_MODEM_GSM_ACCESS_TECH_UNKNOWN,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    /* Signals */
    signals[SIGNAL_QUALITY] =
        g_signal_new ("signal-quality",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemGsmNetwork, signal_quality),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);

    signals[REGISTRATION_INFO] =
        g_signal_new ("registration-info",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMModemGsmNetwork, registration_info),
                      NULL, NULL,
                      mm_marshal_VOID__UINT_STRING_STRING,
                      G_TYPE_NONE, 3,
                      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

    signals[NETWORK_MODE] =
        g_signal_new ("network-mode",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      0, NULL, NULL,
                      g_cclosure_marshal_VOID__UINT,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);

    initialized = TRUE;
}

GType
mm_modem_gsm_network_get_type (void)
{
    static GType network_type = 0;

    if (!G_UNLIKELY (network_type)) {
        const GTypeInfo network_info = {
            sizeof (MMModemGsmNetwork), /* class_size */
            mm_modem_gsm_network_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        network_type = g_type_register_static (G_TYPE_INTERFACE,
                                               "MMModemGsmNetwork",
                                               &network_info, 0);

        g_type_interface_add_prerequisite (network_type, G_TYPE_OBJECT);
        g_type_interface_add_prerequisite (network_type, MM_TYPE_MODEM);
        dbus_g_object_type_install_info (network_type, &dbus_glib_mm_modem_gsm_network_object_info);
    }

    return network_type;
}
