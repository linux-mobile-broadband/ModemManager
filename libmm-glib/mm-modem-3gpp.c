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
#include "mm-modem-3gpp.h"

/**
 * SECTION: mm-modem-3gpp
 * @title: MMModem3gpp
 * @short_description: The 3GPP interface
 *
 * The #MMModem3gpp is an object providing access to the methods, signals and
 * properties of the 3GPP interface.
 *
 * The 3GPP interface is exposed whenever a modem has any of the 3GPP
 * capabilities (%MM_MODEM_CAPABILITY_GSM_UMTS, %MM_MODEM_CAPABILITY_LTE or %MM_MODEM_CAPABILITY_LTE_ADVANCED).
 */

G_DEFINE_TYPE (MMModem3gpp, mm_modem_3gpp, MM_GDBUS_TYPE_MODEM3GPP_PROXY)

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_path:
 * @self: A #MMModem3gpp.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_3gpp_get_path (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_3gpp_dup_path:
 * @self: A #MMModem3gpp.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_3gpp_dup_path (MMModem3gpp *self)
{
    gchar *value;

    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_imei:
 * @self: A #MMModem3gpp.
 *
 * Gets the <ulink url="http://en.wikipedia.org/wiki/Imei">IMEI</ulink>,
 * as reported by this #MMModem3gpp.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_3gpp_dup_imei() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The IMEI, or %NULL if none available.
 */
const gchar *
mm_modem_3gpp_get_imei (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem3gpp_get_imei (MM_GDBUS_MODEM3GPP (self)));
}

/**
 * mm_modem_3gpp_dup_imei:
 * @self: A #MMModem3gpp.
 *
 * Gets a copy of the <ulink url="http://en.wikipedia.org/wiki/Imei">IMEI</ulink>,
 * as reported by this #MMModem3gpp.
 *
 * Returns: (transfer full): The IMEI, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_3gpp_dup_imei (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_dup_imei (MM_GDBUS_MODEM3GPP (self)));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_operator_code:
 * @self: A #MMModem3gpp.
 *
 * Gets the code of the operator to which the mobile is
 * currently registered.
 *
 * Returned in the format <literal>"MCCMNC"</literal>, where
 * <literal>MCC</literal> is the three-digit ITU E.212 Mobile Country Code
 * and <literal>MNC</literal> is the two- or three-digit GSM Mobile Network
 * Code. e.g. e<literal>"31026"</literal> or <literal>"310260"</literal>.
 *
 * If the <literal>MCC</literal> and <literal>MNC</literal> are not known
 * or the mobile is not registered to a mobile network, this property will
 * be a zero-length (blank) string.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_3gpp_dup_operator_code() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The operator code, or %NULL if none available.
 */
const gchar *
mm_modem_3gpp_get_operator_code (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem3gpp_get_operator_code (MM_GDBUS_MODEM3GPP (self)));
}

/**
 * mm_modem_3gpp_dup_operator_code:
 * @self: A #MMModem3gpp.
 *
 * Gets a copy of the code of the operator to which the mobile is
 * currently registered.
 *
 * Returned in the format <literal>"MCCMNC"</literal>, where
 * <literal>MCC</literal> is the three-digit ITU E.212 Mobile Country Code
 * and <literal>MNC</literal> is the two- or three-digit GSM Mobile Network
 * Code. e.g. e<literal>"31026"</literal> or <literal>"310260"</literal>.
 *
 * Returns: (transfer full): The operator code, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_3gpp_dup_operator_code (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_dup_operator_code (MM_GDBUS_MODEM3GPP (self)));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_operator_name:
 * @self: A #MMModem3gpp.
 *
 * Gets the name of the operator to which the mobile is
 * currently registered.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_3gpp_dup_operator_name() if on another
 * thread.</warning>
 *
 * Returns: (transfer none): The operator name, or %NULL if none available.
 */
const gchar *
mm_modem_3gpp_get_operator_name (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem3gpp_get_operator_name (MM_GDBUS_MODEM3GPP (self)));
}

/**
 * mm_modem_3gpp_dup_operator_name:
 * @self: A #MMModem3gpp.
 *
 * Gets a copy of the name of the operator to which the mobile is
 * currently registered.
 *
 * Returns: (transfer full): The operator name, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_3gpp_dup_operator_name (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_dup_operator_name (MM_GDBUS_MODEM3GPP (self)));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_registration_state:
 * @self: A #MMModem.
 *
 * Get the the mobile registration status as defined in 3GPP TS 27.007
 * section 10.1.19.
 *
 * Returns: A #MMModem3gppRegistrationState value, specifying the current registration state.
 */
MMModem3gppRegistrationState
mm_modem_3gpp_get_registration_state (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);

    return mm_gdbus_modem3gpp_get_registration_state (MM_GDBUS_MODEM3GPP (self));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_subscription_state:
 * @self: A #MMModem.
 *
 * Get the current subscription status of the account. This value is only
 * available after the modem attempts to register with the network.
 *
 * Returns: A #MMModem3gppSubscriptionState value, specifying the current subscription state.
 */
