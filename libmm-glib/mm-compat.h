/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm-glib -- Access modem status & information from glib applications
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
 * Copyright (C) 2021 Aleksander Morgado <aleksander@aleksander.es>
 */

#ifndef _MM_COMPAT_H_
#define _MM_COMPAT_H_

#ifndef MM_DISABLE_DEPRECATED

#if !defined (__LIBMM_GLIB_H_INSIDE__) && !defined (LIBMM_GLIB_COMPILATION)
#error "Only <libmm-glib.h> can be included directly."
#endif

#include "mm-simple-connect-properties.h"
#include "mm-bearer-properties.h"
#include "mm-call-properties.h"
#include "mm-location-gps-nmea.h"
#include "mm-location-3gpp.h"
#include "mm-pco.h"
#include "mm-simple-status.h"
#include "mm-modem-3gpp.h"
#include "mm-modem-oma.h"
#include "mm-modem.h"

/**
 * SECTION:mm-compat
 * @short_description: Deprecated types and methods.
 *
 * These types and methods are flagged as deprecated and therefore
 * shouldn't be used in newly written code. They are provided to avoid
 * innecessary API/ABI breaks, for compatibility purposes only.
 */

/*****************************************************************************/

/**
 * mm_simple_connect_properties_set_number:
 * @self: a #MMSimpleConnectProperties.
 * @number: the number.
 *
 * Sets the number to use when performing the connection.
 *
 * Since: 1.0
 * Deprecated: 1.10.0. The number setting is not used anywhere, and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
G_DEPRECATED
void mm_simple_connect_properties_set_number (MMSimpleConnectProperties *self,
                                              const gchar               *number);

/**
 * mm_simple_connect_properties_get_number:
 * @self: a #MMSimpleConnectProperties.
 *
 * Gets the number to use when performing the connection.
 *
 * Returns: (transfer none): the number, or #NULL if not set. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.0
 * Deprecated: 1.10.0. The number setting is not used anywhere, and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
G_DEPRECATED
const gchar *mm_simple_connect_properties_get_number (MMSimpleConnectProperties *self);

/*****************************************************************************/

/**
 * mm_bearer_properties_set_number:
 * @self: a #MMBearerProperties.
 * @number: the number.
 *
 * Sets the number to use when performing the connection.
 *
 * Since: 1.0
 * Deprecated: 1.10.0. The number setting is not used anywhere, and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
G_DEPRECATED
void mm_bearer_properties_set_number (MMBearerProperties *self,
                                      const gchar        *number);

/**
 * mm_bearer_properties_get_number:
 * @self: a #MMBearerProperties.
 *
 * Gets the number to use when performing the connection.
 *
 * Returns: (transfer none): the number, or #NULL if not set. Do not free the
 * returned value, it is owned by @self.
 *
 * Since: 1.0
 * Deprecated: 1.10.0. The number setting is not used anywhere, and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
G_DEPRECATED
const gchar *mm_bearer_properties_get_number (MMBearerProperties *self);

/*****************************************************************************/

/**
 * mm_call_properties_set_direction:
 * @self: A #MMCallProperties.
 * @direction: the call direction
 *
 * Sets the call direction.
 *
 * Since: 1.6
 * Deprecated: 1.12: the user should not specify the direction of the call, as
 * it is implicit (outgoing always). Anyway, this parameter has always been
 * ignored during the new call creation processing.
 */
G_DEPRECATED
void mm_call_properties_set_direction (MMCallProperties *self,
                                       MMCallDirection   direction);

/**
 * mm_call_properties_set_state_reason:
 * @self: A #MMCallProperties.
 * @state_reason: the call state reason.
 *
 * Sets the call state reason.
 *
 * Since: 1.6
 * Deprecated: 1.12: the user should not specify the state reason of the call
 * before the call is created. This parameter has always been ignored during the
 * new call creation processing.
 */
