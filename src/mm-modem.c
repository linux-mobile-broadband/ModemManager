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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#include <config.h>
#include <string.h>
#include <dbus/dbus-glib.h>
#include "mm-modem.h"
#include "mm-log.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-marshal.h"

static void impl_modem_enable (MMModem *modem, gboolean enable, DBusGMethodInvocation *context);
static void impl_modem_connect (MMModem *modem, const char *number, DBusGMethodInvocation *context);
static void impl_modem_disconnect (MMModem *modem, DBusGMethodInvocation *context);
static void impl_modem_get_ip4_config (MMModem *modem, DBusGMethodInvocation *context);
static void impl_modem_get_info (MMModem *modem, DBusGMethodInvocation *context);
static void impl_modem_reset (MMModem *modem, DBusGMethodInvocation *context);
static void impl_modem_factory_reset (MMModem *modem, const char *code, DBusGMethodInvocation *context);

#include "mm-modem-glue.h"

static void
async_op_not_supported (MMModem *self,
                        MMModemFn callback,
                        gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (self, callback, user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");
    mm_callback_info_schedule (info);
}

static void
async_call_done (MMModem *modem, GError *error, gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context);
}

void
mm_modem_enable (MMModem *self,
                 MMModemFn callback,
                 gpointer user_data)
{
    MMModemState state;

    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    state = mm_modem_get_state (self);
    if (state >= MM_MODEM_STATE_ENABLED) {
        MMCallbackInfo *info;

        info = mm_callback_info_new (self, callback, user_data);

        if (state == MM_MODEM_STATE_ENABLING) {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_OPERATION_IN_PROGRESS,
                                               "The device is already being enabled.");
        } else {
            /* Already enabled */
        }

        mm_callback_info_schedule (info);
        return;
    }

    if (MM_MODEM_GET_INTERFACE (self)->enable)
        MM_MODEM_GET_INTERFACE (self)->enable (self, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
finish_disable (MMModem *self,
                MMModemFn callback,
                gpointer user_data)
{
    if (MM_MODEM_GET_INTERFACE (self)->disable)
        MM_MODEM_GET_INTERFACE (self)->disable (self, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

typedef struct {
    MMModemFn callback;
    gpointer user_data;
} DisableDisconnectInfo;

static void
disable_disconnect_done (MMModem *self,
                         GError *error,
                         gpointer user_data)
{
    DisableDisconnectInfo *cb_data = user_data;
    GError *tmp_error = NULL;

    /* Check for modem removal */
    if (g_error_matches (error, MM_MODEM_ERROR, MM_MODEM_ERROR_REMOVED))
        tmp_error = g_error_copy (error);
    else if (!self) {
        tmp_error = g_error_new_literal (MM_MODEM_ERROR,
                                         MM_MODEM_ERROR_REMOVED,
                                         "The modem was removed.");
    }

    /* And send an immediate error reply if the modem was removed */
    if (tmp_error) {
        cb_data->callback (NULL, tmp_error, cb_data->user_data);
        g_free (cb_data);
        g_error_free (tmp_error);
        return;
    }

    if (error) {
        char *device = mm_modem_get_device (self);

        /* Don't really care what the error was; log it and proceed to disable */
        g_warning ("%s: (%s): error disconnecting the modem while disabling: (%d) %s",
                   __func__,
                   device,
                   error ? error->code : -1,
                   error && error->message ? error->message : "(unknown)");
        g_free (device);
    }
    finish_disable (self, cb_data->callback, cb_data->user_data);
    g_free (cb_data);
}

void
mm_modem_disable (MMModem *self,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMModemState state;

    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    state = mm_modem_get_state (self);
    if (state <= MM_MODEM_STATE_DISABLING) {
        MMCallbackInfo *info;

        info = mm_callback_info_new (self, callback, user_data);

        if (state == MM_MODEM_STATE_DISABLING) {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_OPERATION_IN_PROGRESS,
                                               "The device is already being disabled.");
        } else {
            /* Already disabled */
        }

        mm_callback_info_schedule (info);
        return;
    }

    /* If the modem is connected, disconnect it */
    if (state >= MM_MODEM_STATE_CONNECTING) {
        DisableDisconnectInfo *cb_data;

        cb_data = g_malloc0 (sizeof (DisableDisconnectInfo));
        cb_data->callback = callback;
        cb_data->user_data = user_data;
        mm_modem_disconnect (self, disable_disconnect_done, cb_data);
    } else
        finish_disable (self, callback, user_data);
}

static void
impl_modem_enable (MMModem *modem,
                   gboolean enable,
                   DBusGMethodInvocation *context)
{
    if (enable)
        mm_modem_enable (modem, async_call_done, context);
    else
        mm_modem_disable (modem, async_call_done, context);
}

void
mm_modem_connect (MMModem *self,
                  const char *number,
                  MMModemFn callback,
                  gpointer user_data)
{
    MMModemState state;

    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);
    g_return_if_fail (number != NULL);

    state = mm_modem_get_state (self);
    if (state >= MM_MODEM_STATE_CONNECTING) {
        MMCallbackInfo *info;

        /* Already connecting */
        info = mm_callback_info_new (self, callback, user_data);
        if (state == MM_MODEM_STATE_CONNECTING) {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_OPERATION_IN_PROGRESS,
                                               "The device is already being connected.");
        } else {
            /* already connected */
        }

        mm_callback_info_schedule (info);
        return;
    }

    if (MM_MODEM_GET_INTERFACE (self)->connect)
        MM_MODEM_GET_INTERFACE (self)->connect (self, number, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_connect (MMModem *modem,
                    const char *number,
                    DBusGMethodInvocation *context)
{
    mm_modem_connect (modem, number, async_call_done, context);
}

static void
get_ip4_invoke (MMCallbackInfo *info)
{
    MMModemIp4Fn callback = (MMModemIp4Fn) info->callback;

    callback (info->modem,
              GPOINTER_TO_UINT (mm_callback_info_get_data (info, "ip4-address")),
              (GArray *) mm_callback_info_get_data (info, "ip4-dns"),
              info->error, info->user_data);
}

void
mm_modem_get_ip4_config (MMModem *self,
                         MMModemIp4Fn callback,
                         gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->get_ip4_config)
        MM_MODEM_GET_INTERFACE (self)->get_ip4_config (self, callback, user_data);
    else {
        MMCallbackInfo *info;

        info = mm_callback_info_new_full (self,
                                          get_ip4_invoke,
                                          G_CALLBACK (callback),
                                          user_data);

        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                           "Operation not supported");
        mm_callback_info_schedule (info);
    }
}

