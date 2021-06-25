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
#include "mm-bearer.h"
#include "mm-pco.h"

/**
 * SECTION: mm-modem-3gpp
 * @title: MMModem3gpp
 * @short_description: The 3GPP interface
 *
 * The #MMModem3gpp is an object providing access to the methods, signals and
 * properties of the 3GPP interface.
 *
 * The 3GPP interface is exposed whenever a modem has any of the 3GPP
 * capabilities (%MM_MODEM_CAPABILITY_GSM_UMTS, %MM_MODEM_CAPABILITY_LTE
 * or %MM_MODEM_CAPABILITY_5GNR).
 */

G_DEFINE_TYPE (MMModem3gpp, mm_modem_3gpp, MM_GDBUS_TYPE_MODEM3GPP_PROXY)

struct _MMModem3gppPrivate {
    /* Properties */
    GMutex              initial_eps_bearer_settings_mutex;
    guint               initial_eps_bearer_settings_id;
    MMBearerProperties *initial_eps_bearer_settings;
};

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_path:
 * @self: A #MMModem3gpp.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 *
 * Since: 1.0
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
 * Gets a copy of the DBus path of the #MMObject object which implements this
 * interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value
 * should be freed with g_free().
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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
 * Returns: (transfer full): The IMEI, or %NULL if none available. The returned
 * value should be freed with g_free().
 *
 * Since: 1.0
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
 * Gets the code of the operator to which the mobile is currently registered.
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
 *
 * Since: 1.0
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
 * Gets a copy of the code of the operator to which the mobile is currently
 * registered.
 *
 * Returned in the format <literal>"MCCMNC"</literal>, where
 * <literal>MCC</literal> is the three-digit ITU E.212 Mobile Country Code
 * and <literal>MNC</literal> is the two- or three-digit GSM Mobile Network
 * Code. e.g. e<literal>"31026"</literal> or <literal>"310260"</literal>.
 *
 * Returns: (transfer full): The operator code, or %NULL if none available.
 * The returned value should be freed with g_free().
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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
 * Returns: (transfer full): The operator name, or %NULL if none available.
 * The returned value should be freed with g_free().
 *
 * Since: 1.0
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
 * Returns: A #MMModem3gppRegistrationState value, specifying the current
 * registration state.
 *
 * Since: 1.0
 */
MMModem3gppRegistrationState
mm_modem_3gpp_get_registration_state (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);

    return mm_gdbus_modem3gpp_get_registration_state (MM_GDBUS_MODEM3GPP (self));
}

/*****************************************************************************/

#ifndef MM_DISABLE_DEPRECATED

/**
 * mm_modem_3gpp_get_subscription_state:
 * @self: A #MMModem.
 *
 * Get the current subscription status of the account. This value is only
 * available after the modem attempts to register with the network.
 *
 * The value of this property can only be obtained with operator specific logic
 * (e.g. processing specific PCO info), and therefore it doesn't make sense to
 * expose it in the ModemManager interface.
 *
 * Returns: A #MMModem3gppSubscriptionState value, specifying the current
 * subscription state.
 *
 * Since: 1.0
 * Deprecated: 1.10.0. The value of this property can only be obtained with
 * operator specific logic (e.g. processing specific PCO info), and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
MMModem3gppSubscriptionState
mm_modem_3gpp_get_subscription_state (MMModem3gpp *self)
{
    return MM_MODEM_3GPP_SUBSCRIPTION_STATE_UNKNOWN;
}

#endif /* MM_DISABLE_DEPRECATED */

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_enabled_facility_locks:
 * @self: A #MMModem3gpp.
 *
 * Get the list of facilities for which PIN locking is enabled.
 *
 * Returns: A bitmask of #MMModem3gppFacility flags, specifying which facilities
 * have locks enabled.
 *
 * Since: 1.0
 */
MMModem3gppFacility
mm_modem_3gpp_get_enabled_facility_locks (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), MM_MODEM_3GPP_FACILITY_NONE);

    return mm_gdbus_modem3gpp_get_enabled_facility_locks (MM_GDBUS_MODEM3GPP (self));
}