G_DEPRECATED
void mm_call_properties_set_state_reason (MMCallProperties  *self,
                                          MMCallStateReason  state_reason);

/**
 * mm_call_properties_set_state:
 * @self: A #MMCallProperties.
 * @state: the call state
 *
 * Sets the call state
 *
 * Since: 1.6
 * Deprecated: 1.12: the user should not specify the state of the call before
 * the call is created. This parameter has always been ignored during the new
 * call creation processing.
 */
G_DEPRECATED
void mm_call_properties_set_state (MMCallProperties *self,
                                   MMCallState       state);

/**
 * mm_call_properties_get_direction:
 * @self: A #MMCallProperties.
 *
 * Gets the call direction.
 *
 * Returns: the call direction.
 *
 * Since: 1.6
 * Deprecated: 1.12: the user should not specify the direction of the call, as
 * it is implicit (outgoing always). This parameter has always been ignored
 * during the new call creation processing.
 */
G_DEPRECATED
MMCallDirection mm_call_properties_get_direction (MMCallProperties *self);

/**
 * mm_call_properties_get_state_reason:
 * @self: A #MMCallProperties.
 *
 * Gets the call state reason.
 *
 * Returns: the call state reason.
 *
 * Since: 1.6
 * Deprecated: 1.12: the user should not specify the state reason of the call
 * before the call is created. This parameter has always been ignored during the
 * new call creation processing.
 */
G_DEPRECATED
MMCallStateReason mm_call_properties_get_state_reason (MMCallProperties *self);

/**
 * mm_call_properties_get_state:
 * @self: A #MMCallProperties.
 *
 * Gets the call state.
 *
 * Returns: the call state.
 *
 * Since: 1.6
 * Deprecated: 1.12: the user should not specify the state of the call before
 * the call is created. This parameter has always been ignored during the new
 * call creation processing.
 */
G_DEPRECATED
MMCallState mm_call_properties_get_state (MMCallProperties *self);

/*****************************************************************************/
/**
 * mm_location_3gpp_get_mobile_network_code:
 * @self: a #MMLocation3gpp.
 *
 * Gets the Mobile Network Code of the 3GPP network.
 *
 * Note that 0 may actually be a valid MNC. In general, the MNC should be
 * considered valid just if the reported MCC is valid, as MCC should never
 * be 0.
 *
 * Returns: the MNC, or 0 if unknown.
 *
 * Since: 1.0
 * Deprecated: 1.18.0. This function can not separate between two-digit MNCs
 * and three-digit MNCs with a leading zero. Use mm_location_3gpp_get_operator_code()
 * instead.
 */
G_DEPRECATED
guint mm_location_3gpp_get_mobile_network_code (MMLocation3gpp *self);

/*****************************************************************************/

/**
 * mm_location_gps_nmea_build_full:
 * @self: a #MMLocationGpsNmea.
 *
 * Gets a compilation of all cached traces, in a single string.
 * Traces are separated by '\r\n'.
 *
 * Returns: (transfer full): a string containing all traces, or #NULL if none
 * available. The returned value should be freed with g_free().
 *
 * Since: 1.0
 * Deprecated: 1.14: user should use mm_location_gps_nmea_get_traces() instead,
 * which provides a much more generic interface to the full list of traces.
 */
G_DEPRECATED_FOR(mm_location_gps_nmea_get_traces)
gchar *mm_location_gps_nmea_build_full (MMLocationGpsNmea *self);

/*****************************************************************************/

/**
 * mm_pco_list_free:
 * @pco_list: (transfer full)(element-type ModemManager.Pco): a #GList of
 *  #MMPco.
 *
 * Frees all of the memory used by a #GList of #MMPco.
 *
 * Since: 1.10
 * Deprecated: 1.12.0: Use g_list_free_full() using g_object_unref() as
 * #GDestroyNotify function instead.
 */
G_DEPRECATED
void mm_pco_list_free (GList *pco_list);

/*****************************************************************************/

