/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Google, Inc.
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-modem-time.h"

/**
 * mm_modem_time_get_path:
 * @self: A #MMModemTime.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_time_get_path (MMModemTime *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_time_dup_path:
 * @self: A #MMModemTime.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_time_dup_path (MMModemTime *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

gchar *
mm_modem_time_get_network_time_finish (MMModemTime *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    gchar *network_time = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_TIME (self), NULL);

    if (!mm_gdbus_modem_time_call_get_network_time_finish (self, &network_time, res, error))
        return NULL;

    return network_time;
}

void
mm_modem_time_get_network_time (MMModemTime *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM_TIME (self));

    mm_gdbus_modem_time_call_get_network_time (self,
                                               cancellable,
                                               callback,
                                               user_data);
}

gchar *
mm_modem_time_get_network_time_sync (MMModemTime *self,
                                     GCancellable *cancellable,
                                     GError **error)
{
    gchar *network_time = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_TIME (self), NULL);

    if (!mm_gdbus_modem_time_call_get_network_time_sync (self, &network_time, cancellable, error))
        return NULL;

    return network_time;
}

MMNetworkTimezone *
mm_modem_time_get_network_timezone (MMModemTime *self)
{
    GVariant *dictionary;
    MMNetworkTimezone *tz;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_TIME (self), NULL);

    dictionary = mm_gdbus_modem_time_dup_network_timezone (self);
    tz = mm_network_timezone_new_from_dictionary (dictionary, NULL);
    g_variant_unref (dictionary);

    return tz;
}