/**
 * mm_modem_3gpp_get_eps_ue_mode_operation:
 * @self: A #MMModem3gpp.
 *
 * Get the UE mode of operation for EPS.
 *
 * Returns: A #MMModem3gppEpsUeModeOperation.
 *
 * Since: 1.8
 */
MMModem3gppEpsUeModeOperation
mm_modem_3gpp_get_eps_ue_mode_operation (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_UNKNOWN);

    return mm_gdbus_modem3gpp_get_eps_ue_mode_operation (MM_GDBUS_MODEM3GPP (self));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_pco:
 * @self: A #MMModem3gpp.
 *
 * Get the list of #MMPco received from the network.
 *
 * Returns: (transfer full) (element-type ModemManager.Pco): a list of #MMPco
 * objects, or #NULL if @error is set. The returned value should be freed with
 * g_list_free_full() using g_object_unref() as #GDestroyNotify function.
 *
 * Since: 1.10
 */
GList *
mm_modem_3gpp_get_pco (MMModem3gpp *self)
{
    GList *pco_list = NULL;
    GVariant *container, *child;
    GVariantIter iter;

    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    container = mm_gdbus_modem3gpp_get_pco (MM_GDBUS_MODEM3GPP (self));

    g_return_val_if_fail (g_variant_is_of_type (container, G_VARIANT_TYPE ("a(ubay)")),
                          NULL);
    g_variant_iter_init (&iter, container);
    while ((child = g_variant_iter_next_value (&iter))) {
        MMPco *pco;

        pco = mm_pco_from_variant (child, NULL);
        pco_list = mm_pco_list_add (pco_list, pco);
        g_object_unref (pco);
        g_variant_unref (child);
    }

    return pco_list;
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_initial_eps_bearer_path: (skip)
 * @self: A #MMModem3gpp.
 *
 * Gets the DBus path of the initial EPS #MMBearer exposed in this #MMModem3gpp.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_3gpp_dup_initial_eps_bearer_path() if on
 * another thread.</warning>
 *
 * Returns: (transfer none): The DBus path of the #MMBearer, or %NULL if none
 * available. Do not free the returned value, it belongs to @self.
 *
 * Since: 1.10
 */
const gchar *
mm_modem_3gpp_get_initial_eps_bearer_path (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (mm_gdbus_modem3gpp_get_initial_eps_bearer (MM_GDBUS_MODEM3GPP (self)));
}

/**
 * mm_modem_3gpp_dup_initial_eps_bearer_path:
 * @self: A #MMModem3gpp.
 *
 * Gets a copy of the DBus path of the initial EPS #MMBearer exposed in this
 * #MMModem3gpp.
 *
 * Returns: (transfer full): The DBus path of the #MMBearer, or %NULL if none
 * available. The returned value should be freed with g_free().
 *
 * Since: 1.10
 */
gchar *
mm_modem_3gpp_dup_initial_eps_bearer_path (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem3gpp_dup_initial_eps_bearer (MM_GDBUS_MODEM3GPP (self)));
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_register_finish:
 * @self: A #MMModem3gpp.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_register().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_register().
 *
 * Returns: %TRUE if the modem was registered, %FALSE if @error is set.
 *
 * Since: 1.0
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
 * @network_id: The operator ID to register. An empty string can be used to
 *  register to the home network.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests registration with a given mobile network.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_register_finish() to get the result of the operation.
 *
 * See mm_modem_3gpp_register_sync() for the synchronous, blocking version of
 * this method.
 *
 * Since: 1.0
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
 * @network_id: The operator ID to register. An empty string can be used to
 *  register to the home network.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests registration with a given mobile network.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_register() for the asynchronous version of this method.
 *
 * Returns: %TRUE if the modem was registered, %FALSE if @error is set.
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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

static MMModem3gppNetwork *
modem_3gpp_network_copy (MMModem3gppNetwork *network)
{
    MMModem3gppNetwork *network_copy;

    network_copy = g_slice_new0 (MMModem3gppNetwork);
    network_copy->availability      = network->availability;
    network_copy->operator_long     = g_strdup (network->operator_long);
    network_copy->operator_short    = g_strdup (network->operator_short);
    network_copy->operator_code     = g_strdup (network->operator_code);
    network_copy->access_technology = network->access_technology;

    return network_copy;
}

G_DEFINE_BOXED_TYPE (MMModem3gppNetwork, mm_modem_3gpp_network, (GBoxedCopyFunc)modem_3gpp_network_copy, (GBoxedFreeFunc)mm_modem_3gpp_network_free)

/**
 * mm_modem_3gpp_network_get_availability:
 * @network: A #MMModem3gppNetwork.
 *
 * Get availability of the 3GPP network.
 *
 * Returns: A #MMModem3gppNetworkAvailability.
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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
 *
 * Since: 1.0
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
 *
 * Since: 1.0
 */
MMModemAccessTechnology
mm_modem_3gpp_network_get_access_technology (const MMModem3gppNetwork *network)
{
    g_return_val_if_fail (network != NULL, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);

    return network->access_technology;
}

/*****************************************************************************/

static void
initial_eps_bearer_settings_updated (MMModem3gpp *self,
                                     GParamSpec  *pspec)
{
    g_mutex_lock (&self->priv->initial_eps_bearer_settings_mutex);
    {
        GVariant *dictionary;

        g_clear_object (&self->priv->initial_eps_bearer_settings);

        dictionary = mm_gdbus_modem3gpp_get_initial_eps_bearer_settings (MM_GDBUS_MODEM3GPP (self));
        if (dictionary) {
            GError *error = NULL;

            self->priv->initial_eps_bearer_settings = mm_bearer_properties_new_from_dictionary (dictionary, &error);
            if (error) {
                g_warning ("Invalid bearer properties received: %s", error->message);
                g_error_free (error);
            }
        }
    }
    g_mutex_unlock (&self->priv->initial_eps_bearer_settings_mutex);
}

static void
ensure_internal_initial_eps_bearer_settings (MMModem3gpp         *self,
                                             MMBearerProperties **dup)
{
    g_mutex_lock (&self->priv->initial_eps_bearer_settings_mutex);
    {
        /* If this is the first time ever asking for the object, setup the
         * update listener and the initial object, if any. */
        if (!self->priv->initial_eps_bearer_settings_id) {
            GVariant *dictionary;

            dictionary = mm_gdbus_modem3gpp_dup_initial_eps_bearer_settings (MM_GDBUS_MODEM3GPP (self));
            if (dictionary) {
                GError *error = NULL;

                self->priv->initial_eps_bearer_settings = mm_bearer_properties_new_from_dictionary (dictionary, &error);
                if (error) {
                    g_warning ("Invalid initial bearer properties: %s", error->message);
                    g_error_free (error);
                }
                g_variant_unref (dictionary);
            }

            /* No need to clear this signal connection when freeing self */
            self->priv->initial_eps_bearer_settings_id =
                g_signal_connect (self,
                                  "notify::initial-eps-bearer-settings",
                                  G_CALLBACK (initial_eps_bearer_settings_updated),
                                  NULL);
        }

        if (dup && self->priv->initial_eps_bearer_settings)
            *dup = g_object_ref (self->priv->initial_eps_bearer_settings);
    }
    g_mutex_unlock (&self->priv->initial_eps_bearer_settings_mutex);
}

/**
 * mm_modem_3gpp_get_initial_eps_bearer_settings:
 * @self: A #MMModem3gpp.
 *
 * Gets a #MMBearerProperties object specifying the settings configured in
 * the device to use when attaching to the LTE network.
 *
 * <warning>The values reported by @self are not updated when the values in the
 * interface change. Instead, the client is expected to call
 * mm_modem_3gpp_get_initial_eps_bearer_settings() again to get a new
 * #MMBearerProperties with the new values.</warning>
 *
 * Returns: (transfer full): A #MMBearerProperties that must be freed with
 * g_object_unref() or %NULL if unknown.
 *
 * Since: 1.10
 */
MMBearerProperties *
mm_modem_3gpp_get_initial_eps_bearer_settings (MMModem3gpp *self)
{
    MMBearerProperties *props = NULL;

    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    ensure_internal_initial_eps_bearer_settings (self, &props);
    return props;
}

/**
 * mm_modem_3gpp_peek_initial_eps_bearer_settings:
 * @self: A #MMModem3gpp.
 *
 * Gets a #MMBearerProperties object specifying the settings configured in
 * the device to use when attaching to the LTE network.
 *
 * <warning>The returned value is only valid until the property changes so
 * it is only safe to use this function on the thread where
 * @self was constructed. Use mm_modem_3gpp_get_initial_eps_bearer_settings()
 * if on another thread.</warning>
 *
 * Returns: (transfer none): A #MMBearerProperties. Do not free the returned
 * value, it belongs to @self.
 *
 * Since: 1.10
 */
MMBearerProperties *
mm_modem_3gpp_peek_initial_eps_bearer_settings (MMModem3gpp *self)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    ensure_internal_initial_eps_bearer_settings (self, NULL);
    return self->priv->initial_eps_bearer_settings;
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
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_scan().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_scan().
 *
 * Returns: (transfer full) (element-type ModemManager.Modem3gppNetwork): a list
 * of #MMModem3gppNetwork structs, or #NULL if @error is set. The returned value
 * should be freed with g_list_free_full() using mm_modem_3gpp_network_free() as
 * #GDestroyNotify function.
 *
 * Since: 1.0
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
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to scan available 3GPP networks.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_scan_finish() to get the result of the operation.
 *
 * See mm_modem_3gpp_scan_sync() for the synchronous, blocking version of this
 * method.
 *
 * Since: 1.0
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
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_scan() for the asynchronous version of this method.
 *
 * Returns: (transfer full) (element-type ModemManager.Modem3gppNetwork): a list
 * of #MMModem3gppNetwork structs, or #NULL if @error is set. The returned value
 * should be freed with g_list_free_full() using mm_modem_3gpp_network_free() as
 * #GDestroyNotify function.
 *
 * Since: 1.0
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

/**
 * mm_modem_3gpp_set_eps_ue_mode_operation_finish:
 * @self: A #MMModem3gpp.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_set_eps_ue_mode_operation().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_set_eps_ue_mode_operation().
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.8
 */
gboolean
mm_modem_3gpp_set_eps_ue_mode_operation_finish (MMModem3gpp   *self,
                                                GAsyncResult  *res,
                                                GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), FALSE);

    return mm_gdbus_modem3gpp_call_set_eps_ue_mode_operation_finish (MM_GDBUS_MODEM3GPP (self), res, error);
}

