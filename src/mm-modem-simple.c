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
 * Copyright (C) 2009 Novell, Inc.
 * Copyright (C) 2009 Red Hat, Inc.
 */

#include <dbus/dbus-glib.h>

#include "mm-modem-simple.h"
#include "mm-errors.h"
#include "mm-callback-info.h"

static void impl_modem_simple_connect (MMModemSimple *self, GHashTable *properties, DBusGMethodInvocation *context);
static void impl_modem_simple_get_status (MMModemSimple *self, DBusGMethodInvocation *context);

#include "mm-modem-simple-glue.h"

void
mm_modem_simple_connect (MMModemSimple *self,
                         GHashTable *properties,
                         MMModemFn callback,
                         gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE (self));
    g_return_if_fail (properties != NULL);

    if (MM_MODEM_SIMPLE_GET_INTERFACE (self)->connect)
        MM_MODEM_SIMPLE_GET_INTERFACE (self)->connect (self, properties, callback, user_data);
    else {
        MMCallbackInfo *info;

        info = mm_callback_info_new (MM_MODEM (self), callback, user_data);
        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                           "Operation not supported");
        mm_callback_info_schedule (info);
    }
}

static void
simple_get_status_invoke (MMCallbackInfo *info)
{
    MMModemSimpleGetStatusFn callback = (MMModemSimpleGetStatusFn) info->callback;

    callback (MM_MODEM_SIMPLE (info->modem), NULL, info->error, info->user_data);
}

void
mm_modem_simple_get_status (MMModemSimple *self,
                            MMModemSimpleGetStatusFn callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_SIMPLE (self));

    if (MM_MODEM_SIMPLE_GET_INTERFACE (self)->get_status)
        MM_MODEM_SIMPLE_GET_INTERFACE (self)->get_status (self, callback, user_data);
    else {
        MMCallbackInfo *info;

        info = mm_callback_info_new_full (MM_MODEM (self),
                                          simple_get_status_invoke,
                                          G_CALLBACK (callback),
                                          user_data);

        info->error = g_error_new_literal (MM_MODEM_ERROR, MM_MODEM_ERROR_OPERATION_NOT_SUPPORTED,
                                           "Operation not supported");
        mm_callback_info_schedule (info);
    }
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
impl_modem_simple_connect (MMModemSimple *self,
                           GHashTable *properties,
                           DBusGMethodInvocation *context)
{
    mm_modem_simple_connect (self, properties, async_call_done, context);
}

static void
get_status_done (MMModemSimple *modem,
                 GHashTable *properties,
                 GError *error,
                 gpointer user_data)
{
    DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

    if (error)
        dbus_g_method_return_error (context, error);
    else
        dbus_g_method_return (context, properties);
}

static void
impl_modem_simple_get_status (MMModemSimple *self,
                              DBusGMethodInvocation *context)
{
    mm_modem_simple_get_status (self, get_status_done, context);
}

/*****************************************************************************/

static void
mm_modem_simple_init (gpointer g_iface)
{
}

GType
mm_modem_simple_get_type (void)
{
    static GType modem_type = 0;

    if (!G_UNLIKELY (modem_type)) {
        const GTypeInfo modem_info = {
            sizeof (MMModemSimple), /* class_size */
            mm_modem_simple_init,   /* base_init */
            NULL,       /* base_finalize */
            NULL,
            NULL,       /* class_finalize */
            NULL,       /* class_data */
            0,
            0,              /* n_preallocs */
            NULL
        };

        modem_type = g_type_register_static (G_TYPE_INTERFACE,
                                             "MMModemSimple",
                                             &modem_info, 0);

        g_type_interface_add_prerequisite (modem_type, G_TYPE_OBJECT);
        dbus_g_object_type_install_info (modem_type, &dbus_glib_mm_modem_simple_object_info);
    }

    return modem_type;
}
