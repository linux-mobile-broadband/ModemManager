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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include "mm-object.h"

/**
 * mm_object_get_path:
 * @self: A #MMObject.
 *
 * Gets the DBus path of the #MMObject object.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_object_get_path (MMObject *self)
{
    g_return_val_if_fail (G_IS_DBUS_OBJECT (self), NULL);

    return g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
}

/**
 * mm_object_dup_path:
 * @self: A #MMObject.
 *
 * Gets a copy of the DBus path of the #MMObject object.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_object_dup_path (MMObject *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_OBJECT_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    return value;
}

/**
 * mm_object_get_modem:
 * @object: A #MMModem
 *
 * Gets the #MMModem instance for the D-Bus interface <link linkend="gdbus-interface-org-freedesktop-ModemManager1-Modem.top_of_page">org.freedesktop.ModemManager1.Modem</link> on @object, if any.
 *
 * Returns: (transfer full): A #MMModem that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
MMModem *
mm_object_get_modem (MMObject *object)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (object), NULL);

    return mm_gdbus_object_get_modem (object);
}

/**
 * mm_object_get_modem_3gpp:
 * @object: A #MMObject.
 *
 * Gets the #MMModem3gpp instance for the D-Bus interface <link linkend="gdbus-interface-org-freedesktop-ModemManager1-Modem-Modem3gpp.top_of_page">org.freedesktop.ModemManager1.Modem.Modem3gpp</link> on @object, if any.
 *
 * Returns: (transfer full): A #MMModem3gpp that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
MMModem3gpp *
mm_object_get_modem_3gpp (MMObject *object)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (object), NULL);

    return mm_gdbus_object_get_modem3gpp (object);
}

/**
 * mm_object_get_modem_simple:
 * @object: A #MMObject.
 *
 * Gets the #MMModemSimple instance for the D-Bus interface <link linkend="gdbus-interface-org-freedesktop-ModemManager1-Modem-Modemsimple.top_of_page">org.freedesktop.ModemManager1.Modem.Modemsimple</link> on @object, if any.
 *
 * Returns: (transfer full): A #MMModemSimple that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
MMModemSimple *
mm_object_get_modem_simple (MMObject *object)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (object), NULL);

    return mm_gdbus_object_get_modem_simple (object);
}

/**
 * mm_object_peek_modem: (skip)
 * @object: A #MMObject.
 *
 * Like mm_object_get_modem() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModem or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
MMModem *
mm_object_peek_modem (MMObject *object)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (object), NULL);

    return mm_gdbus_object_peek_modem (object);
}

/**
 * mm_object_peek_modem_3gpp: (skip)
 * @object: A #MMObject.
 *
 * Like mm_object_get_modem_3gpp() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModem3gpp or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
MMModem3gpp *
mm_object_peek_modem_3gpp (MMObject *object)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (object), NULL);

    return mm_gdbus_object_peek_modem3gpp (object);
}

/**
 * mm_object_peek_modem_simple: (skip)
 * @object: A #MMObject.
 *
 * Like mm_object_get_modem_simple() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #MMManager is running.</warning>
 *
 * Returns: (transfer none): A #MMModemSimple or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
MMModemSimple *
mm_object_peek_modem_simple (MMObject *object)
{
    g_return_val_if_fail (MM_GDBUS_IS_OBJECT (object), NULL);

    return mm_gdbus_object_peek_modem_simple (object);
}