static void
value_array_add_uint (GValueArray *array, guint32 i)
{
    GValue value = { 0, };

    g_value_init (&value, G_TYPE_UINT);
    g_value_set_uint (&value, i);
    g_value_array_append (array, &value);
    g_value_unset (&value);
}

static void
get_ip4_done (MMModem *modem,
              guint32 address,
              GArray *dns,
              GError *error,
              gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else {
        GValueArray *array;
        guint32 dns1 = 0;
        guint32 dns2 = 0;
        guint32 dns3 = 0;

        array = g_value_array_new (4);

        if (dns) {
            if (dns->len > 0)

                dns1 = g_array_index (dns, guint32, 0);
            if (dns->len > 1)
                dns2 = g_array_index (dns, guint32, 1);
            if (dns->len > 2)
                dns3 = g_array_index (dns, guint32, 2);
        }

        value_array_add_uint (array, address);
        value_array_add_uint (array, dns1);
        value_array_add_uint (array, dns2);
        value_array_add_uint (array, dns3);

        dbus_g_method_return (context, array);
    }
}

static void
impl_modem_get_ip4_config (MMModem *modem,
                           DBusGMethodInvocation *context)
{
    mm_modem_get_ip4_config (modem, get_ip4_done, context);
}

void
mm_modem_disconnect (MMModem *self,
                     MMModemFn callback,
                     gpointer user_data)
{
    MMModemState state;

    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    state = mm_modem_get_state (self);
    if (state <= MM_MODEM_STATE_DISCONNECTING) {
        MMCallbackInfo *info;

        /* Already connecting */
        info = mm_callback_info_new (self, callback, user_data);
        if (state == MM_MODEM_STATE_DISCONNECTING) {
            info->error = g_error_new_literal (MM_MODEM_ERROR,
                                               MM_MODEM_ERROR_OPERATION_IN_PROGRESS,
                                               "The device is already being disconnected.");
        } else {
            /* already disconnected */
        }

        mm_callback_info_schedule (info);
        return;
    }

    if (MM_MODEM_GET_INTERFACE (self)->disconnect)
        MM_MODEM_GET_INTERFACE (self)->disconnect (self, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

static void
impl_modem_disconnect (MMModem *modem,
                       DBusGMethodInvocation *context)
{
    mm_modem_disconnect (modem, async_call_done, context);
}

static void
info_call_done (MMModem *self,
                const char *manufacturer,
                const char *model,
                const char *version,
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

        /* Manufacturer */
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, manufacturer);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        /* Model */
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, model);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        /* Version */
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, version);
        g_value_array_append (array, &value);
        g_value_unset (&value);

        dbus_g_method_return (context, array);
    }
}