MMModem3gppSubscriptionState
mm_modem_3gpp_get_subscription_state (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN);

    return mm_gdbus_modem3gpp_get_subscription_state (MM_GDBUS_MODEM3GPP (self));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_enabled_facility_locks:
 * @self: A #MMModem3gpp.
 *
 * Get the list of facilities for which PIN locking is enabled.
 *
 * Returns: A bitmask of #MMModem3gppFacility flags, specifying which facilities have locks enabled.
 */
MMModem3gppFacility
mm_modem_3gpp_get_enabled_facility_locks (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), MM_MODEM_3GPP_FACILITY_NONE);

    return mm_gdbus_modem3gpp_get_enabled_facility_locks (MM_GDBUS_MODEM3GPP (self));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_register_finish:
 * @self: A #MMModem3gpp.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_3gpp_register().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_register().
 *
 * Returns: %TRUE if the modem was registered, %FALSE if @error is set.
 */
gboolean
mm_modem_3gpp_register_finish (MMModem3gpp *self,
                               GAsyncResult *res,
                               GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), FALSE);

    return mm_gdbus_modem3gpp_call_register_finish (MM_GDBUS_MODEM3GPP (self), res, error);
}

/**
 * mm_modem_3gpp_register:
 * @self: A #MMModem3gpp.
 * @network_id: The operator ID to register. An empty string can be used to register to the home network.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests registration with a given mobile network.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_3gpp_register_finish() to get the result of the operation.
 *
 * See mm_modem_3gpp_register_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_3gpp_register (MMModem3gpp *self,
                        const gchar *network_id,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_3GPP (self));

    mm_gdbus_modem3gpp_call_register (MM_GDBUS_MODEM3GPP (self), network_id, cancellable, callback, user_data);
}

/**
 * mm_modem_3gpp_register_sync:
 * @self: A #MMModem3gpp.
 * @network_id: The operator ID to register. An empty string can be used to register to the home network.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests registration with a given mobile network.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_3gpp_register()
 * for the asynchronous version of this method.
 *
 * Returns: %TRUE if the modem was registered, %FALSE if @error is set.
 */
gboolean
mm_modem_3gpp_register_sync (MMModem3gpp *self,
                             const gchar *network_id,
                             GCancellable *cancellable,
                             GError **error)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), FALSE);

    return mm_gdbus_modem3gpp_call_register_sync (MM_GDBUS_MODEM3GPP (self), network_id, cancellable, error);
}

/*****************************************************************************/

struct _MMModem3gppNetwork {
    MMModem3gppNetworkAvailability availability;
    gchar *operator_long;
    gchar *operator_short;
    gchar *operator_code;
    MMModemAccessTechnology access_technology;
};

/**
 * mm_modem_3gpp_network_free:
 * @network: A #MMModem3gppNetwork.
 *
 * Frees a #MMModem3gppNetwork.
 */
void
mm_modem_3gpp_network_free (MMModem3gppNetwork *network)
{
    if (!network)
        return;

    g_free (network->operator_long);
    g_free (network->operator_short);
    g_free (network->operator_code);
    g_slice_free (MMModem3gppNetwork, network);
}

/**
 * mm_modem_3gpp_network_get_availability:
 * @network: A #MMModem3gppNetwork.
 *
 * Get availability of the 3GPP network.
 *
 * Returns: A #MMModem3gppNetworkAvailability.
 */
MMModem3gppNetworkAvailability
mm_modem_3gpp_network_get_availability (const MMModem3gppNetwork *network)
{
    g_return_val_if_fail (network != NULL, MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN);

    return network->availability;
}

/**
 * mm_modem_3gpp_network_get_operator_long:
 * @network: A #MMModem3gppNetwork.
 *
 * Get the long operator name of the 3GPP network.
 *
 * Returns: (transfer none): The long operator name, or %NULL if none available.
 */
const gchar *
mm_modem_3gpp_network_get_operator_long (const MMModem3gppNetwork *network)
{
    g_return_val_if_fail (network != NULL, NULL);

    return network->operator_long;
}

/**
 * mm_modem_3gpp_network_get_operator_short:
 * @network: A #MMModem3gppNetwork.
 *
 * Get the short operator name of the 3GPP network.
 *
 * Returns: (transfer none): The long operator name, or %NULL if none available.
 */
const gchar *
mm_modem_3gpp_network_get_operator_short (const MMModem3gppNetwork *network)
{
    g_return_val_if_fail (network != NULL, NULL);

    return network->operator_short;
}

