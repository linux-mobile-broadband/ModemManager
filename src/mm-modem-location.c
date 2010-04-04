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
 * Copyright (C) 2010 Red Hat, Inc.
 */

#include <string.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-location.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-marshal.h"

static void impl_modem_location_enable (MMModemLocation *modem,
                                        gboolean enable,
                                        gboolean signal_location,
                                        DBusGMethodInvocation *context);

static void impl_modem_location_get_location (MMModemLocation *modem,
                                              DBusGMethodInvocation *context);

#include "mm-modem-location-glue.h"

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
async_call_not_supported (MMModemLocation *self,
                          MMModemFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");
    mm_callback_info_schedule (info);
}

/*****************************************************************************/

typedef struct {
    gboolean enable;
    gboolean signals_location;
} LocAuthInfo;

static void
loc_auth_info_destroy (gpointer data)
{
    LocAuthInfo *info = data;

    memset (info, 0, sizeof (LocAuthInfo));
    g_free (info);
}

static LocAuthInfo *
loc_auth_info_new (gboolean enable, gboolean signals_location)
{
    LocAuthInfo *info;

    info = g_malloc0 (sizeof (LocAuthInfo));
    info->enable = enable;
    info->signals_location = signals_location;
    return info;
}

/*****************************************************************************/

void
mm_modem_location_enable (MMModemLocation *self,
                          gboolean enable,
                          gboolean signals_location,
                          MMModemFn callback,
                          gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_LOCATION (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_LOCATION_GET_INTERFACE (self)->enable)
        MM_MODEM_LOCATION_GET_INTERFACE (self)->enable (self, enable, signals_location, callback, user_data);
    else
        async_call_not_supported (self, callback, user_data);
}

/*****************************************************************************/

static void
loc_enable_auth_cb (MMAuthRequest *req,
                    GObject *owner,
                    DBusGMethodInvocation *context,
                    gpointer user_data)
{
    MMModemLocation *self = MM_MODEM_LOCATION (owner);
    LocAuthInfo *info = (LocAuthInfo *) user_data;
    GError *error = NULL;

    /* Return any authorization error, otherwise enable location gathering */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_location_enable (self, info->enable, info->signals_location, async_call_done, context);
}

static void
impl_modem_location_enable (MMModemLocation *modem,
                            gboolean enable,
                            gboolean signals_location,
                            DBusGMethodInvocation *context)
{
    GError *error = NULL;
    LocAuthInfo *info;

    info = loc_auth_info_new (enable, signals_location);

    /* Make sure the caller is authorized to enable location gathering */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_LOCATION,
                                context,
                                loc_enable_auth_cb,
                                info,
                                loc_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
loc_get_invoke (MMCallbackInfo *info)
{
    MMModemLocationGetFn callback = (MMModemLocationGetFn) info->callback;

    callback ((MMModemLocation *) info->modem, NULL, info->error, info->user_data);
}

static void
async_get_call_not_supported (MMModemLocation *self,
                              MMModemLocationGetFn callback,
                              gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (self),
                                      loc_get_invoke,
                                      G_CALLBACK (callback),
                                      user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");
    mm_callback_info_schedule (info);
}

void
mm_modem_location_get_location (MMModemLocation *self,
                                MMModemLocationGetFn callback,
                                gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_LOCATION (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_LOCATION_GET_INTERFACE (self)->get_location)
        MM_MODEM_LOCATION_GET_INTERFACE (self)->get_location (self, callback, user_data);
    else
        async_get_call_not_supported (self, callback, user_data);
}

/*****************************************************************************/

static void
async_get_call_done (MMModemLocation *self,
                     GHashTable *locations,
                     GError *error,
                     gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, locations);
}

static void
loc_get_auth_cb (MMAuthRequest *req,
                 GObject *owner,
                 DBusGMethodInvocation *context,
                 gpointer user_data)
{
    MMModemLocation *self = MM_MODEM_LOCATION (owner);
    GError *error = NULL;

    /* Return any authorization error, otherwise get the location */
    if (!mm_modem_auth_finish (MM_MODEM (self), req, &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    } else
        mm_modem_location_get_location (self, async_get_call_done, context);
}

static void
impl_modem_location_get_location (MMModemLocation *modem,
                                  DBusGMethodInvocation *context)
{
    GError *error = NULL;
    LocAuthInfo *info;

    info = loc_auth_info_new (FALSE, FALSE);

    /* Make sure the caller is authorized to enable location gathering */
    if (!mm_modem_auth_request (MM_MODEM (modem),
                                MM_AUTHORIZATION_LOCATION,
                                context,
                                loc_get_auth_cb,
                                info,
                                loc_auth_info_destroy,
                                &error)) {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
}

/*****************************************************************************/

static void
mm_modem_location_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_boxed (MM_MODEM_LOCATION_LOCATION,
                             "Location",
                             "Available location information",
                             G_TYPE_HASH_TABLE,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_uint (MM_MODEM_LOCATION_CAPABILITIES,
                            "Capabilities",
                            "Supported location information methods",
                            MM_MODEM_LOCATION_CAPABILITY_UNKNOWN,
                              MM_MODEM_LOCATION_CAPABILITY_GPS_NMEA
                            | MM_MODEM_LOCATION_CAPABILITY_GSM_LAC_CI,
                            MM_MODEM_LOCATION_CAPABILITY_UNKNOWN,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_MODEM_LOCATION_ENABLED,
                               "Enabled",
                               "Whether or not location gathering is enabled",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_MODEM_LOCATION_SIGNALS_LOCATION,
                               "SignalsLocation",
                               "Whether or not location updates are emitted as signals",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    initialized = TRUE;
}

GType
mm_modem_location_get_type (void)
{
    static GType loc_type = 0;

    if (!G_UNLIKELY (loc_type)) {
        const GTypeInfo loc_info = {
            sizeof (MMModemLocation), /* class_size */
            mm_modem_location_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        loc_type = g_type_register_static (G_TYPE_INTERFACE,
                                           "MMModemLocation",
                                           &loc_info, 0);

        g_type_interface_add_prerequisite (loc_type, G_TYPE_OBJECT);
        dbus_g_object_type_install_info (loc_type, &dbus_glib_mm_modem_location_object_info);

        /* Register some shadow properties to handle Enabled and Capabilities
         * since these could be used by other interfaces.
         */
        dbus_g_object_type_register_shadow_property (loc_type,
                                                     "Enabled",
                                                     MM_MODEM_LOCATION_ENABLED);
        dbus_g_object_type_register_shadow_property (loc_type,
                                                     "Capabilities",
                                                     MM_MODEM_LOCATION_CAPABILITIES);
    }

    return loc_type;
}