/**
 * mm_modem_3gpp_set_eps_ue_mode_operation:
 * @self: A #MMModem3gpp.
 * @mode: A #MMModem3gppEpsUeModeOperation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests to update the EPS UE mode of operation.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_set_eps_ue_mode_operation_finish() to get the result of the
 * operation.
 *
 * See mm_modem_3gpp_set_eps_ue_mode_operation_sync() for the synchronous,
 * blocking version of this method. The calling thread is blocked until a reply
 * is received.
 *
 * Since: 1.8
 */
void
mm_modem_3gpp_set_eps_ue_mode_operation (MMModem3gpp                    *self,
                                         MMModem3gppEpsUeModeOperation   mode,
                                         GCancellable                   *cancellable,
                                         GAsyncReadyCallback             callback,
                                         gpointer                        user_data)
{
    g_return_if_fail (MM_IS_MODEM_3GPP (self));
    g_return_if_fail (mode != MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_UNKNOWN);

    mm_gdbus_modem3gpp_call_set_eps_ue_mode_operation (MM_GDBUS_MODEM3GPP (self), (guint) mode, cancellable, callback, user_data);
}

/**
 * mm_modem_3gpp_set_eps_ue_mode_operation_sync:
 * @self: A #MMModem3gpp.
 * @mode: A #MMModem3gppEpsUeModeOperation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests to update the EPS UE mode of operation.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_modem_3gpp_set_eps_ue_mode_operation() for the asynchronous version
 * of this method.
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.8
 */