static void
info_invoke (MMCallbackInfo *info)
{
    MMModemInfoFn callback = (MMModemInfoFn) info->callback;

    callback (info->modem, NULL, NULL, NULL, info->error, info->user_data);
}

static void
info_call_not_supported (MMModem *self,
                         MMModemInfoFn callback,
                         gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (self), info_invoke, G_CALLBACK (callback), user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");

    mm_callback_info_schedule (info);
}

void
mm_modem_get_info (MMModem *self,
                   MMModemInfoFn callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->get_info)
        MM_MODEM_GET_INTERFACE (self)->get_info (self, callback, user_data);
    else
        info_call_not_supported (self, callback, user_data);
}

static void
impl_modem_get_info (MMModem *modem,
                     DBusGMethodInvocation *context)
{
    mm_modem_get_info (modem, info_call_done, context);
}

/*****************************************************************************/

static void
reset_auth_cb (MMAuthRequest *req,
               GObject *owner,
               DBusGMethodInvocation *context,
               gpointer user_data)
{
    MMModem *self = MM_MODEM (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise try to reset the modem */
    if (!mm_modem_auth_finish (self, req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_reset (self, async_call_done, context);
}

static void
impl_modem_reset (MMModem *modem, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to reset the device */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_CONTROL,
                                context,
                                reset_auth_cb,
                                NULL, NULL,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

void
mm_modem_reset (MMModem *self,
                MMModemFn callback,
                gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->reset)
        MM_MODEM_GET_INTERFACE (self)->reset (self, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

/*****************************************************************************/

static void
factory_reset_auth_cb (MMAuthRequest *req,
                       GObject *owner,
                       DBusGMethodInvocation *context,
                       gpointer user_data)
{
    MMModem *self = MM_MODEM (owner);
    const char *code = user_data;
    GError *error = NULL;

    /* Return any authorization error, otherwise try to reset the modem */
    if (!mm_modem_auth_finish (self, req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_factory_reset (self, code, async_call_done, context);
}

static void
impl_modem_factory_reset (MMModem *modem,
                          const char *code,
                          DBusGMethodInvocation *context)
{
    GError *error = NULL;

    /* Make sure the caller is authorized to reset the device */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_DEVICE_CONTROL,
                                context,
                                factory_reset_auth_cb,
                                g_strdup (code),
                                g_free,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

void
mm_modem_factory_reset (MMModem *self,
                        const char *code,
                        MMModemFn callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);
    g_return_if_fail (code != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->factory_reset)
        MM_MODEM_GET_INTERFACE (self)->factory_reset (self, code, callback, user_data);
    else
        async_op_not_supported (self, callback, user_data);
}

/*****************************************************************************/

void
mm_modem_get_supported_charsets (MMModem *self,
                                 MMModemUIntFn callback,
                                 gpointer user_data)
{
    MMCallbackInfo *info;

    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->get_supported_charsets)
        MM_MODEM_GET_INTERFACE (self)->get_supported_charsets (self, callback, user_data);
    else {
        info = mm_callback_info_uint_new (MM_MODEM (self), callback, user_data);
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                           "Operation not supported");
        mm_callback_info_schedule (info);
    }
}

void
mm_modem_set_charset (MMModem *self,
                      MMModemCharset charset,
                      MMModemFn callback,
                      gpointer user_data)
{
    MMCallbackInfo *info;

    g_return_if_fail (charset != MM_MODEM_CHARSET_UNKNOWN);
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_GET_INTERFACE (self)->set_charset)
        MM_MODEM_GET_INTERFACE (self)->set_charset (self, charset, callback, user_data);
    else {
        info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                           "Operation not supported");
        mm_callback_info_schedule (info);
    }
}

/*****************************************************************************/

gboolean
mm_modem_owns_port (MMModem *self,
                    const char *subsys,
                    const char *name)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (subsys, FALSE);
    g_return_val_if_fail (name, FALSE);

    g_assert (MM_MODEM_GET_INTERFACE (self)->owns_port);
    return MM_MODEM_GET_INTERFACE (self)->owns_port (self, subsys, name);
}

gboolean
mm_modem_grab_port (MMModem *self,
                    const char *subsys,
                    const char *name,
                    MMPortType suggested_type,
                    gpointer user_data,
                    GError **error)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (subsys, FALSE);
    g_return_val_if_fail (name, FALSE);

    g_assert (MM_MODEM_GET_INTERFACE (self)->grab_port);
    return MM_MODEM_GET_INTERFACE (self)->grab_port (self, subsys, name, suggested_type, user_data, error);
}

void
mm_modem_release_port (MMModem *self,
                       const char *subsys,
                       const char *name)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (MM_IS_MODEM (self));
    g_return_if_fail (subsys);
    g_return_if_fail (name);

    g_assert (MM_MODEM_GET_INTERFACE (self)->release_port);
    MM_MODEM_GET_INTERFACE (self)->release_port (self, subsys, name);
}