/**
 * mm_simple_status_get_3gpp_subscription_state:
 * @self: a #MMSimpleStatus.
 *
 * Gets the current subscription status of the account.
 *
 * Returns: a #MMModem3gppSubscriptionState.
 *
 * Since: 1.0
 * Deprecated: 1.12.0. The value of this property can only be obtained with
 * operator specific logic (e.g. processing specific PCO info), and therefore
 * it doesn't make sense to expose it in the ModemManager interface.
 */
MMModem3gppSubscriptionState mm_simple_status_get_3gpp_subscription_state (MMSimpleStatus *self);

/*****************************************************************************/

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
G_DEPRECATED
MMModem3gppSubscriptionState mm_modem_3gpp_get_subscription_state (MMModem3gpp *self);

/*****************************************************************************/

/**
 * mm_modem_get_pending_network_initiated_sessions:
 * @self: A #MMModem.
 * @sessions: (out) (array length=n_sessions): Return location for the array of
 *  #MMOmaPendingNetworkInitiatedSession structs. The returned array should be
 *  freed with g_free() when no longer needed.
 * @n_sessions: (out): Return location for the number of values in @sessions.
 *
 * Gets the list of pending network-initiated OMA sessions.
 *
 * Returns: %TRUE if @sessions and @n_sessions are set, %FALSE otherwise.
 *
 * Since: 1.2
 * Deprecated: 1.18: Use mm_modem_oma_get_pending_network_initiated_sessions() instead.
 */
G_DEPRECATED_FOR (mm_modem_oma_get_pending_network_initiated_sessions)
gboolean mm_modem_get_pending_network_initiated_sessions (MMModemOma                           *self,
                                                          MMOmaPendingNetworkInitiatedSession **sessions,
                                                          guint                                *n_sessions);

/**
 * mm_modem_peek_pending_network_initiated_sessions:
 * @self: A #MMModem.
 * @sessions: (out) (array length=n_sessions): Return location for the array of
 *  #MMOmaPendingNetworkInitiatedSession values. Do not free the returned array,
 *  it is owned by @self.
 * @n_sessions: (out): Return location for the number of values in @sessions.
 *
 * Gets the list of pending network-initiated OMA sessions.
 *
 * Returns: %TRUE if @sessions and @n_sessions are set, %FALSE otherwise.
 *
 * Since: 1.2
 * Deprecated: 1.18: Use mm_modem_oma_peek_pending_network_initiated_sessions() instead.
 */
G_DEPRECATED_FOR (mm_modem_oma_peek_pending_network_initiated_sessions)
gboolean mm_modem_peek_pending_network_initiated_sessions (MMModemOma                                 *self,
                                                           const MMOmaPendingNetworkInitiatedSession **sessions,
                                                           guint                                      *n_sessions);

/*****************************************************************************/

/**
 * mm_modem_get_max_bearers:
 * @self: a #MMModem.
 *
 * Gets the maximum number of defined packet data bearers this #MMModem
 * supports.
 *
 * This is not the number of active/connected bearers the modem supports,
 * but simply the number of bearers that may be defined at any given time.
 * For example, POTS and CDMA2000-only devices support only one bearer,
 * while GSM/UMTS devices typically support three or more, and any
 * LTE-capable device (whether LTE-only, GSM/UMTS-capable, and/or
 * CDMA2000-capable) also typically support three or more.
 *
 * Returns: the maximum number of defined packet data bearers.
 *
 * Since: 1.0
 * Deprecated: 1.18. There is no way to query the modem how many bearers
 * it supports, so the value exposed in this property in all the different
 * implementations is always equal to the one retrieved with
 * mm_modem_get_max_active_bearers(), so there is no point in using this
 * method.
 */
G_DEPRECATED
guint mm_modem_get_max_bearers (MMModem *self);

#endif /* MM_DISABLE_DEPRECATED */

#endif /* _MM_COMPAT_H_ */