gboolean
mm_modem_3gpp_set_eps_ue_mode_operation_sync (MMModem3gpp                    *self,
                                              MMModem3gppEpsUeModeOperation   mode,
                                              GCancellable                   *cancellable,
                                              GError                        **error)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), FALSE);
    g_return_val_if_fail (mode != MM_MODEM_3GPP_EPS_UE_MODE_OPERATION_UNKNOWN, FALSE);

    return mm_gdbus_modem3gpp_call_set_eps_ue_mode_operation_sync (MM_GDBUS_MODEM3GPP (self), (guint) mode, cancellable, error);
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_get_initial_eps_bearer_finish:
 * @self: A #MMModem3gpp.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_get_initial_eps_bearer().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_3gpp_get_initial_eps_bearer().
 *
 * Returns: (transfer full): a #MMSim or #NULL if @error is set. The returned
 * value should be freed with g_object_unref().
 *
 * Since: 1.10
 */
MMBearer *
mm_modem_3gpp_get_initial_eps_bearer_finish (MMModem3gpp   *self,
                                             GAsyncResult  *res,
                                             GError       **error)
{
    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
modem_3gpp_get_initial_eps_bearer_ready (GDBusConnection *connection,
                                         GAsyncResult    *res,
                                         GTask           *task)
{
    GError *error = NULL;
    GObject *sim;
    GObject *source_object;

    source_object = g_async_result_get_source_object (res);
    sim = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, &error);
    g_object_unref (source_object);

    if (error)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, sim, g_object_unref);

    g_object_unref (task);
}

