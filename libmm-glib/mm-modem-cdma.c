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

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-modem-cdma.h"

/**
 * mm_modem_cdma_get_path:
 * @self: A #MMModemCdma.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_cdma_get_path (MMModemCdma *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_cdma_dup_path:
 * @self: A #MMModemCdma.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_cdma_dup_path (MMModemCdma *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/**
 * mm_modem_cdma_get_meid:
 * @self: A #MMModemCdma.
 *
 * Gets the <ulink url="http://en.wikipedia.org/wiki/MEID">Mobile Equipment Identifier</ulink>,
 * as reported by this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_cdma_dup_meid() if on another thread.</warning>
 *
 * Returns: (transfer none): The MEID, or %NULL if none available.
 */
const gchar *
mm_modem_cdma_get_meid (MMModemCdma *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_cdma_get_meid (self));
}

/**
 * mm_modem_cdma_dup_meid:
 * @self: A #MMModemCdma.
 *
 * Gets a copy of the <ulink url="http://en.wikipedia.org/wiki/MEID">Mobile Equipment Identifier</ulink>,
 * as reported by this #MMModem.
 *
 * Returns: (transfer full): The MEID, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_cdma_dup_meid (MMModemCdma *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_cdma_dup_meid (self));
}

/**
 * mm_modem_cdma_get_esn:
 * @self: A #MMModemCdma.
 *
 * Gets the <ulink url="http://en.wikipedia.org/wiki/Electronic_serial_number">Electronic Serial Number</ulink>,
 * as reported by this #MMModem.
 *
 * The ESN is superceded by MEID, but still used in older devices.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_cdma_dup_esn() if on another thread.</warning>
 *
 * Returns: (transfer none): The ESN, or %NULL if none available.
 */
const gchar *
mm_modem_cdma_get_esn (MMModemCdma *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_cdma_get_esn (self));
}

/**
 * mm_modem_cdma_dup_esn:
 * @self: A #MMModemCdma.
 *
 * Gets a copy of the <ulink url="http://en.wikipedia.org/wiki/Electronic_serial_number">Electronic Serial Number</ulink>,
 * as reported by this #MMModem.
 *
 * The ESN is superceded by MEID, but still used in older devices.
 *
 * Returns: (transfer full): The ESN, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_cdma_dup_esn (MMModemCdma *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_cdma_dup_esn (self));
}

/**
 * mm_modem_cdma_get_sid:
 * @self: A #MMModemCdma.
 *
 * Gets the <ulink url="http://en.wikipedia.org/wiki/System_Identification_Number">System Identifier</ulink>
 * of the serving CDMA 1x network, if known, and if the modem is registered with
 * a CDMA 1x network.
 *
 * Returns: The SID, or %MM_MODEM_CDMA_SID_UNKNOWN.
 */
guint
mm_modem_cdma_get_sid (MMModemCdma *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), MM_MODEM_CDMA_SID_UNKNOWN);

    return mm_gdbus_modem_cdma_get_sid (self);
}

/**
 * mm_modem_cdma_get_nid:
 * @self: A #MMModemCdma.
 *
 * Gets the <ulink url="http://en.wikipedia.org/wiki/Network_Identification_Number">Network Identifier</ulink>
 * of the serving CDMA 1x network, if known, and if the modem is registered with
 * a CDMA 1x network.
 *
 * Returns: The NID, or %MM_MODEM_CDMA_NID_UNKNOWN.
 */
guint
mm_modem_cdma_get_nid (MMModemCdma *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), MM_MODEM_CDMA_NID_UNKNOWN);

    return mm_gdbus_modem_cdma_get_nid (self);
}

/**
 * mm_modem_cdma_get_cdma1x_registration_state:
 * @self: A #MMModemCdma.
 *
 * Gets the state of the registration in the CDMA 1x network.
 *
 * Returns: a #MMModemCdmaRegistrationState.
 */
MMModemCdmaRegistrationState
mm_modem_cdma_get_cdma1x_registration_state (MMModemCdma *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

    return mm_gdbus_modem_cdma_get_cdma1x_registration_state (self);
}

/**
 * mm_modem_cdma_get_evdo_registration_state:
 * @self: A #MMModemCdma.
 *
 * Gets the state of the registration in the EV-DO network.
 *
 * Returns: a #MMModemCdmaRegistrationState.
 */
MMModemCdmaRegistrationState
mm_modem_cdma_get_evdo_registration_state (MMModemCdma *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

    return mm_gdbus_modem_cdma_get_evdo_registration_state (self);
}

void
mm_modem_cdma_activate (MMModemCdma *self,
                        const gchar *carrier,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM_CDMA (self));

    mm_gdbus_modem_cdma_call_activate (self,
                                       carrier,
                                       cancellable,
                                       callback,
                                       user_data);
}

gboolean
mm_modem_cdma_activate_finish (MMModemCdma *self,
                               GAsyncResult *res,
                               GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), FALSE);

    return mm_gdbus_modem_cdma_call_activate_finish (self,
                                                     res,
                                                     error);
}

gboolean
mm_modem_cdma_activate_sync (MMModemCdma *self,
                             const gchar *carrier,
                             GCancellable *cancellable,
                             GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_CDMA (self), FALSE);

    return mm_gdbus_modem_cdma_call_activate_sync (self,
                                                   carrier,
                                                   cancellable,
                                                   error);
}
