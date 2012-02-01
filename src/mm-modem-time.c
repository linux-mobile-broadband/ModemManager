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
 * Copyright (C) 2011 The Chromium OS Authors
 */

#include <string.h>
#include <dbus/dbus-glib.h>

#include "mm-modem-time.h"
#include "mm-errors.h"
#include "mm-callback-info.h"
#include "mm-marshal.h"

static void impl_modem_time_get_network_time (MMModemTime *self,
                                              DBusGMethodInvocation *context);

#include "mm-modem-time-glue.h"

enum {
    NETWORK_TIME_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
async_get_call_done (MMModemTime *self,
                     const char *network_time,
                     GError *error,
                     gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, network_time);
}

static void
time_get_invoke (MMCallbackInfo *info)
{
    MMModemTimeGetNetworkTimeFn callback = (MMModemTimeGetNetworkTimeFn) info->callback;

    callback ((MMModemTime *) info->modem, NULL, info->error, info->user_data);
}

static void
async_get_call_not_supported (MMModemTime *self,
                          MMModemTimeGetNetworkTimeFn callback,
                          gpointer user_data)
{
    MMCallbackInfo *info;

    info = mm_callback_info_new_full (MM_MODEM (self),
                                      time_get_invoke,
                                      G_CALLBACK (callback),
                                      user_data);
    info->error = g_error_new_literal (MM_MODEM_ERROR,
                                       MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                       "Operation not supported");

    mm_callback_info_schedule (info);
}

void
mm_modem_time_get_network_time (MMModemTime *self,
                                MMModemTimeGetNetworkTimeFn callback,
                                gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_TIME (self));
    g_return_if_fail (callback != NULL);

    if (MM_MODEM_TIME_GET_INTERFACE (self)->get_network_time)
        MM_MODEM_TIME_GET_INTERFACE (self)->get_network_time (self, callback, user_data);
    else
        async_get_call_not_supported (self, callback, user_data);
}

gboolean
mm_modem_time_poll_network_timezone (MMModemTime *self,
                                     MMModemFn callback,
                                     gpointer user_data)
{
    g_return_val_if_fail (MM_IS_MODEM_TIME (self), FALSE);
    g_return_val_if_fail (callback != NULL, FALSE);

    if (MM_MODEM_TIME_GET_INTERFACE (self)->poll_network_timezone)
        return MM_MODEM_TIME_GET_INTERFACE (self)->poll_network_timezone
                (self, callback, user_data);
    else
        return FALSE;
}

static void
impl_modem_time_get_network_time (MMModemTime *self,
                                  DBusGMethodInvocation *context)
{
    mm_modem_time_get_network_time (self, async_get_call_done, context);
}

#define DBUS_TYPE_G_MAP_OF_VARIANT (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))

static void
mm_modem_time_init (gpointer g_iface)
{
    GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);
    static gboolean initialized = FALSE;

    if (initialized)
        return;
    initialized = TRUE;

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boxed (MM_MODEM_TIME_NETWORK_TIMEZONE,
                             "NetworkTimezone",
                             "The network timezone data",
                             DBUS_TYPE_G_MAP_OF_VARIANT,
                             G_PARAM_READABLE));

    signals[NETWORK_TIME_CHANGED] =
        g_signal_new ("network-time-changed",
                      iface_type,
                      G_SIGNAL_RUN_FIRST,
                      0, NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

GType
mm_modem_time_get_type (void)
{
    static GType time_type = 0;

    if (!G_UNLIKELY (time_type)) {
        const GTypeInfo time_info = {
            sizeof(MMModemTime), /* class_size */
            mm_modem_time_init, /* base_init */
            NULL, /* base_finalize */
            NULL,
            NULL, /* class_finalize */
            NULL, /* class_data */
            0,
            0, /* n_preallocs */
            NULL
        };

        time_type = g_type_register_static (G_TYPE_INTERFACE,
                                            "MMModemTime",
                                            &time_info, 0);

        g_type_interface_add_prerequisite (time_type, G_TYPE_OBJECT);
        dbus_g_object_type_install_info (time_type, &dbus_glib_mm_modem_time_object_info);
    }

    return time_type;
}