gboolean
mm_modem_get_valid (MMModem *self)
{
    gboolean valid = FALSE;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);

    g_object_get (G_OBJECT (self), MM_MODEM_VALID, &valid, NULL);
    return valid;
}

char *
mm_modem_get_device (MMModem *self)
{
    char *device;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (MM_IS_MODEM (self), NULL);

    g_object_get (G_OBJECT (self), MM_MODEM_MASTER_DEVICE, &device, NULL);
    return device;
}

MMModemState
mm_modem_get_state (MMModem *self)
{
    MMModemState state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (G_OBJECT (self), MM_MODEM_STATE, &state, NULL);
    return state;
}

static const char *
state_to_string (MMModemState state)
{
    switch (state) {
    case MM_MODEM_STATE_UNKNOWN:
        return "unknown";
    case MM_MODEM_STATE_DISABLED:
        return "disabled";
    case MM_MODEM_STATE_DISABLING:
        return "disabling";
    case MM_MODEM_STATE_ENABLING:
        return "enabling";
    case MM_MODEM_STATE_ENABLED:
        return "enabled";
    case MM_MODEM_STATE_SEARCHING:
        return "searching";
    case MM_MODEM_STATE_REGISTERED:
        return "registered";
    case MM_MODEM_STATE_DISCONNECTING:
        return "disconnecting";
    case MM_MODEM_STATE_CONNECTING:
        return "connecting";
    case MM_MODEM_STATE_CONNECTED:
        return "connected";
    default:
        g_assert_not_reached ();
        break;
    }

    g_assert_not_reached ();
    return "(invalid)";
}

void
mm_modem_set_state (MMModem *self,
                    MMModemState new_state,
                    MMModemStateReason reason)
{
    MMModemState old_state = MM_MODEM_STATE_UNKNOWN;
    const char *dbus_path;

    g_object_get (G_OBJECT (self), MM_MODEM_STATE, &old_state, NULL);

    if (new_state != old_state) {
        g_object_set (G_OBJECT (self), MM_MODEM_STATE, new_state, NULL);
        g_signal_emit_by_name (G_OBJECT (self), "state-changed", old_state, new_state, reason);

        dbus_path = (const char *) g_object_get_data (G_OBJECT (self), DBUS_PATH_TAG);
        if (dbus_path) {
            mm_info ("Modem %s: state changed (%s -> %s)",
                     dbus_path,
                     state_to_string (old_state),
                     state_to_string (new_state));
        }
    }
}

/*****************************************************************************/