/**
 * mm_modem_3gpp_network_get_operator_code:
 * @network: A #MMModem3gppNetwork.
 *
 * Get the operator code (MCCMNC) of the 3GPP network.
 *
 * Returns: (transfer none): The operator code, or %NULL if none available.
 */
const gchar *
mm_modem_3gpp_network_get_operator_code (const MMModem3gppNetwork *network)
{
    g_return_val_if_fail (network != NULL, NULL);

    return network->operator_code;
}

/**
 * mm_modem_3gpp_network_get_access_technology:
 * @network: A #MMModem3gppNetwork.
 *
 * Get the technology used to access the 3GPP network.
 *
 * Returns: A #MMModemAccessTechnology.
 */
MMModemAccessTechnology
mm_modem_3gpp_network_get_access_technology (const MMModem3gppNetwork *network)
{
    g_return_val_if_fail (network != NULL, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    return network->access_technology;
}

/*****************************************************************************/

static GList *
create_networks_list (GVariant *variant)
{
    GList *list = NULL;
    GVariantIter dict_iter;
    GVariant *dict;

    /* Input is aa{sv} */
    g_variant_iter_init (&dict_iter, variant);
    while ((dict = g_variant_iter_next_value (&dict_iter))) {
        GVariantIter iter;
        gchar *key;
        GVariant *value;
        MMModem3gppNetwork *network;

        network = g_slice_new0 (MMModem3gppNetwork);

        g_variant_iter_init (&iter, dict);
        while (g_variant_iter_next (&iter, "{sv}", &key, &value)) {
            if (g_str_equal (key, "status")) {
                network->availability = (MMModem3gppNetworkAvailability)g_variant_get_uint32 (value);
            } else if (g_str_equal (key, "operator-long")) {
                g_warn_if_fail (network->operator_long == NULL);
                network->operator_long = g_variant_dup_string (value, NULL);
            } else if (g_str_equal (key, "operator-short")) {
                g_warn_if_fail (network->operator_short == NULL);
                network->operator_short = g_variant_dup_string (value, NULL);
            } else if (g_str_equal (key, "operator-code")) {
                g_warn_if_fail (network->operator_code == NULL);
                network->operator_code = g_variant_dup_string (value, NULL);
            }  else if (g_str_equal (key, "access-technology")) {
                network->access_technology = (MMModemAccessTechnology)g_variant_get_uint32 (value);
            } else
                g_warning ("Unexpected property '%s' found in Network info", key);

            g_free (key);
            g_variant_unref (value);
        }

        list = g_list_prepend (list, network);
        g_variant_unref (dict);
    }

    return list;
}

/**
 * mm_modem_3gpp_scan_finish:
 * @self: A #MMModem3gpp.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_3gpp_scan().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_scan().
 *
 * Returns: (transfer full) (element-type ModemManager.Modem3gppNetwork): a list of #MMModem3gppNetwork structs, or #NULL if @error is set. The returned value should be freed with g_list_free_full() using mm_modem_3gpp_network_free() as #GDestroyNotify function.
 */
GList *
mm_modem_3gpp_scan_finish (MMModem3gpp *self,
                           GAsyncResult *res,
                           GError **error)
{
    GVariant *result = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), FALSE);

    if (!mm_gdbus_modem3gpp_call_scan_finish (MM_GDBUS_MODEM3GPP (self), &result, res, error))
        return NULL;

    return create_networks_list (result);
}

/**
 * mm_modem_3gpp_scan:
 * @self: A #MMModem3gpp.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to scan available 3GPP networks.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_3gpp_scan_finish() to get the result of the operation.
 *
 * See mm_modem_3gpp_scan_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_3gpp_scan (MMModem3gpp *self,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    g_return_if_fail (MM_IS_MODEM_3GPP (self));

    mm_gdbus_modem3gpp_call_scan (MM_GDBUS_MODEM3GPP (self), cancellable, callback, user_data);
}

/**
 * mm_modem_3gpp_scan_sync:
 * @self: A #MMModem3gpp.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to scan available 3GPP networks.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_3gpp_scan()
 * for the asynchronous version of this method.
 *
 * Returns:  (transfer full) (element-type ModemManager.Modem3gppNetwork):  a list of #MMModem3gppNetwork structs, or #NULL if @error is set. The returned value should be freed with g_list_free_full() using mm_modem_3gpp_network_free() as #GDestroyNotify function.
 */
GList *
mm_modem_3gpp_scan_sync (MMModem3gpp *self,
                         GCancellable *cancellable,
                         GError **error)
{
    GVariant *result = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), FALSE);

    if (!mm_gdbus_modem3gpp_call_scan_sync (MM_GDBUS_MODEM3GPP (self), &result,cancellable, error))
        return NULL;

    return create_networks_list (result);
}

/*****************************************************************************/

static void
mm_modem_3gpp_init (MMModem3gpp *self)
{
}

static void
mm_modem_3gpp_class_init (MMModem3gppClass *modem_class)
{
}