/**
 * mm_modem_3gpp_get_initial_eps_bearer:
 * @self: A #MMModem3gpp.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously gets the initial EPS #MMBearer object exposed by this
 * #MMModem3gpp.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_get_initial_eps_bearer_finish() to get the result of the
 * operation.
 *
 * See mm_modem_3gpp_get_initial_eps_bearer_sync() for the synchronous, blocking
 * version of this method.
 *
 * Since: 1.10
 */
void
mm_modem_3gpp_get_initial_eps_bearer (MMModem3gpp         *self,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    GTask       *task;
    const gchar *bearer_path;

    g_return_if_fail (MM_IS_MODEM_3GPP (self));

    task = g_task_new (self, cancellable, callback, user_data);

    bearer_path = mm_modem_3gpp_get_initial_eps_bearer_path (self);
    if (!bearer_path || g_str_equal (bearer_path, "/")) {
        g_task_return_new_error (task,
                                 MM_CORE_ERROR,
                                 MM_CORE_ERROR_NOT_FOUND,
                                 "No initial EPS bearer object available");
        g_object_unref (task);
        return;
    }

    g_async_initable_new_async (MM_TYPE_BEARER,
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                (GAsyncReadyCallback)modem_3gpp_get_initial_eps_bearer_ready,
                                task,
                                "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                "g-name",           MM_DBUS_SERVICE,
                                "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                                "g-object-path",    bearer_path,
                                "g-interface-name", "org.freedesktop.ModemManager1.Bearer",
                                NULL);
}

/**
 * mm_modem_3gpp_get_initial_eps_bearer_sync:
 * @self: A #MMModem3gpp.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets the initial EPS #MMBearer object exposed by this
 * #MMModem3gpp.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_get_initial_eps_bearer() for the asynchronous version of this
 * method.
 *
 * Returns: (transfer full): a #MMBearer or #NULL if @error is set. The returned
 * value should be freed with g_object_unref().
 *
 * Since: 1.10
 */
MMBearer *
mm_modem_3gpp_get_initial_eps_bearer_sync (MMModem3gpp   *self,
                                           GCancellable  *cancellable,
                                           GError       **error)
{
    GObject     *bearer;
    const gchar *bearer_path;

    g_return_val_if_fail (MM_IS_MODEM_3GPP (self), NULL);

    bearer_path = mm_modem_3gpp_get_initial_eps_bearer_path (self);
    if (!bearer_path || g_str_equal (bearer_path, "/")) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_NOT_FOUND,
                     "No initial EPS bearer object available");
        return NULL;
    }

    bearer = g_initable_new (MM_TYPE_BEARER,
                             cancellable,
                             error,
                             "g-flags",          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                             "g-name",           MM_DBUS_SERVICE,
                             "g-connection",     g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                             "g-object-path",    bearer_path,
                             "g-interface-name", "org.freedesktop.ModemManager1.Bearer",
                             NULL);

    return (bearer ? MM_BEARER (bearer) : NULL);
}