gboolean
mm_modem_auth_request (MMModem *self,
                       const char *authorization,
                       DBusGMethodInvocation *context,
                       MMAuthRequestCb callback,
                       gpointer callback_data,
                       GDestroyNotify notify,
                       GError **error)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (authorization != NULL, FALSE);
    g_return_val_if_fail (context != NULL, FALSE);
    g_return_val_if_fail (callback != NULL, FALSE);

    g_return_val_if_fail (MM_MODEM_GET_INTERFACE (self)->auth_request, FALSE);
    return MM_MODEM_GET_INTERFACE (self)->auth_request (self,
                                                        authorization,
                                                        context,
                                                        callback,
                                                        callback_data,
                                                        notify,
                                                        error);
}

gboolean
mm_modem_auth_finish (MMModem *self,
                      MMAuthRequest *req,
                      GError **error)
{
    gboolean success;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (MM_IS_MODEM (self), FALSE);
    g_return_val_if_fail (req != NULL, FALSE);

    g_return_val_if_fail (MM_MODEM_GET_INTERFACE (self)->auth_finish, FALSE);
    success = MM_MODEM_GET_INTERFACE (self)->auth_finish (self, req, error);

    /* If the request failed, the implementor *should* return an error */
    if (!success && error)
        g_warn_if_fail (*error != NULL);

    return success;
}

/*****************************************************************************/

static void
mm_modem_init (gpointer g_iface)
{
    GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_DATA_DEVICE,
                              "Device",
                              "Data device",
                              NULL,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_MASTER_DEVICE,
                              "MasterDevice",
                              "Master modem parent device of all the modem's ports",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_DRIVER,
                              "Driver",
                              "Driver",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_PLUGIN,
                              "Plugin",
                              "Plugin name",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_TYPE,
                            "Type",
                            "Type",
                            0, G_MAXUINT32, 0,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_IP_METHOD,
                            "IP method",
                            "IP configuration method",
                            MM_MODEM_IP_METHOD_PPP,
                            MM_MODEM_IP_METHOD_DHCP,
                            MM_MODEM_IP_METHOD_PPP,
                            G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_MODEM_VALID,
                               "Valid",
                               "Modem is valid",
                               FALSE,
                               G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_STATE,
                            "State",
                            "State",
                            MM_MODEM_STATE_UNKNOWN,
                            MM_MODEM_STATE_LAST,
                            MM_MODEM_STATE_UNKNOWN,
                            G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_MODEM_ENABLED,
                               "Enabled",
                               "Modem is enabled",
                               FALSE,
                               G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_EQUIPMENT_IDENTIFIER,
                               "EquipmentIdentifier",
                               "The equipment identifier of the device",
                               NULL,
                               G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_DEVICE_IDENTIFIER,
                               "DeviceIdentifier",
                               "A best-effort identifer of the device",
                               NULL,
                               G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_string (MM_MODEM_UNLOCK_REQUIRED,
                               "UnlockRequired",
                               "Whether or not the modem requires an unlock "
                               "code to become usable, and if so, which unlock code is required",
                               NULL,
                               G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_UNLOCK_RETRIES,
                               "UnlockRetries",
                               "The remaining number of unlock attempts",
                               0, G_MAXUINT32, 0,
                               G_PARAM_READABLE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_HW_VID,
                            "Hardware vendor ID",
                            "Hardware vendor ID",
                            0, G_MAXUINT, 0,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_HW_PID,
                            "Hardware product ID",
                            "Hardware product ID",
                            0, G_MAXUINT, 0,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    /* Signals */
    g_signal_new ("state-changed",
                  iface_type,
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (MMModem, state_changed),
                  NULL, NULL,
                  mm_marshal_VOID__UINT_UINT_UINT,
                  G_TYPE_NONE, 3,
                  G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

    initialized = TRUE;
}

GType
mm_modem_get_type (void)
{
    static GType modem_type = 0;

    if (!G_UNLIKELY (modem_type)) {
        const GTypeInfo modem_info = {
            sizeof (MMModem), /* class_size */
            mm_modem_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        modem_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "MMModem",
                                             &modem_info, 0);

        g_type_interface_add_prerequisite (modem_type, G_TYPE_OBJECT);

        dbus_g_object_type_install_info (modem_type, &dbus_glib_mm_modem_object_info);
    }

    return modem_type;
}
