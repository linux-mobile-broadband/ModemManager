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
#include "mm-modem-3gpp-ussd.h"

/**
 * mm_modem_3gpp_ussd_get_path:
 * @self: A #MMModem3gppUssd.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_3gpp_ussd_get_path (MMModem3gppUssd *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_3gpp_ussd_dup_path:
 * @self: A #MMModem3gppUssd.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_3gpp_ussd_dup_path (MMModem3gppUssd *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/**
 * mm_modem_3gpp_ussd_get_network_request:
 * @self: A #MMModem3gppUssd.
 *
 * Gets any pending network-initiated request.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_3gpp_ussd_dup_network_request() if on another thread.</warning>
 *
 * Returns: (transfer none): The network request, or %NULL if none available.
 */
const gchar *
mm_modem_3gpp_ussd_get_network_request (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem3gpp_ussd_get_network_request (self));
}

/**
 * mm_modem_3gpp_ussd_dup_network_request:
 * @self: A #MMModem3gppUssd.
 *
 * Gets a copy of any pending network-initiated request.
 *
 * Returns: (transfer full): The network request, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_3gpp_ussd_dup_network_request (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_ussd_dup_network_request (self));
}

/**
 * mm_modem_3gpp_ussd_get_network_notification:
 * @self: A #MMModem3gppUssd.
 *
 * Gets any pending network-initiated request to which no USSD response is required.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_3gpp_ussd_dup_network_notification() if on another thread.</warning>
 *
 * Returns: (transfer none): The network notification, or %NULL if none available.
 */
const gchar *
mm_modem_3gpp_ussd_get_network_notification (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem3gpp_ussd_get_network_notification (self));
}

/**
 * mm_modem_3gpp_ussd_dup_network_notification:
 * @self: A #MMModem3gppUssd.
 *
 * Gets a copy of any pending network-initiated request to which no USSD response is required.
 *
 * Returns: (transfer full): The network notification, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_3gpp_ussd_dup_network_notification (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_ussd_dup_network_notification (self));
}

/**
 * mm_modem_3gpp_ussd_get_state:
 * @self: A #MMModem.
 *
 * Get the state of the ongoing USSD session, if any.
 * section 10.1.19.
 *
 * Returns: A #MMModem3gppUssdSessionState value, specifying the current state.
 */
MMModem3gppUssdSessionState
mm_modem_3gpp_ussd_get_state (MMModem3gppUssd *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), MM_MODEM_3GPP_USSD_SESSION_STATE_UNKNOWN);

    return mm_gdbus_modem3gpp_ussd_get_state (self);
}

void
mm_modem_3gpp_ussd_initiate (MMModem3gppUssd *self,
                             const gchar *command,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self));

    mm_gdbus_modem3gpp_ussd_call_initiate (self,
                                           command,
                                           cancellable,
                                           callback,
                                           user_data);
}

gchar *
mm_modem_3gpp_ussd_initiate_finish (MMModem3gppUssd *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    gchar *reply = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), NULL);

    mm_gdbus_modem3gpp_ussd_call_initiate_finish (self,
                                                  &reply,
                                                  res,
                                                  error);
    return reply;
}

gchar *
mm_modem_3gpp_ussd_initiate_sync (MMModem3gppUssd *self,
                                  const gchar *command,
                                  GCancellable *cancellable,
                                  GError **error)
{
    gchar *reply = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), NULL);

    mm_gdbus_modem3gpp_ussd_call_initiate_sync (self,
                                                command,
                                                &reply,
                                                cancellable,
                                                error);
    return reply;
}

void
mm_modem_3gpp_ussd_respond (MMModem3gppUssd *self,
                            const gchar *response,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self));

    mm_gdbus_modem3gpp_ussd_call_respond (self,
                                          response,
                                          cancellable,
                                          callback,
                                          user_data);
}

gchar *
mm_modem_3gpp_ussd_respond_finish (MMModem3gppUssd *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    gchar *reply = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), NULL);

    mm_gdbus_modem3gpp_ussd_call_respond_finish (self,
                                                 &reply,
                                                 res,
                                                 error);
    return reply;
}

gchar *
mm_modem_3gpp_ussd_respond_sync (MMModem3gppUssd *self,
                                 const gchar *response,
                                 GCancellable *cancellable,
                                 GError **error)
{
    gchar *reply = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), NULL);

    mm_gdbus_modem3gpp_ussd_call_respond_sync (self,
                                               response,
                                               &reply,
                                               cancellable,
                                               error);
    return reply;
}

void
mm_modem_3gpp_ussd_cancel (MMModem3gppUssd *self,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self));

    mm_gdbus_modem3gpp_ussd_call_cancel (self,
                                         cancellable,
                                         callback,
                                         user_data);
}

gboolean
mm_modem_3gpp_ussd_cancel_finish (MMModem3gppUssd *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), FALSE);

    return mm_gdbus_modem3gpp_ussd_call_cancel_finish (self,
                                                       res,
                                                       error);
}

gboolean
mm_modem_3gpp_ussd_cancel_sync (MMModem3gppUssd *self,
                                GCancellable *cancellable,
                                GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM3GPP_USSD (self), FALSE);

    return mm_gdbus_modem3gpp_ussd_call_cancel_sync (self,
                                                     cancellable,
                                                     error);
}