/*****************************************************************************/

/**
 * mm_modem_3gpp_set_initial_eps_bearer_settings_finish:
 * @self: A #MMModem3gpp.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *  mm_modem_3gpp_set_initial_eps_bearer_settings().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with
 * mm_modem_3gpp_set_initial_eps_bearer_settings().
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.10
 */
gboolean
mm_modem_3gpp_set_initial_eps_bearer_settings_finish (MMModem3gpp   *self,
                                                      GAsyncResult  *res,
                                                      GError       **error)
{
    return mm_gdbus_modem3gpp_call_set_initial_eps_bearer_settings_finish (MM_GDBUS_MODEM3GPP (self), res, error);
}

/**
 * mm_modem_3gpp_set_initial_eps_bearer_settings:
 * @self: A #MMModem3gpp.
 * @config: A #MMBearerProperties object with the properties to use.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or
 *  %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously configures the settings for the initial LTE default bearer.
 *
 * When the operation is finished, @callback will be invoked in the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread you are calling this method from. You can then call
 * mm_modem_3gpp_set_initial_eps_bearer_settings_finish() to get the result of
 * the operation.
 *
 * Since: 1.10
 */
void
mm_modem_3gpp_set_initial_eps_bearer_settings (MMModem3gpp         *self,
                                               MMBearerProperties  *config,
                                               GCancellable        *cancellable,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data)
{
    GVariant *dictionary;

    dictionary = mm_bearer_properties_get_dictionary (config);
    mm_gdbus_modem3gpp_call_set_initial_eps_bearer_settings (MM_GDBUS_MODEM3GPP (self),
                                                             dictionary,
                                                             cancellable,
                                                             callback,
                                                             user_data);
    g_variant_unref (dictionary);
}

/**
 * mm_modem_3gpp_set_initial_eps_bearer_settings_sync:
 * @self: A #MMModem3gpp.
 * @config: A #MMBearerProperties object with the properties to use.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously configures the settings for the initial LTE default bearer.
 *
 * The calling thread is blocked until a reply is received. See
 * mm_modem_3gpp_set_initial_eps_bearer_settings() for the asynchronous
 * version of this method.
 *
 * Returns: %TRUE if the operation was successful, %FALSE if @error is set.
 *
 * Since: 1.10
 */
gboolean
mm_modem_3gpp_set_initial_eps_bearer_settings_sync (MMModem3gpp         *self,
                                                    MMBearerProperties  *config,
                                                    GCancellable        *cancellable,
                                                    GError             **error)
{
    gboolean  result;
    GVariant *dictionary;

    dictionary = mm_bearer_properties_get_dictionary (config);
    result = mm_gdbus_modem3gpp_call_set_initial_eps_bearer_settings_sync (MM_GDBUS_MODEM3GPP (self),
                                                                           dictionary,
                                                                           cancellable,
                                                                           error);
    g_variant_unref (dictionary);
    return result;
}

/*****************************************************************************/

static void
mm_modem_3gpp_init (MMModem3gpp *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, MM_TYPE_MODEM_3GPP, MMModem3gppPrivate);
    g_mutex_init (&self->priv->initial_eps_bearer_settings_mutex);
}

static void
finalize (GObject *object)
{
    MMModem3gpp *self = MM_MODEM_3GPP (object);

    g_mutex_clear (&self->priv->initial_eps_bearer_settings_mutex);

    G_OBJECT_CLASS (mm_modem_3gpp_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
    MMModem3gpp *self = MM_MODEM_3GPP (object);

    g_clear_object (&self->priv->initial_eps_bearer_settings);

    G_OBJECT_CLASS (mm_modem_3gpp_parent_class)->dispose (object);
}

static void
mm_modem_3gpp_class_init (MMModem3gppClass *modem_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (modem_class);

    g_type_class_add_private (object_class, sizeof (MMModem3gppPrivate));

    /* Virtual methods */
    object_class->dispose  = dispose;
    object_class->finalize = finalize;
}
