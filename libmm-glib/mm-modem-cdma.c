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
 * Copyright (C) 2011 - 2012 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2012 Google, Inc.
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-errors-types.h"
#include "mm-modem-cdma.h"

/**
 * SECTION: mm-modem-cdma
 * @title: MMModemCdma
 * @short_description: The CDMA interface
 *
 * The #MMModemCdma is an object providing access to the methods, signals and
 * properties of the CDMA interface.
 *
 * The CDMA interface is exposed whenever a modem has CDMA capabilities
 * (%MM_MODEM_CAPABILITY_CDMA_EVDO).
 */

G_DEFINE_TYPE (MMModemCdma, mm_modem_cdma, MM_GDBUS_TYPE_MODEM_CDMA_PROXY)

/*****************************************************************************/

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
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), NULL);

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

    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_cdma_get_meid:
 * @self: A #MMModemCdma.
 *
 * Gets the <ulink url="http://en.wikipedia.org/wiki/MEID">Mobile Equipment Identifier</ulink>,
 * as reported by this #MMModemCdma.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_cdma_dup_meid() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The MEID, or %NULL if none available.
 */
const gchar *
mm_modem_cdma_get_meid (MMModemCdma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_cdma_get_meid (MM_GDBUS_MODEM_CDMA (self)));
}

/**
 * mm_modem_cdma_dup_meid:
 * @self: A #MMModemCdma.
 *
 * Gets a copy of the <ulink url="http://en.wikipedia.org/wiki/MEID">Mobile Equipment Identifier</ulink>,
 * as reported by this #MMModemCdma.
 *
 * Returns: (transfer full): The MEID, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_cdma_dup_meid (MMModemCdma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_cdma_dup_meid (MM_GDBUS_MODEM_CDMA (self)));
}

/*****************************************************************************/

/**
 * mm_modem_cdma_get_esn:
 * @self: A #MMModemCdma.
 *
 * Gets the <ulink url="http://en.wikipedia.org/wiki/Electronic_serial_number">Electronic Serial Number</ulink>,
 * as reported by this #MMModemCdma.
 *
 * The ESN is superceded by MEID, but still used in older devices.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_cdma_dup_esn() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The ESN, or %NULL if none available.
 */
const gchar *
mm_modem_cdma_get_esn (MMModemCdma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_cdma_get_esn (MM_GDBUS_MODEM_CDMA (self)));
}

/**
 * mm_modem_cdma_dup_esn:
 * @self: A #MMModemCdma.
 *
 * Gets a copy of the <ulink url="http://en.wikipedia.org/wiki/Electronic_serial_number">Electronic Serial Number</ulink>,
 * as reported by this #MMModemCdma.
 *
 * The ESN is superceded by MEID, but still used in older devices.
 *
 * Returns: (transfer full): The ESN, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_cdma_dup_esn (MMModemCdma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_cdma_dup_esn (MM_GDBUS_MODEM_CDMA (self)));
}

/*****************************************************************************/

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
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), MM_MODEM_CDMA_SID_UNKNOWN);

    return mm_gdbus_modem_cdma_get_sid (MM_GDBUS_MODEM_CDMA (self));
}

/*****************************************************************************/

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
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), MM_MODEM_CDMA_NID_UNKNOWN);

    return mm_gdbus_modem_cdma_get_nid (MM_GDBUS_MODEM_CDMA (self));
}

/*****************************************************************************/

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
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

    return mm_gdbus_modem_cdma_get_cdma1x_registration_state (MM_GDBUS_MODEM_CDMA (self));
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
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);

    return mm_gdbus_modem_cdma_get_evdo_registration_state (MM_GDBUS_MODEM_CDMA (self));
}

/*****************************************************************************/

/**
 * mm_modem_cdma_get_activation_state:
 * @self: A #MMModemCdma.
 *
 * Gets the state of the activation in the 3GPP2 network.
 *
 * Returns: a #MMModemCdmaActivationState.
 */
MMModemCdmaActivationState
mm_modem_cdma_get_activation_state (MMModemCdma *self)
{
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), MM_MODEM_CDMA_ACTIVATION_STATE_UNKNOWN);

    return mm_gdbus_modem_cdma_get_activation_state (MM_GDBUS_MODEM_CDMA (self));
}

/*****************************************************************************/

/**
 * mm_modem_cdma_activate_finish:
 * @self: A #MMModemCdma.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_cdma_activate().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_cdma_activate().
 *
 * Returns: %TRUE if the activation was successful, %FALSE if @error is set.
 */
gboolean
mm_modem_cdma_activate_finish (MMModemCdma *self,
                               GAsyncResult *res,
                               GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), FALSE);

    return mm_gdbus_modem_cdma_call_activate_finish (MM_GDBUS_MODEM_CDMA (self), res, error);
}

/**
 * mm_modem_cdma_activate:
 * @self: A #MMModemCdma.
 * @carrier: Name of the carrier.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to provision the modem for use with a given carrier
 * using the modem's OTA activation functionality, if any.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_cdma_activate_finish() to get the result of the operation.
 *
 * See mm_modem_cdma_activate_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_cdma_activate (MMModemCdma *self,
                        const gchar *carrier,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_CDMA (self));

    mm_gdbus_modem_cdma_call_activate (MM_GDBUS_MODEM_CDMA (self), carrier, cancellable, callback, user_data);
}

/**
 * mm_modem_cdma_activate_sync:
 * @self: A #MMModemCdma.
 * @carrier: Name of the carrier.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to provision the modem for use with a given carrier
 * using the modem's OTA activation functionality, if any.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_cdma_activate()
 * for the asynchronous version of this method.
 *
 * Returns: %TRUE if the activation was successful, %FALSE if @error is set.
 */
gboolean
mm_modem_cdma_activate_sync (MMModemCdma *self,
                             const gchar *carrier,
                             GCancellable *cancellable,
                             GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_CDMA (self), FALSE);

    return mm_gdbus_modem_cdma_call_activate_sync (MM_GDBUS_MODEM_CDMA (self), carrier, cancellable, error);
}

/*****************************************************************************/

static void
mm_modem_cdma_init (MMModemCdma *self)
{
}

static void
mm_modem_cdma_class_init (MMModemCdmaClass *modem_class)
{
}
